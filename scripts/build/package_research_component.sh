#!/usr/bin/env bash
# Bundle trained research engines beside the existing relocatable PECJ runtime.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BASE_COMPONENT="${PROJECT_ROOT}/dist/sage-tsdb-pecj-component-0.2.0-linux-x86_64"
MODEL="${PROJECT_ROOT}/examples/outputs/reused_models/diagnosis/srtfd.checkpoint"
PECJ_RESULTS="${PROJECT_ROOT}/examples/outputs/reused_models/pecj"
EXPORT_PYTHON="${PYTHON:-python3}"
BUILD_DIR="${PROJECT_ROOT}/build/research_package"
OUTPUT_DIR="${PROJECT_ROOT}/dist"
JOBS=2
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --component) BASE_COMPONENT="$2"; shift 2 ;;
        --model) MODEL="$2"; shift 2 ;;
        --pecj-results) PECJ_RESULTS="$2"; shift 2 ;;
        --python) EXPORT_PYTHON="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        --help)
            echo "Usage: $0 [--component ROOT] [--model CHECKPOINT] [--python PYTHON]"
            echo "          [--pecj-results DIR] [--build-dir DIR] [--output-dir DIR] [--jobs N] [--force]"
            echo "The export Python must use the same CPU LibTorch version/ABI as the base component."
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 64 ;;
    esac
done
[[ -x "${BASE_COMPONENT}/bin/sage_tsdb_pecj" ]] || { echo "Missing base component" >&2; exit 66; }
[[ -f "${MODEL}" ]] || { echo "Train SRTFD first: missing ${MODEL}" >&2; exit 66; }
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"
TORCH_CMAKE_DIR="$("${EXPORT_PYTHON}" -c 'import torch; print(torch.utils.cmake_prefix_path)')"
if [[ -f "${TORCH_CMAKE_DIR}/Torch/TorchConfig.cmake" ]]; then
    TORCH_CMAKE_DIR="${TORCH_CMAKE_DIR}/Torch"
fi
[[ -f "${TORCH_CMAKE_DIR}/TorchConfig.cmake" ]] || { echo "TorchConfig.cmake not found" >&2; exit 66; }
VERSION="$(sed -n -E 's/^project\(sageTSDB VERSION ([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' "${PROJECT_ROOT}/CMakeLists.txt")"
PACKAGE_NAME="sage-tsdb-upstream-component-${VERSION}-linux-x86_64"
STAGE="${OUTPUT_DIR}/${PACKAGE_NAME}"
if [[ -e "${STAGE}" || -e "${STAGE}.tar.gz" ]]; then
    [[ ${FORCE} -eq 1 ]] || { echo "Package exists; use --force: ${STAGE}" >&2; exit 73; }
    rm -rf -- "${STAGE}"
    rm -f -- "${STAGE}.tar.gz"
fi
mkdir -p "${BUILD_DIR}" "${OUTPUT_DIR}"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_PLUGINS=OFF \
    -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED -DPECJ_FULL_INTEGRATION=ON \
    -DPECJ_DIR="${PROJECT_ROOT}/../PECJ" \
    -DPECJ_LIBRARY="${BASE_COMPONENT}/lib/libIntelliStreamOoOJoin.so" \
    -DSAGE_TSDB_ENABLE_SRTFD=ON -DSAGE_TSDB_ENABLE_SRTFD_TORCH=ON \
    -DSAGE_TSDB_BUILD_PECJ_C_API=ON -DSAGE_TSDB_BUILD_COMPUTE_CLI=ON \
    -DTorch_DIR="${TORCH_CMAKE_DIR}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
mkdir -p "${STAGE}"
cp -a "${BASE_COMPONENT}/." "${STAGE}/"
cmake --install "${BUILD_DIR}" --prefix "${STAGE}"
mkdir -p "${STAGE}/models" "${STAGE}/share/licenses" "${STAGE}/validation"
cp "${MODEL}" "${STAGE}/models/srtfd.checkpoint"
cp "${PROJECT_ROOT}/docs/compute/RESEARCH_ENGINE.md" "${STAGE}/README.md"
cp "${PROJECT_ROOT}/docs/licenses/SRTFD-LICENSE" "${STAGE}/share/licenses/"
if [[ -f "${PECJ_RESULTS}/summary.json" ]]; then
    cp "${PECJ_RESULTS}/summary.json" "${STAGE}/validation/pecj_models.json"
fi
if [[ -f "$(dirname "${MODEL}")/summary.json" ]]; then
    cp "$(dirname "${MODEL}")/summary.json" "${STAGE}/validation/diagnosis.json"
fi
"${EXPORT_PYTHON}" -m sage_tsdb.intelligence.cli export \
    --model "${MODEL}" --output "${STAGE}/models/srtfd.ts"
"${EXPORT_PYTHON}" - "${MODEL}" "${STAGE}/validation" <<'PY'
import csv, json, sys, torch
from pathlib import Path
from sage_tsdb.intelligence import SRTFDLearner
torch.set_num_threads(2)
learner = SRTFDLearner.load(sys.argv[1])
directory = Path(sys.argv[2])
x = torch.stack((learner.model.normal_mean,
                 learner.model.normal_mean + learner.model.normal_scale,
                 learner.model.normal_mean - learner.model.normal_scale))
prediction = learner.predict(x)["class"].tolist()
with (directory / "smoke.csv").open("w", newline="") as output:
    writer = csv.writer(output)
    writer.writerow(["timestamp_us", "expected_class"] + ["f%d" % i for i in range(x.shape[1])])
    for i, row in enumerate(x.tolist()):
        writer.writerow([1000 + i * 1000, prediction[i]] + row)
(directory / "model.json").write_text(json.dumps({"input_dim": learner.config.input_dim,
    "num_classes": learner.config.num_classes, "torch_export": str(torch.__version__)}))
print(learner.config.input_dim, learner.config.num_classes)
PY
read -r MODEL_DIM MODEL_CLASSES < <("${EXPORT_PYTHON}" - "${STAGE}/validation/model.json" <<'PY'
import json,sys
m=json.load(open(sys.argv[1])); print(m["input_dim"],m["num_classes"])
PY
)
"${EXPORT_PYTHON}" -m sage_tsdb.intelligence.cli pecj-demo \
    --component "${STAGE}" --pecj-root "${STAGE}/share/upstream/PECJ" \
    --output "${STAGE}/validation/pecj"
# Executables must resolve bundled libraries without build-host search paths.
env -u LD_LIBRARY_PATH "${STAGE}/bin/sage_tsdb_pecj" --self-test > "${STAGE}/validation/pecj.log" 2>&1
env -u LD_LIBRARY_PATH OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 \
    "${STAGE}/bin/sage_tsdb_srtfd" --model "${STAGE}/models/srtfd.ts" \
    --input "${STAGE}/validation/smoke.csv" --dim "${MODEL_DIM}" --classes "${MODEL_CLASSES}" \
    > "${STAGE}/validation/native.json"
if [[ -f "$(dirname "${MODEL}")/inference_probe.csv" ]]; then
    cp "$(dirname "${MODEL}")/inference_probe.csv" "${STAGE}/validation/tep_probe.csv"
    env -u LD_LIBRARY_PATH OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 \
        "${STAGE}/bin/sage_tsdb_srtfd" --model "${STAGE}/models/srtfd.ts" \
        --input "${STAGE}/validation/tep_probe.csv" --dim "${MODEL_DIM}" --classes "${MODEL_CLASSES}" \
        --output "${STAGE}/validation/tep_native_predictions.csv" \
        > "${STAGE}/validation/native_tep.json"
fi
for binary in "${STAGE}/bin/sage_tsdb_srtfd" "${STAGE}/lib/libsage_tsdb_compute.so"; do
    if env -u LD_LIBRARY_PATH ldd "${binary}" | grep -q 'not found'; then
        echo "Missing bundled runtime dependency: ${binary}" >&2; exit 65
    fi
done
(
    cd "${STAGE}"
    find . \( -type f -o -type l \) ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
tar -C "${OUTPUT_DIR}" -czf "${STAGE}.tar.gz" "${PACKAGE_NAME}"
printf 'Upstream component: %s.tar.gz\n' "${STAGE}"
