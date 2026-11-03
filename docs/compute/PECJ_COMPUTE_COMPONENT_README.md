# PECJ + sageTSDB 计算组件

版本：0.2.0

平台：Linux x86_64

接口：C ABI v1（推荐），C++20（兼容保留）

本目录是可独立部署的 PECJ + sageTSDB 计算组件，包含命令行程序、C/C++ 动态库、公共
头文件、CMake 包配置、接入示例和接口文档。跨编译器、跨语言或长期二进制兼容场景应使用
C ABI；调用方不需要 PECJ 原生头文件。

## 1. 运行环境

- 64 位 Linux x86_64，推荐 Ubuntu 22.04。
- glibc 必须提供 `GLIBC_2.34` 或更高版本。
- libstdc++ 必须提供 `GLIBCXX_3.4.30` 或更高版本。
- CPU 指令集为 `x86-64-baseline`。
- 生产环境建议使用 Linux 5.4 或更高版本内核。
- 动态加载器路径为 `/lib64/ld-linux-x86-64.so.2`。

本次构建环境及 ELF 要求的完整快照见：
`share/doc/sageTSDB/compute/BUILD_ENVIRONMENT.txt`。

## 2. 首次验收

在当前目录执行：

```bash
sha256sum -c SHA256SUMS
./bin/sage_tsdb_pecj --self-test
```

校验和应全部显示 `OK`。自检进程退出码应为 0，JSON 结果应包含：

```json
{
  "success": true,
  "input_s_count": 3,
  "input_r_count": 3,
  "join_count": 4
}
```

完整包使用相对 RPATH，正常情况下不需要设置 `LD_LIBRARY_PATH`。

## 3. 命令行计算

输入 S/R 两个 CSV 文件，每行固定为 `timestamp_us,key,value`，首行表头可省略：

```csv
timestamp_us,key,value
1000000,7,10.5
1100000,8,20.0
```

执行窗口连接：

```bash
./bin/sage_tsdb_pecj \
  --s-file stream_s.csv \
  --r-file stream_r.csv \
  --operator IAWJ \
  --window-start-us 1000000 \
  --window-end-us 2000000
```

时间戳单位是微秒，窗口范围为左闭右开 `[start_us, end_us)`。程序向标准输出返回
`ComputeStatus` JSON，失败时返回非零退出码。使用 `./bin/sage_tsdb_pecj --help`
查看全部参数。

## 4. C ABI 集成（推荐）

纯 C 调用方的 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.15)
project(pecj_c_consumer LANGUAGES C)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS pecj_c)

add_executable(pecj_c_consumer main.c)
target_link_libraries(pecj_c_consumer PRIVATE sageTSDB::pecj_c)
```

公共入口头文件：

```c
#include <sage_tsdb/c_api/pecj.h>
```

包内纯 C 示例的验收方法：

```bash
cmake -S share/sageTSDB/c_component_consumer -B /tmp/pecj-c-consumer \
  -DCMAKE_PREFIX_PATH="$PWD"
cmake --build /tmp/pecj-c-consumer
/tmp/pecj-c-consumer/pecj_c_consumer
```

预期输出包含 `version=0.2.0 abi=1 join_count=4`、`reuse_join_count=1` 和
`error_path=ok`。完整定义见 `share/doc/sageTSDB/compute/PECJ_C_API.md`。

## 5. C++ 集成（兼容保留）

调用方的 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.15)
project(pecj_consumer LANGUAGES CXX)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS compute)

add_executable(pecj_consumer main.cpp)
target_compile_features(pecj_consumer PRIVATE cxx_std_20)
target_link_libraries(pecj_consumer PRIVATE sageTSDB::compute)
```

配置时将当前组件目录作为 CMake 前缀：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/sage-tsdb-pecj-component-0.2.0-linux-x86_64
cmake --build build
```

公共入口头文件：

```cpp
#include <sage_tsdb/compute/pecj_compute_engine.h>
#include <sage_tsdb/core/time_series_db.h>
```

不要直接调用 PECJ 的 `OoOJoin::*` 原生 API；本包没有交付其原生头文件。PECJ 算法实现
位于 `lib/libIntelliStreamOoOJoin.so`，由 `libsage_tsdb_compute.so` 封装调用。

包内可编译示例的验收方法：

```bash
cmake -S share/sageTSDB/component_consumer -B /tmp/pecj-consumer \
  -DCMAKE_PREFIX_PATH="$PWD"
cmake --build /tmp/pecj-consumer
/tmp/pecj-consumer/pecj_consumer
```

预期输出为 `join_count=1`。

## 6. 目录说明

| 路径 | 内容 |
|---|---|
| `bin/sage_tsdb_pecj` | 命令行程序和自检入口 |
| `include/sage_tsdb/c_api/pecj.h` | 稳定 C ABI 公共头文件 |
| `include/sage_tsdb/` | sageTSDB C/C++ 公共头文件 |
| `lib/libsage_tsdb_pecj_c.so` | C ABI 动态库 |
| `lib/` | C API、sageTSDB、PECJ、Torch 动态库 |
| `lib/cmake/sageTSDB/` | `find_package(sageTSDB)` 配置 |
| `share/sageTSDB/c_component_consumer/` | 纯 C 接入示例 |
| `share/sageTSDB/component_consumer/` | 独立 CMake 接入示例 |
| `share/doc/sageTSDB/compute/` | 接口、交付和环境文档 |
| `share/licenses/` | PECJ 和 Torch 许可证 |
| `SHA256SUMS` | 包内文件完整性校验清单 |

## 7. 接口约定与限制

- C ABI 使用 `sage_pecj_record_v1 {timestamp_us, key, value}`，不暴露 STL 或 C++ 对象。
- C++ API 的输入连接键放在 `TimeSeriesData.tags["key"]`，值放在
  `TimeSeriesData.fields["value"]`。
- `TimeSeriesDB` 的生命周期必须长于 `PECJComputeEngine`。
- 同一个 `PECJComputeEngine` 实例内部串行执行窗口；窗口并行需使用多个引擎实例。
- `ComputeStatus` 当前返回聚合结果，不返回匹配明细，也不会自动写回 `result_table`。
- `AI` 和 `LinearSVI` 需要额外的 `torchscripts` 模型文件，默认包不含这些资产。
- 默认包不包含达梦数据库驱动。
- 组件构建时关闭了 SRTFD；异常诊断头文件不表示已提供相应后端或持续学习实现。

完整接口定义：`share/doc/sageTSDB/compute/PECJ_COMPUTE_LAYER_INTERFACE.md`

完整交付说明：`share/doc/sageTSDB/compute/PECJ_COMPUTE_COMPONENT_DELIVERY.md`

## 8. 课题 Demo（新增源码打包内容）

新增源码重新打包后包含 `share/sageTSDB/task_demos/`，以 Python 标准库实现工况漂移检测、
增量诊断和提前预警演示；其中 Join 与预警会调用本组件的真实 CLI。既有 0.2.0 压缩包
可配合独立的 Demo 补充包使用。新增 Python 算法属于演示基线，不是 SRTFD 神经网络后端。

```bash
python3 share/sageTSDB/task_demos/run_demos.py \
  --component "$PWD" --output /tmp/task-demo-results
```

运行说明：`share/sageTSDB/task_demos/README.md`。

课题覆盖与未实现项：`share/doc/sageTSDB/examples/TASK_DEMOS.md`。

直接复用 PECJ / SRTFD 原始网络的当前版本单独交付为
`sage-tsdb-upstream-component-0.2.0-linux-x86_64.tar.gz`，包含原网络、原 PECJ 资产与原 SRTFD 的新训练权重。
旧 research 组件属于已停用的自建网络实验。
该实现及实测局限见源码 `docs/compute/RESEARCH_ENGINE.md`。
