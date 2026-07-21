# 🚀 sageTSDB 快速上手（C++ / PECJ / SRTFD）

本指南带你从零跑通 sageTSDB 的 C++ 构建，拉取并编译 PECJ，运行流式 Join benchmark。
SRTFD 诊断引擎已内置在 sageTSDB 中（无需外部依赖即可运行），SRTFD 原始仓库仅在需要
参考/训练时拉取。

> 只需要纯 C++ 时序库、不涉及 PECJ？跳到 [附录 A：最小构建](#附录-a最小构建仅-c-core)。

---

## 0. 环境要求

- Ubuntu 22.04+（GLIBC 2.35+）或等价 Linux
- GCC 11+（C++20）、CMake 3.15+、make、git
- Python 3.10+ 和 pip（PECJ 依赖 PyTorch）

```bash
sudo apt update
sudo apt install -y gcc g++ cmake make git python3 python3-pip
```

---

## 1. 拉取三个仓库

三者建议放在同一父目录下（下文假设父目录为 `$WORK`）：

```bash
export WORK=$HOME/dameng        # 任选一个工作目录
mkdir -p "$WORK" && cd "$WORK"

git clone https://github.com/intellistream/sageTSDB.git
git clone https://github.com/intellistream/PECJ.git
git clone https://github.com/intellistream/SRTFD.git
```

目录结构：

```
$WORK/
├── sageTSDB/   # 本仓库：时序库 + PECJ/SRTFD 计算引擎 + benchmark
├── PECJ/       # 乱序流 Join 算子库（C++，依赖 PyTorch）
└── SRTFD/      # 实时故障诊断（Python 原始算法仓库，可选）
```

---

## 2. 安装 PyTorch（PECJ 依赖）

PECJ 链接 libtorch。安装 CPU 版即可：

```bash
pip3 install torch==1.13.0 torchvision torchaudio \
    --index-url https://download.pytorch.org/whl/cpu
# 验证
python3 -c "import torch; print('torch', torch.__version__)"
```

---

## 3. 编译 PECJ

PECJ 用 CMake 构建，通过 PyTorch 的 cmake prefix 找到 libtorch：

```bash
cd "$WORK/PECJ"
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH=$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)') ..
make -j"$(nproc)"
```

构建产物 `libIntelliStreamOoOJoin.so` 会出现在 `$WORK/PECJ/build/` 下。

---

## 4. 配置并编译 sageTSDB（含 PECJ 深度集成）

```bash
cd "$WORK/sageTSDB"
cmake -B build -S . \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_FULL_INTEGRATION=ON \
    -DPECJ_DIR="$WORK/PECJ" \
    -DSAGE_TSDB_ENABLE_SRTFD=ON \
    -DBUILD_TESTS=ON

cmake --build build -j"$(nproc)"
```

构建选项说明：

| 选项 | 作用 |
| --- | --- |
| `SAGE_TSDB_ENABLE_PECJ=ON` | 启用 PECJ 计算引擎 |
| `PECJ_MODE=INTEGRATED` | 深度集成模式（数据先入库，计算从表读取） |
| `PECJ_FULL_INTEGRATION=ON` | 链接真实 PECJ 库 + Torch |
| `PECJ_DIR` | 指向已编译的 PECJ 源码/构建目录 |
| `SAGE_TSDB_ENABLE_SRTFD=ON` | 启用内置 SRTFD 诊断引擎（默认开） |

> ABI 提示：sageTSDB 与 PECJ/Torch 统一使用 `_GLIBCXX_USE_CXX11_ABI=0`，根 CMake 已处理，无需手动设置。

---

## 5. 设置运行时库路径

benchmark/demo 需要在运行时找到 PECJ 与 Torch 的动态库：

```bash
export LD_LIBRARY_PATH="$WORK/PECJ/build":\
"$(python3 -c 'import torch, os; print(os.path.dirname(torch.__file__))')/lib":\
$LD_LIBRARY_PATH
```

---

## 6. 运行 Benchmark

benchmark 位于 `build/examples/`，使用仓库自带数据集 `examples/datasets/{sTuple,rTuple}.csv`。

### 6.1 性能基准（performance_benchmark）

扫描多种算子/规模/线程组合，输出吞吐与延迟：

```bash
cd "$WORK/sageTSDB/build/examples"
./performance_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --output /tmp/benchmark_results.csv
```

### 6.2 集成模式 vs 插件模式对比（pecj_integrated_vs_plugin_benchmark）

对比"深度集成"与"插件"两种架构的性能：

```bash
./pecj_integrated_vs_plugin_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --events 5000 --threads 4 --repeat 3
```

常用参数：`--events N`（事件数）、`--threads N`、`--operator {IMA,IAWJ,SHJ,MSWJ,...}`、
`--window-us`、`--slide-us`、`--repeat N`、`--help`。

> ⚠️ 这两个 benchmark 都必须提供 `--s-file`/`--r-file`；缺数据时会明确报错并退出（不再崩溃）。

---

## 7. SRTFD 说明

- **sageTSDB 内置 C++ SRTFD 诊断引擎**（`SRTFDComputeEngine`，默认 `statistical` 后端），
  随 `SAGE_TSDB_ENABLE_SRTFD=ON` 编译，无需外部依赖即可运行。验证：

  ```bash
  ./build/tests/test_srtfd_compute_engine
  ```

- **SRTFD 原始仓库**（`$WORK/SRTFD`）是 Python 项目，用于故障诊断模型的训练与复现，
  与 C++ 侧的推理/诊断语义解耦。如需运行原始算法：

  ```bash
  cd "$WORK/SRTFD"
  pip3 install -r requirements.txt
  python3 general_main.py --data TEP --num_tasks 22 --cl_type nc --agent SRTFD --num_runs 1 --N 1000
  ```

---

## 8. 运行测试（可选）

```bash
cd "$WORK/sageTSDB"
ctest --test-dir build --output-on-failure
```

---

## 附录 A：最小构建（仅 C++ core）

不需要 PECJ/Torch 时，可只构建时序库核心与内置算法：

```bash
cd "$WORK/sageTSDB"
cmake -B build -S . -DSAGE_TSDB_ENABLE_PECJ=OFF -DBUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

此模式不依赖 PECJ、Torch，SRTFD（statistical 后端）与全部 core/算法测试均可运行。

---

## 附录 B：常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `error while loading shared libraries: libIntelliStreamOoOJoin.so` | 未设置 `LD_LIBRARY_PATH`，见第 5 步 |
| `libtorch` / `libc10` 找不到 | `LD_LIBRARY_PATH` 未包含 torch 的 `lib` 目录 |
| PECJ cmake 找不到 Torch | 确认 `pip3 install torch==1.13.0` 成功，且用了 `CMAKE_PREFIX_PATH=$(python3 -c '...')` |
| benchmark 报 `No input data` | 未传 `--s-file`/`--r-file`，见第 6 步 |
| 想接入企业级数据库（达梦 DM） | 见 [docs/STORAGE_BACKEND_CONTRACT.md](docs/STORAGE_BACKEND_CONTRACT.md) 与 [docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md](docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md) |

## 📚 更多文档

- 架构设计：[docs/DESIGN_DOC_SAGETSDB_PECJ.md](docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- 存储后端契约（达梦对接）：[docs/STORAGE_BACKEND_CONTRACT.md](docs/STORAGE_BACKEND_CONTRACT.md)
- PECJ 深度集成 demo：[docs/examples/README_DEEP_INTEGRATION_DEMO.md](docs/examples/README_DEEP_INTEGRATION_DEMO.md)
