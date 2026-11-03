#include <sage_tsdb/c_api/pecj.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(sage_pecj_handle* handle, const char* operation, int32_t code) {
    const uint64_t required = sage_pecj_last_error(handle, NULL, 0);
    char* message = (char*)malloc((size_t)required);
    if (message != NULL) {
        sage_pecj_last_error(handle, message, required);
        fprintf(stderr, "%s failed (%" PRId32 "): %s\n", operation, code, message);
        free(message);
    } else {
        fprintf(stderr, "%s failed (%" PRId32 ")\n", operation, code);
    }
    return 1;
}

int main(void) {
    const sage_pecj_record_v1 stream_s[] = {
        {1000000, 1, 10.0},
        {1100000, 2, 20.0},
        {1200000, 1, 30.0},
    };
    const sage_pecj_record_v1 stream_r[] = {
        {1050000, 1, 40.0},
        {1150000, 3, 50.0},
        {1250000, 1, 60.0},
    };

    sage_pecj_config_v1 config;
    sage_pecj_result_v1 result;
    sage_pecj_handle* handle = NULL;
    int32_t status = sage_pecj_config_v1_init(&config);
    if (status != SAGE_PECJ_OK) {
        return fail(NULL, "config init", status);
    }
    config.window_len_us = 500000;
    config.slide_len_us = 500000;

    status = sage_pecj_create(&config, &handle);
    if (status != SAGE_PECJ_OK) {
        return fail(NULL, "create", status);
    }
    status = sage_pecj_insert_s(handle, stream_s, 3);
    if (status == SAGE_PECJ_OK) {
        status = sage_pecj_insert_r(handle, stream_r, 3);
    }
    if (status != SAGE_PECJ_OK) {
        const int rc = fail(handle, "insert", status);
        sage_pecj_destroy(handle);
        return rc;
    }

    sage_pecj_result_v1_init(&result);
    status = sage_pecj_execute(handle, 1, 1000000, 1300001, &result);
    if (status != SAGE_PECJ_OK) {
        const int rc = fail(handle, "execute", status);
        sage_pecj_destroy(handle);
        return rc;
    }

    printf("version=%s abi=%" PRIu32 " join_count=%" PRIu64 "\n",
           sage_pecj_component_version(), sage_pecj_abi_version(), result.join_count);

    if (result.join_count != 4 || sage_pecj_clear(handle) != SAGE_PECJ_OK) {
        sage_pecj_destroy(handle);
        return 2;
    }

    status = sage_pecj_insert_s(handle, stream_s, 1);
    if (status == SAGE_PECJ_OK) {
        status = sage_pecj_insert_r(handle, stream_r, 1);
    }
    sage_pecj_result_v1_init(&result);
    if (status == SAGE_PECJ_OK) {
        status = sage_pecj_execute(handle, 2, 1000000, 1100000, &result);
    }
    if (status != SAGE_PECJ_OK || result.join_count != 1) {
        const int rc = fail(handle, "reuse", status);
        sage_pecj_destroy(handle);
        return rc;
    }

    status = sage_pecj_insert_s(handle, NULL, 1);
    if (status != SAGE_PECJ_ERROR_INVALID_ARGUMENT ||
        sage_pecj_last_error(handle, NULL, 0) <= 1) {
        sage_pecj_destroy(handle);
        return 3;
    }

    printf("reuse_join_count=%" PRIu64 " error_path=ok\n", result.join_count);
    sage_pecj_destroy(handle);
    return 0;
}
