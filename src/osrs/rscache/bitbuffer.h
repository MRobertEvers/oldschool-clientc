#ifndef _BITBUF_H
#define _BITBUF_H

#include "rsbuf.h"

#include <stdint.h>

/**
 * BitBuffer wraps an RSBuffer to read arbitrary bit counts.
 * Instantiate from an RSBuffer via bitbuffer_init_from_rsbuf.
 *
 * Interface (mirrors TS usage):
 *   buf.bits();           -> bitbuffer_bits(&buf)  [no-op / optional]
 *   buf.gBit(n);          -> bitbuffer_gbit(&buf, n)
 */
struct BitBuffer
{
    const uint8_t* data;
    int size_bytes;
    int byte_position;
    int bit_offset; /* 0..7 within current byte */
};

void
bitbuffer_init_from_rsbuf(
    struct BitBuffer* buf,
    const struct RSBuffer* rsbuf);

void
bitbuffer_bits(struct BitBuffer* buf);

unsigned int
bitbuffer_gbits(
    struct BitBuffer* buf,
    int n);

#define bits(buf) bitbuffer_bits(buf)
#define gbits(buf, n) bitbuffer_gbits(buf, n)

#endif
