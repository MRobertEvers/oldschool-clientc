#ifndef SRC_NET_REV_RSPROT_BRIDGE_H
#define SRC_NET_REV_RSPROT_BRIDGE_H

/*
 * The rsprot codecs, wired into this project's inbound packet path.
 *
 * ## What this is for
 *
 * `3rd/rsprot/packets/` states what the bytes are, per layout version, for
 * every vendored revision. This file is where the client asks it. Given a
 * canonical `GameProtoPktName` and a revision it returns a filled `RevPacket`,
 * or <0 for "no rsprot codec for that packet at that revision" -- which is the
 * signal `net_process_packets` uses to fall through to the hand-written
 * parsers, exactly as `rev->parse` already returns <0 to fall through to
 * `gameproto_parse`.
 *
 * So this is a THIRD stage in front of a chain that already had two, and it is
 * meant to grow until the two behind it are empty. A packet moves across by
 * being generated into `3rd/rsprot/packets/` and getting a row in the table in
 * the .c; its arm in `osrs239_parse.c` is then dead and gets deleted. Nothing
 * has to move all at once, and at every point the tree builds and runs.
 *
 * ## The adapter, and why it is temporary
 *
 * Each row carries a small function copying an rsprot `Msg*` into the matching
 * `Pkt*` arm of `RevPacket`. That copy is pure transition cost: the `Msg` IS
 * the parsed packet, and the only reason it is not simply handed onward is
 * that every consumer in the tree reads `RevPacket` today. When enough packets
 * have moved, the `Pkt*` structs are what should go, not this seam -- and the
 * adapters are deliberately trivial so that deleting them later is mechanical.
 *
 * A field the canonical struct has no home for is DROPPED, and the adapter
 * says so where it happens. That is not a silent loss: the codec transferred
 * it, `DESCRIBE` will list it, and the drop is one grep away.
 *
 * ## Strings
 *
 * rsprot decodes strings as borrowed pointers into the payload buffer
 * (`rsprot_exec.h`, "Strings borrow"). `RevPacket` owns its strings and
 * `gameproto_free` frees them, so an adapter storing one COPIES it. Handing
 * the borrow straight over would free a pointer into a network buffer.
 */

#include "gameproto_revisions.h"
#include "revpacket.h"

#include <stdint.h>

/**
 * Decode `data` into `out` using the rsprot codec for (`pkt_name`, revision).
 *
 * Returns 1 on success, 0 when a codec ran but the payload was malformed, and
 * <0 when there is no codec for this packet at this revision (the caller should
 * fall through to the hand-written parsers).
 *
 * `rev` supplies the revision NUMBER; a revision this tree speaks that RSProt
 * never vendored (lc254, xrsps233) simply has no codec and returns <0.
 */
int
rsprot_bridge_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

/** The RSProt revision number a rev table speaks, or 0 for the project-native
 *  protocols RSProt never carried. Exposed for tests and for the trace. */
int
rsprot_bridge_revision(struct GameProtoRevTable const* rev);

#endif
