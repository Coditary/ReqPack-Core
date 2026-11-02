#pragma once

#include "platform/common/types.h"
#include "engine/scheduler/executor.h"

#include <string>
#include <vector>

class ParallelTaskScheduler {
  public:
    explicit ParallelTaskScheduler(Executer& executor);

    std::vector<Executer::TransactionRecord> run(const std::vector<Executer::TaskGroup>& taskGroups,
                                                 const Graph* graph, const std::string& runId) const;

  private:
    Executer& executor;
};
