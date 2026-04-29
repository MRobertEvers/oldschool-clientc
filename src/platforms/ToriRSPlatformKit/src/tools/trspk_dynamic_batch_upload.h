#ifndef TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_BATCH_UPLOAD_H
#define TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_BATCH_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Grow-once merge scratch + sort index buffer for batched buffer subdata flushes. */
typedef struct TRSPK_DynamicBatchScratch
{
    uint8_t* merge;
    size_t merge_bytes;
    uint32_t* sort_idx;
    uint32_t sort_idx_cap;
} TRSPK_DynamicBatchScratch;

bool
trspk_dynamic_batch_scratch_ensure_merge(TRSPK_DynamicBatchScratch* s, size_t need_bytes);
bool
trspk_dynamic_batch_scratch_ensure_sort_idx(TRSPK_DynamicBatchScratch* s, uint32_t n);
void
trspk_dynamic_batch_scratch_destroy(TRSPK_DynamicBatchScratch* s);

/** Sort `idx` (each entry is a row index) by monotonic `key(rows, row_i)`. */
void
trspk_dynamic_batch_sort_indices_by_u32(
    void* rows,
    uint32_t* idx,
    int n,
    uint32_t (*key)(void* rows, uint32_t row_i));

typedef void (*TRSPK_DynamicBatchUploadFn)(
    void* ctx,
    int is_element_array_buffer,
    uintptr_t buffer_id,
    intptr_t offset,
    size_t size,
    const void* data);

/**
 * For sorted `idx`, merge abutting/overlapping [beg, beg+len) runs and upload each run with
 * `upload`. Single-row runs upload from `cpu_src` directly; multi-row runs memcpy into merge
 * scratch then one upload.
 */
void
trspk_dynamic_batch_upload_merged_subdata_runs(
    TRSPK_DynamicBatchScratch* scratch,
    uint32_t* idx,
    int n,
    void* rows,
    uint32_t (*byte_beg)(void* rows, uint32_t ri),
    uint32_t (*byte_len)(void* rows, uint32_t ri),
    const void* (*cpu_src)(void* rows, uint32_t ri),
    uintptr_t buffer_id,
    int is_element_array_buffer,
    TRSPK_DynamicBatchUploadFn upload,
    void* upload_ctx);

#ifdef __cplusplus
}
#endif

#endif
