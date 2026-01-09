# PECJ Operators Integration Guide

## Overview

sageTSDB now supports calling all PECJ operators through the `PECJComputeEngine`. This document describes the available operators and how to use them.

## Supported Operators

### 1. IAWJ (Intra-Window Join)
- **Tag**: `"IAWJ"`
- **Description**: The intra-window join operator, only considers a single window
- **Features**: Basic nested loop join within a single window
- **Use Case**: Simple window joins without AQP support
- **Reference**: `Operator/IAWJOperator.h`

### 2. MeanAQP (Mean-based AQP)
- **Tag**: `"MeanAQP"`
- **Description**: IAWJ operator under the simplest AQP strategy using exponential weighted moving average for prediction
- **Features**: 
  - AQP support via `getAQPResult()`
  - Time breakdown information
- **Use Case**: When approximate results are acceptable for faster processing
- **Reference**: `Operator/MeanAQPIAWJOperator.h`

### 3. IMA (Incremental Moving Average)
- **Tag**: `"IMA"`
- **Description**: IAWJ operator with incremental updates and AQP support (EAGER join)
- **Features**:
  - Incremental result updates
  - AQP with compensation
  - Can disable compensation via `imaDisableCompensation` config
- **Use Case**: Real-time join processing with approximate early results
- **Reference**: `Operator/IMAIAWJOperator.h`

### 4. MSWJ (Multi-Stream Window Join)
- **Tag**: `"MSWJ"`
- **Description**: Multi-stream window join operator based on ICDE2016 paper
- **Features**:
  - K-Slack buffering
  - Stream synchronization
  - Buffer size management
  - Optional linear compensation via `mswjCompensation` config
- **Use Case**: Complex multi-stream scenarios with out-of-order data
- **Reference**: `Operator/MSWJOperator.h`

### 5. IAWJSel (IAWJ with Selectivity)
- **Tag**: `"IAWJSel"`
- **Description**: IAWJ operator with coarse-grained selectivity-based AQP strategy
- **Features**:
  - Selectivity-based compensation
  - AQP support
- **Use Case**: When selectivity varies across windows
- **Reference**: `Operator/IAWJSelOperator.h`

### 6. LazyIAWJSel (Lazy IAWJ with Selectivity)
- **Tag**: `"LazyIAWJSel"`
- **Description**: Lazy evaluation PECJ join with selectivity
- **Features**:
  - Lazy join evaluation
  - Lazy statistics gathering
  - Throughput monitoring via `getLazyRunningThroughput()`
- **Use Case**: When delayed processing is preferred for better throughput
- **Reference**: `Operator/LazyIAWJSelOperator.h`

### 7. AI (AI-based Operator)
- **Tag**: `"AI"`
- **Description**: AI-based operator for advanced prediction
- **Features**: Machine learning based compensation
- **Use Case**: Complex prediction scenarios
- **Reference**: `Operator/AIOperator.h`

### 8. LinearSVI (Linear Stochastic Variational Inference)
- **Tag**: `"LinearSVI"`
- **Description**: Operator using linear stochastic variational inference
- **Features**: Statistical inference based compensation
- **Use Case**: Probabilistic join processing
- **Reference**: `Operator/LinearSVIOperator.h`

### 9. SHJ (Symmetric Hash Join)
- **Tag**: `"SHJ"`
- **Description**: Raw symmetric hash join baseline
- **Features**: Standard hash join implementation
- **Use Case**: Baseline comparison
- **Reference**: `Operator/RawSHJOperator.h`

### 10. PRJ (Progressive Join)
- **Tag**: `"PRJ"`
- **Description**: Raw progressive join baseline
- **Features**: Progressive result generation
- **Use Case**: Baseline comparison
- **Reference**: `Operator/RawPRJOperator.h`

### 11. PECJ (Full PECJ)
- **Tag**: `"PECJ"` or `"PEC"`
- **Description**: Full PECJ operator with all optimizations
- **Features**: Complete PECJ algorithm
- **Use Case**: Production use with optimal performance
- **Reference**: `Operator/PECJOperator.h`

## Configuration

### ComputeConfig Parameters

```cpp
struct ComputeConfig {
    // Window parameters
    uint64_t window_len_us = 1000000;     // Window length in microseconds
    uint64_t slide_len_us = 500000;       // Slide length in microseconds
    
    // Algorithm parameters
    std::string operator_type = "IAWJ";   // Operator type string tag
    PECJOperatorType operator_enum;       // Operator type enum
    uint64_t max_delay_us = 100000;       // Maximum allowed delay
    double aqp_threshold = 0.05;          // AQP error threshold (5%)
    
    // PECJ-specific parameters
    uint64_t s_buffer_len = 100000;       // S buffer size
    uint64_t r_buffer_len = 100000;       // R buffer size
    uint64_t time_step_us = 1000;         // Simulation time step
    std::string watermark_tag = "arrival"; // Watermark generator (arrival/lateness)
    uint64_t watermark_time_ms = 100;     // Watermark time for ArrivalWM
    uint64_t lateness_ms = 50;            // Max lateness for LatenessWM
    
    // IMA/PECJ specific
    bool ima_disable_compensation = false; // Disable compensation in IMA
    
    // MSWJ specific
    bool mswj_compensation = false;        // Enable linear compensation in MSWJ
    
    // ... other parameters ...
};
```

## Usage Example

```cpp
#include "sage_tsdb/compute/pecj_compute_engine.h"

// Create configuration
sage_tsdb::compute::ComputeConfig config;
config.operator_type = "IMA";  // Use Incremental Moving Average operator
config.window_len_us = 1000000;  // 1 second window
config.slide_len_us = 500000;    // 500ms slide
config.enable_aqp = true;

// Initialize compute engine
sage_tsdb::compute::PECJComputeEngine engine;
engine.initialize(config, db, resource_handle);

// Execute window join
sage_tsdb::compute::TimeRange range(start_us, end_us);
auto status = engine.executeWindowJoin(window_id, range);

// Check results
if (status.success) {
    std::cout << "Join count: " << status.join_count << std::endl;
    if (status.used_aqp) {
        std::cout << "AQP estimate: " << status.aqp_estimate << std::endl;
        std::cout << "AQP error: " << (status.aqp_error * 100) << "%" << std::endl;
    }
}
```

## Window Types

sageTSDB now supports additional window types to match PECJ capabilities:

```cpp
enum class WindowType {
    Tumbling,       // Non-overlapping windows
    Sliding,        // Overlapping windows with slide interval
    Session,        // Session windows (gap-based)
    IntraWindow,    // Intra-window join (IAWJ) - single window only
    MultiStream     // Multi-stream window join (MSWJ)
};

enum class JoinSemantics {
    Eager,          // Immediate join on tuple arrival
    Lazy,           // Delay join until window completion
    AQP             // Approximate query processing
};
```

## AQP Support

Operators that support AQP (Approximate Query Processing):
- MeanAQP
- IMA
- MSWJ
- IAWJSel
- LazyIAWJSel
- PECJ

Use `operatorSupportsAQP(PECJOperatorType type)` to check if an operator supports AQP.

## References

- PECJ source code: `/home/cdb/dameng/PECJ/include/Operator/`
- PECJ OperatorTable: `/home/cdb/dameng/PECJ/src/Operator/OperatorTable.cpp`
