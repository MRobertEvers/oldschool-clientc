#include "webgl1_index16.h"

#include "../core/trspk_drawrangelist.h"

#include <stdint.h>

/* 65536 distinct indices fit in a u16, so the widest legal span between the
 * lowest and highest vertex a chunk touches is 65535. */
#define TRSPK_WEBGL1_INDEX_SPAN 65535u

uint32_t
trspk_webgl1_split16(
    struct TRSPK_DrawRangeList const* ranges,
    uint32_t const* src,
    uint16_t* dst,
    struct TRSPK_WebGL1Chunk* chunks,
    uint32_t chunk_capacity,
    uint32_t* out_chunk_count,
    int* out_overflow)
{
    uint32_t written = 0u;
    uint32_t chunk_count = 0u;
    int overflow = 0;
    struct TRSPK_DrawRange const* range;

    if( out_chunk_count )
        *out_chunk_count = 0u;
    if( out_overflow )
        *out_overflow = 0;
    if( !ranges || !src || !dst || !chunks || chunk_capacity == 0u )
        return 0u;

    range = trspk_drawrangelist_head((struct TRSPK_DrawRangeList*)ranges);
    while( range )
    {
        uint32_t i = range->start;

        /* A chunk never spans two ranges: they differ in draw config or group,
         * which is state set between draw calls. */
        while( i + 2u < range->end )
        {
            uint32_t lo = src[i];
            uint32_t hi = src[i];
            uint32_t start = i;

            /* Seed from the first triangle, then take triangles while they all
             * stay inside one window. `lo`/`hi` track the window as it grows. */
            for( uint32_t k = 1u; k < 3u; k++ )
            {
                uint32_t v = src[i + k];
                if( v < lo )
                    lo = v;
                if( v > hi )
                    hi = v;
            }
            i += 3u;

            while( i + 2u < range->end )
            {
                uint32_t next_lo = lo;
                uint32_t next_hi = hi;
                for( uint32_t k = 0u; k < 3u; k++ )
                {
                    uint32_t v = src[i + k];
                    if( v < next_lo )
                        next_lo = v;
                    if( v > next_hi )
                        next_hi = v;
                }
                if( next_hi - next_lo > TRSPK_WEBGL1_INDEX_SPAN )
                    break;
                lo = next_lo;
                hi = next_hi;
                i += 3u;
            }

            if( chunk_count >= chunk_capacity )
            {
                overflow = 1;
                goto done;
            }

            for( uint32_t k = start; k < i; k++ )
                dst[written + (k - start)] = (uint16_t)(src[k] - lo);

            chunks[chunk_count].index_start = written;
            chunks[chunk_count].index_count = i - start;
            chunks[chunk_count].base_vertex = lo;
            chunks[chunk_count].group = range->group;
            chunks[chunk_count].config_idx = range->config_idx;
            chunk_count++;
            written += i - start;
        }

        range = trspk_drawrangelist_next((struct TRSPK_DrawRangeList*)ranges, range);
    }

done:
    if( out_chunk_count )
        *out_chunk_count = chunk_count;
    if( out_overflow )
        *out_overflow = overflow;
    return written;
}
