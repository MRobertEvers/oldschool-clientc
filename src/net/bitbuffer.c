#include "bitbuffer.h"

#include <assert.h>
#include <stddef.h>

void
Net_BitBufferInit(
    struct Net_BitBuffer* buf,
    const uint8_t* data,
    int size_bytes)
{
    buf->data = data;
    buf->size_bytes = size_bytes;
    buf->byte_position = 0;
    buf->bit_offset = 0;
}

unsigned int
Net_BitBufferGbits(
    struct Net_BitBuffer* buf,
    int count)
{
    /* Match TS Packet.gBit(n): bitPos, bytePos, remaining, advance first,
     * then read. */
    static const unsigned int bitmask[9] = { 0, 1, 3, 7, 15, 31, 63, 127, 255 };

    assert(count >= 1 && count <= 32);
    assert(buf->byte_position >= 0 && buf->bit_offset >= 0 && buf->bit_offset < 8);
    assert(buf->size_bytes > 0 && buf->data != NULL);

    int bit_pos = buf->byte_position * 8 + buf->bit_offset;
    assert(bit_pos + count <= buf->size_bytes * 8);
    int byte_pos = bit_pos >> 3;
    int remaining = 8 - (bit_pos & 7);
    unsigned int value = 0;

    buf->byte_position = (bit_pos + count) >> 3;
    buf->bit_offset = (bit_pos + count) & 7;

    for( ; count > remaining; remaining = 8 )
    {
        value += (buf->data[byte_pos++] & bitmask[remaining]) << (count - remaining);
        count -= remaining;
    }

    if( count == remaining )
        value += buf->data[byte_pos] & bitmask[remaining];
    else
        value += (buf->data[byte_pos] >> (remaining - count)) & bitmask[count];

    return value;
}

int
Net_BitBufferBitPos(const struct Net_BitBuffer* buf)
{
    return buf->byte_position * 8 + buf->bit_offset;
}

int
Net_BitBufferBytePos(const struct Net_BitBuffer* buf)
{
    return buf->byte_position + (buf->bit_offset + 7) / 8;
}
