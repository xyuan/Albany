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


#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"
#include "Sacado_ParameterRegistration.hpp"

// **********************************************************************
// Genereric Template Code for Constructor and PostRegistrationSetup
// **********************************************************************

namespace LCM {

template <typename EvalT, typename Traits> 
KfieldBC_Base<EvalT, Traits>::
KfieldBC_Base(Teuchos::ParameterList& p) :
  PHAL::DirichletBase<EvalT, Traits>(p),
  mu(p.get<RealType>("Shear Modulus")),
  nu(p.get<RealType>("Poissons Ratio"))
{
  KIval  = p.get<RealType>("KI Value");
  KIIval = p.get<RealType>("KII Value");

  KI = KIval;
  KII = KIIval;

  KI_name  = p.get< std::string >("Kfield KI Name");
  KII_name = p.get< std::string >("Kfield KII Name");

  // Set up values as parameters for parameter library
  Teuchos::RCP<ParamLib> paramLib = p.get< Teuchos::RCP<ParamLib> >
    ("Parameter Library", Teuchos::null);
  
  new Sacado::ParameterRegistration<EvalT, SPL_Traits> (KI_name, this, paramLib);
  new Sacado::ParameterRegistration<EvalT, SPL_Traits> (KII_name, this, paramLib);
}

// **********************************************************************
template<typename EvalT, typename Traits>
typename KfieldBC_Base<EvalT, Traits>::ScalarT&
KfieldBC_Base<EvalT, Traits>::
getValue(const std::string &n)
{
  if (n == KI_name)
    return KI;
  return KII;
}

// **********************************************************************
template<typename EvalT, typename Traits>
void
KfieldBC_Base<EvalT, Traits>::
computeBCs(double* coord, ScalarT& Xval, ScalarT& Yval)
{
  RealType X, Y, R, theta;
  ScalarT coeff_1, coeff_2;
  RealType tau = 6.283185307179586;
  ScalarT KI_X, KI_Y, KII_X, KII_Y;
    
  X = coord[0];
  Y = coord[1];
  R = std::sqrt(X*X + Y*Y);
  theta = std::atan2(Y,X);
    
  coeff_1 = ( KI / mu ) * std::sqrt( R / tau );
  coeff_2 = ( KII / mu ) * std::sqrt( R / tau );
    
  KI_X  = coeff_1 * ( 1.0 - 2.0 * nu + std::sin( theta / 2.0 ) * std::sin( theta / 2.0 ) ) * std::cos( theta / 2.0 );  
  KI_Y  = coeff_1 * ( 2.0 - 2.0 * nu - std::cos( theta / 2.0 ) * std::cos( theta / 2.0 ) ) * std::sin( theta / 2.0 );  
  
  KII_X = coeff_2 * ( 2.0 - 2.0 * nu + std::cos( theta / 2.0 ) * std::cos( theta / 2.0 ) ) * std::sin( theta / 2.0 );  
  KII_Y = coeff_2 * (-1.0 + 2.0 * nu + std::sin( theta / 2.0 ) * std::sin( theta / 2.0 ) ) * std::cos( theta / 2.0 );  
    
  Xval = KI_X + KII_X;
  Yval = KI_Y + KII_Y;

//  JTO: I am going to leave this here for now...
//     std::cout << "================" << std::endl;
//     std::cout.precision(15);
//     std::cout << "X : " << X << ", Y: " << Y << ", R: " << R << std::endl;
//     std::cout << "Node : " << nsNodes[inode] << std::endl;
//     std::cout << "KI : " << KI << ", KII: " << KII << std::endl;
//     std::cout << "theta: " << theta << std::endl;
//     std::cout << "coeff_1: " << coeff_1 << ", coeff_2: " << coeff_2 << std::endl;
//     std::cout << "KI_X: " << KI_X << ", KI_Y: " << KI_Y << std::endl;
//     std::cout << "Xval: " << Xval << ", Yval: " << Yval << std::endl;
//     std::cout << "dx: " << (*x)[xlunk] << std::endl;
//     std::cout << "dy: " << (*x)[ylunk] << std::endl;
//     std::cout << "fx: " << ((*x)[xlunk] - Xval) << std::endl;
//     std::cout << "fy: " << ((*x)[ylunk] - Yval) << std::endl;
//     std::cout << "sin(theta/2): " << std::sin( theta / 2.0 ) << std::endl;
//     std::cout << "cos(theta/2): " << std::cos( theta / 2.0 ) << std::endl;
}

// **********************************************************************
// Specialization: Residual
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::Residual, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::Residual, Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void 
KfieldBC<PHAL::AlbanyTraits::Residual, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Epetra_Vector> f = dirichletWorkset.f;
  Teuchos::RCP<const Epetra_Vector> x = dirichletWorkset.x;
  // Grab the vector off node GIDs for this Node Set ID from the std::map
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];

    this->computeBCs(coord, Xval, Yval);

    (*f)[xlunk] = ((*x)[xlunk] - Xval);
    (*f)[ylunk] = ((*x)[ylunk] - Yval);
  }
}

// **********************************************************************
// Specialization: Jacobian
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::Jacobian, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::Jacobian, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::Jacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{

  Teuchos::RCP<Epetra_Vector> f = dirichletWorkset.f;
  Teuchos::RCP<Epetra_CrsMatrix> jac = dirichletWorkset.Jac;
  Teuchos::RCP<const Epetra_Vector> x = dirichletWorkset.x;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  RealType* matrixEntries;
  int*    matrixIndices;
  int     numEntries;
  RealType diag=j_coeff;
  bool fillResid = (f != Teuchos::null);

  int xlunk, ylunk; // local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval; 
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);
    
    // replace jac values for the X dof 
    jac->ExtractMyRowView(xlunk, numEntries, matrixEntries, matrixIndices);
    for (int i=0; i<numEntries; i++) matrixEntries[i]=0;
    jac->ReplaceMyValues(xlunk, 1, &diag, &xlunk);

    // replace jac values for the y dof
    jac->ExtractMyRowView(ylunk, numEntries, matrixEntries, matrixIndices);
    for (int i=0; i<numEntries; i++) matrixEntries[i]=0;
    jac->ReplaceMyValues(ylunk, 1, &diag, &ylunk);
    
    if (fillResid)
    {
      (*f)[xlunk] = ((*x)[xlunk] - Xval.val());
      (*f)[ylunk] = ((*x)[ylunk] - Yval.val());
    } 
  }
}

// **********************************************************************
// Specialization: Tangent
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::Tangent, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::Tangent, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::Tangent, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Epetra_Vector> f = dirichletWorkset.f;
  Teuchos::RCP<Epetra_MultiVector> fp = dirichletWorkset.fp;
  Teuchos::RCP<Epetra_MultiVector> JV = dirichletWorkset.JV;
  Teuchos::RCP<const Epetra_Vector> x = dirichletWorkset.x;
  Teuchos::RCP<const Epetra_MultiVector> Vx = dirichletWorkset.Vx;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);

    if (f != Teuchos::null)
    {
      (*f)[xlunk] = ((*x)[xlunk] - Xval.val());
      (*f)[ylunk] = ((*x)[ylunk] - Yval.val());
    } 

    if (JV != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_x; i++)
      {
	(*JV)[i][xlunk] = j_coeff*(*Vx)[i][xlunk];
	(*JV)[i][ylunk] = j_coeff*(*Vx)[i][ylunk];
      }
    }
    
    if (fp != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_p; i++)
      {
	(*fp)[i][xlunk] = -Xval.dx(dirichletWorkset.param_offset+i);
	(*fp)[i][ylunk] = -Yval.dx(dirichletWorkset.param_offset+i);
      }
    }

  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Residual
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::SGResidual, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::SGResidual, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::SGResidual, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Stokhos::EpetraVectorOrthogPoly> f = 
    dirichletWorkset.sg_f;
  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly> x = 
    dirichletWorkset.sg_x;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
 const std::vector<double*>& nsNodeCoords = 
   dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  
  int nblock = x->size();
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];

    this->computeBCs(coord, Xval, Yval);

    for (int block=0; block<nblock; block++) {
      (*f)[block][xlunk] = ((*x)[block][xlunk] - Xval.coeff(block));
      (*f)[block][ylunk] = ((*x)[block][ylunk] - Yval.coeff(block));
    }
  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Jacobian
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::SGJacobian, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::SGJacobian, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::SGJacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP< Stokhos::EpetraVectorOrthogPoly> f = 
    dirichletWorkset.sg_f;
  Teuchos::RCP< Stokhos::VectorOrthogPoly<Epetra_CrsMatrix> > jac = 
    dirichletWorkset.sg_Jac;
  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly> x = 
    dirichletWorkset.sg_x;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  RealType* matrixEntries;
  int*    matrixIndices;
  int     numEntries;
  RealType diag=j_coeff;
  bool fillResid = (f != Teuchos::null);

  int nblock = 0;
  if (f != Teuchos::null)
    nblock = f->size();
  int nblock_jac = jac->size();

  int xlunk, ylunk; // local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval; 
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);
    
    // replace jac values for the X dof 
    for (int block=0; block<nblock_jac; block++) {
      (*jac)[block].ExtractMyRowView(xlunk, numEntries, matrixEntries, 
				     matrixIndices);
      for (int i=0; i<numEntries; i++) matrixEntries[i]=0;

      // replace jac values for the y dof
      (*jac)[block].ExtractMyRowView(ylunk, numEntries, matrixEntries, 
				     matrixIndices);
      for (int i=0; i<numEntries; i++) matrixEntries[i]=0;
    }
    (*jac)[0].ReplaceMyValues(xlunk, 1, &diag, &xlunk);
    (*jac)[0].ReplaceMyValues(ylunk, 1, &diag, &ylunk);
    
    if (fillResid)
    {
      for (int block=0; block<nblock; block++) {
	(*f)[block][xlunk] = ((*x)[block][xlunk] - Xval.val().coeff(block));
	(*f)[block][ylunk] = ((*x)[block][ylunk] - Yval.val().coeff(block));
      }
    } 
  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Tangent
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::SGTangent, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::SGTangent, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::SGTangent, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Stokhos::EpetraVectorOrthogPoly> f = 
    dirichletWorkset.sg_f;
  Teuchos::RCP<Stokhos::EpetraMultiVectorOrthogPoly> fp = 
    dirichletWorkset.sg_fp;
  Teuchos::RCP<Stokhos::EpetraMultiVectorOrthogPoly> JV = 
    dirichletWorkset.sg_JV;
  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly> x = 
    dirichletWorkset.sg_x;
  Teuchos::RCP<const Epetra_MultiVector> Vx = dirichletWorkset.Vx;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int nblock = x->size();

  if (JV != Teuchos::null)
    JV->init(0.0);
  if (fp != Teuchos::null)
    fp->init(0.0);

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);

    if (f != Teuchos::null)
    {
      for (int block=0; block<nblock; block++) {
	(*f)[block][xlunk] = (*x)[block][xlunk] - Xval.val().coeff(block);
	(*f)[block][ylunk] = (*x)[block][ylunk] - Yval.val().coeff(block);
      }
    } 

    if (JV != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_x; i++)
      {
	(*JV)[0][i][xlunk] = j_coeff*(*Vx)[i][xlunk];
	(*JV)[0][i][ylunk] = j_coeff*(*Vx)[i][ylunk];
      }
    }
    
    if (fp != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_p; i++)
      {
	for (int block=0; block<nblock; block++) {
	  (*fp)[block][i][xlunk] = 
	    -Xval.dx(dirichletWorkset.param_offset+i).coeff(block);
	  (*fp)[block][i][ylunk] = 
	    -Yval.dx(dirichletWorkset.param_offset+i).coeff(block);
	}
      }
    }

  }
}


// **********************************************************************
// Specialization: Multi-point Residual
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::MPResidual, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::MPResidual, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::MPResidual, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector> f = 
    dirichletWorkset.mp_f;
  Teuchos::RCP<const Stokhos::ProductEpetraVector> x = 
    dirichletWorkset.mp_x;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  
  int nblock = x->size();
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];

    this->computeBCs(coord, Xval, Yval);

    for (int block=0; block<nblock; block++) {
      (*f)[block][xlunk] = ((*x)[block][xlunk] - Xval.coeff(block));
      (*f)[block][ylunk] = ((*x)[block][ylunk] - Yval.coeff(block));
    }
  }
}

// **********************************************************************
// Specialization: Multi-point Jacobian
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::MPJacobian, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::MPJacobian, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::MPJacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector> f = 
    dirichletWorkset.mp_f;
  Teuchos::RCP< Stokhos::ProductContainer<Epetra_CrsMatrix> > jac = 
    dirichletWorkset.mp_Jac;
  Teuchos::RCP<const Stokhos::ProductEpetraVector> x = 
    dirichletWorkset.mp_x;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  RealType* matrixEntries;
  int*    matrixIndices;
  int     numEntries;
  RealType diag=j_coeff;
  bool fillResid = (f != Teuchos::null);

  int nblock = 0;
  if (f != Teuchos::null)
    nblock = f->size();
  int nblock_jac = jac->size();

  int xlunk, ylunk; // local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval; 
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);
    
    // replace jac values for the X dof 
    for (int block=0; block<nblock_jac; block++) {
      (*jac)[block].ExtractMyRowView(xlunk, numEntries, matrixEntries, 
				     matrixIndices);
      for (int i=0; i<numEntries; i++) matrixEntries[i]=0;
      (*jac)[block].ReplaceMyValues(xlunk, 1, &diag, &xlunk);

      // replace jac values for the y dof
      (*jac)[block].ExtractMyRowView(ylunk, numEntries, matrixEntries, 
				     matrixIndices);
      for (int i=0; i<numEntries; i++) matrixEntries[i]=0;
      (*jac)[block].ReplaceMyValues(ylunk, 1, &diag, &ylunk);
    }
    
    if (fillResid)
    {
      for (int block=0; block<nblock; block++) {
	(*f)[block][xlunk] = ((*x)[block][xlunk] - Xval.val().coeff(block));
	(*f)[block][ylunk] = ((*x)[block][ylunk] - Yval.val().coeff(block));
      }
    } 
  }
}

// **********************************************************************
// Specialization: Multi-point Tangent
// **********************************************************************
template<typename Traits>
KfieldBC<PHAL::AlbanyTraits::MPTangent, Traits>::
KfieldBC(Teuchos::ParameterList& p) :
  KfieldBC_Base<PHAL::AlbanyTraits::MPTangent, Traits>(p)
{
}
// **********************************************************************
template<typename Traits>
void KfieldBC<PHAL::AlbanyTraits::MPTangent, Traits>::
evaluateFields(typename Traits::EvalData dirichletWorkset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector> f = 
    dirichletWorkset.mp_f;
  Teuchos::RCP<Stokhos::ProductEpetraMultiVector> fp = 
    dirichletWorkset.mp_fp;
  Teuchos::RCP<Stokhos::ProductEpetraMultiVector> JV = 
    dirichletWorkset.mp_JV;
  Teuchos::RCP<const Stokhos::ProductEpetraVector> x = 
    dirichletWorkset.mp_x;
  Teuchos::RCP<const Epetra_MultiVector> Vx = dirichletWorkset.Vx;
  const RealType j_coeff = dirichletWorkset.j_coeff;
  const std::vector<std::vector<int> >& nsNodes = 
    dirichletWorkset.nodeSets->find(this->nodeSetID)->second;
  const std::vector<double*>& nsNodeCoords = 
    dirichletWorkset.nodeSetCoords->find(this->nodeSetID)->second;

  int nblock = x->size();

  int xlunk, ylunk; // global and local indicies into unknown vector
  double* coord;
  ScalarT Xval, Yval;
  for (unsigned int inode = 0; inode < nsNodes.size(); inode++) 
  {
    xlunk = nsNodes[inode][0];
    ylunk = nsNodes[inode][1];
    coord = nsNodeCoords[inode];
    
    this->computeBCs(coord, Xval, Yval);

    if (f != Teuchos::null)
    {
      for (int block=0; block<nblock; block++) {
	(*f)[block][xlunk] = (*x)[block][xlunk] - Xval.val().coeff(block);
	(*f)[block][ylunk] = (*x)[block][ylunk] - Yval.val().coeff(block);
      }
    } 

    if (JV != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_x; i++)
      {
	for (int block=0; block<nblock; block++) {
	  (*JV)[block][i][xlunk] = j_coeff*(*Vx)[i][xlunk];
	  (*JV)[block][i][ylunk] = j_coeff*(*Vx)[i][ylunk];
	}
      }
    }
    
    if (fp != Teuchos::null) {
      for (int i=0; i<dirichletWorkset.num_cols_p; i++)
      {
	for (int block=0; block<nblock; block++) {
	  (*fp)[block][i][xlunk] = 
	    -Xval.dx(dirichletWorkset.param_offset+i).coeff(block);
	  (*fp)[block][i][ylunk] = 
	    -Yval.dx(dirichletWorkset.param_offset+i).coeff(block);
	}
      }
    }

  }
}

} // namespace LCM
