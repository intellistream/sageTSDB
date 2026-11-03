#!/usr/bin/env bash
# Package runnable Python demos without rebuilding or changing the PECJ package.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="$(sed -n -E 's/^project\(sageTSDB VERSION ([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' "${PROJECT_ROOT}/CMakeLists.txt")"
OUTPUT_DIR="${1:-${PROJECT_ROOT}/dist}"
PACKAGE_NAME="sage-tsdb-task-demos-${VERSION}"
STAGING_DIR="$(mktemp -d)"
trap 'rm -rf -- "${STAGING_DIR}"' EXIT

mkdir -p "${OUTPUT_DIR}" "${STAGING_DIR}/${PACKAGE_NAME}/examples/task_demos" \
    "${STAGING_DIR}/${PACKAGE_NAME}/scripts/demo" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/examples" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/compute" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/reports"
cp "${PROJECT_ROOT}/examples/task_demos/"*.py \
    "${PROJECT_ROOT}/examples/task_demos/README.md" \
    "${STAGING_DIR}/${PACKAGE_NAME}/examples/task_demos/"
cp "${PROJECT_ROOT}/scripts/demo/run_task_demos.sh" \
    "${STAGING_DIR}/${PACKAGE_NAME}/scripts/demo/"
cp "${PROJECT_ROOT}/docs/TASK.md" "${STAGING_DIR}/${PACKAGE_NAME}/docs/"
cp "${PROJECT_ROOT}/docs/examples/TASK_DEMOS.md" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/examples/"
cp "${PROJECT_ROOT}/docs/compute/RESEARCH_ENGINE.md" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/compute/"
cp "${PROJECT_ROOT}/docs/reports/TASK_PACKAGE_AUDIT_2026-11-03.md" \
    "${STAGING_DIR}/${PACKAGE_NAME}/docs/reports/"
cat > "${STAGING_DIR}/${PACKAGE_NAME}/README.md" <<'README'
# 课题 Demo 补充包

需要 Python 3.8+。在本目录执行：

```bash
bash scripts/demo/run_task_demos.sh \
  --component /path/to/sage-tsdb-pecj-component-0.2.0-linux-x86_64
```

结果位于 examples/outputs/task_demos/。Join 和预警调用既有 PECJ 组件，无需重新编译。
仅运行独立的漂移/持续学习示例时可用 --demo drift 或 --demo continual，无需组件。
详细说明见 examples/task_demos/README.md，课题对照见 docs/examples/TASK_DEMOS.md。
新增算法是 Python 演示基线，不是 SRTFD 神经网络、PDA/VI 或 CXL 实现。
README

tar -C "${STAGING_DIR}" -czf "${OUTPUT_DIR}/${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
(cd "${OUTPUT_DIR}" && sha256sum "${PACKAGE_NAME}.tar.gz" > "${PACKAGE_NAME}.tar.gz.sha256")
printf 'Demo supplement: %s/%s.tar.gz\n' "${OUTPUT_DIR}" "${PACKAGE_NAME}"
