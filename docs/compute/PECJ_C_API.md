# PECJ C API

版本：1

头文件：`sage_tsdb/c_api/pecj.h`

动态库：`libsage_tsdb_pecj_c.so`（SONAME：`libsage_tsdb_pecj_c.so.1`）

此接口是 PECJ 计算组件推荐的二进制集成边界。实现内部仍使用 C++，但导出接口只包含
C linkage、定宽整数、普通结构体和 opaque handle，不向调用方暴露 STL、异常、RTTI 或
C++ 对象布局。

## ABI 规则

- `sage_pecj_abi_version()` 当前返回 `SAGE_PECJ_ABI_VERSION_V1`。
- ELF SONAME 主版本为 `1`；不兼容的 ABI 变更必须同时提升 SONAME 和 API 版本。
- 配置和结果必须先分别调用 `sage_pecj_config_v1_init()` 与
  `sage_pecj_result_v1_init()`。
- 不得自行修改 `struct_size` 或 `abi_version`。
- `operator_name` 和 `watermark_name` 只在 `sage_pecj_create()` 调用期间读取，创建成功后
  调用方可释放对应字符串。
- handle 及其内部对象必须使用 `sage_pecj_destroy()` 释放。
- 所有异常都在动态库内部转换为 `SAGE_PECJ_ERROR_*`，不会穿越 C ABI。
- 单个 handle 的插入、执行和清空操作串行化；多个 handle 可以并行使用。
- `sage_pecj_last_error()` 将错误复制到调用方缓冲区。先以空缓冲区调用可获得所需字节数。
- 销毁 handle 时，调用方必须保证没有其他线程仍在使用该 handle。

## 基本流程

```c
#include <sage_tsdb/c_api/pecj.h>

sage_pecj_config_v1 config;
sage_pecj_result_v1 result;
sage_pecj_handle *handle = NULL;

sage_pecj_config_v1_init(&config);
config.operator_name = "IAWJ";

if (sage_pecj_create(&config, &handle) != SAGE_PECJ_OK) {
    /* Read sage_pecj_last_error(NULL, ...). */
}

sage_pecj_record_v1 s[] = {{1000000, 7, 10.0}};
sage_pecj_record_v1 r[] = {{1050000, 7, 20.0}};
sage_pecj_insert_s(handle, s, 1);
sage_pecj_insert_r(handle, r, 1);

sage_pecj_result_v1_init(&result);
sage_pecj_execute(handle, 1, 1000000, 1100000, &result);
sage_pecj_destroy(handle);
```

包内完整示例位于 `share/sageTSDB/c_component_consumer/`。

## 数据及窗口语义

- `timestamp_us` 和窗口边界的单位均为微秒。
- 窗口范围是左闭右开 `[start_us, end_us)`。
- 相同 key 的 S/R 记录构成连接匹配。
- `sage_pecj_clear()` 清空 S/R 输入数据并重置累计指标，但保留当前配置。
- 当前结果只返回聚合统计，不返回匹配明细，也不自动持久化结果。

## 兼容边界

C ABI 消除了调用方对 GCC C++ ABI、STL 布局和 `_GLIBCXX_USE_CXX11_ABI` 的依赖。
动态库内部仍依赖随包提供的 PECJ/Torch 库，并受交付说明列出的 glibc、CPU 和操作系统要求
约束。
