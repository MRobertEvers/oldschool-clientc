/* IF_OPENTOP. See if_opentop.h — this file is the pattern every packet follows. */
#include "if_opentop.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-<N>/osrs-<N>-desktop/src/main/kotlin/net/rsprot/protocol/
 *       game/outgoing/codec/interfaces/IfOpenTopEncoder.kt
 *
 *   rev 221 / 225 / 236                buffer.p2(message.interfaceId)
 *   rev 222 / 226 / 235 / 238          buffer.p2_21_add128(message.interfaceId)
 *   rev 223-224 / 227-230 /
 *       232-234 / 237                  buffer.p2_21(message.interfaceId)
 *   rev 231 / 239                      buffer.p2_12_add128(message.interfaceId)
 *
 * Read out of the vendored tree, not from memory.
 *
 * One function per layout, and it runs in whichever direction the executor is
 * in — so the server's writer and the client's reader cannot be two different
 * transcriptions of this.
 */

void
packet_if_opentop_v1_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_12(x, m->interface_id);
}

void
packet_if_opentop_v2_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_21_ADD128(x, m->interface_id);
}

void
packet_if_opentop_v3_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_21(x, m->interface_id);
}

void
packet_if_opentop_v4_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_12_ADD128(x, m->interface_id);
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
    { RSPROT_OSRS_ID(221), RSPROT_OSRS_ID(221), (RsprotCodecFn)packet_if_opentop_v1_out },
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(222), (RsprotCodecFn)packet_if_opentop_v2_out },
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(224), (RsprotCodecFn)packet_if_opentop_v3_out },
    { RSPROT_OSRS_ID(225), RSPROT_OSRS_ID(225), (RsprotCodecFn)packet_if_opentop_v1_out },
    { RSPROT_OSRS_ID(226), RSPROT_OSRS_ID(226), (RsprotCodecFn)packet_if_opentop_v2_out },
    { RSPROT_OSRS_ID(227), RSPROT_OSRS_ID(230), (RsprotCodecFn)packet_if_opentop_v3_out },
    { RSPROT_OSRS_ID(231), RSPROT_OSRS_ID(231), (RsprotCodecFn)packet_if_opentop_v4_out },
    { RSPROT_OSRS_ID(232), RSPROT_OSRS_ID(234), (RsprotCodecFn)packet_if_opentop_v3_out },
    { RSPROT_OSRS_ID(235), RSPROT_OSRS_ID(235), (RsprotCodecFn)packet_if_opentop_v2_out },
    { RSPROT_OSRS_ID(236), RSPROT_OSRS_ID(236), (RsprotCodecFn)packet_if_opentop_v1_out },
    { RSPROT_OSRS_ID(237), RSPROT_OSRS_ID(237), (RsprotCodecFn)packet_if_opentop_v3_out },
    { RSPROT_OSRS_ID(238), RSPROT_OSRS_ID(238), (RsprotCodecFn)packet_if_opentop_v2_out },
    { RSPROT_OSRS_ID(239), RSPROT_OSRS_ID(239), (RsprotCodecFn)packet_if_opentop_v4_out },
    /* 240 (RSProx osrs-240, IfOpenTopDecoder): g2_21_add128 -- v2, again. */
    { RSPROT_OSRS_ID(240), RSPROT_OSRS_ID(240), (RsprotCodecFn)packet_if_opentop_v2_out },
    /* 241 (RSProx osrs-241, IfOpenTopDecoder): decode body unchanged from 239. */
    { RSPROT_OSRS_ID(241), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_if_opentop_v4_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_if_opentop_v2_out },
};

const int rsprot_if_opentop_out_count =
    (int)(sizeof(rsprot_if_opentop_out) / sizeof(rsprot_if_opentop_out[0]));
