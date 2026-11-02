#pragma once

#include "platform/common/types.h"
#include "engine/scheduler/executor.h"

#include <vector>

class TaskGraphBuilder {
  public:
    explicit TaskGraphBuilder(Executer& executor);

    std::vector<Executer::TaskGroup> createFromGraph(const Graph& graph) const;
    void preload(std::vector<Executer::TaskGroup>& taskGroups) const;
    std::vector<Executer::TaskGroupPlan> createPlans(const std::vector<Executer::TaskGroup>& taskGroups,
                                                     const Graph* graph) const;
    std::vector<Executer::TaskGroup> filterExecutable(const std::vector<Executer::TaskGroup>& taskGroups) const;
    std::vector<Package> collectPackages(const std::vector<Executer::TaskGroup>& taskGroups) const;

  private:
    Executer& executor;
    std::vector<Graph::vertex_descriptor> orderedVertices(const Graph& graph) const;
};
