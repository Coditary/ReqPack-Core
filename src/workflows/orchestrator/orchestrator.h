#pragma once

#include "platform/config/configuration.h"
#include "engine/scheduler/executor.h"
#include "workflows/reporting/audit_exporter.h"
#include "workflows/reporting/sbom_exporter.h"
#include "workflows/reporting/snapshot_exporter.h"
#include "engine/planner/planner.h"
#include "ecosystem/registry/registry.h"
#include "ecosystem/registry/registry_database.h"
#include <rqp/security/validator.h>

#include "platform/common/types.h"

class Orchestrator {

    Registry* registry;
    Planner* planner;
    AuditExporter* auditExporter;
    SbomExporter* sbomExporter;
    SnapshotExporter* snapshotExporter;
    Validator* validator;
    Executer* executor;
    ReqPackConfig config;
    std::vector<Request> requests;

  public:
    Orchestrator(std::vector<Request> requests, const ReqPackConfig& config = default_reqpack_config());
    ~Orchestrator();

    int countRequestedItems() const;
    int run();

  private:
    bool shouldRefreshMainRegistry() const;
    bool shouldRefreshPluginWrappers() const;
    bool shouldRunSystemWidePackageUpdates() const;
    int runPluginInstallRequests();
    int runPluginRemoveRequests();
    int runPluginWrapperRefresh();
    int runSystemWidePackageUpdates();

    friend class OrchestratorDispatcher;
};
