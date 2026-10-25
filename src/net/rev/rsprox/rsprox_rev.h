#ifndef SRC_NET_REV_RSPROX_RSPROX_REV_H
#define SRC_NET_REV_RSPROX_RSPROX_REV_H

/*
 * The data-driven OSRS revisions, 227-241 ("rsprox<N>").
 *
 * Every other module under net/rev/ is a revision written by hand: its own
 * opcode tables, its own parser, its own entity-info readers. This one is
 * written once and DRIVEN by what 3rd/rsprot generates from RSProt and RSProx:
 *
 *   wire tables     gen/rev<N>_prot.h (both directions, plus the zone index)
 *   payloads        the packets/ codecs, found by prot name through
 *                   rsprot_registry, at revision N
 *   info streams    streams/ (PLAYER_INFO / NPC_INFO_*), one file per prot;
 *                   each revision binds stream_<prot>_rev<N>_read
 *
 * so a revision is a NUMBER here, and supporting the next one is regenerating
 * 3rd/rsprot rather than copying a module. RSProx records sessions at all of
 * these revisions; replay (net/rsprox_recording.c) plays any of them with the
 * table for its header's revision.
 *
 * What is still revision-specific in C, and where:
 *   rsprox_parse.c        adapters from each prot's codec message to the
 *                         canonical RevPacket arm, keyed by PROT name (the
 *                         layout identity), plus the non-codec shapes: the
 *                         rebuild grids, the enclosed zone walk, the raw
 *                         social payloads, WORLDENTITY_INFO
 *   rsprox_entity_info.c  the pass mapping a stream reader's op list onto
 *                         the op lists the entity-info executor consumes
 */

#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include "rsprot_tables_def.h"
#include "streams/rsprot_info.h"

#include <stdint.h>

#define RSPROX_REV_FIRST 227
#define RSPROX_REV_LAST 241
#define RSPROX_REV_COUNT (RSPROX_REV_LAST - RSPROX_REV_FIRST + 1)

/** Opcodes this module can index. The highest server opcode across 227-241 is
 *  well under this; a table past it fails the build's assert, not the wire. */
#define RSPROX_MAX_OPCODE 512

struct RsproxRev
{
    int revision;
    RsprotRevTable const* prots;
    /** wire opcode -> index in prots->server / prots->client, or -1. */
    int16_t server_by_wire[RSPROX_MAX_OPCODE];
    int16_t client_by_wire[RSPROX_MAX_OPCODE];
    /** wire opcode -> canonical name (0 = none). */
    int16_t server_name[RSPROX_MAX_OPCODE];
    /** canonical name -> first wire opcode carrying it, or -1. */
    int16_t name_wire[PKT_NAME_COUNT];
    int16_t outname_wire[PKTOUT_NAME_COUNT];
    /** WORLDENTITY_INFO_V<n>'s reader at this revision, by n; NULL where the
     *  revision has no such prot. */
    RsprotWorldEntityInfoReadFn worldentity_read[8];
    struct GameProtoRevTable table;
    char name[16];
};

/** The module's state for `rev` (one of its own tables). */
struct RsproxRev const*
rsprox_rev(struct GameProtoRevTable const* rev);

/** The server prot at `wire`, or NULL. */
RsprotProtDef const*
rsprox_server_prot(struct RsproxRev const* r, int wire);

/* rsprox_canon.c */
int
rsprox_canon_server(char const* prot);
int
rsprox_canon_client(char const* prot);

/* rsprox_parse.c */
/** Run `prot`'s codec over the payload: 1 decoded and consumed it exactly, 0
 *  did not, -1 no codec carries `prot` at this revision. */
int
rsprox_parse_validate(
    struct GameProtoRevTable const* rev, char const* prot, uint8_t const* data, int len);
int
rsprox_parse_wire(
    struct GameProtoRevTable const* rev,
    int wire_opcode,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

/* rsprox_entity_info.c -- the GameProtoRevTable entity hooks, with the
 * stream reader passed explicitly (gameproto_rev_rsprox.c stamps out the
 * per-revision wrappers the table's context-free slots need). */
int
rsprox_player_info_convert(
    RsprotPlayerInfoReadFn read, uint8_t const* data, int len, struct PktPlayerInfoOp* ops,
    int cap);
int
rsprox_npc_info_convert(
    RsprotNpcInfoReadFn read, uint8_t const* data, int len, struct PktNpcInfoOp* ops, int cap);
/** WORLDENTITY_INFO through `read` into the canonical arm. 1 when the stream
 *  read whole and fits the arm, 0 otherwise. */
int
rsprox_worldentity_info_convert(
    RsprotWorldEntityInfoReadFn read, uint8_t const* data, int len, struct PktWorldEntityInfo* out);
void
rsprox_playerinfo_set_local(int local_index);
void
rsprox_playerinfo_init(uint8_t const* data, int len);
size_t
rsprox_playerinfo_state_size(void);
/** 1 when the last PLAYER_INFO / NPC_INFO decoded consumed its payload exactly
 *  as the revision says (the op lists are delivered either way). */
int
rsprox_entity_info_last_ok(void);
void
rsprox_playerinfo_state_save(void* out);
void
rsprox_playerinfo_state_load(void const* in);

#endif
