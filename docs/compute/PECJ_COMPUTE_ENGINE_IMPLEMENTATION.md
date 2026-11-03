# PECJ 计算引擎实现与使用

PECJ 的输入由 sageTSDB 表保存；计算引擎查询窗口数据、执行 Join 并返回统计。
公共接口以 [PECJ_COMPUTE_LAYER_INTERFACE.md](PECJ_COMPUTE_LAYER_INTERFACE.md)
和 `include/sage_tsdb/compute/` 头文件为准。二进制接入见 [C API](PECJ_C_API.md)。
原 `src/compute/README.md` 的使用说明统一收录于本目录。

## 源码与职责

| 文件 | 职责 |
| --- | --- |
| `src/compute/pecj_compute_engine.cpp` | 初始化、窗口查询、算子调用、结果与指标 |
| `src/compute/window_scheduler.cpp` | 窗口触发、任务调度和生命周期 |
| `src/compute/compute_state_manager.cpp` | 计算状态保存、恢复和持久化 |
| `src/compute/srtfd_compute_engine.cpp` | 统计 / TorchScript 诊断与结果写库 |
| `CMakeLists.txt` | 计算库、C ABI、CLI、测试及安装配置 |

头文件位于 `include/sage_tsdb/compute/`，测试位于 `tests/`。
计算目标由根 `CMakeLists.txt` 定义；`src/compute/` 无单独构建入口。

## 构建与运行

```bash
# 验证集成 API，使用 stub 后端
cmake -S . -B build/compute-stub \
  -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED \
  -DPECJ_FULL_INTEGRATION=OFF -DBUILD_TESTS=ON
cmake --build build/compute-stub --parallel 2
ctest --test-dir build/compute-stub --output-on-failure

# 真实 PECJ 需要预先构建的 PECJ 与兼容 LibTorch
bash scripts/build/build.sh --pecj-dir /path/to/PECJ --test
```

C++ 完整示例位于
[examples/component_consumer/main.cpp](../../examples/component_consumer/main.cpp)。
主要调用顺序：

1. 创建 `stream_s`、`stream_r` 表，插入包含 `key` 的 `TimeSeriesData`。
2. 配置 `ComputeConfig`，调用 `engine.initialize(config, &db, resource_handle)`。
3. 调用 `executeWindowJoin(window_id, TimeRange(start_us, end_us))`。
4. 检查 `success`、`error`、`join_count`、`aqp_estimate` 和指标。

时间单位为微秒，窗口使用左闭右开 `[start_us,end_us)`。
默认 `IAWJ` 执行精确窗口 Join；使用 AQP 算子时应分别报告已确认匹配与估计结果。
不同算子的语义和配置见 [算子集成](PECJ_OPERATORS_INTEGRATION.md)。

## 配置与诊断

窗口长度、滑动步长、内存和线程预算通过 `ComputeConfig` 配置；调用方可提供
`ResourceHandle` 托管资源。神经算子还需要原始权重及缩放文件，详见
[原网络接入](RESEARCH_ENGINE.md)。

结果为空时先检查输入表、key 和窗口边界；耗时较高时分别测量查询、算子计算和写回，
再调整窗口大小、批量大小与资源预算。错误处理应检查返回状态，并保留实际后端日志。
超时、降级或使用 stub 不能作为真实 PECJ 的正确性与性能证据。

## 相关文档

- [计算模块索引](README.md)
- [总体设计](../DESIGN_DOC_SAGETSDB_PECJ.md)
- [资源管理](../core/RESOURCE_MANAGER_GUIDE.md)
- [组件构建和交付](PECJ_COMPUTE_COMPONENT_DELIVERY.md)
- [基准测试](PECJ_BENCHMARK_README.md)
