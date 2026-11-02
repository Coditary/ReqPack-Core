#pragma once

#include "platform/common/types.h"
#include "orchestrator_services.hpp"

#include <vector>

class QueryWorkflow {
  public:
    static int run(const OrchestratorServices& services, ActionType action, const std::vector<Request>& requests);
};
