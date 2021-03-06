//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "Intrepid_FunctionSpaceTools.hpp"

namespace LCM {

//**********************************************************************
template<typename EvalT, typename Traits>
ElasticityResid<EvalT, Traits>::
ElasticityResid(const Teuchos::ParameterList& p) :
  Stress      (p.get<std::string>                   ("Stress Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout") ),
  wGradBF     (p.get<std::string>                   ("Weighted Gradient BF Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node QP Vector Data Layout") ),
  ExResidual   (p.get<std::string>                   ("Residual Name"),
	       p.get<Teuchos::RCP<PHX::DataLayout> >("Node Vector Data Layout") )
{
  this->addDependentField(Stress);
  this->addDependentField(wGradBF);

  this->addEvaluatedField(ExResidual);

  if (p.isType<bool>("Disable Transient"))
    enableTransient = !p.get<bool>("Disable Transient");
  else enableTransient = true;

  if (enableTransient) {
    // Two more fields are required for transient capability
    Teuchos::RCP<PHX::DataLayout> node_qp_scalar_dl =
       p.get< Teuchos::RCP<PHX::DataLayout> >("Node QP Scalar Data Layout");
    Teuchos::RCP<PHX::DataLayout> vector_dl =
       p.get< Teuchos::RCP<PHX::DataLayout> >("QP Vector Data Layout");

    PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF_tmp
      (p.get<std::string>("Weighted BF Name"), node_qp_scalar_dl);
    wBF = wBF_tmp;
    PHX::MDField<ScalarT,Cell,QuadPoint,Dim> uDotDot_tmp
      (p.get<std::string>("Time Dependent Variable Name"), vector_dl);
    uDotDot = uDotDot_tmp;

   this->addDependentField(wBF);
   this->addDependentField(uDotDot);
  }


  this->setName("ElasticityResid"+PHX::TypeString<EvalT>::value);

  std::vector<PHX::DataLayout::size_type> dims;
  wGradBF.fieldTag().dataLayout().dimensions(dims);
  numNodes = dims[1];
  numQPs   = dims[2];
  numDims  = dims[3];
}

//**********************************************************************
template<typename EvalT, typename Traits>
void ElasticityResid<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(Stress,fm);
  this->utils.setFieldData(wGradBF,fm);

  this->utils.setFieldData(ExResidual,fm);

  if (enableTransient) this->utils.setFieldData(uDotDot,fm);
  if (enableTransient) this->utils.setFieldData(wBF,fm);

}

//**********************************************************************
template<typename EvalT, typename Traits>
void ElasticityResid<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  typedef Intrepid::FunctionSpaceTools FST;

    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t node=0; node < numNodes; ++node) {
              for (std::size_t dim=0; dim<numDims; dim++)  ExResidual(cell,node,dim)=0.0;
          for (std::size_t qp=0; qp < numQPs; ++qp) {
            for (std::size_t i=0; i<numDims; i++) {
              for (std::size_t dim=0; dim<numDims; dim++) {
                ExResidual(cell,node,i) += Stress(cell, qp, i, dim) * wGradBF(cell, node, qp, dim);
    } } } } }


  if (workset.transientTerms && enableTransient)
    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t node=0; node < numNodes; ++node) {
          for (std::size_t qp=0; qp < numQPs; ++qp) {
            for (std::size_t i=0; i<numDims; i++) {
                ExResidual(cell,node,i) += uDotDot(cell, qp, i) * wBF(cell, node, qp);
    } } } }


//  FST::integrate<ScalarT>(ExResidual, Stress, wGradBF, Intrepid::COMP_CPP, false); // "false" overwrites

}

//**********************************************************************
}

