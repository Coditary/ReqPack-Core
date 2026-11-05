# ReqPack Core

C++ library extracted from ReqPack: orchestration, planning, execution, registry, plugins (LuaBridge), state, remote, export, and security bridge glue.

ReqPack (`rqp`) links this library together with the CLI and output layers from the main ReqPack repository.

[![Quality](https://github.com/Coditary/ReqPack-Core/actions/workflows/quality.yml/badge.svg)](https://github.com/Coditary/ReqPack-Core/actions/workflows/quality.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Coditary/ReqPack-Core/main/.github/badges/coverage.json)](https://github.com/Coditary/ReqPack-Core/actions/workflows/coverage.yml)

## Layout

```
src/main/cpp/core/       Domain implementation
src/main/cpp/plugins/    LuaBridge + plugin runtime
src/main/include/        Public headers (core/, plugins/)
tests/unit/              Library unit tests
tests/integration/       Library integration tests
tests/fixtures/          Shared plugin/config fixtures
```

## Build (standalone)

Requires the ReqPack CLI checkout for output sources (`REQPACK_CLI_DIR`), **Rust (`rustc`)** for the linked
**reqpack-security-core** CycloneDX FFI, and (for tests) Coditary **Shared-Cpp completion** (local monorepo layout
`~/Dev/Coditary/shared/Shared-Cpp/completion`, or clone [Coditary/Shared-Cpp](https://github.com/Coditary/Shared-Cpp)
and pass `-DCOMPLETION_DIR=…/completion` via the CLI’s `ResolveCompletion.cmake`).

In the ReqPack monorepo, CMake can also use the sibling `../reqpack-security-core` when present.

**CI and standalone clones** do not rely on that layout: workflows run
`.github/scripts/prepare-reqpack-core-checkouts.sh`, which clones **ReqPack CLI**, **reqpack-security-core** (Git ref from
`.github/reqpack-security-core.ref`), and checks out **Coditary/Shared-Cpp** for completion. Without those checkouts, CMake falls back to
**FetchContent** using the same ref file.

Pin or bump the security-core revision by editing `.github/reqpack-security-core.ref` (full commit SHA on
`Coditary/reqpack-security-core`).

```bash
cmake -S . -B build \
  -DREQPACK_CORE_BUILD_TESTS=ON \
  -DREQPACK_CLI_DIR=../reqpack \
  -DSOL2_INCLUDE_DIR_OVERRIDE=/path/to/sol2/include
cmake --build build
ctest --test-dir build --output-on-failure
```

Or:

```bash
make build
make test-unit
make test-integration
make test-coverage   # unit tests + gcovr summary (>=85% tracked)
make qa              # format-check + lint + unit tests + fuzz
```

## Quality tooling

| Target | Description |
| --- | --- |
| `make format-check` | Verify `clang-format` (dry run) |
| `make format` | Apply `clang-format` in place |
| `make lint` | Run `clang-tidy` via `build/lint/compile_commands.json` |
| `make fuzz` | libFuzzer smoke run (Clang required) |
| Fuzz targets | `action_tokens`, `registry_json`, `manifest_loader`, `rq_metadata_json`, `reqpack_hooks`, `plugin_bundle_metadata`, `cli_argv` |
| `make test-coverage` | Debug build with gcovr summary + badge update |
| `make qa` | `format-check` + `lint` + `test-unit` + `fuzz` |

Coverage excludes TCP remote serve/client glue, hermetic plugin test runner E2E paths, full orchestrator subprocess flows, platform host probing, archive/process subprocess helpers, and a few plugin runtime dispatch backends that are exercised by integration tests. See `tests/coverage_summary.py` for the full exclusion list.

## Integration with ReqPack

ReqPack resolves this library automatically (local sibling, `-DREQPACK_CORE_DIR=…`, or FetchContent from GitHub). Core unit/integration tests use the `core::unit::` and `core::integration::` CTest prefixes. ReqPack keeps CLI output tests (`cli::unit::`) and system tests.

## Related

- [ReqPack](https://github.com/Coditary/ReqPack) — `rqp` CLI
- [reqpack-security-core](https://github.com/Coditary/reqpack-security-core) — vulnerability scanning library
