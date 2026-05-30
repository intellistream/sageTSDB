#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build}"
PECJ_DIR="${PECJ_DIR:-${PROJECT_ROOT}/../PECJ}"
DO_BUILD=true
FULL_PECJ=ON

EVENTS=48
WINDOWS=6
WINDOW_US=12000
SLIDE_US=6000
THRESHOLD=3.0

usage() {
    cat <<USAGE
Usage: $0 [options]

Options:
  --no-build          Run the existing executable without rebuilding
    --stub-pecj         Build the PECJ integrated API without PECJ_FULL_INTEGRATION
  --build-dir <path>  CMake build directory (default: build)
  --pecj-dir <path>   PECJ checkout path (default: ../PECJ)
  --events <n>        Events per stream (default: 48)
  --windows <n>       PECJ windows to execute (default: 6)
  --window-us <n>     PECJ window length in microseconds (default: 12000)
  --slide-us <n>      PECJ window slide in microseconds (default: 6000)
  --threshold <v>     SRTFD anomaly threshold (default: 3.0)
  --help              Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            DO_BUILD=false
            shift
            ;;
        --stub-pecj)
            FULL_PECJ=OFF
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --pecj-dir)
            PECJ_DIR="$2"
            shift 2
            ;;
        --events)
            EVENTS="$2"
            shift 2
            ;;
        --windows)
            WINDOWS="$2"
            shift 2
            ;;
        --window-us)
            WINDOW_US="$2"
            shift 2
            ;;
        --slide-us)
            SLIDE_US="$2"
            shift 2
            ;;
        --threshold)
            THRESHOLD="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ "$DO_BUILD" == true ]]; then
    cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
        -DBUILD_TESTS=OFF \
        -DSAGE_TSDB_ENABLE_SRTFD=ON \
        -DSAGE_TSDB_ENABLE_PECJ=ON \
        -DPECJ_MODE=INTEGRATED \
        -DPECJ_FULL_INTEGRATION="$FULL_PECJ" \
        -DPECJ_DIR="$PECJ_DIR" \
        -DCMAKE_BUILD_TYPE=Release

    cmake --build "$BUILD_DIR" --target pecj_srtfd_showcase_demo -j"$(nproc)"
fi

DEMO_BIN="${BUILD_DIR}/examples/pecj_srtfd_showcase_demo"
if [[ ! -x "$DEMO_BIN" ]]; then
    echo "Demo executable not found: $DEMO_BIN" >&2
    echo "Run without --no-build or check the build output." >&2
    exit 1
fi

"$DEMO_BIN" \
    --events "$EVENTS" \
    --windows "$WINDOWS" \
    --window-us "$WINDOW_US" \
    --slide-us "$SLIDE_US" \
    --threshold "$THRESHOLD"