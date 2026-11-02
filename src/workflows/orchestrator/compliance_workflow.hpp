#pragma once

#include "platform/common/types.h"
#include "orchestrator_compliance_prep.hpp"
#include "orchestrator_request_prep.hpp"
#include "orchestrator_services.hpp"

#include <optional>
#include <string>
#include <vector>

class ComplianceWorkflow {
  public:
    struct PreparedRequests {
        std::vector<Request> planned;
        std::vector<std::string> missingSbomPackages;
    };

    static PreparedRequests prepare(ActionType action, const OrchestratorServices& services,
                                    const std::vector<Request>& requests);

    static int runSnapshot(const OrchestratorServices& services, const Request& request);
    static int runSbomExport(const OrchestratorServices& services, Graph& graph, const Request& request);
    static int runAuditExport(const OrchestratorServices& services, Graph& graph, const Request& request);
};
