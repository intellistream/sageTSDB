#pragma once

#include "sage_tsdb/core/time_series_data.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace sage_tsdb {

// Forward declaration
class TimeSeriesDB;

namespace compute {

/**
 * @brief Compute state data structure
 * 
 * Represents the serializable state of a compute engine (e.g., PECJ).
 * State includes:
 * - Watermark information
 * - Window progress tracking
 * - Operator-specific internal state
 */
struct ComputeState {
    std::string compute_name;           ///< Compute engine identifier
    int64_t timestamp;                  ///< State snapshot timestamp
    
    // Common state fields
    int64_t watermark;                  ///< Current watermark value
    uint64_t window_id;                 ///< Current window ID
    uint64_t processed_events;          ///< Total events processed
    
    // Operator-specific state (serialized)
    std::vector<uint8_t> operator_state;  ///< Opaque operator state
    
    // Metadata
    std::map<std::string, std::string> metadata;  ///< Additional metadata
    
    ComputeState()
        : timestamp(0), watermark(0), window_id(0), processed_events(0) {}
};

/**
 * @brief Compute state manager
 * 
 * Manages the lifecycle of compute engine states:
 * - Serialization/deserialization
 * - Persistence through LSM-Tree
 * - State versioning and recovery
 * 
 * Design:
 * - States are stored in a special table "_compute_state"
 * - Each compute engine has its own state record
 * - Supports checkpoint-based recovery
 * - Thread-safe for concurrent access
 */
class ComputeStateManager {
public:
    /**
     * @brief Constructor
     * @param db Pointer to TimeSeriesDB for state storage
     */
    explicit ComputeStateManager(TimeSeriesDB* db);
    
    ~ComputeStateManager();
    
    /**
     * @brief Save compute state
     * @param compute_name Compute engine identifier
     * @param state State to save
     * @return true if saved successfully
     * 
     * Note: This writes to in-memory table. Call persistState() for durability.
     */
    bool saveState(const std::string& compute_name, const ComputeState& state);
    
    /**
     * @brief Load compute state
     * @param compute_name Compute engine identifier
     * @param state Output state object
     * @return true if loaded successfully, false if not found
     */
    bool loadState(const std::string& compute_name, ComputeState& state);
    
    /**
     * @brief Check if state exists
     * @param compute_name Compute engine identifier
     * @return true if state exists
     */
    bool hasState(const std::string& compute_name) const;
    
    /**
     * @brief Delete compute state
     * @param compute_name Compute engine identifier
     * @return true if deleted successfully
     */
    bool deleteState(const std::string& compute_name);
    
    /**
     * @brief List all compute engines with saved state
     * @return Vector of compute engine names
     */
    std::vector<std::string> listStates() const;
    
    /**
     * @brief Persist state to disk (through LSM-Tree)
     * @param compute_name Compute engine identifier (empty = all)
     * @return true if persisted successfully
     * 
     * This triggers a flush from MemTable to LSM-Tree Level 0.
     */
    bool persistState(const std::string& compute_name = "");
    
    /**
     * @brief Create a checkpoint of current state
     * @param compute_name Compute engine identifier
     * @param checkpoint_id Checkpoint identifier
     * @return true if checkpoint created successfully
     * 
     * Checkpoints are immutable snapshots stored separately.
     */
    bool createCheckpoint(const std::string& compute_name, uint64_t checkpoint_id);
    
    /**
     * @brief Restore state from checkpoint
     * @param compute_name Compute engine identifier
     * @param checkpoint_id Checkpoint identifier
     * @param state Output state object
     * @return true if restored successfully
     */
    bool restoreCheckpoint(const std::string& compute_name, 
                          uint64_t checkpoint_id,
                          ComputeState& state);
    
    /**
     * @brief List checkpoints for a compute engine
     * @param compute_name Compute engine identifier
     * @return Vector of checkpoint IDs and metadata
     */
    std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> 
    listCheckpoints(const std::string& compute_name) const;
    
    /**
     * @brief Delete a checkpoint
     * @param compute_name Compute engine identifier
     * @param checkpoint_id Checkpoint identifier
     * @return true if deleted successfully
     */
    bool deleteCheckpoint(const std::string& compute_name, uint64_t checkpoint_id);
    
    // ========== Serialization Utilities ==========
    
    /**
     * @brief Serialize ComputeState to bytes
     * @param state State to serialize
     * @return Serialized bytes
     */
    static std::vector<uint8_t> serialize(const ComputeState& state);
    
    /**
     * @brief Deserialize ComputeState from bytes
     * @param data Serialized bytes
     * @param state Output state object
     * @return true if deserialized successfully
     */
    static bool deserialize(const std::vector<uint8_t>& data, ComputeState& state);

private:
    TimeSeriesDB* db_;  ///< Database reference (not owned)
    
    static constexpr const char* STATE_TABLE_NAME = "_compute_state";
    static constexpr const char* CHECKPOINT_TABLE_NAME = "_compute_checkpoint";
    
    // Helper: Convert ComputeState to TimeSeriesData
    TimeSeriesData stateToData(const ComputeState& state) const;
    
    // Helper: Convert TimeSeriesData to ComputeState
    bool dataToState(const TimeSeriesData& data, ComputeState& state) const;
    
    // Helper: Ensure state tables exist
    void ensureTablesExist();
};

} // namespace compute
} // namespace sage_tsdb
