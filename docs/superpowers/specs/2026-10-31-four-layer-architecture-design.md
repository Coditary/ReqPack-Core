# ReqPack-Core: Vier-Schichten-Architektur

**Status:** Approved (Spezifikation vom Maintainer, 2026-10-31)

## Problem

- 18 flache Ordner unter `core/` ohne Dependency-Hierarchie
- God-Classes über `*_internal.h` statt Module
- Drei Plugin-Orte: `core/plugins/`, `cpp/plugins/`, `core/luaenv/`
- Bedeutungsloses `*_core`-Suffix
- `include/core/security/` ohne Implementierung in Core (Re-Exports)

## Ziel: Dependency Rule (top → bottom only)

1. **workflows** — Orchestrator, Query, Reporting, Remote  
2. **engine** — Resolution, Planner, Scheduler, Security-Fassade  
3. **ecosystem** — Manifest, Registry, rqp_format, storage, plugin_host  
4. **platform** — Config, common, net, archive (kein Paket-Domänenwissen)

## Verzeichnisziel

```
ReqPack-Core/
├── include/reqpack/          # schmale Public API für rqp (Langfrist)
├── src/
│   ├── platform/
│   ├── ecosystem/
│   │   └── plugin_host/{api,lua,native_rqp,testing}/
│   ├── engine/
│   └── workflows/
└── tests/                    # spiegelt src/ (Langfrist)
```

Legacy bis Migration abgeschlossen: `src/main/include` + `src/main/cpp/core/` bleiben kompatibel, bis Schichten vollständig verschoben sind.

## Public API vs. CLI-Realität

Langfristig nur wenige Fassaden in `include/reqpack/`. **Heute** inkludiert `reqpack/` direkt ~25 Core-Header (Configuration, Registry, Remote, …). Migration: erst Core restrukturieren, dann CLI auf Fassaden umstellen (oder Übergangs-Forwarder unter `src/main/include/`).

## LOC-Hebel (spätere Phasen)

- Generischer Export-Writer (audit/sbom/snapshot)
- `LmdbStore` + JSON-Codec in `ecosystem/storage/`
- Workflow-Klassen statt Orchestrator/Executor-Splitting
- Zentraler `LuaTableMapper` in platform
- Downloader nur Bytes; Materialize nur in `plugin_host`

## CMake

Vier interne Static-Libraries: `reqpack_platform` → `reqpack_ecosystem` → `reqpack_engine` → `reqpack_workflows` → `reqpack_core` (Alias unverändert).
