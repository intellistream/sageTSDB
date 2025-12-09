#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <functional>
#include <iostream>
#include <algorithm>
#include "sage_tsdb/core/time_series_data.h"

namespace sage_tsdb {
namespace utils {

/**
 * @brief PECJ 数据格式的元组结构
 * 
 * 对应 PECJ 数据集的 CSV 格式: key,value,eventTime,arrivalTime
 */
struct PECJTuple {
    uint64_t key;           // Join key
    double value;           // Tuple value
    uint64_t eventTime;     // Event timestamp (microseconds)
    uint64_t arrivalTime;   // Arrival timestamp (microseconds)
    
    PECJTuple() : key(0), value(0.0), eventTime(0), arrivalTime(0) {}
    
    PECJTuple(uint64_t k, double v, uint64_t et, uint64_t at)
        : key(k), value(v), eventTime(et), arrivalTime(at) {}
};

/**
 * @brief CSV记录结构（兼容旧API）
 * 
 * Note: PECJ CSV files use different time units:
 * - eventTime: typically in microseconds (us) in the CSV
 * - arrivalTime: typically in microseconds (us) in the CSV
 */
struct CSVRecord {
    int64_t key;
    double value;
    int64_t event_time;     // Time in original CSV unit
    int64_t arrival_time;   // Time in original CSV unit
};

/**
 * @brief CSV 数据加载器，用于加载 PECJ 数据集
 * 
 * 支持两种模式：
 * 1. 批量加载：一次性加载所有数据
 * 2. 流式加载：逐行读取并通过回调函数处理
 * 
 * 数据格式：key,value,eventTime,arrivalTime
 */
class CSVDataLoader {
public:
    /**
     * @brief 构造函数
     * @param filepath CSV 文件路径
     * @param skip_header 是否跳过第一行（表头）
     */
    explicit CSVDataLoader(const std::string& filepath, bool skip_header = true)
        : filepath_(filepath), skip_header_(skip_header) {}
    
    /**
     * @brief 批量加载所有数据
     * @return 所有元组的向量
     * @throw std::runtime_error 文件打开失败或解析错误
     */
    std::vector<PECJTuple> loadAll() {
        std::vector<PECJTuple> tuples;
        std::ifstream file(filepath_);
        
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath_);
        }
        
        std::string line;
        int line_number = 0;
        
        // 跳过表头
        if (skip_header_ && std::getline(file, line)) {
            line_number++;
        }
        
        while (std::getline(file, line)) {
            line_number++;
            if (line.empty() || line[0] == '#') {
                continue;  // 跳过空行和注释
            }
            
            try {
                tuples.push_back(parseLine(line));
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "Parse error at line " + std::to_string(line_number) + 
                    " in file " + filepath_ + ": " + e.what()
                );
            }
        }
        
        return tuples;
    }
    
    /**
     * @brief 流式加载数据，每读取一行就调用回调函数
     * @param callback 处理每个元组的回调函数
     * @param max_tuples 最多读取的元组数量（0 表示不限制）
     * @return 实际读取的元组数量
     */
    size_t loadStream(std::function<void(const PECJTuple&)> callback, size_t max_tuples = 0) {
        std::ifstream file(filepath_);
        
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath_);
        }
        
        std::string line;
        int line_number = 0;
        size_t tuple_count = 0;
        
        // 跳过表头
        if (skip_header_ && std::getline(file, line)) {
            line_number++;
        }
        
        while (std::getline(file, line)) {
            line_number++;
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            try {
                PECJTuple tuple = parseLine(line);
                callback(tuple);
                tuple_count++;
                
                if (max_tuples > 0 && tuple_count >= max_tuples) {
                    break;
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "Parse error at line " + std::to_string(line_number) + 
                    " in file " + filepath_ + ": " + e.what()
                );
            }
        }
        
        return tuple_count;
    }
    
    /**
     * @brief 按到达时间顺序加载数据（用于重放）
     * @param max_tuples 最多读取的元组数量（0 表示不限制）
     * @return 按到达时间排序的元组向量
     */
    std::vector<PECJTuple> loadSortedByArrival(size_t max_tuples = 0) {
        auto tuples = loadAll();
        
        // 按到达时间排序
        std::sort(tuples.begin(), tuples.end(), 
            [](const PECJTuple& a, const PECJTuple& b) {
                return a.arrivalTime < b.arrivalTime;
            });
        
        // 限制数量
        if (max_tuples > 0 && tuples.size() > max_tuples) {
            tuples.resize(max_tuples);
        }
        
        return tuples;
    }
    
    /**
     * @brief 获取文件路径
     */
    const std::string& getFilePath() const { return filepath_; }

    // ========== 兼容性静态方法（用于旧版本API） ==========
    
    /**
     * @brief Load data from a PECJ-format CSV file (静态方法，兼容旧API)
     * @param filename Path to CSV file
     * @param time_unit_multiplier Multiplier to convert time to microseconds (e.g., 1000 for ms->us, 1 for us->us)
     * @return Vector of CSV records, empty on error
     */
    static std::vector<CSVRecord> loadFromFile(const std::string& filename, int64_t time_unit_multiplier = 1) {
        std::vector<CSVRecord> records;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open file: " << filename << std::endl;
            return records;
        }
        
        std::string line;
        bool is_header = true;
        size_t line_num = 0;
        
        // Column indices (default for PECJ format)
        int idx_key = 0, idx_value = 1, idx_event_time = 2, idx_arrival_time = 3;
        
        while (std::getline(file, line)) {
            line_num++;
            
            // Remove trailing \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            if (line.empty()) continue;
            
            std::vector<std::string> tokens = split(line, ',');
            
            if (is_header) {
                // Parse header to find column indices
                for (size_t i = 0; i < tokens.size(); i++) {
                    if (tokens[i] == "key") idx_key = i;
                    else if (tokens[i] == "value") idx_value = i;
                    else if (tokens[i] == "eventTime") idx_event_time = i;
                    else if (tokens[i] == "arrivalTime" || tokens[i] == "arriveTime") {
                        idx_arrival_time = i;
                    }
                }
                is_header = false;
                continue;
            }
            
            // Parse data row
            if (tokens.size() < 4) {
                std::cerr << "⚠ Skipping invalid line " << line_num << ": " << line << std::endl;
                continue;
            }
            
            try {
                CSVRecord record;
                record.key = std::stoll(tokens[idx_key]);
                record.value = std::stod(tokens[idx_value]);
                // Apply time unit conversion (e.g., milliseconds to microseconds)
                record.event_time = static_cast<int64_t>(std::stod(tokens[idx_event_time]) * time_unit_multiplier);
                record.arrival_time = static_cast<int64_t>(std::stod(tokens[idx_arrival_time]) * time_unit_multiplier);
                records.push_back(record);
            } catch (const std::exception& e) {
                std::cerr << "⚠ Error parsing line " << line_num << ": " << e.what() << std::endl;
            }
        }
        
        file.close();
        std::cout << "✓ Loaded " << records.size() << " records from " << filename << std::endl;
        return records;
    }
    
    /**
     * @brief Convert CSV record to TimeSeriesData (静态方法，兼容旧API)
     * @param record CSV record
     * @param stream_name Stream identifier ("S" or "R")
     * @return TimeSeriesData object
     */
    static TimeSeriesData toTimeSeriesData(const CSVRecord& record, const std::string& stream_name) {
        TimeSeriesData data;
        data.timestamp = record.event_time;  // Use event_time as timestamp
        data.tags["stream"] = stream_name;
        data.tags["key"] = std::to_string(record.key);
        data.fields["value"] = std::to_string(record.value);
        data.fields["arrival_time"] = std::to_string(record.arrival_time);
        return data;
    }
    
    /**
     * @brief Get statistics about loaded data (静态方法，兼容旧API)
     */
    static void printStatistics(const std::vector<CSVRecord>& records, const std::string& name) {
        if (records.empty()) {
            std::cout << "[" << name << "] No data\n";
            return;
        }
        
        int64_t min_event_time = records[0].event_time;
        int64_t max_event_time = records[0].event_time;
        int64_t min_key = records[0].key;
        int64_t max_key = records[0].key;
        
        for (const auto& r : records) {
            min_event_time = std::min(min_event_time, r.event_time);
            max_event_time = std::max(max_event_time, r.event_time);
            min_key = std::min(min_key, r.key);
            max_key = std::max(max_key, r.key);
        }
        
        std::cout << "\n[" << name << " Statistics]\n";
        std::cout << "  Records           : " << records.size() << "\n";
        std::cout << "  Time Range        : [" << min_event_time << ", " << max_event_time << "] us\n";
        std::cout << "  Duration          : " << (max_event_time - min_event_time) / 1000.0 << " ms\n";
        std::cout << "  Key Range         : [" << min_key << ", " << max_key << "]\n";
    }

private:
    /**
     * @brief 解析一行 CSV 数据
     * @param line CSV 行字符串
     * @return 解析后的 PECJTuple
     */
    PECJTuple parseLine(const std::string& line) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        // 分割逗号
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() != 4) {
            throw std::runtime_error(
                "Invalid CSV format. Expected 4 columns (key,value,eventTime,arrivalTime), got " + 
                std::to_string(tokens.size())
            );
        }
        
        try {
            PECJTuple tuple;
            tuple.key = std::stoull(tokens[0]);
            tuple.value = std::stod(tokens[1]);
            tuple.eventTime = std::stoull(tokens[2]);
            tuple.arrivalTime = std::stoull(tokens[3]);
            return tuple;
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to parse numeric values: " + std::string(e.what()));
        }
    }
    
    /**
     * @brief Split string by delimiter (静态辅助函数)
     */
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        
        return tokens;
    }
    
    std::string filepath_;
    bool skip_header_;
};

}  // namespace utils
}  // namespace sage_tsdb
