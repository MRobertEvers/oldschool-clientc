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

void
trspk_dynamic_batch_sort_indices_by_u32(
    void* rows,
    uint32_t* idx,
    int n,
    uint32_t (*key)(void* rows, uint32_t row_i))
{
    if( !rows || !idx || n <= 0 || !key )
        return;
    for( int i = 1; i < n; ++i )
    {
        const uint32_t key_row = idx[i];
        const uint32_t kb = key(rows, key_row);
        int j = i - 1;
        while( j >= 0 && key(rows, idx[j]) > kb )
        {
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
    void* rows,
    uint32_t (*byte_beg)(void* rows, uint32_t ri),
    uint32_t (*byte_len)(void* rows, uint32_t ri),
    const void* (*cpu_src)(void* rows, uint32_t ri),
    uintptr_t buffer_id,
    int is_element_array_buffer,
    TRSPK_DynamicBatchUploadFn upload,
    void* upload_ctx)
{
    if( !scratch || !idx || n <= 0 || !rows || !byte_beg || !byte_len || !cpu_src || !upload )
        return;
    int p = 0;
    while( p < n )
    {
        const uint32_t i0 = idx[p];
        uint32_t run_lo = byte_beg(rows, i0);
        uint32_t run_hi = run_lo + byte_len(rows, i0);
        int q = p + 1;
        while( q < n )
        {
            const uint32_t ij = idx[q];
            const uint32_t lo = byte_beg(rows, ij);
            const uint32_t hi = lo + byte_len(rows, ij);
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
            upload(
                upload_ctx,
                is_element_array_buffer,
                buffer_id,
                (intptr_t)byte_beg(rows, ri),
                (size_t)byte_len(rows, ri),
                cpu_src(rows, ri));
        }
        else
        {
            if( !trspk_dynamic_batch_scratch_ensure_merge(scratch, total) )
                break;
            uint8_t* merge_buf = scratch->merge;
            for( int t = p; t < q; ++t )
            {
                const uint32_t ri = idx[t];
                memcpy(
                    merge_buf + ((size_t)byte_beg(rows, ri) - (size_t)run_lo),
                    cpu_src(rows, ri),
                    (size_t)byte_len(rows, ri));
            }
            upload(
                upload_ctx,
                is_element_array_buffer,
                buffer_id,
                (intptr_t)run_lo,
                total,
                merge_buf);
        }
        p = q;
    }
}
