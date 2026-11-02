# ReqPack-Core Consolidation (Hard Cut) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Each checkpoint must show **net file/LOC reduction**, not new wrappers.

**Goal:** ~135 `.cpp`-Fragmente und 64 `src/main/include`-Header auf ~42 Module reduzieren; `src/main/` vollständig entfernen; keine `*_internal.h`-Splits; einheitlich `.h`.

**Architecture:** Keine Forwarder. Alte Pfade werden gelöscht, nicht gespiegelt. Includes wechseln auf `platform/`, `ecosystem/`, `engine/`, `workflows/` unter `src/`. Public API bleibt nur `include/reqpack/` (4 Host-Header + schmale Fassaden).

## Global Constraints

- **Regel 1:** Ordnername nicht im Dateinamen wiederholen (`config/config.h`, nicht `configuration_loader.cpp`).
- **Regel 2:** Kein `*_internal.h`; max. ein `.cpp` pro Klasse; >800 LOC → zweite **Klasse**, nicht acht Dateien.
- **Regel 3:** Nur `.h` für interne Header neben `.cpp`; kein `src/main/include/`.
- **DoD pro Subsystem:** `find src -name '<old_basename>*'` leer; `git diff --stat` zeigt Löschungen >> Additionen.
- Nach jedem Checkpoint: `cd ReqPack-Core && cmake --build build && ctest --test-dir build -R 'core::'`

---

## Phase 7A: Tod von `src/main/include/` (Pflicht zuerst)

**Definition of Done:** `test ! -d src/main`; CMake ohne `src/main/include`; kein `#include "core/` in `ReqPack-Core/src` oder `tests`.

- [ ] **7A.1:** Alle echten Header `git mv` nach `src/{platform,ecosystem,engine,workflows}/…`
- [ ] **7A.2:** `_core.h`-Ketten löschen; Includes auf kanonische Header (`*_store.h`, `planner_engine.h`, `downloader_engine.h`)
- [ ] **7A.3:** `core/security/*`-Zombies entfernen; direkt `<rqp/security/…>`; `security_bridge.h` nach `engine/security/`
- [ ] **7A.4:** Repo-weites Include-Rewrite; `include/reqpack/*` anpassen
- [ ] **7A.5:** CMake (`reqpack_layers.cmake`, `CMakeLists.txt`) — nur `include/reqpack` + `src`
- [ ] **7A.6:** `git rm -rf src/main`

## Phase 7B: Scheduler — Executor streichen

**DoD:** Keine `executor_*.cpp`; kein `Executer`-Wrapper; `MutationWorkflow` → `TransactionalRunner` direkt.

| Ziel | Verschmilzt |
|------|-------------|
| `engine/scheduler/task_graph.cpp` | `task_graph_builder.cpp` + `executor_task_graph.cpp` |
| `engine/scheduler/task_scheduler.cpp` | `parallel_task_scheduler.cpp` + `executor_parallel.cpp`, `executor_task_scheduler.cpp`, `executor_dispatch.cpp`, `executor_runtime_actions.cpp` |
| `engine/scheduler/transactional_runner.cpp` | `transactional_runner.cpp` + `executor.cpp`, `executor_run.cpp`, `executor_transactional_dispatch.cpp`, `executor_transaction_recovery.cpp`, `executor_transaction_state.cpp`, `executor_history_sync.cpp`, `executor_update.cpp`, `executor_internal.h` |
| `workflows/query/query_workflow.cpp` | + `executor_query_ops.cpp` |

## Phase 7C: Orchestrator & Reporting

| Ziel | Verschmilzt |
|------|-------------|
| `workflows/orchestrator/orchestrator.cpp` | `orchestrator.cpp`, `orchestrator_dispatcher.cpp`, `orchestrator_request_prep.cpp`, `orchestrator_internal.h`, `orchestrator_services.hpp` |
| `workflows/orchestrator/mutation_workflow.cpp` | + `orchestrator_plugin_flows.cpp` |
| `workflows/orchestrator/compliance_workflow.cpp` | + `orchestrator_analysis.cpp`, `orchestrator_compliance_prep.hpp` |
| `workflows/reporting/report_writer.cpp` | `file_output_writer.cpp`, `report_format_common.cpp`, `compliance_report_output.cpp`, table-Renderer |
| `workflows/reporting/exporters.cpp` | 12 Exporter-Fragmente + 3 `*_internal.h` |

## Phase 7D: Platform (~15 → ~8 Module)

Siehe Spezifikation Nutzer: `archive.cpp`, `config.cpp`, `config_loader.cpp`, `host_info.cpp`, `http_client.cpp` (download), `process.cpp`, `crypto.cpp`, `semver.cpp`, `env.cpp`; `downloader_plugin*` → `ecosystem/registry/sync.cpp`.

## Phase 7E: Ecosystem (~50 → ~12 Module)

`storage/` absorbiert `state/` + `history/`; `rqp_format/packages/` flach; registry/sync/package/repository/lua/native_rqp/test_runner Konsolidierung laut Tabelle.

## Phase 7F: `.hpp` → `.h` unter `src/`

Einzeiler-Rename; Sol-Third-Party unberührt.

---

## Metriken (Baseline 2026-10-31)

| Metrik | Ist |
|--------|-----|
| `src/**/*.cpp` + `.h` (ohne main) | 236 |
| `src/main/include` | 64 |
| `*_internal.h` in ReqPack-Core/src | 16 (+3 Duplikate in main) |
| `engine/scheduler/executor_*.cpp` | 14 |

**Ziel:** ≤42 `.cpp`-Module mit passenden `.h`; ≤120 Header gesamt unter `src/`; 0 unter `src/main`.
