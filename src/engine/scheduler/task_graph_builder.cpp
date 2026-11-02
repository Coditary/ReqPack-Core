#include "engine/scheduler/task_graph_builder.hpp"

#include "executor_internal.h"

#include "engine/planner/planner_platform_policy.h"

#include <boost/graph/topological_sort.hpp>

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <vector>

TaskGraphBuilder::TaskGraphBuilder(Executer& executor) : executor(executor) {}

std::vector<Package> TaskGraphBuilder::collectPackages(const std::vector<Executer::TaskGroup>& taskGroups) const {
    std::vector<Package> packages;
    for (const Executer::TaskGroup& taskGroup : taskGroups) {
        packages.insert(packages.end(), taskGroup.packages.begin(), taskGroup.packages.end());
    }
    return packages;
}

std::vector<Graph::vertex_descriptor> TaskGraphBuilder::orderedVertices(const Graph& graph) const {
    const auto numVertices = boost::num_vertices(graph);
    if (numVertices == 0) {
        return {};
    }

    std::vector<Graph::vertex_descriptor> order;
    boost::topological_sort(graph, std::back_inserter(order));
    std::reverse(order.begin(), order.end());

    std::vector<int> levels(numVertices, 0);
    for (const Graph::vertex_descriptor v : order) {
        auto [edgeBegin, edgeEnd] = boost::out_edges(v, graph);
        for (auto edgeIt = edgeBegin; edgeIt != edgeEnd; ++edgeIt) {
            const Graph::vertex_descriptor target = boost::target(*edgeIt, graph);
            if (levels[target] <= levels[v]) {
                levels[target] = levels[v] + 1;
            }
        }
    }

    std::stable_sort(order.begin(), order.end(),
                     [&](const Graph::vertex_descriptor a, const Graph::vertex_descriptor b) {
                         if (levels[a] != levels[b]) {
                             return levels[a] < levels[b];
                         }
                         const std::optional<std::size_t> ensureOrderA = internal_ensure_order(graph[a]);
                         const std::optional<std::size_t> ensureOrderB = internal_ensure_order(graph[b]);
                         if (ensureOrderA.has_value() || ensureOrderB.has_value()) {
                             if (ensureOrderA.has_value() != ensureOrderB.has_value()) {
                                 return ensureOrderA.has_value();
                             }
                             if (ensureOrderA.value() != ensureOrderB.value()) {
                                 return ensureOrderA.value() < ensureOrderB.value();
                             }
                         }
                         return graph[a].system < graph[b].system;
                     });

    return order;
}

std::vector<Executer::TaskGroup> TaskGraphBuilder::createFromGraph(const Graph& graph) const {
    std::vector<Executer::TaskGroup> groups;

    for (const Graph::vertex_descriptor vertex : this->orderedVertices(graph)) {
        const Package& package = graph[vertex];
        if (groups.empty() || groups.back().action != package.action || groups.back().system != package.system) {
            groups.push_back(Executer::TaskGroup {.action = package.action,
                                                  .system = package.system,
                                                  .flags = package.flags});
        } else {
            merge_task_group_flags(groups.back().flags, package.flags);
        }

        groups.back().packages.push_back(package);
        if (package.localTarget) {
            groups.back().usesLocalTarget = true;
            groups.back().localPath = package.sourcePath;
        }
    }

    if constexpr (planner_platform::reorderNonNixBeforeNix) {
        std::stable_partition(groups.begin(), groups.end(), [](const Executer::TaskGroup& group) {
            return !planner_platform::isNixInstallSystem(group.system);
        });
    }

    return groups;
}

void TaskGraphBuilder::preload(std::vector<Executer::TaskGroup>& taskGroups) const {
    for (Executer::TaskGroup& taskGroup : taskGroups) {
        taskGroup.plugin = nullptr;
        taskGroup.pluginLoadFailed = false;
        if (executor.securityGateway.isGatewaySystem(taskGroup.system)) {
            continue;
        }
        if (executor.registry->getPlugin(taskGroup.system) == nullptr ||
            !executor.registry->loadPlugin(taskGroup.system)) {
            taskGroup.pluginLoadFailed = true;
            continue;
        }
        taskGroup.plugin = executor.registry->getPlugin(taskGroup.system);
        if (taskGroup.plugin == nullptr) {
            taskGroup.pluginLoadFailed = true;
        }
    }
}

std::vector<Executer::TaskGroupPlan> TaskGraphBuilder::createPlans(const std::vector<Executer::TaskGroup>& taskGroups,
                                                                   const Graph* graph) const {
    std::vector<Executer::TaskGroupPlan> plans;
    plans.reserve(taskGroups.size());
    for (const Executer::TaskGroup& taskGroup : taskGroups) {
        plans.push_back(Executer::TaskGroupPlan {.taskGroup = taskGroup});
    }
    if (graph == nullptr || taskGroups.empty()) {
        return plans;
    }

    std::map<std::string, std::size_t> packageToGroupIndex;
    for (std::size_t groupIndex = 0; groupIndex < taskGroups.size(); ++groupIndex) {
        for (const Package& package : taskGroups[groupIndex].packages) {
            packageToGroupIndex[package_identity_key(package)] = groupIndex;
        }
    }

    auto isInstallLike = [](const ActionType action) {
        return action == ActionType::INSTALL || action == ActionType::ENSURE;
    };

    std::set<std::pair<std::size_t, std::size_t>> seenEdges;
    auto addScheduleEdge = [&](const std::size_t sourceGroup, const std::size_t targetGroup) {
        if (sourceGroup == targetGroup) {
            return;
        }
        const std::pair<std::size_t, std::size_t> edge {sourceGroup, targetGroup};
        if (!seenEdges.insert(edge).second) {
            return;
        }
        plans[sourceGroup].successors.push_back(targetGroup);
        ++plans[targetGroup].pendingDependencies;
    };

    auto [edgeBegin, edgeEnd] = boost::edges(*graph);
    for (auto edgeIt = edgeBegin; edgeIt != edgeEnd; ++edgeIt) {
        const Package& sourcePackage = (*graph)[boost::source(*edgeIt, *graph)];
        const Package& targetPackage = (*graph)[boost::target(*edgeIt, *graph)];
        const auto sourceGroupIt = packageToGroupIndex.find(package_identity_key(sourcePackage));
        const auto targetGroupIt = packageToGroupIndex.find(package_identity_key(targetPackage));
        if (sourceGroupIt == packageToGroupIndex.end() || targetGroupIt == packageToGroupIndex.end()) {
            continue;
        }

        const std::string resolvedSource = executor.registry->resolvePluginName(sourcePackage.system);
        const std::string resolvedTarget = executor.registry->resolvePluginName(targetPackage.system);

        if (planner_platform::isNixInstallSystem(resolvedSource) &&
            !planner_platform::isNixInstallSystem(resolvedTarget)) {
            auto& consumers = plans[sourceGroupIt->second].taskGroup.nixSoftSkipConsumers;
            if (std::find(consumers.begin(), consumers.end(), resolvedTarget) == consumers.end()) {
                consumers.push_back(resolvedTarget);
            }
        }

        if (planner_platform::shouldIgnoreScheduleEdge(resolvedSource, resolvedTarget)) {
            continue;
        }

        addScheduleEdge(sourceGroupIt->second, targetGroupIt->second);
    }

    if (planner_platform::needsNonNixBeforeNixBarrier()) {
        std::vector<std::size_t> nonNixInstallGroups;
        std::vector<std::size_t> nixInstallGroups;
        for (std::size_t groupIndex = 0; groupIndex < plans.size(); ++groupIndex) {
            const Executer::TaskGroup& group = plans[groupIndex].taskGroup;
            if (!isInstallLike(group.action)) {
                continue;
            }
            const std::string resolvedSystem = executor.registry->resolvePluginName(group.system);
            if (planner_platform::isNixInstallSystem(resolvedSystem)) {
                nixInstallGroups.push_back(groupIndex);
            } else {
                nonNixInstallGroups.push_back(groupIndex);
            }
        }

        for (const std::size_t nixGroup : nixInstallGroups) {
            for (const std::size_t nonNixGroup : nonNixInstallGroups) {
                addScheduleEdge(nonNixGroup, nixGroup);
            }
        }
    }

    return plans;
}

std::vector<Executer::TaskGroup> TaskGraphBuilder::filterExecutable(
    const std::vector<Executer::TaskGroup>& taskGroups) const {
    std::vector<Executer::TaskGroup> filteredTaskGroups;

    for (const Executer::TaskGroup& taskGroup : taskGroups) {
        if (taskGroup.packages.empty()) {
            continue;
        }

        if (!actionUsesMissingPackageFilter(taskGroup.action)) {
            filteredTaskGroups.push_back(taskGroup);
            continue;
        }

        IPlugin* plugin = executor.registry->getPlugin(taskGroup.system);
        if (executor.securityGateway.isGatewaySystem(taskGroup.system)) {
            filteredTaskGroups.push_back(taskGroup);
            continue;
        }
        if (plugin == nullptr || !executor.registry->loadPlugin(taskGroup.system)) {
            filteredTaskGroups.push_back(taskGroup);
            continue;
        }

        Executer::TaskGroup filteredTaskGroup = taskGroup;
        if (filteredTaskGroup.usesLocalTarget) {
            filteredTaskGroups.push_back(std::move(filteredTaskGroup));
            continue;
        }
        std::vector<Package> packagesForFilter = taskGroup.packages;
        for (Package& package : packagesForFilter) {
            if (package.action == ActionType::UNKNOWN) {
                package.action = taskGroup.action;
            }
        }
        filteredTaskGroup.packages = plugin->getMissingPackages(packagesForFilter);
        if (!filteredTaskGroup.packages.empty()) {
            filteredTaskGroups.push_back(std::move(filteredTaskGroup));
        }
    }

    return filteredTaskGroups;
}
