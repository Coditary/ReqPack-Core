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

    local candidate=""
    for path in \
        "${REQPACK_SHARED_CPP_DIR:-}" \
        "${workspace}/coditary-shared" \
        "${workspace}/shared" \
        "${REQPACK_MONOREPO_ROOT:-}/../Coditary/shared" \
        "${REQPACK_MONOREPO_ROOT:-}/../../Coditary/shared" \
        "${HOME}/Dev/Coditary/shared"; do
        if [ -n "${path}" ] && [ -f "${path}/Shared-Cpp/completion/CMakeLists.txt" ]; then
            candidate="${path}"
            break
        fi
    done

    if [ -n "${candidate}" ]; then
        ln -snf "${candidate}" "${coditary_root}/shared"
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

GitHub Actions must check out Coditary/shared before this script (see quality.yml):
  actions/checkout@v4 with repository: Coditary/shared, path: coditary-shared

For a private shared repo, configure repository secret REQPACK_SHARED_REPO_TOKEN
(classic PAT or fine-grained token with read access to Coditary/shared).
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
