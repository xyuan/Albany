/********************************************************************\
*            Albany, Copyright (2010) Sandia Corporation             *
*                                                                    *
* Notice: This computer software was prepared by Sandia Corporation, *
* hereinafter the Contractor, under Contract DE-AC04-94AL85000 with  *
* the Department of Energy (DOE). All rights in the computer software*
* are reserved by DOE on behalf of the United States Government and  *
* the Contractor as provided in the Contract. You are authorized to  *
* use this computer software for Governmental purposes but it is not *
* to be released or distributed to the public. NEITHER THE GOVERNMENT*
* NOR THE CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR      *
* ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE. This notice    *
* including this sentence must appear on any copies of this software.*
*    Questions to Andy Salinger, agsalin@sandia.gov                  *
\********************************************************************/

#include <Teuchos_Array.hpp>
#include <Epetra_LocalMap.h>
#include "Albany_Utils.hpp"
#include "QCAD_SaddleValueResponseFunction.hpp"
#include <fstream>

//! Helper function prototypes
namespace QCAD 
{
  bool ptInPolygon(const std::vector<QCAD::mathVector>& polygon, const QCAD::mathVector& pt);
  bool ptInPolygon(const std::vector<QCAD::mathVector>& polygon, const double* pt);

  void gatherVector(std::vector<double>& v, std::vector<double>& gv,
		    const Epetra_Comm& comm);
  void getOrdering(const std::vector<double>& v, std::vector<int>& ordering);
  bool lessOp(std::pair<std::size_t, double> const& a,
	      std::pair<std::size_t, double> const& b);
  double averageOfVector(const std::vector<double>& v);
  double distance(const std::vector<double>* vCoords, int ind1, int ind2, std::size_t nDims);
}

QCAD::SaddleValueResponseFunction::
SaddleValueResponseFunction(
  const Teuchos::RCP<Albany::Application>& application,
  const Teuchos::RCP<Albany::AbstractProblem>& problem,
  const Teuchos::RCP<Albany::MeshSpecsStruct>&  ms,
  const Teuchos::RCP<Albany::StateManager>& stateMgr,
  Teuchos::ParameterList& params) : 
  Albany::FieldManagerScalarResponseFunction(application, problem, ms, stateMgr),
  numDims(problem->spatialDimension())
{
  TEUCHOS_TEST_FOR_EXCEPTION (numDims < 2 || numDims > 3, Teuchos::Exceptions::InvalidParameter, std::endl 
	      << "Saddle Point not implemented for " << numDims << " dimensions." << std::endl); 

  params.set("Response Function", Teuchos::rcp(this,false));

  Teuchos::Array<double> ar;

  imagePtSize   = params.get<double>("Image Point Size", 0.01);
  nImagePts     = params.get<int>("Number of Image Points", 10);
  maxTimeStep   = params.get<double>("Max Time Step", 1.0);
  minTimeStep   = params.get<double>("Min Time Step", 0.002);
  maxIterations = params.get<int>("Maximum Iterations", 100);
  backtraceAfterIters = params.get<int>("Backtrace After Iteration", 10000000);
  convergeTolerance   = params.get<double>("Convergence Tolerance", 1e-5);
  minSpringConstant   = params.get<double>("Min Spring Constant", 1.0);
  maxSpringConstant   = params.get<double>("Max Spring Constant", 1.0);
  outputFilename = params.get<std::string>("Output Filename", "");
  debugFilename  = params.get<std::string>("Debug Filename", "");
  nEvery         = params.get<int>("Output Interval", 0);
  bClimbing      = params.get<bool>("Climbing NEB", true);
  antiKinkFactor = params.get<double>("Anti-Kink Factor", 0.0);
  bAggregateWorksets = params.get<bool>("Aggregate Worksets", false);
  bAdaptivePointSize = params.get<bool>("Adaptive Image Point Size", false);
  minAdaptivePointWt = params.get<double>("Adaptive Min Point Weight", 5);
  maxAdaptivePointWt = params.get<double>("Adaptive Max Point Weight", 10);
  shortenBeginPc = params.get<double>("Percent to Shorten Begin", 0);
  shortenEndPc   = params.get<double>("Percent to Shorten End", 0);

  fieldCutoffFctr = params.get<double>("Levelset Field Cutoff Factor", 1.0);
  minPoolDepthFctr = params.get<double>("Levelset Minimum Pool Depth Factor", 1.0);
  distanceCutoffFctr = params.get<double>("Levelset Distance Cutoff Factor", 1.0);
  levelSetRadius = params.get<double>("Levelset Radius", 0);

  maxFinalPts     = params.get<int>("Maximum Number of Final Points", 0);
  finalPtSpacing  = params.get<double>("Final Point Spacing", 1);


  if(backtraceAfterIters < 0) backtraceAfterIters = 10000000;
  else if(backtraceAfterIters <= 1) backtraceAfterIters = 2; // can't backtrace until the second iteration

  bLockToPlane = false;
  if(params.isParameter("Lock to z-coord")) {
    bLockToPlane = true;
    lockedZ = params.get<double>("Lock to z-coord");
  }

  iSaddlePt = -1;        //clear "found" saddle point index
  returnFieldVal = -1.0; //init to nonzero is important - so doesn't "match" default init

  //Beginning target region
  if(params.isParameter("Begin Point")) {
    beginRegionType = "Point";
    ar = params.get<Teuchos::Array<double> >("Begin Point");
    TEUCHOS_TEST_FOR_EXCEPTION (ar.size() != (int)numDims, Teuchos::Exceptions::InvalidParameter, std::endl 
				<< "Begin Point does not have " << numDims << " elements" << std::endl); 
    beginPolygon.resize(1); beginPolygon[0].resize(numDims);
    for(std::size_t i=0; i<numDims; i++) beginPolygon[0][i] = ar[i];
  }
  else if(params.isParameter("Begin Element Block")) {
    beginRegionType = "Element Block";
    beginElementBlock = params.get<std::string>("Begin Element Block");
  }
  else if(params.isSublist("Begin Polygon")) {
    beginRegionType = "Polygon";

    Teuchos::ParameterList& polyList = params.sublist("Begin Polygon");
    int nPts = polyList.get<int>("Number of Points");
    beginPolygon.resize(nPts); 

    for(int i=0; i<nPts; i++) {
      beginPolygon[i].resize(numDims);
      ar = polyList.get<Teuchos::Array<double> >( Albany::strint("Point",i) );
      TEUCHOS_TEST_FOR_EXCEPTION (ar.size() != (int)numDims, Teuchos::Exceptions::InvalidParameter, std::endl 
				  << "Begin polygon point does not have " << numDims << " elements" << std::endl); 
      for(std::size_t k=0; k<numDims; k++) beginPolygon[i][k] = ar[k];
    }
  }
  else TEUCHOS_TEST_FOR_EXCEPTION (true, Teuchos::Exceptions::InvalidParameter, std::endl 
				  << "No beginning region specified for saddle pt" << std::endl); 

  

  //Ending target region
  if(params.isParameter("End Point")) {
    endRegionType = "Point";
    ar = params.get<Teuchos::Array<double> >("End Point");
    TEUCHOS_TEST_FOR_EXCEPTION (ar.size() != (int)numDims, Teuchos::Exceptions::InvalidParameter, std::endl 
				<< "End Point does not have " << numDims << " elements" << std::endl); 
    endPolygon.resize(1); endPolygon[0].resize(numDims);
    for(std::size_t i=0; i<numDims; i++) endPolygon[0][i] = ar[i];
  }
  else if(params.isParameter("End Element Block")) {
    endRegionType = "Element Block";
    endElementBlock = params.get<std::string>("End Element Block");
  }
  else if(params.isSublist("End Polygon")) {
    endRegionType = "Polygon";
    
    Teuchos::ParameterList& polyList = params.sublist("End Polygon");
    int nPts = polyList.get<int>("Number of Points");
    endPolygon.resize(nPts); 
    
    for(int i=0; i<nPts; i++) {
      endPolygon[i].resize(numDims);
      ar = polyList.get<Teuchos::Array<double> >( Albany::strint("Point",i) );
      TEUCHOS_TEST_FOR_EXCEPTION (ar.size() != (int)numDims, Teuchos::Exceptions::InvalidParameter, std::endl 
				  << "End polygon point does not have " << numDims << " elements" << std::endl); 
      for(std::size_t k=0; k<numDims; k++) endPolygon[i][k] = ar[k];
    }
  }
  else TEUCHOS_TEST_FOR_EXCEPTION (true, Teuchos::Exceptions::InvalidParameter, std::endl 
				  << "No ending region specified for saddle pt" << std::endl); 
  

  //Guess at the saddle point
  saddleGuessGiven = false;
  if(params.isParameter("Saddle Point Guess")) {
    saddleGuessGiven = true;
    ar = params.get<Teuchos::Array<double> >("Saddle Point Guess");
    TEUCHOS_TEST_FOR_EXCEPTION (ar.size() != (int)numDims, Teuchos::Exceptions::InvalidParameter, std::endl 
				<< "Saddle point guess does not have " << numDims << " elements" << std::endl); 
    saddlePointGuess.resize(numDims);
    for(std::size_t i=0; i<numDims; i++) saddlePointGuess[i] = ar[i];
  }

  debugMode = params.get<int>("Debug Mode",0);

  imagePts.resize(nImagePts);
  imagePtValues.resize(nImagePts);
  imagePtWeights.resize(nImagePts);
  imagePtGradComps.resize(nImagePts*numDims);

  // Add allowed z-range if in 3D (lateral volume assumed)
  //  - rest (xmin, etc) computed dynamically
  if(numDims > 2) {
    zmin = params.get<double>("z min");
    zmax = params.get<double>("z max");
  }  

  this->setup(params);
  this->num_responses = 5;
}

QCAD::SaddleValueResponseFunction::
~SaddleValueResponseFunction()
{
}

unsigned int
QCAD::SaddleValueResponseFunction::
numResponses() const 
{
  return this->num_responses;  // returnFieldValue, fieldValue, saddleX, saddleY, saddleZ
}

void
QCAD::SaddleValueResponseFunction::
evaluateResponse(const double current_time,
		     const Epetra_Vector* xdot,
		     const Epetra_Vector& x,
		     const Teuchos::Array<ParamVec>& p,
		     Epetra_Vector& g)
{
  const Epetra_Comm& comm = x.Map().Comm();

  int dbMode = (comm.MyPID() == 0) ? debugMode : 0;
  if(comm.MyPID() != 0) outputFilename = ""; //Only root process outputs to files
  if(comm.MyPID() != 0) debugFilename = ""; //Only root process outputs to files
  
  TEUCHOS_TEST_FOR_EXCEPTION (nImagePts < 2, Teuchos::Exceptions::InvalidParameter, std::endl 
	      << "Saddle Point needs more than 2 image pts (" << nImagePts << " given)" << std::endl); 

  // Find saddle point in stages:
 
  //  1) Initialize image points
  initializeImagePoints(current_time, xdot, x, p, g, dbMode);
  
  if(maxIterations > 0) {
    //  2) Perform Nudged Elastic Band (NEB) algorithm on image points (iterative)
    doNudgedElasticBand(current_time, xdot, x, p, g, dbMode);
  }
  else {
    // If no NEB iteractions, choose center image point as saddle point
    int nFirstLeg = (nImagePts+1)/2, iCenter = nFirstLeg-1;
    iSaddlePt = iCenter; //don't need to check for positive weight at this point
  }

  //  3) Perform level-set method in a radius around saddle image point
  doLevelSet(current_time, xdot, x, p, g, dbMode);

  //  4) Get data at "final points" which can be more dense than neb image points, if desired
  if(maxIterations > 0 && maxFinalPts > 0) {
    initializeFinalImagePoints(current_time, xdot, x, p, g, dbMode);
    getFinalImagePointValues(current_time, xdot, x, p, g, dbMode);

    // append "final point" data to output
    if( outputFilename.length() > 0) {
      std::fstream out; double pathLength = 0.0;
      out.open(outputFilename.c_str(), std::fstream::out | std::fstream::app);
      out << std::endl << std::endl << "# Final points" << std::endl;
      for(std::size_t i=0; i<finalPts.size(); i++) {
	out << i << " " << finalPts[i].coords[0] << " " << finalPts[i].coords[1] 
	    << " " << finalPts[i].value << " " << pathLength << " " << finalPts[i].radius << std::endl;
	std::cout << "DEBUG printing pt " << i << " of " << finalPts.size() << std::endl;
	if(i < (finalPts.size()-1)) pathLength += finalPts[i].coords.distanceTo(finalPts[i+1].coords);
      }
      out.close();
    }
  }

  //  5) Fill response (g-vector) with values near the highest image point
  fillSaddlePointData(current_time, xdot, x, p, g, dbMode);

  return;
}


void
QCAD::SaddleValueResponseFunction::
initializeImagePoints(const double current_time,
		     const Epetra_Vector* xdot,
		     const Epetra_Vector& x,
		     const Teuchos::Array<ParamVec>& p,
		     Epetra_Vector& g, int dbMode)
{
  // 1) Determine initial and final points depending on region type
  //     - Point: take point given directly
  //     - Element Block: take minimum point within the specified element block (and allowed z-range)
  //     - Polygon: take minimum point within specified 2D polygon and allowed z-range
  
  const Epetra_Comm& comm = x.Map().Comm();
  if(dbMode > 1) std::cout << "Saddle Point:  Beginning end point location" << std::endl;

    // Initialize intial/final points
  imagePts[0].init(numDims, imagePtSize);
  imagePts[nImagePts-1].init(numDims, imagePtSize);

  mode = "Point location";
  Albany::FieldManagerScalarResponseFunction::evaluateResponse(
	current_time, xdot, x, p, g);
  if(dbMode > 2) std::cout << "Saddle Point:   -- done evaluation" << std::endl;

  if(beginRegionType == "Point") {
    imagePts[0].coords = beginPolygon[0];
  }
  else { 

    //MPI: get global min for begin point
    double globalMin; int procToBcast, winner;
    comm.MinAll( &imagePts[0].value, &globalMin, 1);
    if( fabs(imagePts[0].value - globalMin) < 1e-8 ) 
      procToBcast = comm.MyPID();
    else procToBcast = -1;

    comm.MaxAll( &procToBcast, &winner, 1 );
    comm.Broadcast( imagePts[0].coords.data(), numDims, winner); //broadcast winner's min position to others
    imagePts[0].value = globalMin;                               //no need to broadcast winner's value
  }

  if(endRegionType   == "Point") {
    imagePts[nImagePts-1].coords = endPolygon[0];
  }
  else { 

    //MPI: get global min for end point
    double globalMin; int procToBcast, winner;
    comm.MinAll( &imagePts[nImagePts-1].value, &globalMin, 1);
    if( fabs(imagePts[nImagePts-1].value - globalMin) < 1e-8 ) 
      procToBcast = comm.MyPID();
    else procToBcast = -1;

    comm.MaxAll( &procToBcast, &winner, 1 );
    comm.Broadcast( imagePts[nImagePts-1].coords.data(), numDims, winner); //broadcast winner's min position to others
    imagePts[nImagePts-1].value = globalMin;                               //no need to broadcast winner's value
  }

  //! Shorten beginning and end of path if requested (used to move begin/end point off of a contact region in QCAD)
  if(shortenBeginPc > 1e-6) {
     if(saddleGuessGiven)
       imagePts[0].coords = imagePts[0].coords + (saddlePointGuess - imagePts[0].coords) * (shortenBeginPc/100.0);
     else
       imagePts[0].coords = imagePts[0].coords + (imagePts[nImagePts-1].coords - imagePts[0].coords) * (shortenBeginPc/100.0);
  }
  if(shortenEndPc > 1e-6) {
     if(saddleGuessGiven)
       imagePts[nImagePts-1].coords = imagePts[nImagePts-1].coords + (saddlePointGuess - imagePts[nImagePts-1].coords) * (shortenEndPc/100.0);
     else
       imagePts[nImagePts-1].coords = imagePts[nImagePts-1].coords + (imagePts[0].coords - imagePts[nImagePts-1].coords) * (shortenEndPc/100.0);
  }

  if(dbMode > 2) std::cout << "Saddle Point:   -- done begin/end point initialization" << std::endl;

  //! Initialize Image Points:  
  //   interpolate between initial and final points (and possibly guess point) 
  //   to get all the image points
  const mathVector& initialPt = imagePts[0].coords;
  const mathVector& finalPt   = imagePts[nImagePts-1].coords;

  // Lock z-coordinate of initial and final points (and therefore of the rest of the points) if requested
  if(bLockToPlane && numDims > 2)
    imagePts[0].coords[2] = imagePts[nImagePts-1].coords[2] = lockedZ;

  if(saddleGuessGiven) {

    // two line segements (legs) initialPt -> guess, guess -> finalPt
    int nFirstLeg = (nImagePts+1)/2, nSecondLeg = nImagePts - nFirstLeg + 1; // +1 because both legs include middle pt
    for(int i=1; i<nFirstLeg-1; i++) {
      double s = i * 1.0/(nFirstLeg-1);
      imagePts[i].init(initialPt + (saddlePointGuess - initialPt) * s, imagePtSize);
    }
    for(int i=0; i<nSecondLeg-1; i++) {
      double s = i * 1.0/(nSecondLeg-1);
      imagePts[i+nFirstLeg-1].init(saddlePointGuess + (finalPt - saddlePointGuess) * s, imagePtSize);
    }
  }
  else {

    // one line segment initialPt -> finalPt
    for(std::size_t i=1; i<nImagePts-1; i++) {
      double s = i * 1.0/(nImagePts-1);   // nIntervals = nImagePts-1
      imagePts[i].init(initialPt + (finalPt - initialPt) * s, imagePtSize);
    }     
  }
 
  // Print initial point locations to stdout if requested
  if(dbMode > 1) {
    for(std::size_t i=0; i<nImagePts; i++)
      std::cout << "Saddle Point:   -- imagePt[" << i << "] = " << imagePts[i].coords << std::endl;
  }

  // If we aggregate workset data then call evaluator once more to accumulate 
  //  field and coordinate data into vFieldValues and vCoords members.
  if(bAggregateWorksets) {
    vFieldValues.clear();
    vCoords.clear();
    vGrads.clear();

    mode = "Accumulate all field data";
    Albany::FieldManagerScalarResponseFunction::evaluateResponse(
				    current_time, xdot, x, p, g);
    //No MPI here - each proc only holds all of it's worksets -- not other procs worksets
  }


  return;
}

void
QCAD::SaddleValueResponseFunction::
initializeFinalImagePoints(const double current_time,
		     const Epetra_Vector* xdot,
		     const Epetra_Vector& x,
		     const Teuchos::Array<ParamVec>& p,
		     Epetra_Vector& g, int dbMode)
{
  // Determine the locations of the "final" image points, which interpolate between the image points used
  //    in the nudged elastic band algorithm, and are used only as a means of getting more dense output data (more points along saddle path)
  
  if(dbMode > 1) std::cout << "Saddle Point:  Initializing Final Image Points" << std::endl;

  double ptSpacing = finalPtSpacing; // space between final image points
  int maxPoints    = maxFinalPts;    // maximum number of total final image points
  
  double* segmentLength = new double[nImagePts-1]; // segmentLength[i] == distance between imagePt[i] and imagePt[i+1]
  double lengthBefore = 0.0, lengthAfter = 0.0;    // path length before and after saddle point
  int nPtsBefore = 0, nPtsAfter = 0, nFinalPts;

  // Get the distances along each leg of the current (final) saddle path
  for(std::size_t i=0; i<nImagePts-1; i++) {
    segmentLength[i] = imagePts[i].coords.distanceTo(imagePts[i+1].coords);
    if( (int)i < iSaddlePt ) lengthBefore += segmentLength[i];
    else lengthAfter += segmentLength[i];
  }

  // We'd like to put equal number of final points on each side of the saddle point.  Compute here how 
  //  many final points (fixed spacing) will lie on each side of the saddle point.
  if(maxPoints * ptSpacing < lengthBefore + lengthAfter) {
    if( maxPoints * ptSpacing / 2 > lengthBefore)
      nPtsBefore = int(lengthBefore / ptSpacing);
    else if( maxPoints * ptSpacing / 2 > lengthAfter)
      nPtsBefore = maxPoints - int(lengthAfter / ptSpacing);
    else nPtsBefore = maxPoints / 2;

    nPtsAfter = maxPoints - nPtsBefore;
  }
  else {
    nPtsBefore = int(lengthBefore / ptSpacing);
    nPtsAfter  = int(lengthAfter  / ptSpacing);
  }

  nFinalPts = nPtsBefore + nPtsAfter;
  finalPts.resize(nFinalPts);
  finalPtValues.resize(nFinalPts);
  finalPtWeights.resize(nFinalPts);

  //! Initialize Final Image Points:  
  //   interpolate between current image points

  double offset = 0.0;
  int iCurFinalPt = nPtsBefore;
  for(int i=iSaddlePt-1; i >= 0; i--) {
    const mathVector& initialPt = imagePts[i+1].coords;
    const mathVector& v = (imagePts[i].coords - imagePts[i+1].coords) * (1.0/segmentLength[i]);  // normalized vector from initial -> final pt

    int nPts = int((segmentLength[i]-offset) / ptSpacing);
    double leftover = (segmentLength[i]-offset) - ptSpacing * nPts;

    for(int j=0; j<nPts && iCurFinalPt >= 0; j++) {
      finalPts[iCurFinalPt].init(initialPt + v * (ptSpacing * j + offset), (imagePts[i].radius + imagePts[i+1].radius)/2 );
      iCurFinalPt--;
    }
    offset = ptSpacing - leftover; //how much to advance the first point of the next segment
  }

  offset = ptSpacing;  //start initial point *after* saddle point this time
  iCurFinalPt = nPtsBefore+1;
  for(std::size_t i=iSaddlePt; i < nImagePts-1; i++) {
    const mathVector& initialPt = imagePts[i].coords;
    const mathVector& v = (imagePts[i+1].coords - imagePts[i].coords) * (1.0/segmentLength[i]);  // normalized vector from initial -> final pt

    int nPts = int((segmentLength[i]-offset) / ptSpacing);
    double leftover = (segmentLength[i]-offset) - ptSpacing * nPts;

    for(int j=0; j<nPts && iCurFinalPt < nFinalPts; j++) {
      finalPts[iCurFinalPt].init(initialPt + v * (ptSpacing * j + offset), (imagePts[i].radius + imagePts[i+1].radius)/2 );
      iCurFinalPt++;
    }
    offset = ptSpacing - leftover; //how much to advance the first point of the next segment
  }

  //If there are any leftover points, initialize them too
  for(int j=iCurFinalPt; j<nFinalPts; j++) 
    finalPts[j].init(imagePts[nImagePts-1].coords, imagePts[nImagePts-1].radius);

  return;
}


void
QCAD::SaddleValueResponseFunction::
doNudgedElasticBand(const double current_time,
		    const Epetra_Vector* xdot,
		    const Epetra_Vector& x,
		    const Teuchos::Array<ParamVec>& p,
		    Epetra_Vector& g, int dbMode)
{
  //  2) Perform Nudged Elastic Band Algorithm to find saddle point.
  //      Iterate over field manager fills of each image point's value and gradient
  //       then update image point positions user Verlet algorithm

  std::size_t nIters, nInitialIterations;
  double dp, s;
  double gradScale, springScale, springBase;
  double avgForce=0, avgOpposingForce=0;
  double dt = maxTimeStep;
  double dt2 = dt*dt;
  double acceptedHighestPtGradNorm = -1.0, highestPtGradNorm;
  int iHighestPt, nConsecLowForceDiff=0;  

  mathVector tangent(numDims);  
  std::vector<mathVector> force(nImagePts), lastForce(nImagePts), lastPositions(nImagePts), lastVelocities(nImagePts);
  std::vector<double> springConstants(nImagePts-1, minSpringConstant);

  //initialize force variables and last positions
  for(std::size_t i=0; i<nImagePts; i++) {
    force[i].resize(numDims); force[i].fill(0.0);
    lastForce[i] = force[i];
    lastPositions[i] = imagePts[i].coords;
    lastVelocities[i] = imagePts[i].velocity;
  }

  //get distance between initial and final points
  double max_dCoords = imagePts[0].coords.distanceTo( imagePts[nImagePts-1].coords ) / nImagePts;

  nIters = 0;
  nInitialIterations = 20; // TODO: make into parameter?
  
  //Storage for aggrecated image point data (needed for MPI)
  double*  globalPtValues   = new double [nImagePts];
  double*  globalPtWeights  = new double [nImagePts];
  double*  globalPtGrads    = new double [nImagePts*numDims];

  //Write headers to output files
  std::fstream fDebug;

  if( outputFilename.length() > 0) {
    std::fstream out;
    out.open(outputFilename.c_str(), std::fstream::out);
    out << "# Saddle point path" << std::endl;
    out << "# index xCoord yCoord value pathLength pointRadius" << std::endl;
    out.close();
  }
  if(debugFilename.length() > 0) {
    fDebug.open(debugFilename.c_str(), std::fstream::out);
    fDebug << "# HighestValue  HighestIndex  AverageForce  TimeStep"
	   << "  HighestPtGradNorm  AverageOpposingForce  SpringBase" << std::endl;
  }



  // Begin NEB iteration loop
  while( ++nIters <= maxIterations) {

    if(dbMode > 1) std::cout << "Saddle Point:  NEB Algorithm iteration " << nIters << " -----------------------" << std::endl;
    writeOutput(nIters);


    if(nIters > 1) {
      //Update coordinates and velocity using (modified) Verlet integration. Reset
      // the velocity to zero if it is directed opposite to force (reduces overshoot)
      for(std::size_t i=1; i<nImagePts-1; i++) {
	dp = imagePts[i].velocity.dot(force[i]);
	if(dp < 0) imagePts[i].velocity.fill(0.0);

	//save last position & velocity in case the new position brings 
	//  us outside the mesh or we need to backtrace
	lastPositions[i] = imagePts[i].coords; 
	lastVelocities[i] = imagePts[i].velocity; //Note: will be zero if force opposed velocity (above)

	// ** Update **
	mathVector dCoords = lastVelocities[i] * dt + force[i] * dt2 * 0.5;
	imagePts[i].coords = lastPositions[i] + dCoords;
	imagePts[i].velocity = lastVelocities[i] + force[i] * dt;
      }
    }

    getImagePointValues(current_time, xdot, x, p, g, 
			globalPtValues, globalPtWeights, globalPtGrads,
			lastPositions, dbMode);
    iHighestPt = getHighestPtIndex();
    highestPtGradNorm = imagePts[iHighestPt].grad.norm();

    // Setup scaling factors on first iteration
    if(nIters == 1) initialIterationSetup(gradScale, springScale, dbMode);

    // If in "backtrace" mode, require grad norm to decrease (or take minimum timestep)
    if(nIters > backtraceAfterIters) {
      while(dt > minTimeStep && highestPtGradNorm > acceptedHighestPtGradNorm) {

	//reduce dt
	dt = (dt/2 < minTimeStep) ? minTimeStep : dt/2;
	dt /= 2; dt2=dt*dt; 
	
	if(dbMode > 2) std::cout << "Saddle Point:  ** Backtrace dt => " << dt << std::endl;

	//Update coordinates and velocity using (modified) Verlet integration. Reset
	// the velocity to zero if it is directed opposite to force (reduces overshoot)
	for(std::size_t i=1; i<nImagePts-1; i++) {
	  mathVector dCoords = lastVelocities[i] * dt + force[i] * dt2 * 0.5;
	  imagePts[i].coords = lastPositions[i] + dCoords;
	  imagePts[i].velocity = lastVelocities[i] + force[i] * dt;
	}
	
	getImagePointValues(current_time, xdot, x, p, g, 
			    globalPtValues, globalPtWeights, globalPtGrads,
			    lastPositions, dbMode);
	iHighestPt = getHighestPtIndex();
	highestPtGradNorm = imagePts[iHighestPt].grad.norm();
      }

      if(dt == minTimeStep && highestPtGradNorm > acceptedHighestPtGradNorm && dbMode > 2)
	std::cout << "Saddle Point:  ** Warning: backtrace hit min dt == " << dt << std::endl;
    }	
    acceptedHighestPtGradNorm = highestPtGradNorm;

    // Compute spring base constant for this iteration
    s = ((double)nIters-1.0)/maxIterations;    
    springBase = springScale * ( (1.0-s)*minSpringConstant + s*maxSpringConstant ); 
    for(std::size_t i=0; i<nImagePts-1; i++) springConstants[i] = springBase;
	  
    avgForce = avgOpposingForce = 0.0;

    // Compute force acting on each image point
    for(std::size_t i=1; i<nImagePts-1; i++) {
      if(dbMode > 2) std::cout << std::endl << "Saddle Point:  >> Updating pt[" << i << "]:" << imagePts[i];

      // compute the tangent vector for the ith image point
      computeTangent(i, tangent, dbMode);

      // compute the force vector for the ith image point
      if((int)i == iHighestPt && bClimbing && nIters > nInitialIterations)
	computeClimbingForce(i, tangent, gradScale, force[i], dbMode);
      else
	computeForce(i, tangent, springConstants, gradScale, springScale,
		     force[i], dt, dt2, dbMode);

      // update avgForce and avgOpposingForce
      avgForce += force[i].norm();
      dp = force[i].dot(lastForce[i]) / (force[i].norm() * lastForce[i].norm()); 
      if( dp < 0 ) {  //if current force and last force point in "opposite" directions
	mathVector v = force[i] - lastForce[i];
	avgOpposingForce += v.norm() / (force[i].norm() + lastForce[i].norm());
	//avgOpposingForce += dp;  //an alternate implementation
      } 
    } // end of loop over image points 

    avgForce /= (nImagePts-2);
    avgOpposingForce /= (nImagePts-2);


    //print debug output
    if(dbMode > 1) 
      std::cout << "Saddle Point:  ** Summary:"
		<< "  highest val[" << iHighestPt << "] = " << imagePts[iHighestPt].value
		<< "  AverageForce = " << avgForce << "  dt = " << dt 
		<< "  gradNorm = " << imagePts[iHighestPt].grad.norm() 
		<< "  AvgOpposingForce = " << avgOpposingForce 
		<< "  SpringBase = " << springBase << std::endl;
    if(debugFilename.length() > 0)
      fDebug << imagePts[iHighestPt].value << "  " << iHighestPt << "  "
	     << avgForce << "  "  << dt << "  " << imagePts[iHighestPt].grad.norm() << "  "
	     << avgOpposingForce << "  " << springBase << std::endl;


    // Check for convergence in gradient norm
    if(imagePts[iHighestPt].grad.norm() < convergeTolerance) {
      if(dbMode > 2) std::cout << "Saddle Point:  ** Converged (grad norm " << 
	       imagePts[iHighestPt].grad.norm() << " < " << convergeTolerance << ")" << std::endl;
      break; // exit iterations loop
    }
    else if(nIters == maxIterations) break; //max iterations reached -- exit iterations loop now
                                            // (important so coords & radii don't get updated)

    // Save last force for next iteration
    for(std::size_t i=1; i<nImagePts-1; i++) lastForce[i] = force[i];
    
    // If all forces have remained in the same direction, tally this.  If this happens too many times
    //  increase dt, as this is a sign the time step is too small.
    if(avgOpposingForce < 1e-6) nConsecLowForceDiff++; else nConsecLowForceDiff = 0;
    if(nConsecLowForceDiff >= 3 && dt < maxTimeStep) { 
      dt *= 2; dt2=dt*dt; nConsecLowForceDiff = 0;
      if(dbMode > 2) std::cout << "Saddle Point:  ** Consecutive low dForce => dt = " << dt << std::endl;
    }

    //Shouldn't be necessary since grad_z == 0, but just to be sure all points 
    //  are locked to their given (initial) z-coordinate
    if(bLockToPlane && numDims > 2) {
      for(std::size_t i=1; i<nImagePts-1; i++) force[i][2] = 0.0;
    }

    //Reduce dt if movement of any point exceeds initial average distance btwn pts
    bool reduce_dt = true;
    while(reduce_dt && dt/2 > minTimeStep) {
      reduce_dt = false;
      for(std::size_t i=1; i<nImagePts-1; i++) {
	if(imagePts[i].velocity.norm() * dt > max_dCoords) { reduce_dt = true; break; }
	if(0.5 * force[i].norm() * dt2 > max_dCoords) { reduce_dt = true; break; }
      }
      if(reduce_dt) { 
	dt /= 2; dt2=dt*dt;
	if(dbMode > 2) std::cout << "Saddle Point:  ** Warning: dCoords too large: dt => " << dt << std::endl;
      }
    }
	  
    // adjust image point size based on weight (if requested)
    //  --> try to get weight between MIN/MAX target weights by varying image pt size
    if(bAdaptivePointSize) {
      for(std::size_t i=0; i<nImagePts; i++) {
	if(imagePts[i].weight < minAdaptivePointWt) imagePts[i].radius *= 2;
	else if(imagePts[i].weight > maxAdaptivePointWt) imagePts[i].radius /= 2;
      }
    }

  }  // end of NEB iteration loops

  //deallocate storage used for MPI communication
  delete [] globalPtValues; 
  delete [] globalPtWeights;
  delete [] globalPtGrads;  

  // Check if converged: nIters < maxIters ?
  if(dbMode) {
    if(nIters <= maxIterations) 
      std::cout << "Saddle Point:  NEB Converged after " << nIters << " iterations" << std::endl;
    else
      std::cout << "Saddle Point:  NEB Giving up after " << maxIterations << " iterations" << std::endl;

    for(std::size_t i=0; i<nImagePts; i++) {
      std::cout << "Saddle Point:  --   Final pt[" << i << "] = " << imagePts[i].value 
		<< " : " << imagePts[i].coords << "  (wt = " << imagePts[i].weight << " )" 
		<< "  (r= " << imagePts[i].radius << " )" << std::endl;
    }
  }

  // Choose image point with highest value (and positive weight) as saddle point
  std::size_t imax = 0;
  for(std::size_t i=0; i<nImagePts; i++) {
    if(imagePts[i].weight > 0) { imax = i; break; }
  }
  for(std::size_t i=imax+1; i<nImagePts; i++) {
    if(imagePts[i].value > imagePts[imax].value && imagePts[i].weight > 0) imax = i;
  }
  iSaddlePt = imax;

  if(debugFilename.length() > 0) fDebug.close();
  return;
}

void
QCAD::SaddleValueResponseFunction::
fillSaddlePointData(const double current_time,
		    const Epetra_Vector* xdot,
		    const Epetra_Vector& x,
		    const Teuchos::Array<ParamVec>& p,
		    Epetra_Vector& g, int dbMode)
{
  if(dbMode > 1) std::cout << "Saddle Point:  Begin filling saddle point data" << std::endl;

  if(bAggregateWorksets) {  //in aggregate workset mode, there are currently no x-y cutoffs imposed on points in getImagePointValues
    xmin = -1e10; xmax = 1e10; ymin = -1e10; ymax = 1e10;
  }

  mode = "Fill saddle point";
  Albany::FieldManagerScalarResponseFunction::evaluateResponse(
				   current_time, xdot, x, p, g);
  if(dbMode > 1) std::cout << "Saddle Point:  Done filling saddle point data" << std::endl;

  //Note: MPI: saddle weight is already summed in evaluator's 
  //   postEvaluate, so no need to do anything here

  returnFieldVal = g[0];
  imagePts[iSaddlePt].value = g[1];
  
  // Overwrite response indices 2+ with saddle point coordinates
  for(std::size_t i=0; i<numDims; i++) g[2+i] = imagePts[iSaddlePt].coords[i]; 

  if(dbMode) {
    std::cout << "Saddle Point:  Return Field value = " << g[0] << std::endl;
    std::cout << "Saddle Point:         Field value = " << g[1] << std::endl;
    for(std::size_t i=0; i<numDims; i++)
      std::cout << "Saddle Point:         Coord[" << i << "] = " << g[2+i] << std::endl;
  }

  if( outputFilename.length() > 0) {
    std::fstream out; double pathLength = 0.0;
    out.open(outputFilename.c_str(), std::fstream::out | std::fstream::app);
    out << std::endl << std::endl << "# Image points" << std::endl;
    for(std::size_t i=0; i<nImagePts; i++) {
      out << i << " " << imagePts[i].coords[0] << " " << imagePts[i].coords[1]
	  << " " << imagePts[i].value << " " << pathLength << " " << imagePts[i].radius << std::endl;
      if(i<nImagePts-1) pathLength += imagePts[i].coords.distanceTo(imagePts[i+1].coords);
    }    
    out.close();
  }

  return;
}



void
QCAD::SaddleValueResponseFunction::
doLevelSet(const double current_time,
	   const Epetra_Vector* xdot,
	   const Epetra_Vector& x,
	   const Teuchos::Array<ParamVec>& p,
	   Epetra_Vector& g, int dbMode)
{
  int result;
  const Epetra_Comm& comm = x.Map().Comm();

  if( fabs(levelSetRadius) < 1e-9 ) return; //don't run if level-set radius is zero

  vlsFieldValues.clear(); vlsCellAreas.clear();
  for(std::size_t k=0; k<numDims; k++) vlsCoords[k].clear();

  mode = "Level set data collection";
  Albany::FieldManagerScalarResponseFunction::evaluateResponse(
				   current_time, xdot, x, p, g);

  //! Gather data from different processors
  std::vector<double> allFieldVals;
  std::vector<double> allCellAreas;
  std::vector<double> allCoords[MAX_DIMENSIONS];

  QCAD::gatherVector(vlsFieldValues, allFieldVals, comm);  
  QCAD::gatherVector(vlsCellAreas, allCellAreas, comm);
  for(std::size_t k=0; k<numDims; k++)
    QCAD::gatherVector(vlsCoords[k], allCoords[k], comm);

  //! Exit early if there are no field values in the specified region
  if( allFieldVals.size()  == 0 ) return;

  //! Print gathered size on proc 0
  if(dbMode) {
    std::cout << std::endl << "--- Begin Saddle Level Set Algorithm ---" << std::endl;
    std::cout << "--- Saddle Level Set: local size (this proc) = " << vlsFieldValues.size()
	      << ", gathered size (all procs) = " << allFieldVals.size() << std::endl;
  }

  //! Sort data by field value
  std::vector<int> ordering;
  QCAD::getOrdering(allFieldVals, ordering);


  //! Compute max/min field values
  double maxFieldVal = allFieldVals[0], minFieldVal = allFieldVals[0];
  double maxCoords[3], minCoords[3];

  for(std::size_t k=0; k<numDims && k < 3; k++)
    maxCoords[k] = minCoords[k] = allCoords[k][0];

  std::size_t N = allFieldVals.size();
  for(std::size_t i=0; i<N; i++) {
    for(std::size_t k=0; k<numDims && k < 3; k++) {
      if(allCoords[k][i] > maxCoords[k]) maxCoords[k] = allCoords[k][i];
      if(allCoords[k][i] < minCoords[k]) minCoords[k] = allCoords[k][i];
    }
    if(allFieldVals[i] > maxFieldVal) maxFieldVal = allFieldVals[i];
    if(allFieldVals[i] < minFieldVal) minFieldVal = allFieldVals[i];
  }
  
  double avgCellLength = pow(QCAD::averageOfVector(allCellAreas), 0.5); //assume 2D areas
  double maxFieldDifference = fabs(maxFieldVal - minFieldVal);
  double currentSaddleValue = imagePts[iSaddlePt].value;
  

  if(dbMode > 1) {
    std::cout << "--- Saddle Level Set: max field difference = " << maxFieldDifference
	      << ", avg cell length = " << avgCellLength << std::endl;
  }

  //! Set cutoffs
  double cutoffDistance, cutoffFieldVal, minDepth;
  cutoffDistance = avgCellLength * distanceCutoffFctr;
  cutoffFieldVal = maxFieldDifference * fieldCutoffFctr;
  minDepth = minPoolDepthFctr * (currentSaddleValue - minFieldVal) / 2.0; //maxFieldDifference * minPoolDepthFctr;

  result = FindSaddlePoint_LevelSet(allFieldVals, allCoords, ordering,
			   cutoffDistance, cutoffFieldVal, minDepth, dbMode, g);
  // result == 0 ==> success: found 2 "deep" pools & saddle pt
  if(result == 0) { 
    //update imagePts[iSaddlePt] to be newly found saddle value
    for(std::size_t i=0; i<numDims; i++) imagePts[iSaddlePt].coords[i] = g[2+i];
    imagePts[iSaddlePt].value = g[1];
    imagePts[iSaddlePt].radius = 1e-5; //very small so only pick up point of interest?
    //set weight?
  }

  return;
}

//! Level-set Algorithm for finding saddle point
int QCAD::SaddleValueResponseFunction::
FindSaddlePoint_LevelSet(std::vector<double>& allFieldVals,
		std::vector<double>* allCoords, std::vector<int>& ordering,
		double cutoffDistance, double cutoffFieldVal, double minDepth, int dbMode,
		Epetra_Vector& g)
{
  if(dbMode) {
    std::cout << "--- Saddle Level Set: distance cutoff = " << cutoffDistance
	      << ", field cutoff = " << cutoffFieldVal 
	      << ", min depth = " << minDepth << std::endl;
  }

  // Walk through sorted data.  At current point, walk backward in list 
  //  until either 1) a "close" point is found, as given by tolerance -> join to tree
  //            or 2) the change in field value exceeds some maximium -> new tree
  std::size_t N = allFieldVals.size();
  std::vector<int> treeIDs(N, -1);
  std::vector<double> minFieldVals; //for each tree
  std::vector<int> treeSizes; //for each tree
  int nextAvailableTreeID = 0;

  int nTrees = 0, nMaxTrees = 0;
  int nDeepTrees=0, lastDeepTrees=0, treeIDtoReplace;
  int I, J, K;
  for(std::size_t i=0; i < N; i++) {
    I = ordering[i];

    if(dbMode > 1) {
      nDeepTrees = 0;
      for(std::size_t t=0; t < treeSizes.size(); t++) {
	if(treeSizes[t] > 0 && (allFieldVals[I]-minFieldVals[t]) > minDepth) nDeepTrees++;
      }
    }

    if(dbMode > 3) std::cout << "DEBUG: i=" << i << "( I = " << I << "), val="
			 << allFieldVals[I] << ", loc=(" << allCoords[0][I] 
			 << "," << allCoords[1][I] << ")" << " nD=" << nDeepTrees;

    if(dbMode > 1 && lastDeepTrees != nDeepTrees) {
      std::cout << "--- Saddle: i=" << i << " new deep pool: nPools=" << nTrees 
		<< " nDeep=" << nDeepTrees << std::endl;
      lastDeepTrees = nDeepTrees;
    }

    for(int j=i-1; j >= 0 && fabs(allFieldVals[I] - allFieldVals[ordering[j]]) < cutoffFieldVal; j--) {
      J = ordering[j];

      if( QCAD::distance(allCoords, I, J, numDims) < cutoffDistance ) {
	if(treeIDs[I] == -1) {
	  treeIDs[I] = treeIDs[J];
	  treeSizes[treeIDs[I]]++;

	  if(dbMode > 3) std::cout << " --> tree " << treeIDs[J] 
			       << " ( size=" << treeSizes[treeIDs[J]] << ", depth=" 
			       << (allFieldVals[I]-minFieldVals[treeIDs[J]]) << ")" << std::endl;
	}
	else if(treeIDs[I] != treeIDs[J]) {

	  //update number of deep trees
	  nDeepTrees = 0;
	  for(std::size_t t=0; t < treeSizes.size(); t++) {
	    if(treeSizes[t] > 0 && (allFieldVals[I]-minFieldVals[t]) > minDepth) nDeepTrees++;
	  }

	  bool mergingTwoDeepTrees = false;
	  if((allFieldVals[I]-minFieldVals[treeIDs[I]]) > minDepth && 
	     (allFieldVals[I]-minFieldVals[treeIDs[J]]) > minDepth) {
	    mergingTwoDeepTrees = true;
	    nDeepTrees--;
	  }

	  treeIDtoReplace = treeIDs[I];
	  if( minFieldVals[treeIDtoReplace] < minFieldVals[treeIDs[J]] )
	    minFieldVals[treeIDs[J]] = minFieldVals[treeIDtoReplace];

	  for(int k=i; k >=0; k--) {
	    K = ordering[k];
	    if(treeIDs[K] == treeIDtoReplace) {
	      treeIDs[K] = treeIDs[J];
	      treeSizes[treeIDs[J]]++;
	    }
	  }
	  treeSizes[treeIDtoReplace] = 0;
	  nTrees -= 1;

	  if(dbMode > 3) std::cout << "DEBUG:   also --> " << treeIDs[J] 
			       << " [merged] size=" << treeSizes[treeIDs[J]]
			       << " (treecount after merge = " << nTrees << ")" << std::endl;

	  if(dbMode > 1) std::cout << "--- Saddle: i=" << i << "merge: nPools=" << nTrees 
				   << " nDeep=" << nDeepTrees << std::endl;


	  if(mergingTwoDeepTrees && nDeepTrees == 1) {
	    if(dbMode > 3) std::cout << "DEBUG: FOUND SADDLE! exiting." << std::endl;
	    if(dbMode > 1) std::cout << "--- Saddle: i=" << i << " Found saddle at ";

            //Found saddle at I
	    g[0] = 0; //TODO - change this g[.] interface to something more readable -- and we don't use g[0] now
            g[1] = allFieldVals[I];
            for(std::size_t k=0; k<numDims && k < 3; k++) {
              g[2+k] = allCoords[k][I];
              if(dbMode > 1) std::cout << allCoords[k][I] << ", ";
	    }
            
	    if(dbMode > 1) std::cout << "ret=" << g[0] << std::endl;
            return 0; //success
	  }

	}
      }

    } //end j loop
    
    if(treeIDs[I] == -1) {
      if(dbMode > 3) std::cout << " --> new tree with ID " << nextAvailableTreeID
			       << " (treecount after new = " << (nTrees+1) << ")" << std::endl;
      if(dbMode > 1) std::cout << "--- Saddle: i=" << i << " new pool: nPools=" << (nTrees+1) 
			       << " nDeep=" << nDeepTrees << std::endl;

      treeIDs[I] = nextAvailableTreeID++;
      minFieldVals.push_back(allFieldVals[I]);
      treeSizes.push_back(1);

      nTrees += 1;
      if(nTrees > nMaxTrees) nMaxTrees = nTrees;
    }

  } // end i loop

  // if no saddle found, return all zeros
  if(dbMode > 3) std::cout << "DEBUG: NO SADDLE. exiting." << std::endl;
  for(std::size_t k=0; k<5; k++) g[k] = 0;

  // if two or more trees where found, then reason for failure is that not
  //  enough deep pools were found - so could try to reduce minDepth and re-run.
  if(nMaxTrees >= 2) return 1;

  // nMaxTrees < 2 - so we need more trees.  Could try to increase cutoffDistance and/or cutoffFieldVal.
  return 2;
}


void
QCAD::SaddleValueResponseFunction::
evaluateTangent(const double alpha, 
		const double beta,
		const double current_time,
		bool sum_derivs,
		const Epetra_Vector* xdot,
		const Epetra_Vector& x,
		const Teuchos::Array<ParamVec>& p,
		ParamVec* deriv_p,
		const Epetra_MultiVector* Vxdot,
		const Epetra_MultiVector* Vx,
		const Epetra_MultiVector* Vp,
		Epetra_Vector* g,
		Epetra_MultiVector* gx,
		Epetra_MultiVector* gp)
{

  // Require that g be computed to get tangent info 
  if(g != NULL) {
    
    // HACK: for now do not evaluate response when tangent is requested,
    //   as it is assumed that evaluateResponse has already been called
    //   directly or by evaluateGradient.  This prevents repeated calling 
    //   of evaluateResponse within the dg/dp loop of Albany::ModelEvaluator's
    //   evalModel(...) function.  matchesCurrentResults(...) would be able to 
    //   determine if evaluateResponse needs to be run, but 
    //   Albany::AggregateScalarReponseFunction does not copy from global g 
    //   to local g so the g parameter passed to this function will always 
    //   be zeros when used in an aggregate response fn.  Change this?

    // Evaluate response g and run algorithm (if it hasn't run already)
    //if(!matchesCurrentResults(*g)) 
    //  evaluateResponse(current_time, xdot, x, p, *g);

    mode = "Fill saddle point";
    Albany::FieldManagerScalarResponseFunction::evaluateTangent(
                alpha, beta, current_time, sum_derivs, xdot,
  	        x, p, deriv_p, Vxdot, Vx, Vp, g, gx, gp);
  }
  else {
    if (gx != NULL) gx->PutScalar(0.0);
    if (gp != NULL) gp->PutScalar(0.0);
  }
}

void
QCAD::SaddleValueResponseFunction::
evaluateGradient(const double current_time,
		 const Epetra_Vector* xdot,
		 const Epetra_Vector& x,
		 const Teuchos::Array<ParamVec>& p,
		 ParamVec* deriv_p,
		 Epetra_Vector* g,
		 Epetra_MultiVector* dg_dx,
		 Epetra_MultiVector* dg_dxdot,
		 Epetra_MultiVector* dg_dp)
{

  // Require that g be computed to get gradient info 
  if(g != NULL) {

    // Evaluate response g and run algorithm (if it hasn't run already)
    if(!matchesCurrentResults(*g)) 
      evaluateResponse(current_time, xdot, x, p, *g);

    mode = "Fill saddle point";
    Albany::FieldManagerScalarResponseFunction::evaluateGradient(
	   current_time, xdot, x, p, deriv_p, g, dg_dx, dg_dxdot, dg_dp);
  }
  else {
    if (dg_dx != NULL)    dg_dx->PutScalar(0.0);
    if (dg_dxdot != NULL) dg_dxdot->PutScalar(0.0);
    if (dg_dp != NULL)    dg_dp->PutScalar(0.0);
  }
}

void 
QCAD::SaddleValueResponseFunction::
postProcessResponses(const Epetra_Comm& comm, const Teuchos::RCP<Epetra_Vector>& g)
{
}

void 
QCAD::SaddleValueResponseFunction::
postProcessResponseDerivatives(const Epetra_Comm& comm, const Teuchos::RCP<Epetra_MultiVector>& gt)
{
}


void
QCAD::SaddleValueResponseFunction::
getImagePointValues(const double current_time,
		    const Epetra_Vector* xdot,
		    const Epetra_Vector& x,
		    const Teuchos::Array<ParamVec>& p,
		    Epetra_Vector& g, 
		    double* globalPtValues,
		    double* globalPtWeights,
		    double* globalPtGrads,
		    std::vector<QCAD::mathVector> lastPositions,
		    int dbMode)
{
  const Epetra_Comm& comm = x.Map().Comm();

  //Set xmax,xmin,ymax,ymin based on points
  xmax = xmin = imagePts[0].coords[0];
  ymax = ymin = imagePts[0].coords[1];
  for(std::size_t i=1; i<nImagePts; i++) {
    xmin = std::min(imagePts[i].coords[0],xmin);
    xmax = std::max(imagePts[i].coords[0],xmax);
    ymin = std::min(imagePts[i].coords[1],ymin);
    ymax = std::max(imagePts[i].coords[1],ymax);
  }
  xmin -= 5*imagePtSize; xmax += 5*imagePtSize;
  ymin -= 5*imagePtSize; ymax += 5*imagePtSize;
    
  //Reset value, weight, and gradient of image points as these are accumulated by evaluator fill
  imagePtValues.fill(0.0);
  imagePtWeights.fill(0.0);
  imagePtGradComps.fill(0.0);

  if(bAggregateWorksets) {
    //Use cached field and coordinate values to perform fill    
    for(std::size_t i=0; i<vFieldValues.size(); i++) {
      addImagePointData( vCoords[i].data, vFieldValues[i], vGrads[i].data );
    } 
  }
  else {
    mode = "Collect image point data";
    Albany::FieldManagerScalarResponseFunction::evaluateResponse(
				     current_time, xdot, x, p, g);
  }

  //MPI -- sum weights, value, and gradient for each image pt
  comm.SumAll( imagePtValues.data(),    globalPtValues,  nImagePts );
  comm.SumAll( imagePtWeights.data(),   globalPtWeights, nImagePts );
  comm.SumAll( imagePtGradComps.data(), globalPtGrads,   nImagePts*numDims );

  // Put summed data into imagePts, normalizing value and 
  //   gradient from different cell contributions
  for(std::size_t i=0; i<nImagePts; i++) {
    imagePts[i].weight = globalPtWeights[i];
    if(globalPtWeights[i] > 1e-6) {
      imagePts[i].value = globalPtValues[i] / imagePts[i].weight;
      for(std::size_t k=0; k<numDims; k++) 
	imagePts[i].grad[k] = globalPtGrads[k*nImagePts+i] / imagePts[i].weight;
    }
    else { 
      //assume point has drifted off region: leave value as, set gradient to zero, and reset position
      imagePts[i].grad.fill(0.0);
      imagePts[i].coords = lastPositions[i];
    }
  }

  return;
}

void
QCAD::SaddleValueResponseFunction::
getFinalImagePointValues(const double current_time,
		    const Epetra_Vector* xdot,
		    const Epetra_Vector& x,
		    const Teuchos::Array<ParamVec>& p,
		    Epetra_Vector& g, 
		    int dbMode)
{
  const Epetra_Comm& comm = x.Map().Comm();

  //Set xmax,xmin,ymax,ymin based on points
  xmax = xmin = imagePts[0].coords[0];
  ymax = ymin = imagePts[0].coords[1];
  for(std::size_t i=1; i<nImagePts; i++) {
    xmin = std::min(imagePts[i].coords[0],xmin);
    xmax = std::max(imagePts[i].coords[0],xmax);
    ymin = std::min(imagePts[i].coords[1],ymin);
    ymax = std::max(imagePts[i].coords[1],ymax);
  }
  xmin -= 5*imagePtSize; xmax += 5*imagePtSize;
  ymin -= 5*imagePtSize; ymax += 5*imagePtSize;
    
  //Reset value and weight of final image points as these are accumulated by evaluator fill
  finalPtValues.fill(0.0);
  finalPtWeights.fill(0.0);

  if(bAggregateWorksets) {
    //Use cached field and coordinate values to perform fill    
    for(std::size_t i=0; i<vFieldValues.size(); i++) {
      addFinalImagePointData( vCoords[i].data, vFieldValues[i] );
    } 
  }
  else {
    mode = "Collect final image point data";
    Albany::FieldManagerScalarResponseFunction::evaluateResponse(
				     current_time, xdot, x, p, g);
  }

  //MPI -- sum weights, value, and gradient for each image pt
  std::size_t nFinalPts = finalPts.size();
  if(nFinalPts > 0) {
    double*  globalPtValues   = new double [nFinalPts];
    double*  globalPtWeights  = new double [nFinalPts];
    comm.SumAll( finalPtValues.data(),    globalPtValues,  nFinalPts );
    comm.SumAll( finalPtWeights.data(),   globalPtWeights, nFinalPts );

    // Put summed data into imagePts, normalizing value from different cell contributions
    for(std::size_t i=0; i<nFinalPts; i++) {
      finalPts[i].weight = globalPtWeights[i];
      finalPts[i].grad.fill(0.0); // don't use gradient -- always fill with zeros

      if(globalPtWeights[i] > 1e-6) 
	finalPts[i].value = globalPtValues[i] / finalPts[i].weight;
      else 
	finalPts[i].value = 0.0; // no weight, so just set value to zero
    }
  }

  return;
}



void
QCAD::SaddleValueResponseFunction::
writeOutput(int nIters)
{
  // Write output every nEvery iterations
  if( (nEvery > 0) && (nIters % nEvery == 1) && (outputFilename.length() > 0)) {
    std::fstream out; double pathLength = 0.0;
    out.open(outputFilename.c_str(), std::fstream::out | std::fstream::app);
    out << "# Iteration " << nIters << std::endl;
    for(std::size_t i=0; i<nImagePts; i++) {
      out << i << " " << imagePts[i].coords[0] << " " << imagePts[i].coords[1]
	  << " " << imagePts[i].value << " " << pathLength << " " << imagePts[i].radius << std::endl;
      if(i<nImagePts-1) pathLength += imagePts[i].coords.distanceTo(imagePts[i+1].coords);
    }    
    out << std::endl << std::endl; //dataset separation
    out.close();
  }
}

void
QCAD::SaddleValueResponseFunction::
initialIterationSetup(double& gradScale, double& springScale, int dbMode)
{
  const QCAD::mathVector& initialPt = imagePts[0].coords;
  const QCAD::mathVector& finalPt = imagePts[nImagePts-1].coords;

  double maxGradMag = 0.0, avgWeight = 0.0, ifDist;
  for(std::size_t i=0; i<nImagePts; i++) {
    maxGradMag = std::max(maxGradMag, imagePts[i].grad.norm());
    avgWeight += imagePts[i].weight;
  }
  ifDist = initialPt.distanceTo(finalPt);
  avgWeight /= nImagePts;
  gradScale = ifDist / maxGradMag;  // want scale*maxGradMag*(dt=1.0) = distance btw initial & final pts

  // want springScale * (baseSpringConst=1.0) * (initial distance btwn pts) = scale*maxGradMag = distance btwn initial & final pts
  //  so springScale = (nImagePts-1)
  springScale = (double) (nImagePts-1);

  if(dbMode) std::cout << "Saddle Point:  First iteration:  maxGradMag=" << maxGradMag
		       << " |init-final|=" << ifDist << " gradScale=" << gradScale 
		       << " springScale=" << springScale << " avgWeight=" << avgWeight << std::endl;
  return;
}

void
QCAD::SaddleValueResponseFunction::
computeTangent(std::size_t i, QCAD::mathVector& tangent, int dbMode)
{
  // Compute tangent vector: use only higher neighboring imagePt.  
  //   Linear combination if both neighbors are above/below to smoothly interpolate cases
  double dValuePrev = imagePts[i-1].value - imagePts[i].value;
  double dValueNext = imagePts[i+1].value - imagePts[i].value;

  if(dValuePrev * dValueNext < 0.0) { //if both neighbors are either above or below current pt
    double dmax = std::max( fabs(dValuePrev), fabs(dValueNext) );
    double dmin = std::min( fabs(dValuePrev), fabs(dValueNext) );
    if(imagePts[i-1].value > imagePts[i+1].value)
      tangent = (imagePts[i+1].coords - imagePts[i].coords) * dmin + (imagePts[i].coords - imagePts[i-1].coords) * dmax;
    else
      tangent = (imagePts[i+1].coords - imagePts[i].coords) * dmax + (imagePts[i].coords - imagePts[i-1].coords) * dmin;
  }
  else {  //if one neighbor is above, the other below, just use the higher neighbor
    if(imagePts[i+1].value > imagePts[i].value)
      tangent = (imagePts[i+1].coords - imagePts[i].coords);
    else
      tangent = (imagePts[i].coords - imagePts[i-1].coords);
  }
  tangent.normalize();
  return;
}

void
QCAD::SaddleValueResponseFunction::
computeClimbingForce(std::size_t i, const QCAD::mathVector& tangent, const double& gradScale,
		     QCAD::mathVector& force, int dbMode)
{
  // Special case for highest point in climbing-NEB: force has full -Grad(V) but with parallel 
  //    component reversed and no force from springs (Future: add some perp spring force to avoid plateaus?)
  double dp = imagePts[i].grad.dot( tangent );
  force = (imagePts[i].grad * -1.0 + (tangent*dp) * 2) * gradScale; // force += -Grad(V) + 2*Grad(V)_parallel

  if(dbMode > 2) {
    std::cout << "Saddle Point:  --   tangent = " << tangent << std::endl;
    std::cout << "Saddle Point:  --   grad along tangent = " << dp << std::endl;
    std::cout << "Saddle Point:  --   total force (climbing) = " << force[i] << std::endl;
  }
}

void
QCAD::SaddleValueResponseFunction::
computeForce(std::size_t i, const QCAD::mathVector& tangent, const std::vector<double>& springConstants,
	     const double& gradScale,  const double& springScale, QCAD::mathVector& force,
	     double& dt, double& dt2, int dbMode)
{
  force.fill(0.0);
	
  // Get gradient projected perpendicular to the tangent and add to the force
  double dp = imagePts[i].grad.dot( tangent );
  force -= (imagePts[i].grad - tangent * dp) * gradScale; // force += -Grad(V)_perp

  if(dbMode > 2) {
    std::cout << "Saddle Point:  --   tangent = " << tangent << std::endl;
    std::cout << "Saddle Point:  --   grad along tangent = " << dp << std::endl;
    std::cout << "Saddle Point:  --   grad force = " << force[i] << std::endl;
  }

  // Get spring force projected parallel to the tangent and add to the force
  mathVector dNext(numDims), dPrev(numDims);
  mathVector parallelSpringForce(numDims), perpSpringForce(numDims);
  mathVector springForce(numDims);

  dPrev = imagePts[i-1].coords - imagePts[i].coords;
  dNext = imagePts[i+1].coords - imagePts[i].coords;

  double prevNorm = dPrev.norm();
  double nextNorm = dNext.norm();

  double perpFactor = 0.5 * (1 + cos(3.141592 * fabs(dPrev.dot(dNext) / (prevNorm * nextNorm))));
  springForce = ((dNext * springConstants[i]) + (dPrev * springConstants[i-1]));
  parallelSpringForce = tangent * springForce.dot(tangent);
  perpSpringForce = (springForce - tangent * springForce.dot(tangent) );
	
  springForce = parallelSpringForce + perpSpringForce * (perpFactor * antiKinkFactor);  
  while(springForce.norm() * dt2 > std::max(dPrev.norm(),dNext.norm()) && dt > minTimeStep) {
    dt /= 2; dt2=dt*dt;
    if(dbMode > 2) std::cout << "Saddle Point:  ** Warning: spring forces seem too large: dt => " << dt << std::endl;
  }

  force += springForce; // force += springForce_parallel + part of springForce_perp

  if(dbMode > 2) {
    std::cout << "Saddle Point:  --   spring force = " << springForce << std::endl;
    std::cout << "Saddle Point:  --   total force = " << force[i] << std::endl;
  }
}



std::string QCAD::SaddleValueResponseFunction::getMode()
{
  return mode;
}


bool QCAD::SaddleValueResponseFunction::
pointIsInImagePtRegion(const double* p, double refZ) const
{
  //assumes at least 2 dimensions
  if(numDims > 2 && (refZ < zmin || refZ > zmax)) return false;
  return !(p[0] < xmin || p[1] < ymin || p[0] > xmax || p[1] > ymax);
}

bool QCAD::SaddleValueResponseFunction::
pointIsInAccumRegion(const double* p, double refZ) const
{
  //assumes at least 2 dimensions
  if(numDims > 2 && (refZ < zmin || refZ > zmax)) return false;
  return true;
}

bool QCAD::SaddleValueResponseFunction::
pointIsInLevelSetRegion(const double* p, double refZ) const
{
  //assumes at least 2 dimensions
  if(numDims > 2 && (refZ < zmin || refZ > zmax)) return false;
  return (imagePts[iSaddlePt].coords.distanceTo(p) < levelSetRadius);
}



void QCAD::SaddleValueResponseFunction::
addBeginPointData(const std::string& elementBlock, const double* p, double value)
{
  //"Point" case: no need to process anything
  if( beginRegionType == "Point" ) return;

  //"Element Block" case: keep track of point with minimum value
  if( beginRegionType == "Element Block" ) {
    if(elementBlock == beginElementBlock) {
      if( value < imagePts[0].value || imagePts[0].weight == 0 ) {
	imagePts[0].value = value;
	imagePts[0].weight = 1.0; //positive weight flags initialization
	imagePts[0].coords.fill(p);
      }
    }
    return;
  }

  //"Polygon" case: keep track of point with minimum value
  if( beginRegionType == "Polygon" ) {
    if( QCAD::ptInPolygon(beginPolygon, p) ) {
      if( value < imagePts[0].value || imagePts[0].weight == 0 ) {
	imagePts[0].value = value;
	imagePts[0].weight = 1.0; //positive weight flags initialization
	imagePts[0].coords.fill(p);
      }
    }
    return;
  }

  TEUCHOS_TEST_FOR_EXCEPTION (true, Teuchos::Exceptions::InvalidParameter, std::endl 
      << "Invalid region type: " << beginRegionType << " for begin pt" << std::endl); 
  return;
}

void QCAD::SaddleValueResponseFunction::
addEndPointData(const std::string& elementBlock, const double* p, double value)
{
  //"Point" case: no need to process anything
  if( endRegionType == "Point" ) return;

  //"Element Block" case: keep track of point with minimum value
  if( endRegionType == "Element Block" ) {
    if(elementBlock == endElementBlock) {
      if( value < imagePts[nImagePts-1].value || imagePts[nImagePts-1].weight == 0 ) {
	imagePts[nImagePts-1].value = value;
	imagePts[nImagePts-1].weight = 1.0; //positive weight flags initialization
	imagePts[nImagePts-1].coords.fill(p);
      }
    }
    return;
  }

  //"Polygon" case: keep track of point with minimum value
  if( endRegionType == "Polygon" ) {
    if( QCAD::ptInPolygon(endPolygon, p) ) {
      if( value < imagePts[nImagePts-1].value || imagePts[nImagePts-1].weight == 0 ) {
	imagePts[nImagePts-1].value = value;
	imagePts[nImagePts-1].weight = 1.0; //positive weight flags initialization
	imagePts[nImagePts-1].coords.fill(p);
      }
    }
    return;
  }

  TEUCHOS_TEST_FOR_EXCEPTION (true, Teuchos::Exceptions::InvalidParameter, std::endl 
      << "Invalid region type: " << endRegionType << " for end pt" << std::endl); 
  return;
}


void QCAD::SaddleValueResponseFunction::
addImagePointData(const double* p, double value, double* grad)
{
  double w, effDims = (bLockToPlane && numDims > 2) ? 2 : numDims;
  for(std::size_t i=0; i<nImagePts; i++) {
    w = pointFn(imagePts[i].coords.distanceTo(p) , imagePts[i].radius );
    if(w > 0) {
      imagePtWeights[i] += w;
      imagePtValues[i] += w*value;
      for(std::size_t k=0; k<effDims; k++)
	imagePtGradComps[k*nImagePts+i] += w*grad[k];
      //std::cout << "DEBUG Image Pt " << i << " close to (" << p[0] << "," << p[1] << "," << p[2] << ")=" << value
      //	<< "  wt=" << w << "  totalW=" << imagePtWeights[i] << "  totalVal=" << imagePtValues[i]
      //	<< "  val=" << imagePtValues[i] / imagePtWeights[i] << std::endl;
    }
  }
  return;
}

void QCAD::SaddleValueResponseFunction::
addFinalImagePointData(const double* p, double value)
{
  double w;
  for(std::size_t i=0; i< finalPts.size(); i++) {
    w = pointFn(finalPts[i].coords.distanceTo(p) , finalPts[i].radius );
    if(w > 0) {
      finalPtWeights[i] += w;
      finalPtValues[i] += w*value;
    }
  }
  return;
}

void QCAD::SaddleValueResponseFunction::
accumulatePointData(const double* p, double value, double* grad)
{
  vFieldValues.push_back(value);
  vCoords.push_back( QCAD::maxDimPt(p,numDims) );
  vGrads.push_back( QCAD::maxDimPt(grad,numDims) );
}


void QCAD::SaddleValueResponseFunction::
accumulateLevelSetData(const double* p, double value, double cellArea)
{
  vlsFieldValues.push_back(value);
  vlsCellAreas.push_back(cellArea);
  for(std::size_t i=0; i < numDims; ++i)
    vlsCoords[i].push_back(p[i]);
}
 

//Adds and returns the weight of a point relative to the saddle point position.
double QCAD::SaddleValueResponseFunction::
getSaddlePointWeight(const double* p) const
{
  return pointFn(imagePts[iSaddlePt].coords.distanceTo(p) , imagePts[iSaddlePt].radius );
}

double QCAD::SaddleValueResponseFunction::
getTotalSaddlePointWeight() const
{
  return imagePts[iSaddlePt].weight;
}

const double* QCAD::SaddleValueResponseFunction::
getSaddlePointPosition() const
{
  return imagePts[iSaddlePt].coords.data();
}

bool QCAD::SaddleValueResponseFunction::
matchesCurrentResults(Epetra_Vector& g) const
{
  const double TOL = 1e-8;

  if(iSaddlePt < 0) return false;

  if( fabs(g[0] - returnFieldVal) > TOL || fabs(g[1] - imagePts[iSaddlePt].value) > TOL)
    return false;

  for(std::size_t i=0; i<numDims; i++) {
    if(  fabs(g[2+i] - imagePts[iSaddlePt].coords[i]) > TOL ) return false;
  }

  return true;
}


double QCAD::SaddleValueResponseFunction::
pointFn(double d, double radius) const {
  //return ( d < radius ) ? 1.0 : 0.0;  //alternative?

  const double N = 1.0;
  double val = N*exp(-d*d / (2*radius*radius));
  return (val >= 1e-2) ? val : 0.0;
}

int QCAD::SaddleValueResponseFunction::
getHighestPtIndex() const 
{
  // Find the highest image point 
  int iHighestPt = 0; // init to the first point being the highest
  for(std::size_t i=1; i<nImagePts; i++) {
    if(imagePts[i].value > imagePts[iHighestPt].value) iHighestPt = i;
  }
  return iHighestPt;
}



/*************************************************************/
//! Helper functions
/*************************************************************/

/*double distanceFromLineSegment(const double* p, const double* p1, const double* p2, int dims)
{
  //get pt c, the point along the full line p1->p2 closest to p
  double s, dp = 0, mag2 = 0; 

  for(int k=0; k<dims; k++) mag2 += pow(p2[k]-p1[k],2);
  for(int k=0; k<dims; k++) dp += (p[k]-p1[k])*(p2[k]-p1[k]);
  s = dp / sqrt(mag2); // < 0 or > 1 if c is outside segment, 0 <= s <= 1 if c is on segment

  if(0 <= s && s <= 1) { //just return distance between c and p
    double cp = 0;
    for(int k=0; k<dims; k++) cp += pow(p[k] - (p1[k]+s*(p2[k]-p1[k])),2);
    return sqrt(cp);
  }
  else { //take closer distance from the endpoints
    double d1=0, d2=0;
    for(int k=0; k<dims; k++) d1 += pow(p[k]-p1[k],2);
    for(int k=0; k<dims; k++) d2 += pow(p[k]-p2[k],2);
    if(d1 < d2) return sqrt(d1);
    else return sqrt(d2);
  }
  }*/

// Returns true if point is inside polygon, false otherwise
//  Assumes 2D points (more dims ok, but uses only first two components)
//  Algorithm = ray trace along positive x-axis
bool QCAD::ptInPolygon(const std::vector<QCAD::mathVector>& polygon, const QCAD::mathVector& pt) 
{
  return QCAD::ptInPolygon(polygon, pt.data());
}

bool QCAD::ptInPolygon(const std::vector<QCAD::mathVector>& polygon, const double* pt)
{
  bool c = false;
  int n = (int)polygon.size();
  double x=pt[0], y=pt[1];
  const int X=0,Y=1;

  for (int i = 0, j = n-1; i < n; j = i++) {
    const QCAD::mathVector& pi = polygon[i];
    const QCAD::mathVector& pj = polygon[j];
    if ((((pi[Y] <= y) && (y < pj[Y])) ||
	 ((pj[Y] <= y) && (y < pi[Y]))) &&
	(x < (pj[X] - pi[X]) * (y - pi[Y]) / (pj[Y] - pi[Y]) + pi[X]))
      c = !c;
  }
  return c;
}


//Not used - but keep for reference
// Returns true if point is inside polygon, false otherwise
/*bool orig_ptInPolygon(int npol, float *xp, float *yp, float x, float y)
{
  int i, j; bool c = false;
  for (i = 0, j = npol-1; i < npol; j = i++) {
    if ((((yp[i] <= y) && (y < yp[j])) ||
	 ((yp[j] <= y) && (y < yp[i]))) &&
	(x < (xp[j] - xp[i]) * (y - yp[i]) / (yp[j] - yp[i]) + xp[i]))
      c = !c;
  }
  return c;
}*/







/*************************************************************/
//! mathVector class: a vector with math operations (helper class) 
/*************************************************************/

QCAD::mathVector::mathVector() 
{
  dim_ = -1;
}

QCAD::mathVector::mathVector(int n) 
 :data_(n) 
{ 
  dim_ = n;
}


QCAD::mathVector::mathVector(const mathVector& copy) 
{ 
  data_ = copy.data_;
  dim_ = copy.dim_;
}

QCAD::mathVector::~mathVector() 
{
}


void 
QCAD::mathVector::resize(std::size_t n) 
{ 
  data_.resize(n); 
  dim_ = n;
}

void 
QCAD::mathVector::fill(double d) 
{ 
  for(int i=0; i<dim_; i++) data_[i] = d;
}

void 
QCAD::mathVector::fill(const double* vec) 
{ 
  for(int i=0; i<dim_; i++) data_[i] = vec[i];
}

double 
QCAD::mathVector::dot(const mathVector& v2) const
{
  double d=0;
  for(int i=0; i<dim_; i++)
    d += data_[i] * v2[i];
  return d;
}

QCAD::mathVector& 
QCAD::mathVector::operator=(const mathVector& rhs)
{
  data_ = rhs.data_;
  dim_ = rhs.dim_;
  return *this;
}

QCAD::mathVector 
QCAD::mathVector::operator+(const mathVector& v2) const
{
  mathVector result(dim_);
  for(int i=0; i<dim_; i++) result[i] = data_[i] + v2[i];
  return result;
}

QCAD::mathVector 
QCAD::mathVector::operator-(const mathVector& v2) const
{
  mathVector result(dim_);
  for(int i=0; i<dim_; i++) result[i] = data_[i] - v2[i];
  return result;
}

QCAD::mathVector
QCAD::mathVector::operator*(double scale) const 
{
  mathVector result(dim_);
  for(int i=0; i<dim_; i++) result[i] = scale*data_[i];
  return result;
}

QCAD::mathVector& 
QCAD::mathVector::operator+=(const mathVector& v2)
{
  for(int i=0; i<dim_; i++) data_[i] += v2[i];
  return *this;
}

QCAD::mathVector& 
QCAD::mathVector::operator-=(const mathVector& v2) 
{
  for(int i=0; i<dim_; i++) data_[i] -= v2[i];
  return *this;
}

QCAD::mathVector& 
QCAD::mathVector::operator*=(double scale) 
{
  for(int i=0; i<dim_; i++) data_[i] *= scale;
  return *this;
}

QCAD::mathVector&
QCAD::mathVector::operator/=(double scale) 
{
  for(int i=0; i<dim_; i++) data_[i] /= scale;
  return *this;
}

double&
QCAD::mathVector::operator[](int i) 
{ 
  return data_[i];
}

const double& 
QCAD::mathVector::operator[](int i) const 
{ 
  return data_[i];
}

double 
QCAD::mathVector::distanceTo(const mathVector& v2) const
{
  double d = 0.0;
  for(int i=0; i<dim_; i++) d += pow(data_[i]-v2[i],2);
  return sqrt(d);
}

double 
QCAD::mathVector::distanceTo(const double* p) const
{
  double d = 0.0;
  for(int i=0; i<dim_; i++) d += pow(data_[i]-p[i],2);
  return sqrt(d);
}
				 
double 
QCAD::mathVector::norm() const
{ 
  return sqrt(dot(*this)); 
}

double 
QCAD::mathVector::norm2() const
{ 
  return dot(*this); 
}


void 
QCAD::mathVector::normalize() 
{
  (*this) /= norm(); 
}

const double* 
QCAD::mathVector::data() const
{ 
  return data_.data();
}

double* 
QCAD::mathVector::data()
{ 
  return data_.data();
}


std::size_t 
QCAD::mathVector::size() const
{
  return dim_; 
}

std::ostream& QCAD::operator<<(std::ostream& os, const QCAD::mathVector& mv) 
{
  std::size_t size = mv.size();
  os << "(";
  for(std::size_t i=0; i<size-1; i++) os << mv[i] << ", ";
  if(size > 0) os << mv[size-1];
  os << ")";
  return os;
}

std::ostream& QCAD::operator<<(std::ostream& os, const QCAD::nebImagePt& np)
{
  os << std::endl;
  os << "coords = " << np.coords << std::endl;
  os << "veloc  = " << np.velocity << std::endl;
  os << "grad   = " << np.grad << std::endl;
  os << "value  = " << np.value << std::endl;
  os << "weight = " << np.weight << std::endl;
  return os;
}



/*************************************************************/
//! Helper functions
/*************************************************************/

void QCAD::gatherVector(std::vector<double>& v, std::vector<double>& gv, const Epetra_Comm& comm)
{
  double *pvec, zeroSizeDummy = 0;
  pvec = (v.size() > 0) ? &v[0] : &zeroSizeDummy;

  Epetra_Map map(-1, v.size(), 0, comm);
  Epetra_Vector ev(View, map, pvec);
  int  N = map.NumGlobalElements();
  Epetra_LocalMap lomap(N,0,comm);

  gv.resize(N);
  pvec = (gv.size() > 0) ? &gv[0] : &zeroSizeDummy;
  Epetra_Vector egv(View, lomap, pvec);
  Epetra_Import import(lomap,map);
  egv.Import(ev, import, Insert);
}

bool QCAD::lessOp(std::pair<std::size_t, double> const& a,
	    std::pair<std::size_t, double> const& b) {
  return a.second < b.second;
}

void QCAD::getOrdering(const std::vector<double>& v, std::vector<int>& ordering)
{
  typedef std::vector<double>::const_iterator dbl_iter;
  typedef std::vector<std::pair<std::size_t, double> >::const_iterator pair_iter;
  std::vector<std::pair<std::size_t, double> > vPairs(v.size());

  size_t n = 0;
  for (dbl_iter it = v.begin(); it != v.end(); ++it, ++n)
    vPairs[n] = std::make_pair(n, *it);


  std::sort(vPairs.begin(), vPairs.end(), QCAD::lessOp);

  ordering.resize(v.size()); n = 0;
  for (pair_iter it = vPairs.begin(); it != vPairs.end(); ++it, ++n)
    ordering[n] = it->first;
}


double QCAD::averageOfVector(const std::vector<double>& v)
{
  double avg = 0.0;
  for(std::size_t i=0; i < v.size(); i++) {
    avg += v[i];
  }
  avg /= v.size();
  return avg;
}

double QCAD::distance(const std::vector<double>* vCoords, int ind1, int ind2, std::size_t nDims)
{
  double d2 = 0;
  for(std::size_t k=0; k<nDims; k++)
    d2 += pow( vCoords[k][ind1] - vCoords[k][ind2], 2 );
  return sqrt(d2);
}
