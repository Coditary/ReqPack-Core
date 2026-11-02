#include "compliance_workflow.hpp"

#include "engine/security/security_bridge.h"

#include "orchestrator_compliance_prep.hpp"

#include "output/logger.h"

#include <rqp/security/security_status.h>

#include <sstream>
#include <utility>

ComplianceWorkflow::PreparedRequests ComplianceWorkflow::prepare(const ActionType action,
                                                                 const OrchestratorServices& services,
                                                                 const std::vector<Request>& requests) {
    PreparedRequests prepared {.planned = requests};
    if (action == ActionType::AUDIT) {
        prepared.planned =
            orchestrator_compliance_prep::expand_system_only_audit_requests(services.executor, requests);
        prepared.planned = orchestrator_compliance_prep::resolve_audit_requests(services.executor, prepared.planned);
    } else if (action == ActionType::SBOM) {
        orchestrator_compliance_prep::SbomResolutionResult resolved =
            orchestrator_compliance_prep::resolve_sbom_requests(services.executor, *services.config, requests);
        prepared.planned = std::move(resolved.requests);
        prepared.missingSbomPackages = std::move(resolved.missingPackages);
    }
    return prepared;
}

int ComplianceWorkflow::runSnapshot(const OrchestratorServices& services, const Request& request) {
    return services.snapshotExporter->exportSnapshot(request) ? 0 : 1;
}

int ComplianceWorkflow::runSbomExport(const OrchestratorServices& services, Graph& graph, const Request& request) {
    return services.sbomExporter->exportGraph(graph, request) ? 0 : 1;
}

int ComplianceWorkflow::runAuditExport(const OrchestratorServices& services, Graph& graph, const Request& request) {
    if (orchestrator_compliance_prep::request_has_flag(request, "doctor")) {
        const SecurityStatusReport status =
            build_security_status(security_settings_from(*services.config), services.registry);
        std::ostringstream statusOutput;
        print_security_status_report(status, statusOutput);
        Logger::instance().logStdout(statusOutput.str());
    }
    const std::vector<ValidationFinding> findings = services.validator->audit(&graph);
    const bool exported = services.auditExporter->exportGraph(graph, findings, request);
    if (!exported) {
        return 1;
    }
    const bool failOnFindings = services.config->security.auditFailOnFindings ||
                                orchestrator_compliance_prep::request_has_flag(request, "fail-on-findings");
    if (!request.outputPath.empty() && !failOnFindings) {
        return 0;
    }
    return findings.empty() ? 0 : 1;
}
