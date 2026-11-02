#pragma once

#include "platform/config/configuration.h"
#include "engine/scheduler/executor.h"
#include "workflows/reporting/audit_exporter.h"
#include "workflows/reporting/sbom_exporter.h"
#include "workflows/reporting/snapshot_exporter.h"
#include "engine/planner/planner.h"
#include "ecosystem/registry/registry.h"
#include <rqp/security/validator.h>

struct OrchestratorServices {
    Registry* registry {nullptr};
    Planner* planner {nullptr};
    Validator* validator {nullptr};
    Executer* executor {nullptr};
    AuditExporter* auditExporter {nullptr};
    SbomExporter* sbomExporter {nullptr};
    SnapshotExporter* snapshotExporter {nullptr};
    const ReqPackConfig* config {nullptr};
};
