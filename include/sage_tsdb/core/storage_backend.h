#pragma once

/**
 * @file storage_backend.h
 * @brief Pluggable storage backend contract for sageTSDB.
 *
 * This header defines the seam through which sageTSDB can persist and query
 * time series data against different storage engines. Today the data path is
 * hard-wired to an in-memory index; this contract abstracts it so the same
 * TimeSeriesDB read/write API can be served by:
 *
 *   - MemoryBackend  : the existing in-memory TimeSeriesIndex (default, also
 *                      the regression baseline). Delivered in D2.
 *   - DamengBackend  : an adapter to the 达梦 (Dameng) DM enterprise database.
 *                      Skeleton with `// TODO(DM):` fill points delivered in
 *                      D3; connection/SQL code is filled by the DM-owning side.
 *
 * Design boundaries:
 *   - A backend is responsible ONLY for "table x time-series record" storage
 *     and retrieval. It does NOT perform computation, window scheduling, or
 *     resource management (see docs/adr/0001-boundary-and-mode-policy.md).
 *   - No silent fallback between backends. Backend selection is explicit; a
 *     backend that cannot service a call must fail-fast with a clear error,
 *     never silently degrade to another backend.
 *
 * Contract invariants (see docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md):
 *   - Time unit is MICROSECONDS. All timestamps, TimeRange bounds and
 *     QueryConfig.window_size are microseconds. TimeRange is inclusive on both
 *     ends.
 *   - Vector/scalar values are encoded with the `stsb1` byte format when a
 *     backend stores them as an opaque blob (see the plan, section 3.6).
 *
 * @see docs/STORAGE_BACKEND_CONTRACT.md
 * @see docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md
 */

#include "time_series_data.h"

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sage_tsdb {
namespace core {

/**
 * @brief Exception thrown by a backend method that has not been implemented.
 *
 * Used primarily by the DamengBackend skeleton (D3) to fail-fast at every
 * `// TODO(DM):` fill point until the DM-owning side wires in the driver.
 * Distinct from runtime connection/query errors so tests and callers can tell
 * "not wired yet" apart from "the database rejected the operation".
 */
class NotImplemented : public std::logic_error {
public:
    /**
     * @brief Construct with a human-readable description of the missing piece.
     * @param what_arg Message, e.g. "DamengBackend::insert - DM driver not wired".
     */
    explicit NotImplemented(const std::string& what_arg)
        : std::logic_error(what_arg) {}
};

/**
 * @brief Configuration used to construct and select a storage backend.
 *
 * The `backend` field selects the implementation (e.g. "memory", "dameng").
 * All backend-specific settings (DSN, host, port, user, schema, ...) are
 * passed as string key-value pairs in `params` so the contract stays
 * backend-agnostic and stable across backends.
 *
 * @note Credentials (e.g. passwords) MUST NOT be hard-coded. Prefer reading
 *       them from environment variables or a secured config file; when passed
 *       via `params`, treat the values as secret (do not log them).
 */
struct StorageBackendConfig {
    /// Backend identifier, e.g. "memory" (default) or "dameng".
    std::string backend = "memory";

    /// Backend-specific parameters as key-value pairs (connection info, etc.).
    std::map<std::string, std::string> params;

    /**
     * @brief Look up a parameter with a default.
     * @param key Parameter name.
     * @param fallback Value returned when @p key is absent.
     * @return The parameter value, or @p fallback if not present.
     */
    std::string get(const std::string& key,
                    const std::string& fallback = "") const {
        auto it = params.find(key);
        return it == params.end() ? fallback : it->second;
    }
};

/**
 * @brief Abstract storage backend for time series tables.
 *
 * A backend stores records (::sage_tsdb::TimeSeriesData) organized into named
 * tables and answers time-range/tag queries. It is the single seam that lets
 * TimeSeriesDB run on top of the in-memory index or an external enterprise
 * database without any change to the compute engines (PECJ, SRTFD), which only
 * ever call TimeSeriesDB::query / insert / createTable.
 *
 * Method contracts common to all backends:
 *   - Time unit is MICROSECONDS; TimeRange bounds are inclusive.
 *   - Table names are backend-neutral identifiers (e.g. "stream_s"). A backend
 *     may map them to a physical name (e.g. a prefixed table) internally.
 *   - Methods should be safe to call concurrently from multiple threads;
 *     implementations document their exact guarantees.
 *   - On unrecoverable errors, throw (std::runtime_error for backend/driver
 *     failures, NotImplemented for not-yet-wired skeleton methods) rather than
 *     silently degrading. No cross-backend fallback.
 */
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    // ---- Table management ------------------------------------------------

    /**
     * @brief Create a named table of the given type.
     * @param name Table name (e.g. "stream_s").
     * @param type Table category used for storage/query specialization.
     * @return true if the table was created; false if it already exists.
     * @note Idempotency: returning false for an existing table is expected and
     *       is not an error.
     */
    virtual bool createTable(const std::string& name, TableType type) = 0;

    /**
     * @brief Drop a table and all of its data.
     * @param name Table name.
     * @return true if the table existed and was dropped; false otherwise.
     */
    virtual bool dropTable(const std::string& name) = 0;

    /**
     * @brief Check whether a table exists.
     * @param name Table name.
     * @return true if the table exists.
     */
    virtual bool hasTable(const std::string& name) const = 0;

    /**
     * @brief List all table names known to this backend.
     * @return Table names in unspecified order.
     */
    virtual std::vector<std::string> listTables() const = 0;

    // ---- Writes ----------------------------------------------------------

    /**
     * @brief Insert a single record into a table.
     * @param table Target table name.
     * @param data Record to insert (timestamp in microseconds).
     * @return A backend-assigned row index for the inserted record, matching
     *         the return-value semantics of TimeSeriesDB::insert.
     * @throws std::runtime_error if the table does not exist or the write fails.
     */
    virtual size_t insert(const std::string& table,
                          const TimeSeriesData& data) = 0;

    /**
     * @brief Insert a batch of records into a table.
     * @param table Target table name.
     * @param data_list Records to insert, in order.
     * @return Row indices for the inserted records, aligned with @p data_list.
     * @throws std::runtime_error if the table does not exist or the write fails.
     * @note Batch is the performance-critical write path; backends backed by an
     *       external database should use parameterized bulk binding here.
     */
    virtual std::vector<size_t> insertBatch(
        const std::string& table,
        const std::vector<TimeSeriesData>& data_list) = 0;

    // ---- Queries ---------------------------------------------------------

    /**
     * @brief Query records from a table.
     * @param table Source table name.
     * @param config Query configuration: time range (inclusive, microseconds),
     *               tag filters, aggregation, window size and result limit.
     * @return Matching records. Ordering and aggregation follow the same
     *         semantics as the in-memory MemoryBackend, which is the reference
     *         implementation for correctness.
     * @throws std::runtime_error if the table does not exist or the query fails.
     * @note Tag filtering and time-range selection SHOULD be pushed down to the
     *       underlying store where possible; the observable result must match
     *       MemoryBackend regardless.
     */
    virtual std::vector<TimeSeriesData> query(
        const std::string& table,
        const QueryConfig& config) const = 0;

    // ---- Statistics / lifecycle -----------------------------------------

    /**
     * @brief Number of records currently stored in a table.
     * @param table Table name.
     * @return Record count; 0 if the table is empty or does not exist.
     */
    virtual size_t size(const std::string& table) const = 0;

    /**
     * @brief Remove all records from a table without dropping the table.
     * @param table Table name.
     */
    virtual void clear(const std::string& table) = 0;

    /**
     * @brief Flush pending writes so they become durable/visible.
     * @return true on success.
     * @note For in-memory backends this may be a no-op returning true. For
     *       external databases this typically commits the current transaction.
     */
    virtual bool flush() = 0;

    /**
     * @brief Human-readable backend identifier, e.g. "memory" or "dameng".
     * @return Backend name, primarily for logging and explicit mode checks.
     */
    virtual std::string backendName() const = 0;
};

/// Convenience alias for an owned storage backend instance.
using StorageBackendPtr = std::unique_ptr<IStorageBackend>;

/// Factory callable that constructs a backend from a configuration.
using StorageBackendCreator =
    std::function<StorageBackendPtr(const StorageBackendConfig&)>;

/**
 * @brief Registry that maps backend names to their factory functions.
 *
 * Backends register a creator under a name (e.g. "memory", "dameng"); callers
 * construct a backend by name via create(). Selection is always explicit: an
 * unknown name throws rather than falling back to any default backend.
 *
 * Header-only Meyers singleton so D1 introduces no new build targets. Thread
 * safety: register your backends during startup before concurrent create()
 * calls (registration is not synchronized).
 */
class StorageBackendRegistry {
public:
    /**
     * @brief Access the process-wide registry instance.
     * @return Reference to the singleton registry.
     */
    static StorageBackendRegistry& instance() {
        static StorageBackendRegistry reg;
        return reg;
    }

    /**
     * @brief Register a backend creator under a name.
     * @param name Backend identifier (e.g. "memory"). Re-registering a name
     *             overwrites the previous creator.
     * @param creator Factory that builds the backend from a config.
     */
    void registerBackend(const std::string& name,
                         StorageBackendCreator creator) {
        creators_[name] = std::move(creator);
    }

    /**
     * @brief Check whether a backend name is registered.
     * @param name Backend identifier.
     * @return true if a creator is registered for @p name.
     */
    bool isRegistered(const std::string& name) const {
        return creators_.find(name) != creators_.end();
    }

    /**
     * @brief List all registered backend names.
     * @return Registered names in unspecified order.
     */
    std::vector<std::string> registeredBackends() const {
        std::vector<std::string> names;
        names.reserve(creators_.size());
        for (const auto& kv : creators_) names.push_back(kv.first);
        return names;
    }

    /**
     * @brief Construct a backend from a configuration.
     * @param config Configuration whose `backend` field selects the creator.
     * @return An owned backend instance.
     * @throws std::runtime_error if `config.backend` is not registered. There
     *         is deliberately no fallback to a default backend.
     */
    StorageBackendPtr create(const StorageBackendConfig& config) const {
        auto it = creators_.find(config.backend);
        if (it == creators_.end()) {
            throw std::runtime_error(
                "StorageBackendRegistry: unknown backend '" + config.backend +
                "'. Registered: [" + joinRegistered() + "]");
        }
        return it->second(config);
    }

private:
    StorageBackendRegistry() = default;

    /// Build a comma-separated list of registered names for error messages.
    std::string joinRegistered() const {
        std::string out;
        for (const auto& kv : creators_) {
            if (!out.empty()) out += ", ";
            out += kv.first;
        }
        return out;
    }

    std::map<std::string, StorageBackendCreator> creators_;
};

/**
 * @brief Register a storage backend at static-initialization time.
 *
 * Place at file scope in a backend's .cpp, e.g.:
 * @code
 *   REGISTER_STORAGE_BACKEND("memory", MemoryBackend);
 * @endcode
 * The backend class must be constructible from `const StorageBackendConfig&`.
 *
 * @param name_literal String literal name to register under.
 * @param BackendClass Concrete IStorageBackend subclass to construct.
 */
#define REGISTER_STORAGE_BACKEND(name_literal, BackendClass)                  \
    namespace {                                                               \
    const bool BackendClass##_storage_backend_registered = [] {              \
        ::sage_tsdb::core::StorageBackendRegistry::instance().registerBackend(\
            name_literal,                                                     \
            [](const ::sage_tsdb::core::StorageBackendConfig& cfg)           \
                -> ::sage_tsdb::core::StorageBackendPtr {                     \
                return std::make_unique<BackendClass>(cfg);                   \
            });                                                               \
        return true;                                                          \
    }();                                                                      \
    }

} // namespace core
} // namespace sage_tsdb
