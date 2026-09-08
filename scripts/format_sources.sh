#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found in PATH" >&2
    exit 127
fi

mapfile -t files < <(
    find src/main tests -type f \( -name '*.cpp' -o -name '*.h' \) \
        ! -path '*/build/*' ! -path '*/_deps/*' \
        | sort
)

clang-format -i "${files[@]}"
echo "Formatted ${#files[@]} files."
