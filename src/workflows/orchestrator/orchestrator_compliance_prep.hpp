#pragma once

#include "engine/scheduler/executor.h"
#include "platform/common/types.h"
#include <rqp/security/validator.h>

#include <string>
#include <vector>

namespace orchestrator_compliance_prep {

struct SbomResolutionResult {
    std::vector<Request> requests;
    std::vector<std::string> missingPackages;
};

std::string package_specifier_from_info(const PackageInfo& item);

std::vector<Request> resolve_audit_requests(Executer* executor, const std::vector<Request>& requests);
std::vector<Request> expand_system_only_audit_requests(Executer* executor, const std::vector<Request>& requests);
SbomResolutionResult resolve_sbom_requests(Executer* executor, const ReqPackConfig& config,
                                           const std::vector<Request>& requests);
void log_missing_sbom_packages(const std::vector<std::string>& missingPackages);
void log_validation_blocked(const std::vector<ValidationFinding>& findings);
bool request_has_flag(const Request& request, const std::string& flag);

} // namespace orchestrator_compliance_prep
