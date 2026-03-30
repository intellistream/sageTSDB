# ADR 0001: 边界职责与模式边界治理（Phase 1）

- Status: Accepted
- Date: 2026-03-03
- Scope: `sageTSDB` (L4-cpp)

## Context

为完成边界重构 Phase 1，需要将仓库职责、禁止依赖、模式边界以及兼容路径策略显式化，避免隐式兼容与跨层职责扩散。

## Decision

### 1) 仓库职责边界

#### In Scope

- 时序数据存储核心：`core/`（TableManager、Stream/Join 表、ResourceManager）
- 计算编排与触发：`compute/`（PECJComputeEngine、WindowScheduler）
- 插件适配层：`plugins/`（算法适配，不持有跨层业务编排）
- C++/Python 绑定与性能基准

#### Out of Scope

- 上层业务编排（应用/CLI 工作流）
- 跨仓库 orchestration 与发布流水线编排
- 通过兼容层维持历史调用路径

#### Forbidden Imports / Patterns

- 禁止引入新的 `ray` 依赖
- 禁止静默 fallback/shim/re-export 作为迁移手段
- 禁止自动从 `integrated` 回退到 `baseline`

### 2) 模式边界（Baseline vs Integrated）

- Baseline 模式：插件独立线程/资源，自主管理
- Integrated 模式：必须通过 ResourceManager 显式初始化
- `PluginManager::loadPlugin` 仅接受显式 `mode=baseline|integrated`
- 集成模式初始化失败时 fail-fast，不再自动回退

### 3) 依赖与运行时一致性

- `pyproject.toml` 仅保留运行时必需依赖（当前核心为 `numpy`）
- 运行时资源管理统一经 `core::ResourceManager`
- 资源句柄由 manager 分配，插件不自行创建隐式线程池（integrated 模式）

### Dependency Audit (Phase 1)

| 类别 | 位置 | 结论 |
| --- | --- | --- |
| Python runtime deps | `pyproject.toml` | 核心运行时依赖最小化（`numpy`） |
| C++ compile-time deps | `CMakeLists.txt`, `src/compute/CMakeLists.txt` | PECJ/PyTorch 由构建开关控制（可 stub） |
| Runtime imports | `src/plugins/`, `src/compute/` | 不引入 `ray`；模式分支显式，不再隐式回退 |

## 4) Phase 1 Checklist (Issue #18)

- [x] 产出边界清单：in-scope / out-of-scope / forbidden patterns
- [x] 盘点模式边界并去除自动兼容回退路径
- [x] 依赖审计：`pyproject.toml` 与运行时/构建时依赖语义对齐
- [x] 子任务落地：#19（C1）、#20（C2）、#21（C3）

## Implementation Notes (Phase 1)

- `src/plugins/plugin_manager.cpp`
  - 删除“自动 fallback 到 legacy init”行为
  - 增加显式模式检查与 fail-fast
- `include/sage_tsdb/plugins/plugin_interface.h`
  - 移除接口文档中的“隐式 fallback”语义
- `FaultDetectionAdapter`
  - 补齐 ResourceManager 初始化重载，支持 integrated 显式路径
- `tests/`
  - `test_window_scheduler_simple.cpp` 增强为可执行断言（构造依赖、手工触发、watermark 单调）
  - `tests/CMakeLists.txt` 为 `test_window_scheduler` 启用 `PECJ_MODE_INTEGRATED`

## Consequences

### Positive

- 边界与模式语义明确，降低隐式行为导致的调试成本
- 满足“无兼容层优先”的迁移约束
- 为 Issue #18/#19/#20/#21 提供统一验收基线

### Trade-offs

- 旧调用若依赖自动回退将失败（需要显式设置模式）
- 集成模式要求插件实现 resource-managed 初始化能力

## Follow-ups

- 在 issue #18 回填本 ADR 链接与子任务结论
- 对 `plugins/` 适配器继续补齐 integrated 覆盖率与回归测试报告
