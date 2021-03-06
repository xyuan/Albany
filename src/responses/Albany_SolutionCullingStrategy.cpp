//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Albany_SolutionCullingStrategy.hpp"

#include "Epetra_BlockMap.h"

#include "Epetra_GatherAllV.hpp"

#include "Teuchos_Assert.hpp"

#include <algorithm>

namespace Albany {

class UniformSolutionCullingStrategy : public SolutionCullingStrategyBase {
public:
  explicit UniformSolutionCullingStrategy(int numValues);

  virtual Teuchos::Array<int> selectedGIDs(const Epetra_BlockMap &sourceMap) const;

private:
  int numValues_;
};

} // namespace Albany

Albany::UniformSolutionCullingStrategy::
UniformSolutionCullingStrategy(int numValues) :
  numValues_(numValues)
{
  // Nothing to do
}

Teuchos::Array<int>
Albany::UniformSolutionCullingStrategy::
selectedGIDs(const Epetra_BlockMap &sourceMap) const
{
  Teuchos::Array<int> allGIDs(sourceMap.NumGlobalElements());
  {
    const int ierr = Epetra::GatherAllV(
        sourceMap.Comm(),
        sourceMap.MyGlobalElements(), sourceMap.NumMyElements(),
        allGIDs.getRawPtr(), allGIDs.size());
    TEUCHOS_ASSERT(ierr == 0);
  }
  std::sort(allGIDs.begin(), allGIDs.end());

  Teuchos::Array<int> result(numValues_);
  const int stride = 1 + (allGIDs.size() - 1) / numValues_;
  for (int i = 0; i < numValues_; ++i) {
    result[i] = allGIDs[i * stride];
  }
  return result;
}


#include "Albany_SolutionCullingStrategy.hpp"

#include "Albany_Application.hpp"
#include "Albany_AbstractDiscretization.hpp"

#include "Epetra_BlockMap.h"
#include "Epetra_Comm.h"

#include "Epetra_GatherAllV.hpp"

#include "Teuchos_Assert.hpp"

#include <string>
#include <algorithm>

namespace Albany {

class NodeSetSolutionCullingStrategy : public SolutionCullingStrategyBase {
public:
  NodeSetSolutionCullingStrategy(
      const std::string &nodeSetLabel,
      const Teuchos::RCP<const Application> &app);

  virtual void setup();

  virtual Teuchos::Array<int> selectedGIDs(const Epetra_BlockMap &sourceMap) const;

private:
  std::string nodeSetLabel_;
  Teuchos::RCP<const Application> app_;

  Teuchos::RCP<const AbstractDiscretization> disc_;
};

} // namespace Albany

Albany::NodeSetSolutionCullingStrategy::
NodeSetSolutionCullingStrategy(
    const std::string &nodeSetLabel,
    const Teuchos::RCP<const Application> &app) :
  nodeSetLabel_(nodeSetLabel),
  app_(app),
  disc_(Teuchos::null)
{
  // setup() must be called after the discretization has been created to finish initialization
}

void
Albany::NodeSetSolutionCullingStrategy::
setup()
{
  disc_ = app_->getDiscretization();
  // Once the discretization has been obtained, a handle to the application is not required
  // Release the resource to avoid possible circular references
  app_.reset();
}

Teuchos::Array<int>
Albany::NodeSetSolutionCullingStrategy::
selectedGIDs(const Epetra_BlockMap &sourceMap) const
{
  Teuchos::Array<int> result;
  {
    Teuchos::Array<int> mySelectedGIDs;
    {
      const NodeSetList &nodeSets = disc_->getNodeSets();

      const NodeSetList::const_iterator it = nodeSets.find(nodeSetLabel_);
      if (it != nodeSets.end()) {
        typedef NodeSetList::mapped_type NodeSetEntryList;
        const NodeSetEntryList &sampleNodeEntries = it->second;

        for (NodeSetEntryList::const_iterator jt = sampleNodeEntries.begin(); jt != sampleNodeEntries.end(); ++jt) {
          typedef NodeSetEntryList::value_type NodeEntryList;
          const NodeEntryList &sampleEntries = *jt;
          for (NodeEntryList::const_iterator kt = sampleEntries.begin(); kt != sampleEntries.end(); ++kt) {
            mySelectedGIDs.push_back(sourceMap.GID(*kt));
          }
        }
      }
    }

    const Epetra_Comm &comm = sourceMap.Comm();

    {
      int selectedGIDCount;
      {
        int mySelectedGIDCount = mySelectedGIDs.size();
        comm.SumAll(&mySelectedGIDCount, &selectedGIDCount, 1);
      }
      result.resize(selectedGIDCount);
    }

    const int ierr = Epetra::GatherAllV(
        comm,
        mySelectedGIDs.getRawPtr(), mySelectedGIDs.size(),
        result.getRawPtr(), result.size());
    TEUCHOS_ASSERT(ierr == 0);
  }

  std::sort(result.begin(), result.end());

  return result;
}


#include "Albany_Application.hpp"

#include "Teuchos_TestForException.hpp"

#include <string>

namespace Albany {

Teuchos::RCP<SolutionCullingStrategyBase>
createSolutionCullingStrategy(
    const Teuchos::RCP<const Application> &app,
    Teuchos::ParameterList &params)
{
  const std::string cullingStrategyToken = params.get("Culling Strategy", "Uniform");

  if (cullingStrategyToken == "Uniform") {
    const int numValues = params.get("Num Values", 10);
    return Teuchos::rcp(new UniformSolutionCullingStrategy(numValues));
  } else if (cullingStrategyToken == "Node Set") {
    const std::string nodeSetLabel = params.get<std::string>("Node Set Label");
    return Teuchos::rcp(new NodeSetSolutionCullingStrategy(nodeSetLabel, app));
  }

  const bool unsupportedCullingStrategy = true;
  TEUCHOS_TEST_FOR_EXCEPT(unsupportedCullingStrategy);
}

} // namespace Albany
