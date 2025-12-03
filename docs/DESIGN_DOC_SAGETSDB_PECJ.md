# sageTSDB 管理型 PECJ 集成设计文档

版本: v2.0 (Resource-Managed Architecture)
更新日期: 2024-12-03
分支: `pecj_resource_integration`

摘要
---
本文档描述将 PECJ 作为受控算子集成到 sageTSDB 的方案。核心原则是由 sageTSDB 统一管理所有运行资源（线程、内存、GPU、模型生命周期），并把传统的解耦（插件独立运行）方案保留为实验对比的 baseline。文档聚焦资源代管设计、最小接口契约、构建/运行开关与迁移步骤，删除冗余实现细节和示例代码。

设计目标
---
- 以 sageTSDB 的 ResourceManager 为中心，统一管理线程池、内存配额、设备（GPU）与模型加载。
- 在构建时通过 CMake 参数决定集成深度：Stub（默认）、Integrated（资源代管/生产）、Baseline（原有解耦，仅用于对比）。
- 保持外部接口简单：PluginManager 只需申请资源、发送数据、接收结果；PECJ 的内部实现通过适配器封装。
- 强制可观测性：资源使用、队列长度、延迟、错误率必须上报。

模式与优先级
---
1. Integrated（推荐，生产）
     - sageTSDB 分配并强制执行资源配额（线程、内存、GPU），PECJ 在这些约束下运行。
     - 目的：防止线程/内存爆炸，集中监控与运维控制。
2. Stub（默认，开发/CI）
     - 仅编译适配器接口与轻量 stub 行为，不依赖 PECJ 库，便于快速编译与单元测试。
## 3. 集成方案及实施摘要

为保持简洁，本节以要点方式说明 Integrated（资源代管）模式的实现约定，并把原有解耦模式作为 baseline 仅用于实验对比。

核心约定：
- 不修改 PECJ 外部 API；通过 `PECJAdapter` 做适配和受控运行。
- 在 Integrated 模式下，ResourceManager 为每个插件分配 ResourceHandle，包含线程配额、内存上限和可见 GPU 列表。
- 插件通过 ResourceHandle 在 sageTSDB 提供的受控执行上下文（线程池或任务队列）提交工作，不得自行无限制创建线程。

最小接口契约
---
PECJAdapter 必须实现：
- bool init(const PluginConfig &cfg, const ResourceRequest &req);
- void start();
- void stop();
- void onData(std::shared_ptr<TimeSeriesData> data);
- PluginStatus status() const; // 返回资源使用、队列长度、uptime 与最后错误

ResourceRequest 字段（示例）：
```
struct ResourceRequest {
    int requested_threads = 0;
    uint64_t max_memory_bytes = 0;
    std::vector<int> gpu_ids; // optional
    std::string model_path; // optional
};
```

CMake / 构建开关（建议）
---
- `-DPECJ_DIR=/path/to/PECJ`：PECJ 源或构建路径
- `-DPECJ_FULL_INTEGRATION=ON|OFF`：是否在构建时链接 PECJ（默认 OFF）
- `-DPECJ_MODE=Integrated|Stub|Baseline`：显式选择运行模式（优先级与上文一致）
- `-DPECJ_MANAGED_THREADS=<n>`：当 >0 时建议 ResourceManager 将该线程数作为默认配额

资源代管关键点
---
- 线程：使用 ResourceManager 提供的线程池或任务队列；插件应以任务/回调方式提交计算。
- 内存：实现软/硬阈值监控；超阈值时触发降级策略。
- GPU/模型：ResourceManager 控制 CUDA 可见设备并缓存模型实例，避免重复加载与 OOM。
- 监控：每个插件按周期上报 metrics（threads_used, memory_used_bytes, queue_length, tuples_processed, avg_latency_ms）。

降级与运维
---
- 监控到资源异常时优先收紧配额，必要时切换到 Stub 模式并报警。
- 提供运维 API 支持热启/停插件，以及查询/调整 ResourceRequest。

实施路线（最小可行步）
---
1. 定义 `resource_manager.h`（ResourceRequest / ResourceHandle / ResourceManager 接口）并提交草案。
2. 调整 `pecj_adapter.h` 的 init 签名以接收 ResourceRequest；实现 Stub 版本并提交。
3. 在 PluginManager 中集成 ResourceManager：加载插件时发起资源申请，并将 ResourceHandle 注入插件。
4. 实现最小 ResourceManager（线程池 + 内存监控），运行 CI 验证（Stub 模式下）。
5. 在含 PECJ 的环境中启用 Integrated 模式，验证资源限额与功能兼容性。

验证要点（验收准则）
---
- 默认构建（不启用 PECJ）通过所有单元测试。
- Integrated 模式下全局线程数与内存使用不超过配置阈值。
- EventBus 语义与现有插件兼容，无需下游改造。
- Baseline 与 Integrated 给出对比报告（吞吐 vs 延迟 vs 资源使用）。

后续工作与注意事项
---
- 补充 API 文档（函数签名、错误码、metrics schema）。
- 在实现阶段优先以小步快频提交（Stub -> ResourceManager -> Integrated）。
- CI 环境建议保留一个带 PECJ 的测试 runner 用于集成验证。

---
**文档状态**: 本文件为精简版设计稿，冗余细节与历史实现保留在 `DESIGN_DOC_SAGETSDB_PECJ.md.backup`。
    *   将数据和流类型标记 `(data, is_s_stream)` 推入异步队列 `data_queue_`。
    *   通过条件变量 `queue_cv_` 唤醒工作线程。
