#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <functional>

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
    
    std::string filepath_;
    bool skip_header_;
};

}  // namespace utils
}  // namespace sage_tsdb
