#include "engine/scheduler/executor.h"

#include "engine/scheduler/task_graph_builder.hpp"

#include <vector>

std::vector<Package> Executer::collectPackages(const std::vector<TaskGroup>& taskGroups) const {
    return TaskGraphBuilder(const_cast<Executer&>(*this)).collectPackages(taskGroups);
}

std::vector<Executer::TaskGroup> Executer::createTaskGroups(const Graph& graph) const {
    return TaskGraphBuilder(const_cast<Executer&>(*this)).createFromGraph(graph);
}

void Executer::preloadTaskGroups(std::vector<TaskGroup>& taskGroups) const {
    TaskGraphBuilder(const_cast<Executer&>(*this)).preload(taskGroups);
}

std::vector<Executer::TaskGroupPlan> Executer::createTaskGroupPlans(const std::vector<TaskGroup>& taskGroups,
                                                                    const Graph* graph) const {
    return TaskGraphBuilder(const_cast<Executer&>(*this)).createPlans(taskGroups, graph);
}

std::vector<Executer::TaskGroup> Executer::filterExecutableTaskGroups(const std::vector<TaskGroup>& taskGroups) const {
    return TaskGraphBuilder(const_cast<Executer&>(*this)).filterExecutable(taskGroups);
}
