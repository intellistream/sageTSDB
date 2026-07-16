/**
 * @file test_storage_backend_contract.cpp
 * @brief Backend-agnostic consistency tests for IStorageBackend (D7).
 *
 * These tests pin down the observable IStorageBackend contract:
 *   - runContractSuite() runs the full read/write matrix against ANY backend.
 *   - It is executed against core::MemoryBackend (the reference) here.
 *   - The same suite is wired against core::DamengBackend behind
 *     SAGE_TSDB_ENABLE_DM; until the DM driver is filled in, that case SKIPs
 *     (prints "pending"), so the differential framework is already in place.
 *   - Stsb1 codec round-trip tests (with fixed hex test vectors) guarantee the
 *     value encoding both backends must share.
 *
 * @see docs/STORAGE_BACKEND_CONTRACT.md
 */

#include <gtest/gtest.h>

#include "sage_tsdb/core/backends/memory_backend.h"
#include "sage_tsdb/core/storage_backend.h"
#include "sage_tsdb/core/value_codec.h"

#ifdef SAGE_TSDB_ENABLE_DM
#include "sage_tsdb/core/backends/dameng_backend.h"
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace sage_tsdb;
using namespace sage_tsdb::core;

namespace {

// Compare two result sequences by the fields backends must preserve.
bool sameSeq(const std::vector<TimeSeriesData>& a,
             const std::vector<TimeSeriesData>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].timestamp != b[i].timestamp) return false;
        if (a[i].is_array() != b[i].is_array()) return false;
        if (a[i].as_vector() != b[i].as_vector()) return false;
        if (a[i].tags != b[i].tags) return false;
    }
    return true;
}

// Deterministic sample data: out-of-order timestamps, tags, and one vector.
std::vector<TimeSeriesData> sampleData() {
    std::vector<TimeSeriesData> v;
    v.emplace_back(300, 3.0, Tags{{"sensor", "a"}, {"key", "3"}});
    v.emplace_back(100, 1.0, Tags{{"sensor", "b"}, {"key", "1"}});
    v.emplace_back(200, 2.0, Tags{{"sensor", "a"}, {"key", "2"}});
    v.emplace_back(500, 5.0, Tags{{"sensor", "b"}, {"key", "5"}});
    v.emplace_back(400, 4.0, Tags{{"sensor", "a"}, {"key", "4"}});
    v.emplace_back(600, std::vector<double>{1.5, 2.5, 3.5},
                   Tags{{"sensor", "a"}, {"key", "6"}});
    return v;
}

/**
 * @brief Run the full IStorageBackend contract matrix against a backend.
 *
 * Any conforming backend must satisfy every assertion. MemoryBackend is the
 * reference; DamengBackend must match once its driver is wired.
 */
void runContractSuite(IStorageBackend& be) {
    const auto data = sampleData();

    // -- table management --
    EXPECT_TRUE(be.createTable("t", TableType::Stream));
    EXPECT_FALSE(be.createTable("t", TableType::Stream));   // duplicate -> false
    EXPECT_TRUE(be.hasTable("t"));
    EXPECT_FALSE(be.hasTable("missing"));

    // -- writes --
    auto idx = be.insertBatch("t", data);
    EXPECT_EQ(idx.size(), data.size());
    EXPECT_EQ(be.size("t"), data.size());
    EXPECT_EQ(be.size("missing"), 0u);                      // missing -> 0

    {  // listTables contains "t"
        auto tables = be.listTables();
        EXPECT_NE(std::find(tables.begin(), tables.end(), "t"), tables.end());
    }

    // -- queries: full range, inclusive boundaries, tag filter, limit --
    auto q = [&](TimeRange r, Tags tags, int32_t limit) {
        QueryConfig c; c.time_range = r; c.filter_tags = std::move(tags); c.limit = limit;
        return be.query("t", c);
    };
    EXPECT_EQ(q({0, 1000}, {}, 1000).size(), 6u);           // full
    EXPECT_EQ(q({100, 500}, {}, 1000).size(), 5u);          // inclusive both ends
    EXPECT_EQ(q({150, 450}, {}, 1000).size(), 3u);          // inner (200,300,400)
    EXPECT_EQ(q({0, 1000}, {{"sensor", "a"}}, 1000).size(), 4u);   // tag filter
    EXPECT_EQ(q({0, 1000}, {{"sensor", "b"}}, 1000).size(), 2u);
    EXPECT_EQ(q({0, 1000}, {}, 2).size(), 2u);              // limit
    EXPECT_EQ(q({700, 800}, {}, 1000).size(), 0u);          // empty range
    EXPECT_EQ(q({0, 1000}, {{"sensor", "z"}}, 1000).size(), 0u);   // no-match tag

    // -- vector value round-trip --
    {
        auto r = q({600, 600}, {}, 1000);
        ASSERT_EQ(r.size(), 1u);
        EXPECT_TRUE(r[0].is_array());
        ASSERT_EQ(r[0].as_vector().size(), 3u);
        EXPECT_DOUBLE_EQ(r[0].as_vector()[1], 2.5);
    }

    // -- single insert returns a usable index; feeds size --
    be.insert("t", TimeSeriesData(700, 7.0, Tags{{"sensor", "a"}}));
    EXPECT_EQ(be.size("t"), data.size() + 1);

    // -- clear keeps table, drops rows; flush; drop --
    be.clear("t");
    EXPECT_EQ(be.size("t"), 0u);
    EXPECT_TRUE(be.hasTable("t"));
    EXPECT_TRUE(be.flush());
    EXPECT_TRUE(be.dropTable("t"));
    EXPECT_FALSE(be.hasTable("t"));

    // -- missing table throws on insert (no silent create) --
    EXPECT_THROW(be.insert("nope", data[0]), std::runtime_error);
}

// ===================== stsb1 codec round-trip =====================

TEST(Stsb1Codec, ScalarRoundTrip) {
    auto blob = stsb1::encode(TimeSeriesValue{42.5});
    EXPECT_EQ(blob.size(), 20u);                 // 12 header + 8
    EXPECT_EQ(blob[5], stsb1::kKindScalar);
    auto v = stsb1::decode(blob);
    ASSERT_TRUE(std::holds_alternative<double>(v));
    EXPECT_DOUBLE_EQ(std::get<double>(v), 42.5);
}

TEST(Stsb1Codec, VectorRoundTrip) {
    std::vector<double> vec(52, 0.0);
    for (size_t i = 0; i < vec.size(); ++i) vec[i] = static_cast<double>(i) * 0.25;
    auto blob = stsb1::encode(TimeSeriesValue{vec});
    EXPECT_EQ(blob.size(), 12u + 8u * 52u);      // 428 bytes
    EXPECT_EQ(blob[5], stsb1::kKindVector);
    auto v = stsb1::decode(blob);
    ASSERT_TRUE(std::holds_alternative<std::vector<double>>(v));
    EXPECT_EQ(std::get<std::vector<double>>(v), vec);
}

TEST(Stsb1Codec, FixedHeaderBytes) {
    // Header + magic/version/kind/count for scalar 1.0, checked byte-exactly so
    // the DM side can validate its encoder against these vectors.
    auto blob = stsb1::encode(TimeSeriesValue{1.0});
    const std::vector<uint8_t> expected = {
        'S', 'T', 'S', 'B',            // magic
        0x01,                          // version
        0x01,                          // kind = scalar
        0x00, 0x00,                    // reserved
        0x01, 0x00, 0x00, 0x00,        // count = 1 (LE)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F  // 1.0 IEEE-754 LE
    };
    EXPECT_EQ(blob, expected);
}

TEST(Stsb1Codec, RejectsMalformed) {
    EXPECT_THROW(stsb1::decode({0x00, 0x01}), std::runtime_error);       // too short
    auto blob = stsb1::encode(TimeSeriesValue{1.0});
    blob[0] = 'X';                                                        // bad magic
    EXPECT_THROW(stsb1::decode(blob), std::runtime_error);
}

// ===================== backend runs =====================

TEST(StorageBackendContract, MemoryBackend) {
    MemoryBackend be;
    runContractSuite(be);
}

#ifdef SAGE_TSDB_ENABLE_DM
TEST(StorageBackendContract, DamengBackend) {
    StorageBackendConfig cfg;
    cfg.backend = "dameng";
    DamengBackend be(cfg);
    try {
        runContractSuite(be);
    } catch (const NotImplemented& e) {
        GTEST_SKIP() << "DamengBackend not wired yet (pending): " << e.what();
    }
}
#endif

}  // namespace
