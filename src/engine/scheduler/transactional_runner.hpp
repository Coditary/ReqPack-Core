#pragma once

#include "engine/scheduler/executor.h"

#include <string>
#include <vector>

class TransactionalRunner {
  public:
    explicit TransactionalRunner(Executer& executor);

    std::vector<Executer::TransactionRecord> runTaskGroup(const Executer::TaskGroup& taskGroup,
                                                          const std::string& runId) const;

  private:
    Executer& executor;
};
