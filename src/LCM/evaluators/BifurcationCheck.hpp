//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#if !defined(LCM_BifurcationCheck_hpp)
#define LCM_BifurcationCheck_hpp

#include "Phalanx_ConfigDefs.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Albany_Layouts.hpp"

namespace LCM {
  /// \brief BifurcationCheck Evaluator
  ///
  ///  This evaluator checks whether a material point has become
  ///  unstable
  ///
  template<typename EvalT, typename Traits>
  class BifurcationCheck : public PHX::EvaluatorWithBaseImpl<Traits>,
                           public PHX::EvaluatorDerived<EvalT, Traits>  {

  public:

    ///
    /// Constructor
    ///
    BifurcationCheck(const Teuchos::ParameterList& p,
                     const Teuchos::RCP<Albany::Layouts>& dl);

    ///
    /// Phalanx method to allocate space
    ///
    void postRegistrationSetup(typename Traits::SetupData d,
                               PHX::FieldManager<Traits>& vm);

    ///
    /// Implementation of physics
    ///
    void evaluateFields(typename Traits::EvalData d);

  private:

    typedef typename EvalT::ScalarT ScalarT;
    typedef typename EvalT::MeshScalarT MeshScalarT;

    //! Input: material tangent
    PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim,Dim,Dim> tangent_;

    //! Output: ellipticity indicator
    PHX::MDField<ScalarT,Cell,QuadPoint> ellipticity_flag_;

    //! Output: instability direction
    PHX::MDField<ScalarT,Cell,QuadPoint,Dim> direction_;

    //! number of integration points
    std::size_t num_pts_;

    //! number of spatial dimensions
    std::size_t num_dims_;
  };

}
#endif
