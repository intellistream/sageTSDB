#include "sage_tsdb/plugins/resource_manager.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>

using namespace sage_tsdb::plugins;

class ResourceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        rm_ = createResourceManager();
        ASSERT_NE(rm_, nullptr);
    }
    
    void TearDown() override {
        rm_.reset();
    }
    
    std::shared_ptr<ResourceManager> rm_;
};

TEST_F(ResourceManagerTest, BasicAllocation) {
    ResourceRequest req;
    req.requested_threads = 2;
    req.max_memory_bytes = 1024 * 1024; // 1MB
    
    auto handle = rm_->allocate("test_plugin", req);
    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(handle->isValid());
    
    auto allocated = handle->getAllocated();
    EXPECT_EQ(allocated.requested_threads, 2);
}

TEST_F(ResourceManagerTest, TaskSubmission) {
    ResourceRequest req;
    req.requested_threads = 1;
    
    auto handle = rm_->allocate("task_plugin", req);
    ASSERT_NE(handle, nullptr);
    
    std::atomic<int> counter{0};
    
    // Submit multiple tasks
    for (int i = 0; i < 10; ++i) {
        bool submitted = handle->submitTask([&counter]() {
            counter.fetch_add(1);
        });
        EXPECT_TRUE(submitted);
    }
    
    // Wait for tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    EXPECT_EQ(counter.load(), 10);
}

TEST_F(ResourceManagerTest, UsageReporting) {
    ResourceRequest req;
    req.requested_threads = 1;
    
    auto handle = rm_->allocate("metrics_plugin", req);
    ASSERT_NE(handle, nullptr);
    
    ResourceUsage usage;
    usage.threads_used = 1;
    usage.memory_used_bytes = 1024;
    usage.tuples_processed = 100;
    usage.avg_latency_ms = 1.5;
    
    handle->reportUsage(usage);
    
    auto queried = rm_->queryUsage("metrics_plugin");
    EXPECT_EQ(queried.threads_used, 1);
    EXPECT_EQ(queried.memory_used_bytes, 1024);
    EXPECT_EQ(queried.tuples_processed, 100);
    EXPECT_DOUBLE_EQ(queried.avg_latency_ms, 1.5);
}

TEST_F(ResourceManagerTest, GlobalLimits) {
    rm_->setGlobalLimits(8, 2ULL * 1024 * 1024 * 1024); // 8 threads, 2GB
    
    // Allocate resources for multiple plugins
    auto handle1 = rm_->allocate("plugin1", ResourceRequest{});
    auto handle2 = rm_->allocate("plugin2", ResourceRequest{});
    
    EXPECT_NE(handle1, nullptr);
    EXPECT_NE(handle2, nullptr);
}

TEST_F(ResourceManagerTest, TotalUsage) {
    auto handle1 = rm_->allocate("plugin1", ResourceRequest{});
    auto handle2 = rm_->allocate("plugin2", ResourceRequest{});
    
    ResourceUsage usage1;
    usage1.memory_used_bytes = 1024;
    usage1.tuples_processed = 50;
    
    ResourceUsage usage2;
    usage2.memory_used_bytes = 2048;
    usage2.tuples_processed = 75;
    
    handle1->reportUsage(usage1);
    handle2->reportUsage(usage2);
    
    auto total = rm_->getTotalUsage();
    EXPECT_EQ(total.memory_used_bytes, 3072);
    EXPECT_EQ(total.tuples_processed, 125);
}

TEST_F(ResourceManagerTest, Release) {
    auto handle = rm_->allocate("temp_plugin", ResourceRequest{});
    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(handle->isValid());
    
    rm_->release("temp_plugin");
    
    // After release, querying should return empty usage
    auto usage = rm_->queryUsage("temp_plugin");
    EXPECT_EQ(usage.memory_used_bytes, 0);
}

TEST_F(ResourceManagerTest, PressureDetection) {
    rm_->setGlobalLimits(10, 1024); // Very tight limits
    
    ResourceRequest req;
    req.requested_threads = 9;
    req.max_memory_bytes = 950;
    
    auto handle = rm_->allocate("heavy_plugin", req);
    ASSERT_NE(handle, nullptr);
    
    ResourceUsage usage;
    usage.threads_used = 9;
    usage.memory_used_bytes = 950; // 93% of limit
    
    handle->reportUsage(usage);
    
    // Give a moment for usage to be registered
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should detect pressure when close to limits
    EXPECT_TRUE(rm_->isUnderPressure());
    
    // Clean up before handle goes out of scope
    rm_->release("heavy_plugin");
    handle.reset();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
