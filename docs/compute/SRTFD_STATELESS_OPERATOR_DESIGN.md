# SRTFD Stateless Operator Integration Design

版本: v0.1
日期: 2026-05-30
范围: sageTSDB compute layer, SRTFD inference path

## 背景

SRTFD 原仓库实现的是在线持续学习流程。`agents/srtfd.py` 中的 `SRTFD` agent 持有训练 buffer、伪标签集合、任务进度和 optimizer 状态；这些状态适合离线或在线训练编排，但不适合作为 sageTSDB integrated 模式里的计算算子直接持有。

sageTSDB 的 PECJ 深度融合模式已经定义了可复用原则：数据先进入 `TimeSeriesDB` 表，计算引擎按窗口查询表数据，算法执行过程不拥有输入缓冲或线程生命周期，结果再写回结果表。本设计将 SRTFD 接入为同类无状态诊断算子。

## 目标

- 将 SRTFD 的推理/诊断能力作为 `compute/` 层无状态算子接入 sageTSDB。
- 保持 `TimeSeriesDB` 为唯一数据入口和结果查询入口。
- 不在算子内持有训练 buffer、optimizer、伪标签集合或后台线程。
- 提供 stub-friendly 的 C++ 实现，便于在没有 PyTorch/SRTFD 模型文件时编译和测试。
- 为后续接入 TorchScript 或 ONNX 推理后端预留边界。

## 非目标

- 不把 SRTFD 的训练循环迁移进 sageTSDB。
- 不引入新的 Python virtualenv 或运行时环境管理。
- 不在 integrated 模式中自动 fallback 到插件模式。
- 不改造 SRTFD 原仓库的数据集加载、continual learning 训练协议。

## 架构映射

| PECJ deep integration | SRTFD stateless integration |
| --- | --- |
| `stream_s` / `stream_r` 输入表 | `sensor_events` 输入表 |
| `PECJComputeEngine::executeWindowJoin()` | `SRTFDComputeEngine::executeWindowDiagnosis()` |
| 外部 PECJ operator | SRTFD 推理后端或内置统计 baseline |
| `join_results` 结果表 | `srtfd_results` 结果表 |
| `ComputeStatus` / `ComputeMetrics` | `SRTFDStatus` / `SRTFDMetrics` |

## 数据契约

### 输入表: `sensor_events`

- `timestamp`: 事件时间。
- `value`: 传感器特征向量。TEP 使用 52 维，HRS 使用 120 维，CARLS 使用 10 维。
- `tags["asset_id"]`: 可选，设备或生产线标识。
- `tags["dataset"]`: 可选，`TEP` / `HRS` / `CARLS_S` / `CARLS_M`。
- `fields`: 可携带原始标签、批次号或来源文件路径等元数据。

### 结果表: `srtfd_results`

每条输入样本产生一条诊断结果：

- `timestamp`: 对应输入样本时间。
- `value`: fault class id 或异常分数。
- `tags["operator"] = "srtfd"`。
- `tags["window_id"]`: 诊断窗口 ID。
- `tags["asset_id"]`: 从输入继承。
- `fields["fault_class"]`: 预测类别。
- `fields["confidence"]`: 置信度。
- `fields["anomaly_score"]`: 异常评分。
- `fields["backend"]`: `statistical` / `torchscript` / `external`。

## 执行计划

1. 文档与边界确认
   - 阅读 PECJ deep integration 文档和实现。
   - 阅读 SRTFD agent/model 代码，区分训练状态和推理路径。
   - 输出本设计文档作为执行锚点。

2. C++ 无状态算子骨架
   - 新增 `include/sage_tsdb/compute/srtfd_compute_engine.h`。
   - 新增 `src/compute/srtfd_compute_engine.cpp`。
   - 接口采用 `initialize(config, db, resource_handle)` 和 `executeWindowDiagnosis(window_id, range)`。

3. 数据读取与结果写回
   - 从 `input_table` 查询窗口内 `TimeSeriesData`。
   - 校验特征维度并转换为 `std::vector<double>`。
   - 对每条样本生成 `SRTFDDiagnosis`。
   - 将结果写入 `result_table`，供 `TimeSeriesDB::query()` 查询。

4. 推理后端策略
   - 初版实现 `statistical` backend：不依赖 PyTorch，用归一化能量分数和阈值生成稳定可测试的诊断结果。
   - 保留 `torchscript` backend 配置字段：`model_path`、`input_dim`、`num_classes`，后续用 libtorch 或独立推理进程替换 `statistical` backend。

5. 构建与测试
   - 将 SRTFD compute engine 编入 `sage_tsdb_compute`。
   - 新增 `tests/test_srtfd_compute_engine.cpp`。
   - 覆盖初始化失败、空窗口、维度校验、正常/异常诊断、结果表写回和 metrics。

6. 文档收口
   - 更新 `docs/compute/README.md` 索引。
   - 在本文件记录当前实现状态和后续 TorchScript 接入点。

## 当前实现状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 设计文档 | 进行中 | 本文件 |
| `SRTFDComputeEngine` 接口 | 已实现 | `include/sage_tsdb/compute/srtfd_compute_engine.h` |
| 结果写回 | 已实现 | 写入 `srtfd_results` |
| 统计 baseline backend | 已实现 | 用于无模型环境测试 |
| TorchScript backend | 未开始 | 后续扩展 |
| 单元测试 | 已实现 | `tests/test_srtfd_compute_engine.cpp` |

## 验收准则

- 能在 integrated build 中编译 `SRTFDComputeEngine`。
- 无模型文件时也能通过 `statistical` backend 完成窗口诊断。
- 输入数据只从 `TimeSeriesDB` 表读取，不通过 `feedData()` 旁路进入算子。
- 诊断结果可通过 `db.query("srtfd_results", range)` 查询。
- 算子不创建后台线程，不保存训练样本 buffer，不修改 SRTFD 原训练代码。

## 后续扩展

- TorchScript backend: 将 SRTFD 的 `GCFAggMVC` 或等价模型导出为 TorchScript，C++ 侧只加载 immutable model artifact。
- 模型仓库化: 将 `model_path`、label map 和标准化参数作为外部 artifact 管理。
- 调度统一化: 后续实现通用 `IComputeEngine` 后，让 PECJ 与 SRTFD 共享 scheduler 抽象。