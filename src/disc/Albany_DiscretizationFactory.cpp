//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Albany_DiscretizationFactory.hpp"
#include "Albany_STKDiscretization.hpp"
#include "Albany_TmplSTKMeshStruct.hpp"
#include "Albany_GenericSTKMeshStruct.hpp"

#ifdef ALBANY_SEACAS
#include "Albany_IossSTKMeshStruct.hpp"
#endif
#include "Albany_AsciiSTKMeshStruct.hpp"
#include "Albany_AsciiSTKMesh2D.hpp"
#include "Albany_ExtrudedSTKMeshStruct.hpp"
#ifdef ALBANY_CUTR
#include "Albany_FromCubitSTKMeshStruct.hpp"
#endif
#ifdef ALBANY_SCOREC
#include "AlbPUMI_FMDBDiscretization.hpp"
#include "AlbPUMI_FMDBMeshStruct.hpp"
#endif
#ifdef ALBANY_CATALYST
#include "Albany_Catalyst_Decorator.hpp"
#endif

#ifdef ALBANY_LCM
#include "Topology_Utils.h"
#endif // ALBANY_LCM

Albany::DiscretizationFactory::DiscretizationFactory(
  const Teuchos::RCP<Teuchos::ParameterList>& topLevelParams,
  const Teuchos::RCP<const Epetra_Comm>& epetra_comm_) :
  epetra_comm(epetra_comm_) {

  discParams = Teuchos::sublist(topLevelParams, "Discretization", true);

  if(topLevelParams->isSublist("Piro"))

    piroParams = Teuchos::sublist(topLevelParams, "Piro", true);

  if(topLevelParams->isSublist("Problem")) {

    Teuchos::RCP<Teuchos::ParameterList> problemParams = Teuchos::sublist(topLevelParams, "Problem", true);

    if(problemParams->isSublist("Adaptation"))

      adaptParams = Teuchos::sublist(problemParams, "Adaptation", true);

    if(problemParams->isSublist("Catalyst"))

      catalystParams = Teuchos::sublist(problemParams, "Catalyst", true);

  }

}

#ifdef ALBANY_CUTR
void
Albany::DiscretizationFactory::setMeshMover(const Teuchos::RCP<CUTR::CubitMeshMover>& meshMover_) {
  meshMover = meshMover_;
}
#endif

#ifdef ALBANY_LCM

namespace {

void createInterfaceParts(
    Teuchos::RCP<Teuchos::ParameterList> const & adapt_params,
    Teuchos::RCP<Albany::AbstractMeshStruct> & mesh_struct
    )
{
  bool const
  do_adaptation = adapt_params.is_null() == false;

  if (do_adaptation == false) return;

  std::string const &
  adaptation_method_name = adapt_params->get<std::string>("Method");

  bool const
  is_topology_modification = adaptation_method_name == "Topmod";

  if (is_topology_modification == false) return;

  std::string const &
  bulk_part_name = adapt_params->get<std::string>("Bulk Block Name");

  Albany::AbstractSTKMeshStruct &
  stk_mesh_struct = dynamic_cast<Albany::AbstractSTKMeshStruct &>(*mesh_struct);

  stk::mesh::MetaData &
  meta_data = *(stk_mesh_struct.metaData);

  stk::mesh::Part &
  bulk_part = *(meta_data.get_part(bulk_part_name));

  shards::CellTopology const &
  bulk_cell_topology = meta_data.get_cell_topology(bulk_part);

  std::string const &
  interface_part_name(adapt_params->get<std::string>("Interface Block Name"));

  shards::CellTopology const
  interface_cell_topology =
      LCM::interfaceCellTopogyFromBulkCellTopogy(bulk_cell_topology);

  stk::mesh::EntityRank const
  interface_dimension = static_cast<stk::mesh::EntityRank>(
      interface_cell_topology.getDimension());

  stk::mesh::Part &
  interface_part =
      meta_data.declare_part(interface_part_name, interface_dimension);

  stk::mesh::set_cell_topology(interface_part, interface_cell_topology);

#ifdef ALBANY_SEACAS
  stk::io::put_io_part_attribute(interface_part);
#endif // ALBANY_SEACAS

  // Augment the MeshSpecsStruct array with one additional entry for
  // the interface block. Essentially copy the last entry from the array
  // and modify some of its fields as needed.
  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> > &
  mesh_specs_struct = stk_mesh_struct.getMeshSpecs();

  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >::size_type
  number_blocks = mesh_specs_struct.size();

  Albany::MeshSpecsStruct &
  last_mesh_specs_struct = *(mesh_specs_struct[number_blocks - 1]);

  CellTopologyData const &
  interface_cell_topology_data =
      *(interface_cell_topology.getCellTopologyData());

  int const
  dimension = interface_cell_topology.getDimension();

  int const
  cubature_degree = last_mesh_specs_struct.cubatureDegree;

  std::vector<std::string>
  node_sets, side_sets;

  int const
  workset_size = last_mesh_specs_struct.worksetSize;

  std::string const &
  element_block_name = interface_part_name;

  std::map<std::string, int> &
  eb_name_to_index_map = last_mesh_specs_struct.ebNameToIndex;

  // Add entry to the map for this block
  eb_name_to_index_map.insert(
      std::make_pair(element_block_name, number_blocks));

  bool const
  is_interleaved = last_mesh_specs_struct.interleavedOrdering;

  Intrepid::EIntrepidPLPoly const
  cubature_rule = last_mesh_specs_struct.cubatureRule;

  mesh_specs_struct.resize(number_blocks + 1);

  mesh_specs_struct[number_blocks] =
      Teuchos::rcp(
          new Albany::MeshSpecsStruct(
              interface_cell_topology_data,
              dimension,
              cubature_degree,
              node_sets,
              side_sets,
              workset_size,
              element_block_name,
              eb_name_to_index_map,
              is_interleaved,
              cubature_rule));

  return;
}

} // anonymous namespace

#endif //ALBANY_LCM


Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >
Albany::DiscretizationFactory::createMeshSpecs() {
  std::string& method = discParams->get("Method", "STK1D");

  if(method == "STK1D") {
    meshStruct = Teuchos::rcp(new Albany::TmplSTKMeshStruct<1>(discParams, adaptParams, epetra_comm));
  }

  else if(method == "STK0D") {
    meshStruct = Teuchos::rcp(new Albany::TmplSTKMeshStruct<0>(discParams, adaptParams, epetra_comm));
  }

  else if(method == "STK2D") {
    meshStruct = Teuchos::rcp(new Albany::TmplSTKMeshStruct<2>(discParams, adaptParams, epetra_comm));
  }

  else if(method == "STK3D") {
    meshStruct = Teuchos::rcp(new Albany::TmplSTKMeshStruct<3>(discParams, adaptParams, epetra_comm));
  }

  else if(method == "Ioss" || method == "Exodus" ||  method == "Pamgen") {
#ifdef ALBANY_SEACAS
    meshStruct = Teuchos::rcp(new Albany::IossSTKMeshStruct(discParams, adaptParams, epetra_comm));
#else
    TEUCHOS_TEST_FOR_EXCEPTION(method == "Ioss" || method == "Exodus" ||  method == "Pamgen",
                               Teuchos::Exceptions::InvalidParameter,
                               "Error: Discretization method " << method
                               << " requested, but not compiled in" << std::endl);
#endif
  }

  else if(method == "Ascii") {
    meshStruct = Teuchos::rcp(new Albany::AsciiSTKMeshStruct(discParams, epetra_comm));
  }
  else if(method == "Ascii2D") {
	  Teuchos::RCP<Albany::GenericSTKMeshStruct> meshStruct2D;
      meshStruct2D = Teuchos::rcp(new Albany::AsciiSTKMesh2D(discParams, epetra_comm));
      Teuchos::RCP<Albany::StateInfoStruct> sis=Teuchos::rcp(new Albany::StateInfoStruct);
	  Albany::AbstractFieldContainer::FieldContainerRequirements req;
	  int neq=2;
      meshStruct2D->setFieldAndBulkData(epetra_comm, discParams, neq, req,
                                        sis, meshStruct2D->getMeshSpecs()[0]->worksetSize);
      Ioss::Init::Initializer io;
      Teuchos::RCP<stk::io::StkMeshIoBroker> mesh_data =Teuchos::rcp(new stk::io::StkMeshIoBroker(MPI_COMM_WORLD));
      mesh_data->set_bulk_data(*meshStruct2D->bulkData);
      size_t idx = mesh_data->create_output_mesh("IceSheet.exo", stk::io::WRITE_RESULTS);
      mesh_data->process_output_request(idx, 0.0);
  }
  else if(method == "Extruded") {
  	  meshStruct = Teuchos::rcp(new Albany::ExtrudedSTKMeshStruct(discParams, epetra_comm));
  }
  else if(method == "Cubit") {
#ifdef ALBANY_CUTR
    AGS"need to inherit from Generic"
    meshStruct = Teuchos::rcp(new Albany::FromCubitSTKMeshStruct(meshMover, discParams, neq));
#else
    TEUCHOS_TEST_FOR_EXCEPTION(method == "Cubit",
                               Teuchos::Exceptions::InvalidParameter,
                               "Error: Discretization method " << method
                               << " requested, but not compiled in" << std::endl);
#endif
  }

  else if(method == "FMDB") {
#ifdef ALBANY_SCOREC
    meshStruct = Teuchos::rcp(new AlbPUMI::FMDBMeshStruct(discParams, epetra_comm));
#else
    TEUCHOS_TEST_FOR_EXCEPTION(method == "FMDB",
                               Teuchos::Exceptions::InvalidParameter,
                               "Error: Discretization method " << method
                               << " requested, but not compiled in" << std::endl);
#endif
  }

  else {
    TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter, std::endl <<
                               "Error!  Unknown discretization method in DiscretizationFactory: " << method <<
                               "!" << std::endl << "Supplied parameter list is " << std::endl << *discParams
                               << "\nValid Methods are: STK1D, STK2D, STK3D, Ioss, Exodus, Cubit, FMDB" << std::endl);
  }

#ifdef ALBANY_LCM
  // Add an interface block. For now relies on STK, so we force a cast that
  // will fail if the underlying meshStruct is not based on STK.
  createInterfaceParts(adaptParams, meshStruct);
#endif // ALBANY_LCM

  return meshStruct->getMeshSpecs();

}

Teuchos::RCP<Albany::AbstractDiscretization>
Albany::DiscretizationFactory::createDiscretization(unsigned int neq,
    const Teuchos::RCP<Albany::StateInfoStruct>& sis,
    const AbstractFieldContainer::FieldContainerRequirements& req,
    const Teuchos::RCP<Piro::MLRigidBodyModes>& rigidBodyModes) {
  TEUCHOS_TEST_FOR_EXCEPTION(meshStruct == Teuchos::null,
                             std::logic_error,
                             "meshStruct accessed, but it has not been constructed" << std::endl);

  setupInternalMeshStruct(neq, sis, req);
  Teuchos::RCP<Albany::AbstractDiscretization> result =
      createDiscretizationFromInternalMeshStruct(rigidBodyModes);

  // Wrap the discretization in the catalyst decorator if needed.
#ifdef ALBANY_CATALYST

  if(Teuchos::nonnull(catalystParams) && catalystParams->get<bool>("Interface Activated", false))
    result = Teuchos::rcp(static_cast<Albany::AbstractDiscretization*>(
                          new Catalyst::Decorator(result, catalystParams)));

#endif

  return result;
}

Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >
Albany::DiscretizationFactory::createMeshSpecs(Teuchos::RCP<Albany::AbstractMeshStruct> mesh) {
  meshStruct = mesh;
  return meshStruct->getMeshSpecs();
}

void
Albany::DiscretizationFactory::setupInternalMeshStruct(
  unsigned int neq,
  const Teuchos::RCP<Albany::StateInfoStruct>& sis,
  const AbstractFieldContainer::FieldContainerRequirements& req) {
  meshStruct->setFieldAndBulkData(epetra_comm, discParams, neq, req,
                                  sis, meshStruct->getMeshSpecs()[0]->worksetSize);
}

Teuchos::RCP<Albany::AbstractDiscretization>
Albany::DiscretizationFactory::createDiscretizationFromInternalMeshStruct(
  const Teuchos::RCP<Piro::MLRigidBodyModes>& rigidBodyModes) {

  if(!piroParams.is_null() && !rigidBodyModes.is_null())

    rigidBodyModes->setPiroPL(piroParams);

  switch(meshStruct->meshSpecsType()) {

    case Albany::AbstractMeshStruct::STK_MS: {
      Teuchos::RCP<Albany::AbstractSTKMeshStruct> ms = Teuchos::rcp_dynamic_cast<Albany::AbstractSTKMeshStruct>(meshStruct);
      return Teuchos::rcp(new Albany::STKDiscretization(ms, epetra_comm, rigidBodyModes));
    }
    break;

#ifdef ALBANY_SCOREC

    case Albany::AbstractMeshStruct::FMDB_VTK_MS: {
      Teuchos::RCP<AlbPUMI::FMDBMeshStruct> ms = Teuchos::rcp_dynamic_cast<AlbPUMI::FMDBMeshStruct>(meshStruct);
      return Teuchos::rcp(new AlbPUMI::FMDBDiscretization<AlbPUMI::FMDBVtk>(ms, epetra_comm, rigidBodyModes));
    }
    break;

    case Albany::AbstractMeshStruct::FMDB_EXODUS_MS: {
      Teuchos::RCP<AlbPUMI::FMDBMeshStruct> ms = Teuchos::rcp_dynamic_cast<AlbPUMI::FMDBMeshStruct>(meshStruct);
      return Teuchos::rcp(new AlbPUMI::FMDBDiscretization<AlbPUMI::FMDBExodus>(ms, epetra_comm, rigidBodyModes));
    }
    break;
#endif

  }
}
