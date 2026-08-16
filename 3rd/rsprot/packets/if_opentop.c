/* IF_OPENTOP. See if_opentop.h — this file is the pattern every packet follows. */
#include "if_opentop.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-<N>/osrs-<N>-desktop/src/main/kotlin/net/rsprot/protocol/
 *       game/outgoing/codec/interfaces/IfOpenTopEncoder.kt
 *
 *   rev 221 / 225 / 236                buffer.p2(message.interfaceId)
 *   rev 222 / 226 / 235 / 238          buffer.p2Alt3(message.interfaceId)
 *   rev 223-224 / 227-230 /
 *       232-234 / 237                  buffer.p2Alt1(message.interfaceId)
 *   rev 231 / 239                      buffer.p2Alt2(message.interfaceId)
 *
 * Read out of the vendored tree, not from memory. The four layouts and their
 * revision sets agree with gen/packet_versions.txt rows 410-413, which are
 * derived independently by content-hashing the same Kotlin — the numbers here
 * come from that ledger rather than being allocated in this file.
 *
 * One function per layout, and it runs in whichever direction the executor is
 * in — so the server's writer and the client's reader cannot be two different
 * transcriptions of this.
 */

void
packet_if_opentop_v1_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2(x, m->interface_id);
}

void
packet_if_opentop_v2_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_ALT3(x, m->interface_id);
}

void
packet_if_opentop_v3_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_ALT1(x, m->interface_id);
}

void
packet_if_opentop_v4_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_ALT2(x, m->interface_id);
}

/*
 * Thirteen rows for four layouts — the run/layout distinction made concrete.
 * The table is complete over every vendored revision, so `rsprot_version_pick`
 * returning NULL here means one thing only: a revision outside 221-239. An
 * incomplete table would make "this build does not carry that layout" and "no
 * such packet at that revision" the same answer, and they are different facts.
 *
 * Order does not matter — rsprot_version_pick is a linear scan — but the rows
 * are kept in ascending revision order so a gap is visible by eye.
 */
const RsprotVersionRange rsprot_if_opentop_out[] = {
    { 221, 221, (RsprotCodecFn)packet_if_opentop_v1_out },
    { 222, 222, (RsprotCodecFn)packet_if_opentop_v2_out },
    { 223, 224, (RsprotCodecFn)packet_if_opentop_v3_out },
    { 225, 225, (RsprotCodecFn)packet_if_opentop_v1_out },
    { 226, 226, (RsprotCodecFn)packet_if_opentop_v2_out },
    { 227, 230, (RsprotCodecFn)packet_if_opentop_v3_out },
    { 231, 231, (RsprotCodecFn)packet_if_opentop_v4_out },
    { 232, 234, (RsprotCodecFn)packet_if_opentop_v3_out },
    { 235, 235, (RsprotCodecFn)packet_if_opentop_v2_out },
    { 236, 236, (RsprotCodecFn)packet_if_opentop_v1_out },
    { 237, 237, (RsprotCodecFn)packet_if_opentop_v3_out },
    { 238, 238, (RsprotCodecFn)packet_if_opentop_v2_out },
    { 239, 239, (RsprotCodecFn)packet_if_opentop_v4_out },
};

const int rsprot_if_opentop_out_count =
    (int)(sizeof(rsprot_if_opentop_out) / sizeof(rsprot_if_opentop_out[0]));
