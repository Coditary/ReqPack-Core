#include "mutation_workflow.hpp"

#include "orchestrator_compliance_prep.hpp"
#include "orchestrator_request_prep.hpp"

#include "output/logger.h"
#include "output/run_result_json.h"

#include <boost/graph/graph_traits.hpp>

namespace {

bool requests_include_system_scoped_install_or_ensure(const std::vector<Request>& requests) {
    for (const Request& request : requests) {
        if (request.system.empty() || request.usesLocalTarget) {
            continue;
        }
        if (request.action != ActionType::INSTALL && request.action != ActionType::ENSURE) {
            continue;
        }
        if (request.packages.empty()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<int> MutationWorkflow::runEmptyGraphInstallEnsure(const OrchestratorServices& services,
                                                                const ActionType action, Graph* graph,
                                                                const std::vector<Request>& requests,
                                                                const int requestedItemCount) {
    if (graph == nullptr || boost::num_vertices(*graph) != 0) {
        return std::nullopt;
    }
    if ((requestedItemCount <= 0 && !requests_include_system_scoped_install_or_ensure(requests)) ||
        (action != ActionType::INSTALL && action != ActionType::ENSURE)) {
        return std::nullopt;
    }

    for (const Request& request : requests) {
        if (request.system.empty()) {
            continue;
        }
        if (services.registry->getPlugin(request.system) != nullptr || services.registry->loadPlugin(request.system)) {
            continue;
        }
        Logger::instance().err("unknown or unavailable plugin system: " + request.system);
        if (services.config->display.jsonOutput) {
            RunResultJsonDocument document;
            document.ok = false;
            document.dryRun = services.config->execution.dryRun;
            RunResultJsonItem item;
            item.system = request.system;
            item.status = "failed";
            item.errorMessage = "unknown or unavailable plugin system";
            document.items.push_back(std::move(item));
            emit_run_result_json(document);
        }
        return 1;
    }

    if (services.config->display.jsonOutput) {
        emit_run_result_json(orchestrator_request_prep::build_already_satisfied_json(
            requests, *services.registry, action, services.config->execution.dryRun));
    } else {
        orchestrator_request_prep::display_already_satisfied_session(requests, *services.registry, action,
                                                                     requestedItemCount);
    }
    return 0;
}

int MutationWorkflow::runValidatedExecute(const OrchestratorServices& services, Graph* graph,
                                          const int requestedItemCount) {
    Graph* validatedGraph = services.validator->validate(graph);
    if (validatedGraph == nullptr) {
        if (graph != nullptr) {
            orchestrator_compliance_prep::log_validation_blocked(services.validator->getLastFindings());
        }
        delete graph;
        return 1;
    }
    graph = validatedGraph;
    services.executor->setRequestedItemCount(requestedItemCount, false);
    const bool executeOk = services.executor->execute(graph);
    delete graph;
    return executeOk ? 0 : 1;
}
