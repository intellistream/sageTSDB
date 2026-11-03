# TASK.md 与交付包核查（2026-11-03）

结论：**最新 upstream 包已补入可运行异常检测、原神经网络权重和相关 Demo，但没有完成 TASK.md 的全部功能及考核要求。** 可以作为功能演示版交付，不能作为课题全部完成或最终验收通过的依据。

## 核查对象

- 要求：[docs/TASK.md](../TASK.md)。
- 实际解压核查：`dist/sage-tsdb-upstream-component-0.2.0-linux-x86_64.tar.gz`。
- 压缩包 SHA256：`6f4fe9de13f97882c18a8cb9728788a48a9ab35cf5dab7effefaa18ec10239fc`。
- 原始 `sage-tsdb-pecj-component-0.2.0-linux-x86_64.tar.gz` 仍只有 PECJ 程序和 SRTFD 头文件；本次完成状态不适用于那个旧包。
- `sage-tsdb-research-component` 是已停用的自建网络实验，不作为当前交付版本。

本次从压缩包重新解压到 `/tmp/sage-task-audit-20261103/` 后运行。Python 入口清除了
`PYTHONPATH`、`SRTFD_DIR`、`PECJ_DIR`，验证安装目录可以自行找到包内网络源码和资产。
本次只核查和生成报告，没有修改或重新打包现有压缩包，也没有修改神经网络。

## 按课题逐项核对

| TASK.md 要求 | 包内实现与证据 | 对应 Demo | 判断 |
|---|---|---|---|
| 乱序窗口聚合、PDA 迟到补偿 | PECJ 原生 IAWJ/IMA/AI；另有 Python Poisson/LogNormal 后验与缺失 Join 补偿 | 四个轻量示例中的 `join` 只演示缺失影响；`pecj-demo` 演示原生估计；`demo --posterior-only` 才演示解析后验补偿 | 已有可运行实现；算法精度和适用范围仍需验证 |
| Mean-field VI、ELBO、窗口参数优化 | `posterior.py` 有均值场分布、ELBO、重参数化梯度；60 个窗口均发生优化且 ELBO 提升 | `demo --posterior-only` 输出 `windows.csv`、预测区间和 ELBO | 解析 VI 已实现；未证明高频场景的目标时延和长期稳定性 |
| EMA/MLP 双模、按延迟方差和乱序比例自动切换 | EMA/VI 是配置选项，PECJ 神经 AI 是另一手动选定算子；没有连接二者的运行时自动切换控制器 | 当前对比独立运行 IAWJ/IMA/AI；没有自动切换轨迹 | **未完成自动双模机制**；复用原 linearVAE 不等于已经完成双模 |
| 神经网络拟合 ELBO、流式更新后验 | 包含原 linearVAE 权重及损失方法；当前 PECJ 主 Demo 以 `aiMode=inference` 做前向推断 | `pecj-demo` 日志证明确实执行神经估计，但没有展示在线 ELBO 更新 | **只有原模型推断演示，未完成该训练链路的演示验证** |
| 运行时自适应聚类、结构稳定演化 | `task_demos/models.py` 的 EMA 微簇、紧凑新簇缓冲、旧簇复用和容量淘汰 | `drift` 已运行 | 有独立轻量基线；未接入原 SRTFD 持续学习代理的聚类流程 |
| 持续学习、代表性核心集、均衡回放 | 原 GCFAggMVC 加训练接入层，实现梯度更新、特征核心集和类均衡回放；`train --resume` 可继续训练 | `continual` 是最近类中心基线；神经 `demo` 是全标注训练；本次额外验证包内模型继续训练 1 步并导出成功 | 神经在线更新能力已实现；**缺少 TEP 分阶段新故障、旧类遗忘的交付 Demo** |
| 稀缺标签、置信度伪标签 | Python `observe(..., use_pseudo=True)` 有置信度和距离门控，并保护已揭示的新类别批次；默认关闭 | 轻量 `continual` 演示伪标签；主神经训练 `pseudo_labels=0`，CLI `train` 没有开启伪标签的参数 | 代码机制存在；**原网络的稀缺标签伪标签 Demo 未填充** |
| 流式故障提前预警 | `warning` 通过真实 PECJ Join 加应用层残差告警，含质量门控；神经模型可做故障分类 | 合成时钟提前 7.9 秒；没有原神经模型的真实设备故障提前量评测 | 有合成预警 Demo；未完成真实设备神经预警验证 |
| 基于 CXL 的异构架构 | 未发现 CXL 接入或硬件运行证据 | 无 | **未完成** |
| 第三方测试报告 | 包内为内部测试日志、JSON 和样本 | 无第三方报告 | **未完成** |

SRTFD 的全部训练参数来自原 `GCFAggMVC`，网络源码与原仓库逐字节一致。
但包内只有 `models/network.py`，没有完整原 `agents/srtfd.py`、聚类、Buffer 和训练环境；
当前接入层不能称为已经完整复现原 SRTFD 持续学习算法。

## 包内 Demo 是否填充

**已填充且本次运行成功：**

| 入口（组件根目录下） | 实际展示内容 |
|---|---|
| `python3 share/sageTSDB/task_demos/run_demos.py --demo join` | 12 次真实 PECJ Join，全部符合可见输入的独立真值 |
| 同上 `--demo drift` | 在线微簇漂移、旧簇复用；此合成场景误报率由 33.19% 降至 2.98% |
| 同上 `--demo continual` | 最近类中心、稀缺标签、核心集与伪标签；不是神经网络 |
| 同上 `--demo warning` | Join、质量门控、残差预警；合成提前量 7.9 秒，拦截 1 个缺失窗口 |
| `bin/sage_tsdb_research pecj-demo` | 原 linearVAE 三组权重、原生 IAWJ/IMA/AI；AI 日志有实际神经估计 |
| `bin/sage_tsdb_research demo --posterior-only` | 解析 EMA/VI、迟到补偿、区间及 ELBO 输出 |
| `bin/sage_tsdb_srtfd --model models/srtfd.ts --input validation/tep_probe.csv` | 原网络 TorchScript 推断，44 条样本分类与 Python 一致 |
| `bin/sage_tsdb_research train --resume ...` | 包内原网络检查点的继续训练、回放、检查点及导出；需提供带 `label` 的 CSV |

`bin/sage_tsdb_research demo --tep-root ...` 的全标注原网络训练入口已经提供，
完整 TEP 数据没有放入包内，运行时需要提供数据目录。本次未重新跑 17 轮训练。
包内 `validation/diagnosis.json` 保存既有评测结果，44 条 probe 只用于一致性检查，
不能代替完整测试集或真实设备评测。

**尚未填充的专门 Demo：** 自动双模切换、PECJ 神经在线 ELBO 更新、TEP 神经分阶段持续学习
与伪标签、原神经模型真实设备提前预警、CXL 硬件接入。

## 本次运行证据与精度限制

- 包内 SHA256 清单：132 项全部通过。
- 包内 9 个 intelligence Python 源文件与当前源码一致；原 SRTFD 网络 SHA256 为
  `71662b7d92e40e2f3cc2567c8727244db8f5b0962aea1a2fcde404115ed449de`。
- 包内 Demo 测试：11 项通过，无跳过。
- 源码研究引擎测试：25 项通过，使用包内原始网络和 PECJ 资产，无跳过。
- 现有构建的原生回归：SRTFD 9 项、PECJ 19 项、调度器 9 项通过，无跳过；SRTFD 使用本次解压的模型。
- 包内 Python 与 C++ 推断：44 条样本，分类差异 0 条。
- 使用 probe 的真实标签和复制的检查点，完成 1 次梯度更新，输出新检查点和 TorchScript；
  原交付模型未改动。这只检查训练入口可运行，不作为更新效果评测。

本次解析后验 Demo（种子 2026、60 窗、每窗 VI 32 步）：

| 指标 | 结果 |
|---|---|
| 未补偿平均相对误差 | 16.94% |
| EMA 补偿平均相对误差 | 17.81%，本场景未优于未补偿 |
| VI 补偿平均相对误差 | 9.66% |
| VI 预测 95% 区间实际覆盖率 | 83.33%，仍欠校准 |
| VI 窗口平均计算时间 | 约 96 ms；为当前 CPU 运行结果，不是吞吐 SLA |

原生 PECJ Demo 的独立完整 Join 真值为 200000。IAWJ 返回 200000；IMA 估计 234761，
相对误差约 17.38%；提前水位线的 AI 已确认 50100、估计 300060，相对完整真值误差约 50.03%。
说明原权重调用已经接通，但不能据此宣称神经估计对当前数据已达到高精度。

包内既有 SRTFD 全标注评测：测试宏平均召回率约 **58.32%**、正常类召回率约 **14.4%**。
按非零类均判为异常的当前规则，正常类误报约 **85.6%**。因此模型能运行，
但当前诊断质量不足以支持生产预警达标结论。

## 解压后的复核命令

在最新组件根目录执行；`PYTHON` 需指向已安装 NumPy/PyTorch 的解释器：

```bash
sha256sum -c SHA256SUMS

python3 share/sageTSDB/task_demos/run_demos.py --demo all \
  --component "$PWD" --output /tmp/task-demos-check --seed 2026

OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 bin/sage_tsdb_srtfd \
  --model models/srtfd.ts --input validation/tep_probe.csv

PYTHON=/path/to/torch-python bin/sage_tsdb_research pecj-demo \
  --component "$PWD" --output /tmp/pecj-check

PYTHON=/path/to/torch-python bin/sage_tsdb_research demo \
  --posterior-only --windows 60 --vi-steps 32 --output /tmp/posterior-check
```

机器结果和本次日志保存在源码目录的
`examples/outputs/task_audit_20261103/audit.json` 及同目录日志。
原包缺少 `TASK.md` 副本，包内 `TASK_DEMOS.md` 的相对课题链接不能打开，后续打包应补齐。

建议对甲方描述为：**已补齐 PECJ/SRTFD 原网络可运行组件及多场景功能 Demo，
部分研究机制和硬件验收尚待完成。** 不应描述为完整 PDA/VI、自动双模、完整 SRTFD
持续学习和 CXL 第三方验收均已完成。
