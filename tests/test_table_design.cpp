#include <gtest/gtest.h>
#include "sage_tsdb/core/stream_table.h"
#include "sage_tsdb/core/join_result_table.h"
#include "sage_tsdb/core/table_manager.h"

using namespace sage_tsdb;

// ========== StreamTable 测试 ==========

class StreamTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        TableConfig config;
        config.memtable_size_bytes = 1024 * 1024; // 1MB
        config.enable_timestamp_index = true;
        table = std::make_unique<StreamTable>("test_stream", config);
    }
    
    std::unique_ptr<StreamTable> table;
};

TEST_F(StreamTableTest, InsertAndQuery) {
    // 插入数据
    TimeSeriesData data1(1000, 10.5);
    data1.tags["symbol"] = "AAPL";
    
    TimeSeriesData data2(2000, 20.3);
    data2.tags["symbol"] = "GOOGL";
    
    size_t idx1 = table->insert(data1);
    size_t idx2 = table->insert(data2);
    
    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, 1);
    EXPECT_EQ(table->size(), 2);
    
    // 查询所有数据
    TimeRange range(0, 3000);
    auto results = table->query(range);
    
    EXPECT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].timestamp, 1000);
    EXPECT_EQ(results[1].timestamp, 2000);
}

TEST_F(StreamTableTest, BatchInsert) {
    std::vector<TimeSeriesData> batch;
    for (int i = 0; i < 100; i++) {
        TimeSeriesData data(i * 100, static_cast<double>(i));
        data.tags["id"] = std::to_string(i);
        batch.push_back(data);
    }
    
    auto indices = table->insertBatch(batch);
    
    EXPECT_EQ(indices.size(), 100);
    EXPECT_EQ(table->size(), 100);
}

TEST_F(StreamTableTest, QueryWithTimeRange) {
    // 插入 10 条数据
    for (int i = 0; i < 10; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        table->insert(data);
    }
    
    // 查询 [2000, 5000] 范围
    TimeRange range(2000, 5000);
    auto results = table->query(range);
    
    EXPECT_EQ(results.size(), 4); // 2000, 3000, 4000, 5000
    EXPECT_EQ(results[0].timestamp, 2000);
    EXPECT_EQ(results[3].timestamp, 5000);
}

TEST_F(StreamTableTest, QueryWithTags) {
    // 插入带不同标签的数据
    for (int i = 0; i < 5; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        data.tags["symbol"] = (i % 2 == 0) ? "AAPL" : "GOOGL";
        table->insert(data);
    }
    
    // 查询 AAPL 数据
    TimeRange range(0, 10000);
    Tags filter_tags = {{"symbol", "AAPL"}};
    auto results = table->query(range, filter_tags);
    
    EXPECT_EQ(results.size(), 3); // 索引 0, 2, 4
}

TEST_F(StreamTableTest, Count) {
    for (int i = 0; i < 20; i++) {
        TimeSeriesData data(i * 100, static_cast<double>(i));
        table->insert(data);
    }
    
    TimeRange range(500, 1500);
    size_t count = table->count(range);
    
    EXPECT_EQ(count, 11); // 500-1500: 500, 600, ..., 1500
}

TEST_F(StreamTableTest, QueryLatest) {
    for (int i = 0; i < 10; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        table->insert(data);
    }
    
    auto latest = table->queryLatest(3);
    
    EXPECT_EQ(latest.size(), 3);
    EXPECT_EQ(latest[0].timestamp, 9000); // 最新的
    EXPECT_EQ(latest[2].timestamp, 7000);
}

TEST_F(StreamTableTest, CreateIndex) {
    EXPECT_TRUE(table->createIndex("symbol"));
    EXPECT_FALSE(table->createIndex("symbol")); // 重复创建
    
    auto indexes = table->listIndexes();
    EXPECT_GE(indexes.size(), 1);
}

TEST_F(StreamTableTest, Clear) {
    for (int i = 0; i < 10; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        table->insert(data);
    }
    
    EXPECT_EQ(table->size(), 10);
    
    table->clear();
    
    EXPECT_EQ(table->size(), 0);
    EXPECT_TRUE(table->empty());
}

// ========== JoinResultTable 测试 ==========

class JoinResultTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        table = std::make_unique<JoinResultTable>("test_join_results");
    }
    
    std::unique_ptr<JoinResultTable> table;
};

TEST_F(JoinResultTableTest, InsertAndQuery) {
    JoinResultTable::JoinRecord record;
    record.window_id = 1;
    record.timestamp = 1000;
    record.join_count = 42;
    record.selectivity = 0.15;
    record.metrics.computation_time_ms = 5.2;
    record.metrics.algorithm_type = "IAWJ";
    
    size_t idx = table->insertJoinResult(record);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(table->size(), 1);
    
    // 按窗口查询
    auto results = table->queryByWindow(1);
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].window_id, 1);
    EXPECT_EQ(results[0].join_count, 42);
}

TEST_F(JoinResultTableTest, MultipleWindows) {
    // 插入多个窗口的结果
    for (int i = 1; i <= 5; i++) {
        JoinResultTable::JoinRecord record;
        record.window_id = i;
        record.timestamp = i * 1000;
        record.join_count = i * 10;
        record.metrics.computation_time_ms = 5.0 + i;
        
        table->insertJoinResult(record);
    }
    
    EXPECT_EQ(table->size(), 5);
    
    // 查询特定窗口
    auto win3 = table->queryByWindow(3);
    EXPECT_EQ(win3.size(), 1);
    EXPECT_EQ(win3[0].join_count, 30);
}

TEST_F(JoinResultTableTest, QueryByTimeRange) {
    for (int i = 1; i <= 10; i++) {
        JoinResultTable::JoinRecord record;
        record.window_id = i;
        record.timestamp = i * 1000;
        record.join_count = i;
        
        table->insertJoinResult(record);
    }
    
    TimeRange range(3000, 7000);
    auto results = table->queryByTimeRange(range);
    
    EXPECT_EQ(results.size(), 5); // windows 3-7
}

TEST_F(JoinResultTableTest, QueryLatest) {
    for (int i = 1; i <= 10; i++) {
        JoinResultTable::JoinRecord record;
        record.window_id = i;
        record.timestamp = i * 1000;
        record.join_count = i;
        
        table->insertJoinResult(record);
    }
    
    auto latest = table->queryLatest(3);
    
    EXPECT_EQ(latest.size(), 3);
    EXPECT_EQ(latest[0].window_id, 10); // 最新的
}

TEST_F(JoinResultTableTest, AggregateStats) {
    for (int i = 1; i <= 5; i++) {
        JoinResultTable::JoinRecord record;
        record.window_id = i;
        record.timestamp = i * 1000;
        record.join_count = i * 10;
        record.selectivity = 0.1 * i;
        record.metrics.computation_time_ms = 5.0 + i;
        record.metrics.used_aqp = (i == 3 || i == 5);
        
        table->insertJoinResult(record);
    }
    
    TimeRange range(0, 10000);
    auto stats = table->queryAggregateStats(range);
    
    EXPECT_EQ(stats.total_windows, 5);
    EXPECT_EQ(stats.total_joins, 10 + 20 + 30 + 40 + 50); // 150
    EXPECT_DOUBLE_EQ(stats.avg_join_count, 30.0);
    EXPECT_EQ(stats.aqp_usage_count, 2);
}

TEST_F(JoinResultTableTest, InsertSimpleResult) {
    JoinResultTable::JoinRecord::ComputeMetrics metrics;
    metrics.computation_time_ms = 4.5;
    metrics.memory_used_bytes = 1024 * 1024;
    metrics.threads_used = 4;
    
    size_t idx = table->insertSimpleResult(1, 1000, 100, metrics);
    
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(table->size(), 1);
}

// ========== TableManager 测试 ==========

class TableManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<TableManager>("/tmp/test_sage_tsdb");
    }
    
    void TearDown() override {
        manager->dropAllTables();
    }
    
    std::unique_ptr<TableManager> manager;
};

TEST_F(TableManagerTest, CreateAndGetStreamTable) {
    EXPECT_TRUE(manager->createStreamTable("test_stream"));
    EXPECT_TRUE(manager->hasTable("test_stream"));
    
    auto table = manager->getStreamTable("test_stream");
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(table->getName(), "test_stream");
}

TEST_F(TableManagerTest, CreateAndGetJoinResultTable) {
    EXPECT_TRUE(manager->createJoinResultTable("test_join"));
    EXPECT_TRUE(manager->hasTable("test_join"));
    
    auto table = manager->getJoinResultTable("test_join");
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(table->getName(), "test_join");
}

TEST_F(TableManagerTest, CreatePECJTables) {
    EXPECT_TRUE(manager->createPECJTables("query1_"));
    
    EXPECT_TRUE(manager->hasTable("query1_stream_s"));
    EXPECT_TRUE(manager->hasTable("query1_stream_r"));
    EXPECT_TRUE(manager->hasTable("query1_join_results"));
    
    EXPECT_EQ(manager->getTableCount(), 3);
}

TEST_F(TableManagerTest, ListTables) {
    manager->createStreamTable("stream1");
    manager->createStreamTable("stream2");
    manager->createJoinResultTable("join1");
    
    auto all_tables = manager->listTables();
    EXPECT_EQ(all_tables.size(), 3);
    
    auto stream_tables = manager->listTablesByType(TableManager::TableType::Stream);
    EXPECT_EQ(stream_tables.size(), 2);
    
    auto join_tables = manager->listTablesByType(TableManager::TableType::JoinResult);
    EXPECT_EQ(join_tables.size(), 1);
}

TEST_F(TableManagerTest, DropTable) {
    manager->createStreamTable("temp_table");
    EXPECT_TRUE(manager->hasTable("temp_table"));
    
    EXPECT_TRUE(manager->dropTable("temp_table"));
    EXPECT_FALSE(manager->hasTable("temp_table"));
    
    EXPECT_FALSE(manager->dropTable("nonexistent")); // 不存在的表
}

TEST_F(TableManagerTest, ClearTable) {
    manager->createStreamTable("test_stream");
    auto table = manager->getStreamTable("test_stream");
    
    // 插入数据
    for (int i = 0; i < 10; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        table->insert(data);
    }
    
    EXPECT_EQ(table->size(), 10);
    
    // 清空表
    EXPECT_TRUE(manager->clearTable("test_stream"));
    EXPECT_EQ(table->size(), 0);
}

TEST_F(TableManagerTest, BatchOperations) {
    manager->createStreamTable("stream1");
    manager->createStreamTable("stream2");
    
    // 批量插入
    std::map<std::string, std::vector<TimeSeriesData>> batch_data;
    
    for (int i = 0; i < 5; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        batch_data["stream1"].push_back(data);
        batch_data["stream2"].push_back(data);
    }
    
    auto indices = manager->insertBatchToTables(batch_data);
    
    EXPECT_EQ(indices["stream1"].size(), 5);
    EXPECT_EQ(indices["stream2"].size(), 5);
    
    // 批量查询
    std::map<std::string, TimeRange> queries;
    queries["stream1"] = TimeRange(0, 5000);
    queries["stream2"] = TimeRange(0, 5000);
    
    auto results = manager->queryBatchFromTables(queries);
    
    EXPECT_EQ(results["stream1"].size(), 5);
    EXPECT_EQ(results["stream2"].size(), 5);
}

TEST_F(TableManagerTest, GlobalStats) {
    manager->createPECJTables();
    
    auto stream_s = manager->getStreamTable("stream_s");
    auto stream_r = manager->getStreamTable("stream_r");
    
    // 插入数据
    for (int i = 0; i < 100; i++) {
        TimeSeriesData data(i * 1000, static_cast<double>(i));
        stream_s->insert(data);
        stream_r->insert(data);
    }
    
    auto stats = manager->getGlobalStats();
    
    EXPECT_EQ(stats.total_tables, 3);
    EXPECT_EQ(stats.total_records, 200); // 100 + 100
    EXPECT_EQ(stats.table_sizes.size(), 3);
}

// ========== 集成测试 ==========

TEST(IntegrationTest, EndToEndWorkflow) {
    TableManager manager("/tmp/integration_test");
    
    // 创建表
    manager.createPECJTables("e2e_");
    
    auto stream_s = manager.getStreamTable("e2e_stream_s");
    auto stream_r = manager.getStreamTable("e2e_stream_r");
    auto join_results = manager.getJoinResultTable("e2e_join_results");
    
    // 写入数据
    for (int i = 0; i < 50; i++) {
        TimeSeriesData s_data(i * 100, static_cast<double>(i));
        s_data.tags["key"] = std::to_string(i % 10);
        stream_s->insert(s_data);
        
        TimeSeriesData r_data(i * 100 + 50, static_cast<double>(i));
        r_data.tags["key"] = std::to_string(i % 10);
        stream_r->insert(r_data);
    }
    
    // 查询窗口数据
    TimeRange window(0, 1000);
    auto s_data = stream_s->query(window);
    auto r_data = stream_r->query(window);
    
    // 模拟 Join 计算
    size_t join_count = s_data.size() * r_data.size(); // 笛卡尔积
    
    JoinResultTable::JoinRecord result;
    result.window_id = 1;
    result.timestamp = window.end_time;
    result.join_count = join_count;
    result.metrics.computation_time_ms = 10.5;
    result.metrics.algorithm_type = "IAWJ";
    
    join_results->insertJoinResult(result);
    
    // 验证结果
    auto stored_results = join_results->queryByWindow(1);
    ASSERT_EQ(stored_results.size(), 1);
    EXPECT_EQ(stored_results[0].join_count, join_count);
    
    // 清理
    manager.dropAllTables();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
