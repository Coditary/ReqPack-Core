#pragma once

#include "platform/common/types.h"
#include "orchestrator_services.hpp"

#include <vector>

class MutationWorkflow {
  public:
    static std::optional<int> runEmptyGraphInstallEnsure(const OrchestratorServices& services, ActionType action,
                                                         Graph* graph, const std::vector<Request>& requests,
                                                         int requestedItemCount);

    static int runValidatedExecute(const OrchestratorServices& services, Graph* graph, int requestedItemCount);
};
