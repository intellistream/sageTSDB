#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
exec python3 "${PROJECT_ROOT}/examples/task_demos/run_demos.py" \
    --output "${PROJECT_ROOT}/examples/outputs/task_demos" "$@"
