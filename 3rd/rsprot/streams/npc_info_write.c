#include "npc_info_write.h"

#include "../src/rsprot_buf.h"

void
rsprot_npc_info_write_empty(struct RsprotBuf* buf)
{
    rsprot_buf_bit_begin(buf);
    rsprot_pbits(buf, 8, 0); /* no high-resolution npcs */
    /*
     * NO 0xFFFF TERMINATOR HERE, and that is the whole subtlety of the empty
     * packet.
     *
     * The client's low-resolution loop only attempts to read an index when at
     * least 16 + 12 bits remain; below that it stops. With no npcs and no
     * extended info there is nothing after the count, so it stops immediately
     * and consumes ONE byte. Writing the terminator anyway makes the packet
     * three bytes that the client reads one of, and it reports the difference
     * as `RuntimeException: 1,3` and drops the connection — which presents as
     * the client returning to the login screen a second after logging in.
     *
     * The terminator is required once something follows: with additions or
     * extended-info bytes in the buffer the loop has enough bits to keep going
     * and would read them as npc indices. So it belongs in the populated
     * writer, not here.
     */
    rsprot_buf_bit_end(buf);
}

int
rsprot_npc_info_tail_needs_sentinel(
    size_t bit_position,
    size_t extended_bytes)
{
    size_t const padding_bits = (8u - (bit_position & 7u)) & 7u;

    return padding_bits + extended_bytes * 8u >= 28u;
}
