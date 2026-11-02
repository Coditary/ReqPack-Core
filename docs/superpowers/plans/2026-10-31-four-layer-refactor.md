# Four-Layer Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ReqPack-Core in vier Schichten (platform → ecosystem → engine → workflows) mit vereinheitlichtem `plugin_host`, schmaler Public API und CMake-erzwungener Dependency Rule.

**Architecture:** Inkrementelle Migration: zuerst Subsysteme zusammenführen und Includes stabilisieren, dann Storage/Export deduplizieren, dann God-Objects schneiden, zuletzt vier CMake-Targets. Legacy-Pfade (`src/main/…`) bis Phase B nur per Forwarder/Compat.

**Tech Stack:** C++20, CMake 3.15+, Catch2, bestehende reqpack-security-core-Anbindung.

## Global Constraints

- Nach jedem Phase-Checkpoint: `cd ReqPack-Core && cmake --build build && ctest --test-dir build -R 'core::'`
- Minimale Diffs; bestehende Namenskonventionen in geänderten Dateien beibehalten
- Kein force-push; Commits nur auf Anfrage
- `reqpack/` CLI bleibt buildbar (Geschwister-Checkout)

---

### Phase 1: Public vs. internal headers (Compat)

**Files:**
- Create: `include/reqpack/types.h`, `config.h`, `orchestrator.h`, `host_events.h` (Forwarder)
- Modify: `ReqPack-Core/CMakeLists.txt`, `reqpack/CMakeLists.txt` (zusätzlicher `include/reqpack` PUBLIC)
- Later: interne Header aus `src/main/include` neben `.cpp` (modulweise)

- [x] **Step 1:** CMake PUBLIC `include/reqpack` + BUILD_INTERFACE behalten für `src/main/include`
- [x] **Step 2:** Forwarder-Header anlegen (keine Logik)
- [x] **Step 3:** Modul `planning/planner_internal.h` als Pilot: nur aus `.cpp` inkludieren
- [x] **Step 4:** `make test-unit` grün

### Phase 2: `ecosystem/plugin_host` (Plugin-Welten vereinen)

**Files:**
- Move: `src/main/cpp/core/plugins/*` → `src/ecosystem/plugin_host/testing/`
- Move: `src/main/cpp/plugins/*` → `src/ecosystem/plugin_host/{api,lua,native_rqp}/`
- Move: `src/main/cpp/core/luaenv/lua_env_ffi.cpp` → `src/ecosystem/plugin_host/lua/`
- Move headers: `src/main/include/plugins/*`, `src/main/include/core/plugins/*`, `src/main/include/core/luaenv/*` → passende Unterordner
- Modify: `CMakeLists.txt` GLOB, alle `#include "plugins/…"` / `#include "core/plugins/…"` / `#include "core/luaenv/…"`
- Modify: `tests/fuzz` manifest_loader source path
- Compat: dünne Forwarder unter `src/main/include/plugins/iplugin.h` etc. (optional, 1 Release)

**Include-Konvention:** PRIVATE `target_include_directories(reqpack_core PRIVATE ${CMAKE_SOURCE_DIR}/src)`; öffentliche Plugin-API weiter über `src/main/include` bis Phase 1 abgeschlossen.

- [x] **Step 1:** Verzeichnisbaum anlegen + `git mv`
- [x] **Step 2:** CMake + Test-Include-Pfade
- [x] **Step 3:** Repo-weites Include-Update (ReqPack-Core + reqpack grep)
- [x] **Step 4:** `ctest` grün

### Phase 3: Storage + Export-Basis

**Files:**
- Create: `src/ecosystem/storage/lmdb_store.hpp`, `json_codec_helpers.hpp`
- Create: `src/workflows/reporting/table_formatter.cpp`, `file_output_writer.cpp`
- Refactor: `registry_database_storage`, `transaction_database_storage`, `history_manager_entry_codec`, `rqp_state_store_load`, drei Exporter

- [x] **Step 1:** LmdbStore + Tests portieren aus transaction/registry storage
- [x] **Step 2:** Registry auf LmdbStore
- [x] **Step 3:** Transaction + History + rqp_state
- [x] **Step 4:** Generischer Report-Writer; Audit/SBOM/Snapshot Mapper
- [x] **Step 5:** Alte `*_storage.cpp` / duplizierte `*_write.cpp` löschen

### Phase 4: Engine / Workflows split

**Files:**
- Create: `src/workflows/{orchestrator,query,reporting,remote}/`
- Create: `src/engine/{resolution,planner,scheduler,security}/`
- Dissolve: `orchestrator_internal.h`, `executor_internal.h` → Workflow- + Scheduler-Klassen

- [x] **Step 1:** `git mv` execution/query/export/remote/planning in engine/workflows
- [x] **Step 2:** `MutationWorkflow`, `QueryWorkflow`, `PackWorkflow`, `ComplianceWorkflow`
- [x] **Step 3:** `TaskGraphBuilder`, `ParallelTaskScheduler`, `TransactionalRunner`
- [x] **Step 4:** Orchestrator < 200 LOC Dispatcher

### Phase 5: Platform + CMake layers

**Files:**
- Move: archive, common, config, download → `src/platform/`
- Move: manifest, registry, packages → `src/ecosystem/` (packages → `rqp_format/`)
- Modify: `CMakeLists.txt` — `reqpack_platform`, `reqpack_ecosystem`, `reqpack_engine`, `reqpack_workflows`
- Rename: `*_core.cpp` → `*_policy` / `*_engine` / `*_store` (modulweise)

- [x] **Step 1:** Source-Zuordnung in `cmake/reqpack_layers.cmake`
- [x] **Step 2:** Vier Targets linken `workflows → engine → ecosystem → platform`
- [x] **Step 3:** Tests unter `tests/` spiegeln (schrittweise)

### Phase 6: CLI auf `include/reqpack/` (reqpack repo)

- [x] **Step 1:** CLI-Includes auf Fassaden umstellen
- [x] **Step 2:** Legacy-Forwarder entfernen

---

## Execution Handoff

Plan gespeichert. Empfohlen: **Phase 2 zuerst implementieren** (größter Strukturgewinn), dann Phase 3 (LOC), dann 4/5 parallelisierbar.
