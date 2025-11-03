# sageTSDB 数据持久化功能文档

## 概述

sageTSDB 现在支持完整的数据持久化功能，允许将时序数据保存到磁盘并在需要时恢复。持久化系统包括：

- ✅ 二进制序列化存储
- ✅ 检查点 (Checkpoint) 管理
- ✅ 增量写入支持
- ✅ 数据完整性验证
- ✅ 标量和向量值支持
- ✅ 标签和字段元数据持久化

## 功能特性

### 1. 数据保存和加载

```cpp
#include "sage_tsdb/core/time_series_db.h"

TimeSeriesDB db;

// 添加数据
db.add(timestamp, value, tags, fields);

// 保存到磁盘
db.save_to_disk("data.tsdb");

// 从磁盘加载
db.load_from_disk("data.tsdb");
```

### 2. 检查点 (Checkpoints)

检查点允许您在特定时间点保存数据库状态，并在以后恢复到该状态。

```cpp
// 创建检查点
db.create_checkpoint(1);

// 添加更多数据
db.add(timestamp2, value2);

// 创建第二个检查点
db.create_checkpoint(2);

// 恢复到检查点 1
db.restore_from_checkpoint(1);

// 列出所有检查点
auto checkpoints = db.list_checkpoints();
for (const auto& [id, metadata] : checkpoints) {
    std::cout << "检查点 #" << id 
              << " 有 " << metadata.at("data_count") << " 个数据点\n";
}

// 删除检查点
db.delete_checkpoint(1);
```

### 3. 自定义存储路径

```cpp
// 设置存储基路径
db.set_storage_path("/path/to/storage");

// 获取当前存储路径
std::string path = db.get_storage_path();
```

### 4. 存储统计

```cpp
// 获取存储统计信息
auto stats = db.get_storage_stats();
std::cout << "已写入: " << stats["bytes_written"] << " bytes\n";
std::cout << "已读取: " << stats["bytes_read"] << " bytes\n";
std::cout << "检查点数: " << stats["checkpoint_count"] << "\n";
```

## 文件格式

### 文件头结构

每个 `.tsdb` 文件都包含一个文件头：

```cpp
struct FileHeader {
    uint32_t magic_number;      // 魔数: 0x53544442 ("STDB")
    uint32_t format_version;    // 格式版本: 1
    uint64_t data_count;        // 数据点数量
    uint64_t checkpoint_id;     // 检查点 ID
    int64_t min_timestamp;      // 最小时间戳
    int64_t max_timestamp;      // 最大时间戳
    uint64_t index_offset;      // 索引偏移量
    uint64_t metadata_offset;   // 元数据偏移量
};
```

### 数据点结构

每个数据点以二进制格式存储：

- **Timestamp** (8 bytes): int64_t
- **Value Type** (1 byte): 标量或向量标志
- **Value Data**: 
  - 标量: 8 bytes (double)
  - 向量: 8 bytes (size) + size * 8 bytes (double array)
- **Tags**: 键值对数量 + 每个键值对 (长度 + 字符串)
- **Fields**: 键值对数量 + 每个键值对 (长度 + 字符串)

## 性能

基于测试结果：

| 操作 | 数据量 | 耗时 |
|------|--------|------|
| 保存 | 10,000 点 | ~3 ms |
| 加载 | 10,000 点 | ~7 ms |
| 检查点创建 | 10,000 点 | ~3 ms |

## 使用示例

### 完整示例

查看 `examples/persistence_example.cpp` 获取完整的使用示例。

编译并运行示例：

```bash
cd build
cmake ..
make persistence_example
./examples/persistence_example
```

### 基本工作流程

```cpp
#include "sage_tsdb/core/time_series_db.h"
#include <iostream>

int main() {
    sage_tsdb::TimeSeriesDB db;
    
    // 1. 添加数据
    for (int i = 0; i < 100; ++i) {
        db.add(1000 + i * 1000, 20.0 + i * 0.1, 
               {{"sensor", "temp_01"}});
    }
    
    // 2. 保存到磁盘
    if (db.save_to_disk("sensor_data.tsdb")) {
        std::cout << "数据保存成功\n";
    }
    
    // 3. 创建检查点
    db.create_checkpoint(1);
    
    // 4. 清空并重新加载
    db.clear();
    db.load_from_disk("sensor_data.tsdb");
    
    std::cout << "加载了 " << db.size() << " 个数据点\n";
    
    return 0;
}
```

## 存储目录结构

默认情况下，持久化文件存储在 `./sage_tsdb_data/` 目录下：

```
sage_tsdb_data/
├── sensor_data.tsdb          # 主数据文件
├── checkpoint_1.tsdb         # 检查点 1
├── checkpoint_2.tsdb         # 检查点 2
└── checkpoints.meta          # 检查点元数据
```

## API 参考

### TimeSeriesDB 持久化方法

#### save_to_disk()
```cpp
bool save_to_disk(const std::string& file_path);
```
保存所有数据到指定文件。

**参数:**
- `file_path`: 保存文件路径

**返回:** 成功返回 true，失败返回 false

#### load_from_disk()
```cpp
bool load_from_disk(const std::string& file_path, bool clear_existing = true);
```
从文件加载数据。

**参数:**
- `file_path`: 加载文件路径
- `clear_existing`: 是否在加载前清空现有数据（默认 true）

**返回:** 成功返回 true，失败返回 false

#### create_checkpoint()
```cpp
bool create_checkpoint(uint64_t checkpoint_id);
```
创建数据库检查点。

**参数:**
- `checkpoint_id`: 检查点 ID

**返回:** 成功返回 true，失败返回 false

#### restore_from_checkpoint()
```cpp
bool restore_from_checkpoint(uint64_t checkpoint_id, bool clear_existing = true);
```
从检查点恢复数据。

**参数:**
- `checkpoint_id`: 要恢复的检查点 ID
- `clear_existing`: 是否在恢复前清空现有数据（默认 true）

**返回:** 成功返回 true，失败返回 false

#### list_checkpoints()
```cpp
std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> list_checkpoints() const;
```
列出所有可用的检查点。

**返回:** 检查点 ID 和元数据的向量

#### delete_checkpoint()
```cpp
bool delete_checkpoint(uint64_t checkpoint_id);
```
删除指定检查点。

**参数:**
- `checkpoint_id`: 要删除的检查点 ID

**返回:** 成功返回 true，失败返回 false

#### set_storage_path()
```cpp
void set_storage_path(const std::string& path);
```
设置存储基路径。

**参数:**
- `path`: 存储目录路径

#### get_storage_path()
```cpp
std::string get_storage_path() const;
```
获取当前存储基路径。

**返回:** 存储目录路径

#### get_storage_stats()
```cpp
std::map<std::string, uint64_t> get_storage_stats() const;
```
获取存储统计信息。

**返回:** 包含以下键的统计信息：
- `bytes_written`: 已写入字节数
- `bytes_read`: 已读取字节数
- `checkpoint_count`: 检查点数量
- `compression_enabled`: 是否启用压缩（未来功能）

## 高级功能（规划中）

以下功能将在未来版本中实现：

- 🔜 数据压缩（Gorilla、Delta-of-Delta）
- 🔜 WAL（Write-Ahead Logging）
- 🔜 增量备份
- 🔜 异步 I/O
- 🔜 加密存储
- 🔜 数据分片

## 测试

运行持久化功能测试：

```bash
cd build
make test_storage_engine
./tests/test_storage_engine
```

或运行所有测试：

```bash
cd build
ctest
```

## 注意事项

1. **文件格式兼容性**: 当前版本为 1.0，未来版本将保持向后兼容
2. **并发访问**: 当前实现不支持多进程并发访问同一文件
3. **大文件处理**: 对于超大数据集，建议使用检查点功能进行分段存储
4. **磁盘空间**: 确保有足够的磁盘空间存储数据和检查点

## 错误处理

所有持久化操作都会返回布尔值指示成功或失败。失败时会在标准错误输出打印详细错误信息：

```cpp
if (!db.save_to_disk("data.tsdb")) {
    std::cerr << "保存失败，请检查磁盘空间和权限\n";
}
```

## 贡献

如果您发现 bug 或有功能建议，请在 GitHub 上提交 issue 或 pull request。

## 许可证

本项目采用与 sageTSDB 相同的许可证。
