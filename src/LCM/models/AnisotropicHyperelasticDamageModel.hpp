//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#if !defined(LCM_AnisotropicHyperelasticDamageModel_hpp)
#define LCM_AnisotropicHyperelasticDamageModel_hpp

#include "Phalanx_ConfigDefs.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Albany_Layouts.hpp"
#include "LCM/models/ConstitutiveModel.hpp"

namespace LCM
{

//! \brief Constitutive Model Base Class
template<typename EvalT, typename Traits>
class AnisotropicHyperelasticDamageModel: public LCM::ConstitutiveModel<EvalT,
    Traits>
{
public:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  using ConstitutiveModel<EvalT, Traits>::num_dims_;
  using ConstitutiveModel<EvalT, Traits>::num_pts_;
  using ConstitutiveModel<EvalT, Traits>::field_name_map_;

  ///
  /// Constructor
  ///
  AnisotropicHyperelasticDamageModel(Teuchos::ParameterList* p,
      const Teuchos::RCP<Albany::Layouts>& dl);

  ///
  /// Virtual Destructor
  virtual
  ~AnisotropicHyperelasticDamageModel()
  {};

  ///
  /// Method to compute the state (e.g. energy, stress, tangent)
  ///
  virtual
  void
  computeState(typename Traits::EvalData workset,
      std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT> > > dep_fields,
      std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT> > > eval_fields);

private:

  ///
  /// Private to prohibit copying
  ///
  AnisotropicHyperelasticDamageModel(const AnisotropicHyperelasticDamageModel&);

  ///
  /// Private to prohibit copying
  ///
  AnisotropicHyperelasticDamageModel& operator=(
      const AnisotropicHyperelasticDamageModel&);

  ///
  /// Fiber 1 constants
  ///
  RealType k_f1_, q_f1_, volume_fraction_f1_, max_damage_f1_, saturation_f1_;

  ///
  /// Fiber 2 constants
  ///
  RealType k_f2_, q_f2_, volume_fraction_f2_, max_damage_f2_, saturation_f2_;

  ///
  /// Matrix constants
  ///
  RealType volume_fraction_m_, max_damage_m_, saturation_m_;

  ///
  /// Fiber 1 orientation vector
  ///
  std::vector<RealType> direction_f1_;

  ///
  /// Fiber 2 orientation vector
  ///
  std::vector<RealType> direction_f2_;

};
}

#endif
