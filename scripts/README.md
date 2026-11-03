# sageTSDB 脚本

所有命令在仓库根目录执行；脚本通过自身位置定位源码。

| 目录 | 用途 | 说明 |
| --- | --- | --- |
| [build/](build/README.md) | C++ 构建、wheel 和组件打包 | `build.sh` 是统一构建入口 |
| [demo/](demo/README.md) | 课题、原网络、集成与性能演示 | 源码位于 `examples/` |
| [test/](test/README.md) | 存储测试和 PECJ 模式对比 | 单元测试位于 `tests/` |
| `release/` | 上传已有 wheel | `upload_to_pypi.sh` 手动选择 PyPI 或 TestPyPI |

```bash
# 独立 C++ 构建与测试
bash scripts/build/build.sh --test

# 插件适配器，默认使用 stub
bash scripts/build/build.sh --plugins --test

# 真实 PECJ 集成
bash scripts/build/build.sh --pecj-dir /path/to/PECJ --test

# 四个课题基线 Demo
bash scripts/demo/run_task_demos.sh --component /path/to/component
```

构建产物统一位于 `build/`，交付包位于 `dist/`，演示结果位于
`examples/outputs/`。这些目录不提交到 Git。训练权重和评测证据应在清理前另行保存。
