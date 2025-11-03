#include "sage_tsdb/core/storage_engine.h"
#include "sage_tsdb/core/time_series_db.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

namespace sage_tsdb {
namespace test {

class StorageEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "./test_storage_data";
        // Clean up test directory
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
        fs::create_directories(test_dir_);
        
        engine_ = std::make_unique<StorageEngine>(test_dir_);
    }
    
    void TearDown() override {
        engine_.reset();
        // Clean up test directory
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }
    
    std::vector<TimeSeriesData> generate_test_data(size_t count) {
        std::vector<TimeSeriesData> data;
        auto now = std::chrono::system_clock::now();
        auto base_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        for (size_t i = 0; i < count; ++i) {
            TimeSeriesData point;
            point.timestamp = base_time + i * 1000; // 1 second apart
            point.value = 100.0 + i;
            point.tags = {{"sensor", "temp_" + std::to_string(i % 3)},
                         {"location", "room_" + std::to_string(i % 2)}};
            point.fields = {{"unit", "celsius"}};
            data.push_back(point);
        }
        
        return data;
    }
    
    std::string test_dir_;
    std::unique_ptr<StorageEngine> engine_;
};

TEST_F(StorageEngineTest, SaveAndLoad) {
    // Generate test data
    auto test_data = generate_test_data(100);
    std::string file_path = test_dir_ + "/test_data.tsdb";
    
    // Save data
    ASSERT_TRUE(engine_->save(test_data, file_path));
    ASSERT_TRUE(fs::exists(file_path));
    
    // Load data
    auto loaded_data = engine_->load(file_path);
    ASSERT_EQ(loaded_data.size(), test_data.size());
    
    // Verify data integrity
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_EQ(loaded_data[i].timestamp, test_data[i].timestamp);
        EXPECT_DOUBLE_EQ(loaded_data[i].as_double(), test_data[i].as_double());
        EXPECT_EQ(loaded_data[i].tags, test_data[i].tags);
        EXPECT_EQ(loaded_data[i].fields, test_data[i].fields);
    }
}

TEST_F(StorageEngineTest, SaveEmptyData) {
    std::vector<TimeSeriesData> empty_data;
    std::string file_path = test_dir_ + "/empty.tsdb";
    
    // Should succeed for empty data
    ASSERT_TRUE(engine_->save(empty_data, file_path));
}

TEST_F(StorageEngineTest, LoadNonExistentFile) {
    std::string file_path = test_dir_ + "/nonexistent.tsdb";
    
    // Should return empty vector
    auto loaded_data = engine_->load(file_path);
    EXPECT_TRUE(loaded_data.empty());
}

TEST_F(StorageEngineTest, VectorValueSupport) {
    std::vector<TimeSeriesData> test_data;
    
    TimeSeriesData point;
    point.timestamp = 1000;
    point.value = std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0};
    point.tags = {{"type", "vector"}};
    test_data.push_back(point);
    
    std::string file_path = test_dir_ + "/vector_data.tsdb";
    
    // Save and load
    ASSERT_TRUE(engine_->save(test_data, file_path));
    auto loaded_data = engine_->load(file_path);
    
    ASSERT_EQ(loaded_data.size(), 1);
    EXPECT_TRUE(loaded_data[0].is_array());
    
    auto loaded_vec = loaded_data[0].as_vector();
    auto original_vec = std::get<std::vector<double>>(point.value);
    ASSERT_EQ(loaded_vec.size(), original_vec.size());
    
    for (size_t i = 0; i < loaded_vec.size(); ++i) {
        EXPECT_DOUBLE_EQ(loaded_vec[i], original_vec[i]);
    }
}

TEST_F(StorageEngineTest, CreateAndRestoreCheckpoint) {
    auto test_data = generate_test_data(50);
    uint64_t checkpoint_id = 1;
    
    // Create checkpoint
    ASSERT_TRUE(engine_->create_checkpoint(test_data, checkpoint_id));
    
    // Restore checkpoint
    auto restored_data = engine_->restore_checkpoint(checkpoint_id);
    ASSERT_EQ(restored_data.size(), test_data.size());
    
    // Verify data
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_EQ(restored_data[i].timestamp, test_data[i].timestamp);
        EXPECT_DOUBLE_EQ(restored_data[i].as_double(), test_data[i].as_double());
    }
}

TEST_F(StorageEngineTest, MultipleCheckpoints) {
    // Create multiple checkpoints
    for (uint64_t i = 1; i <= 3; ++i) {
        auto test_data = generate_test_data(10 * i);
        ASSERT_TRUE(engine_->create_checkpoint(test_data, i));
    }
    
    // List checkpoints
    auto checkpoints = engine_->list_checkpoints();
    ASSERT_EQ(checkpoints.size(), 3);
    
    // Verify checkpoint IDs are sorted
    for (size_t i = 0; i < checkpoints.size(); ++i) {
        EXPECT_EQ(checkpoints[i].checkpoint_id, i + 1);
    }
    
    // Restore each checkpoint
    for (uint64_t i = 1; i <= 3; ++i) {
        auto data = engine_->restore_checkpoint(i);
        EXPECT_EQ(data.size(), 10 * i);
    }
}

TEST_F(StorageEngineTest, DeleteCheckpoint) {
    auto test_data = generate_test_data(20);
    uint64_t checkpoint_id = 5;
    
    // Create checkpoint
    ASSERT_TRUE(engine_->create_checkpoint(test_data, checkpoint_id));
    
    // Verify checkpoint exists
    auto checkpoints = engine_->list_checkpoints();
    ASSERT_EQ(checkpoints.size(), 1);
    
    // Delete checkpoint
    ASSERT_TRUE(engine_->delete_checkpoint(checkpoint_id));
    
    // Verify checkpoint is deleted
    checkpoints = engine_->list_checkpoints();
    EXPECT_EQ(checkpoints.size(), 0);
    
    // Try to restore deleted checkpoint
    auto data = engine_->restore_checkpoint(checkpoint_id);
    EXPECT_TRUE(data.empty());
}

TEST_F(StorageEngineTest, AppendData) {
    auto initial_data = generate_test_data(30);
    auto append_data = generate_test_data(20);
    
    std::string file_path = test_dir_ + "/append_test.tsdb";
    
    // Save initial data
    ASSERT_TRUE(engine_->save(initial_data, file_path));
    
    // Append more data
    ASSERT_TRUE(engine_->append(append_data, file_path));
    
    // Load and verify
    auto loaded_data = engine_->load(file_path);
    EXPECT_EQ(loaded_data.size(), initial_data.size() + append_data.size());
}

TEST_F(StorageEngineTest, Statistics) {
    auto test_data = generate_test_data(100);
    std::string file_path = test_dir_ + "/stats_test.tsdb";
    
    // Save data
    ASSERT_TRUE(engine_->save(test_data, file_path));
    
    // Load data
    auto loaded_data = engine_->load(file_path);
    
    // Get statistics
    auto stats = engine_->get_statistics();
    EXPECT_GT(stats["bytes_written"], 0);
    EXPECT_GT(stats["bytes_read"], 0);
}

TEST_F(StorageEngineTest, LargeDataset) {
    // Test with larger dataset
    auto test_data = generate_test_data(10000);
    std::string file_path = test_dir_ + "/large_data.tsdb";
    
    // Save
    auto start = std::chrono::high_resolution_clock::now();
    ASSERT_TRUE(engine_->save(test_data, file_path));
    auto save_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    
    std::cout << "Saved 10000 points in " << save_duration.count() << " ms" << std::endl;
    
    // Load
    start = std::chrono::high_resolution_clock::now();
    auto loaded_data = engine_->load(file_path);
    auto load_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    
    std::cout << "Loaded 10000 points in " << load_duration.count() << " ms" << std::endl;
    
    ASSERT_EQ(loaded_data.size(), test_data.size());
}

TEST_F(StorageEngineTest, ComplexTagsAndFields) {
    TimeSeriesData point;
    point.timestamp = 1000;
    point.value = 42.5;
    
    // Complex tags with special characters
    point.tags = {
        {"sensor_name", "temp-sensor-01"},
        {"location", "Building A, Floor 3, Room 301"},
        {"environment", "production"},
        {"version", "v2.3.1"}
    };
    
    // Complex fields
    point.fields = {
        {"unit", "celsius"},
        {"accuracy", "±0.5°C"},
        {"calibration_date", "2025-01-15"},
        {"manufacturer", "SensorCorp Inc."}
    };
    
    std::vector<TimeSeriesData> test_data = {point};
    std::string file_path = test_dir_ + "/complex_metadata.tsdb";
    
    // Save and load
    ASSERT_TRUE(engine_->save(test_data, file_path));
    auto loaded_data = engine_->load(file_path);
    
    ASSERT_EQ(loaded_data.size(), 1);
    EXPECT_EQ(loaded_data[0].tags, point.tags);
    EXPECT_EQ(loaded_data[0].fields, point.fields);
}

// Integration test with TimeSeriesDB
class TimeSeriesDBPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "./test_db_storage";
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
        fs::create_directories(test_dir_);
        
        db_ = std::make_unique<TimeSeriesDB>();
        db_->set_storage_path(test_dir_);
    }
    
    void TearDown() override {
        db_.reset();
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<TimeSeriesDB> db_;
};

TEST_F(TimeSeriesDBPersistenceTest, SaveAndLoadDatabase) {
    // Add test data to database
    for (int i = 0; i < 100; ++i) {
        db_->add(1000 + i * 1000, 100.0 + i,
                {{"sensor", "temp"}, {"id", std::to_string(i)}});
    }
    
    EXPECT_EQ(db_->size(), 100);
    
    // Save to disk
    std::string file_path = test_dir_ + "/db_snapshot.tsdb";
    ASSERT_TRUE(db_->save_to_disk(file_path));
    
    // Clear database
    db_->clear();
    EXPECT_EQ(db_->size(), 0);
    
    // Load from disk
    ASSERT_TRUE(db_->load_from_disk(file_path));
    EXPECT_EQ(db_->size(), 100);
    
    // Query and verify
    TimeRange range(1000, 100000);
    auto results = db_->query(range);
    EXPECT_EQ(results.size(), 100);
}

TEST_F(TimeSeriesDBPersistenceTest, CheckpointAndRestore) {
    // Add initial data
    for (int i = 0; i < 50; ++i) {
        db_->add(1000 + i * 1000, 50.0 + i);
    }
    
    // Create checkpoint 1
    ASSERT_TRUE(db_->create_checkpoint(1));
    
    // Add more data
    for (int i = 50; i < 100; ++i) {
        db_->add(1000 + i * 1000, 50.0 + i);
    }
    
    // Create checkpoint 2
    ASSERT_TRUE(db_->create_checkpoint(2));
    
    EXPECT_EQ(db_->size(), 100);
    
    // List checkpoints
    auto checkpoints = db_->list_checkpoints();
    ASSERT_EQ(checkpoints.size(), 2);
    
    // Restore checkpoint 1
    ASSERT_TRUE(db_->restore_from_checkpoint(1));
    EXPECT_EQ(db_->size(), 50);
    
    // Restore checkpoint 2
    ASSERT_TRUE(db_->restore_from_checkpoint(2));
    EXPECT_EQ(db_->size(), 100);
}

TEST_F(TimeSeriesDBPersistenceTest, DeleteCheckpoint) {
    // Add data and create checkpoint
    for (int i = 0; i < 30; ++i) {
        db_->add(1000 + i * 1000, 30.0 + i);
    }
    
    ASSERT_TRUE(db_->create_checkpoint(10));
    
    auto checkpoints = db_->list_checkpoints();
    ASSERT_EQ(checkpoints.size(), 1);
    
    // Delete checkpoint
    ASSERT_TRUE(db_->delete_checkpoint(10));
    
    checkpoints = db_->list_checkpoints();
    EXPECT_EQ(checkpoints.size(), 0);
}

TEST_F(TimeSeriesDBPersistenceTest, StorageStatistics) {
    // Add data and save
    for (int i = 0; i < 100; ++i) {
        db_->add(1000 + i * 1000, 100.0 + i);
    }
    
    std::string file_path = test_dir_ + "/stats_test.tsdb";
    ASSERT_TRUE(db_->save_to_disk(file_path));
    
    // Get storage statistics
    auto stats = db_->get_storage_stats();
    EXPECT_GT(stats["bytes_written"], 0);
}

TEST_F(TimeSeriesDBPersistenceTest, PersistenceWithQuery) {
    // Add data with different tags
    for (int i = 0; i < 50; ++i) {
        db_->add(1000 + i * 1000, 100.0 + i,
                {{"sensor", "temp"}, {"location", "room1"}});
    }
    for (int i = 50; i < 100; ++i) {
        db_->add(1000 + i * 1000, 100.0 + i,
                {{"sensor", "temp"}, {"location", "room2"}});
    }
    
    // Save to disk
    std::string file_path = test_dir_ + "/query_test.tsdb";
    ASSERT_TRUE(db_->save_to_disk(file_path));
    
    // Clear and reload
    db_->clear();
    ASSERT_TRUE(db_->load_from_disk(file_path));
    
    // Query with tag filter
    TimeRange range(1000, 200000);
    auto results = db_->query(range, {{"location", "room1"}});
    EXPECT_EQ(results.size(), 50);
    
    results = db_->query(range, {{"location", "room2"}});
    EXPECT_EQ(results.size(), 50);
}

} // namespace test
} // namespace sage_tsdb
