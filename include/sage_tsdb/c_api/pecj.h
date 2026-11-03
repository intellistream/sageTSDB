#ifndef SAGE_TSDB_C_API_PECJ_H
#define SAGE_TSDB_C_API_PECJ_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SAGE_TSDB_PECJ_C_BUILDING)
#    define SAGE_PECJ_API __declspec(dllexport)
#  else
#    define SAGE_PECJ_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define SAGE_PECJ_API __attribute__((visibility("default")))
#else
#  define SAGE_PECJ_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SAGE_PECJ_ABI_VERSION_V1 UINT32_C(1)

#define SAGE_PECJ_OK INT32_C(0)
#define SAGE_PECJ_ERROR_INVALID_ARGUMENT INT32_C(1)
#define SAGE_PECJ_ERROR_UNSUPPORTED_ABI INT32_C(2)
#define SAGE_PECJ_ERROR_OUT_OF_MEMORY INT32_C(3)
#define SAGE_PECJ_ERROR_INITIALIZATION INT32_C(4)
#define SAGE_PECJ_ERROR_COMPUTE INT32_C(5)
#define SAGE_PECJ_ERROR_INTERNAL INT32_C(6)

#define SAGE_PECJ_RESULT_USED_AQP UINT32_C(1)
#define SAGE_PECJ_RESULT_TIMEOUT UINT32_C(2)

typedef struct sage_pecj_handle sage_pecj_handle;

typedef struct sage_pecj_config_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* operator_name;
    const char* watermark_name;
    uint64_t window_len_us;
    uint64_t slide_len_us;
    uint64_t max_delay_us;
    uint64_t s_buffer_len;
    uint64_t r_buffer_len;
    uint64_t time_step_us;
    uint64_t watermark_time_ms;
    uint64_t lateness_ms;
    uint64_t max_memory_bytes;
    uint64_t timeout_ms;
    double aqp_threshold;
    int32_t max_threads;
    uint32_t enable_aqp;
    uint32_t enable_simd;
    uint32_t join_sum;
    uint32_t ima_disable_compensation;
    uint32_t mswj_compensation;
} sage_pecj_config_v1;

typedef struct sage_pecj_record_v1 {
    int64_t timestamp_us;
    uint64_t key;
    double value;
} sage_pecj_record_v1;

typedef struct sage_pecj_result_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t success;
    uint32_t flags;
    int32_t error_code;
    uint32_t reserved;
    uint64_t window_id;
    uint64_t join_count;
    uint64_t input_s_count;
    uint64_t input_r_count;
    uint64_t memory_used_bytes;
    double aqp_estimate;
    double computation_time_ms;
    double selectivity;
    double aqp_error;
} sage_pecj_result_v1;

SAGE_PECJ_API uint32_t sage_pecj_abi_version(void);
SAGE_PECJ_API const char* sage_pecj_component_version(void);

SAGE_PECJ_API int32_t sage_pecj_config_v1_init(sage_pecj_config_v1* config);
SAGE_PECJ_API int32_t sage_pecj_result_v1_init(sage_pecj_result_v1* result);

SAGE_PECJ_API int32_t sage_pecj_create(
    const sage_pecj_config_v1* config,
    sage_pecj_handle** out_handle);

SAGE_PECJ_API int32_t sage_pecj_insert_s(
    sage_pecj_handle* handle,
    const sage_pecj_record_v1* records,
    uint64_t count);

SAGE_PECJ_API int32_t sage_pecj_insert_r(
    sage_pecj_handle* handle,
    const sage_pecj_record_v1* records,
    uint64_t count);

SAGE_PECJ_API int32_t sage_pecj_execute(
    sage_pecj_handle* handle,
    uint64_t window_id,
    int64_t start_us,
    int64_t end_us,
    sage_pecj_result_v1* result);

SAGE_PECJ_API int32_t sage_pecj_clear(sage_pecj_handle* handle);

/* Returns the bytes required including the trailing NUL. A NULL handle reads
 * the calling thread's most recent create/argument error. */
SAGE_PECJ_API uint64_t sage_pecj_last_error(
    const sage_pecj_handle* handle,
    char* buffer,
    uint64_t buffer_size);

SAGE_PECJ_API void sage_pecj_destroy(sage_pecj_handle* handle);

#ifdef __cplusplus
}
#endif

#endif
