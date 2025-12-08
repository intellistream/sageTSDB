/**
 * @file csv_data_loader.h
 * @brief CSV Data Loader for PECJ-format datasets
 * 
 * This utility loads PECJ benchmark datasets (CSV format) into sageTSDB tables.
 * CSV format: key,value,eventTime,arrivalTime
 * 
 * @version 1.0
 * @date 2024-12-08
 */

#ifndef SAGE_TSDB_CSV_DATA_LOADER_H
#define SAGE_TSDB_CSV_DATA_LOADER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include "sage_tsdb/core/time_series_data.h"

namespace sage_tsdb {
namespace utils {

/**
 * @brief Structure to hold a single CSV record
 */
struct CSVRecord {
    int64_t key;
    double value;
    int64_t event_time;     // microseconds
    int64_t arrival_time;   // microseconds
};

/**
 * @brief CSV Data Loader for PECJ datasets
 */
class CSVDataLoader {
public:
    /**
     * @brief Load data from a PECJ-format CSV file
     * @param filename Path to CSV file
     * @return Vector of CSV records, empty on error
     */
    static std::vector<CSVRecord> loadFromFile(const std::string& filename) {
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
                record.event_time = std::stoll(tokens[idx_event_time]);
                record.arrival_time = std::stoll(tokens[idx_arrival_time]);
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
     * @brief Convert CSV record to TimeSeriesData
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
     * @brief Get statistics about loaded data
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
     * @brief Split string by delimiter
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
};

} // namespace utils
} // namespace sage_tsdb

#endif // SAGE_TSDB_CSV_DATA_LOADER_H
