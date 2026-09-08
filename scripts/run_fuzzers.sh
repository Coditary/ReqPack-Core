#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build/fuzz}"
RUNS="${FUZZ_RUNS:-4096}"
MAX_TIME="${FUZZ_MAX_TIME:-20}"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Missing fuzz build directory: $BUILD_DIR — run 'make fuzz-build' first." >&2
    exit 1
fi

declare -a fuzzers=(
    action_tokens_fuzzer
    registry_json_fuzzer
    manifest_loader_fuzzer
)

for fuzzer in "${fuzzers[@]}"; do
    bin="$BUILD_DIR/$fuzzer"
    corpus="$ROOT/tests/fuzz/corpus/${fuzzer%_fuzzer}"
    if [[ ! -x "$bin" ]]; then
        echo "Missing fuzzer binary: $bin" >&2
        exit 1
    fi

    args=(-runs="$RUNS" -max_total_time="$MAX_TIME" -timeout=5 -rss_limit_mb=512)
    if [[ -d "$corpus" ]]; then
        args+=("$corpus")
    fi

    echo "Running $fuzzer (${RUNS} runs, max ${MAX_TIME}s)..."
    "$bin" "${args[@]}"
done

echo "All fuzzers completed."
