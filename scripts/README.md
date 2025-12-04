# sageTSDB 脚本工具

本目录包含用于构建、测试和管理 sageTSDB 的各种脚本工具。

## 脚本列表

### 构建脚本

#### build.sh
**功能**: 主构建脚本
- 自动检测构建目录（SAGE 统一构建或本地构建）
- 配置 CMake 并编译项目
- 支持测试和安装选项

**用法**:
```bash
# 基本构建
./scripts/build.sh

# 构建并运行测试
./scripts/build.sh --test

# 构建并安装
./scripts/build.sh --install
```

#### build_plugins.sh
**功能**: 构建插件系统
- 检测 PECJ 库位置
- 编译核心库和插件
- 支持有/无 PECJ 的构建

**用法**:
```bash
./scripts/build_plugins.sh
```

**环境变量**:
- `PECJ_DIR`: PECJ 库路径（可选）

### 示例和演示脚本

#### build_and_test.sh
**功能**: 构建并运行完整的示例测试
- 检查前置条件
- 编译所有示例程序
- 运行快速验证测试
- 生成测试报告

**用法**:
```bash
./scripts/build_and_test.sh
```

**详细说明**: 参见脚本内部注释（290 行）

#### run_demo.sh
**功能**: 快速启动演示
- 交互式菜单选择演示
- 自动检查依赖和数据文件
- 支持多种演示场景

**用法**:
```bash
# 交互模式
./scripts/run_demo.sh

# 直接运行特定演示
./scripts/run_demo.sh integrated
./scripts/run_demo.sh pecj
./scripts/run_demo.sh persistence
```

**详细说明**: 参见脚本内部注释（306 行）

### 测试脚本

#### test_lsm_tree.sh
**功能**: LSM Tree 存储引擎测试
- 运行所有单元测试
- LSM Tree 性能测试
- 存储结构验证
- 生成测试报告

**用法**:
```bash
./scripts/test_lsm_tree.sh
```

**注意**: 需要先运行 `build.sh` 完成构建

### 已废弃脚本

#### ~~setup_repo.sh~~ (已废弃)
此脚本用于早期仓库设置，现已不再需要。
如需初始化 Git 仓库，请直接使用标准 Git 命令。

## 脚本使用指南

### 首次设置

```bash
# 1. 构建项目
./scripts/build.sh

# 2. 运行测试验证
./scripts/build.sh --test

# 3. 尝试运行演示
./scripts/run_demo.sh
```

### 日常开发

```bash
# 修改代码后重新构建
./scripts/build.sh

# 构建并测试
./scripts/build.sh --test

# 测试特定功能
./scripts/test_lsm_tree.sh

# 运行示例验证
./scripts/run_demo.sh
```

### 集成 PECJ

```bash
# 设置 PECJ 路径
export PECJ_DIR=/path/to/PECJ

# 使用 PECJ 支持构建
./scripts/build_plugins.sh

# 运行 PECJ 演示
./scripts/run_demo.sh pecj
```

## 脚本特性

### 自动路径检测
所有脚本都能自动检测：
- SAGE 项目根目录
- 构建目录位置
- PECJ 库路径

### 彩色输出
脚本使用彩色终端输出：
- 🟢 绿色：成功信息
- 🟡 黄色：警告信息
- 🔴 红色：错误信息
- 🔵 蓝色：标题和分隔

### 错误处理
所有脚本都包含：
- 前置条件检查
- 详细的错误消息
- 优雅的失败处理

## 环境变量

### 常用变量

- `PECJ_DIR`: PECJ 库安装路径
- `BUILD_DIR`: 自定义构建目录
- `CMAKE_BUILD_TYPE`: 构建类型（Debug/Release）

### 示例

```bash
# 自定义构建配置
export BUILD_DIR=./my_build
export CMAKE_BUILD_TYPE=Debug
./scripts/build.sh

# 使用特定 PECJ 版本
export PECJ_DIR=$HOME/libraries/PECJ
./scripts/build_plugins.sh
```

## 疑难解答

### 构建失败
1. 检查依赖是否安装（见根目录 README.md）
2. 清理构建目录：`rm -rf build && ./scripts/build.sh`
3. 查看详细错误输出

### 脚本权限问题
```bash
# 添加执行权限
chmod +x scripts/*.sh
```

### 路径问题
- 确保从项目根目录或 scripts 目录运行脚本
- 脚本会自动检测和调整工作目录

## 贡献指南

添加新脚本时请遵循：
1. 使用 `set -e` 启用错误退出
2. 添加详细的头部注释
3. 包含用法说明
4. 使用彩色输出
5. 更新本 README

## 更多信息

- **构建文档**: `docs/` 目录
- **示例程序**: `examples/` 目录  
- **测试用例**: `tests/` 目录
