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

if ((${#files[@]} == 0)); then
    echo "No source files selected for clang-format check" >&2
    exit 1
fi

echo "Checking clang-format on ${#files[@]} files..."
clang-format --dry-run --Werror "${files[@]}"
echo "clang-format check passed."
