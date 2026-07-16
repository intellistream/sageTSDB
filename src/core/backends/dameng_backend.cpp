/**
 * @file dameng_backend.cpp
 * @brief Skeleton implementation of the Dameng (达梦) DM storage backend (D3).
 *
 * Every method that needs the DM driver is marked `// TODO(DM):` with a precise
 * contract (inputs, outputs, constraints) and fails fast via
 * core::NotImplemented until wired. No method silently falls back to another
 * backend (ADR 0001).
 *
 * This translation unit is compiled into sage_tsdb_core ONLY when
 * -DSAGE_TSDB_ENABLE_DM=ON. When compiled, it registers the "dameng" backend.
 *
 * To wire the real driver, the DM-owning side should:
 *   1. Add the driver include(s) and link flags in CMake (see the guarded block
 *      around SAGE_TSDB_ENABLE_DM), keeping ABI _GLIBCXX_USE_CXX11_ABI=0.
 *   2. Give `struct Impl` real connection/statement handles.
 *   3. Replace each `// TODO(DM):` body with parameterized SQL (never string
 *      concatenation) using the `stsb1` codec for values and microsecond
 *      timestamps, so results match core::MemoryBackend.
 */

#include "sage_tsdb/core/backends/dameng_backend.h"

#include <cstdlib>   // std::getenv
#include <iostream>

namespace sage_tsdb {
namespace core {

// Opaque driver state. Kept empty in the skeleton so the header depends on no
// DM headers. When wiring the driver, put the connection handle / statement
// cache / pool here.
struct DamengBackend::Impl {
    bool connected = false;
    // TODO(DM): add driver-specific handles, e.g.:
    //   dpi_conn_t*  conn;      // for the DPI driver
    //   SQLHDBC      odbc_dbc;  // for the ODBC driver
    //   a mutex guarding the connection/statement cache
};

namespace {
/// Fetch a value from params with a default (small helper for readability).
std::string paramOr(const StorageBackendConfig& cfg, const std::string& key,
                    const std::string& fallback) {
    return cfg.get(key, fallback);
}
}  // namespace

DamengBackend::DamengBackend(const StorageBackendConfig& config)
    : impl_(std::make_unique<Impl>()) {
    // Parse connection parameters. NOTE: the password is resolved from the
    // environment variable NAMED by "password_env" (default "DM_PASSWORD"); the
    // secret itself is never taken from params and never logged.
    params_.host         = paramOr(config, "host", "127.0.0.1");
    params_.port         = paramOr(config, "port", "5236");  // DM default port
    params_.user         = paramOr(config, "user", "SYSDBA");
    params_.schema       = paramOr(config, "schema", "");
    params_.table_prefix = paramOr(config, "table_prefix", "ts_");
    params_.driver       = paramOr(config, "driver", "dpi");

    const std::string pw_env = paramOr(config, "password_env", "DM_PASSWORD");
    if (const char* pw = std::getenv(pw_env.c_str())) {
        params_.password = pw;  // resolved secret; do not log
    }

    // Log connection target WITHOUT the password.
    std::cout << "[DamengBackend] configured driver=" << params_.driver
              << " host=" << params_.host << " port=" << params_.port
              << " user=" << params_.user
              << " schema=" << (params_.schema.empty() ? "<default>" : params_.schema)
              << " table_prefix=" << params_.table_prefix
              << " (password from $" << pw_env << ")" << std::endl;
}

DamengBackend::~DamengBackend() = default;

std::string DamengBackend::physicalTable(const std::string& name) const {
    return params_.table_prefix + name;
}

void DamengBackend::ensureConnected() const {
    if (impl_->connected) {
        return;
    }
    // TODO(DM): open the connection using params_ (host/port/user/password/
    //   schema) via the selected driver (params_.driver == "dpi" | "odbc").
    //   On success set impl_->connected = true. On a genuine connection failure
    //   throw std::runtime_error (NOT NotImplemented) so it is distinguishable
    //   from this not-yet-wired state.
    throw NotImplemented(
        "DamengBackend: DM driver not wired (ensureConnected). "
        "Fill the // TODO(DM) in src/core/backends/dameng_backend.cpp.");
}

// ---- Table management ---------------------------------------------------

bool DamengBackend::createTable(const std::string& name, TableType /*type*/) {
    if (name.empty()) {
        throw std::runtime_error("DamengBackend::createTable: empty table name");
    }
    ensureConnected();
    // TODO(DM): issue a parameterized "CREATE TABLE IF NOT EXISTS
    //   <physicalTable(name)> (...)" DDL. Columns per docs table mapping:
    //   ts BIGINT (microseconds, indexed), value VARBINARY (stsb1), tags/fields
    //   as columns or CLOB(JSON). Return false if it already existed, true if
    //   created (match IStorageBackend::createTable semantics).
    throw NotImplemented("DamengBackend::createTable not wired");
}

bool DamengBackend::dropTable(const std::string& name) {
    if (name.empty()) {
        throw std::runtime_error("DamengBackend::dropTable: empty table name");
    }
    ensureConnected();
    // TODO(DM): "DROP TABLE IF EXISTS <physicalTable(name)>". Return true if it
    //   existed and was dropped, false otherwise.
    throw NotImplemented("DamengBackend::dropTable not wired");
}

bool DamengBackend::hasTable(const std::string& name) const {
    if (name.empty()) {
        return false;
    }
    ensureConnected();
    // TODO(DM): query the DM catalog (e.g. USER_TABLES/ALL_TABLES) for
    //   physicalTable(name); return whether it exists.
    throw NotImplemented("DamengBackend::hasTable not wired");
}

std::vector<std::string> DamengBackend::listTables() const {
    ensureConnected();
    // TODO(DM): query the catalog for tables matching params_.table_prefix and
    //   return the LOGICAL names (strip the prefix), so the names round-trip
    //   with createTable()/query() at the TimeSeriesDB layer.
    throw NotImplemented("DamengBackend::listTables not wired");
}

// ---- Writes -------------------------------------------------------------

size_t DamengBackend::insert(const std::string& table, const TimeSeriesData& data) {
    if (table.empty()) {
        throw std::runtime_error("DamengBackend::insert: empty table name");
    }
    (void)data;
    ensureConnected();
    // TODO(DM): parameterized INSERT into physicalTable(table). Encode
    //   data.value with the stsb1 codec, store data.timestamp as microseconds,
    //   bind tags/fields. Return a row index consistent with
    //   IStorageBackend::insert (e.g. the pre-insert row count of the table).
    throw NotImplemented("DamengBackend::insert not wired");
}

std::vector<size_t> DamengBackend::insertBatch(
    const std::string& table, const std::vector<TimeSeriesData>& data_list) {
    if (table.empty()) {
        throw std::runtime_error("DamengBackend::insertBatch: empty table name");
    }
    (void)data_list;
    ensureConnected();
    // TODO(DM): performance-critical path. Use array/bulk parameter binding in a
    //   single transaction rather than per-row INSERTs. Return one row index per
    //   input element, aligned with data_list order.
    throw NotImplemented("DamengBackend::insertBatch not wired");
}

// ---- Queries ------------------------------------------------------------

std::vector<TimeSeriesData> DamengBackend::query(
    const std::string& table, const QueryConfig& config) const {
    if (table.empty()) {
        throw std::runtime_error("DamengBackend::query: empty table name");
    }
    (void)config;
    ensureConnected();
    // TODO(DM): SELECT from physicalTable(table) WHERE ts BETWEEN
    //   config.time_range.start_time AND end_time (INCLUSIVE, microseconds),
    //   pushing tag filters into the WHERE clause and applying config.limit
    //   (limit <= 0 means no limit). Decode value via the stsb1 codec. Ordering
    //   and results MUST match core::MemoryBackend for the same inputs.
    throw NotImplemented("DamengBackend::query not wired");
}

// ---- Statistics / lifecycle --------------------------------------------

size_t DamengBackend::size(const std::string& table) const {
    if (table.empty()) {
        return 0;
    }
    ensureConnected();
    // TODO(DM): "SELECT COUNT(*) FROM physicalTable(table)". Return 0 if the
    //   table does not exist (match IStorageBackend::size).
    throw NotImplemented("DamengBackend::size not wired");
}

void DamengBackend::clear(const std::string& table) {
    if (table.empty()) {
        return;
    }
    ensureConnected();
    // TODO(DM): "TRUNCATE TABLE physicalTable(table)" (or DELETE) to remove all
    //   rows while keeping the table. No-op if the table does not exist.
    throw NotImplemented("DamengBackend::clear not wired");
}

bool DamengBackend::flush() {
    // TODO(DM): commit the current transaction so writes become durable/visible.
    //   If autocommit is used, this may simply return true.
    throw NotImplemented("DamengBackend::flush not wired");
}

} // namespace core
} // namespace sage_tsdb

// Register the "dameng" backend. Only compiled/registered when the translation
// unit is built (SAGE_TSDB_ENABLE_DM=ON), so disabling the option makes
// StorageBackendRegistry::create("dameng") throw "unknown backend".
REGISTER_STORAGE_BACKEND("dameng", ::sage_tsdb::core::DamengBackend)
