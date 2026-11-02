#pragma once

#include "platform/common/types.h"
#include "orchestrator_services.hpp"

class PackWorkflow {
  public:
    static int run(const OrchestratorServices& services, const Request& request);
};
