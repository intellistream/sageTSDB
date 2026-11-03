# 计算引擎

计算层的公共头文件位于 `include/sage_tsdb/compute/`，实现位于 `src/compute/`，
由根 `CMakeLists.txt` 构建。

| 文档 | 用途 |
| --- | --- |
| [实现与使用](PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md) | 源码职责、构建、窗口调用与诊断 |
| [计算层接口](PECJ_COMPUTE_LAYER_INTERFACE.md) | C++ 接口和调用契约 |
| [C API](PECJ_C_API.md) | C ABI v1、生命周期和二进制兼容边界 |
| [算子集成](PECJ_OPERATORS_INTEGRATION.md) | IAWJ、IMA、MSWJ、AI 等算子 |
| [基准测试](PECJ_BENCHMARK_README.md) | 融合模式与插件模式对比 |
| [组件构建与交付](PECJ_COMPUTE_COMPONENT_DELIVERY.md) | 构建、安装、运行库与交付验证 |
| [组件随包说明](PECJ_COMPUTE_COMPONENT_README.md) | 安装包根目录 README 模板 |
| [原网络接入](RESEARCH_ENGINE.md) | PECJ linearVAE 与 SRTFD GCFAggMVC、训练和导出 |
| [SRTFD 算子设计](SRTFD_STATELESS_OPERATOR_DESIGN.md) | 表契约、统计与神经诊断边界 |

C/C++ 独立消费者位于 `examples/c_component_consumer/` 和
`examples/component_consumer/`。课题基线示例位于 `examples/task_demos/`，
范围及验收缺口见 [课题示例对照](../examples/TASK_DEMOS.md)。
