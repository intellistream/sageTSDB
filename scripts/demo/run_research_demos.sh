#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RESEARCH_PYTHON="${PYTHON:-python3}"
if ! "${RESEARCH_PYTHON}" -c 'import torch' >/dev/null 2>&1; then
    echo "PyTorch is required. Set PYTHON to an interpreter with PyTorch 1.13 or newer." >&2
    exit 69
fi
DEFAULT_COMPONENT="${PROJECT_ROOT}/build/research_native"
if [[ -x "${PROJECT_ROOT}/dist/sage-tsdb-upstream-component-0.2.0-linux-x86_64/bin/sage_tsdb_pecj" ]]; then
    DEFAULT_COMPONENT="${PROJECT_ROOT}/dist/sage-tsdb-upstream-component-0.2.0-linux-x86_64"
fi
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"
exec "${RESEARCH_PYTHON}" -m sage_tsdb.intelligence.cli demo \
    --output "${PROJECT_ROOT}/examples/outputs/reused_models" \
    --tep-root "${PROJECT_ROOT}/../SRTFD/data/TEP" \
    --pecj-root "${PROJECT_ROOT}/../PECJ" \
    --component "${DEFAULT_COMPONENT}" "$@"
