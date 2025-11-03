#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/storage_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace sage_tsdb;

// 格式化时间戳显示
std::string format_timestamp(int64_t timestamp) {
    auto time = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(timestamp));
    std::time_t t = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::localtime(&t);
    
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buffer);
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "sageTSDB 数据持久化功能示例" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 创建数据库实例
    TimeSeriesDB db;
    
    // 设置存储路径
    db.set_storage_path("./example_storage");
    std::cout << "✓ 存储路径设置为: " << db.get_storage_path() << "\n" << std::endl;
    
    // 1. 添加时序数据
    std::cout << "1. 添加时序数据..." << std::endl;
    auto now = std::chrono::system_clock::now();
    auto base_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // 模拟传感器数据
    for (int i = 0; i < 100; ++i) {
        int64_t timestamp = base_time + i * 1000; // 每秒一个数据点
        double temperature = 20.0 + (i % 10) * 0.5; // 模拟温度变化
        
        Tags tags = {
            {"sensor_id", "temp_sensor_01"},
            {"location", "server_room_" + std::to_string(i % 3 + 1)},
            {"device_type", "temperature"}
        };
        
        Fields fields = {
            {"unit", "celsius"},
            {"accuracy", "±0.1°C"}
        };
        
        db.add(timestamp, temperature, tags, fields);
    }
    
    std::cout << "   添加了 " << db.size() << " 个数据点" << std::endl;
    std::cout << "   时间范围: " << format_timestamp(base_time) 
              << " ~ " << format_timestamp(base_time + 99000) << "\n" << std::endl;
    
    // 2. 保存到磁盘
    std::cout << "2. 保存数据到磁盘..." << std::endl;
    std::string save_path = "./example_storage/sensor_data.tsdb";
    
    auto start = std::chrono::high_resolution_clock::now();
    if (db.save_to_disk(save_path)) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start);
        std::cout << "   ✓ 成功保存到: " << save_path << std::endl;
        std::cout << "   耗时: " << duration.count() / 1000.0 << " ms" << std::endl;
        
        // 显示文件大小
        auto stats = db.get_storage_stats();
        std::cout << "   写入字节数: " << stats["bytes_written"] << " bytes" << "\n" << std::endl;
    }
    
    // 3. 创建检查点
    std::cout << "3. 创建检查点..." << std::endl;
    uint64_t checkpoint_id = 1;
    if (db.create_checkpoint(checkpoint_id)) {
        std::cout << "   ✓ 检查点 #" << checkpoint_id << " 创建成功\n" << std::endl;
    }
    
    // 4. 添加更多数据
    std::cout << "4. 添加更多数据..." << std::endl;
    for (int i = 100; i < 150; ++i) {
        int64_t timestamp = base_time + i * 1000;
        double temperature = 20.0 + (i % 10) * 0.5;
        
        Tags tags = {
            {"sensor_id", "temp_sensor_01"},
            {"location", "server_room_" + std::to_string(i % 3 + 1)}
        };
        
        db.add(timestamp, temperature, tags);
    }
    std::cout << "   现在有 " << db.size() << " 个数据点\n" << std::endl;
    
    // 5. 创建第二个检查点
    std::cout << "5. 创建第二个检查点..." << std::endl;
    checkpoint_id = 2;
    if (db.create_checkpoint(checkpoint_id)) {
        std::cout << "   ✓ 检查点 #" << checkpoint_id << " 创建成功\n" << std::endl;
    }
    
    // 6. 列出所有检查点
    std::cout << "6. 列出所有检查点..." << std::endl;
    auto checkpoints = db.list_checkpoints();
    std::cout << "   找到 " << checkpoints.size() << " 个检查点:" << std::endl;
    for (const auto& [id, metadata] : checkpoints) {
        std::cout << "   - 检查点 #" << id << ":" << std::endl;
        std::cout << "     数据点数: " << metadata.at("data_count") << std::endl;
        std::cout << "     创建时间: " << format_timestamp(metadata.at("timestamp")) << std::endl;
    }
    std::cout << std::endl;
    
    // 7. 从检查点恢复
    std::cout << "7. 从检查点 #1 恢复数据..." << std::endl;
    size_t size_before = db.size();
    if (db.restore_from_checkpoint(1)) {
        size_t size_after = db.size();
        std::cout << "   ✓ 恢复成功" << std::endl;
        std::cout << "   恢复前: " << size_before << " 个数据点" << std::endl;
        std::cout << "   恢复后: " << size_after << " 个数据点\n" << std::endl;
    }
    
    // 8. 查询数据验证
    std::cout << "8. 查询数据验证..." << std::endl;
    TimeRange range(base_time, base_time + 20000); // 前20秒
    auto results = db.query(range);
    std::cout << "   查询时间范围: " << format_timestamp(base_time) 
              << " ~ " << format_timestamp(base_time + 20000) << std::endl;
    std::cout << "   查询结果: " << results.size() << " 个数据点" << std::endl;
    
    // 显示前5个结果
    std::cout << "   前5个数据点:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(5), results.size()); ++i) {
        std::cout << "     [" << i << "] "
                  << "时间: " << format_timestamp(results[i].timestamp)
                  << ", 值: " << std::fixed << std::setprecision(2) 
                  << results[i].as_double() << "°C" << std::endl;
    }
    std::cout << std::endl;
    
    // 9. 清空数据库并从磁盘加载
    std::cout << "9. 测试从磁盘加载..." << std::endl;
    db.clear();
    std::cout << "   清空后: " << db.size() << " 个数据点" << std::endl;
    
    start = std::chrono::high_resolution_clock::now();
    if (db.load_from_disk(save_path)) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start);
        std::cout << "   ✓ 从磁盘加载成功" << std::endl;
        std::cout << "   加载后: " << db.size() << " 个数据点" << std::endl;
        std::cout << "   耗时: " << duration.count() / 1000.0 << " ms" << std::endl;
        
        auto stats = db.get_storage_stats();
        std::cout << "   读取字节数: " << stats["bytes_read"] << " bytes" << "\n" << std::endl;
    }
    
    // 10. 统计信息
    std::cout << "10. 数据库统计信息..." << std::endl;
    auto db_stats = db.get_stats();
    std::cout << "   总数据点数: " << db_stats["size"] << std::endl;
    std::cout << "   查询次数: " << db_stats["query_count"] << std::endl;
    std::cout << "   写入次数: " << db_stats["write_count"] << std::endl;
    
    auto storage_stats = db.get_storage_stats();
    std::cout << "   存储统计:" << std::endl;
    std::cout << "     已写入: " << storage_stats["bytes_written"] << " bytes" << std::endl;
    std::cout << "     已读取: " << storage_stats["bytes_read"] << " bytes" << std::endl;
    std::cout << "     检查点数: " << storage_stats["checkpoint_count"] << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "示例完成！" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
