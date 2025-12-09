/**
 * @file test_resource_manager.cpp
 * @brief 资源管理器单元测试
 * 
 * 测试内容：
 * 1. BasicAllocation - 测试基本资源分配功能，验证线程和内存分配
 * 2. TaskSubmission - 测试任务提交和执行，验证多任务并发执行
 * 3. UsageReporting - 测试资源使用情况上报，验证指标收集
 * 4. GlobalLimits - 测试全局资源限制设置，验证多插件资源管理
 * 5. TotalUsage - 测试总资源使用统计，验证跨插件资源汇总
 * 6. Release - 测试资源释放功能，验证资源正确回收
 * 7. PressureDetection - 测试资源压力检测，验证高负载下的压力识别
 * 
 * 依赖：ResourceManager 类及相关接口
 */

#include "sage_tsdb/plugins/resource_manager.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>

using namespace sage_tsdb::plugins;

/**
 * @class ResourceManagerTest
 * @brief 资源管理器测试套件基类
 * 
 * 提供测试环境的初始化和清理功能
 */
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

/**
 * @test BasicAllocation
 * @brief 测试基本的资源分配功能
 * 
 * 测试目的：验证 ResourceManager 能够正确分配资源给插件
 * 测试步骤：
 *   1. 创建资源请求（2个线程，1MB内存）
 *   2. 为插件分配资源
 *   3. 验证返回的句柄有效
 *   4. 验证分配的资源数量正确
 */
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

/**
 * @test TaskSubmission
 * @brief 测试任务提交和执行功能
 * 
 * 测试目的：验证资源句柄能够正确提交和执行多个并发任务
 * 测试步骤：
 *   1. 分配1个线程的资源
 *   2. 提交10个原子计数任务
 *   3. 等待任务完成
 *   4. 验证所有任务都成功执行
 */
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

/**
 * @test UsageReporting
 * @brief 测试资源使用情况上报功能
 * 
 * 测试目的：验证插件能够上报资源使用情况，并且可以查询这些指标
 * 测试步骤：
 *   1. 分配资源并获取句柄
 *   2. 上报资源使用情况（线程、内存、处理元组数、延迟）
 *   3. 查询资源使用情况
 *   4. 验证所有指标都正确记录
 */
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

/**
 * @test GlobalLimits
 * @brief 测试全局资源限制设置
 * 
 * 测试目的：验证可以设置全局资源限制，并为多个插件分配资源
 * 测试步骤：
 *   1. 设置全局限制（8个线程，2GB内存）
 *   2. 为两个不同插件分配资源
 *   3. 验证两个插件都成功获得资源
 */
TEST_F(ResourceManagerTest, GlobalLimits) {
    rm_->setGlobalLimits(8, 2ULL * 1024 * 1024 * 1024); // 8 threads, 2GB
    
    // Allocate resources for multiple plugins
    auto handle1 = rm_->allocate("plugin1", ResourceRequest{});
    auto handle2 = rm_->allocate("plugin2", ResourceRequest{});
    
    EXPECT_NE(handle1, nullptr);
    EXPECT_NE(handle2, nullptr);
}

/**
 * @test TotalUsage
 * @brief 测试总资源使用统计
 * 
 * 测试目的：验证能够正确统计所有插件的总资源使用量
 * 测试步骤：
 *   1. 为两个插件分配资源
 *   2. 各自上报不同的资源使用量
 *   3. 查询总资源使用量
 *   4. 验证总量为各插件使用量之和
 */
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

/**
 * @test Release
 * @brief 测试资源释放功能
 * 
 * 测试目的：验证资源释放后能够正确回收，查询结果为空
 * 测试步骤：
 *   1. 为插件分配资源
 *   2. 验证资源有效
 *   3. 释放资源
 *   4. 查询该插件的资源使用情况，验证已清零
 */
TEST_F(ResourceManagerTest, Release) {
    auto handle = rm_->allocate("temp_plugin", ResourceRequest{});
    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(handle->isValid());
    
    rm_->release("temp_plugin");
    
    // After release, querying should return empty usage
    auto usage = rm_->queryUsage("temp_plugin");
    EXPECT_EQ(usage.memory_used_bytes, 0);
}

/**
 * @test PressureDetection
 * @brief 测试资源压力检测功能
 * 
 * 测试目的：验证在资源接近限制时能够检测到压力状态
 * 测试步骤：
 *   1. 设置较小的全局限制
 *   2. 分配接近限制的资源（93%）
 *   3. 上报高资源使用量
 *   4. 验证系统检测到压力状态
 *   5. 清理资源
 */
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
