#pragma once

/**
 * @file pecj_mswj_factory.h
 * @brief Shared helper to correctly construct a PECJ MSWJ operator.
 *
 * OoOJoin::MSWJOperator cannot be used after a plain default construction: its
 * setConfig() unconditionally dereferences an internal streamOperator that is
 * only wired up by the 7-argument parameterized constructor. Default-
 * constructing it (as the other PECJ operators allow) and then calling
 * setConfig() dereferences a null pointer and crashes inside PECJ.
 *
 * Both the compute engine (sage_tsdb_compute) and the plugin adapter
 * (sage_tsdb_plugins) need to build MSWJ, so this construction lives here to
 * avoid duplication and drift. Mirrors PECJ's own benchmark helper
 * (benchmark/src/Benchmark.cpp mswjConfiguration).
 *
 * Header-only and only meaningful under PECJ_FULL_INTEGRATION (it includes PECJ
 * headers). Include it only inside a `#ifdef PECJ_FULL_INTEGRATION` block.
 */

#ifdef PECJ_FULL_INTEGRATION

#include <cstdint>
#include <memory>

#include <Operator/MSWJOperator.h>
#include <Operator/MSWJ/Profiler/TupleProductivityProfiler.h>
#include <Operator/MSWJ/Operator/StreamOperator.h>

namespace sage_tsdb {
namespace detail {

/**
 * @brief Construct a fully-initialized PECJ MSWJ operator.
 * @param cfg PECJ configuration map (edited in place with MSWJ defaults). Its
 *            setConfig() is invoked internally, so callers must NOT call
 *            setConfig() again on the returned operator.
 * @return An MSWJ operator whose internal streamOperator is non-null.
 */
inline std::shared_ptr<OoOJoin::MSWJOperator> createMSWJOperator(
    const INTELLI::ConfigMapPtr& cfg) {
    // Time-unit multipliers used by PECJ's MSWJ defaults (see Benchmark.cpp).
    constexpr uint64_t kSeconds = 1000000;      // 1 s in PECJ time units
    constexpr uint64_t kMillionSeconds = 1000;  // PECJ's "MILLION_SECONDS"

    cfg->edit("g", static_cast<uint64_t>(10 * kMillionSeconds));
    cfg->edit("L", static_cast<uint64_t>(50 * kMillionSeconds));
    cfg->edit("userRecall", 0.4);
    cfg->edit("b", static_cast<uint64_t>(10 * kMillionSeconds));
    cfg->edit("confidenceValue", 0.5);
    cfg->edit("P", static_cast<uint64_t>(10 * kSeconds));
    cfg->edit("maxDelay", static_cast<uint64_t>(INT16_MAX));
    cfg->edit("StreamCount", static_cast<uint64_t>(2));
    cfg->edit("Stream_1", static_cast<uint64_t>(0));
    cfg->edit("Stream_2", static_cast<uint64_t>(0));

    // Build sub-components in dependency order.
    auto tupleProductivityProfiler =
        std::make_shared<MSWJ::TupleProductivityProfiler>(cfg);
    auto statisticsManager = std::make_shared<MSWJ::StatisticsManager>(
        tupleProductivityProfiler.get(), cfg);
    auto bufferSizeManager = std::make_shared<MSWJ::BufferSizeManager>(
        statisticsManager.get(), tupleProductivityProfiler.get());
    auto streamOperator = std::make_shared<MSWJ::StreamOperator>(
        tupleProductivityProfiler.get(), cfg);
    auto synchronizer = std::make_shared<MSWJ::Synchronizer>(
        2, streamOperator.get(), cfg);

    bufferSizeManager->setConfig(cfg);

    auto kSlackS = std::make_shared<MSWJ::KSlack>(
        1, bufferSizeManager.get(), statisticsManager.get(), synchronizer.get());
    auto kSlackR = std::make_shared<MSWJ::KSlack>(
        2, bufferSizeManager.get(), statisticsManager.get(), synchronizer.get());

    auto mswj = std::make_shared<OoOJoin::MSWJOperator>(
        bufferSizeManager, tupleProductivityProfiler, synchronizer,
        streamOperator, statisticsManager, kSlackR, kSlackS);
    mswj->setConfig(cfg);
    return mswj;
}

}  // namespace detail
}  // namespace sage_tsdb

#endif  // PECJ_FULL_INTEGRATION
