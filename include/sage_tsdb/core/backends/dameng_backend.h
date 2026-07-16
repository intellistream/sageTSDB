#pragma once

/**
 * @file dameng_backend.h
 * @brief Storage backend adapter for the 达梦 (Dameng) DM enterprise database.
 *
 * SKELETON (deliverable D3). This class provides the full IStorageBackend shape
 * for the Dameng adapter: method signatures, parameter validation, error paths,
 * logging points and a PIMPL-hidden connection handle. The actual driver calls
 * (connect / SQL / parameter binding) are marked with `// TODO(DM):` and are
 * filled in by the DM-owning side.
 *
 * Boundary / policy:
 *   - This header intentionally includes NO Dameng driver headers, so the
 *     skeleton always compiles whether or not a real DM driver is present. The
 *     driver handle lives behind a PIMPL (struct Impl) defined in the .cpp.
 *   - Until the TODOs are wired, every data method fails fast by throwing
 *     core::NotImplemented. It NEVER silently falls back to another backend
 *     (ADR 0001). "Not wired yet" is therefore always distinguishable from a
 *     real database error (std::runtime_error).
 *   - Registered under the name "dameng" only when built with
 *     -DSAGE_TSDB_ENABLE_DM=ON. When disabled, StorageBackendRegistry::create()
 *     for "dameng" throws "unknown backend" rather than degrading.
 *
 * Contract invariants (see docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md):
 *   - Time unit is MICROSECONDS; TimeRange is inclusive on both ends.
 *   - Scalar/vector values use the `stsb1` byte format when stored as a blob.
 *   - Results must match core::MemoryBackend for identical inputs.
 *
 * Connection parameters (via StorageBackendConfig::params):
 *   - "host", "port", "user", "schema"     : connection target
 *   - "password_env" (default "DM_PASSWORD"): NAME of the environment variable
 *        holding the password. The password itself is NEVER read from params
 *        and NEVER logged.
 *   - "table_prefix" (default "ts_")        : physical table name prefix
 *   - "driver" (default "dpi")              : "dpi" or "odbc" (see the plan §4)
 *
 * @see docs/STORAGE_BACKEND_CONTRACT.md
 * @see docs/EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md (D3)
 */

#include "sage_tsdb/core/storage_backend.h"

#include <memory>
#include <string>
#include <vector>

namespace sage_tsdb {
namespace core {

/**
 * @brief IStorageBackend adapter for the Dameng DM database (skeleton).
 *
 * Thread safety: intended to be safe for concurrent readers and writers once
 * the driver is wired (the underlying connection/pool must be guarded by the
 * implementation). The skeleton stores only immutable configuration.
 */
class DamengBackend : public IStorageBackend {
public:
    /**
     * @brief Construct from a configuration; does NOT open a connection yet.
     * @param config Backend configuration (see the file header for keys).
     *
     * Parses and validates connection parameters. The actual connection is
     * opened lazily by ensureConnected() on first use, so constructing a
     * DamengBackend never blocks on network I/O.
     */
    explicit DamengBackend(const StorageBackendConfig& config);

    ~DamengBackend() override;

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

    /// @copydoc IStorageBackend::flush
    bool flush() override;

    /// @copydoc IStorageBackend::backendName
    std::string backendName() const override { return "dameng"; }

private:
    /**
     * @brief Resolved connection parameters (immutable after construction).
     * @note `password` holds the resolved secret at runtime only; it is read
     *       from the environment variable named by "password_env" and is never
     *       logged. It is left empty in the skeleton.
     */
    struct ConnectionParams {
        std::string host;
        std::string port;
        std::string user;
        std::string password;       // resolved from env; never logged
        std::string schema;
        std::string table_prefix = "ts_";
        std::string driver = "dpi"; // "dpi" | "odbc"
    };

    /**
     * @brief Open the DB connection if not already open (lazy connect).
     * @throws core::NotImplemented until the driver is wired.
     * @throws std::runtime_error on a real connection failure (once wired).
     * @note Caller-visible connection errors must be std::runtime_error so they
     *       are distinct from the not-yet-implemented NotImplemented signal.
     */
    void ensureConnected() const;

    /**
     * @brief Map a logical table name to its physical DM table name.
     * @param name Logical table name (e.g. "stream_s").
     * @return Physical name with the configured prefix (e.g. "ts_stream_s").
     */
    std::string physicalTable(const std::string& name) const;

    /// Opaque driver state (connection/handles), defined in the .cpp via PIMPL.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    /// Parsed connection parameters.
    ConnectionParams params_;
};

} // namespace core
} // namespace sage_tsdb
