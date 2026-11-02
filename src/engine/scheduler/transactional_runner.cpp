#include "engine/scheduler/transactional_runner.hpp"

#include "executor_internal.h"

#include "output/logger.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

TransactionalRunner::TransactionalRunner(Executer& executor) : executor(executor) {}

std::vector<Executer::TransactionRecord> TransactionalRunner::runTaskGroup(const Executer::TaskGroup& taskGroup,
                                                                             const std::string& runId) const {
    if (taskGroup.usesLocalTarget) {
        if (!executor.dispatchTaskGroupToPlugin(taskGroup)) {
            std::vector<Executer::TransactionRecord> failureRecords = executor.buildFailureRecords(taskGroup);
            for (Executer::TransactionRecord& record : failureRecords) {
                record.runId = runId;
                record.errorMessage = "plugin action failed";
            }
            Logger::instance().diagnostic(plugin_group_failure_diagnostic(
                taskGroup.system, "Local target install failed for system '" + taskGroup.system + "'",
                "Plugin could not process requested local file or archive target."));
            return failureRecords;
        }

        std::vector<Executer::TransactionRecord> successRecords = executor.buildSuccessRecords(taskGroup);
        for (Executer::TransactionRecord& record : successRecords) {
            record.runId = runId;
        }
        return successRecords;
    }

    std::vector<Executer::TransactionRecord> records;
    records.reserve(taskGroup.packages.size());

    auto appendSuccessRecords = [&](const std::vector<Package>& packages) {
        if (packages.empty()) {
            return;
        }
        Executer::TaskGroup resultTaskGroup = taskGroup;
        resultTaskGroup.packages = packages;
        std::vector<Executer::TransactionRecord> successRecords = executor.buildSuccessRecords(resultTaskGroup);
        for (Executer::TransactionRecord& record : successRecords) {
            record.runId = runId;
        }
        records.insert(records.end(), successRecords.begin(), successRecords.end());
    };

    auto appendFailureRecords = [&](const std::vector<Package>& packages, const std::string& errorMessage) {
        if (packages.empty()) {
            return;
        }
        Executer::TaskGroup resultTaskGroup = taskGroup;
        resultTaskGroup.packages = packages;
        std::vector<Executer::TransactionRecord> failureRecords = executor.buildFailureRecords(resultTaskGroup);
        for (Executer::TransactionRecord& record : failureRecords) {
            record.runId = runId;
            record.errorMessage = errorMessage;
        }
        records.insert(records.end(), failureRecords.begin(), failureRecords.end());
    };

    auto appendFailureRecord = [&](const Package& package, const std::string& errorMessage) {
        Executer::TaskGroup resultTaskGroup = taskGroup;
        resultTaskGroup.packages = {package};
        std::vector<Executer::TransactionRecord> failureRecords = executor.buildFailureRecords(resultTaskGroup);
        for (Executer::TransactionRecord& record : failureRecords) {
            record.runId = runId;
            record.errorMessage = errorMessage;
        }
        records.insert(records.end(), failureRecords.begin(), failureRecords.end());
    };

    auto containsPackage = [](const std::vector<Package>& packages, const Package& candidate) {
        return std::any_of(packages.begin(), packages.end(),
                           [&](const Package& package) { return samePackage(package, candidate); });
    };

    auto displaySuccessfulPackages = [&](const std::vector<Package>& packages) {
        for (const Package& package : packages) {
            Logger::instance().displayItemSuccess(package_item_id(taskGroup.system, package), package.version);
        }
    };

    for (const Package& package : taskGroup.packages) {
        Logger::instance().displayItemBegin(package_item_id(taskGroup.system, package), package.name);
    }

    const Executer::TaskGroup& executableTaskGroup = taskGroup;

    if (!executor.transactionDatabase->updateItemsStatus(runId, executableTaskGroup.packages, "running")) {
        for (const Package& package : executableTaskGroup.packages) {
            Logger::instance().displayItemFailure(package_item_id(taskGroup.system, package),
                                                  transaction_update_failure_diagnostic(taskGroup.system));
        }
        appendFailureRecords(executableTaskGroup.packages, "transaction update failed");
        return records;
    }

    if (executor.dispatchTaskGroupToPlugin(executableTaskGroup)) {
        appendSuccessRecords(executableTaskGroup.packages);
        displaySuccessfulPackages(executableTaskGroup.packages);
        return records;
    }

    Logger::instance().flushSync();

    std::set<std::string> unavailablePackages;
    std::string pluginFailureMessage;
    bool pluginReportedFailure = false;
    if (IPlugin* plugin = executor.registry->getPlugin(taskGroup.system); plugin != nullptr) {
        for (const PluginEventRecord& event : plugin->takeRecentEvents()) {
            if (event.name == "unavailable" && !event.payload.empty()) {
                unavailablePackages.insert(event.payload);
            } else if (event.name == "failed" && !event.payload.empty()) {
                pluginReportedFailure = true;
                pluginFailureMessage = event.payload;
            }
        }
    }

    std::vector<Package> succeededPackages;
    std::vector<Package> failedPackages = executableTaskGroup.packages;
    if (actionUsesMissingPackageFilter(taskGroup.action)) {
        IPlugin* plugin = executor.registry->getPlugin(taskGroup.system);
        if (plugin != nullptr) {
            const std::vector<Package> remainingMissingPackages =
                plugin->getMissingPackages(executableTaskGroup.packages);
            succeededPackages.clear();
            failedPackages.clear();
            for (const Package& package : executableTaskGroup.packages) {
                if (containsPackage(remainingMissingPackages, package)) {
                    failedPackages.push_back(package);
                } else {
                    succeededPackages.push_back(package);
                }
            }
        }
    }

    appendSuccessRecords(succeededPackages);
    displaySuccessfulPackages(succeededPackages);
    for (const Package& package : failedPackages) {
        const bool unavailable = unavailablePackages.find(packageRequestSpec(package)) != unavailablePackages.end();
        const std::string errorMessage = unavailable ? "package unavailable" : "plugin action failed";
        if (!pluginReportedFailure) {
            if (Logger::instance().quietTransactionTranscript()) {
                std::string reason = pluginFailureMessage;
                if (unavailable) {
                    reason = "package unavailable";
                } else if (reason.empty()) {
                    reason = "plugin action failed";
                }
                Logger::instance().displayItemFailure(package_item_id(taskGroup.system, package), reason);
            } else {
                Logger::instance().displayItemFailure(package_item_id(taskGroup.system, package),
                                                      package_failure_diagnostic(taskGroup.system, package.name,
                                                                                 unavailable));
            }
        }
        appendFailureRecord(package, errorMessage);
    }

    return records;
}
