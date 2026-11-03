# sageTSDB 文档

文档按模块维护，示例运行方法放在 `examples/`，构建命令放在 `scripts/`。

| 入口 | 内容 |
| --- | --- |
| [快速开始](../QUICKSTART.md) | Python、C++、PECJ 与 SRTFD 的构建和运行 |
| [总体设计](DESIGN_DOC_SAGETSDB_PECJ.md) | 存储、计算、资源管理与 Python 层 |
| [架构边界](adr/0001-boundary-and-mode-policy.md) | 模块职责、模式与兼容策略 |
| [存储模块](core/README.md) | LSM Tree、持久化、表与资源管理 |
| [计算模块](compute/README.md) | PECJ / SRTFD、C API 与组件交付 |
| [算法模块](algorithms/README.md) | 流 Join 与窗口聚合 |
| [插件系统](plugins/README.md) | 插件模式与开发接口 |
| [工具模块](utils/README.md) | 配置、日志与数据加载 |
| [示例索引](examples/README.md) | 深度集成、乱序和课题示例 |
| [存储后端契约](STORAGE_BACKEND_CONTRACT.md) | 达梦接入边界与一致性约束 |
| [企业数据库集成计划](EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md) | 分工、联调与后续工作 |
| [wheel 构建与发布](WHEEL_BUILD_AND_PYPI_PUBLISH.md) | Python 包构建和发布流程 |

课题要求见 [TASK.md](TASK.md)，可运行能力与待完成项见
[课题示例对照](examples/TASK_DEMOS.md)。
[2026-11-03 交付包核查](reports/TASK_PACKAGE_AUDIT_2026-11-03.md)
是指定压缩包的历史核查记录，报告中的哈希、数值和包路径不会随源码更新。

目录约定：

- `docs/` 保存维护中的设计、接口和操作说明；日期明确的核查证据放在 `reports/`。
- `include/` 与 `src/` 保存接口和实现，不再保留过时的占位说明或备用构建入口。
- `build/`、`dist/`、`examples/outputs/`、`results/` 是本地产物目录，不纳入 Git。
- 修改演示应编辑 `examples/task_demos/`；`dist/` 内的同名文件是打包副本，更新需重新打包。
