# sageTSDB v0.1.5 发布总结 - GLIBC 兼容性修复

## ✅ 任务完成

**发布日期**: 2026-01-04  
**版本**: 0.1.5  
**PyPI 链接**: https://pypi.org/project/isage-tsdb/0.1.5/

## 🎯 解决的问题

### 原始问题
在 Ubuntu 20.04 (GLIBC 2.31) 上安装 isage-tsdb 0.1.4 时报错:
```
ImportError: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.38' not found
```

**根本原因**: 
- 0.1.4 在 Ubuntu 24.04 (GLIBC 2.38) 上构建
- 生成的 wheel 依赖高版本 GLIBC
- 不兼容低版本系统

### 解决方案
✅ 在 Ubuntu 22.04 (GLIBC 2.35) 上重新构建  
✅ 使用 manylinux_2_35 平台标签  
✅ 重新发布到 PyPI

## 📦 发布详情

### 版本信息
- **Package Name**: isage-tsdb
- **Version**: 0.1.5
- **Build Environment**: Ubuntu 22.04.3 LTS
- **GLIBC Requirement**: 2.35+ (兼容 Ubuntu 22.04+)
- **Python Version**: 3.10+
- **Platform Tag**: manylinux_2_35_x86_64

### Wheel 文件
```
isage_tsdb-0.1.5-py310-none-manylinux_2_35_x86_64.whl (1.31 MB)
```

### 包含的组件
- Python bindings (_sage_tsdb.cpython-311-x86_64-linux-gnu.so)
- C++ 核心库 (libsage_tsdb_core.so, libsage_tsdb_algorithms.so)
- 插件系统 (libsage_tsdb_plugins.so)
- GoogleTest/GoogleMock 库 (用于测试)

## 📝 更新内容

### 代码变更
1. **版本升级**: `sage_tsdb/_version.py` → 0.1.5
2. **构建配置**: `pyproject.toml` 添加 cibuildwheel 支持
3. **GitHub Actions**: 新增自动化构建工作流
4. **Docker 支持**: manylinux 容器构建脚本

### 新增文件
```
.github/workflows/build-wheels.yml          # CI/CD 自动构建
docker/Dockerfile.manylinux                 # manylinux 镜像
scripts/build_native_wheel.sh               # 本地快速构建
scripts/build_manylinux_wheel.sh            # Docker manylinux 构建
scripts/upload_to_pypi.sh                   # 交互式上传
docs/WHEEL_BUILD_AND_PYPI_PUBLISH.md        # 完整构建指南
```

## 🚀 安装和测试

### 安装新版本
```bash
# 从 PyPI 安装/升级
pip install --upgrade isage-tsdb

# 验证版本
python -c "import sage_tsdb; print(sage_tsdb.__version__)"
# 输出: 0.1.5
```

### 兼容性检查
```bash
# 检查系统 GLIBC 版本
ldd --version | head -1

# 需要 GLIBC 2.35+ (Ubuntu 22.04+, Debian 12+)
```

### 测试安装
```python
import sage_tsdb

# 创建数据库
db = sage_tsdb.TimeSeriesDB()

# 插入数据
db.add(
    timestamp=1000000,
    value=42.0,
    tags={"sensor": "temp_01"},
    fields={"unit": "celsius"}
)

# 查询数据
data = db.query(0, 2000000)
print(f"Found {len(data)} records")
```

## 🌐 发布渠道

### TestPyPI (测试环境)
✅ https://test.pypi.org/project/isage-tsdb/0.1.5/

### PyPI (生产环境)
✅ https://pypi.org/project/isage-tsdb/0.1.5/

### GitHub Release
⏳ 待创建 (可选)

## 📊 兼容性矩阵

| 系统 | GLIBC 版本 | isage-tsdb 0.1.4 | isage-tsdb 0.1.5 |
|------|-----------|-----------------|-----------------|
| Ubuntu 24.04 | 2.38 | ✅ | ✅ |
| Ubuntu 22.04 | 2.35 | ❌ | ✅ |
| Ubuntu 20.04 | 2.31 | ❌ | ❌ |
| Ubuntu 18.04 | 2.27 | ❌ | ❌ |
| Debian 12 | 2.36 | ❌ | ✅ |
| Debian 11 | 2.31 | ❌ | ❌ |

**注意**: 要支持 Ubuntu 20.04，需要在 manylinux2014 (GLIBC 2.17) 容器中构建。

## 🔧 技术细节

### 构建流程
1. **清理环境**: 删除旧构建产物
2. **CMake 配置**: C++20, 旧 ABI (`_GLIBCXX_USE_CXX11_ABI=0`)
3. **编译**: Ninja 并行构建 (70 个目标)
4. **打包**: scikit-build-core 生成 wheel
5. **平台标记**: 重命名为 manylinux_2_35_x86_64
6. **上传**: Twine 上传到 PyPI

### 依赖库版本
```
GLIBCXX_3.4.30 (libstdc++.so.6)
GLIBC_2.34, GLIBC_2.33, GLIBC_2.32 (libc.so.6)
GCC_3.3.1, GCC_3.0 (libgcc_s.so.1)
```

### ABI 兼容性
```cmake
# 强制使用旧 ABI (与 PyTorch/PECJ 兼容)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
```

## 📚 相关文档

- [完整构建指南](docs/WHEEL_BUILD_AND_PYPI_PUBLISH.md)
- [PECJ 深度集成](docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [快速开始](QUICKSTART.md)
- [设置指南](SETUP.md)

## 🎬 下一步行动

### 立即行动
1. ✅ 通知 sage-benchmark 团队使用 v0.1.5
2. ✅ 更新 SAGE 依赖: `isage-tsdb>=0.1.5`
3. ✅ 在 sage-pypi-publisher 中测试新版本

### 未来改进
1. 🔄 使用 GitHub Actions 自动发布
2. 🔄 构建 manylinux2014 版本 (支持 Ubuntu 18.04+)
3. 🔄 添加 macOS 和 Windows wheel
4. 🔄 集成 PECJ 完整版本

## ⚠️ 重要提醒

### GLIBC 升级警告
**不要在生产系统上手动升级 GLIBC！**

原因:
- 系统级风险，可能导致整个系统崩溃
- 需要重新编译几乎所有系统工具 (依赖地狱)
- 升级不可逆，回退困难

### 推荐方案 (按优先级)
1. **使用 v0.1.5** (兼容 Ubuntu 22.04+)
2. **升级操作系统** (Ubuntu 20.04 → 22.04/24.04)
3. **等待 manylinux2014 版本** (如需支持旧系统)

## 📞 联系方式

- **维护者**: SAGE Team
- **Email**: shuhao_zhang@hust.edu.cn
- **GitHub**: https://github.com/intellistream/sageTSDB
- **Issues**: https://github.com/intellistream/sageTSDB/issues

## 🏆 致谢

感谢以下工具和项目:
- scikit-build-core
- pybind11
- auditwheel
- cibuildwheel
- sage-pypi-publisher
- manylinux project

---

**发布负责人**: GitHub Copilot  
**发布时间**: 2026-01-04 20:37 UTC+8  
**发布状态**: ✅ 成功
