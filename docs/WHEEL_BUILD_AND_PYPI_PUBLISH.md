# sageTSDB Wheel 构建和 PyPI 发布指南

## 概述

本文档说明如何构建 manylinux 兼容的 sageTSDB wheel 并发布到 PyPI，以解决 GLIBC 版本兼容性问题。

## 问题背景

### GLIBC 兼容性问题

**现象**: 在 Ubuntu 20.04 (GLIBC 2.31) 上安装 isage-tsdb 时报错：
```
ImportError: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.38' not found
```

**原因**: 
- sageTSDB 之前在 Ubuntu 24.04 (GLIBC 2.38) 上构建
- 生成的 wheel 依赖高版本 GLIBC
- 旧版本系统无法使用

**解决方案**: 
- ✅ 在 Ubuntu 22.04 (GLIBC 2.35) 上重新构建
- ✅ 使用 manylinux2014 标准（GLIBC 2.17+）构建兼容 wheel
- ✅ 重新发布到 PyPI

## 当前状态

### 版本信息
- **当前版本**: 0.1.5
- **构建环境**: Ubuntu 22.04.3 LTS (GLIBC 2.35)
- **Python 版本**: 3.11
- **Wheel 平台**: linux_x86_64

### 已完成的改进

1. **更新版本号**: 0.1.4 → 0.1.5
2. **配置 manylinux 支持**: 添加 cibuildwheel 配置
3. **创建构建脚本**: 
   - `scripts/build_native_wheel.sh` - 本地构建
   - `scripts/build_manylinux_wheel.sh` - Docker manylinux 构建
   - `scripts/upload_to_pypi.sh` - PyPI 上传
4. **GitHub Actions**: `.github/workflows/build-wheels.yml` (CI/CD)

## 构建流程

### 方法 1: 本地快速构建（推荐用于当前系统）

适用于在 Ubuntu 22.04+ 上构建，兼容 GLIBC 2.35+：

```bash
cd /home/shuhao/sageTSDB

# 1. 清理旧构建
rm -rf build dist *.egg-info

# 2. 构建 wheel
./scripts/build_native_wheel.sh

# 输出: dist/isage_tsdb-0.1.5-py310-none-linux_x86_64.whl
```

**优点**:
- 快速（~2-3 分钟）
- 适合本系统和更高版本 GLIBC

**缺点**:
- 不兼容低版本 GLIBC 系统

### 方法 2: manylinux2014 构建（最佳兼容性）

使用 Docker 构建兼容 GLIBC 2.17+ 的 wheel：

```bash
cd /home/shuhao/sageTSDB

# 1. 确保 Docker 已安装并运行
docker --version

# 2. 使用 manylinux 镜像构建
./scripts/build_manylinux_wheel.sh

# 输出: dist/isage_tsdb-0.1.5-cp310-cp310-manylinux2014_x86_64.whl
```

**优点**:
- 兼容 GLIBC 2.17+ (CentOS 7, Ubuntu 18.04+)
- 符合 PyPI manylinux 标准
- 适合公开发布

**缺点**:
- 需要 Docker
- 构建时间较长（~5-10 分钟）

### 方法 3: 使用 cibuildwheel（CI/CD）

通过 GitHub Actions 自动构建多平台 wheel：

```bash
# 1. 推送 tag 触发构建
git tag v0.1.5
git push origin v0.1.5

# 2. GitHub Actions 自动：
#    - 构建 Python 3.10, 3.11, 3.12 wheels
#    - 使用 manylinux2014
#    - 运行 auditwheel repair
#    - 上传到 PyPI (如果有 PYPI_TOKEN)
```

## 上传到 PyPI

### 使用 sage-pypi-publisher

```bash
cd /home/shuhao/sageTSDB

# 1. 确认 wheel 存在
ls -lh dist/

# 2. 交互式上传
./scripts/upload_to_pypi.sh

# 按提示选择：
#   1) TestPyPI - 测试环境
#   2) PyPI - 生产环境
```

### 手动上传（备选）

```bash
# TestPyPI
twine upload --repository testpypi dist/*.whl

# 生产 PyPI
twine upload dist/*.whl
```

### 配置 PyPI 认证

创建 `~/.pypirc`:

```ini
[distutils]
index-servers =
    pypi
    testpypi

[pypi]
username = __token__
password = pypi-YOUR_PRODUCTION_TOKEN

[testpypi]
repository = https://test.pypi.org/legacy/
username = __token__
password = pypi-YOUR_TEST_TOKEN
```

或使用环境变量：

```bash
export TWINE_USERNAME=__token__
export TWINE_PASSWORD=pypi-YOUR_TOKEN
```

## 测试安装

### 从 TestPyPI 测试

```bash
# 创建新虚拟环境
python3 -m venv test_env
source test_env/bin/activate

# 从 TestPyPI 安装
pip install -i https://test.pypi.org/simple/ \
    --extra-index-url https://pypi.org/simple \
    isage-tsdb==0.1.5

# 测试导入
python -c "import sage_tsdb; print(sage_tsdb.__version__)"
```

### 从生产 PyPI 安装

```bash
# 安装
pip install --upgrade isage-tsdb

# 验证
python -c "import sage_tsdb; print(sage_tsdb.__version__)"
```

## 兼容性检查

### 查看 wheel 依赖的 GLIBC 版本

```bash
# 提取 .so 文件
unzip -j dist/isage_tsdb-0.1.5*.whl 'sage_tsdb/*.so' -d /tmp/check/

# 查看依赖
objdump -p /tmp/check/_sage_tsdb*.so | grep GLIBC

# 应该看到 GLIBC_2.XX 版本要求
```

### 在目标系统测试

```bash
# 在 Ubuntu 20.04/22.04 虚拟机或容器中
docker run -it ubuntu:20.04 bash

apt update && apt install -y python3 python3-pip
pip3 install isage-tsdb==0.1.5
python3 -c "import sage_tsdb; print('Success!')"
```

## 持续集成

### GitHub Actions 自动发布

1. **配置 Secret**: 
   - 在 GitHub repo settings → Secrets 中添加 `PYPI_TOKEN`

2. **触发发布**:
   ```bash
   git tag v0.1.6
   git push origin v0.1.6
   ```

3. **自动流程**:
   - 构建多平台 wheels
   - 运行测试
   - 上传到 PyPI
   - 创建 GitHub Release

## 故障排除

### 问题: GLIBC 版本冲突

```
ImportError: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.XX' not found
```

**解决**:
1. 使用 manylinux2014 构建（`build_manylinux_wheel.sh`）
2. 或升级目标系统到 Ubuntu 22.04+

### 问题: 构建失败 - CMake 错误

```
CMake Error: ... not found
```

**解决**:
```bash
# 安装构建依赖
sudo apt install -y cmake g++ make python3-dev
pip install scikit-build-core pybind11
```

### 问题: 上传失败 - 认证错误

```
403 Forbidden: Invalid or non-existent authentication information
```

**解决**:
1. 检查 PyPI token 是否正确
2. 确认 `~/.pypirc` 配置
3. 使用 `--repository testpypi` 先测试

### 问题: wheel 平台标签不兼容

```
isage_tsdb-0.1.5-py310-none-linux_x86_64.whl is not a supported wheel
```

**解决**:
- 使用 `auditwheel repair` 修复：
  ```bash
  pip install auditwheel
  auditwheel repair dist/*.whl -w dist/fixed/
  ```

## 文件清单

### 新增/修改的文件

```
sageTSDB/
├── sage_tsdb/_version.py                      # 版本号 → 0.1.5
├── pyproject.toml                              # 添加 cibuildwheel 配置
├── .github/workflows/build-wheels.yml          # CI/CD 工作流
├── docker/Dockerfile.manylinux                 # manylinux 构建镜像
└── scripts/
    ├── build_native_wheel.sh                   # 本地构建脚本
    ├── build_manylinux_wheel.sh                # Docker manylinux 构建
    └── upload_to_pypi.sh                       # PyPI 上传脚本
```

## 推荐工作流

### 开发迭代

1. 本地修改代码
2. 运行测试: `./scripts/build/build_and_test.sh`
3. 本地构建: `./scripts/build_native_wheel.sh`
4. 本地测试安装

### 发布版本

1. 更新版本号: `sage_tsdb/_version.py`
2. 提交更改: `git commit -am "Bump version to 0.1.X"`
3. 创建 tag: `git tag v0.1.X && git push origin v0.1.X`
4. GitHub Actions 自动构建和发布
5. 或手动: `./scripts/build_manylinux_wheel.sh && ./scripts/upload_to_pypi.sh`

## 相关资源

- **PyPI Package**: https://pypi.org/project/isage-tsdb/
- **TestPyPI**: https://test.pypi.org/project/isage-tsdb/
- **manylinux**: https://github.com/pypa/manylinux
- **cibuildwheel**: https://cibuildwheel.readthedocs.io/
- **sage-pypi-publisher**: /home/shuhao/sage-pypi-publisher/

## 下一步

1. ✅ 测试 v0.1.5 在 TestPyPI
2. ✅ 确认在 Ubuntu 20.04/22.04 上可用
3. ✅ 发布到生产 PyPI
4. 🔄 通知 sage-benchmark 团队使用新版本
5. 🔄 更新 SAGE 依赖到 isage-tsdb>=0.1.5

---

**构建时间**: 2026-01-04  
**维护者**: SAGE Team  
**联系方式**: shuhao_zhang@hust.edu.cn
