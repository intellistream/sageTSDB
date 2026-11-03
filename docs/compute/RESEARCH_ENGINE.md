# 直接复用 PECJ / SRTFD 原始神经网络

当前实现遵循“不要新增网络，直接复用 PECJ 和 SRTFD 中的神经网络”。
交付入口是 `sage-tsdb-upstream-component-0.2.0-linux-x86_64.tar.gz`。
此前的 `sage-tsdb-research-component` 和 `examples/outputs/research/` 属于已停用的自建网络实验，
其权重和准确率不作为当前版本的交付依据。旧研究组件包已从本地交付目录清除；保留的训练结果仅用于追溯，不参与当前交付。

## 复用边界

| 部分 | 直接使用的原始实现 | 接入层负责什么 |
|---|---|---|
| PECJ 神经推断 | `PECJ/benchmark/torchscripts/linearVAE/` 中的选择率、S/R 到达率 `.pt` 权重；原生 `AIOperator` / `LinearVAE` | 模型路径、缩放因子、CSV、到达顺序、原生算子调用及结果输出 |
| SRTFD 网络 | 直接导入 `SRTFD/models/network.py` 的 `GCFAggMVC` 类 | 训练数据标准化、训练循环、检查点、TorchScript 导出、C++ 推断与写库 |

接入代码不再定义 `Linear`、`TransformerEncoderLayer` 等网络层。SRTFD 的全部可训练参数
都属于原始 `GCFAggMVC` 实例；没有添加、替换或缩减原编码器、解码器、注意力和分类头。
模型来源记录原文件/资产的 SHA256，恢复检查点时验证 SRTFD 网络源码哈希。
包中 `share/upstream/` 保存原始文件，源码中的源文件与包内副本逐字节一致。

复用原网络不表示已经复现论文中的所有数据处理、训练协议和实验结果。当前训练接入层
与原始 `SRTFD/agents/srtfd.py` 是不同的训练驱动，不将二者混称为完整论文复现。

## PECJ：确实调用原神经网络

Python `PECJNeuralModels` 使用 `torch.jit.load` 加载原始 `.pt`，读取原始 `_s.pt` 缩放因子，
调用原模型的 `forward` 与 `getMuEstimation`。没有用新建网络替代原权重。

原生命令行新增 `--model-root`、`--model-prefix`、`--watermark-ms`。`AI` 以原有
`aiMode=inference` 执行。示例设置提前水位线，检查日志中实际出现选择率/到达率神经估计，
避免仅加载模型却没有执行神经推断。`aqp_estimate` 返回原算子的近似结果。

CSV 支持 `timestamp_us,key,value`，可选第四列 `arrival_time_us`；时间均为微秒。
调用方保留独立的事件/到达时间，S/R 记录合并后按到达时间输入原生 PECJ。未提供第四列时
使用事件时间作为到达时间。默认水位线为 0，仍可用完整批次执行精确窗口 Join。

```bash
# 在解压后的组件根目录：
./bin/sage_tsdb_pecj --self-test
OMP_NUM_THREADS=2 ./bin/sage_tsdb_pecj \
  --operator AI --model-root share/upstream/PECJ --model-prefix linearVAE \
  --s-file validation/pecj/S.csv --r-file validation/pecj/R.csv \
  --window-start-us 1000000 --window-end-us 1012000 --watermark-ms 6
```

原始权重的目标是到达率和 Join 选择率，不能拿来直接预测故障类别。其在不同数据流上的
估计精度需要单独评测，不能因为提供了 `.pt` 文件就宣称对任意场景准确。

## SRTFD：保持原网络的输入和计算方式

`srtfd_class()` 直接从原文件导入类，实例化方式与原 TEP 配置相同：

```python
GCFAggMVC(input_size=52, low_feature_dim=52, num_class=22, device=cpu)
```

原网络采用二维 `[batch,feature]` 输入，预测与批次上下文有关。当前没有再改成每样本
一个 token，而是原样调用 `network.forward`。Python 与导出模型采用固定 100 条分批，
C++ 每次把有效窗口记录作为批次交给导出模型，由模型内部完成相同分批。
一致性比较必须使用相同的记录顺序与分批；同一向量放进不同批次可能得到不同预测。

`SRTFDAdapter` 只包含输入标准化、输出类别掩码和原网络调用。`seen_classes`、均值和
标准差都是非训练缓冲区。网络结构、隐藏层宽度、原注意力计算均保持不变。

本次重新训练全标注基线，先确认原网络行为；不沿用旧自建网络权重。伪标签默认关闭，
在线更新遇到真实标签确认的新类别时也禁用该批伪标签，避免用旧类别误标新故障。
在线接口仍支持真实标签更新、类均衡回放和核心集；这些是训练接入机制，不是新增网络。

## 训练、验证与输出

Python 需要 3.10+、NumPy、PyTorch 1.13+，CPU 可运行。PyTorch 1.x 推荐 NumPy 1.x；
在 NumPy 2.x 环境中旧版 PyTorch 可能打印兼容警告。原生推断不需要 Python。

在 sageTSDB 源码目录执行：

```bash
PYTHON=/path/to/torch-python bash scripts/demo/run_research_demos.sh \
  --epochs 40 --patience 8 --component "$PWD/build/research_native"
```

该入口运行原始 PECJ 模型及原生 IAWJ/IMA/AI 对比，然后训练原始 SRTFD 网络。
可通过 `--tep-root` 指向 TEP 文件目录，通过 `--pecj-root` 指向 PECJ 仓库或资产根目录。
输出为 `examples/outputs/reused_models/`：

| 路径 | 内容 |
|---|---|
| `pecj/summary.json` | 原资产哈希、原模型预测、原生算子结果 |
| `pecj/AI.log` | 提前水位线及实际神经网络推断日志 |
| `diagnosis/history.csv` | 每轮训练损失、验证集宏召回率和正常类召回率 |
| `diagnosis/best.checkpoint` | 仅按验证集选择的原网络检查点 |
| `diagnosis/srtfd.checkpoint` | 可继续训练的原网络、优化器、标准化和回放样本 |
| `diagnosis/srtfd.ts` | 原网络的 TorchScript 部署快照 |
| `diagnosis/inference_probe.csv` | 44 条真实 TEP 样本与 Python 预测，用于原生一致性检查 |

TEP 使用 22 个 960x52 文件。正常文件训练行 0:640，故障文件训练行 160:640；
各文件验证行 640:800，测试行 800:960。故障文件前 160 条正常样本不标成故障。
标准化只拟合训练集。验证/测试以固定随机顺序分批，避免类别连续排列成为额外上下文。
验证集负责早停和检查点选择，测试集只对选中的最终模型评测。

本次种子 2026 的全标注基线运行 17 轮后早停，选中第 9 轮：

| 指标 | 结果 |
|---|---|
| 验证集宏平均召回率 | 59.29% |
| 测试集宏平均召回率 | 58.32% |
| 测试集正常类召回率 | 14.37% |
| 网络参数 | 2843598，全部来自原 GCFAggMVC |
| Python / C++ 一致性 | 44 条样本，0 条分类差异 |

全标注基线与旧版 25% 标签实验条件不同，不能把数值差异解释为算法优劣。
正常类识别效果仍不足，当前模型不作为生产诊断达标的证明。该划分来自同一仿真运行内的
时间段，也不能代替跨工厂或独立仿真运行评测。

## 导出、接入和打包

导出使用与部署 LibTorch 相同版本的 PyTorch。当前基础组件的 CPU LibTorch 为 1.13：

```bash
PYTHONPATH="$PWD" /path/to/torch-1.13-python -m sage_tsdb.intelligence.cli export \
  --model examples/outputs/reused_models/diagnosis/srtfd.checkpoint \
  --output examples/outputs/reused_models/diagnosis/srtfd-libtorch-1.13.ts

bash scripts/build/package_research_component.sh --python /path/to/torch-1.13-python
```

新包为 `dist/sage-tsdb-upstream-component-0.2.0-linux-x86_64.tar.gz`，包含原始网络定义、
原始 PECJ 模型资产、重新训练的原 SRTFD 权重、两个原生命令行、训练接入代码和验证数据。
旧格式自建网络检查点会被拒绝加载，不会静默改名当成原网络权重。

解压后执行：

```bash
sha256sum -c SHA256SUMS
OMP_NUM_THREADS=2 ./bin/sage_tsdb_srtfd \
  --model models/srtfd.ts --input validation/tep_probe.csv

PYTHON=/path/to/torch-python ./bin/sage_tsdb_research pecj-demo \
  --component "$PWD" --pecj-root "$PWD/share/upstream/PECJ" --output /tmp/pecj-original
```

自有传感器 CSV 使用 `f0,...,f51`，训练时添加 `label`；缺失标签填 -1：

```bash
PYTHONPATH="$PWD" /path/to/python -m sage_tsdb.intelligence.cli train \
  --input new_sensor_batch.csv \
  --resume examples/outputs/reused_models/diagnosis/srtfd.checkpoint \
  --steps 16 --output /tmp/srtfd-updated
```

PECJ C ABI 保持 v1。原生 SRTFD 的 `backend="torchscript"` 加载部署快照，并能写回
`srtfd_results`；Python 负责训练和导出，更新后需用新快照初始化原生引擎。

## 对照实现与验证范围

原有四个标准库 Demo 仍保留为轻量功能基线。Python 中的解析 EMA/VI 也可作为独立
数学对照；自建 `AmortizedPosterior` 已删除，`mlp/dual` 模式不再接到自建网络。
主 Demo 的神经输出来自原生 PECJ AI 和原 SRTFD 类，不来自这些对照代码。

测试检查原类身份、原网络参数归属、原文件哈希、接入代码无新增网络层、伪标签防护、
保存恢复、TorchScript 一致性，以及原生 PECJ/SRTFD/调度器回归。

```bash
PYTHONPATH="$PWD" /path/to/python -m unittest discover \
  -s tests -p test_research_intelligence.py -v
```

模型可运行、源码复用、预测准确率和课题全部验收是不同的验证范围。CXL、真实设备故障
提前量、正常类误报控制及论文指标复现仍需独立测试。
