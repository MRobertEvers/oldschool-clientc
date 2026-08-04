#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Revision-239 parse (server -> this client).
 *
 * Written from RSProt's own encoders, one at a time, NOT from what this repo's
 * server happens to emit. That distinction is the whole discipline of this
 * file: a decoder written to match our encoder agrees with it by construction
 * and proves nothing, and this session already produced one bug of exactly that
 * shape -- the server wrote an ABSOLUTE coordinate into a 30-bit field the
 * client adds as a DELTA, and every self-consistent test passed while a real
 * client drifted off the map. Two implementations only check each other when
 * they are written from the same spec independently, so each decoder below
 * names the encoder it came from.
 *
 * WHAT DELEGATES AND WHY IT IS A SHORT LIST
 *
 * The `default` arm hands the rest to `osrs230_parse`. That is safe only for
 * packets whose payload genuinely did not move, and the set is smaller than it
 * looks: `src/net/rev/osrs230/` is a hybrid whose payloads are lc254-shaped, so
 * "delegate" means "read what our own server writes at 230", which for anything
 * RSProt renamed or reordered is simply wrong. Everything this file handles is
 * a packet where that would have been wrong.
 */

/* ------------------------------------------------------------------ */
/* Cursor                                                              */
/* ------------------------------------------------------------------ */

/*
 * Every reader clamps to `len` and sets `over` on the first read past the end,
 * so a layout that does not match the wire is caught by the caller (which drops
 * the packet) rather than applying half-decoded state.
 */
struct Cur
{
    uint8_t const* data;
    int len;
    int pos;
    int over;
};

static int
g1(struct Cur* c)
{
    if( c->pos + 1 > c->len )
    {
        c->over = 1;
        return 0;
    }
    return c->data[c->pos++];
}

/* The four byte orders RSProt writes ints in. Named for its accessors so a
 * reader can be diffed against the encoder line by line. */
static int
g2(struct Cur* c)
{
    int a = g1(c), b = g1(c);
    return (a << 8) | b;
}

/* p2Alt1: [v, v>>8] -- little endian. */
static int
g2_alt1(struct Cur* c)
{
    int a = g1(c), b = g1(c);
    return (b << 8) | a;
}

/* p2Alt2: [v>>8, v+128]. */
static int
g2_alt2(struct Cur* c)
{
    int a = g1(c), b = g1(c);
    return (a << 8) | ((b - 128) & 0xff);
}

/* p2Alt3: [v+128, v>>8]. */
static int
g2_alt3(struct Cur* c)
{
    int a = g1(c), b = g1(c);
    return (b << 8) | ((a - 128) & 0xff);
}

static int
g4(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c), e = g1(c);
    return (a << 24) | (b << 16) | (d << 8) | e;
}

/* p4Alt1: little endian. */
static int
g4_alt1(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c), e = g1(c);
    return (e << 24) | (d << 16) | (b << 8) | a;
}

/* p4Alt2: [v>>8, v, v>>24, v>>16]. */
static int
g4_alt2(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c), e = g1(c);
    return (d << 24) | (e << 16) | (a << 8) | b;
}

/* p4Alt3: [v>>16, v>>24, v, v>>8]. */
static int
g4_alt3(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c), e = g1(c);
    return (b << 24) | (a << 16) | (d << 8) | e;
}

/* p1Alt1: v + 128. */
static int
g1_alt1(struct Cur* c)
{
    return (g1(c) - 128) & 0xff;
}

/* p1Alt2: -v. */
static int
g1_alt2(struct Cur* c)
{
    return (-g1(c)) & 0xff;
}

/** Signed byte, for the varp-small value. */
static int
g1b(struct Cur* c)
{
    int v = g1(c);
    return v > 127 ? v - 256 : v;
}

int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

int
osrs239_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    struct Cur c = { data, len, 0, 0 };

    switch( pkt_name )
    {
    /*
     * RebuildNormalV2Encoder: p2Alt1 worldArea, p2Alt2 zoneZ, p2Alt2 zoneX.
     *
     * V2 dropped the XTEA key array entirely -- OldSchool stores map archives
     * in the clear from revision 237 -- so there is no key count and no keys.
     * Reading the V1 layout here recovers a plausible zone and then walks into
     * a key count that is not there.
     */
    case PKT_NAME_REBUILD_NORMAL:
    {
        struct PktMapRebuild* p = &out->_map_rebuild;
        memset(p, 0, sizeof(*p));
        (void)g2_alt1(&c); /* worldArea */
        p->zonez = g2_alt2(&c);
        p->zonex = g2_alt2(&c);
        p->region_count = 0;
        p->region_ids = NULL;
        p->region_keys = NULL;
        return c.over ? -1 : 0;
    }

    /*
     * IfSetEventsV2Encoder:
     *   p2Alt2 end, p4 events2, p4Alt2 events1, p2 start, pCombinedIdAlt3 uid
     *
     * V2 carries TWO 32-bit masks where the older packet carried one, and in a
     * different field order -- 12 bytes became 16. `events2` is the high range;
     * this client has no representation for it yet, so it is read and dropped
     * rather than skipped, which keeps the cursor honest.
     */
    case PKT_NAME_IF_SETEVENTS:
    {
        struct PktIfSetEvents* p = &out->_if_setevents;
        p->to = g2_alt2(&c);
        (void)g4(&c); /* events2 -- high range, unrepresented here */
        p->events = g4_alt2(&c);
        p->from = g2(&c);
        p->component_id = g4_alt3(&c);
        return c.over ? -1 : 0;
    }

    /* IfOpenTopEncoder: p2Alt2 interfaceId. The 230 hybrid writes p2Alt1 --
     * same two bytes, different order, a different interface. */
    case PKT_NAME_IF_OPENTOP:
    {
        struct PktIfOpenTop* p = &out->_if_opentop;
        p->interface_id = g2_alt2(&c);
        return c.over ? -1 : 0;
    }

    /*
     * IfOpenSubEncoder: p2Alt3 interfaceId, pCombinedIdAlt3 dest, p1 type.
     *
     * Seven bytes at both revisions with the three fields in the OPPOSITE
     * order -- the 230 hybrid writes type first. A packet read the wrong way
     * round mounts an arbitrary interface into an arbitrary slot and frames
     * perfectly while doing it.
     */
    case PKT_NAME_IF_OPENSUB:
    {
        struct PktIfOpenSub* p = &out->_if_opensub;
        p->interface_id = g2_alt3(&c);
        p->target_uid = g4_alt3(&c);
        p->type = g1(&c);
        return c.over ? -1 : 0;
    }

    /* IfCloseSubEncoder: pCombinedId (plain p4). */
    case PKT_NAME_IF_CLOSESUB:
    {
        struct PktIfCloseSub* p = &out->_if_closesub;
        p->target_uid = g4(&c);
        return c.over ? -1 : 0;
    }

    /* VarpSmallEncoder: p1Alt1 value, p2Alt3 id. */
    case PKT_NAME_VARP_SMALL:
    {
        struct PktVarpSmall* p = &out->_varp_small;
        int v = g1_alt1(&c);
        p->value = v > 127 ? v - 256 : v;
        p->variable = g2_alt3(&c);
        return c.over ? -1 : 0;
    }

    /* VarpLargeEncoder: p2Alt2 id, p4Alt1 value. Note the id comes FIRST here
     * and second in the small form; there is no "id first" rule. */
    case PKT_NAME_VARP_LARGE:
    {
        struct PktVarpLarge* p = &out->_varp_large;
        p->variable = g2_alt2(&c);
        p->value = g4_alt1(&c);
        return c.over ? -1 : 0;
    }

    /*
     * UpdateStatV2Encoder:
     *   p1 invisibleBoostedLevel, p1 currentLevel, p1Alt1 stat, p4Alt2 experience
     *
     * Seven bytes at both revisions and not one field in the same place. The
     * 230 layout is `stat, level, xp, boosted`; read that way, the stat id
     * arrives as a level and the level as a stat.
     */
    case PKT_NAME_UPDATE_STAT:
    {
        struct PktUpdateStat* p = &out->_update_stat;
        (void)g1(&c); /* invisibleBoostedLevel */
        p->level = g1(&c);
        p->stat = g1_alt1(&c);
        p->xp = g4_alt2(&c);
        return c.over ? -1 : 0;
    }

    /*
     * Still refused, and each for its own reason rather than as a group:
     *
     *   IF_SETMODEL   V2 addresses the component by a 4-byte combined uid.
     *   CAM_MOVETO    V2/V3 moved to absolute coordinates; the 230 reader
     *   CAM_LOOKAT    expects the classic scene-local 6-byte body.
     *   REBUILD_REGION V2, like REBUILD_NORMAL, but its zone descriptor grid
     *                 has not been re-read against the V2 encoder.
     *   PLAYER_INFO   the v5 stream is a different CODEC, not a field order.
     *   NPC_INFO      It belongs in the `player_info_read` / `npc_info_read`
     *                 rev-table slots, not here.
     *
     * Dropping is the only answer that cannot corrupt state.
     */
    case PKT_NAME_REBUILD_REGION:
    case PKT_NAME_IF_SETMODEL:
    case PKT_NAME_CAM_MOVETO:
    case PKT_NAME_CAM_LOOKAT:
    case PKT_NAME_PLAYER_INFO:
    case PKT_NAME_NPC_INFO:
        return -1;

    default:
        /*
         * The rest share the 230 layout. That is a claim per packet, not a
         * default in the reassuring sense: what it means is "our own server
         * writes the same bytes at both revisions", and it is true only because
         * the packets left here are the ones RSProt did not rename or reorder.
         */
        return osrs230_parse(rev, pkt_name, data, len, out);
    }
}

/*
 * Byte orders no decoder needs yet.
 *
 * Kept rather than deleted because the next decoder written will want one of
 * them, and a half-present set is how the wrong order gets picked. Marked used
 * so the compiler does not have an opinion about it.
 */
__attribute__((used)) static int (*const osrs239_unused_orders[])(struct Cur*) = {
    g2_alt1, g1_alt2, g1b, g4_alt1,
};
