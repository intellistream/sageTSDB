# 构建和打包

从仓库根目录运行。C++ 构建统一使用 `build.sh`，原 `build_and_test.sh` 和
`build_plugins.sh` 的功能分别合入 `--test` 和 `--plugins`。

```bash
bash scripts/build/build.sh --test
bash scripts/build/build.sh --plugins --test
bash scripts/build/build.sh --pecj-dir /path/to/PECJ --test
bash scripts/build/build.sh --python --install
bash scripts/build/build.sh --help
```

默认构建 C++ 库、示例和测试，不启用 Python 绑定或真实 PECJ。
`--python` 使用现有 Python 环境中的 pybind11；`--pecj-dir` 需要已构建的 PECJ
和兼容的 LibTorch。额外 CMake 选项放在 `--` 后，例如：

```bash
bash scripts/build/build.sh --build-dir build/debug --test -- -DCMAKE_BUILD_TYPE=Debug
```

`--clean` 清理当前构建目标，`--install` 安装到构建目录的 `install/`。

| 脚本 | 产物 / 用途 | 详细说明 |
| --- | --- | --- |
| `build_native_wheel.sh` | 当前平台的 Python wheel；保留已有组件包 | [wheel 构建](../../docs/WHEEL_BUILD_AND_PYPI_PUBLISH.md) |
| `build_manylinux_wheel.sh` | Docker manylinux wheel | [wheel 构建](../../docs/WHEEL_BUILD_AND_PYPI_PUBLISH.md) |
| `package_pecj_compute_component.sh` | PECJ C/C++ 库、CLI、运行库和消费者验证 | [组件交付](../../docs/compute/PECJ_COMPUTE_COMPONENT_DELIVERY.md) |
| `package_research_component.sh` | 复用原 PECJ/SRTFD 网络的 upstream 组件 | [原网络接入](../../docs/compute/RESEARCH_ENGINE.md) |
| `package_task_demos.sh` | 无需重新编译的 Python 示例补充包 | [课题示例](../../examples/task_demos/README.md) |

上传 wheel 使用 `bash scripts/release/upload_to_pypi.sh`，构建过程本身不发布。
