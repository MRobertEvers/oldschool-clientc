#ifndef SRC_TORIRSSERVER_MOCK239_INBOUND_H
#define SRC_TORIRSSERVER_MOCK239_INBOUND_H

#include <stdint.h>

/**
 * Rewrite one revision-239 client packet into the body `torirs_server_world.c` reads.
 *
 * WHY THIS IS A TRANSLATION AND NOT A TABLE ENTRY
 *
 * The outbound direction needed only a new opcode number per packet, because
 * the mock writes the bytes and could simply write the 239 ones. Inbound it is
 * the CLIENT that writes the bytes, and at 239 it writes them differently in
 * three distinct ways, only the first of which a wire-opcode table can express:
 *
 *   1. the opcode moved                -- the table handles this;
 *   2. the FIELDS moved                -- OpLoc1V2 is `z, x, ctrl, subop, id`
 *                                         where 230's is `x, z, id`. Same eight
 *                                         bytes, none of them in the same place;
 *   3. a whole FAMILY collapsed        -- IF_BUTTON1..10, OPHELD1..5 and
 *                                         INV_BUTTON1..5 are one opcode at 239
 *                                         (IF_BUTTONX) with the op number in
 *                                         the payload. The unified body stays
 *                                         intact until the interface router so
 *                                         item ops 6..10 and IF_SUBOP's final
 *                                         byte are not discarded.
 *
 * Case 3 is why the return value is a NAME rather than just a length: even a
 * packet retained in unified form must be renamed from an unmapped wire row
 * into the canonical interface callback.
 *
 * None of this fails loudly on its own. A packet read in the wrong field order
 * frames perfectly -- the length is right, so the stream stays in sync -- and
 * arrives as an interaction with a plausible loc id at a coordinate on the far
 * side of the map. That is what "clicks route nowhere" looked like: the server
 * received every click and acted on none of them.
 *
 * Each case below names the RSProt decoder it was transcribed from. The decoder
 * is the source of truth in this direction, the same way the encoder is in the
 * other one; both live in
 * `protocol/osrs-239/osrs-239-desktop/src/main/kotlin/.../incoming/codec/`.
 *
 * @param wire_opcode  the 239 opcode as it arrived, for the cases where one
 *                     opcode fans out to several canonical names.
 * @param name         what the 239 table called it, or PKTOUT_NAME_NONE.
 * @param out          receives either the 230-shaped body or a validated,
 *                     losslessly retained 239 interface body. Must hold
 *                     `in_len + 8`; no translation here grows a packet, but
 *                     the slack means a future one that does cannot silently
 *                     truncate.
 * @return the canonical PKTOUT_NAME_* to route with `*out_len` bytes of `out`,
 *         or PKTOUT_NAME_NONE to drop the packet.
 */
int
mock239_inbound_translate(
    int wire_opcode,
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len);

#endif /* SRC_TORIRSSERVER_MOCK239_INBOUND_H */
