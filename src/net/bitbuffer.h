#ifndef SRC_NET_BITBUFFER_H
#define SRC_NET_BITBUFFER_H

/*
 * Bit reader over a raw byte span (port of v0 shared_bit_buffer, itself a
 * port of the reference Packet.gBit). Used by the PLAYER_INFO / NPC_INFO
 * command-stream decoders; byte-mode reads resume at
 * Net_BitBufferBytePos (rounds the cursor up to the next byte).
 */

#include <stdint.h>

struct Net_BitBuffer
{
    const uint8_t* data;
    int size_bytes;
    int byte_position;
    int bit_offset; /* 0..7 within the current byte */
};

void
Net_BitBufferInit(
    struct Net_BitBuffer* buf,
    const uint8_t* data,
    int size_bytes);

unsigned int
Net_BitBufferGbits(
    struct Net_BitBuffer* buf,
    int count);

/** Bits consumed so far. */
int
Net_BitBufferBitPos(const struct Net_BitBuffer* buf);

/** Byte position for resuming byte-aligned reads (rounds up). */
int
Net_BitBufferBytePos(const struct Net_BitBuffer* buf);

#endif
