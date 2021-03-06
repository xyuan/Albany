//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include <iostream>

#include "Albany_GenericSTKFieldContainer.hpp"
#include "Albany_STKNodeFieldContainer.hpp"

#include "Albany_Utils.hpp"
#include "Albany_StateInfoStruct.hpp"
#include "Albany_BucketArray.hpp"
#include <stk_mesh/base/GetBuckets.hpp>

#ifdef ALBANY_SEACAS
#include <stk_io/IossBridge.hpp>
#endif

template<bool Interleaved>
Albany::GenericSTKFieldContainer<Interleaved>::GenericSTKFieldContainer(
  const Teuchos::RCP<Teuchos::ParameterList>& params_,
  stk::mesh::MetaData* metaData_,
  const int neq_,
  const int numDim_)
  : metaData(metaData_),
    params(params_),
    neq(neq_),
    numDim(numDim_) {
}

template<bool Interleaved>
Albany::GenericSTKFieldContainer<Interleaved>::~GenericSTKFieldContainer() {
}


template<bool Interleaved>
void
Albany::GenericSTKFieldContainer<Interleaved>::buildStateStructs(const Teuchos::RCP<Albany::StateInfoStruct>& sis){

  using namespace Albany;

  // QuadPoint fields
  // dim[0] = nCells, dim[1] = nQP, dim[2] = nVec dim[3] = nVec dim[4] = nVec
  typedef typename AbstractSTKFieldContainer::QPScalarFieldType QPSFT;
  typedef typename AbstractSTKFieldContainer::QPVectorFieldType QPVFT;
  typedef typename AbstractSTKFieldContainer::QPTensorFieldType QPTFT;
  typedef typename AbstractSTKFieldContainer::QPTensor3FieldType QPT3FT;

  // Code to parse the vector of StateStructs and create STK fields
  for(std::size_t i = 0; i < sis->size(); i++) {
    StateStruct& st = *((*sis)[i]);
    StateStruct::FieldDims& dim = st.dim;

    if(st.entity == StateStruct::ElemData){
      qpscalar_states.push_back(& metaData->declare_field< QPSFT >(stk::topology::ELEMENT_RANK, st.name));
      stk::mesh::put_field(*qpscalar_states.back() ,
                           metaData->universal_part(), 1);
      
      //Debug
      //      cout << "Allocating qps field name " << qpscalar_states.back()->name() <<
      //            " size: (" << dim[0] << ", " << dim[1] << ")" <<endl;

#ifdef ALBANY_SEACAS
        if(st.output) stk::io::set_field_role(*qpscalar_states.back(), Ioss::Field::TRANSIENT);
#endif

    } else if(st.entity == StateStruct::QuadPoint || st.entity == StateStruct::ElemNode){

        if(dim.size() == 2){ // Scalar at QPs
          qpscalar_states.push_back(& metaData->declare_field< QPSFT >(stk::topology::ELEMENT_RANK, st.name));
          stk::mesh::put_field(*qpscalar_states.back() ,
                               metaData->universal_part(), dim[1]);
        //Debug
        //      cout << "Allocating qps field name " << qpscalar_states.back()->name() <<
        //            " size: (" << dim[0] << ", " << dim[1] << ")" <<endl;
#ifdef ALBANY_SEACAS

          if(st.output) stk::io::set_field_role(*qpscalar_states.back(), Ioss::Field::TRANSIENT);

#endif
        }
        else if(dim.size() == 3){ // Vector at QPs
          qpvector_states.push_back(& metaData->declare_field< QPVFT >(stk::topology::ELEMENT_RANK, st.name));
          // Multi-dim order is Fortran Ordering, so reversed here
          stk::mesh::put_field(*qpvector_states.back() ,
                           metaData->universal_part(), dim[2], dim[1]);
          //Debug
          //      cout << "Allocating qpv field name " << qpvector_states.back()->name() <<
          //            " size: (" << dim[0] << ", " << dim[1] << ", " << dim[2] << ")" <<endl;
#ifdef ALBANY_SEACAS

          if(st.output) stk::io::set_field_role(*qpvector_states.back(), Ioss::Field::TRANSIENT);

#endif
        }
        else if(dim.size() == 4){ // Tensor at QPs
          qptensor_states.push_back(& metaData->declare_field< QPTFT >(stk::topology::ELEMENT_RANK, st.name));
          // Multi-dim order is Fortran Ordering, so reversed here
          stk::mesh::put_field(*qptensor_states.back() ,
                           metaData->universal_part(), dim[3], dim[2], dim[1]);
          //Debug
          //      cout << "Allocating qpt field name " << qptensor_states.back()->name() <<
          //            " size: (" << dim[0] << ", " << dim[1] << ", " << dim[2] << ", " << dim[3] << ")" <<endl;
#ifdef ALBANY_SEACAS

          if(st.output) stk::io::set_field_role(*qptensor_states.back(), Ioss::Field::TRANSIENT);

#endif
        }
        else if(dim.size() == 5){ // Tensor3 at QPs
          qptensor3_states.push_back(& metaData->declare_field< QPT3FT >(stk::topology::ELEMENT_RANK, st.name));
          // Multi-dim order is Fortran Ordering, so reversed here
          stk::mesh::put_field(*qptensor3_states.back() ,
                               metaData->universal_part(), dim[4], dim[3], dim[2], dim[1]);
          //Debug
          //      cout << "Allocating qpt field name " << qptensor_states.back()->name() <<
          //            " size: (" << dim[0] << ", " << dim[1] << ", " << dim[2] << ", " << dim[3] << ", " << dim[4] << ")" <<endl;
#ifdef ALBANY_SEACAS

          if(st.output) stk::io::set_field_role(*qptensor3_states.back(), Ioss::Field::TRANSIENT);

#endif
        }
        // Something other than a scalar, vector, tensor, or tensor3 at the QPs is an error
        else TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
            "Error: GenericSTKFieldContainer - cannot match QPData");
    } // end QuadPoint
    // Single scalar at center of the workset
    else if(dim.size() == 1 && st.entity == StateStruct::WorksetValue) { // A single value that applies over the entire workset (time)
      scalarValue_states.push_back(st.name);
    } // End scalar at center of element
    else if((st.entity == StateStruct::NodalData) ||(st.entity == StateStruct::NodalDataToElemNode) || (st.entity == StateStruct::NodalDistParameter)) { // Data at the node points

        const Teuchos::RCP<Albany::NodeFieldContainer>& nodeContainer 
               = sis->getNodalDataBlock()->getNodeContainer();

        if(st.entity == StateStruct::NodalDataToElemNode) {
          nodal_sis.push_back((*sis)[i]);
          StateStruct::FieldDims nodalFieldDim;
          //convert ElemNode dims to NodalData dims.
          nodalFieldDim.insert(nodalFieldDim.begin(), dim.begin()+1,dim.end());
          (*nodeContainer)[st.name] = Albany::buildSTKNodeField(st.name, nodalFieldDim, metaData, st.output);
        }
        else if(st.entity == StateStruct::NodalDistParameter) {
          nodal_parameter_sis.push_back((*sis)[i]);
          StateStruct::FieldDims nodalFieldDim;
          //convert ElemNode dims to NodalData dims.
          nodalFieldDim.insert(nodalFieldDim.begin(), dim.begin()+1,dim.end());
          (*nodeContainer)[st.name] = Albany::buildSTKNodeField(st.name, nodalFieldDim, metaData, st.output);
        }
        else
          (*nodeContainer)[st.name] = Albany::buildSTKNodeField(st.name, dim, metaData, st.output);



    } // end Node class - anything else is an error
    else TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
            "Error: GenericSTKFieldContainer - cannot match unknown entity : " << st.entity << std::endl);

  }
}


template<bool Interleaved>
template<class T>
typename boost::disable_if< boost::is_same<T, Albany::AbstractSTKFieldContainer::ScalarFieldType>, void >::type
Albany::GenericSTKFieldContainer<Interleaved>::fillVectorHelper(Epetra_Vector& soln,
    T* solution_field,
    const Teuchos::RCP<Epetra_Map>& node_map,
    const stk::mesh::Bucket& bucket, int offset) {

  // Fill the result vector
  // Create a multidimensional array view of the
  // solution field data for this bucket of nodes.
  // The array is two dimensional ( Cartesian X NumberNodes )
  // and indexed by ( 0..2 , 0..NumberNodes-1 )

  BucketArray<T> solution_array(*solution_field, bucket);

  const int num_vec_components = solution_array.dimension(0);
  const int num_nodes_in_bucket = solution_array.dimension(1);

  stk::mesh::BulkData& mesh = solution_field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    for(std::size_t j = 0; j < num_vec_components; j++)

      soln[getDOF(node_lid, offset + j)] = solution_array(j, i);

  }
}

// Specialization for ScalarFieldType

template<bool Interleaved>
void Albany::GenericSTKFieldContainer<Interleaved>::fillVectorHelper(Epetra_Vector& soln,
    ScalarFieldType* solution_field,
    const Teuchos::RCP<Epetra_Map>& node_map,
    const stk::mesh::Bucket& bucket, int offset) {

  // Fill the result vector
  // Create a multidimensional array view of the
  // solution field data for this bucket of nodes.
  // The array is two dimensional ( Cartesian X NumberNodes )
  // and indexed by ( 0..2 , 0..NumberNodes-1 )

  BucketArray<ScalarFieldType> solution_array(*solution_field, bucket);

  const int num_nodes_in_bucket = solution_array.dimension(0);

  stk::mesh::BulkData& mesh = solution_field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    soln[getDOF(node_lid, offset)] = solution_array(i);

  }
}

template<bool Interleaved>
template<class T>
typename boost::disable_if< boost::is_same<T, Albany::AbstractSTKFieldContainer::ScalarFieldType>, void >::type
Albany::GenericSTKFieldContainer<Interleaved>::fillVectorHelper(
        Epetra_Vector& field_vector, T* field, const Teuchos::RCP<Epetra_Map>& node_map,
        const stk::mesh::Bucket& bucket, const NodalDOFManager& nodalDofManager, int offset) {

  BucketArray<T> field_array(*field, bucket);
  const int num_nodes_in_bucket = field_array.dimension(1);

  stk::mesh::BulkData& mesh = field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    if(node_lid>=0)
      for(std::size_t j = 0; j < (std::size_t)nodalDofManager.numComponents(); j++)
        field_vector[nodalDofManager.getLocalDOF(node_lid,offset+j)] = field_array(j,i);
  }

}

template<bool Interleaved>
void Albany::GenericSTKFieldContainer<Interleaved>::fillVectorHelper(
        Epetra_Vector& field_vector, ScalarFieldType* field, const Teuchos::RCP<Epetra_Map>& node_map,
        const stk::mesh::Bucket& bucket, const NodalDOFManager& nodalDofManager, int offset) {

  BucketArray<ScalarFieldType> field_array(*field, bucket);
  const int num_nodes_in_bucket = field_array.dimension(0);

  stk::mesh::BulkData& mesh = field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    if(node_lid>=0)
      field_vector[nodalDofManager.getLocalDOF(node_lid,offset)] = field_array(i);
  }
}


template<bool Interleaved>
template<class T>
typename boost::disable_if< boost::is_same<T, Albany::AbstractSTKFieldContainer::ScalarFieldType>, void >::type
Albany::GenericSTKFieldContainer<Interleaved>::saveVectorHelper(const Epetra_Vector& soln,
    T* solution_field,
    const Teuchos::RCP<Epetra_Map>& node_map,
    const stk::mesh::Bucket& bucket, int offset) {

  // Fill the result vector
  // Create a multidimensional array view of the
  // solution field data for this bucket of nodes.
  // The array is two dimensional ( Cartesian X NumberNodes )
  // and indexed by ( 0..2 , 0..NumberNodes-1 )

  BucketArray<T> solution_array(*solution_field, bucket);

  const int num_vec_components = solution_array.dimension(0);
  const int num_nodes_in_bucket = solution_array.dimension(1);

  stk::mesh::BulkData& mesh = solution_field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    for(std::size_t j = 0; j < num_vec_components; j++)
      solution_array(j, i) = soln[getDOF(node_lid, offset + j)];

  }
}

// Specialization for ScalarFieldType
template<bool Interleaved>
void Albany::GenericSTKFieldContainer<Interleaved>::saveVectorHelper(const Epetra_Vector& soln,
    ScalarFieldType* solution_field,
    const Teuchos::RCP<Epetra_Map>& node_map,
    const stk::mesh::Bucket& bucket, int offset) {

  // Fill the result vector
  // Create a multidimensional array view of the
  // solution field data for this bucket of nodes.
  // The array is two dimensional ( Cartesian X NumberNodes )
  // and indexed by ( 0..2 , 0..NumberNodes-1 )

  BucketArray<ScalarFieldType> solution_array(*solution_field, bucket);

  const int num_nodes_in_bucket = solution_array.dimension(0);

  stk::mesh::BulkData& mesh = solution_field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    solution_array(i) = soln[getDOF(node_lid, offset)];

  }
}

template<bool Interleaved>
template<class T>
typename boost::disable_if< boost::is_same<T, Albany::AbstractSTKFieldContainer::ScalarFieldType>, void >::type
Albany::GenericSTKFieldContainer<Interleaved>::saveVectorHelper(
        const Epetra_Vector& field_vector, T* field, const Teuchos::RCP<Epetra_Map>& node_map,
        const stk::mesh::Bucket& bucket, const NodalDOFManager& nodalDofManager, int offset) {

  BucketArray<T> field_array(*field, bucket);
  const int num_nodes_in_bucket = field_array.dimension(1);

  stk::mesh::BulkData& mesh = field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = node_map->LID(node_gid);

    if(node_lid>=0)
      for(std::size_t j = 0; j < (std::size_t)nodalDofManager.numComponents(); j++)
        field_array(j,i) = field_vector[nodalDofManager.getLocalDOF(node_lid,offset+j)];
  }
}


template<bool Interleaved>
void Albany::GenericSTKFieldContainer<Interleaved>::saveVectorHelper(
        const Epetra_Vector& field_vector, ScalarFieldType* field, const Teuchos::RCP<Epetra_Map>& field_node_map,
        const stk::mesh::Bucket& bucket, const NodalDOFManager& nodalDofManager, int offset) {

  BucketArray<ScalarFieldType> field_array(*field, bucket);
  const int num_nodes_in_bucket = field_array.dimension(0);

  stk::mesh::BulkData& mesh = field->get_mesh();

  for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

    //      const unsigned node_gid = bucket[i].identifier();
    const int node_gid = mesh.identifier(bucket[i]) - 1;
    int node_lid = field_node_map->LID(node_gid);

    if(node_lid>=0)
      field_array(i)=field_vector[nodalDofManager.getLocalDOF(node_lid,offset)];
  }
}


template<bool Interleaved>
template<class T>
typename boost::disable_if< boost::is_same<T, Albany::AbstractSTKFieldContainer::ScalarFieldType>, void >::type
Albany::GenericSTKFieldContainer<Interleaved>::copySTKField(const T* source, T* target) {

  stk::mesh::BulkData& mesh = source->get_mesh();
  const stk::mesh::BucketVector& bv = mesh.buckets(stk::topology::NODE_RANK);

  for(stk::mesh::BucketVector::const_iterator it = bv.begin() ; it != bv.end() ; ++it) {

    const stk::mesh::Bucket& bucket = **it;

    BucketArray<T> source_array(*source, bucket);
    BucketArray<T> target_array(*target, bucket);

    const int num_source_components = source_array.dimension(0);
    const int num_target_components = target_array.dimension(0);
    const int num_nodes_in_bucket = source_array.dimension(1);

    int downsample = num_source_components / num_target_components;
    int uneven_downsampling = num_source_components % num_target_components;

    TEUCHOS_TEST_FOR_EXCEPTION((uneven_downsampling) ||
                               (num_nodes_in_bucket != target_array.dimension(1)),
                               std::logic_error,
                               "Error in stk fields: specification of coordinate vector vs. solution layout is incorrect." 
                               << std::endl);

    for(std::size_t i = 0; i < num_nodes_in_bucket; i++) {

// In source, j varies over neq (num phys vectors * numDim)
// We want target to only vary over the first numDim components
// Not sure how to do this generally...

      for(std::size_t j = 0; j < num_target_components; j++) {

        target_array(j, i) = source_array(j, i);
      }
   }

  }
}

// Specialization for ScalarFieldType

template<bool Interleaved>
void Albany::GenericSTKFieldContainer<Interleaved>::copySTKField(const ScalarFieldType* source, ScalarFieldType* target) {

  stk::mesh::BulkData& mesh = source->get_mesh();
  const stk::mesh::BucketVector& bv = mesh.buckets(stk::topology::NODE_RANK);

  for(stk::mesh::BucketVector::const_iterator it = bv.begin() ; it != bv.end() ; ++it) {

    const stk::mesh::Bucket& bucket = **it;

    BucketArray<ScalarFieldType> source_array(*source, bucket);
    BucketArray<ScalarFieldType> target_array(*target, bucket);

    const int num_nodes_in_bucket = source_array.dimension(0);

    TEUCHOS_TEST_FOR_EXCEPTION((num_nodes_in_bucket != target_array.dimension(0)),
                               std::logic_error,
                               "Error in stk fields: specification of coordinate vector vs. solution layout is incorrect." << std::endl);

    for(std::size_t i = 0; i < num_nodes_in_bucket; i++)

      target_array(i) = source_array(i);

  }
}

