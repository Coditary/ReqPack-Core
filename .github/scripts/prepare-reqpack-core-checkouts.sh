#!/usr/bin/env bash
# Prepare sibling checkouts expected by ReqPack-Core CMake (CLI output + security-core).
# GitHub Actions: clones from GitHub. nektos/act: symlinks from REQPACK_MONOREPO_ROOT.
set -euo pipefail

workspace="${1:-${GITHUB_WORKSPACE:-$(pwd)}}"
cd "${workspace}"

prepare_shared_completion() {
    local coditary_root
    coditary_root="$(cd "${workspace}/../.." && pwd)/Coditary"
    mkdir -p "${coditary_root}"

    if [ -f "${coditary_root}/shared/Shared-Cpp/completion/CMakeLists.txt" ]; then
        return 0
    fi

    shared_completion_cmake() {
        local root="$1"
        [ -f "${root}/Shared-Cpp/completion/CMakeLists.txt" ] \
            || [ -f "${root}/completion/CMakeLists.txt" ]
    }

    link_shared_completion() {
        local path="$1"
        mkdir -p "${coditary_root}/shared"
        if [ -f "${path}/Shared-Cpp/completion/CMakeLists.txt" ]; then
            ln -snf "${path}" "${coditary_root}/shared"
        elif [ -f "${path}/completion/CMakeLists.txt" ]; then
            # GitHub: Coditary/Shared-Cpp (completion/ at repo root)
            ln -snf "${path}" "${coditary_root}/shared/Shared-Cpp"
        else
            return 1
        fi
    }

    local candidate=""
    for path in \
        "${REQPACK_SHARED_CPP_DIR:-}" \
        "${workspace}/coditary-shared" \
        "${workspace}/shared" \
        "${REQPACK_MONOREPO_ROOT:-}/../Coditary/shared" \
        "${REQPACK_MONOREPO_ROOT:-}/../../Coditary/shared" \
        "${HOME}/Dev/Coditary/shared"; do
        if [ -n "${path}" ] && shared_completion_cmake "${path}"; then
            candidate="${path}"
            break
        fi
    done

    if [ -n "${candidate}" ]; then
        link_shared_completion "${candidate}"
        return 0
    fi

    if [ "${ACT:-}" = "true" ]; then
        echo "Shared-Cpp completion not found for act. Expected one of:" >&2
        echo "  ${REQPACK_MONOREPO_ROOT:-}/../Coditary/shared" >&2
        echo "  ${HOME}/Dev/Coditary/shared" >&2
        echo "  or existing ${coditary_root}/shared" >&2
        exit 1
    fi

    cat >&2 <<EOF
Shared-Cpp completion not found.

GitHub Actions must check out Coditary/Shared-Cpp before this script (see quality.yml):
  actions/checkout@v4 with repository: Coditary/Shared-Cpp, path: coditary-shared
EOF
    exit 1
}

if [ "${ACT:-}" = "true" ]; then
    : "${REQPACK_MONOREPO_ROOT:?REQPACK_MONOREPO_ROOT must be set when ACT=true}"
    rm -rf reqpack-cli reqpack-security-core
    cp -a "${REQPACK_MONOREPO_ROOT}/reqpack" reqpack-cli
    ln -snf "${REQPACK_MONOREPO_ROOT}/reqpack-security-core" reqpack-security-core
    prepare_shared_completion
    exit 0
fi

security_core_ref="main"
if [ -f .github/reqpack-security-core.ref ]; then
    security_core_ref="$(tr -d '[:space:]' < .github/reqpack-security-core.ref)"
fi

rm -rf reqpack-cli reqpack-security-core
git clone --depth 1 https://github.com/Coditary/ReqPack.git reqpack-cli
git clone --depth 1 "https://github.com/Coditary/reqpack-security-core.git" reqpack-security-core
(
    cd reqpack-security-core
    git fetch --depth 1 origin "${security_core_ref}"
    git checkout FETCH_HEAD
)

prepare_shared_completion
