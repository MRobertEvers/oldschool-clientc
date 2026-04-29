#include "trspk_dynamic_batch_upload.h"

#include <stdlib.h>
#include <string.h>

bool
trspk_dynamic_batch_scratch_ensure_merge(TRSPK_DynamicBatchScratch* s, size_t need_bytes)
{
    if( !s )
        return false;
    if( s->merge_bytes >= need_bytes )
        return true;
    void* p = realloc(s->merge, need_bytes);
    if( !p )
        return false;
    s->merge = (uint8_t*)p;
    s->merge_bytes = need_bytes;
    return true;
}

bool
trspk_dynamic_batch_scratch_ensure_sort_idx(TRSPK_DynamicBatchScratch* s, uint32_t n)
{
    if( !s )
        return false;
    if( s->sort_idx_cap >= n )
        return true;
    void* p = realloc(s->sort_idx, (size_t)n * sizeof(uint32_t));
    if( !p )
        return false;
    s->sort_idx = (uint32_t*)p;
    s->sort_idx_cap = n;
    return true;
}

void
trspk_dynamic_batch_scratch_destroy(TRSPK_DynamicBatchScratch* s)
{
    if( !s )
        return;
    free(s->merge);
    free(s->sort_idx);
    s->merge = NULL;
    s->merge_bytes = 0u;
    s->sort_idx = NULL;
    s->sort_idx_cap = 0u;
}

static const TRSPK_DynamicBatchFlushRow*
trspk_dynamic_batch_flush_row(
    const void* rows_base,
    size_t row_stride,
    size_t flush_offset,
    uint32_t ri)
{
    return (const TRSPK_DynamicBatchFlushRow*)(
        (const uint8_t*)rows_base + (size_t)ri * row_stride + flush_offset);
}

static uint32_t
trspk_dynamic_batch_stream_beg(const TRSPK_DynamicBatchFlushRow* d, int stream)
{
    return stream == TRSPK_DYNAMIC_BATCH_STREAM_VBO ? d->vbo_beg_bytes : d->ebo_beg_bytes;
}

static uint32_t
trspk_dynamic_batch_stream_len(const TRSPK_DynamicBatchFlushRow* d, int stream)
{
    return stream == TRSPK_DYNAMIC_BATCH_STREAM_VBO ? d->vbo_len_bytes : d->ebo_len_bytes;
}

static const void*
trspk_dynamic_batch_stream_src(const TRSPK_DynamicBatchFlushRow* d, int stream)
{
    return stream == TRSPK_DYNAMIC_BATCH_STREAM_VBO ? d->vbo_src : d->ebo_src;
}

void
trspk_dynamic_batch_sort_indices_by_stream(
    const void* rows_base,
    size_t row_stride,
    size_t flush_offset,
    uint32_t* idx,
    int n,
    int stream)
{
    if( !rows_base || !idx || n <= 0 )
        return;
    if( stream != TRSPK_DYNAMIC_BATCH_STREAM_VBO && stream != TRSPK_DYNAMIC_BATCH_STREAM_EBO )
        return;
    for( int i = 1; i < n; ++i )
    {
        const uint32_t key_row = idx[i];
        const TRSPK_DynamicBatchFlushRow* dk =
            trspk_dynamic_batch_flush_row(rows_base, row_stride, flush_offset, key_row);
        const uint32_t kb = trspk_dynamic_batch_stream_beg(dk, stream);
        int j = i - 1;
        while( j >= 0 )
        {
            const TRSPK_DynamicBatchFlushRow* dj = trspk_dynamic_batch_flush_row(
                rows_base, row_stride, flush_offset, idx[j]);
            if( !(trspk_dynamic_batch_stream_beg(dj, stream) > kb) )
                break;
            idx[j + 1] = idx[j];
            --j;
        }
        idx[j + 1] = key_row;
    }
}

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
    void* upload_ctx)
{
    if( !scratch || !idx || n <= 0 || !rows_base || !upload )
        return;
    if( stream != TRSPK_DYNAMIC_BATCH_STREAM_VBO && stream != TRSPK_DYNAMIC_BATCH_STREAM_EBO )
        return;
    int p = 0;
    while( p < n )
    {
        const uint32_t i0 = idx[p];
        const TRSPK_DynamicBatchFlushRow* d0 =
            trspk_dynamic_batch_flush_row(rows_base, row_stride, flush_offset, i0);
        uint32_t run_lo = trspk_dynamic_batch_stream_beg(d0, stream);
        uint32_t run_hi = run_lo + trspk_dynamic_batch_stream_len(d0, stream);
        int q = p + 1;
        while( q < n )
        {
            const uint32_t ij = idx[q];
            const TRSPK_DynamicBatchFlushRow* dj =
                trspk_dynamic_batch_flush_row(rows_base, row_stride, flush_offset, ij);
            const uint32_t lo = trspk_dynamic_batch_stream_beg(dj, stream);
            const uint32_t hi = lo + trspk_dynamic_batch_stream_len(dj, stream);
            if( lo <= run_hi )
            {
                if( hi > run_hi )
                    run_hi = hi;
                ++q;
            }
            else
                break;
        }
        const size_t total = (size_t)run_hi - (size_t)run_lo;
        if( q == p + 1 )
        {
            const uint32_t ri = idx[p];
            const TRSPK_DynamicBatchFlushRow* dr =
                trspk_dynamic_batch_flush_row(rows_base, row_stride, flush_offset, ri);
            upload(
                upload_ctx,
                stream,
                buffer_id,
                (intptr_t)trspk_dynamic_batch_stream_beg(dr, stream),
                (size_t)trspk_dynamic_batch_stream_len(dr, stream),
                trspk_dynamic_batch_stream_src(dr, stream));
        }
        else
        {
            if( !trspk_dynamic_batch_scratch_ensure_merge(scratch, total) )
                break;
            uint8_t* merge_buf = scratch->merge;
            for( int t = p; t < q; ++t )
            {
                const uint32_t ri = idx[t];
                const TRSPK_DynamicBatchFlushRow* dr =
                    trspk_dynamic_batch_flush_row(rows_base, row_stride, flush_offset, ri);
                memcpy(
                    merge_buf + ((size_t)trspk_dynamic_batch_stream_beg(dr, stream) -
                                 (size_t)run_lo),
                    trspk_dynamic_batch_stream_src(dr, stream),
                    (size_t)trspk_dynamic_batch_stream_len(dr, stream));
            }
            upload(upload_ctx, stream, buffer_id, (intptr_t)run_lo, total, merge_buf);
        }
        p = q;
    }
}
