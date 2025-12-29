/**
 * @file pecj_adapter.cpp
 * @brief Implementation of PECJ adapter for sageTSDB
 * 
 * This adapter provides integration between sageTSDB and the PECJ 
 * (Predictive Error-bounded Computation for Joins) library.
 * 
 * Features:
 * - Full PECJ integration when PECJ_FULL_INTEGRATION is defined
 * - Stub mode for development/testing without PECJ dependencies
 * - Multi-threaded data processing
 * - EventBus integration for window result publishing
 */

#include "sage_tsdb/plugins/adapters/pecj_adapter.h"
#include "sage_tsdb/plugins/plugin_registry.h"
#include <chrono>
#include <iostream>
#include <sys/time.h>

// Include specific operator headers for direct instantiation
#ifdef PECJ_FULL_INTEGRATION
#include <Operator/IAWJOperator.h>
#include <Operator/IMAIAWJOperator.h>
#include <Operator/MSWJOperator.h>
#include <Operator/AIOperator.h>
#include <Operator/LinearSVIOperator.h>
#include <Operator/MeanAQPIAWJOperator.h>
#include <Operator/RawSHJOperator.h>
#include <Operator/RawPRJOperator.h>
#endif

namespace sage_tsdb {
namespace plugins {

// ============================================================================
// Constructor / Destructor
// ============================================================================

PECJAdapter::PECJAdapter(const PluginConfig& config)
    : config_(config) {
    parseConfig(config);
}

PECJAdapter::~PECJAdapter() {
    stop();
}

// ============================================================================
// Configuration Parsing
// ============================================================================

void PECJAdapter::parseConfig(const PluginConfig& config) {
    // Parse window configuration
    if (config.count("windowLen")) {
        window_config_.window_len_us = std::stoull(config.at("windowLen"));
    }
    if (config.count("slideLen")) {
        window_config_.slide_len_us = std::stoull(config.at("slideLen"));
    }
    if (config.count("latenessMs")) {
        window_config_.lateness_ms = std::stoull(config.at("latenessMs"));
    }
    if (config.count("timeStep")) {
        window_config_.time_step_us = std::stoull(config.at("timeStep"));
    }
    if (config.count("sLen")) {
        window_config_.s_buffer_len = std::stoull(config.at("sLen"));
    }
    if (config.count("rLen")) {
        window_config_.r_buffer_len = std::stoull(config.at("rLen"));
    }
    
    // Parse watermark configuration
    if (config.count("watermarkTimeMs")) {
        window_config_.watermark_time_ms = std::stoull(config.at("watermarkTimeMs"));
    }
    if (config.count("wmTag")) {
        window_config_.wm_tag = config.at("wmTag");
    }
    
    // Parse operator type
    if (config.count("operator")) {
        std::string op = config.at("operator");
        if (op == "IAWJ") operator_type_ = OperatorType::IAWJ;
        else if (op == "IMA") operator_type_ = OperatorType::IMA;
        else if (op == "MSWJ") operator_type_ = OperatorType::MSWJ;
        else if (op == "AI") operator_type_ = OperatorType::AI;
        else if (op == "LinearSVI") operator_type_ = OperatorType::LINEAR_SVI;
        else if (op == "MeanAQP") operator_type_ = OperatorType::MEAN_AQP;
        else if (op == "SHJ") operator_type_ = OperatorType::SHJ;
        else if (op == "PRJ") operator_type_ = OperatorType::PRJ;
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool PECJAdapter::initialize(const PluginConfig& config) {
    // Legacy initialize - defaults to Baseline mode (independent threads)
    run_mode_ = RunMode::Baseline;
    return initialize(config, core::ResourceRequest{}, nullptr);
}

bool PECJAdapter::initialize(const PluginConfig& config,
                             const core::ResourceRequest& resource_request,
                             core::ResourceHandle* resource_handle) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (initialized_.load()) {
        return true;  // Already initialized
    }
    
    config_ = config;
    resource_request_ = resource_request;
    // Store the raw pointer (we don't own it, ResourceManager does)
    resource_handle_ = resource_handle;
    parseConfig(config);
    
    // Determine run mode
    if (resource_handle) {
        run_mode_ = RunMode::Integrated;
        std::cout << "✓ PECJ Adapter: Integrated mode (ResourceManager-controlled)" << std::endl;
    } else {
#ifdef PECJ_FULL_INTEGRATION
        run_mode_ = RunMode::Baseline;
        std::cout << "✓ PECJ Adapter: Baseline mode (independent threads)" << std::endl;
#else
        run_mode_ = RunMode::Stub;
        std::cout << "✓ PECJ Adapter: Stub mode (no PECJ library)" << std::endl;
#endif
    }
    
    // Initialize PECJ operator
    if (!initializePECJ()) {
        return false;
    }
    
    initialized_.store(true);
    std::cout << "✓ PECJ Adapter initialized successfully" << std::endl;
    return true;
}

bool PECJAdapter::initializePECJ() {
    try {
#ifdef PECJ_FULL_INTEGRATION
        // ====== Full PECJ Integration ======
        std::cout << "Initializing PECJ with full integration..." << std::endl;
        
        // Initialize time base
        gettimeofday(&time_base_, nullptr);
        
        // Create PECJ configuration
        pecj_config_ = std::make_shared<INTELLI::ConfigMap>();
        pecj_config_->edit("windowLen", static_cast<uint64_t>(window_config_.window_len_us));
        pecj_config_->edit("slideLen", static_cast<uint64_t>(window_config_.slide_len_us));
        pecj_config_->edit("sLen", static_cast<uint64_t>(window_config_.s_buffer_len));
        pecj_config_->edit("rLen", static_cast<uint64_t>(window_config_.r_buffer_len));
        pecj_config_->edit("timeStep", static_cast<uint64_t>(window_config_.time_step_us));
        pecj_config_->edit("latenessMs", static_cast<uint64_t>(window_config_.lateness_ms));
        
        // CRITICAL: Match Integrated Mode's joinSum configuration
        // joinSum=1 enables join result counting and affects AQP calculation path
        // Without this, IMA operator uses different AQP formula, causing result mismatch
        pecj_config_->edit("joinSum", static_cast<uint64_t>(1));
        
        // CRITICAL: Set watermark configuration for proper batch processing
        // Without this, watermark triggers too early (default 10ms) and terminates processing
        pecj_config_->edit("watermarkTimeMs", static_cast<uint64_t>(window_config_.watermark_time_ms));
        pecj_config_->edit("wmTag", window_config_.wm_tag);
        
        std::cout << "  Watermark Tag: " << window_config_.wm_tag << std::endl;
        std::cout << "  Watermark Time: " << window_config_.watermark_time_ms << " ms" << std::endl;
        
        // CRITICAL FIX: Create operator directly instead of using OperatorTable
        // OperatorTable creates operator instances in its constructor, and these
        // instances may have different initial state than freshly created ones.
        // This was causing AQP discrepancy between Plugin Mode and Integrated Mode.
        //
        // Match Integrated Mode's approach: use std::make_shared to create operator directly.
        std::string op_name;
        switch (operator_type_) {
            case OperatorType::IAWJ:
                pecj_operator_ = std::make_shared<OoOJoin::IAWJOperator>();
                op_name = "IAWJ";
                break;
            case OperatorType::IMA:
                pecj_operator_ = std::make_shared<OoOJoin::IMAIAWJOperator>();
                op_name = "IMA";
                break;
            case OperatorType::MSWJ:
                pecj_operator_ = std::make_shared<OoOJoin::MSWJOperator>();
                op_name = "MSWJ";
                break;
            case OperatorType::AI:
                pecj_operator_ = std::make_shared<OoOJoin::AIOperator>();
                op_name = "AI";
                break;
            case OperatorType::LINEAR_SVI:
                pecj_operator_ = std::make_shared<OoOJoin::LinearSVIOperator>();
                op_name = "LinearSVI";
                break;
            case OperatorType::MEAN_AQP:
                pecj_operator_ = std::make_shared<OoOJoin::MeanAQPIAWJOperator>();
                op_name = "MeanAQP";
                break;
            case OperatorType::SHJ:
                pecj_operator_ = std::make_shared<OoOJoin::RawSHJOperator>();
                op_name = "SHJ";
                break;
            case OperatorType::PRJ:
                pecj_operator_ = std::make_shared<OoOJoin::RawPRJOperator>();
                op_name = "PRJ";
                break;
            default:
                // Fallback to IMA
                pecj_operator_ = std::make_shared<OoOJoin::IMAIAWJOperator>();
                op_name = "IMA";
                break;
        }
        
        if (!pecj_operator_) {
            std::cerr << "Failed to create PECJ operator: " << op_name << std::endl;
            return false;
        }
        
        // Configure operator
        // NOTE: Match Integrated Mode's initialize() behavior:
        // - Call setConfig() to configure operator
        // - Call setWindow() to set initial window parameters
        // - Call setBufferLen() to set buffer lengths
        // - Do NOT call syncTimeStruct() here - it will be called in restartOperator()
        //   This matches Integrated Mode which doesn't call syncTimeStruct() in initialize()
        pecj_operator_->setConfig(pecj_config_);
        pecj_operator_->setWindow(window_config_.window_len_us, window_config_.slide_len_us);
        pecj_operator_->setBufferLen(window_config_.s_buffer_len, window_config_.r_buffer_len);
        // syncTimeStruct() will be called in restartOperator() before start()
        
        std::cout << "  Operator: " << op_name << std::endl;
        std::cout << "  Window: " << window_config_.window_len_us << " us" << std::endl;
        std::cout << "  Slide: " << window_config_.slide_len_us << " us" << std::endl;
        
#else
        // ====== Stub Mode ======
        std::cout << "PECJ Adapter initialized (stub mode)" << std::endl;
        std::cout << "  To enable full PECJ support:" << std::endl;
        std::cout << "  1. Build PECJ with all dependencies" << std::endl;
        std::cout << "  2. cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON .." << std::endl;
        std::cout << "  Configuration (will be used when PECJ is enabled):" << std::endl;
        std::cout << "    Window: " << window_config_.window_len_us << " us" << std::endl;
        std::cout << "    Slide: " << window_config_.slide_len_us << " us" << std::endl;
        std::cout << "    Lateness: " << window_config_.lateness_ms << " ms" << std::endl;
#endif
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in initializePECJ: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Lifecycle Management
// ============================================================================

bool PECJAdapter::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!initialized_.load()) {
        std::cerr << "PECJ Adapter not initialized" << std::endl;
        return false;
    }
    
    if (running_.load()) {
        return true;  // Already running
    }
    
    // NOTE: Do NOT call pecj_operator_->start() here!
    // The operator will be started by restartOperator() with correct window parameters.
    // Calling start() twice (here and in restartOperator) causes state inconsistency
    // because PECJ's start() doesn't fully reset all internal state variables
    // (e.g., max_ratio, alpha in MeanAQPIAWJOperator), leading to different AQP results.
    
    running_.store(true);
    
    // Mode-specific startup
    if (run_mode_ == RunMode::Integrated && resource_handle_) {
        // Integrated mode: submit worker task to ResourceManager
        resource_handle_->submitTask([this]() {
            workerLoop();
        });
        std::cout << "✓ PECJ Adapter started (Integrated mode)" << std::endl;
    } else {
        // Baseline/Stub mode: create independent thread
        worker_thread_ = std::thread(&PECJAdapter::workerLoop, this);
        std::cout << "✓ PECJ Adapter started (Baseline/Stub mode)" << std::endl;
    }
    
    return true;
}

bool PECJAdapter::stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        if (!running_.load()) {
            return true;  // Already stopped
        }
        
        running_.store(false);
    }
    
    // Wake up worker thread
    queue_cv_.notify_all();
    
    // Wait for worker thread
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
#ifdef PECJ_FULL_INTEGRATION
    // Stop PECJ operator
    if (pecj_operator_) {
        pecj_operator_->stop();
    }
#endif
    
    std::cout << "✓ PECJ Adapter stopped" << std::endl;
    return true;
}

void PECJAdapter::reset() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    tuples_processed_s_.store(0);
    tuples_processed_r_.store(0);
    join_results_.store(0);
    total_latency_us_.store(0);
    
#ifdef PECJ_FULL_INTEGRATION
    // Reset timestamp normalization
    min_timestamp_ = 0;
#endif
    
    // Clear data queue
    {
        std::lock_guard<std::mutex> qlock(queue_mutex_);
        std::queue<std::pair<TimeSeriesData, bool>> empty;
        std::swap(data_queue_, empty);
    }
}

bool PECJAdapter::restartOperator(uint64_t window_start, uint64_t window_len) {
#ifdef PECJ_FULL_INTEGRATION
    if (!pecj_operator_) {
        std::cerr << "PECJ operator not initialized" << std::endl;
        return false;
    }
    
    // Reset statistics
    tuples_processed_s_.store(0);
    tuples_processed_r_.store(0);
    total_latency_us_.store(0);
    
    // Reset timestamp normalization for new window
    min_timestamp_ = window_start;
    
    // Update window configuration if provided
    if (window_len > 0) {
        window_config_.window_len_us = window_len;
    }
    
    // Match Integrated Mode's executeWindowJoin() behavior EXACTLY:
    // Integrated Mode reuses the SAME operator instance created in initialize(),
    // and for each window only calls:
    //   1. setWindow(effective_window_len, slide_len_us)
    //   2. syncTimeStruct(tv)
    //   3. start()
    //
    // Integrated Mode does NOT:
    //   - Create new operator instance
    //   - Call setConfig() again
    //   - Call setBufferLen() again
    //
    // This is critical because:
    // - setConfig() recreates the watermark generator which affects timing behavior
    // - Creating new instances may have different initial state
    
    // 1. setWindow() - configure window parameters
    pecj_operator_->setWindow(window_config_.window_len_us, window_config_.slide_len_us);
    
    // 2. syncTimeStruct() - synchronize time reference
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    pecj_operator_->syncTimeStruct(tv);
    
    // 3. start() - reset window state
    if (!pecj_operator_->start()) {
        std::cerr << "Failed to start PECJ operator" << std::endl;
        return false;
    }
    
    return true;
#else
    (void)window_start;
    (void)window_len;
    return true;
#endif
}

// ============================================================================
// Data Feeding
// ============================================================================

void PECJAdapter::feedData(const TimeSeriesData& data) {
    if (!initialized_.load() || !running_.load()) {
        return;
    }
    
    // Determine stream based on data tags
    bool is_s_stream = true;
    if (data.tags.find("stream") != data.tags.end()) {
        is_s_stream = (data.tags.at("stream") == "S");
    } else {
        // Default: use timestamp parity (even->S, odd->R)
        is_s_stream = ((data.timestamp / 1000) % 2 == 0);
    }
    
    // Add to queue for async processing
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        data_queue_.push({data, is_s_stream});
        queue_length_.store(data_queue_.size());
    }
    queue_cv_.notify_one();
}

void PECJAdapter::feedStreamS(const TimeSeriesData& data) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        auto track_tuple = convertToTrackTuple(data, true);
        if (track_tuple) {
            pecj_operator_->feedTupleS(track_tuple);
        }
    }
#else
    (void)data;  // Suppress unused warning in stub mode
#endif
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    tuples_processed_s_.fetch_add(1);
    total_latency_us_.fetch_add(latency);
}

void PECJAdapter::feedStreamR(const TimeSeriesData& data) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        auto track_tuple = convertToTrackTuple(data, false);
        if (track_tuple) {
            pecj_operator_->feedTupleR(track_tuple);
        }
    }
#else
    (void)data;  // Suppress unused warning in stub mode
#endif
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    tuples_processed_r_.fetch_add(1);
    total_latency_us_.fetch_add(latency);
}

// ============================================================================
// Data Conversion
// ============================================================================

std::shared_ptr<OoOJoin::TrackTuple> PECJAdapter::convertToTrackTuple(
    const TimeSeriesData& data, bool is_s_stream) {
    
#ifdef PECJ_FULL_INTEGRATION
    // Extract key from tags or use timestamp hash
    uint64_t key = 0;
    if (data.tags.find("key") != data.tags.end()) {
        key = std::stoull(data.tags.at("key"));
    } else {
        // Use hash of measurement name as key
        if (data.tags.find("measurement") != data.tags.end()) {
            std::hash<std::string> hasher;
            key = hasher(data.tags.at("measurement"));
        }
    }
    
    // Get value - CRITICAL FIX: Match Integrated Mode which uses fields["value"]
    // Instead of data.as_double() which reads from variant (often unset)
    uint64_t value = 0;
    if (data.fields.find("value") != data.fields.end()) {
        try {
            value = static_cast<uint64_t>(std::stod(data.fields.at("value")));
        } catch (...) {
            value = 0;
        }
    } else {
        // Fallback to as_double() for backward compatibility
        value = static_cast<uint64_t>(data.as_double());
    }
    
    // Create TrackTuple
    auto tuple = std::make_shared<OoOJoin::TrackTuple>(key, value);
    
    // CRITICAL FIX: Normalize BOTH eventTime and arrivalTime
    // This matches Integrated Mode behavior where all timestamps start from 0.
    // 
    // PECJ's window logic uses eventTime to determine if tuple is within window bounds.
    // PECJ's watermark logic uses arrivalTime (for ArrivalWM) or eventTime (for LatenessWM).
    // 
    // Without normalization, data timestamps (e.g., 1000000000) would be outside
    // the typical window range (e.g., [0, 10000]).
    if (min_timestamp_ == 0 && data.timestamp > 0) {
        min_timestamp_ = static_cast<uint64_t>(data.timestamp);
    }
    
    uint64_t normalized_time = static_cast<uint64_t>(data.timestamp) - min_timestamp_;
    tuple->eventTime = normalized_time;
    tuple->arrivalTime = normalized_time;
    
    // Set stream ID
    tuple->streamId = is_s_stream ? 0 : 1;
    
    return tuple;
#else
    (void)data;
    (void)is_s_stream;
    return nullptr;
#endif
}

// ============================================================================
// Worker Thread
// ============================================================================

void PECJAdapter::workerLoop() {
    while (running_.load()) {
        std::pair<TimeSeriesData, bool> item;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !data_queue_.empty() || !running_.load();
            });
            
            if (!running_.load() && data_queue_.empty()) {
                break;
            }
            
            if (!data_queue_.empty()) {
                item = data_queue_.front();
                data_queue_.pop();
            } else {
                continue;
            }
        }
        
        // Process data
        if (item.second) {
            feedStreamS(item.first);
        } else {
            feedStreamR(item.first);
        }
        
        // Check for window results periodically
#ifdef PECJ_FULL_INTEGRATION
        if (pecj_operator_) {
            size_t result = pecj_operator_->getResult();
            if (result > join_results_.load()) {
                size_t new_results = result - join_results_.load();
                join_results_.store(result);
                
                double aqp_result = static_cast<double>(pecj_operator_->getAQPResult());
                publishWindowResult(new_results, aqp_result);
            }
        }
#endif
    }
}

// ============================================================================
// Result Publishing
// ============================================================================

void PECJAdapter::publishWindowResult(size_t join_count, double aqp_result) {
    if (event_bus_) {
        // Create result payload
        auto result = std::make_shared<AlgorithmResult>();
        result->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result->metrics["join_count"] = static_cast<double>(join_count);
        result->metrics["aqp_result"] = aqp_result;
        result->metrics["total_processed_s"] = static_cast<double>(tuples_processed_s_.load());
        result->metrics["total_processed_r"] = static_cast<double>(tuples_processed_r_.load());
        
        // Publish event
        Event event(EventType::WINDOW_TRIGGERED, result->timestamp, result, getName());
        event_bus_->publish(event);
    }
}

// ============================================================================
// Processing and Results
// ============================================================================

AlgorithmResult PECJAdapter::process() {
    AlgorithmResult result;
    result.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        result.metrics["join_result"] = static_cast<double>(pecj_operator_->getResult());
        result.metrics["aqp_result"] = static_cast<double>(pecj_operator_->getAQPResult());
        
        // Get time breakdown if available
        auto breakdown = pecj_operator_->getTimeBreakDown();
        if (breakdown) {
            // Add breakdown metrics
            result.metrics["time_breakdown_available"] = 1.0;
        }
    }
#else
    // Stub mode: return mock results
    result.metrics["join_result"] = static_cast<double>(join_results_.load());
    result.metrics["aqp_result"] = join_results_.load() * 1.02;  // Mock 2% overestimate
    result.metrics["stub_mode"] = 1.0;
#endif
    
    result.metrics["tuples_s"] = static_cast<double>(tuples_processed_s_.load());
    result.metrics["tuples_r"] = static_cast<double>(tuples_processed_r_.load());
    
    return result;
}

size_t PECJAdapter::getJoinResult() const {
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        return pecj_operator_->getResult();
    }
#endif
    return join_results_.load();
}

double PECJAdapter::getApproximateResult() const {
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        return static_cast<double>(pecj_operator_->getAQPResult());
    }
#endif
    return join_results_.load() * 1.02;  // Mock result in stub mode
}

std::map<std::string, int64_t> PECJAdapter::getStats() const {
    size_t total_processed = tuples_processed_s_.load() + tuples_processed_r_.load();
    
    return {
        {"tuples_processed_s", static_cast<int64_t>(tuples_processed_s_.load())},
        {"tuples_processed_r", static_cast<int64_t>(tuples_processed_r_.load())},
        {"join_results", static_cast<int64_t>(getJoinResult())},
        {"avg_latency_us", total_processed > 0 ?
            total_latency_us_.load() / static_cast<int64_t>(total_processed) : 0},
        {"queue_size", static_cast<int64_t>(data_queue_.size())}
    };
}

std::map<std::string, int64_t> PECJAdapter::getTimeBreakdown() const {
    std::map<std::string, int64_t> breakdown;
    
#ifdef PECJ_FULL_INTEGRATION
    if (pecj_operator_) {
        auto pecj_breakdown = pecj_operator_->getTimeBreakDown();
        if (pecj_breakdown) {
            // Convert PECJ breakdown to our format
            // This depends on what PECJ provides
        }
    }
#endif
    
    return breakdown;
}

// ============================================================================
// Resource Usage Reporting
// ============================================================================

core::ResourceUsage PECJAdapter::getResourceUsage() const {
    core::ResourceUsage usage;
    
    // Thread usage
    usage.threads_used = (run_mode_ == RunMode::Integrated) ? 
        resource_request_.requested_threads : 1;
    
    // Memory estimation (simplified - should use actual RSS)
    size_t est_memory = data_queue_.size() * sizeof(TimeSeriesData) * 2;
    usage.memory_used_bytes = est_memory;
    
    // Queue length
    usage.queue_length = queue_length_.load();
    
    // Processing metrics
    usage.tuples_processed = tuples_processed_s_.load() + tuples_processed_r_.load();
    
    size_t total = tuples_processed_s_.load() + tuples_processed_r_.load();
    usage.avg_latency_ms = (total > 0) ? 
        (total_latency_us_.load() / static_cast<double>(total)) / 1000.0 : 0.0;
    
    return usage;
}

// ============================================================================
// Plugin Registration
// ============================================================================

REGISTER_PLUGIN(PECJAdapter, "pecj")

} // namespace plugins
} // namespace sage_tsdb
