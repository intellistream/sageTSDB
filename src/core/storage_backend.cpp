/**
 * @file storage_backend.cpp
 * @brief Out-of-line definition of the storage backend registry singleton.
 *
 * StorageBackendRegistry::instance() is defined here (not inline in the header)
 * so the process has exactly ONE registry instance shared across the core
 * shared library and any executable, plugin, or Python extension that links it.
 * With an inline definition, each module would get its own function-local
 * static, and backend registrations performed inside libsage_tsdb_core would be
 * invisible to a caller living in another module.
 */

#include "sage_tsdb/core/storage_backend.h"

namespace sage_tsdb {
namespace core {

StorageBackendRegistry& StorageBackendRegistry::instance() {
    static StorageBackendRegistry reg;
    return reg;
}

} // namespace core
} // namespace sage_tsdb
