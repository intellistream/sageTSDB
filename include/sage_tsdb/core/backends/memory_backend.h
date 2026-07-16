#pragma once

/**
 * @file memory_backend.h
 * @brief In-memory storage backend for sageTSDB (default backend & baseline).
 *
 * MemoryBackend implements ::sage_tsdb::core::IStorageBackend on top of the
 * existing ::sage_tsdb::TimeSeriesIndex. It owns one index per named table,
 * mirroring exactly how TimeSeriesDB manages its `tables_` map today, so that
 * routing TimeSeriesDB through this backend produces identical observable
 * behavior (the D2 regression baseline).
 *
 * It is also the correctness reference for other backends: DamengBackend (D3)
 * must return results matching MemoryBackend for the same inputs.
 *
 * Registered under the name "memory" (see the .cpp). It is the default backend
 * selected by StorageBackendConfig.
 *
 * @see docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md (D2)
 */

#include "sage_tsdb/core/storage_backend.h"
#include "sage_tsdb/core/time_series_index.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sage_tsdb {
namespace core {

/**
 * @brief IStorageBackend backed by in-memory TimeSeriesIndex instances.
 *
 * Thread safety: table create/drop/list operations are guarded by an internal
 * mutex; per-table reads and writes rely on TimeSeriesIndex's own internal
 * shared_mutex. Concurrent readers and writers on the same table are therefore
 * safe, matching the guarantees TimeSeriesDB relies on today.
 */
class MemoryBackend : public IStorageBackend {
public:
    /**
     * @brief Construct an empty in-memory backend.
     *
     * Provided for direct construction and for the registry factory, which
     * requires a `const StorageBackendConfig&` constructor.
     */
    MemoryBackend() = default;

    /**
     * @brief Construct from a configuration (params are ignored).
     * @param config Backend configuration; MemoryBackend has no parameters.
     *
     * Exists so MemoryBackend can be built uniformly via
     * StorageBackendRegistry::create().
     */
    explicit MemoryBackend(const StorageBackendConfig& config);

    ~MemoryBackend() override = default;

    // ---- Table management ------------------------------------------------

    /// @copydoc IStorageBackend::createTable
    bool createTable(const std::string& name, TableType type) override;

    /// @copydoc IStorageBackend::dropTable
    bool dropTable(const std::string& name) override;

    /// @copydoc IStorageBackend::hasTable
    bool hasTable(const std::string& name) const override;

    /// @copydoc IStorageBackend::listTables
    std::vector<std::string> listTables() const override;

    // ---- Writes ----------------------------------------------------------

    /// @copydoc IStorageBackend::insert
    size_t insert(const std::string& table,
                  const TimeSeriesData& data) override;

    /// @copydoc IStorageBackend::insertBatch
    std::vector<size_t> insertBatch(
        const std::string& table,
        const std::vector<TimeSeriesData>& data_list) override;

    // ---- Queries ---------------------------------------------------------

    /// @copydoc IStorageBackend::query
    std::vector<TimeSeriesData> query(const std::string& table,
                                      const QueryConfig& config) const override;

    // ---- Statistics / lifecycle -----------------------------------------

    /// @copydoc IStorageBackend::size
    size_t size(const std::string& table) const override;

    /// @copydoc IStorageBackend::clear
    void clear(const std::string& table) override;

    /**
     * @copydoc IStorageBackend::flush
     * @note No-op for the in-memory backend; always returns true.
     */
    bool flush() override;

    /// @copydoc IStorageBackend::backendName
    std::string backendName() const override { return "memory"; }

private:
    /**
     * @brief Look up a table's index, or nullptr if the table does not exist.
     * @param name Table name.
     * @return Non-owning pointer to the index, or nullptr.
     * @note Caller must hold @ref tables_mutex_.
     */
    TimeSeriesIndex* findIndex(const std::string& name);

    /// @copydoc findIndex
    const TimeSeriesIndex* findIndex(const std::string& name) const;

    /// Guards the tables_ / table_types_ maps (structure, not per-index data).
    mutable std::mutex tables_mutex_;

    /// table name -> owned in-memory index.
    std::map<std::string, std::unique_ptr<TimeSeriesIndex>> tables_;

    /// table name -> declared table type.
    std::map<std::string, TableType> table_types_;
};

} // namespace core
} // namespace sage_tsdb
