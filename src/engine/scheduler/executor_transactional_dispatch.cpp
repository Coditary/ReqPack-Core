#include "engine/scheduler/executor.h"

#include "engine/scheduler/transactional_runner.hpp"

#include <string>
#include <vector>

std::vector<Executer::TransactionRecord> Executer::executeTransactionalTaskGroup(const TaskGroup& taskGroup,
                                                                                 const std::string& runId) const {
    return TransactionalRunner(const_cast<Executer&>(*this)).runTaskGroup(taskGroup, runId);
}
