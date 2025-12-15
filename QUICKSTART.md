# 🚀 Quick Start Guide - sageTSDB Deep Integration with PyTorch & PECJ

## ✅ 已完成配置

PyTorch 和 PECJ 已成功集成到 sageTSDB 深度融合模式中。

## 📦 环境信息

- **PyTorch 版本**: 1.13.0
- **PyTorch 路径**: `/home/cdb/.local/lib/python3.10/site-packages/torch`
- **PECJ 路径**: `/home/cdb/dameng/PECJ`
- **编译器**: GCC 11.4.0 (C++20)

## 🏃 运行 Demo

### 方法 1: 使用环境脚本（推荐）

```bash
cd /path/to/sageTSDB/build/examples

# 配置环境
source setup_env.sh

# 运行 demo
./deep_integration_demo --events-s 5000 --events-r 5000 --keys 100
```

### 方法 2: 手动设置环境变量

```bash
export LD_LIBRARY_PATH=/home/cdb/dameng/PECJ/build:/home/cdb/.local/lib/python3.10/site-packages/torch/lib:$LD_LIBRARY_PATH

cd /path/to/sageTSDB/build/examples
./deep_integration_demo [options]
```

## 🎯 Demo 参数

```bash
./deep_integration_demo --help

选项:
  --events-s N      Stream S 事件数量 (默认: 10000)
  --events-r N      Stream R 事件数量 (默认: 10000)
  --keys N          Key 范围 (默认: 100)
  --ooo-ratio R     乱序比例 0.0-1.0 (默认: 0.2)
  --threads N       最大线程数 (默认: 4)
  --help            显示帮助
```

## 📝 示例命令

### 快速测试
```bash
./deep_integration_demo --events-s 1000 --events-r 1000 --keys 50
```

### 中等规模
```bash
./deep_integration_demo --events-s 5000 --events-r 5000 --keys 100 --ooo-ratio 0.3 --threads 8
```

### 性能测试
```bash
./deep_integration_demo --events-s 50000 --events-r 50000 --keys 1000 --threads 16
```

## 🔧 重新编译

如果需要重新编译：

```bash
cd /path/to/sageTSDB
rm -rf build
mkdir build && cd build

# 配置
cmake .. \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_FULL_INTEGRATION=ON \
    -DPECJ_DIR=/home/cdb/dameng/PECJ \
    -DBUILD_TESTS=OFF

# 编译
make deep_integration_demo -j$(nproc)

# 运行
cd examples
source setup_env.sh
./deep_integration_demo
```

## ✅ 验证清单

- [x] PyTorch 1.13.0 已找到并链接
- [x] PECJ 库已链接
- [x] Deep Integration 模式已启用
- [x] Demo 编译成功
- [x] Demo 运行成功
- [x] 吞吐量测试通过 (>800K events/sec)

## 📚 更多信息

- 详细文档: `README_DEEP_INTEGRATION_DEMO.md`
- PyTorch 集成: `PYTORCH_INTEGRATION_SUCCESS.md`
- 架构设计: `docs/DESIGN_DOC_SAGETSDB_PECJ.md`

## 🎉 成功！

sageTSDB 深度融合模式已完全就绪，支持：
- ✅ 数据库中心化设计
- ✅ PECJ 无状态计算引擎
- ✅ PyTorch 完整功能
- ✅ 资源管理和调度
- ✅ 高吞吐量数据处理

开始您的时序数据流式 Join 之旅吧！🚀
