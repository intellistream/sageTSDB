#include "sage_tsdb/c_api/pecj.h"

#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

using sage_tsdb::TableType;
using sage_tsdb::TimeSeriesData;
using sage_tsdb::TimeSeriesDB;
using sage_tsdb::compute::ComputeConfig;
using sage_tsdb::compute::PECJComputeEngine;

struct sage_pecj_handle {
    TimeSeriesDB db;
    PECJComputeEngine engine;
    mutable std::mutex mutex;
    std::string last_error;
};

namespace {

thread_local std::string thread_error;

constexpr const char* kStreamS = "stream_s";
constexpr const char* kStreamR = "stream_r";
constexpr std::string_view kOperators[] = {
    "IAWJ", "MeanAQP", "IMA", "MSWJ", "AI", "LinearSVI",
    "IAWJSel", "LazyIAWJSel", "SHJ", "PRJ", "PECJ"
};

int32_t fail(sage_pecj_handle* handle, int32_t code, std::string message) noexcept {
    try {
        thread_error = message;
        if (handle != nullptr) {
            handle->last_error = std::move(message);
        }
    } catch (...) {
    }
    return code;
}

void clearError(sage_pecj_handle* handle) noexcept {
    try {
        thread_error.clear();
        if (handle != nullptr) {
            handle->last_error.clear();
        }
    } catch (...) {
    }
}

bool isSupportedOperator(const char* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string_view candidate(value);
    return std::find(std::begin(kOperators), std::end(kOperators), candidate) !=
           std::end(kOperators);
}

bool isSupportedWatermark(const char* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string_view candidate(value);
    return candidate == "arrival" || candidate == "lateness";
}

int32_t validateConfig(const sage_pecj_config_v1* config) {
    if (config == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "config must not be NULL");
    }
    if (config->struct_size != sizeof(sage_pecj_config_v1) ||
        config->abi_version != SAGE_PECJ_ABI_VERSION_V1) {
        return fail(nullptr, SAGE_PECJ_ERROR_UNSUPPORTED_ABI,
                    "unsupported sage_pecj_config_v1 layout or ABI version");
    }
    if (!isSupportedOperator(config->operator_name)) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "unsupported PECJ operator_name");
    }
    if (!isSupportedWatermark(config->watermark_name)) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "watermark_name must be arrival or lateness");
    }
    if (config->window_len_us == 0 || config->slide_len_us == 0 ||
        config->s_buffer_len == 0 || config->r_buffer_len == 0 ||
        config->max_memory_bytes == 0 || config->max_threads <= 0) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "window, slide, buffers, memory, and thread limits must be positive");
    }
    if (config->max_memory_bytes >
        static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "max_memory_bytes exceeds the platform limit");
    }
    return SAGE_PECJ_OK;
}

int32_t insertRecords(sage_pecj_handle* handle,
                      const char* table,
                      const sage_pecj_record_v1* records,
                      uint64_t count) {
    if (handle == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "handle must not be NULL");
    }
    if (count != 0 && records == nullptr) {
        return fail(handle, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "records must not be NULL when count is non-zero");
    }
    if (count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return fail(handle, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "record count exceeds the platform limit");
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    try {
        std::vector<TimeSeriesData> batch;
        batch.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            TimeSeriesData data;
            data.timestamp = records[i].timestamp_us;
            data.value = records[i].value;
            data.tags["key"] = std::to_string(records[i].key);
            data.fields["value"] = std::to_string(records[i].value);
            batch.push_back(std::move(data));
        }
        if (!batch.empty()) {
            handle->db.insertBatch(table, batch);
        }
        clearError(handle);
        return SAGE_PECJ_OK;
    } catch (const std::bad_alloc&) {
        return fail(handle, SAGE_PECJ_ERROR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& error) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, error.what());
    } catch (...) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, "unknown insertion error");
    }
}

}  // namespace

extern "C" {

uint32_t sage_pecj_abi_version(void) {
    return SAGE_PECJ_ABI_VERSION_V1;
}

const char* sage_pecj_component_version(void) {
    return SAGE_TSDB_COMPONENT_VERSION;
}

int32_t sage_pecj_config_v1_init(sage_pecj_config_v1* config) {
    if (config == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "config must not be NULL");
    }
    *config = {};
    config->struct_size = sizeof(*config);
    config->abi_version = SAGE_PECJ_ABI_VERSION_V1;
    config->operator_name = "IAWJ";
    config->watermark_name = "arrival";
    config->window_len_us = 1000000;
    config->slide_len_us = 500000;
    config->max_delay_us = 100000;
    config->s_buffer_len = 100000;
    config->r_buffer_len = 100000;
    config->time_step_us = 1000;
    config->watermark_time_ms = 0;
    config->lateness_ms = 50;
    config->max_memory_bytes = UINT64_C(2) * 1024 * 1024 * 1024;
    config->timeout_ms = 1000;
    config->aqp_threshold = 0.05;
    config->max_threads = 4;
    config->enable_aqp = 1;
    config->enable_simd = 1;
    clearError(nullptr);
    return SAGE_PECJ_OK;
}

int32_t sage_pecj_result_v1_init(sage_pecj_result_v1* result) {
    if (result == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "result must not be NULL");
    }
    *result = {};
    result->struct_size = sizeof(*result);
    result->abi_version = SAGE_PECJ_ABI_VERSION_V1;
    clearError(nullptr);
    return SAGE_PECJ_OK;
}

int32_t sage_pecj_create(const sage_pecj_config_v1* config,
                         sage_pecj_handle** out_handle) {
    if (out_handle == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "out_handle must not be NULL");
    }
    *out_handle = nullptr;
    const int32_t validation = validateConfig(config);
    if (validation != SAGE_PECJ_OK) {
        return validation;
    }

    try {
        auto handle = std::make_unique<sage_pecj_handle>();
        if (!handle->db.createTable(kStreamS, TableType::Stream) ||
            !handle->db.createTable(kStreamR, TableType::Stream)) {
            return fail(nullptr, SAGE_PECJ_ERROR_INITIALIZATION,
                        "failed to create in-memory stream tables");
        }

        ComputeConfig cpp_config;
        cpp_config.operator_type = config->operator_name;
        cpp_config.operator_enum =
            sage_tsdb::compute::stringToOperatorType(cpp_config.operator_type);
        cpp_config.watermark_tag = config->watermark_name;
        cpp_config.window_len_us = config->window_len_us;
        cpp_config.slide_len_us = config->slide_len_us;
        cpp_config.max_delay_us = config->max_delay_us;
        cpp_config.s_buffer_len = config->s_buffer_len;
        cpp_config.r_buffer_len = config->r_buffer_len;
        cpp_config.time_step_us = config->time_step_us;
        cpp_config.watermark_time_ms = config->watermark_time_ms;
        cpp_config.lateness_ms = config->lateness_ms;
        cpp_config.max_memory_bytes = static_cast<size_t>(config->max_memory_bytes);
        cpp_config.timeout_ms = config->timeout_ms;
        cpp_config.aqp_threshold = config->aqp_threshold;
        cpp_config.max_threads = config->max_threads;
        cpp_config.enable_aqp = config->enable_aqp != 0;
        cpp_config.enable_simd = config->enable_simd != 0;
        cpp_config.join_sum = config->join_sum != 0;
        cpp_config.ima_disable_compensation = config->ima_disable_compensation != 0;
        cpp_config.mswj_compensation = config->mswj_compensation != 0;

        if (!handle->engine.initialize(cpp_config, &handle->db, nullptr)) {
            return fail(nullptr, SAGE_PECJ_ERROR_INITIALIZATION,
                        "failed to initialize PECJ compute engine");
        }
        clearError(handle.get());
        *out_handle = handle.release();
        return SAGE_PECJ_OK;
    } catch (const std::bad_alloc&) {
        return fail(nullptr, SAGE_PECJ_ERROR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& error) {
        return fail(nullptr, SAGE_PECJ_ERROR_INITIALIZATION, error.what());
    } catch (...) {
        return fail(nullptr, SAGE_PECJ_ERROR_INTERNAL, "unknown initialization error");
    }
}

int32_t sage_pecj_insert_s(sage_pecj_handle* handle,
                           const sage_pecj_record_v1* records,
                           uint64_t count) {
    return insertRecords(handle, kStreamS, records, count);
}

int32_t sage_pecj_insert_r(sage_pecj_handle* handle,
                           const sage_pecj_record_v1* records,
                           uint64_t count) {
    return insertRecords(handle, kStreamR, records, count);
}

int32_t sage_pecj_execute(sage_pecj_handle* handle,
                          uint64_t window_id,
                          int64_t start_us,
                          int64_t end_us,
                          sage_pecj_result_v1* result) {
    if (handle == nullptr || result == nullptr) {
        return fail(handle, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "handle and result must not be NULL");
    }
    if (result->struct_size != sizeof(sage_pecj_result_v1) ||
        result->abi_version != SAGE_PECJ_ABI_VERSION_V1) {
        return fail(handle, SAGE_PECJ_ERROR_UNSUPPORTED_ABI,
                    "unsupported sage_pecj_result_v1 layout or ABI version");
    }
    if (end_us <= start_us) {
        return fail(handle, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "end_us must be greater than start_us");
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    try {
        const auto status = handle->engine.executeWindowJoin(
            window_id, sage_tsdb::compute::TimeRange(start_us, end_us));
        *result = {};
        result->struct_size = sizeof(*result);
        result->abi_version = SAGE_PECJ_ABI_VERSION_V1;
        result->success = status.success ? 1U : 0U;
        result->flags = (status.used_aqp ? SAGE_PECJ_RESULT_USED_AQP : 0U) |
                        (status.timeout_occurred ? SAGE_PECJ_RESULT_TIMEOUT : 0U);
        result->error_code = status.success ? SAGE_PECJ_OK : SAGE_PECJ_ERROR_COMPUTE;
        result->window_id = status.window_id;
        result->join_count = static_cast<uint64_t>(status.join_count);
        result->input_s_count = static_cast<uint64_t>(status.input_s_count);
        result->input_r_count = static_cast<uint64_t>(status.input_r_count);
        result->memory_used_bytes = static_cast<uint64_t>(status.memory_used_bytes);
        result->aqp_estimate = status.aqp_estimate;
        result->computation_time_ms = status.computation_time_ms;
        result->selectivity = status.selectivity;
        result->aqp_error = status.aqp_error;
        if (!status.success) {
            return fail(handle, SAGE_PECJ_ERROR_COMPUTE, status.error);
        }
        clearError(handle);
        return SAGE_PECJ_OK;
    } catch (const std::bad_alloc&) {
        return fail(handle, SAGE_PECJ_ERROR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& error) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, error.what());
    } catch (...) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, "unknown compute error");
    }
}

int32_t sage_pecj_clear(sage_pecj_handle* handle) {
    if (handle == nullptr) {
        return fail(nullptr, SAGE_PECJ_ERROR_INVALID_ARGUMENT,
                    "handle must not be NULL");
    }
    std::lock_guard<std::mutex> lock(handle->mutex);
    try {
        const bool dropped_s = handle->db.dropTable(kStreamS);
        const bool dropped_r = handle->db.dropTable(kStreamR);
        const bool created_s = handle->db.createTable(kStreamS, TableType::Stream);
        const bool created_r = handle->db.createTable(kStreamR, TableType::Stream);
        if (!dropped_s || !dropped_r || !created_s || !created_r) {
            return fail(handle, SAGE_PECJ_ERROR_INTERNAL,
                        "failed to clear in-memory stream tables");
        }
        handle->engine.reset();
        clearError(handle);
        return SAGE_PECJ_OK;
    } catch (const std::exception& error) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, error.what());
    } catch (...) {
        return fail(handle, SAGE_PECJ_ERROR_INTERNAL, "unknown clear error");
    }
}

uint64_t sage_pecj_last_error(const sage_pecj_handle* handle,
                              char* buffer,
                              uint64_t buffer_size) {
    try {
        std::string message;
        if (handle != nullptr) {
            std::lock_guard<std::mutex> lock(handle->mutex);
            message = handle->last_error;
        } else {
            message = thread_error;
        }
        const uint64_t required = static_cast<uint64_t>(message.size()) + 1;
        if (buffer != nullptr && buffer_size != 0) {
            const uint64_t copy_size = std::min<uint64_t>(message.size(), buffer_size - 1);
            std::memcpy(buffer, message.data(), static_cast<size_t>(copy_size));
            buffer[copy_size] = '\0';
        }
        return required;
    } catch (...) {
        if (buffer != nullptr && buffer_size != 0) {
            buffer[0] = '\0';
        }
        return 1;
    }
}

void sage_pecj_destroy(sage_pecj_handle* handle) {
    try {
        delete handle;
    } catch (...) {
    }
}

}  // extern "C"
