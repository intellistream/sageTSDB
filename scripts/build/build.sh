#!/usr/bin/env bash
# Shared entrypoint for standalone, plugin and PECJ builds.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
PECJ_SOURCE="${PECJ_DIR:-}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
RUN_TESTS=OFF
ENABLE_PLUGINS=OFF
ENABLE_PECJ=OFF
ENABLE_PYTHON=OFF
CLEAN=OFF
INSTALL=OFF
CMAKE_ARGS=()

usage() {
    cat <<'USAGE'
Usage: scripts/build/build.sh [options] [-- CMAKE_OPTIONS...]
  --test              Build and run CTest
  --plugins           Build plugin adapters (stub unless PECJ is enabled)
  --pecj              Enable full PECJ integration; requires PECJ_DIR
  --pecj-dir DIR      Enable full PECJ integration using DIR
  --python            Build Python bindings using the current environment
  --build-dir DIR     Build directory (default: <repository>/build)
  --jobs N            Parallel build jobs
  --clean             Clean existing build targets before rebuilding
  --install           Install into <build-dir>/install
  --skip-test         Build without running CTest
  --help              Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test) RUN_TESTS=ON; shift ;;
        --plugins) ENABLE_PLUGINS=ON; shift ;;
        --pecj) ENABLE_PECJ=ON; shift ;;
        --pecj-dir) PECJ_SOURCE="$2"; ENABLE_PECJ=ON; shift 2 ;;
        --python) ENABLE_PYTHON=ON; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --clean) CLEAN=ON; shift ;;
        --install) INSTALL=ON; shift ;;
        --skip-test) RUN_TESTS=OFF; shift ;;
        --help|-h) usage; exit 0 ;;
        --) shift; CMAKE_ARGS=("$@"); break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 64 ;;
    esac
done

if [[ "${ENABLE_PECJ}" == ON && ! -d "${PECJ_SOURCE}" ]]; then
    echo "Set PECJ_DIR or pass --pecj-dir to an existing PECJ checkout." >&2
    exit 66
fi
if [[ -n "${PECJ_SOURCE}" && -d "${PECJ_SOURCE}" ]]; then
    PECJ_SOURCE="$(cd "${PECJ_SOURCE}" && pwd)"
fi
mkdir -p "${BUILD_DIR}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
if [[ "${CLEAN}" == ON && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake --build "${BUILD_DIR}" --target clean
fi
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON \
    -DBUILD_PYTHON_BINDINGS="${ENABLE_PYTHON}" \
    -DBUILD_PLUGINS="${ENABLE_PLUGINS}" \
    -DSAGE_TSDB_ENABLE_PECJ="${ENABLE_PECJ}" \
    -DPECJ_FULL_INTEGRATION="${ENABLE_PECJ}" -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR="${PECJ_SOURCE}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
if [[ "${RUN_TESTS}" == ON ]]; then
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi
if [[ "${INSTALL}" == ON ]]; then
    cmake --install "${BUILD_DIR}" --prefix "${BUILD_DIR}/install"
fi
