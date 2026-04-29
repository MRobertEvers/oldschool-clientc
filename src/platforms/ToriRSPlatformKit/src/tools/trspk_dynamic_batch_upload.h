#ifndef TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_BATCH_UPLOAD_H
#define TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_BATCH_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pass to sort / upload helpers: which byte range + CPU source field to use. */
#define TRSPK_DYNAMIC_BATCH_STREAM_VBO 0
#define TRSPK_DYNAMIC_BATCH_STREAM_EBO 1

/** Per-row GPU upload metadata; embed at `flush_offset` inside backend flush structs. */
typedef struct TRSPK_DynamicBatchFlushRow
{
    uint32_t vbo_beg_bytes;
    uint32_t vbo_len_bytes;
    const void* vbo_src;
    uint32_t ebo_beg_bytes;
    uint32_t ebo_len_bytes;
    const void* ebo_src;
} TRSPK_DynamicBatchFlushRow;

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

/**
 * Sort `idx` (each entry is a row index). `rows_base` points to row 0; each row is `row_stride`
 * bytes with a `TRSPK_DynamicBatchFlushRow` at `flush_offset` (use offsetof).
 */
void
trspk_dynamic_batch_sort_indices_by_stream(
    const void* rows_base,
    size_t row_stride,
    size_t flush_offset,
    uint32_t* idx,
    int n,
    int stream);

typedef void (*TRSPK_DynamicBatchUploadFn)(
    void* ctx,
    int is_element_array_buffer,
    uintptr_t buffer_id,
    intptr_t offset,
    size_t size,
    const void* data);

/**
 * For sorted `idx`, merge abutting/overlapping runs on the chosen stream and upload each run.
 * Single-row runs upload from row CPU source; multi-row runs memcpy into merge scratch then one
 * upload. `is_element_array_buffer` passed to `upload` equals `stream` (0 = array, 1 = element).
 */
void
trspk_dynamic_batch_upload_merged_subdata_runs(
    TRSPK_DynamicBatchScratch* scratch,
    uint32_t* idx,
    int n,
    const void* rows_base,
    size_t row_stride,
    size_t flush_offset,
    int stream,
    uintptr_t buffer_id,
    TRSPK_DynamicBatchUploadFn upload,
    void* upload_ctx);

#ifdef __cplusplus
}
#endif

#endif
