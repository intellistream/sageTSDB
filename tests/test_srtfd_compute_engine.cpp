#include <gtest/gtest.h>
#include "sage_tsdb/compute/srtfd_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"
#include <memory>
#include <vector>

using namespace sage_tsdb;
using namespace sage_tsdb::compute;

class SRTFDComputeEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<TimeSeriesDB>();
        db_->createTable("sensor_events", TableType::Stream);
    }

    SRTFDConfig createConfig() const {
        SRTFDConfig config;
        config.backend = "statistical";
        config.input_table = "sensor_events";
        config.result_table = "srtfd_results";
        config.dataset = "TEST";
        config.input_dim = 3;
        config.num_classes = 4;
        config.anomaly_threshold = 3.0;
        config.strict_input_dim = true;
        return config;
    }

    void insertSample(int64_t timestamp, const std::vector<double>& features) {
        TimeSeriesData sample(timestamp, features);
        sample.tags["asset_id"] = "roller-1";
        db_->insert("sensor_events", sample);
    }

    std::unique_ptr<TimeSeriesDB> db_;
};

TEST_F(SRTFDComputeEngineTest, InitializeCreatesResultTable) {
    SRTFDComputeEngine engine;
    auto config = createConfig();

    EXPECT_FALSE(db_->hasTable("srtfd_results"));
    EXPECT_TRUE(engine.initialize(config, db_.get(), nullptr));
    EXPECT_TRUE(engine.isInitialized());
    EXPECT_TRUE(db_->hasTable("srtfd_results"));
}

TEST_F(SRTFDComputeEngineTest, RejectsMissingInputTable) {
    TimeSeriesDB db;
    SRTFDComputeEngine engine;
    auto config = createConfig();

    EXPECT_FALSE(engine.initialize(config, &db, nullptr));
}

TEST_F(SRTFDComputeEngineTest, UnsupportedBackendFailsFast) {
    SRTFDComputeEngine engine;
    auto config = createConfig();
    config.backend = "torchscript";

    EXPECT_FALSE(engine.initialize(config, db_.get(), nullptr));
}

TEST_F(SRTFDComputeEngineTest, EmptyWindowSucceeds) {
    SRTFDComputeEngine engine;
    auto config = createConfig();
    ASSERT_TRUE(engine.initialize(config, db_.get(), nullptr));

    auto status = engine.executeWindowDiagnosis(7, TimeRange(1000, 2000));

    EXPECT_TRUE(status.success) << status.error;
    EXPECT_EQ(status.input_count, 0);
    EXPECT_EQ(status.result_count, 0);
    EXPECT_EQ(status.invalid_count, 0);
}

TEST_F(SRTFDComputeEngineTest, DiagnosesWindowAndWritesResults) {
    insertSample(1000, {0.1, 0.2, 0.3});
    insertSample(2000, {5.0, 5.0, 5.0});

    SRTFDComputeEngine engine;
    auto config = createConfig();
    ASSERT_TRUE(engine.initialize(config, db_.get(), nullptr));

    auto status = engine.executeWindowDiagnosis(42, TimeRange(0, 3000));

    EXPECT_TRUE(status.success) << status.error;
    EXPECT_EQ(status.input_count, 2);
    EXPECT_EQ(status.result_count, 2);
    EXPECT_EQ(status.invalid_count, 0);
    EXPECT_EQ(status.anomaly_count, 1);
    EXPECT_GT(status.max_anomaly_score, 3.0);

    auto results = db_->query("srtfd_results", TimeRange(0, 3000));
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].tags.at("operator"), "srtfd");
    EXPECT_EQ(results[0].tags.at("window_id"), "42");
    EXPECT_EQ(results[0].tags.at("asset_id"), "roller-1");
    EXPECT_EQ(results[1].fields.at("is_anomaly"), "true");

    auto metrics = engine.getMetrics();
    EXPECT_EQ(metrics.total_windows_completed, 1);
    EXPECT_EQ(metrics.total_samples_processed, 2);
    EXPECT_EQ(metrics.total_results_written, 2);
    EXPECT_EQ(metrics.total_anomalies, 1);
}

TEST_F(SRTFDComputeEngineTest, StrictDimensionSkipsInvalidSamples) {
    insertSample(1000, {1.0, 2.0});
    insertSample(2000, {5.0, 5.0, 5.0});

    SRTFDComputeEngine engine;
    auto config = createConfig();
    ASSERT_TRUE(engine.initialize(config, db_.get(), nullptr));

    auto status = engine.executeWindowDiagnosis(9, TimeRange(0, 3000));

    EXPECT_TRUE(status.success) << status.error;
    EXPECT_EQ(status.input_count, 2);
    EXPECT_EQ(status.invalid_count, 1);
    EXPECT_EQ(status.result_count, 1);

    auto results = db_->query("srtfd_results", TimeRange(0, 3000));
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].timestamp, 2000);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}