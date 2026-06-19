#ifndef RSCACHE_RSCACHESHARED_BITBUFFER_H
#define RSCACHE_RSCACHESHARED_BITBUFFER_H

#include "rscache_shared_rs_buffer.h"

#include <stdint.h>

/**
 * BitBuffer wraps an RSBuffer to read arbitrary bit counts.
 * Instantiate from an RSBuffer via RSCacheShared_BitBufferInitFromRsbuf.
 *
 * Interface (mirrors TS usage):
 *   buf.bits();           -> RSCacheShared_BitBufferBits(&buf)  [no-op / optional]
 *   buf.gBit(n);          -> bitbuffer_gbit(&buf, n)
 */
struct RSCacheShared_BitBuffer
{
    const uint8_t* data;
    int size_bytes;
    int byte_position;
    int bit_offset; /* 0..7 within current byte */
};

void
RSCacheShared_BitBufferInitFromRsbuf(
    struct RSCacheShared_BitBuffer* buf,
    const struct RSCacheShared_RSBuffer* rsbuf);

void
RSCacheShared_BitBufferBits(struct RSCacheShared_BitBuffer* buf);

unsigned int
RSCacheShared_BitBufferGbits(
    struct RSCacheShared_BitBuffer* buf,
    int n);

// unsigned int
// bitbuffer_gdata(
//     struct RSCacheShared_BitBuffer* buf,
//     int offset,
//     int n,
//     uint8_t* out,
//     int out_size);

// unsigned int
// bitbuffer_gbyte_offset(struct RSCacheShared_BitBuffer* buf);

#define bits(buf) RSCacheShared_BitBufferBits(buf)
#define gbits(buf, n) RSCacheShared_BitBufferGbits(buf, n)
// #define gdata(buf, offset, n, out, out_size) bitbuffer_gdata(buf, offset, n, out, out_size)
// #define gbyte_offset(buf) bitbuffer_gbyte_offset(buf)

#endif
