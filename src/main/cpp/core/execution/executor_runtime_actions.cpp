#include "core/execution/executor.h"

#include "executor_internal.h"

#include <map>
#include <string>
#include <vector>

bool Executer::ensurePackages(const std::vector<Package>& packages) const {
    if (packages.empty()) {
        return true;
    }

    std::map<std::string, std::vector<Package>> packagesBySystem;
    for (Package package : packages) {
        if (package.name.empty()) {
            continue;
        }

        if (package.system.empty()) {
            return false;
        }

        if (package.action == ActionType::UNKNOWN) {
            package.action = ActionType::ENSURE;
        }

        const std::string resolvedSystem = this->registry->resolvePluginName(package.system);
        if (resolvedSystem.empty() || this->securityGateway.isGatewaySystem(resolvedSystem)) {
            continue;
        }

        package.system = resolvedSystem;
        package.directRequest = false;
        packagesBySystem[resolvedSystem].push_back(std::move(package));
    }

    for (const auto& [system, systemPackages] : packagesBySystem) {
        if (systemPackages.empty()) {
            continue;
        }

        if (this->registry->getPlugin(system) == nullptr || !this->registry->loadPlugin(system)) {
            return false;
        }

        IPlugin* plugin = this->registry->getPlugin(system);
        if (plugin == nullptr) {
            return false;
        }

        TaskGroup taskGroup;
        taskGroup.action = ActionType::ENSURE;
        taskGroup.system = system;
        taskGroup.plugin = plugin;
        taskGroup.packages = actionUsesMissingPackageFilter(taskGroup.action)
                                 ? plugin->getMissingPackages(systemPackages)
                                 : systemPackages;
        if (taskGroup.packages.empty()) {
            continue;
        }

        if (!this->dispatchTaskGroupToPlugin(taskGroup)) {
            return false;
        }
    }

    return true;
}

bool Executer::installRqpLocal(const std::string& localPath) const {
    if (localPath.empty()) {
        return false;
    }

    const std::string rqpSystem = this->registry->resolvePluginName("rqp");
    if (rqpSystem.empty()) {
        return false;
    }

    if (this->registry->getPlugin(rqpSystem) == nullptr || !this->registry->loadPlugin(rqpSystem)) {
        return false;
    }

    IPlugin* plugin = this->registry->getPlugin(rqpSystem);
    if (plugin == nullptr) {
        return false;
    }

    TaskGroup taskGroup;
    taskGroup.action = ActionType::INSTALL;
    taskGroup.system = rqpSystem;
    taskGroup.plugin = plugin;
    taskGroup.localPath = localPath;
    taskGroup.usesLocalTarget = true;
    return this->dispatchTaskGroupToPlugin(taskGroup);
}
