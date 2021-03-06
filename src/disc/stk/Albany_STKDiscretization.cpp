//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//


#include <limits>
#include "Epetra_Export.h"

#include "Albany_Utils.hpp"
#include "Albany_STKDiscretization.hpp"
#include "Albany_NodalGraphUtils.hpp"
#include "Albany_STKNodeFieldContainer.hpp"
#include "Albany_BucketArray.hpp"

#include <string>
#include <iostream>
#include <fstream>

#include <Shards_BasicTopologies.hpp>

#include <Intrepid_CellTools.hpp>
#include <Intrepid_Basis.hpp>

#include <stk_util/parallel/Parallel.hpp>

#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/Selector.hpp>

#include <PHAL_Dimension.hpp>

#include <stk_mesh/base/FEMHelpers.hpp>

#ifdef ALBANY_SEACAS
#include <Ionit_Initializer.h>
#include <netcdf.h>

#ifdef ALBANY_PAR_NETCDF
extern "C" {
#include <netcdf_par.h>
}
#endif
#endif

#include <algorithm>
#include "EpetraExt_MultiVectorOut.h"

const double pi = 3.1415926535897932385;

//uncomment the following line if you want debug output to be printed to screen
//#define OUTPUT_TO_SCREEN

Albany::STKDiscretization::STKDiscretization(Teuchos::RCP<Albany::AbstractSTKMeshStruct> stkMeshStruct_,
					     const Teuchos::RCP<const Epetra_Comm>& comm_,
                         const Teuchos::RCP<Piro::MLRigidBodyModes>& rigidBodyModes_) :

  out(Teuchos::VerboseObjectBase::getDefaultOStream()),
  previous_time_label(-1.0e32),
  metaData(*stkMeshStruct_->metaData),
  bulkData(*stkMeshStruct_->bulkData),
  comm(comm_),
  rigidBodyModes(rigidBodyModes_),
  neq(stkMeshStruct_->neq),
  stkMeshStruct(stkMeshStruct_),
  interleavedOrdering(stkMeshStruct_->interleavedOrdering)
{
  Albany::STKDiscretization::updateMesh();
}

Albany::STKDiscretization::~STKDiscretization()
{
#ifdef ALBANY_SEACAS
  if (stkMeshStruct->exoOutput || stkMeshStruct->cdfOutput) delete mesh_data;

  if (stkMeshStruct->cdfOutput)
      if (netCDFp)
    if (const int ierr = nc_close (netCDFp))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "close returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

#endif

  for (int i=0; i< toDelete.size(); i++) delete [] toDelete[i];
}


Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getMap() const
{
  return map;
}

Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getOverlapMap() const
{
  return overlap_map;
}

Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getMap(const std::string& field_name) const {
  return nodalDOFsStructContainer.getDOFsStruct(field_name).map;
}

Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getOverlapMap(const std::string& field_name) const {
  return nodalDOFsStructContainer.getDOFsStruct(field_name).overlap_map;
}

Teuchos::RCP<const Epetra_CrsGraph>
Albany::STKDiscretization::getJacobianGraph() const
{
  return graph;
}

Teuchos::RCP<const Epetra_CrsGraph>
Albany::STKDiscretization::getOverlapJacobianGraph() const
{
  return overlap_graph;
}

Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getNodeMap() const
{
  return node_map;
}

Teuchos::RCP<const Epetra_Map>
Albany::STKDiscretization::getOverlapNodeMap() const
{
  return overlap_node_map;
}

const Albany::WorksetArray<Teuchos::ArrayRCP<Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> > > >::type&
Albany::STKDiscretization::getWsElNodeEqID() const
{
  return wsElNodeEqID;
}

const Albany::WorksetArray<Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> > >::type&
Albany::STKDiscretization::getWsElNodeID() const
{
  return wsElNodeID;
}

const Albany::WorksetArray<Teuchos::ArrayRCP<Teuchos::ArrayRCP<double*> > >::type&
Albany::STKDiscretization::getCoords() const
{
  return coords;
}

const Albany::WorksetArray<Teuchos::ArrayRCP<double> >::type&
Albany::STKDiscretization::getSphereVolume() const
{
  return sphereVolume;
}

void
Albany::STKDiscretization::printCoords() const
{

std::cout << "Processor " << bulkData.parallel_rank() << " has " << coords.size() << " worksets." << std::endl;

       for (int ws=0; ws<coords.size(); ws++) {  //workset
         for (int e=0; e<coords[ws].size(); e++) { //cell
           for (int j=0; j<coords[ws][e].size(); j++) { //node
             for (int d=0; d<stkMeshStruct->numDim; d++){  //node
std::cout << "Coord for workset: " << ws << " element: " << e << " node: " << j << " DOF: " << d << " is: " <<
                coords[ws][e][j][d] << std::endl;
       } } } }

}


Teuchos::ArrayRCP<double>&
Albany::STKDiscretization::getCoordinates() const
{
  // Coordinates are computed here, and not precomputed,
  // since the mesh can move in shape opt problems

  AbstractSTKFieldContainer::VectorFieldType* coordinates_field = stkMeshStruct->getCoordinatesField();

  for (int i=0; i < numOverlapNodes; i++)  {
    int node_gid = gid(overlapnodes[i]);
    int node_lid = overlap_node_map->LID(node_gid);

    double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
    for (int dim=0; dim<stkMeshStruct->numDim; dim++)
      coordinates[3*node_lid + dim] = x[dim];

  }

  return coordinates;
}

//The function transformMesh() maps a unit cube domain by applying the transformation
//x = L*x
//y = L*y
//z = s(x,y)*z + b(x,y)*(1-z)
//where b(x,y) and s(x,y) are curves specifying the bedrock and top surface
//geometries respectively.
//Currently this function is only needed for some FELIX problems.


void
Albany::STKDiscretization::transformMesh()
{
  using std::cout; using std::endl;
  AbstractSTKFieldContainer::VectorFieldType* coordinates_field = stkMeshStruct->getCoordinatesField();
  std::string transformType = stkMeshStruct->transformType;

  if (transformType == "None") {}
  else if (transformType == "ISMIP-HOM Test A") {
#ifdef OUTPUT_TO_SCREEN
    *out << "Test A!" << endl;
#endif
    double L = stkMeshStruct->felixL;
    double alpha = stkMeshStruct->felixAlpha;
#ifdef OUTPUT_TO_SCREEN
    *out << "L: " << L << endl;
    *out << "alpha degrees: " << alpha << endl;
#endif
    alpha = alpha*pi/180; //convert alpha, read in from ParameterList, to radians
#ifdef OUTPUT_TO_SCREEN
    *out << "alpha radians: " << alpha << endl;
#endif
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = -x[0]*tan(alpha);
      double b = s - 1.0 + 0.5*sin(2*pi/L*x[0])*sin(2*pi/L*x[1]);
      x[2] = s*x[2] + b*(1-x[2]);
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
     }
   }
  else if (transformType == "ISMIP-HOM Test B") {
#ifdef OUTPUT_TO_SCREEN
    *out << "Test B!" << endl;
#endif
    double L = stkMeshStruct->felixL;
    double alpha = stkMeshStruct->felixAlpha;
#ifdef OUTPUT_TO_SCREEN
    *out << "L: " << L << endl;
    *out << "alpha degrees: " << alpha << endl;
#endif
    alpha = alpha*pi/180; //convert alpha, read in from ParameterList, to radians
#ifdef OUTPUT_TO_SCREEN
    *out << "alpha radians: " << alpha << endl;
#endif
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = -x[0]*tan(alpha);
      double b = s - 1.0 + 0.5*sin(2*pi/L*x[0]);
      x[2] = s*x[2] + b*(1-x[2]);
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
     }
   }
   else if ((transformType == "ISMIP-HOM Test C") || (transformType == "ISMIP-HOM Test D")) {
#ifdef OUTPUT_TO_SCREEN
    *out << "Test C and D!" << endl;
#endif
    double L = stkMeshStruct->felixL;
    double alpha = stkMeshStruct->felixAlpha;
#ifdef OUTPUT_TO_SCREEN
    *out << "L: " << L << endl;
    *out << "alpha degrees: " << alpha << endl;
#endif
    alpha = alpha*pi/180; //convert alpha, read in from ParameterList, to radians
#ifdef OUTPUT_TO_SCREEN
    *out << "alpha radians: " << alpha << endl;
#endif
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = -x[0]*tan(alpha);
      double b = s - 1.0;
      x[2] = s*x[2] + b*(1-x[2]);
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
     }
   }
   else if (transformType == "Dome") {
#ifdef OUTPUT_TO_SCREEN
    *out << "Dome transform!" << endl;
#endif
    double L = 0.7071*30;
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = 0.7071*sqrt(450.0 - x[0]*x[0] - x[1]*x[1])/sqrt(450.0);
      x[2] = s*x[2];
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
    }
  }
   else if (transformType == "Confined Shelf") {
#ifdef OUTPUT_TO_SCREEN
    *out << "Confined shelf transform!" << endl;
#endif
    double L = stkMeshStruct->felixL;
    cout << "L: " << L << endl;
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = 0.06; //top surface is at z=0.06km=60m
      double b = -0.440; //basal surface is at z=-0.440km=-440m
      x[2] = s*x[2] + b*(1.0-x[2]);
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
    }
  }
  else if (transformType == "Circular Shelf") {
#ifdef OUTPUT_TO_SCREEN
    *out << "Circular shelf transform!" << endl;
#endif
    double L = stkMeshStruct->felixL;
#ifdef OUTPUT_TO_SCREEN
    *out << "L: " << L << endl;
#endif
    double rhoIce = 910.0; //ice density, in kg/m^3
    double rhoOcean = 1028.0; //ocean density, in kg/m^3
    stkMeshStruct->PBCStruct.scale[0]*=L;
    stkMeshStruct->PBCStruct.scale[1]*=L;
    stk::mesh::Field<double>* surfaceHeight_field = metaData.get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "surface_height");
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = L*x[0];
      x[1] = L*x[1];
      double s = 1.0-rhoIce/rhoOcean; //top surface is at z=(1-rhoIce/rhoOcean) km
      double b = s - 1.0; //basal surface is at z=s-1 km
      x[2] = s*x[2] + b*(1.0-x[2]);
      *stk::mesh::field_data(*surfaceHeight_field, overlapnodes[i]) = s;
    }
  }
#ifdef ALBANY_AERAS
  else if (transformType == "Aeras Schar Mountain") {
    *out << "Aeras Schar Mountain transformation!" << endl;
    double rhoOcean = 1028.0; //ocean density, in kg/m^3
    for (int i=0; i < numOverlapNodes; i++)  {
      double* x = stk::mesh::field_data(*coordinates_field, overlapnodes[i]);
      x[0] = x[0];
      double hstar = 0.0, h;
      if (std::abs(x[0]-150.0) <= 25.0) hstar = 3.0* std::pow(cos(M_PI*(x[0]-150.0) / 50.0),2);
      h = hstar * std::pow(cos(M_PI*(x[0]-150.0) / 8.0),2);
      x[1] = x[1] + h*(25.0 - x[1])/25.0;
    }
  }
#endif
  else {
    TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
      "STKDiscretization::transformMesh() Unknown transform type :" << transformType << std::endl);
  }
}

void
Albany::STKDiscretization::setupMLCoords()
{

  // if ML is not used, return

  if(rigidBodyModes.is_null()) return;

  if(!rigidBodyModes->isMLUsed()) return;

  // Function to return x,y,z at owned nodes as double*, specifically for ML
  int numDim = stkMeshStruct->numDim;
  AbstractSTKFieldContainer::VectorFieldType* coordinates_field = stkMeshStruct->getCoordinatesField();

  rigidBodyModes->resize(numDim, numOwnedNodes);

  double *xx;
  double *yy;
  double *zz;

  rigidBodyModes->getCoordArrays(&xx, &yy, &zz);

  for (int i=0; i < numOwnedNodes; i++)  {
    int node_gid = gid(ownednodes[i]);
    int node_lid = node_map->LID(node_gid);

    double* X = stk::mesh::field_data(*coordinates_field, ownednodes[i]);
    if (numDim > 0) xx[node_lid] = X[0];
    if (numDim > 1) yy[node_lid] = X[1];
    if (numDim > 2) zz[node_lid] = X[2];
  }


  //see if user wants to write the coordinates to matrix market file
  bool writeCoordsToMMFile = stkMeshStruct->writeCoordsToMMFile;
  //if user wants to write the coordinates to matrix market file, write them to matrix market file
  if (writeCoordsToMMFile == true) {
    if (node_map->Comm().MyPID()==0) {std::cout << "Writing mesh coordinates to Matrix Market file." << std::endl;}
    //Writing of coordinates to MatrixMarket file for Ray
    Epetra_Vector xCoords(Copy, *node_map, xx);
    EpetraExt::MultiVectorToMatrixMarketFile("xCoords.mm", xCoords);
    if (yy != NULL) {
      Epetra_Vector yCoords(Copy, *node_map, yy);
      EpetraExt::MultiVectorToMatrixMarketFile("yCoords.mm", yCoords);
    }
    if (zz != NULL){
      Epetra_Vector zCoords(Copy, *node_map, zz);
      EpetraExt::MultiVectorToMatrixMarketFile("zCoords.mm", zCoords);
    }
  }

  rigidBodyModes->informML();

}


const Albany::WorksetArray<std::string>::type&
Albany::STKDiscretization::getWsEBNames() const
{
  return wsEBNames;
}

const Albany::WorksetArray<int>::type&
Albany::STKDiscretization::getWsPhysIndex() const
{
  return wsPhysIndex;
}

//void Albany::STKDiscretization::outputToExodus(const Epetra_Vector& soln, const double time, const bool overlapped)
void Albany::STKDiscretization::writeSolution(const Epetra_Vector& soln, const double time, const bool overlapped){

  // Put solution as Epetra_Vector into STK Mesh
  if(overlapped)

    // soln coming in is overlapped
    setOvlpSolutionField(soln);

  else

    setSolutionField(soln);


#ifdef ALBANY_SEACAS

  if (stkMeshStruct->exoOutput && stkMeshStruct->transferSolutionToCoords) {

   Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

   container->transferSolutionToCoords();

   if (mesh_data != NULL) {

     // Mesh coordinates have changed. Rewrite output file by deleting the mesh data object and recreate it
     delete mesh_data;
     setupExodusOutput();
   }
  }


   // Skip this write unless the proper interval has been reached
  if (stkMeshStruct->exoOutput && !(outputInterval % stkMeshStruct->exoOutputInterval)) {

     double time_label = monotonicTimeLabel(time);

     int out_step = mesh_data->process_output_request(outputFileIdx, time_label);

     if (map->Comm().MyPID()==0) {
       *out << "Albany::STKDiscretization::writeSolution: writing time " << time;
       if (time_label != time) *out << " with label " << time_label;
       *out << " to index " <<out_step<<" in file "<<stkMeshStruct->exoOutFile<< std::endl;
     }
  }
  if (stkMeshStruct->cdfOutput && !(outputInterval % stkMeshStruct->cdfOutputInterval)) {

     double time_label = monotonicTimeLabel(time);

     const int out_step = processNetCDFOutputRequest(soln);

     if (map->Comm().MyPID()==0) {
       *out << "Albany::STKDiscretization::writeSolution: writing time " << time;
       if (time_label != time) *out << " with label " << time_label;
       *out << " to index " <<out_step<<" in file "<<stkMeshStruct->cdfOutFile<< std::endl;
     }
  }
  outputInterval++;
#endif
}

double
Albany::STKDiscretization::monotonicTimeLabel(const double time)
{
  // If increasing, then all is good
  if (time > previous_time_label) {
    previous_time_label = time;
    return time;
  }
// Try absolute value
  double time_label = fabs(time);
  if (time_label > previous_time_label) {
    previous_time_label = time_label;
    return time_label;
  }

  // Try adding 1.0 to time
  if (time_label+1.0 > previous_time_label) {
    previous_time_label = time_label+1.0;
    return time_label+1.0;
  }

  // Otherwise, just add 1.0 to previous
  previous_time_label += 1.0;
  return previous_time_label;
}

void
Albany::STKDiscretization::setResidualField(const Epetra_Vector& residual)
{
#ifdef ALBANY_LCM
  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  if(container->hasResidualField()){

    // Iterate over the on-processor nodes
    stk::mesh::Selector locally_owned = metaData.locally_owned_part();

    container->saveResVector(residual, locally_owned, node_map);

    // Write the overlapped data
//    stk::mesh::Selector select_owned_or_shared = metaData.locally_owned_part() | metaData.globally_shared_part();

//    container->saveResVector(residual, select_owned_or_shared, overlap_node_map);
  }
#endif
}

Teuchos::RCP<Epetra_Vector>
Albany::STKDiscretization::getSolutionField() const
{
  // Copy soln vector into solution field, one node at a time
  Teuchos::RCP<Epetra_Vector> soln = Teuchos::rcp(new Epetra_Vector(*map));
  this->getSolutionField(*soln);
  return soln;
}

int
Albany::STKDiscretization::getSolutionFieldHistoryDepth() const
{
  return stkMeshStruct->getSolutionFieldHistoryDepth();
}

Teuchos::RCP<Epetra_MultiVector>
Albany::STKDiscretization::getSolutionFieldHistory() const
{
  const int stepCount = this->getSolutionFieldHistoryDepth();
  return this->getSolutionFieldHistoryImpl(stepCount);
}

Teuchos::RCP<Epetra_MultiVector>
Albany::STKDiscretization::getSolutionFieldHistory(int maxStepCount) const
{
  const int stepCount = std::min(this->getSolutionFieldHistoryDepth(), maxStepCount);
  return this->getSolutionFieldHistoryImpl(stepCount);
}

void
Albany::STKDiscretization::getSolutionFieldHistory(Epetra_MultiVector &result) const
{
  TEUCHOS_TEST_FOR_EXCEPT(!this->map->SameAs(result.Map()));
  const int stepCount = std::min(this->getSolutionFieldHistoryDepth(), result.NumVectors());
  Epetra_MultiVector head(View, result, 0, stepCount);
  this->getSolutionFieldHistoryImpl(head);
}

Teuchos::RCP<Epetra_MultiVector>
Albany::STKDiscretization::getSolutionFieldHistoryImpl(int stepCount) const
{
  const int vectorCount = stepCount > 0 ? stepCount : 1; // A valid MultiVector has at least one vector
  const Teuchos::RCP<Epetra_MultiVector> result = Teuchos::rcp(new Epetra_MultiVector(*map, vectorCount));
  if (stepCount > 0) {
    this->getSolutionFieldHistoryImpl(*result);
  }
  return result;
}

void
Albany::STKDiscretization::getSolutionFieldHistoryImpl(Epetra_MultiVector &result) const
{
  const int stepCount = result.NumVectors();
  for (int i = 0; i < stepCount; ++i) {
    stkMeshStruct->loadSolutionFieldHistory(i);
    Epetra_Vector v(View, result, i);
    this->getSolutionField(v);
  }
}

void
Albany::STKDiscretization::getSolutionField(Epetra_Vector &result) const
{

  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  // Iterate over the on-processor nodes by getting node buckets and iterating over each bucket.
  stk::mesh::Selector locally_owned = metaData.locally_owned_part();

  container->fillSolnVector(result, locally_owned, node_map);

}


void
Albany::STKDiscretization::getField(Epetra_Vector &result, const std::string& name) const
{

  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  // Iterate over the on-processor nodes by getting node buckets and iterating over each bucket.
  const std::string& part = nodalDOFsStructContainer.fieldToMap.find(name)->second->first.first;
  stk::mesh::Selector selector = metaData.locally_owned_part();
  if(part.size()) {
    std::map<std::string, stk::mesh::Part*>::const_iterator it = stkMeshStruct->nsPartVec.find(part);
    if(it != stkMeshStruct->nsPartVec.end())
      selector &= stk::mesh::Selector( *(it->second) );
  }

  const DOFsStruct& dofsStruct = nodalDOFsStructContainer.getDOFsStruct(name);

  container->fillVector(result, name, selector, dofsStruct.node_map,dofsStruct.dofManager);
}

/*****************************************************************/
/*** Private functions follow. These are just used in above code */
/*****************************************************************/

void
Albany::STKDiscretization::setField(const Epetra_Vector &result, const std::string& name, bool overlapped)
{
  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  const std::string& part = nodalDOFsStructContainer.fieldToMap.find(name)->second->first.first;

  stk::mesh::Selector selector = overlapped ?
      metaData.locally_owned_part() | metaData.globally_shared_part() :
      metaData.locally_owned_part();

  // Iterate over the on-processor nodes by getting node buckets and iterating over each bucket.
  if(part.size()) {
    std::map<std::string, stk::mesh::Part*>::const_iterator it = stkMeshStruct->nsPartVec.find(part);
    if(it != stkMeshStruct->nsPartVec.end())
      selector &= stk::mesh::Selector( *(it->second) );
  }

  const DOFsStruct& dofsStruct = nodalDOFsStructContainer.getDOFsStruct(name);

  if(overlapped)
    container->saveVector(result, name, selector, dofsStruct.overlap_node_map, dofsStruct.overlap_dofManager);
  else
    container->saveVector(result, name, selector, dofsStruct.node_map, dofsStruct.dofManager);
}

void
Albany::STKDiscretization::setSolutionField(const Epetra_Vector& soln)
{
  // Copy soln vector into solution field, one node at a time
  // Note that soln coming in is the local (non overlapped) soln

  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  // Iterate over the on-processor nodes
  stk::mesh::Selector locally_owned = metaData.locally_owned_part();

  container->saveSolnVector(soln, locally_owned, node_map);

}

void
Albany::STKDiscretization::setOvlpSolutionField(const Epetra_Vector& soln)
{
  // Copy soln vector into solution field, one node at a time
  // Note that soln coming in is the local+ghost (overlapped) soln

  Teuchos::RCP<AbstractSTKFieldContainer> container = stkMeshStruct->getFieldContainer();

  // Iterate over the processor-visible nodes
  stk::mesh::Selector select_owned_or_shared = metaData.locally_owned_part() | metaData.globally_shared_part();

  container->saveSolnVector(soln, select_owned_or_shared, overlap_node_map);

}

inline int Albany::STKDiscretization::gid(const stk::mesh::Entity node) const
{ return bulkData.identifier(node)-1; }

int Albany::STKDiscretization::getOwnedDOF(const int inode, const int eq) const
{
  if (interleavedOrdering) return inode*neq + eq;
  else  return inode + numOwnedNodes*eq;
}

int Albany::STKDiscretization::getOverlapDOF(const int inode, const int eq) const
{
  if (interleavedOrdering) return inode*neq + eq;
  else  return inode + numOverlapNodes*eq;
}

int Albany::STKDiscretization::getGlobalDOF(const int inode, const int eq) const
{
  if (interleavedOrdering) return inode*neq + eq;
  else  return inode + numGlobalNodes*eq;
}

int Albany::STKDiscretization::nonzeroesPerRow(const int neq) const
{
  int numDim = stkMeshStruct->numDim;
  int estNonzeroesPerRow;
  switch (numDim) {
  case 0: estNonzeroesPerRow=1*neq; break;
  case 1: estNonzeroesPerRow=3*neq; break;
  case 2: estNonzeroesPerRow=9*neq; break;
  case 3: estNonzeroesPerRow=27*neq; break;
  default: TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
			      "STKDiscretization:  Bad numDim"<< numDim);
  }
  return estNonzeroesPerRow;
}


void Albany::STKDiscretization::computeNodalEpetraMaps (bool overlapped)
{
  // Loads member data:  ownednodes, numOwnedNodes, node_map, numGlobalNodes, map
  // maps for owned nodes and unknowns

  stk::mesh::Selector map_type_selector = overlapped ?
           (metaData.locally_owned_part() | metaData.globally_shared_part()) :
           metaData.locally_owned_part();

  NodalDOFsStructContainer::MapOfDOFsStructs::iterator it, it2;
  NodalDOFsStructContainer::MapOfDOFsStructs& mapOfDOFsStructs = nodalDOFsStructContainer.mapOfDOFsStructs;
  std::vector< stk::mesh::Entity> nodes;
  std::vector<int> indices;
  int numNodes(0);

  //compute NumGlobalNodes
  stk::mesh::get_selected_entities( map_type_selector ,
                  bulkData.buckets( stk::topology::NODE_RANK ) ,
                  nodes );

  long long int maxID(0), maxGID(0);
  for (int i=0; i < nodes.size(); i++)
    maxID = std::max(maxID, static_cast<long long int>(gid(nodes[i])));
  comm->MaxAll(&maxID, &maxGID, 1);
  numGlobalNodes = maxGID+1; //maxGID is the same for overlapped and unique maps

  // build maps
  for(it = mapOfDOFsStructs.begin(); it != mapOfDOFsStructs.end(); ++it ) {
    stk::mesh::Selector selector(map_type_selector);
    const std::string& part = it->first.first;
    int nComp = it->first.second;
    if(part.size())  {
      std::map<std::string, stk::mesh::Part*>::const_iterator it3 = stkMeshStruct->nsPartVec.find(part);
      if(it3 != stkMeshStruct->nsPartVec.end())
        selector &= *(it3->second);
      else { //throw error
        std::ostringstream msg;
        msg << "Albany::STKDiscretization::computeNodalEpetraMaps(overlapped==" << overlapped <<
            "):\n    Part " << part << " is not in  stkMeshStruct->nsPartVec.\n";
        throw std::runtime_error(msg.str());
      }
    }

    stk::mesh::get_selected_entities( selector ,
                bulkData.buckets( stk::topology::NODE_RANK ) ,
                nodes );

    numNodes = nodes.size();
    std::vector<int> indices(numNodes*nComp);
    NodalDOFManager* dofManager = (overlapped) ? &it->second.overlap_dofManager : &it->second.dofManager;
    dofManager->setup(&bulkData, nComp, numNodes, numGlobalNodes, interleavedOrdering);

    for (int i=0; i < numNodes; i++)
      for (int j=0; j < nComp; j++)
        indices[dofManager->getLocalDOF(i,j)] = dofManager->getGlobalDOF(nodes[i],j);

    Teuchos::RCP<Epetra_Map>& map = (overlapped) ? it->second.overlap_map : it->second.map;
    map = Teuchos::null;
    map = Teuchos::rcp(new Epetra_Map(-1, indices.size(), indices.data(), 0, *comm));

    Teuchos::RCP<Epetra_Map>& node_map = (overlapped) ? it->second.overlap_node_map : it->second.node_map;
    node_map = Teuchos::null;

    it2=it;
    if((nComp==1) || ((it2=mapOfDOFsStructs.find(make_pair(part,1)))!=mapOfDOFsStructs.end())) {
      node_map = (overlapped) ? it2->second.overlap_map : it2->second.map;
    }
  }
}

void Albany::STKDiscretization::computeOwnedNodesAndUnknowns()
{
  // Loads member data:  ownednodes, numOwnedNodes, node_map, numGlobalNodes, map
  // maps for owned nodes and unknowns
  stk::mesh::Selector select_owned_in_part =
    stk::mesh::Selector( metaData.universal_part() ) &
    stk::mesh::Selector( metaData.locally_owned_part() );

  stk::mesh::get_selected_entities( select_owned_in_part ,
				    bulkData.buckets( stk::topology::NODE_RANK ) ,
				    ownednodes );

  numOwnedNodes = ownednodes.size();

  node_map = nodalDOFsStructContainer.getDOFsStruct("mesh_nodes").map;

  if(Teuchos::nonnull(stkMeshStruct->nodal_data_block))
    stkMeshStruct->nodal_data_block->resizeLocalMap(node_map, *comm);

  map = nodalDOFsStructContainer.getDOFsStruct("ordinary_solution").map;

}

void Albany::STKDiscretization::computeOverlapNodesAndUnknowns()
{
  // maps for overlap unknowns
  stk::mesh::Selector select_overlap_in_part =
    stk::mesh::Selector( metaData.universal_part() ) &
    ( stk::mesh::Selector( metaData.locally_owned_part() )
      | stk::mesh::Selector( metaData.globally_shared_part() ) );

  //  overlapnodes used for overlap map -- stored for changing coords
  stk::mesh::get_selected_entities( select_overlap_in_part ,
				    bulkData.buckets( stk::topology::NODE_RANK ) ,
				    overlapnodes );

  numOverlapNodes = overlapnodes.size();

  overlap_map = nodalDOFsStructContainer.getDOFsStruct("ordinary_solution").overlap_map;

  overlap_node_map = nodalDOFsStructContainer.getDOFsStruct("mesh_nodes").overlap_map;

  if(Teuchos::nonnull(stkMeshStruct->nodal_data_block))
    stkMeshStruct->nodal_data_block->resizeOverlapMap(overlap_node_map, *comm);

  coordinates.resize(3*numOverlapNodes);

}


void Albany::STKDiscretization::computeGraphs()
{

  std::map<int, stk::mesh::Part*>::iterator pv = stkMeshStruct->partVec.begin();
  int nodes_per_element =  metaData.get_cell_topology(*(pv->second)).getNodeCount();
// int nodes_per_element_est =  metaData.get_cell_topology(*(stkMeshStruct->partVec[0])).getNodeCount();

  // Loads member data:  overlap_graph, numOverlapodes, overlap_node_map, coordinates, graphs

  overlap_graph = Teuchos::null; // delete existing graph happens here on remesh

  overlap_graph =
    Teuchos::rcp(new Epetra_CrsGraph(Copy, *overlap_map,
                                     neq*nodes_per_element, false));

  stk::mesh::Selector select_owned_in_part =
    stk::mesh::Selector( metaData.universal_part() ) &
    stk::mesh::Selector( metaData.locally_owned_part() );

  stk::mesh::get_selected_entities( select_owned_in_part ,
				    bulkData.buckets( stk::topology::ELEMENT_RANK ) ,
				    cells );


  if (comm->MyPID()==0)
    *out << "STKDisc: " << cells.size() << " elements on Proc 0 " << std::endl;

  int row, col;

  for (std::size_t i=0; i < cells.size(); i++) {
    stk::mesh::Entity e = cells[i];
    stk::mesh::Entity const* node_rels = bulkData.begin_nodes(e);
    const size_t num_nodes = bulkData.num_nodes(e);

    // loop over local nodes
    for (std::size_t j=0; j < num_nodes; j++) {
      stk::mesh::Entity rowNode = node_rels[j];

      // loop over eqs
      for (std::size_t k=0; k < neq; k++) {
        row = getGlobalDOF(gid(rowNode), k);
        for (std::size_t l=0; l < num_nodes; l++) {
          stk::mesh::Entity colNode = node_rels[l];
          for (std::size_t m=0; m < neq; m++) {
            col = getGlobalDOF(gid(colNode), m);
            overlap_graph->InsertGlobalIndices(row, 1, &col);
          }
        }
      }
    }
  }
  overlap_graph->FillComplete();

  // Create Owned graph by exporting overlap with known row map

  graph = Teuchos::null; // delete existing graph happens here on remesh

  graph = Teuchos::rcp(new Epetra_CrsGraph(Copy, *map, nonzeroesPerRow(neq), false));

  // Create non-overlapped matrix using two maps and export object
  Epetra_Export exporter(*overlap_map, *map);
  graph->Export(*overlap_graph, exporter, Insert);
  graph->FillComplete();

}

void Albany::STKDiscretization::computeWorksetInfo()
{

  stk::mesh::Selector select_owned_in_part =
    stk::mesh::Selector( metaData.universal_part() ) &
    stk::mesh::Selector( metaData.locally_owned_part() );

  stk::mesh::BucketVector const& buckets = bulkData.get_buckets( stk::topology::ELEMENT_RANK, select_owned_in_part );

  const int numBuckets =  buckets.size();

  typedef AbstractSTKFieldContainer::ScalarFieldType ScalarFieldType;
  typedef AbstractSTKFieldContainer::VectorFieldType VectorFieldType;
  typedef AbstractSTKFieldContainer::TensorFieldType TensorFieldType;

  VectorFieldType* coordinates_field = stkMeshStruct->getCoordinatesField();
  ScalarFieldType* sphereVolume_field;

  if(stkMeshStruct->getFieldContainer()->hasSphereVolumeField())
    sphereVolume_field = stkMeshStruct->getFieldContainer()->getSphereVolumeField();

  wsEBNames.resize(numBuckets);
  for (int i=0; i<numBuckets; i++) {
    stk::mesh::PartVector const& bpv = buckets[i]->supersets();

    for (std::size_t j=0; j<bpv.size(); j++) {
      if (bpv[j]->primary_entity_rank() == stk::topology::ELEMENT_RANK &&
          !stk::mesh::is_auto_declared_part(*bpv[j])) {
        // *out << "Bucket " << i << " is in Element Block:  " << bpv[j]->name()
        //      << "  and has " << buckets[i]->size() << " elements." << std::endl;
        wsEBNames[i]=bpv[j]->name();
      }
    }
  }

  wsPhysIndex.resize(numBuckets);
  if (stkMeshStruct->allElementBlocksHaveSamePhysics)
    for (int i=0; i<numBuckets; i++) wsPhysIndex[i]=0;
  else
    for (int i=0; i<numBuckets; i++) wsPhysIndex[i]=stkMeshStruct->ebNameToIndex[wsEBNames[i]];

  // Fill  wsElNodeEqID(workset, el_LID, local node, Eq) => unk_LID

  wsElNodeEqID.resize(numBuckets);
  wsElNodeID.resize(numBuckets);
  coords.resize(numBuckets);
  sphereVolume.resize(numBuckets);

  nodesOnElemStateVec.resize(numBuckets);
  stateArrays.elemStateArrays.resize(numBuckets);
  const Albany::StateInfoStruct& nodal_states = stkMeshStruct->getFieldContainer()->getNodalSIS();

  // Clear map if remeshing
  if(!elemGIDws.empty()) elemGIDws.clear();

  typedef stk::mesh::Cartesian NodeTag;
  typedef stk::mesh::Cartesian ElemTag;
  typedef stk::mesh::Cartesian CompTag;

  NodalDOFsStructContainer::MapOfDOFsStructs::iterator it;
  NodalDOFsStructContainer::MapOfDOFsStructs& mapOfDOFsStructs = nodalDOFsStructContainer.mapOfDOFsStructs;
  for(it = mapOfDOFsStructs.begin(); it != mapOfDOFsStructs.end(); ++it) {
    it->second.wsElNodeID.resize(numBuckets);
    it->second.wsElNodeID_rawVec.resize(numBuckets);
  }
  
  for (int b=0; b < numBuckets; b++) {

    stk::mesh::Bucket& buck = *buckets[b];
    wsElNodeEqID[b].resize(buck.size());
    wsElNodeID[b].resize(buck.size());
    coords[b].resize(buck.size());


    {  //nodalDataToElemNode.

      nodesOnElemStateVec[b].resize(nodal_states.size());

      for (int is=0; is< nodal_states.size(); ++is) {
        const std::string& name = nodal_states[is]->name;
        const Albany::StateStruct::FieldDims& dim = nodal_states[is]->dim;
        MDArray& array = stateArrays.elemStateArrays[b][name];
        std::vector<double>& stateVec = nodesOnElemStateVec[b][is];
        int dim0 = buck.size(); //may be different from dim[0];
        switch (dim.size()) {
        case 2:     //scalar
        {
          const ScalarFieldType& field = *metaData.get_field<ScalarFieldType>(stk::topology::NODE_RANK, name);
          stateVec.resize(dim0*dim[1]);
          array.assign<ElemTag, NodeTag>(stateVec.data(),dim0,dim[1]);
          for (int i=0; i < dim0; i++) {
            stk::mesh::Entity element = buck[i];
            stk::mesh::Entity const* rel = bulkData.begin_nodes(element);
            for (int j=0; j < dim[1]; j++) {
              stk::mesh::Entity rowNode = rel[j];
              array(i,j) = *stk::mesh::field_data(field, rowNode);
            }
          }
          break;
        }
        case 3:  //vector
        {
          const VectorFieldType& field = *metaData.get_field<VectorFieldType>(stk::topology::NODE_RANK, name);
          stateVec.resize(dim0*dim[1]*dim[2]);
          array.assign<ElemTag, NodeTag,CompTag>(stateVec.data(),dim0,dim[1],dim[2]);
          for (int i=0; i < dim0; i++) {
            stk::mesh::Entity element = buck[i];
            stk::mesh::Entity const* rel = bulkData.begin_nodes(element);
            for (int j=0; j < dim[1]; j++) {
              stk::mesh::Entity rowNode = rel[j];
              double* entry = stk::mesh::field_data(field, rowNode);
              for(int k=0; k<dim[2]; k++)
                array(i,j,k) = entry[k];
            }
          }
          break;
        }
        case 4: //tensor
        {
          const TensorFieldType& field = *metaData.get_field<TensorFieldType>(stk::topology::NODE_RANK, name);
          stateVec.resize(dim0*dim[1]*dim[2]*dim[3]);
          array.assign<ElemTag, NodeTag, CompTag, CompTag>(stateVec.data(),dim0,dim[1],dim[2],dim[3]);
          for (int i=0; i < dim0; i++) {
            stk::mesh::Entity element = buck[i];
            stk::mesh::Entity const* rel = bulkData.begin_nodes(element);
            for (int j=0; j < dim[1]; j++) {
              stk::mesh::Entity rowNode = rel[j];
              double* entry = stk::mesh::field_data(field, rowNode);
              for(int k=0; k<dim[2]; k++)
                for(int l=0; l<dim[3]; l++)
                  array(i,j,k,l) = entry[k*dim[3]+l]; //check this, is stride Correct?
            }
          }
          break;
        }
        }
      }
    }


#ifdef ALBANY_LCM
    if(stkMeshStruct->getFieldContainer()->hasSphereVolumeField())
      sphereVolume[b].resize(buck.size());
#endif

    // i is the element index within bucket b

    stk::mesh::Entity element = buck[0];
    int nodes_per_element = bulkData.num_nodes(element);
    for(it = mapOfDOFsStructs.begin(); it != mapOfDOFsStructs.end(); ++it) {
      int nComp = it->first.second;
      it->second.wsElNodeID_rawVec[b].resize(buck.size()*nodes_per_element*nComp);
      it->second.wsElNodeID[b].assign<ElemTag, NodeTag, CompTag>(it->second.wsElNodeID_rawVec[b].data(),(int)buck.size(),nodes_per_element,nComp);
    }

    for (std::size_t i=0; i < buck.size(); i++) {

      // Traverse all the elements in this bucket
      stk::mesh::Entity element = buck[i];

      // Now, save a map from element GID to workset on this PE
      elemGIDws[gid(element)].ws = b;

      // Now, save a map from element GID to local id on this workset on this PE
      elemGIDws[gid(element)].LID = i;

      stk::mesh::Entity const* node_rels = bulkData.begin_nodes(element);
      const int nodes_per_element = bulkData.num_nodes(element);

      wsElNodeEqID[b][i].resize(nodes_per_element);
      wsElNodeID[b][i].resize(nodes_per_element);
      coords[b][i].resize(nodes_per_element);
 
      for(it = mapOfDOFsStructs.begin(); it != mapOfDOFsStructs.end(); ++it) {
        IDArray& wsElNodeID_array = it->second.wsElNodeID[b];
        int nComp = it->first.second;
          for (int j=0; j < nodes_per_element; j++)
          {
            stk::mesh::Entity node = node_rels[j];
            for (int k=0; k < nComp; k++) {
              int gid = it->second.overlap_dofManager.getGlobalDOF(node,k);
              int node_lid = it->second.overlap_map->LID(gid);
                wsElNodeID_array((int)i,j,k) = node_lid;
            }
          }
      }

#ifdef ALBANY_LCM
      if(stkMeshStruct->getFieldContainer()->hasSphereVolumeField() && nodes_per_element == 1)
	sphereVolume[b][i] = *stk::mesh::field_data(*sphereVolume_field, element);
#endif

      // loop over local nodes
      IDArray& node_array = mapOfDOFsStructs[make_pair(std::string(""),1)].wsElNodeID[b];
      IDArray& sol_array = mapOfDOFsStructs[make_pair(std::string(""),neq)].wsElNodeID[b];
      for (int j=0; j < nodes_per_element; j++) {
        stk::mesh::Entity rowNode = node_rels[j];
        int node_gid = gid(rowNode);
        int node_lid = overlap_node_map->LID(node_gid);

        TEUCHOS_TEST_FOR_EXCEPTION(node_lid<0, std::logic_error,
			   "STK1D_Disc: node_lid out of range " << node_lid << std::endl);
        coords[b][i][j] = stk::mesh::field_data(*coordinates_field, rowNode);

        wsElNodeEqID[b][i][j].resize(neq);
        wsElNodeID[b][i][j] =node_array((int)i,j,0);

        for (int eq=0; eq < neq; eq++)
          wsElNodeEqID[b][i][j][eq] = sol_array((int)i,j,eq);
      }
    }
  }

  for (int d=0; d<stkMeshStruct->numDim; d++) {
  if (stkMeshStruct->PBCStruct.periodic[d]) {
    for (int b=0; b < numBuckets; b++) {
      for (std::size_t i=0; i < buckets[b]->size(); i++) {
        int nodes_per_element = buckets[b]->num_nodes(i);
        bool anyXeqZero=false;
        for (int j=0; j < nodes_per_element; j++)  if (coords[b][i][j][d]==0.0) anyXeqZero=true;
        if (anyXeqZero)  {
          bool flipZeroToScale=false;
          for (int j=0; j < nodes_per_element; j++)
              if (coords[b][i][j][d] > stkMeshStruct->PBCStruct.scale[d]/1.9) flipZeroToScale=true;
          if (flipZeroToScale) {
            for (int j=0; j < nodes_per_element; j++)  {
              if (coords[b][i][j][d] == 0.0) {
                double* xleak = new double [stkMeshStruct->numDim];
                for (int k=0; k < stkMeshStruct->numDim; k++)
                  if (k==d) xleak[d]=stkMeshStruct->PBCStruct.scale[d];
                  else xleak[k] = coords[b][i][j][k];
                std::string transformType = stkMeshStruct->transformType;
                double alpha = stkMeshStruct->felixAlpha;
                alpha *= pi/180.; //convert alpha, read in from ParameterList, to radians
                if ((transformType=="ISMIP-HOM Test A" || transformType == "ISMIP-HOM Test B" ||
                     transformType=="ISMIP-HOM Test C" || transformType == "ISMIP-HOM Test D") && d==0) {
                    xleak[2] -= stkMeshStruct->PBCStruct.scale[d]*tan(alpha);
                    StateArray::iterator sHeight = stateArrays.elemStateArrays[b].find("surface_height");
                    if(sHeight != stateArrays.elemStateArrays[b].end())
               	      sHeight->second(int(i),j) -= stkMeshStruct->PBCStruct.scale[d]*tan(alpha);
                }
                coords[b][i][j] = xleak; // replace ptr to coords
                toDelete.push_back(xleak);
              }
            }
          }
        }
      }
    }
  }
  }

  typedef Albany::AbstractSTKFieldContainer::ScalarValueState ScalarValueState;
  typedef Albany::AbstractSTKFieldContainer::QPScalarState QPScalarState ;
  typedef Albany::AbstractSTKFieldContainer::QPVectorState QPVectorState;
  typedef Albany::AbstractSTKFieldContainer::QPTensorState QPTensorState;
  typedef Albany::AbstractSTKFieldContainer::QPTensor3State QPTensor3State;

  typedef Albany::AbstractSTKFieldContainer::ScalarState ScalarState ;
  typedef Albany::AbstractSTKFieldContainer::VectorState VectorState;
  typedef Albany::AbstractSTKFieldContainer::TensorState TensorState;

  // Pull out pointers to shards::Arrays for every bucket, for every state
  // Code is data-type dependent

  ScalarValueState scalarValue_states = stkMeshStruct->getFieldContainer()->getScalarValueStates();
  QPScalarState qpscalar_states = stkMeshStruct->getFieldContainer()->getQPScalarStates();
  QPVectorState qpvector_states = stkMeshStruct->getFieldContainer()->getQPVectorStates();
  QPTensorState qptensor_states = stkMeshStruct->getFieldContainer()->getQPTensorStates();
  QPTensor3State qptensor3_states = stkMeshStruct->getFieldContainer()->getQPTensor3States();
  std::map<std::string, double>& time = stkMeshStruct->getFieldContainer()->getTime();

  for (std::size_t b=0; b < buckets.size(); b++) {
    stk::mesh::Bucket& buck = *buckets[b];
    for (QPScalarState::iterator qpss = qpscalar_states.begin();
              qpss != qpscalar_states.end(); ++qpss){
      BucketArray<Albany::AbstractSTKFieldContainer::QPScalarFieldType> array(**qpss, buck);
//Debug
//std::cout << "Buck.size(): " << buck.size() << " QPSFT dim[1]: " << array.dimension(1) << std::endl;
      MDArray ar = array;
      stateArrays.elemStateArrays[b][(*qpss)->name()] = ar;
    }
    for (QPVectorState::iterator qpvs = qpvector_states.begin();
              qpvs != qpvector_states.end(); ++qpvs){
      BucketArray<Albany::AbstractSTKFieldContainer::QPVectorFieldType> array(**qpvs, buck);
//Debug
//std::cout << "Buck.size(): " << buck.size() << " QPVFT dim[2]: " << array.dimension(2) << std::endl;
      MDArray ar = array;
      stateArrays.elemStateArrays[b][(*qpvs)->name()] = ar;
    }
    for (QPTensorState::iterator qpts = qptensor_states.begin();
              qpts != qptensor_states.end(); ++qpts){
      BucketArray<Albany::AbstractSTKFieldContainer::QPTensorFieldType> array(**qpts, buck);
//Debug
//std::cout << "Buck.size(): " << buck.size() << " QPTFT dim[3]: " << array.dimension(3) << std::endl;
      MDArray ar = array;
      stateArrays.elemStateArrays[b][(*qpts)->name()] = ar;
    }
    for (QPTensor3State::iterator qpts = qptensor3_states.begin();
              qpts != qptensor3_states.end(); ++qpts){
      BucketArray<Albany::AbstractSTKFieldContainer::QPTensor3FieldType> array(**qpts, buck);
//Debug
//std::cout << "Buck.size(): " << buck.size() << " QPT3FT dim[4]: " << array.dimension(4) << std::endl;
      MDArray ar = array;
      stateArrays.elemStateArrays[b][(*qpts)->name()] = ar;
    }
    for (ScalarValueState::iterator svs = scalarValue_states.begin();
              svs != scalarValue_states.end(); ++svs){
      const int size = 1;
      shards::Array<double, shards::NaturalOrder, Cell> array(&time[*svs], size);
      MDArray ar = array;
//Debug
//std::cout << "Buck.size(): " << buck.size() << " SVState dim[0]: " << array.dimension(0) << std::endl;
//std::cout << "SV Name: " << *svs << " address : " << &array << std::endl;
      stateArrays.elemStateArrays[b][*svs] = ar;
    }
  }

// Process node data sets if present

  if(Teuchos::nonnull(stkMeshStruct->nodal_data_block)){

    Teuchos::RCP<Albany::NodeFieldContainer> node_states = stkMeshStruct->nodal_data_block->getNodeContainer();

    stk::mesh::BucketVector const& node_buckets = bulkData.get_buckets( stk::topology::NODE_RANK, select_owned_in_part );

    const size_t numNodeBuckets = node_buckets.size();

    stateArrays.nodeStateArrays.resize(numNodeBuckets);
    for (std::size_t b=0; b < numNodeBuckets; b++) {
      stk::mesh::Bucket& buck = *node_buckets[b];
      for (Albany::NodeFieldContainer::iterator nfs = node_states->begin();
                nfs != node_states->end(); ++nfs){
        stateArrays.nodeStateArrays[b][(*nfs).first] =
             Teuchos::rcp_dynamic_cast<Albany::AbstractSTKNodeFieldContainer>((*nfs).second)->getMDA(buck);
      }
    }
  }
}

void Albany::STKDiscretization::computeSideSets(){

  // Clean up existing sideset structure if remeshing

  for(int i = 0; i < sideSets.size(); i++)
    sideSets[i].clear(); // empty the ith map

  const stk::mesh::EntityRank element_rank = stk::topology::ELEMENT_RANK;

  // iterator over all side_rank parts found in the mesh
  std::map<std::string, stk::mesh::Part*>::iterator ss = stkMeshStruct->ssPartVec.begin();

  int numBuckets = wsEBNames.size();

  sideSets.resize(numBuckets); // Need a sideset list per workset

  while ( ss != stkMeshStruct->ssPartVec.end() ) {

    // Get all owned sides in this side set
    stk::mesh::Selector select_owned_in_sspart =

      // get only entities in the ss part (ss->second is the current sideset part)
      stk::mesh::Selector( *(ss->second) ) &
      // and only if the part is local
      stk::mesh::Selector( metaData.locally_owned_part() );

    std::vector< stk::mesh::Entity > sides ;
    stk::mesh::get_selected_entities( select_owned_in_sspart , // sides local to this processor
				      bulkData.buckets( metaData.side_rank() ) ,
				      sides ); // store the result in "sides"

    *out << "STKDisc: sideset "<< ss->first <<" has size " << sides.size() << "  on Proc 0." << std::endl;

    // loop over the sides to see what they are, then fill in the data holder
    // for side set options, look at $TRILINOS_DIR/packages/stk/stk_usecases/mesh/UseCase_13.cpp

    for (std::size_t localSideID=0; localSideID < sides.size(); localSideID++) {

      stk::mesh::Entity sidee = sides[localSideID];

      TEUCHOS_TEST_FOR_EXCEPTION(bulkData.num_elements(sidee) != 1, std::logic_error,
                                 "STKDisc: cannot figure out side set topology for side set " << ss->first << std::endl);

      stk::mesh::Entity elem = bulkData.begin_elements(sidee)[0];

      // containing the side. Note that if the side is internal, it will show up twice in the
      // element list, once for each element that contains it.

      SideStruct sStruct;

      // Save elem id. This is the global element id
      sStruct.elem_GID = gid(elem);

      int workset = elemGIDws[sStruct.elem_GID].ws; // Get the ws that this element lives in

      // Save elem id. This is the local element id within the workset
      sStruct.elem_LID = elemGIDws[sStruct.elem_GID].LID;

      // Save the side identifier inside of the element. This starts at zero here.
      sStruct.side_local_id = determine_local_side_id(elem, sidee);

      // Save the index of the element block that this elem lives in
      sStruct.elem_ebIndex = stkMeshStruct->ebNameToIndex[wsEBNames[workset]];

      SideSetList& ssList = sideSets[workset];   // Get a ref to the side set map for this ws
      SideSetList::iterator it = ssList.find(ss->first); // Get an iterator to the correct sideset (if
                                                                // it exists)

      if(it != ssList.end()) // The sideset has already been created

        it->second.push_back(sStruct); // Save this side to the vector that belongs to the name ss->first

      else { // Add the key ss->first to the map, and the side vector to that map

        std::vector<SideStruct> tmpSSVec;
        tmpSSVec.push_back(sStruct);

        ssList.insert(SideSetList::value_type(ss->first, tmpSSVec));

      }

    }

    ss++;
  }
}

unsigned
Albany::STKDiscretization::determine_local_side_id( const stk::mesh::Entity elem , stk::mesh::Entity side ) {

  using namespace stk;

  stk::topology elem_top = bulkData.bucket(elem).topology();

  const unsigned num_elem_nodes = bulkData.num_nodes(elem);
  const unsigned num_side_nodes = bulkData.num_nodes(side);

  stk::mesh::Entity const* elem_nodes = bulkData.begin_nodes(elem);
  stk::mesh::Entity const* side_nodes = bulkData.begin_nodes(side);

  const stk::topology::rank_t side_rank = metaData.side_rank();

  int side_id = -1 ;

  if(num_elem_nodes == 0 || num_side_nodes == 0){ // Node relations are not present, look at elem->face

    const unsigned num_sides = bulkData.num_connectivity(elem, side_rank);
    stk::mesh::Entity const* elem_sides = bulkData.begin(elem, side_rank);

    for ( unsigned i = 0 ; i < num_sides ; ++i ) {

      const stk::mesh::Entity elem_side = elem_sides[i];

      if (bulkData.identifier(elem_side) == bulkData.identifier(side)){ // Found the local side in the element

         side_id = static_cast<int>(i);

         return side_id;
      }

    }

    if ( side_id < 0 ) {
      std::ostringstream msg ;
      msg << "determine_local_side_id( " ;
      msg << elem_top.name() ;
      msg << " , Element[ " ;
      msg << bulkData.identifier(elem);
      msg << " ]{" ;
      for ( unsigned i = 0 ; i < num_sides ; ++i ) {
        msg << " " << bulkData.identifier(elem_sides[i]);
      }
      msg << " } , Side[ " ;
      msg << bulkData.identifier(side);
      msg << " ] ) FAILED" ;
      throw std::runtime_error( msg.str() );
    }

  }
  else { // Conventional elem->node - side->node connectivity present

    std::vector<unsigned> side_map;
    for ( unsigned i = 0 ; side_id == -1 && i < elem_top.num_sides() ; ++i ) {
      stk::topology side_top    = elem_top.side_topology(i);
      side_map.clear();
      elem_top.side_node_ordinals(i, std::back_inserter(side_map));

      if ( num_side_nodes == side_top.num_nodes() ) {

        side_id = i ;

        for ( unsigned j = 0 ;
              side_id == static_cast<int>(i) && j < side_top.num_nodes() ; ++j ) {

          stk::mesh::Entity elem_node = elem_nodes[ side_map[j] ];

          bool found = false ;

          for ( unsigned k = 0 ; ! found && k < side_top.num_nodes() ; ++k ) {
            found = elem_node == side_nodes[k];
          }

          if ( ! found ) { side_id = -1 ; }
        }
      }
    }

    if ( side_id < 0 ) {
      std::ostringstream msg ;
      msg << "determine_local_side_id( " ;
      msg << elem_top.name() ;
      msg << " , Element[ " ;
      msg << bulkData.identifier(elem);
      msg << " ]{" ;
      for ( unsigned i = 0 ; i < num_elem_nodes ; ++i ) {
        msg << " " << bulkData.identifier(elem_nodes[i]);
      }
      msg << " } , Side[ " ;
      msg << bulkData.identifier(side);
      msg << " ]{" ;
      for ( unsigned i = 0 ; i < num_side_nodes ; ++i ) {
        msg << " " << bulkData.identifier(side_nodes[i]);
      }
      msg << " } ) FAILED" ;
      throw std::runtime_error( msg.str() );
    }
  }

  return static_cast<unsigned>(side_id) ;
}

void Albany::STKDiscretization::computeNodeSets()
{

  std::map<std::string, stk::mesh::Part*>::iterator ns = stkMeshStruct->nsPartVec.begin();
  AbstractSTKFieldContainer::VectorFieldType* coordinates_field = stkMeshStruct->getCoordinatesField();

  while ( ns != stkMeshStruct->nsPartVec.end() ) { // Iterate over Node Sets
    // Get all owned nodes in this node set
    stk::mesh::Selector select_owned_in_nspart =
      stk::mesh::Selector( *(ns->second) ) &
      stk::mesh::Selector( metaData.locally_owned_part() );

    std::vector< stk::mesh::Entity > nodes ;
    stk::mesh::get_selected_entities( select_owned_in_nspart ,
				      bulkData.buckets( stk::topology::NODE_RANK ) ,
				      nodes );

    nodeSets[ns->first].resize(nodes.size());
    nodeSetCoords[ns->first].resize(nodes.size());
//    nodeSetIDs.push_back(ns->first); // Grab string ID
    *out << "STKDisc: nodeset "<< ns->first <<" has size " << nodes.size() << "  on Proc 0." << std::endl;
    for (std::size_t i=0; i < nodes.size(); i++) {
      int node_gid = gid(nodes[i]);
      int node_lid = node_map->LID(node_gid);
      nodeSets[ns->first][i].resize(neq);
      for (std::size_t eq=0; eq < neq; eq++)  nodeSets[ns->first][i][eq] = getOwnedDOF(node_lid,eq);
      nodeSetCoords[ns->first][i] = stk::mesh::field_data(*coordinates_field, nodes[i]);
    }
    ns++;
  }
}

void Albany::STKDiscretization::setupExodusOutput()
{
#ifdef ALBANY_SEACAS
  if (stkMeshStruct->exoOutput) {

    outputInterval = 0;

    std::string str = stkMeshStruct->exoOutFile;

    Ioss::Init::Initializer io;
    mesh_data = new stk::io::StkMeshIoBroker(Albany::getMpiCommFromEpetraComm(*comm));
    mesh_data->set_bulk_data(bulkData);
    outputFileIdx = mesh_data->create_output_mesh(str, stk::io::WRITE_RESULTS);

    const stk::mesh::FieldVector &fields = mesh_data->meta_data().get_fields();
    for (size_t i=0; i < fields.size(); i++) {
      // Hacky, but doesn't appear to be a way to query if a field is already
      // going to be output.
      try {
        mesh_data->add_field(outputFileIdx, *fields[i]);
      }
      catch (std::runtime_error const&) { }
    }
  }
#else
  if (stkMeshStruct->exoOutput)
    *out << "\nWARNING: exodus output requested but SEACAS not compiled in:"
         << " disabling exodus output \n" << std::endl;

#endif
}

namespace {
  const std::vector<double> spherical_to_cart(const std::pair<double, double> & sphere){
    const double radius_of_earth = 1;
    std::vector<double> cart(3);

    cart[0] = radius_of_earth*std::cos(sphere.first)*std::cos(sphere.second);
    cart[1] = radius_of_earth*std::cos(sphere.first)*std::sin(sphere.second);
    cart[2] = radius_of_earth*std::sin(sphere.first);

    return cart;
  }
  double distance (const double* x, const double* y) {
    const double d = std::sqrt((x[0]-y[0])*(x[0]-y[0]) +
                               (x[1]-y[1])*(x[1]-y[1]) +
                               (x[2]-y[2])*(x[2]-y[2]));
    return d;
  }
  double distance (const std::vector<double> &x, const std::vector<double> &y) {
    const double d = std::sqrt((x[0]-y[0])*(x[0]-y[0]) +
                               (x[1]-y[1])*(x[1]-y[1]) +
                               (x[2]-y[2])*(x[2]-y[2]));
    return d;
  }

  bool point_inside(const Teuchos::ArrayRCP<double*> &coords,
                    const std::vector<double>        &sphere_xyz) {
    // first check if point is near the element:
    const double  tol_inside = 1e-12;
    const double elem_diam = std::max(::distance(coords[0],coords[2]), ::distance(coords[1],coords[3]));
    std::vector<double> center(3,0);
    for (unsigned i=0; i<4; ++i)
      for (unsigned j=0; j<3; ++j) center[j] += coords[i][j];
    for (unsigned j=0; j<3; ++j) center[j] /= 4;
    bool inside = true;

    if ( ::distance(&center[0],&sphere_xyz[0]) > 1.0*elem_diam ) inside = false;

    unsigned j=3;
    for (unsigned i=0; i<4 && inside; ++i) {
      std::vector<double> cross(3);
      // outward normal to plane containing j->i edge:  corner(i) x corner(j)
      // sphere dot (corner(i) x corner(j) ) = negative if inside
      cross[0]=  coords[i][1]*coords[j][2] - coords[i][2]*coords[j][1];
      cross[1]=-(coords[i][0]*coords[j][2] - coords[i][2]*coords[j][0]);
      cross[2]=  coords[i][0]*coords[j][1] - coords[i][1]*coords[j][0];
      j = i;
      const double dotprod = cross[0]*sphere_xyz[0] +
                             cross[1]*sphere_xyz[1] +
                             cross[2]*sphere_xyz[2];

      // dot product is proportional to elem_diam. positive means outside,
      // but allow machine precision tolorence:
      if (tol_inside*elem_diam < dotprod) inside = false;
    }
    return inside;
  }


  const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > >
  Basis(const int C) {
    TEUCHOS_TEST_FOR_EXCEPTION(C!=4 && C!=9, std::logic_error,
      " Albany_STKDiscretization Error Basis not linear or quad"<<std::endl);
    static const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > > HGRAD_Basis_4 =
      Teuchos::rcp( new Intrepid::Basis_HGRAD_QUAD_C1_FEM<double, Intrepid::FieldContainer<double> >() );
    static const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > > HGRAD_Basis_9 =
      Teuchos::rcp( new Intrepid::Basis_HGRAD_QUAD_C2_FEM<double, Intrepid::FieldContainer<double> >() );
    return C==4 ? HGRAD_Basis_4 : HGRAD_Basis_9;
  }

  double value(const std::vector<double> &soln,
               const std::pair<double, double> &ref) {

    const int C = soln.size();
    const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > > HGRAD_Basis = Basis(C);

    const int numPoints        = 1;
    Intrepid::FieldContainer<double> basisVals (C, numPoints);
    Intrepid::FieldContainer<double> tempPoints(numPoints, 2);
    tempPoints(0,0) = ref.first;
    tempPoints(0,1) = ref.second;

    HGRAD_Basis->getValues(basisVals, tempPoints, Intrepid::OPERATOR_VALUE);

    double x = 0;
    for (unsigned j=0; j<C; ++j) x += soln[j] * basisVals(j,0);
    return x;
  }

  void value(double x[3],
             const Teuchos::ArrayRCP<double*> &coords,
             const std::pair<double, double> &ref){

    const int C = coords.size();
    const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > > HGRAD_Basis = Basis(C);

    const int numPoints        = 1;
    Intrepid::FieldContainer<double> basisVals (C, numPoints);
    Intrepid::FieldContainer<double> tempPoints(numPoints, 2);
    tempPoints(0,0) = ref.first;
    tempPoints(0,1) = ref.second;

    HGRAD_Basis->getValues(basisVals, tempPoints, Intrepid::OPERATOR_VALUE);

    for (unsigned i=0; i<3; ++i) x[i] = 0;
    for (unsigned i=0; i<3; ++i)
      for (unsigned j=0; j<C; ++j)
        x[i] += coords[j][i] * basisVals(j,0);
  }

  void grad(double x[3][2],
             const Teuchos::ArrayRCP<double*> &coords,
             const std::pair<double, double> &ref){

    const int C = coords.size();
    const Teuchos::RCP<Intrepid::Basis<double, Intrepid::FieldContainer<double> > > HGRAD_Basis = Basis(C);

    const int numPoints        = 1;
    Intrepid::FieldContainer<double> basisGrad (C, numPoints, 2);
    Intrepid::FieldContainer<double> tempPoints(numPoints, 2);
    tempPoints(0,0) = ref.first;
    tempPoints(0,1) = ref.second;

    HGRAD_Basis->getValues(basisGrad, tempPoints, Intrepid::OPERATOR_GRAD);

    for (unsigned i=0; i<3; ++i) x[i][0] = x[i][1] = 0;
    for (unsigned i=0; i<3; ++i)
      for (unsigned j=0; j<C; ++j) {
        x[i][0] += coords[j][i] * basisGrad(j,0,0);
        x[i][1] += coords[j][i] * basisGrad(j,0,1);
      }
  }

  std::pair<double, double>  ref2sphere(const Teuchos::ArrayRCP<double*> &coords,
                                        const std::pair<double, double> &ref) {

    static const double DIST_THRESHOLD= 1.0e-9;

    double x[3];
    value(x,coords,ref);

    const double r = std::sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);

    for (unsigned i=0; i<3; ++i) x[i] /= r;

    std::pair<double, double> sphere(std::asin(x[2]), std::atan2(x[1],x[0]));

    // ==========================================================
    // enforce three facts:
    //
    // 1) lon at poles is defined to be zero
    //
    // 2) Grid points must be separated by about .01 Meter (on earth)
    //   from pole to be considered "not the pole".
    //
    // 3) range of lon is { 0<= lon < 2*PI }
    //
    // ==========================================================

    if (std::abs(std::abs(sphere.first)-pi/2) < DIST_THRESHOLD) sphere.second = 0;
    else if (sphere.second < 0) sphere.second += 2*pi;

    return sphere;
  }

  void Dmap(const Teuchos::ArrayRCP<double*> &coords,
            const std::pair<double, double>  &sphere,
            const std::pair<double, double>  &ref,
            double D[][2]) {

    const double th     = sphere.first;
    const double lam    = sphere.second;
    const double sinlam = std::sin(lam);
    const double sinth  = std::sin(th);
    const double coslam = std::cos(lam);
    const double costh  = std::cos(th);

    const double D1[2][3] = {{-sinlam, coslam, 0},
                             {      0,      0, 1}};

    const double D2[3][3] = {{ sinlam*sinlam*costh*costh+sinth*sinth, -sinlam*coslam*costh*costh,             -coslam*sinth*costh},
                             {-sinlam*coslam*costh*costh,              coslam*coslam*costh*costh+sinth*sinth, -sinlam*sinth*costh},
                             {-coslam*sinth,                          -sinlam*sinth,                                        costh}};

    double D3[3][2] = {0};
    grad(D3,coords,ref);

    double D4[3][2] = {0};
    for (unsigned i=0; i<3; ++i)
      for (unsigned j=0; j<2; ++j)
        for (unsigned k=0; k<3; ++k)
           D4[i][j] += D2[i][k] * D3[k][j];

    for (unsigned i=0; i<2; ++i)
      for (unsigned j=0; j<2; ++j) D[i][j] = 0;

    for (unsigned i=0; i<2; ++i)
      for (unsigned j=0; j<2; ++j)
        for (unsigned k=0; k<3; ++k)
          D[i][j] += D1[i][k] * D4[k][j];

  }

  std::pair<double, double> parametric_coordinates(const Teuchos::ArrayRCP<double*> &coords,
                                                   const std::pair<double, double>  &sphere) {

    static const double tol_sq = 1e-26;
    static const unsigned MAX_NR_ITER = 10;
    double costh = std::cos(sphere.first);
    double D[2][2], Dinv[2][2];
    double resa = 1;
    double resb = 1;
    std::pair<double, double> ref(0,0); // initial guess is center of element.

    for (unsigned i=0; i<MAX_NR_ITER && tol_sq < (costh*resb*resb + resa*resa) ; ++i) {
      const std::pair<double, double> sph = ref2sphere(coords,ref);
      resa = sph.first  - sphere.first;
      resb = sph.second - sphere.second;

      if (resb >  pi) resb -= 2*pi;
      if (resb < -pi) resb += 2*pi;

      Dmap(coords, sph, ref, D);
      const double detD = D[0][0]*D[1][1] - D[0][1]*D[1][0];
      Dinv[0][0] =  D[1][1]/detD;
      Dinv[0][1] = -D[0][1]/detD;
      Dinv[1][0] = -D[1][0]/detD;
      Dinv[1][1] =  D[0][0]/detD;

      const std::pair<double, double> del( Dinv[0][0]*costh*resb + Dinv[0][1]*resa,
                                           Dinv[1][0]*costh*resb + Dinv[1][1]*resa);
      ref.first  -= del.first;
      ref.second -= del.second;
    }
    return ref;
  }

  const std::pair<bool,std::pair<unsigned, unsigned> >point_in_element(const std::pair<double, double> &sphere,
      const Albany::WorksetArray<Teuchos::ArrayRCP<Teuchos::ArrayRCP<double*> > >::type& coords,
      std::pair<double, double> &parametric) {
    const std::vector<double> sphere_xyz = spherical_to_cart(sphere);
    std::pair<bool,std::pair<unsigned, unsigned> > element(false,std::pair<unsigned, unsigned>(0,0));
    for (unsigned i=0; i<coords.size() && !element.first; ++i) {
      for (unsigned j=0; j<coords[i].size() && !element.first; ++j) {
        const bool found =  point_inside(coords[i][j], sphere_xyz);
        if (found) {
          parametric = parametric_coordinates(coords[i][j], sphere);
          if (parametric.first  < -1) parametric.first  = -1;
          if (parametric.second < -1) parametric.second = -1;
          if (1 < parametric.first  ) parametric.first  =  1;
          if (1 < parametric.second ) parametric.second =  1;
          element.first         = true;
          element.second.first  = i;
          element.second.second = j;
        }
      }
    }
    return element;
  }

  void setup_latlon_interp(
    const unsigned nlat, const double nlon,
    const Albany::WorksetArray<Teuchos::ArrayRCP<Teuchos::ArrayRCP<double*> > >::type& coords,
    Albany::WorksetArray<Teuchos::ArrayRCP<std::vector<Albany::STKDiscretization::interp> > >::type& interpdata,
    const Teuchos::RCP<const Epetra_Comm> comm) {

    double err=0;
    const long long unsigned rank = comm->MyPID();
    std::vector<double> lat(nlat);
    std::vector<double> lon(nlon);

    unsigned count=0;
    for (unsigned i=0; i<nlat; ++i) lat[i] = -pi/2 + i*pi/(nlat-1);
    for (unsigned j=0; j<nlon; ++j) lon[j] =       2*j*pi/nlon;
    for (unsigned i=0; i<nlat; ++i) {
      for (unsigned j=0; j<nlon; ++j) {
        const std::pair<double, double> sphere(lat[i],lon[j]);
        std::pair<double, double> paramtric;
        const std::pair<bool,std::pair<unsigned, unsigned> >element = point_in_element(sphere, coords, paramtric);
        if (element.first) {
          // compute error: map 'cart' back to sphere and compare with original
          // interpolation point:
          const unsigned b = element.second.first ;
          const unsigned e = element.second.second;
          const std::vector<double> sphere2_xyz = spherical_to_cart(ref2sphere(coords[b][e], paramtric));
          const std::vector<double> sphere_xyz  = spherical_to_cart(sphere);
          err = std::max(err, ::distance(&sphere2_xyz[0],&sphere_xyz[0]));
          Albany::STKDiscretization::interp interp;
          interp.parametric_coords = paramtric;
          interp.latitude_longitude = std::pair<unsigned,unsigned>(i,j);
          interpdata[b][e].push_back(interp);
          ++count;
        }
      }
      if (!rank && (!(i%64) || i==nlat-1)) std::cout<< "Finished Latitude "<<i<<" of "<<nlat<<std::endl;
    }
    if (!rank) std::cout<<"Max interpolation point search error: "<<err<<std::endl;
  }
}

int Albany::STKDiscretization::processNetCDFOutputRequest(const Epetra_Vector& solution_field) {
#ifdef ALBANY_SEACAS
  const long long unsigned rank = comm->MyPID();
  const unsigned nlat = stkMeshStruct->nLat;
  const unsigned nlon = stkMeshStruct->nLon;

  std::vector<double> local(nlat*nlon*neq, -std::numeric_limits<double>::max());


  for (unsigned n=0; n<neq; ++n) {
    for (unsigned b=0; b<interpolateData.size(); ++b) {
      Teuchos::ArrayRCP<std::vector<interp> >        Interpb = interpolateData[b];
      Teuchos::ArrayRCP<Teuchos::ArrayRCP<double*> > Coordsb = coords[b];
      Teuchos::ArrayRCP<Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> > > ElNodeEqID = wsElNodeEqID[b];

      for (unsigned e=0; e<Interpb.size(); ++e) {
        const std::vector<interp>                    &interp = Interpb[e];
        Teuchos::ArrayRCP<double*>                    coordp = Coordsb[e];
        Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >    elnode = ElNodeEqID[e];

        const int C = coordp.size();
        std::vector<double> soln(C);
        for (unsigned i=0; i<C; ++i) {
          const int overlap_dof = elnode[i][n];
          soln[i] = solution_field[overlap_dof];
        }
        for (unsigned p=0; p<interp.size(); ++p) {
          Albany::STKDiscretization::interp par    = interp[p];
          const double y = value(soln, par.parametric_coords);
          std::pair<unsigned,unsigned> latlon =   par.latitude_longitude;
          local[nlon*latlon.first + latlon.second + n*nlat*nlon] = y;
        }
      }
    }
  }

  std::vector<double> global(neq*nlat*nlon);
  comm->MaxAll(&local[0], &global[0], neq*nlat*nlon);

#ifdef ALBANY_PAR_NETCDF
  const long long unsigned np   = comm->NumProc();
  const size_t start            = static_cast<size_t>((rank*nlat)/np);
  const size_t end              = static_cast<size_t>(((rank+1)*nlat)/np);
  const size_t len              = end-start;

  for (unsigned n=0; n<neq; ++n) {
    const size_t  startp[] = {netCDFOutputRequest,    0, start, 0};
    const size_t  countp[] = {1, 1, len, nlon};
    if (const int ierr = nc_put_vara_double (netCDFp, varSolns[n], startp, countp, &global[n*nlat*nlon]))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_vara_double returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
  }
#else
  if (netCDFp) {
    for (unsigned n=0; n<neq; ++n)  {
      const size_t  startp[] = {netCDFOutputRequest,    0, 0, 0};
      const size_t  countp[] = {1, 1, nlat, nlon};
      if (const int ierr = nc_put_vara_double (netCDFp, varSolns[n], startp, countp, &global[n*nlat*nlon]))
        TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
          "nc_put_vara returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
    }
  }
#endif
#endif
  return netCDFOutputRequest++;
}

void Albany::STKDiscretization::setupNetCDFOutput()
{
  const long long unsigned rank = comm->MyPID();
#ifdef ALBANY_SEACAS
  if (stkMeshStruct->cdfOutput) {
    outputInterval = 0;
    const unsigned nlat = stkMeshStruct->nLat;
    const unsigned nlon = stkMeshStruct->nLon;


    std::string str = stkMeshStruct->cdfOutFile;

    interpolateData.resize(coords.size());
    for (int b=0; b < coords.size(); b++) interpolateData[b].resize(coords[b].size());

    setup_latlon_interp(nlat, nlon, coords, interpolateData, comm);

    const std::string name = stkMeshStruct->cdfOutFile;
    netCDFp=0;
    netCDFOutputRequest=0;


#ifdef ALBANY_PAR_NETCDF
    MPI_Comm theMPIComm = Albany::getMpiCommFromEpetraComm(*comm);
    MPI_Info info;
    MPI_Info_create(&info);
    if (const int ierr = nc_create_par (name.c_str(), NC_NETCDF4 | NC_MPIIO | NC_CLOBBER | NC_64BIT_OFFSET, theMPIComm, info, &netCDFp))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_create_par returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
    MPI_Info_free(&info);
#else
    if (!rank)
    if (const int ierr = nc_create (name.c_str(), NC_CLOBBER | NC_SHARE | NC_64BIT_OFFSET | NC_CLASSIC_MODEL, &netCDFp))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_create returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
#endif

    const size_t nlev = 1;
    const char *dimnames[] = {"time","lev","lat","lon"};
    const size_t  dimlen[] = {NC_UNLIMITED, nlev, nlat, nlon};
    int dimID[4]={0,0,0,0};

    for (unsigned i=0; i<4; ++i) {
      if (netCDFp)
      if (const int ierr = nc_def_dim (netCDFp,  dimnames[i], dimlen[i], &dimID[i]))
        TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
          "nc_def_dim returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
    }
    varSolns.resize(neq,0);

    for (unsigned n=0; n<neq; ++n) {
      std::ostringstream var;
      var <<"variable_"<<n;
      const char *field_name = var.str().c_str();
      if (netCDFp)
      if (const int ierr = nc_def_var (netCDFp,  field_name, NC_DOUBLE, 4, dimID, &varSolns[n]))
        TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
          "nc_def_var "<<field_name<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

      const double fillVal = -9999.0;
      if (netCDFp)
      if (const int ierr = nc_put_att (netCDFp,  varSolns[n], "FillValue", NC_DOUBLE, 1, &fillVal))
        TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
          "nc_put_att FillValue returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
    }

    const char lat_name[] = "latitude";
    const char lat_unit[] = "degrees_north";
    const char lon_name[] = "longitude";
    const char lon_unit[] = "degrees_east";
    int latVarID=0;
      if (netCDFp)
    if (const int ierr = nc_def_var (netCDFp,  "lat", NC_DOUBLE, 1, &dimID[2], &latVarID))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_def_var lat returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
      if (netCDFp)
    if (const int ierr = nc_put_att_text (netCDFp,  latVarID, "long_name", sizeof(lat_name), lat_name))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_att_text "<<lat_name<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
      if (netCDFp)
    if (const int ierr = nc_put_att_text (netCDFp,  latVarID, "units", sizeof(lat_unit), lat_unit))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_att_text "<<lat_unit<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

    int lonVarID=0;
      if (netCDFp)
    if (const int ierr = nc_def_var (netCDFp,  "lon", NC_DOUBLE, 1, &dimID[3], &lonVarID))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_def_var lon returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
      if (netCDFp)
    if (const int ierr = nc_put_att_text (netCDFp,  lonVarID, "long_name", sizeof(lon_name), lon_name))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_att_text "<<lon_name<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
      if (netCDFp)
    if (const int ierr = nc_put_att_text (netCDFp,  lonVarID, "units", sizeof(lon_unit), lon_unit))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_att_text "<<lon_unit<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

    const char history[]="Created by Albany";
      if (netCDFp)
    if (const int ierr = nc_put_att_text (netCDFp,  NC_GLOBAL, "history", sizeof(history), history))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_att_text "<<history<<" returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

      if (netCDFp)
    if (const int ierr = nc_enddef (netCDFp))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_enddef returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

    std::vector<double> deglon(nlon);
    std::vector<double> deglat(nlat);
    for (unsigned i=0; i<nlon; ++i) deglon[i] =((      2*i*pi/nlon) *   (180/pi)) - 180;
    for (unsigned i=0; i<nlat; ++i) deglat[i] = (-pi/2 + i*pi/(nlat-1))*(180/pi);


      if (netCDFp)
    if (const int ierr = nc_put_var (netCDFp, lonVarID, &deglon[0]))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_var lon returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);
      if (netCDFp)
    if (const int ierr = nc_put_var (netCDFp, latVarID, &deglat[0]))
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "nc_put_var lat returned error code "<<ierr<<" - "<<nc_strerror(ierr)<<std::endl);

  }
#else
  if (stkMeshStruct->cdfOutput)
    *out << "\nWARNING: NetCDF output requested but SEACAS not compiled in:"
         << " disabling NetCDF output \n" << std::endl;
  stkMeshStruct->cdfOutput = false;

#endif
}

void Albany::STKDiscretization::reNameExodusOutput(std::string& filename)
{
#ifdef ALBANY_SEACAS
  if (stkMeshStruct->exoOutput && mesh_data != NULL) {

   // Delete the mesh data object and recreate it
   delete mesh_data;

   stkMeshStruct->exoOutFile = filename;

   // reset reference value for monotonic time function call as we are writing to a new file
   previous_time_label = -1.0e32;

  }
#else
  if (stkMeshStruct->exoOutput)
    *out << "\nWARNING: exodus output requested but SEACAS not compiled in:"
         << " disabling exodus output \n" << std::endl;

#endif
}

void
Albany::STKDiscretization::meshToGraph()
{
/*
  Convert the stk mesh on this processor to a nodal graph
*/

  // Elements that surround a given node, in the form of Entity *'s
  std::vector<std::vector<stk::mesh::Entity> > sur_elem;
  // numOverlapNodes are the total # of nodes seen by this pe
  // numOwnedNodes are the total # of nodes owned by this pe
  sur_elem.resize(numOverlapNodes);

  std::size_t max_nsur = 0;

  // Get the elements owned by the current processor
  stk::mesh::Selector select_owned_in_part =
    stk::mesh::Selector( metaData.universal_part() ) &
    stk::mesh::Selector( metaData.locally_owned_part() );

  const stk::mesh::BucketVector& buckets = bulkData.get_buckets( stk::topology::ELEMENT_RANK, select_owned_in_part );

  const int numBuckets = buckets.size();
  std::vector<const std::size_t *> table(numBuckets);
  std::vector<std::size_t> nconnect(numBuckets);

  for (int b=0; b < numBuckets; b++) {

    stk::mesh::Bucket& cells = *buckets[b];

    stk::topology elem_top = cells.topology();

    if (elem_top == stk::topology::HEX_8) {
       table[b] = hex_table;
       nconnect[b] = hex_nconnect;
    }
    else if (elem_top == stk::topology::TET_4) {
       table[b] = tet_table;
       nconnect[b] = tet_nconnect;
    }
    else if (elem_top == stk::topology::TRI_3) {
       table[b] = tri_table;
       nconnect[b] = tri_nconnect;
    }
    else if (elem_top == stk::topology::QUAD_4) {
       table[b] = quad_table;
       nconnect[b] = quad_nconnect;
    }
    else {
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
                                 "Error - unknown element type : " << elem_top.name()
                                 << " requested in nodal graph algorithm" << std::endl);
    }

    /* Find the surrounding elements for each node owned by this processor */
    for (std::size_t ecnt=0; ecnt < cells.size(); ecnt++) {
      stk::mesh::Entity e = cells[ecnt];
      stk::mesh::Entity const* node_rels = bulkData.begin_nodes(e);
      const size_t num_node_rels = bulkData.num_nodes(e);

      // loop over nodes within the element
      for (std::size_t ncnt=0; ncnt < num_node_rels; ncnt++) {
        stk::mesh::Entity rowNode = node_rels[ncnt];
        int nodeGID = gid(rowNode);
        int nodeLID = overlap_node_map->LID(nodeGID);

        /*
         * in the case of degenerate elements, where a node can be
         * entered into the connect table twice, need to check to
         * make sure that this element is not already listed as
         * surrounding this node
         */

        if (sur_elem[nodeLID].empty() || entity_in_list(e, sur_elem[nodeLID]) < 0) {
          /* Add the element to the list */
          sur_elem[nodeLID].push_back(e);
        }
      }
    } /* End "for(ecnt=0; ecnt < mesh->num_elems; ecnt++)" */
  } // End of loop over buckets

  for(std::size_t ncnt=0; ncnt < numOverlapNodes; ncnt++) {
    if(sur_elem[ncnt].empty()) {
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "Node = " << ncnt+1 << " has no elements" << std::endl);
    }
    else {
      std::size_t nsur = sur_elem[ncnt].size();
      if (nsur > max_nsur)
        max_nsur = nsur;
    }
  }

//end find_surrnd_elems

// find_adjacency

    // Note that the center node of a subgraph must be owned by this pe, but we want all nodes in the overlap
    // graph to be covered in the nodal graph

    /* Allocate memory necessary for the adjacency */
    nodalGraph.start.resize(numOverlapNodes + 1);
    nodalGraph.adj.clear();
    std::size_t nadj = 0;


      // loop over all the nodes owned by this PE
      for(std::size_t ncnt=0; ncnt < numOverlapNodes; ncnt++) {
//std::cout << "Center node is : " << ncnt + 1 << " num elems around it : " << sur_elem[ncnt].size() << std::endl;
        // save the starting location for the nodes surrounding ncnt
	nodalGraph.start[ncnt] = nadj;
        // loop over the elements surrounding node ncnt
	for(std::size_t ecnt=0; ecnt < sur_elem[ncnt].size(); ecnt++) {
	  stk::mesh::Entity elem   = sur_elem[ncnt][ecnt];
//std::cout << "   Element is : " << elem->identifier() << std::endl;

          stk::mesh::Entity const* node_rels = bulkData.begin_nodes(elem);
          const size_t num_node_rels = bulkData.num_nodes(elem);

          std::size_t ws = elemGIDws[gid(elem)].ws;

          // loop over the nodes in the surrounding element elem
          for (std::size_t lnode=0; lnode < num_node_rels; lnode++) {
            stk::mesh::Entity node_a = node_rels[lnode];
            // entry is the GID of each node
            std::size_t entry = gid(node_a);

            // if "entry" is not the center node AND "entry" does not appear in the current list of nodes surrounding
            // "ncnt", add "entry" to the adj list
	    if(overlap_node_map->GID(ncnt) == entry){ // entry - offset lnode - is where we are in the node
                                                      // ordering within the element

               for(std::size_t k = 0; k < nconnect[ws]; k++){

                  int local_node = table[ws][lnode * nconnect[ws] + k]; // local number of the node connected to the center "entry"

                  std::size_t global_node_id = gid(node_rels[local_node]);
//std::cout << "      Local test node is : " << local_node + 1 << " offset is : " << k << " global node is : " << global_node_id + 1 <<  std::endl;

                  if(in_list(global_node_id,
		       nodalGraph.adj.size()-nodalGraph.start[ncnt],
		       &nodalGraph.adj[nodalGraph.start[ncnt]]) < 0) {
	                     nodalGraph.adj.push_back(global_node_id);
//std::cout << "            Added edge node : " << global_node_id + 1 << std::endl;
	          }
               }
               break;
            }
	  }
	} /* End "for(ecnt=0; ecnt < graph->nsur_elem[ncnt]; ecnt++)" */

        nadj = nodalGraph.adj.size();

      } /* End "for(ncnt=0; ncnt < mesh->num_nodes; ncnt++)" */

    nodalGraph.start[numOverlapNodes] = nadj;

// end find_adjacency

}

void
Albany::STKDiscretization::printVertexConnectivity(){

  for(std::size_t i = 0; i < numOverlapNodes; i++){

    std::cout << "Center vert is : " << overlap_node_map->GID(i) + 1 << std::endl;

    for(std::size_t j = nodalGraph.start[i]; j < nodalGraph.start[i + 1]; j++)

      std::cout << "                  " << nodalGraph.adj[j] + 1 << std::endl;

   }
}

void
Albany::STKDiscretization::updateMesh(bool /*shouldTransferIPData*/)
{

  const Albany::StateInfoStruct& nodal_param_states = stkMeshStruct->getFieldContainer()->getNodalParameterSIS();
  nodalDOFsStructContainer.addEmptyDOFsStruct("ordinary_solution", "", neq);
  nodalDOFsStructContainer.addEmptyDOFsStruct("mesh_nodes", "", 1);
  for(int is=0; is<nodal_param_states.size(); is++) {
    const Albany::StateStruct& param_state = *nodal_param_states[is];
    const Albany::StateStruct::FieldDims& dim = param_state.dim;
    int numComps = 1;
    if (dim.size()==3) //vector
      numComps = dim[2];
    else if (dim.size()==4) //tensor
      numComps = dim[2]*dim[3];

    nodalDOFsStructContainer.addEmptyDOFsStruct(param_state.name, param_state.meshPart,numComps);
    }

  computeNodalEpetraMaps(false);

  computeOwnedNodesAndUnknowns();

  setupMLCoords();

  computeNodalEpetraMaps(true);

  computeOverlapNodesAndUnknowns();

  transformMesh();

  computeGraphs();

  computeWorksetInfo();

  computeNodeSets();

  computeSideSets();

  setupExodusOutput();

  setupNetCDFOutput();
//meshToGraph();
//printVertexConnectivity();

}
