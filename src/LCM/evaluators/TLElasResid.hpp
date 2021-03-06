//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef TLELASRESID_HPP
#define TLELASRESID_HPP

#include "Phalanx_ConfigDefs.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Sacado_ParameterAccessor.hpp"

namespace LCM {
/** \brief Total Lagrangian (Non-linear) Elasticity Residual

    This evaluator computes a nonlinear elasticity residual

*/

template<typename EvalT, typename Traits>
class TLElasResid : public PHX::EvaluatorWithBaseImpl<Traits>,
                    public PHX::EvaluatorDerived<EvalT, Traits>,
                    public Sacado::ParameterAccessor<EvalT, SPL_Traits>   {

public:
  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  TLElasResid(const Teuchos::ParameterList& p);

  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);

  ScalarT& getValue(const std::string &n);

private:

  // Input:
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> stress;
  PHX::MDField<ScalarT,Cell,QuadPoint> J;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> defgrad;
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint,Dim> wGradBF;
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF;
  ScalarT zGrav;

  // Output:
  PHX::MDField<ScalarT,Cell,Node,Dim> Residual;

  std::size_t numNodes;
  std::size_t numQPs;
  std::size_t numDims;

  // Material Name
  std::string matModel;

  // Work space FCs
  Intrepid::FieldContainer<ScalarT> F_inv;
  Intrepid::FieldContainer<ScalarT> F_invT;
  Intrepid::FieldContainer<ScalarT> JF_invT;
  Intrepid::FieldContainer<ScalarT> P;
};
}

#endif
