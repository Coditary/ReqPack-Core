#include "engine/scheduler/executor.h"

#include "engine/scheduler/parallel_task_scheduler.hpp"

#include <string>
#include <vector>

std::vector<Executer::TransactionRecord> Executer::executeRecordedTaskGroups(const std::vector<TaskGroup>& taskGroups,
                                                                             const std::string& runId,
                                                                             const Graph* graph) const {
    return ParallelTaskScheduler(const_cast<Executer&>(*this)).run(taskGroups, graph, runId);
}
