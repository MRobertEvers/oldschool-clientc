#include "bitbuffer.h"

#include "rsbuf.h"

#include <assert.h>
#include <stdlib.h>

void
bitbuffer_init_from_rsbuf(
    struct BitBuffer* buf,
    const struct RSBuffer* rsbuf)
{
    buf->data = (const uint8_t*)rsbuf->data;
    buf->size_bytes = rsbuf->size;
    buf->byte_position = (int)rsbuf->position;
    buf->bit_offset = 0;
}

void
bitbuffer_bits(struct BitBuffer* buf)
{
    (void)buf;
    /* No-op: bit mode is implied when using a BitBuffer. Kept for API parity. */
}

unsigned int
bitbuffer_gbits(
    struct BitBuffer* buf,
    int n)
{
    /* Match TS Packet.gBit(n): bitPos, bytePos, remaining, advance first, then read. */
    static const unsigned int bitmask[9] = { 0, 1, 3, 7, 15, 31, 63, 127, 255 };

    assert(n >= 1 && n <= 32);
    assert(buf->byte_position >= 0 && buf->bit_offset >= 0 && buf->bit_offset < 8);
    assert(buf->size_bytes > 0 && buf->data != NULL);

    int bit_pos = buf->byte_position * 8 + buf->bit_offset;
    assert(bit_pos + n <= buf->size_bytes * 8);
    int byte_pos = bit_pos >> 3;
    int remaining = 8 - (bit_pos & 7);
    unsigned int value = 0;

    buf->byte_position = (bit_pos + n) >> 3;
    buf->bit_offset = (bit_pos + n) & 7;

    for( ; n > remaining; remaining = 8 )
    {
        value += (buf->data[byte_pos++] & bitmask[remaining]) << (n - remaining);
        n -= remaining;
    }

    if( n == remaining )
    {
        value += buf->data[byte_pos] & bitmask[remaining];
    }
    else
    {
        value += (buf->data[byte_pos] >> (remaining - n)) & bitmask[n];
    }

    return value;
}
