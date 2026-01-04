# 测试数据集

本目录包含示例程序使用的测试数据集。

## 📊 数据集列表

### 1. sTuple.csv
**描述**: S 流数据（左表）

**格式**:
```csv
timestamp,key,value
1609459200000,key1,100.5
1609459201000,key2,200.3
...
```

**字段说明**:
- `timestamp`: Unix 时间戳（毫秒）
- `key`: Join 键
- `value`: 数据值

**数据规模**: ~50,000 条记录

**使用场景**: 流式 Join 测试的左表数据

---

### 2. rTuple.csv
**描述**: R 流数据（右表）

**格式**:
```csv
timestamp,key,value
1609459200500,key1,50.2
1609459201500,key3,150.8
...
```

**字段说明**:
- `timestamp`: Unix 时间戳（毫秒）
- `key`: Join 键
- `value`: 数据值

**数据规模**: ~50,000 条记录

**使用场景**: 流式 Join 测试的右表数据

---

## 🎯 数据特征

### 时间特性
- **时间跨度**: 约 1 小时
- **时间间隔**: 不规则（模拟真实流数据）
- **乱序率**: 可配置（0-30%）

### Key 分布
- **基数**: ~1000 个不同的 key
- **分布**: Zipf 分布（符合真实场景）
- **Join 选择性**: ~60%

### 数据质量
- **重复率**: <1%
- **缺失值**: 无
- **异常值**: <5%

---

## 🔧 生成自定义数据集

如果需要生成自定义规模或特性的数据集，可以使用 PECJ 的数据生成工具：

```bash
cd /path/to/PECJ/scripts/GenerateData
python3 generate_stream_data.py \
    --output-s ../../benchmark/datasets/sTuple_custom.csv \
    --output-r ../../benchmark/datasets/rTuple_custom.csv \
    --num-tuples 100000 \
    --disorder-rate 0.3 \
    --key-cardinality 2000
```

**参数说明**:
- `--num-tuples`: 每个流的元组数量
- `--disorder-rate`: 乱序率 (0.0-1.0)
- `--key-cardinality`: 不同 key 的数量
- `--distribution`: Key 分布 (uniform/zipf/normal)

---

## 📈 数据集使用示例

### 基础 Join 测试
```bash
cd build/examples
./pecj_replay_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --max-tuples 5000
```

### 性能测试
```bash
./performance_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --output-json ../../examples/outputs/results/benchmark.json
```

### 乱序处理测试
```bash
./deep_integration_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --disorder-rate 0.3
```

---

## 📖 相关文档

- [PECJ 数据生成工具](../../../PECJ/scripts/GenerateData/)
- [集成示例](../integration/)
- [性能测试](../benchmarks/)

---

## ⚠️ 注意事项

1. **数据路径**: 示例程序默认使用相对路径，请确保在正确的目录下运行
2. **数据格式**: CSV 文件需要包含表头行
3. **时间戳**: 必须是有效的 Unix 时间戳（毫秒）
4. **文件编码**: UTF-8
5. **文件大小**: 较大的数据集可能需要更多内存
