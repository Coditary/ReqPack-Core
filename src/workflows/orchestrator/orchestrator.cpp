#include "workflows/orchestrator/orchestrator.h"

#include "engine/security/security_bridge.h"

#include "orchestrator_dispatcher.hpp"

#include <utility>
#include <vector>

Orchestrator::Orchestrator(std::vector<Request> requests, const ReqPackConfig& config)
    : config(config), requests(std::move(requests)) {
    this->registry = new Registry(this->config);
    this->planner = new Planner(this->registry, this->registry->getDatabase(), this->config);
    this->auditExporter = new AuditExporter(this->registry, this->config);
    this->sbomExporter = new SbomExporter(this->registry, this->config);
    this->snapshotExporter = new SnapshotExporter(this->config);
    this->validator = new Validator(this->registry, security_settings_from(this->config));
    this->executor = new Executer(this->registry, this->config);
}

Orchestrator::~Orchestrator() {
    delete this->registry;
    delete this->planner;
    delete this->auditExporter;
    delete this->sbomExporter;
    delete this->snapshotExporter;
    delete this->validator;
    delete this->executor;
}

int Orchestrator::countRequestedItems() const {
    int count = 0;
    for (const Request& request : this->requests) {
        if (request.action != ActionType::INSTALL && request.action != ActionType::ENSURE &&
            request.action != ActionType::REMOVE && request.action != ActionType::UPDATE &&
            request.action != ActionType::LIST && request.action != ActionType::OUTDATED) {
            continue;
        }
        if (request.usesLocalTarget) {
            ++count;
            continue;
        }
        count += static_cast<int>(request.packages.size());
    }
    return count;
}

int Orchestrator::run() {
    return OrchestratorDispatcher::run(*this);
}
