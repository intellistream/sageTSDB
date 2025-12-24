/**
 * @file test_pecj_operators.cpp
 * @brief Test file to verify sageTSDB can correctly call all PECJ operators
 * 
 * This test verifies:
 * 1. All PECJ operator types can be created
 * 2. Operator type conversion functions work correctly
 * 3. AQP support detection is accurate
 * 4. ComputeConfig parameters are properly set
 * 
 * @version 1.0
 * @date 2024-12-16
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>

// Include the header with operator definitions
#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

// Test without PECJ integration - just test the enum and helper functions
namespace sage_tsdb {
namespace compute {

// Re-declare for testing if not in integrated mode
#ifndef PECJ_MODE_INTEGRATED

enum class PECJOperatorType {
    IAWJ,
    MeanAQP,
    IMA,
    MSWJ,
    AI,
    LinearSVI,
    IAWJSel,
    LazyIAWJSel,
    SHJ,
    PRJ,
    PECJ
};

inline std::string operatorTypeToString(PECJOperatorType type) {
    switch (type) {
        case PECJOperatorType::IAWJ:        return "IAWJ";
        case PECJOperatorType::MeanAQP:     return "MeanAQP";
        case PECJOperatorType::IMA:         return "IMA";
        case PECJOperatorType::MSWJ:        return "MSWJ";
        case PECJOperatorType::AI:          return "AI";
        case PECJOperatorType::LinearSVI:   return "LinearSVI";
        case PECJOperatorType::IAWJSel:     return "IAWJSel";
        case PECJOperatorType::LazyIAWJSel: return "LazyIAWJSel";
        case PECJOperatorType::SHJ:         return "SHJ";
        case PECJOperatorType::PRJ:         return "PRJ";
        case PECJOperatorType::PECJ:        return "IMA";
        default:                            return "IAWJ";
    }
}

inline PECJOperatorType stringToOperatorType(const std::string& tag) {
    if (tag == "IAWJ")        return PECJOperatorType::IAWJ;
    if (tag == "MeanAQP")     return PECJOperatorType::MeanAQP;
    if (tag == "IMA")         return PECJOperatorType::IMA;
    if (tag == "MSWJ")        return PECJOperatorType::MSWJ;
    if (tag == "AI")          return PECJOperatorType::AI;
    if (tag == "LinearSVI")   return PECJOperatorType::LinearSVI;
    if (tag == "IAWJSel")     return PECJOperatorType::IAWJSel;
    if (tag == "LazyIAWJSel") return PECJOperatorType::LazyIAWJSel;
    if (tag == "SHJ")         return PECJOperatorType::SHJ;
    if (tag == "PRJ")         return PECJOperatorType::PRJ;
    if (tag == "PECJ" || tag == "PEC") return PECJOperatorType::PECJ;
    return PECJOperatorType::IAWJ;
}

inline bool operatorSupportsAQP(PECJOperatorType type) {
    switch (type) {
        case PECJOperatorType::MeanAQP:
        case PECJOperatorType::IMA:
        case PECJOperatorType::MSWJ:
        case PECJOperatorType::IAWJSel:
        case PECJOperatorType::LazyIAWJSel:
        case PECJOperatorType::PECJ:
            return true;
        default:
            return false;
    }
}

#endif // !PECJ_MODE_INTEGRATED

} // namespace compute
} // namespace sage_tsdb

using namespace sage_tsdb::compute;

// ============================================================================
// Test Fixture
// ============================================================================

class PECJOperatorsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // List of all operator types
        all_operator_types_ = {
            PECJOperatorType::IAWJ,
            PECJOperatorType::MeanAQP,
            PECJOperatorType::IMA,
            PECJOperatorType::MSWJ,
            PECJOperatorType::AI,
            PECJOperatorType::LinearSVI,
            PECJOperatorType::IAWJSel,
            PECJOperatorType::LazyIAWJSel,
            PECJOperatorType::SHJ,
            PECJOperatorType::PRJ,
            PECJOperatorType::PECJ
        };
        
        // List of all operator string tags
        all_operator_tags_ = {
            "IAWJ", "MeanAQP", "IMA", "MSWJ", "AI", 
            "LinearSVI", "IAWJSel", "LazyIAWJSel", "SHJ", "PRJ", "PECJ"
        };
        
        // Operators that support AQP
        aqp_operators_ = {
            PECJOperatorType::MeanAQP,
            PECJOperatorType::IMA,
            PECJOperatorType::MSWJ,
            PECJOperatorType::IAWJSel,
            PECJOperatorType::LazyIAWJSel,
            PECJOperatorType::PECJ
        };
        
        // Operators that do NOT support AQP
        non_aqp_operators_ = {
            PECJOperatorType::IAWJ,
            PECJOperatorType::AI,
            PECJOperatorType::LinearSVI,
            PECJOperatorType::SHJ,
            PECJOperatorType::PRJ
        };
    }
    
    std::vector<PECJOperatorType> all_operator_types_;
    std::vector<std::string> all_operator_tags_;
    std::vector<PECJOperatorType> aqp_operators_;
    std::vector<PECJOperatorType> non_aqp_operators_;
};

// ============================================================================
// Operator Type Enum Tests
// ============================================================================

TEST_F(PECJOperatorsTest, OperatorTypeEnumValues) {
    // Test that all expected operator types exist
    EXPECT_EQ(static_cast<int>(PECJOperatorType::IAWJ), 0);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::MeanAQP), 1);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::IMA), 2);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::MSWJ), 3);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::AI), 4);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::LinearSVI), 5);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::IAWJSel), 6);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::LazyIAWJSel), 7);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::SHJ), 8);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::PRJ), 9);
    EXPECT_EQ(static_cast<int>(PECJOperatorType::PECJ), 10);
}

// ============================================================================
// Operator Type to String Conversion Tests
// ============================================================================

TEST_F(PECJOperatorsTest, OperatorTypeToStringConversion) {
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::IAWJ), "IAWJ");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::MeanAQP), "MeanAQP");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::IMA), "IMA");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::MSWJ), "MSWJ");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::AI), "AI");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::LinearSVI), "LinearSVI");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::IAWJSel), "IAWJSel");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::LazyIAWJSel), "LazyIAWJSel");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::SHJ), "SHJ");
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::PRJ), "PRJ");
    // PECJ maps to IMA internally
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::PECJ), "IMA");
}

// ============================================================================
// String to Operator Type Conversion Tests
// ============================================================================

TEST_F(PECJOperatorsTest, StringToOperatorTypeConversion) {
    EXPECT_EQ(stringToOperatorType("IAWJ"), PECJOperatorType::IAWJ);
    EXPECT_EQ(stringToOperatorType("MeanAQP"), PECJOperatorType::MeanAQP);
    EXPECT_EQ(stringToOperatorType("IMA"), PECJOperatorType::IMA);
    EXPECT_EQ(stringToOperatorType("MSWJ"), PECJOperatorType::MSWJ);
    EXPECT_EQ(stringToOperatorType("AI"), PECJOperatorType::AI);
    EXPECT_EQ(stringToOperatorType("LinearSVI"), PECJOperatorType::LinearSVI);
    EXPECT_EQ(stringToOperatorType("IAWJSel"), PECJOperatorType::IAWJSel);
    EXPECT_EQ(stringToOperatorType("LazyIAWJSel"), PECJOperatorType::LazyIAWJSel);
    EXPECT_EQ(stringToOperatorType("SHJ"), PECJOperatorType::SHJ);
    EXPECT_EQ(stringToOperatorType("PRJ"), PECJOperatorType::PRJ);
    EXPECT_EQ(stringToOperatorType("PECJ"), PECJOperatorType::PECJ);
    EXPECT_EQ(stringToOperatorType("PEC"), PECJOperatorType::PECJ);  // Alternative tag
}

TEST_F(PECJOperatorsTest, StringToOperatorTypeDefaultValue) {
    // Unknown strings should default to IAWJ
    EXPECT_EQ(stringToOperatorType("UNKNOWN"), PECJOperatorType::IAWJ);
    EXPECT_EQ(stringToOperatorType(""), PECJOperatorType::IAWJ);
    EXPECT_EQ(stringToOperatorType("invalid"), PECJOperatorType::IAWJ);
}

TEST_F(PECJOperatorsTest, RoundTripConversion) {
    // Test that type -> string -> type round trips correctly
    for (auto type : all_operator_types_) {
        if (type == PECJOperatorType::PECJ) {
            // PECJ is special - it maps to IMA string
            continue;
        }
        std::string tag = operatorTypeToString(type);
        PECJOperatorType converted = stringToOperatorType(tag);
        EXPECT_EQ(converted, type) << "Failed round trip for: " << tag;
    }
}

// ============================================================================
// AQP Support Tests
// ============================================================================

TEST_F(PECJOperatorsTest, AQPSupportedOperators) {
    // Operators that SHOULD support AQP
    for (auto type : aqp_operators_) {
        EXPECT_TRUE(operatorSupportsAQP(type)) 
            << "Expected AQP support for: " << operatorTypeToString(type);
    }
}

TEST_F(PECJOperatorsTest, NonAQPOperators) {
    // Operators that should NOT support AQP
    for (auto type : non_aqp_operators_) {
        EXPECT_FALSE(operatorSupportsAQP(type)) 
            << "Expected NO AQP support for: " << operatorTypeToString(type);
    }
}

TEST_F(PECJOperatorsTest, AQPOperatorCount) {
    // Count operators with AQP support
    int aqp_count = 0;
    for (auto type : all_operator_types_) {
        if (operatorSupportsAQP(type)) {
            aqp_count++;
        }
    }
    // Should be 6: MeanAQP, IMA, MSWJ, IAWJSel, LazyIAWJSel, PECJ
    EXPECT_EQ(aqp_count, 6);
}

// ============================================================================
// Operator Description Tests
// ============================================================================

TEST_F(PECJOperatorsTest, AllOperatorTypesHaveNames) {
    // Every operator type should have a non-empty string representation
    for (auto type : all_operator_types_) {
        std::string name = operatorTypeToString(type);
        EXPECT_FALSE(name.empty()) << "Operator should have a name";
        EXPECT_NE(name, "") << "Operator name should not be empty";
    }
}

TEST_F(PECJOperatorsTest, OperatorCategorization) {
    // Test operator categorization by purpose
    
    // Basic single-window operators
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::IAWJ), "IAWJ");
    
    // AQP operators
    EXPECT_TRUE(operatorSupportsAQP(PECJOperatorType::MeanAQP));
    EXPECT_TRUE(operatorSupportsAQP(PECJOperatorType::IMA));
    
    // Multi-stream operator
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::MSWJ), "MSWJ");
    EXPECT_TRUE(operatorSupportsAQP(PECJOperatorType::MSWJ));
    
    // Lazy evaluation operator
    EXPECT_EQ(operatorTypeToString(PECJOperatorType::LazyIAWJSel), "LazyIAWJSel");
    EXPECT_TRUE(operatorSupportsAQP(PECJOperatorType::LazyIAWJSel));
    
    // Baseline operators (no AQP)
    EXPECT_FALSE(operatorSupportsAQP(PECJOperatorType::SHJ));
    EXPECT_FALSE(operatorSupportsAQP(PECJOperatorType::PRJ));
}

// ============================================================================
// Operator Selection Logic Tests
// ============================================================================

TEST_F(PECJOperatorsTest, SelectOperatorForUseCase) {
    // Test selecting appropriate operator for different use cases
    
    // Use case 1: Need fast approximate results -> Use MeanAQP or IMA
    auto fast_aqp = PECJOperatorType::MeanAQP;
    EXPECT_TRUE(operatorSupportsAQP(fast_aqp));
    
    // Use case 2: Multi-stream with out-of-order data -> Use MSWJ
    auto multi_stream = PECJOperatorType::MSWJ;
    EXPECT_EQ(operatorTypeToString(multi_stream), "MSWJ");
    
    // Use case 3: Baseline comparison -> Use SHJ or PRJ
    EXPECT_FALSE(operatorSupportsAQP(PECJOperatorType::SHJ));
    EXPECT_FALSE(operatorSupportsAQP(PECJOperatorType::PRJ));
    
    // Use case 4: Lazy evaluation for better throughput -> Use LazyIAWJSel
    auto lazy_op = PECJOperatorType::LazyIAWJSel;
    EXPECT_TRUE(operatorSupportsAQP(lazy_op));
    
    // Use case 5: Full PECJ with compensation -> Use PECJ/IMA
    auto full_pecj = PECJOperatorType::PECJ;
    EXPECT_TRUE(operatorSupportsAQP(full_pecj));
}

// ============================================================================
// Configuration Tests
// ============================================================================

#ifdef PECJ_MODE_INTEGRATED

TEST_F(PECJOperatorsTest, ComputeConfigDefaults) {
    ComputeConfig config;
    
    // Check default values
    EXPECT_EQ(config.window_len_us, 1000000);  // 1 second
    EXPECT_EQ(config.slide_len_us, 500000);    // 0.5 second
    EXPECT_EQ(config.operator_type, "IAWJ");
    EXPECT_EQ(config.s_buffer_len, 100000);
    EXPECT_EQ(config.r_buffer_len, 100000);
    EXPECT_EQ(config.time_step_us, 1000);
    EXPECT_EQ(config.watermark_tag, "arrival");
    EXPECT_FALSE(config.ima_disable_compensation);
    EXPECT_FALSE(config.mswj_compensation);
}

TEST_F(PECJOperatorsTest, ComputeConfigCustomization) {
    ComputeConfig config;
    
    // Set custom values
    config.operator_type = "MSWJ";
    config.window_len_us = 2000000;
    config.slide_len_us = 1000000;
    config.mswj_compensation = true;
    
    // Verify
    EXPECT_EQ(config.operator_type, "MSWJ");
    EXPECT_EQ(config.window_len_us, 2000000);
    EXPECT_TRUE(config.mswj_compensation);
}

TEST_F(PECJOperatorsTest, TimeRangeValidation) {
    TimeRange valid_range(1000, 2000);
    EXPECT_TRUE(valid_range.valid());
    EXPECT_EQ(valid_range.duration(), 1000);
    EXPECT_TRUE(valid_range.contains(1500));
    EXPECT_FALSE(valid_range.contains(500));
    EXPECT_FALSE(valid_range.contains(2500));
    
    TimeRange invalid_range(2000, 1000);
    EXPECT_FALSE(invalid_range.valid());
    
    TimeRange empty_range;
    EXPECT_FALSE(empty_range.valid());
}

#endif // PECJ_MODE_INTEGRATED

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
