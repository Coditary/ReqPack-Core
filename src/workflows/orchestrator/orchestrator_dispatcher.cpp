#include "orchestrator_dispatcher.hpp"

#include "compliance_workflow.hpp"
#include "mutation_workflow.hpp"
#include "orchestrator_compliance_prep.hpp"
#include "orchestrator_request_prep.hpp"
#include "orchestrator_services.hpp"
#include "pack_workflow.hpp"
#include "query_workflow.hpp"

#include "workflows/orchestrator/orchestrator.h"
#include "engine/security/security_bridge.h"

#include "output/diagnostic.h"
#include "output/logger.h"

#include <boost/graph/graph_traits.hpp>
#include <filesystem>
#include <vector>

int OrchestratorDispatcher::run(Orchestrator& orchestrator) {
    if (orchestrator.shouldRefreshMainRegistry() &&
        !orchestrator.registry->getDatabase()->refreshMainRegistry()) {
        Logger::instance().diagnostic(make_warning_diagnostic(
            "registry", "Registry refresh failed before update",
            "ReqPack could not synchronize main registry before running update and will continue with cached registry "
            "data if available.",
            "Check registry.remoteUrl, network access, and local registry cache if update results look stale.", {},
            "registry", "update"));
    }

    (void)orchestrator.registry->getDatabase()->ensureReady();
    orchestrator_request_prep::expand_update_all_requests(orchestrator.requests, orchestrator.registry,
                                                          orchestrator.config);
    orchestrator_request_prep::ensure_request_plugins_discovered(orchestrator.requests, orchestrator.registry,
                                                                 orchestrator.config);

    if (orchestrator.requests.empty()) {
        return 0;
    }
    orchestrator_request_prep::rewrite_security_gateway_package_requests(orchestrator.requests, orchestrator.registry,
                                                                         orchestrator.config);
    orchestrator_request_prep::rewrite_registry_package_requests(orchestrator.requests,
                                                                 orchestrator.registry->getDatabase());
    if (orchestrator.shouldRunSystemWidePackageUpdates()) {
        return orchestrator.runSystemWidePackageUpdates();
    }
    if (orchestrator.shouldRefreshPluginWrappers()) {
        return orchestrator.runPluginWrapperRefresh();
    }
    if (orchestrator_request_prep::requests_target_plugin_install(orchestrator.requests)) {
        return orchestrator.runPluginInstallRequests();
    }
    if (orchestrator_request_prep::requests_target_plugin_remove(orchestrator.requests)) {
        return orchestrator.runPluginRemoveRequests();
    }

    std::vector<std::filesystem::path> tempFiles;
    if (!orchestrator_request_prep::prepare_requests_for_run(orchestrator.requests, orchestrator.registry,
                                                             orchestrator.config, tempFiles)) {
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return 1;
    }

    const OrchestratorServices services {
        .registry = orchestrator.registry,
        .planner = orchestrator.planner,
        .validator = orchestrator.validator,
        .executor = orchestrator.executor,
        .auditExporter = orchestrator.auditExporter,
        .sbomExporter = orchestrator.sbomExporter,
        .snapshotExporter = orchestrator.snapshotExporter,
        .config = &orchestrator.config,
    };
    const ActionType action = orchestrator.requests.front().action;

    if (action == ActionType::LIST || action == ActionType::OUTDATED || action == ActionType::SEARCH ||
        action == ActionType::INFO) {
        const int exitCode = QueryWorkflow::run(services, action, orchestrator.requests);
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return exitCode;
    }

    if (action == ActionType::SNAPSHOT) {
        const int exitCode = ComplianceWorkflow::runSnapshot(services, orchestrator.requests.front());
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return exitCode;
    }

    if (action == ActionType::PACK) {
        const int exitCode = PackWorkflow::run(services, orchestrator.requests.front());
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return exitCode;
    }

    const ComplianceWorkflow::PreparedRequests prepared =
        ComplianceWorkflow::prepare(action, services, orchestrator.requests);
    if (action == ActionType::SBOM && !prepared.missingSbomPackages.empty()) {
        orchestrator_compliance_prep::log_missing_sbom_packages(prepared.missingSbomPackages);
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return 1;
    }

    Graph* graph = orchestrator.planner->plan(prepared.planned);
    const int requestedItemCount = orchestrator.countRequestedItems();
    if (const std::optional<int> emptyGraphExit = MutationWorkflow::runEmptyGraphInstallEnsure(
            services, action, graph, orchestrator.requests, requestedItemCount);
        emptyGraphExit.has_value()) {
        delete graph;
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return emptyGraphExit.value();
    }

    if (action == ActionType::SBOM) {
        const int exitCode = graph != nullptr
                                 ? ComplianceWorkflow::runSbomExport(services, *graph, orchestrator.requests.front())
                                 : 1;
        delete graph;
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return exitCode;
    }

    if (action == ActionType::AUDIT) {
        const int exitCode = graph != nullptr
                                 ? ComplianceWorkflow::runAuditExport(services, *graph, orchestrator.requests.front())
                                 : 1;
        delete graph;
        orchestrator_request_prep::cleanup_temp_files(tempFiles);
        return exitCode;
    }

    const int exitCode = MutationWorkflow::runValidatedExecute(services, graph, requestedItemCount);
    orchestrator_request_prep::cleanup_temp_files(tempFiles);
    return exitCode;
}
