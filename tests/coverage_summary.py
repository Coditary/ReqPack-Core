#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

TRACKED_PREFIX = "src/main/cpp/"

DEFAULT_EXCLUDE_SUFFIXES = (
    # TCP remote server/client and socket glue (covered via ReqPack system tests).
    "core/remote/remote_client.cpp",
    "core/remote/serve_remote.cpp",
    "core/remote/serve_remote_auth.cpp",
    "core/remote/serve_remote_command.cpp",
    "core/remote/serve_remote_protocol.cpp",
    "core/remote/serve_remote_session.cpp",
    "core/remote/serve_remote_socket.cpp",
    "core/remote/serve_remote_upload.cpp",
    "core/remote/serve_remote_internal.h",
    # Hermetic plugin test runner (E2E via rqp test-plugin).
    "core/plugins/plugin_test_runner.cpp",
    "core/plugins/plugin_test_runner_case.cpp",
    "core/plugins/plugin_test_runner_cli.cpp",
    "core/plugins/plugin_test_runner_report.cpp",
    "core/plugins/plugin_test_runner_internal.h",
    # Full orchestrator subprocess flows (integration with built rqp binary).
    "core/execution/orchestrator_analysis.cpp",
    "core/execution/orchestrator_pack.cpp",
    "core/execution/orchestrator_query.cpp",
    # Platform-specific host probing.
    "core/host/host_info_probe.cpp",
    # Subprocess/archive extraction glue.
    "core/common/process_runner.cpp",
    "core/common/pipe_helpers.cpp",
    "core/archive/archive_resolver_process.cpp",
    "core/archive/archive_resolver.cpp",
    "core/archive/archive_resolver_extract.cpp",
    # Full orchestrator and executor subprocess flows (integration / system tests).
    "core/execution/orchestrator.cpp",
    "core/execution/orchestrator_request_prep.cpp",
    "core/execution/orchestrator_plugin_flows.cpp",
    "core/execution/executor_run.cpp",
    "core/execution/executor_dispatch.cpp",
    "core/execution/executor_history_sync.cpp",
    "core/execution/executor_parallel.cpp",
    "core/execution/executor_task_scheduler.cpp",
    "core/execution/executor_transaction_recovery.cpp",
    "core/execution/executor_transaction_state.cpp",
    "core/execution/executor_transactional_dispatch.cpp",
    "core/execution/executor_internal.h",
    # Git/network registry sync and materialization.
    "core/registry/registry_database_git.cpp",
    "core/registry/registry_database_main_registry.cpp",
    "core/registry/registry_materialize.cpp",
    "core/registry/registry_database.cpp",
    # Network download backends.
    "core/download/downloader_plugin.cpp",
    "core/download/downloader_transfer.cpp",
    "core/download/downloader_transfer_progress.cpp",
    # Export writers (file IO integration).
    "core/export/snapshot_exporter_write.cpp",
    "core/export/sbom_exporter_write.cpp",
    "core/export/audit_exporter_write.cpp",
    # Planner setup and history log IO.
    "core/planning/planner_request_setup.cpp",
    "core/history/history_manager_log.cpp",
    # PTY/subprocess exec rule runner (parser/evaluator covered by unit tests under tests/unit/plugins/exec_rules/).
    "plugins/exec_rules.cpp",
    # Executor query orchestration (covered via integration orchestrator tests).
    "core/execution/executor_query_ops.cpp",
    # Platform host snapshot assembly (host_info_probe excluded above).
    "core/host/host_info.cpp",
    # libcurl download entrypoints (downloader unit tests cover policy helpers).
    "core/download/downloader.cpp",
    "plugins/lua_bridge_host_runtime.cpp",
    # Curl/archive/download hook runtime and Lua plugin API dispatch (covered via service + integration tests).
    "plugins/rqp_plugin_hooks.cpp",
    "plugins/lua_bridge_plugin_api.cpp",
    "plugins/lua_bridge_execution_policy.cpp",
    # LMDB/registry persistence backends (serialization covered by dedicated unit tests).
    "core/state/transaction_database_write.cpp",
    "core/registry/registry_database_storage.cpp",
    "core/registry/registry_runtime.cpp",
    # History manager delegates log IO to excluded history_manager_log.cpp.
    "core/history/history_manager.cpp",
)


def badge_color(coverage: float) -> str:
    if coverage >= 90.0:
        return "brightgreen"
    if coverage >= 85.0:
        return "green"
    if coverage >= 80.0:
        return "yellowgreen"
    if coverage >= 70.0:
        return "yellow"
    if coverage >= 60.0:
        return "orange"
    return "red"


def write_badge_json(path: Path, coverage: float) -> None:
    payload = {
        "schemaVersion": 1,
        "label": "coverage",
        "message": f"{coverage:.2f}%",
        "color": badge_color(coverage),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def is_excluded(relative_path: str, exclude_suffixes: tuple[str, ...]) -> bool:
    normalized = relative_path.replace("\\", "/")
    return any(normalized.endswith(suffix) or suffix in normalized for suffix in exclude_suffixes)


def normalize_gcovr_filename(source_dir: Path, filename: str) -> str | None:
    normalized = filename.replace("\\", "/")
    markers = (
        "/ReqPack-Core/src/main/cpp/",
        "/reqpack-core/src/main/cpp/",
    )
    for marker in markers:
        if marker in normalized:
            return "src/main/cpp/" + normalized.split(marker, 1)[1]
    if normalized.startswith("src/main/cpp/"):
        return normalized
    source_root = source_dir.resolve().as_posix()
    if normalized.startswith(source_root + "/src/main/cpp/"):
        return normalized[len(source_root) + 1 :]
    return None


def run_gcovr_summary(
    build_dir: Path,
    source_dir: Path,
    exclude_suffixes: tuple[str, ...],
) -> tuple[float, int, int, list[tuple[float, int, int, str]]]:
    command = [
        "gcovr",
        "-r",
        str(source_dir),
        "--object-directory",
        str(build_dir),
        "--gcov-ignore-parse-errors",
        "negative_hits.warn",
        "--json-summary-pretty",
        "--include",
        "src/main/cpp/.*",
        "--exclude",
        "build/.*",
        "--exclude",
        "tests/.*",
        "--exclude",
        "_deps/.*",
    ]

    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "gcovr failed")

    payload = json.loads(result.stdout)
    rows: list[tuple[float, int, int, str]] = []
    total_covered = 0
    total_count = 0

    for entry in payload.get("files", []):
        relative = normalize_gcovr_filename(source_dir, entry.get("filename", ""))
        if relative is None or not relative.startswith(TRACKED_PREFIX):
            continue
        if is_excluded(relative, exclude_suffixes):
            continue

        covered = int(entry.get("line_covered", 0))
        total = int(entry.get("line_total", 0))
        if total <= 0:
            continue

        coverage = (covered / total) * 100.0
        total_covered += covered
        total_count += total
        rows.append((coverage, covered, total, relative))

    if not rows:
        raise RuntimeError("gcovr completed, but no tracked source file entries were parsed")

    rows.sort(key=lambda row: (row[0], row[3]))
    overall = (total_covered / total_count) * 100.0 if total_count else 0.0
    return overall, total_covered, total_count, rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize ReqPack-Core coverage from gcov data")
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("--badge-json", type=Path, help="Write Shields endpoint JSON badge to this path")
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Relative path suffix to exclude from coverage totals (may be repeated)",
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    source_dir = args.source_dir.resolve()
    exclude_suffixes = tuple(DEFAULT_EXCLUDE_SUFFIXES) + tuple(args.exclude)

    try:
        overall, total_covered, total_count, rows = run_gcovr_summary(build_dir, source_dir, exclude_suffixes)
    except FileNotFoundError:
        print("gcovr is required for coverage summaries but was not found on PATH", file=sys.stderr)
        return 1
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1

    if args.badge_json is not None:
        write_badge_json(args.badge_json.resolve(), overall)

    print(
        f"Coverage summary: {overall:.2f}% ({total_covered}/{total_count} lines) "
        f"across {len(rows)} source files"
    )
    print(f"Coverage build: {build_dir}")
    print("Lowest covered files:")
    for coverage, tested, total, relative in rows[:10]:
        print(f"  {coverage:6.2f}% ({tested:4d}/{total:4d})  {relative}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
