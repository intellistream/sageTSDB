# PECJ + sageTSDB 计算组件交付说明

版本：0.2.0

更新日期：2026-09-23

适用平台：Linux x86_64

本说明描述如何生成、验收和集成 PECJ + sageTSDB 计算组件。推荐的 C ABI 见
[`PECJ_C_API.md`](PECJ_C_API.md)，兼容保留的 C++ API 见
[`PECJ_COMPUTE_LAYER_INTERFACE.md`](PECJ_COMPUTE_LAYER_INTERFACE.md)。

## 1. 交付物

执行以下命令生成交付目录和同名 `tar.gz`：

```bash
cd sageTSDB
./scripts/build/package_pecj_compute_component.sh
```

默认产物位于：

```text
dist/sage-tsdb-pecj-component-0.2.0-linux-x86_64/
├── README.md
├── bin/
│   └── sage_tsdb_pecj
├── include/sage_tsdb/
│   ├── c_api/
│   ├── compute/
│   └── core/
├── lib/
│   ├── libsage_tsdb_pecj_c.so
│   ├── libsage_tsdb_compute.so
│   ├── libsage_tsdb_core.so
│   ├── libIntelliStreamOoOJoin.so
│   └── libtorch*.so / libc10.so / libgomp-*.so
├── lib/cmake/sageTSDB/
├── share/doc/sageTSDB/compute/
│   └── BUILD_ENVIRONMENT.txt
├── share/sageTSDB/component_consumer/
├── share/sageTSDB/c_component_consumer/
└── SHA256SUMS
```

默认包会携带 PECJ 和 Torch 的非系统动态库，可直接迁移。`--thin` 生成不携带
Torch 运行库的小包，此时目标机必须提供构建时兼容的 libtorch，并配置动态库搜索路径。

脚本结束前会执行五项门禁：

1. 检查全部交付 ELF，拒绝绝对 RPATH/RUNPATH、本机构建路径和无法解析的动态库依赖。
2. 检查 `libsage_tsdb_pecj_c.so` 只导出规定的 `sage_pecj_*` C 符号。
3. 运行已安装的 `sage_tsdb_pecj --self-test`，期望 `join_count=4`。
4. 使用 C 编译器构建并运行纯 C 消费端，验证创建、插入、计算、清空复用和错误路径。
5. 使用已安装的 CMake 包编译并运行一个仓库外 C++ 消费端，期望 `join_count=1`。

## 2. 可执行文件

无参数运行等价于 `--self-test`：

```bash
./bin/sage_tsdb_pecj --self-test
./bin/sage_tsdb_pecj --version
./bin/sage_tsdb_pecj --help
```

也可计算两个 CSV 文件。CSV 首行可选，固定三列：

```csv
timestamp_us,key,value
1000000,7,10.5
1100000,8,20.0
```

```bash
./bin/sage_tsdb_pecj \
  --s-file stream_s.csv \
  --r-file stream_r.csv \
  --operator IAWJ \
  --window-start-us 1000000 \
  --window-end-us 2000000
```

程序向标准输出写出 `ComputeStatus` 的 JSON 表示；参数、输入或初始化错误返回非零退出码。

## 3. C ABI 对接（推荐）

调用方只需 C11 编译器，不需要采用组件内部使用的 C++ 标准库 ABI：

```cmake
cmake_minimum_required(VERSION 3.15)
project(enterprise_compute_consumer LANGUAGES C)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS pecj_c)

add_executable(enterprise_compute main.c)
target_link_libraries(enterprise_compute PRIVATE sageTSDB::pecj_c)
```

头文件为 `sage_tsdb/c_api/pecj.h`，完整说明见 `PECJ_C_API.md`。包内可编译样例位于
`share/sageTSDB/c_component_consumer/`。

## 4. C++ 对接（兼容保留）

调用方使用 C++20，并通过已导出的目标接入。无需自行定义 PECJ 编译宏，也无需添加
PECJ/Torch 头文件路径。

```cmake
cmake_minimum_required(VERSION 3.15)
project(enterprise_compute_consumer LANGUAGES CXX)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS compute)

add_executable(enterprise_compute main.cpp)
target_compile_features(enterprise_compute PRIVATE cxx_std_20)
target_link_libraries(enterprise_compute PRIVATE sageTSDB::compute)
```

配置消费端时指定组件根目录：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/opt/sage-tsdb-pecj-component-0.2.0-linux-x86_64
cmake --build build
```

完整可编译样例位于 `share/sageTSDB/component_consumer/`。公共目标如下：

| CMake 目标 | 用途 |
|---|---|
| `sageTSDB::compute` | PECJ 引擎、窗口调度、计算状态管理 |
| `sageTSDB::core` | `TimeSeriesDB`、存储后端接口、资源接口 |
| `sageTSDB::pecj_c` | 稳定 C ABI（推荐） |

## 5. 对接边界

调用方必须保证：

- `TimeSeriesDB`、`ResourceHandle` 和 `TableManager` 的生命周期长于引用它们的引擎/调度器。
- 输入时间戳统一为微秒；计算窗口使用左闭右开语义 `[start_us, end_us)`。
- `tags["key"]` 是可转换为 `uint64_t` 的字符串，`fields["value"]` 是可转换为
  `double` 的字符串。
- 使用调度器时，每次成功写入 S/R 流后调用 `onDataInserted()`。
- `submitTask()` 必须异步接收任务，并准确返回是否入队成功。
- 停机顺序为 `scheduler.stop(true)`，随后再析构引擎、数据库和资源句柄。

同一个 `PECJComputeEngine` 的窗口计算会在组件内部串行化，以保护 PECJ 的单线程算子状态。
可以并发调用接口，但单实例不会并行执行多个窗口；需要真正的窗口并行时应创建多个引擎实例。

## 6. ABI 与运行环境

- 推荐接口：C ABI v1；调用方不依赖内部 C++ 对象布局或 libstdc++ ABI。
- C++ 兼容接口：C++20、`_GLIBCXX_USE_CXX11_ABI=0`，调用方由导出的 CMake 目标自动继承。
- 动态库方案：组件库使用相对 RPATH；默认完整包运行时不要求设置 `LD_LIBRARY_PATH`。
- STL 类型出现在 C++ API 中，调用方必须使用兼容的编译器、标准库和 ABI 设置。
- 本交付不包含达梦驱动；企业数据库接入通过 `StorageBackend` 实现和注册完成。

跨编译器或跨语言场景使用 `sageTSDB::pecj_c`；当前 C++ API 只用于编译器和标准库环境受控的
既有调用方。

### 6.1 本交付包构建环境

| 项目 | 构建环境 |
|---|---|
| 操作系统 | Ubuntu 22.04.5 LTS（WSL2） |
| 架构 | Linux x86_64，小端序 |
| glibc | 2.35（Ubuntu `2.35-0ubuntu3.14`） |
| libstdc++ | Ubuntu `libstdc++6 12.3.0` |
| C++ 编译器 | GCC/G++ 11.4.0 |
| 内核 | `6.18.33.2-microsoft-standard-WSL2` |
| CPU | AMD Ryzen 7 5800H，构建环境可见 8 个逻辑 CPU |

以上是构建和验收环境，不表示目标机必须使用相同 CPU 或相同内核版本。

### 6.2 目标机要求

| 项目 | 要求 |
|---|---|
| 操作系统 | 64 位 Linux；推荐使用已更新的 Ubuntu 22.04 x86_64 |
| glibc | `GLIBC_2.34` 或更高 |
| libstdc++ | 必须提供 `GLIBCXX_3.4.30` 或更高 |
| CPU | x86_64；主程序 ELF 声明为 `x86-64-baseline`，构建时未使用 `-march=native` |
| 内核 | ELF ABI 标签为 Linux 3.2.0；生产部署建议 Linux 5.4 或更高 |
| 动态加载器 | `/lib64/ld-linux-x86-64.so.2` |

`GLIBC_2.34` 和 `GLIBCXX_3.4.30` 是扫描包内全部 ELF 文件得到的最高符号版本要求。
内核 3.2.0 是 ELF ABI 标签，不代表旧内核已经通过运行验收；当前实际验收环境为上表所列
WSL2 内核。若目标环境不是 Ubuntu 22.04，应先检查：

```bash
uname -m
uname -r
getconf GNU_LIBC_VERSION
strings "$(c++ -print-file-name=libstdc++.so.6)" \
  | grep '^GLIBCXX_' | sort -V | tail -n 1
```

每次打包还会在 `share/doc/sageTSDB/compute/BUILD_ENVIRONMENT.txt` 中生成本次构建环境和
ELF 要求快照。建议将该文件与 `SHA256SUMS` 一并归档。

## 7. 验收命令

```bash
cd /opt/sage-tsdb-pecj-component-0.2.0-linux-x86_64
sha256sum -c SHA256SUMS
./bin/sage_tsdb_pecj --self-test

cmake -S share/sageTSDB/c_component_consumer -B /tmp/pecj-c-consumer \
  -DCMAKE_PREFIX_PATH="$PWD"
cmake --build /tmp/pecj-c-consumer
/tmp/pecj-c-consumer/pecj_c_consumer

cmake -S share/sageTSDB/component_consumer -B /tmp/pecj-consumer \
  -DCMAKE_PREFIX_PATH="$PWD"
cmake --build /tmp/pecj-consumer
/tmp/pecj-consumer/pecj_consumer
```

验收通过标准：校验和全部成功，三个程序退出码均为 0；CLI 自检输出 `join_count=4`，
C 示例输出 `abi=1 join_count=4`、`reuse_join_count=1 error_path=ok`，C++ 示例输出
`join_count=1`。

## 8. 当前限制

- `ComputeStatus` 只返回聚合结果，不返回匹配明细；`result_table` 自动写回尚未实现。
- `AI` 和 `LinearSVI` 依赖 PECJ 的 `torchscripts` 模型资产及其路径配置；默认交付包不含
  这些模型文件，也不把这两个算子列入开箱验收范围。
- `ComputeStateManager::deleteState()` 和 `deleteCheckpoint()` 目前没有真正写入 tombstone。
- `persistState(compute_name)` 当前触发数据库级 checkpoint，参数尚未用于局部持久化。
- `WindowScheduler` 仍要求非空 `TableManager*`，但当前计算路径不读取该对象。
