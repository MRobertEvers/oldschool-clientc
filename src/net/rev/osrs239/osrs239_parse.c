#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"
#include "zoneprot.h"

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

/*
 * p4Alt3: [v>>16, v>>24, v, v>>8].
 *
 * Note which two bytes carry the LOW half — the third byte is `v` and the
 * fourth `v >> 8`, so the reader shifts the fourth and takes the third plain.
 * Reversing those two is not a corruption you can see: a combined component id
 * comes back as (interface, child << 8), which names a component the interface
 * does not have, and IF_OPENSUB skips it and IF_SETEVENTS arms nothing.
 */
static int
g4_alt3(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c), e = g1(c);
    return (b << 24) | (a << 16) | (e << 8) | d;
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

/* p1Alt3: 128 - v. */
static int
g1_alt3(struct Cur* c)
{
    return (128 - g1(c)) & 0xff;
}

/* p3: big-endian 24-bit. */
static int
g3(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c);
    return (a << 16) | (b << 8) | d;
}

/* p3Alt2: [v>>16, v, v>>8]. */
static int
g3_alt2(struct Cur* c)
{
    int a = g1(c), b = g1(c), d = g1(c);
    return (a << 16) | (d << 8) | b;
}

/* A 24-bit entity index is signed: RSProt states a player target as
 * `-(index + 1)`, which reaches the wire as a large positive 24-bit number. */
static int
sign24(int v)
{
    return (v & 0x800000) ? v - 0x1000000 : v;
}

/* pSmart1or2: one byte below 0x80, else a short with 0x8000 set. */
static int
gsmart1or2(struct Cur* c)
{
    int v = g1(c);

    if( v < 0x80 )
        return v;
    return ((v << 8) | g1(c)) & 0x7fff;
}

/*
 * A NUL-terminated Jagex string.
 *
 * This is the terminator revision 239 uses where 230 uses a newline, and the
 * difference is not cosmetic: a reader that stops at '\n' on a NUL-terminated
 * string runs to the end of the packet and swallows every field after it.
 * Returns a heap copy (freed by gameproto_free, like every other parsed
 * string), or NULL on overrun.
 */
static char*
gjstr_nul(struct Cur* c)
{
    int start = c->pos;
    int end = start;
    char* out;

    while( end < c->len && c->data[end] != 0 )
        end++;
    if( end >= c->len )
    {
        c->over = 1;
        return NULL;
    }
    out = (char*)malloc((size_t)(end - start) + 1);
    if( !out )
    {
        c->over = 1;
        return NULL;
    }
    memcpy(out, c->data + start, (size_t)(end - start));
    out[end - start] = '\0';
    c->pos = end + 1;
    return out;
}

/** Signed byte, for the varp-small value. */
static int
g1b(struct Cur* c)
{
    int v = g1(c);
    return v > 127 ? v - 256 : v;
}

/* ------------------------------------------------------------------ */
/* Zone sub-packets                                                    */
/* ------------------------------------------------------------------ */

/*
 * Inside UPDATE_ZONE_PARTIAL_ENCLOSED a sub-packet is addressed by the ORDINAL
 * of RSProt's `IndexedZoneProtEncoder` -- a third numbering, neither the
 * top-level opcode nor RSProt's own `OldSchoolZoneProt` id. Revision 230 uses
 * the top-level opcode here, so a reader that resolves through the packet-in
 * table decodes LOC_DEL (ordinal 0) as whatever opcode 0 happens to be. The
 * blob is length-prefixed as a whole, so nothing about the frame says so.
 */
static enum GameProtoPktName
osrs239_zone_name(int ordinal)
{
    switch( ordinal )
    {
    case OSRS239_ZONE_LOC_DEL: return PKT_NAME_LOC_DEL;
    case OSRS239_ZONE_LOC_MERGE: return PKT_NAME_LOC_MERGE;
    case OSRS239_ZONE_OBJ_COUNT: return PKT_NAME_OBJ_COUNT;
    case OSRS239_ZONE_MAP_ANIM: return PKT_NAME_MAP_ANIM;
    case OSRS239_ZONE_LOC_ADD_CHANGE_V2: return PKT_NAME_LOC_ADD_CHANGE;
    case OSRS239_ZONE_OBJ_ADD: return PKT_NAME_OBJ_ADD;
    case OSRS239_ZONE_LOC_ANIM: return PKT_NAME_LOC_ANIM;
    case OSRS239_ZONE_OBJ_DEL: return PKT_NAME_OBJ_DEL;
    case OSRS239_ZONE_OBJ_ENABLED_OPS: return PKT_NAME_OBJ_REVEAL;
    case OSRS239_ZONE_MAP_PROJANIM_V2: return PKT_NAME_MAP_PROJANIM;
    default: return PKT_NAME_NONE;
    }
}

/*
 * One zone sub-packet, transcribed from RSProt's zone/payload encoders. Returns
 * 1 on success, 0 for an ordinal this client has no decoder for -- which ends
 * the enclosed walk, because the rest of the stream is unreadable without
 * knowing this record's length.
 */
static int
osrs239_read_zone_sub(
    struct Cur* c,
    enum GameProtoPktName name,
    struct PktZoneSubPacket* out)
{
    out->name = name;
    switch( name )
    {
    /* LocAddChangeV2Encoder: p1Alt1 opCount, [ops], p1Alt3 opFlags,
     * p1Alt1 locProperties, p1Alt2 coordInZone, p2Alt3 id.
     *
     * `opCount` leads, and a non-zero count is followed by that many
     * `p1 op-1, pjstr text` pairs that REPLACE the loctype's own right-click
     * options. Skipping the pairs rather than assuming zero is what keeps the
     * cursor honest for the next record in the enclosed stream. */
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        int op_count = g1_alt1(c);

        for( int i = 0; i < op_count && !c->over; i++ )
        {
            (void)g1(c); /* op - 1 */
            free(gjstr_nul(c));
        }
        (void)g1_alt3(c); /* opFlags -- which options are shown */
        out->_loc_add_change.info = g1_alt1(c);
        out->_loc_add_change.pos = g1_alt2(c);
        out->_loc_add_change.loc_id = g2_alt3(c);
        return 1;
    }

    /* LocDelEncoder: p1 locProperties, p1Alt2 coordInZone. */
    case PKT_NAME_LOC_DEL:
        out->_loc_del.info = g1(c);
        out->_loc_del.pos = g1_alt2(c);
        return 1;

    /* LocAnimEncoder: p1 locProperties, p1Alt3 coordInZone, p2Alt3 id. */
    case PKT_NAME_LOC_ANIM:
        out->_loc_anim.info = g1(c);
        out->_loc_anim.pos = g1_alt3(c);
        out->_loc_anim.seq_id = g2_alt3(c);
        return 1;

    /* MapAnimEncoder: p1 height, p2Alt1 id, p2Alt1 delay, p1 coordInZone. */
    case PKT_NAME_MAP_ANIM:
        out->_map_anim.height = g1(c);
        out->_map_anim.id = g2_alt1(c);
        out->_map_anim.delay = g2_alt1(c);
        out->_map_anim.pos = g1(c);
        return 1;

    /* LocMergeEncoder: p1 minX, p2Alt1 index, p1 locProperties, p1 minZ,
     * p2Alt1 id, p2 end, p2Alt3 start, p1Alt1 maxX, p1Alt3 coordInZone,
     * p1Alt3 maxZ. Fourteen bytes at both revisions and every field in a
     * different place. */
    case PKT_NAME_LOC_MERGE:
        out->_loc_merge.west = (int8_t)g1(c);
        out->_loc_merge.pid = g2_alt1(c);
        out->_loc_merge.info = g1(c);
        out->_loc_merge.south = (int8_t)g1(c);
        out->_loc_merge.loc_id = g2_alt1(c);
        out->_loc_merge.end = g2(c);
        out->_loc_merge.start = g2_alt3(c);
        out->_loc_merge.east = (int8_t)g1_alt1(c);
        out->_loc_merge.pos = g1_alt3(c);
        out->_loc_merge.north = (int8_t)g1_alt3(c);
        return 1;

    /*
     * MapProjAnimV2Encoder: p2 startTime, p1 coordInZone, p2Alt2 progress,
     * p2Alt2 id, p2Alt1 endTime, p4Alt1 end.packed, p2Alt3 endHeight,
     * p3Alt2 sourceIndex, p1Alt2 angle, p2 startHeight, p3 targetIndex.
     *
     * Twenty-four bytes against the classic fifteen, and the shape changed
     * rather than just the order. Two conversions belong here rather than
     * downstream:
     *
     *  - the destination is an absolute packed CoordGrid where the classic
     *    sends a pair of signed tile offsets from the source. The offsets are
     *    what the executor consumes, and it knows the zone base, so the
     *    absolute coord is carried through `dst_abs` and resolved there.
     *  - the heights lost their implicit *4 in the client at this revision, so
     *    the wire states them four times larger. `World_ProjectileSpawn` still
     *    applies the multiplier, so they are divided back here; leaving them
     *    scaled makes every projectile fly four times too high.
     */
    case PKT_NAME_MAP_PROJANIM:
    {
        struct PktMapProjAnim* p = &out->_map_projanim;
        int end_packed;

        p->start_delay = g2(c);
        p->pos = g1(c);
        p->arc = g2_alt2(c);
        p->spotanim = g2_alt2(c);
        p->end_delay = g2_alt1(c);
        end_packed = g4_alt1(c);
        p->dst_height = g2_alt3(c) / 4;
        (void)sign24(g3_alt2(c)); /* sourceIndex -- always 0 from this server */
        p->peak = g1_alt2(c);
        p->src_height = g2(c) / 4;
        p->target = sign24(g3(c));
        /*
         * The target index is in the CLIENT's numbering, where the player table
         * is 1..2047 with 0 unused: a player is `-(index + 1)`, one further
         * from zero than the classic `-pid - 1` the executor expects.
         */
        if( p->target < 0 )
            p->target += 1;
        p->dst_abs = 1;
        p->dst_abs_level = (end_packed >> 28) & 0x3;
        p->dst_abs_x = (end_packed >> 14) & 0x3fff;
        p->dst_abs_z = end_packed & 0x3fff;
        p->dx_offset = 0;
        p->dz_offset = 0;
        return 1;
    }

    /* ObjEnabledOpsEncoder: p1Alt1 coordInZone, p2Alt3 id, p1 opFlags.
     * The classic OBJ_REVEAL's count and receiver do not exist here -- this
     * packet only enables ops on an obj that is already on the tile. */
    case PKT_NAME_OBJ_REVEAL:
        out->_obj_reveal.pos = g1_alt1(c);
        out->_obj_reveal.obj_id = g2_alt3(c);
        (void)g1(c); /* opFlags */
        out->_obj_reveal.count = 0;
        out->_obj_reveal.receiver = -1;
        return 1;

    /* ObjDelEncoder: p2 id, p1 coordInZone, p4Alt2 quantity. */
    case PKT_NAME_OBJ_DEL:
        out->_obj_del.obj_id = g2(c);
        out->_obj_del.pos = g1(c);
        (void)g4_alt2(c); /* quantity */
        return 1;

    /* ObjCountEncoder: p4Alt3 oldQuantity, p1Alt1 coordInZone,
     * p4Alt1 newQuantity, p2Alt3 id. Both quantities are four bytes here and
     * two at 230. */
    case PKT_NAME_OBJ_COUNT:
        out->_obj_count.old_count = g4_alt3(c);
        out->_obj_count.pos = g1_alt1(c);
        out->_obj_count.new_count = g4_alt1(c);
        out->_obj_count.obj_id = g2_alt3(c);
        return 1;

    /* ObjAddEncoder: p1Alt1 coordInZone, p2 timeUntilPublic, p1 ownershipType,
     * p2Alt2 id, p1 neverBecomesPublic, p1Alt2 opFlags, p2Alt3 timeUntilDespawn,
     * p4Alt1 quantity. Fourteen bytes against the classic five. */
    case PKT_NAME_OBJ_ADD:
        out->_obj_add.pos = g1_alt1(c);
        (void)g2(c); /* timeUntilPublic */
        (void)g1(c); /* ownershipType */
        out->_obj_add.obj_id = g2_alt2(c);
        (void)g1(c); /* neverBecomesPublic */
        (void)g1_alt2(c); /* opFlags */
        (void)g2_alt3(c); /* timeUntilDespawn */
        out->_obj_add.count = g4_alt1(c);
        return 1;

    default:
        return 0;
    }
}

/* osrs239_entity_info.c */
void
osrs239_playerinfo_init(uint8_t const* data, int len);

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
        /*
         * The six fields are at the END of the payload, not the start.
         *
         * RSProt's RebuildLogin and RebuildNormal share opcode 49 and share an
         * encoder; the login variant simply writes the GPI init block FIRST and
         * then the same three shorts. There is no discriminator on the wire —
         * the length is the discriminator, because the init block is
         * 30 + 2046*18 bits and the fields are always the last six bytes.
         *
         * Reading from the front instead recovers a zone from the init block's
         * leading coordinate bits: a plausible number, a scene built somewhere
         * else or not at all, and nothing malformed to notice.
         */
        if( len < 6 )
            return 0;
        /*
         * Anything before the trailing six bytes is the GPI init block, and it
         * is the only thing that ever seeds the v5 player table: the local
         * player's absolute coordinate plus a rough position for each of the
         * other 2046 slots. Skipping it -- which is what this did -- leaves
         * every PLAYER_INFO after it walking a high-resolution set that has
         * nothing in it, so the local player never appears and no packet is
         * malformed.
         */
        if( len > 6 )
            osrs239_playerinfo_init(data, len - 6);
        c.pos = len - 6;
        (void)g2_alt1(&c); /* worldArea */
        p->zonez = g2_alt2(&c);
        p->zonex = g2_alt2(&c);
        p->region_count = 0;
        p->region_ids = NULL;
        p->region_keys = NULL;
        return c.over ? 0 : 1;
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
        return c.over ? 0 : 1;
    }

    /* IfOpenTopEncoder: p2Alt2 interfaceId. The 230 hybrid writes p2Alt1 --
     * same two bytes, different order, a different interface. */
    case PKT_NAME_IF_OPENTOP:
    {
        struct PktIfOpenTop* p = &out->_if_opentop;
        p->interface_id = g2_alt2(&c);
        return c.over ? 0 : 1;
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
        return c.over ? 0 : 1;
    }

    /* IfCloseSubEncoder: pCombinedId (plain p4). */
    case PKT_NAME_IF_CLOSESUB:
    {
        struct PktIfCloseSub* p = &out->_if_closesub;
        p->target_uid = g4(&c);
        return c.over ? 0 : 1;
    }

    /* VarpSmallEncoder: p1Alt1 value, p2Alt3 id. */
    case PKT_NAME_VARP_SMALL:
    {
        struct PktVarpSmall* p = &out->_varp_small;
        int v = g1_alt1(&c);
        p->value = v > 127 ? v - 256 : v;
        p->variable = g2_alt3(&c);
        return c.over ? 0 : 1;
    }

    /* VarpLargeEncoder: p2Alt2 id, p4Alt1 value. Note the id comes FIRST here
     * and second in the small form; there is no "id first" rule. */
    case PKT_NAME_VARP_LARGE:
    {
        struct PktVarpLarge* p = &out->_varp_large;
        p->variable = g2_alt2(&c);
        p->value = g4_alt1(&c);
        return c.over ? 0 : 1;
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
        return c.over ? 0 : 1;
    }

    /*
     * MessageGameEncoder: pSmart1or2 type, p1 hasName, [pjstr name], pjstr text.
     *
     * The type is a smart rather than the classic single byte, and the optional
     * sender name has no classic equivalent -- so a 230 reader takes the first
     * character of the message as its "has name" flag and prints the rest.
     */
    case PKT_NAME_MESSAGE_GAME:
    {
        struct PktMessageGame* p = &out->_message_game;
        char* name = NULL;

        (void)gsmart1or2(&c); /* type -- the chat filter tab, unused here */
        if( g1(&c) != 0 )
        {
            name = gjstr_nul(&c);
            free(name); /* carried by no field in the canonical struct yet */
        }
        p->text = gjstr_nul(&c);
        if( c.over || !p->text )
        {
            free(p->text);
            p->text = NULL;
            return 0;
        }
        return 1;
    }

    /* IfSetTextEncoder: pjstr text, pCombinedIdAlt2 uid. The text comes FIRST
     * at this revision and second at 230 -- read the other way round, the uid
     * is the first four characters of the string. */
    case PKT_NAME_IF_SETTEXT:
    {
        struct PktIfSetText* p = &out->_if_settext;

        p->text = gjstr_nul(&c);
        p->component_id = g4_alt2(&c);
        if( c.over || !p->text )
        {
            free(p->text);
            p->text = NULL;
            return 0;
        }
        return 1;
    }

    /* IfSetNpcHeadEncoder: pCombinedId uid, p2Alt3 npc. */
    case PKT_NAME_IF_SETNPCHEAD:
    {
        struct PktIfSetNpcHead* p = &out->_if_setnpchead;
        p->component_id = g4(&c);
        p->npc_id = g2_alt3(&c);
        return c.over ? 0 : 1;
    }

    /* IfSetAnimEncoder: pCombinedIdAlt1 uid, p2Alt1 anim. */
    case PKT_NAME_IF_SETANIM:
    {
        struct PktIfSetAnim* p = &out->_if_setanim;
        p->component_id = g4_alt1(&c);
        p->anim_id = g2_alt1(&c);
        return c.over ? 0 : 1;
    }

    /* IfSetPlayerHeadEncoder: pCombinedIdAlt2 uid, and nothing else. */
    case PKT_NAME_IF_SETPLAYERHEAD:
    {
        struct PktIfSetPlayerHead* p = &out->_if_setplayerhead;
        p->component_id = g4_alt2(&c);
        return c.over ? 0 : 1;
    }

    /* IfSetScrollPosEncoder: pCombinedIdAlt3 uid, p2 scrollPos. Six bytes —
     * the shared parser's g2+g2 layout is the classic four and asserts here. */
    case PKT_NAME_IF_SETSCROLLPOS:
    {
        struct PktIfSetScrollPos* p = &out->_if_setscrollpos;
        p->component_id = g4_alt3(&c);
        p->pos = g2(&c);
        return c.over ? 0 : 1;
    }

    /*
     * SetMapFlagV2Encoder: p4 packed CoordGrid.
     *
     * Four bytes carrying an ABSOLUTE coord where the classic sends two
     * scene-local tile bytes with 255,255 meaning "clear". There is no clear
     * sentinel here; a cleared flag is coord 0. `absolute` tells the executor
     * to subtract the scene origin, which this layer does not know.
     */
    case PKT_NAME_UNSET_MAP_FLAG:
    {
        struct PktSetMapFlag* p = &out->_set_map_flag;
        int packed = g4(&c);

        if( c.over )
            return 0;
        p->x = (packed >> 14) & 0x3fff;
        p->z = packed & 0x3fff;
        p->clear = packed == 0;
        p->absolute = 1;
        return 1;
    }

    /*
     * ChatFilterSettingsEncoder: p1Alt1 public, p1Alt3 trade.
     *
     * TWO bytes, not the classic three -- the private-chat filter moved to its
     * own packet. Reading three desynchronises nothing (the frame is fixed) but
     * assigns the trade mode to the private field and garbage to trade.
     */
    case PKT_NAME_CHAT_FILTER_SETTINGS:
    {
        struct PktChatFilterSettings* p = &out->_chat_filter_settings;
        p->chat_public_mode = g1_alt1(&c);
        p->chat_trade_mode = g1_alt3(&c);
        p->chat_private_mode = 0;
        return c.over ? 0 : 1;
    }

    /* SynthSoundEncoder: p2 id, p1 loops, p2 delay -- the classic layout, which
     * is why it is stated rather than delegated: the 230 hybrid's SYNTH_SOUND
     * is lc254-shaped and reads `loops` as part of the id. */
    case PKT_NAME_SYNTH_SOUND:
    {
        struct PktSynthSound* p = &out->_synth_sound;
        p->id = g2(&c);
        p->loops = g1(&c);
        p->delay = g2(&c);
        return c.over ? 0 : 1;
    }

    /*
     * UpdateInvFullEncoder:    p4 combinedId, p2 inventoryId, p2 capacity,
     *                          then per slot p1Alt3 count (255 escapes to
     *                          p4Alt3), p2Alt1 id+1.
     * UpdateInvPartialEncoder: p4 combinedId, p2 inventoryId, then per changed
     *                          slot pSmart1or2 slot, p2 id+1, p1 count (255
     *                          escapes to p4).
     *
     * The full form puts the COUNT first and the id second; the partial form
     * the other way round, and their overflow escapes use different byte
     * orders. The combined id is always -1 at this revision (a positive one
     * crashes a real client), so the inventory id is the only key.
     */
    case PKT_NAME_UPDATE_INV_FULL:
    {
        struct PktUpdateInvFull* p = &out->_update_inv_full;
        int capacity;

        p->component_id = g4(&c);
        p->inv_id = g2(&c);
        capacity = g2(&c);
        if( c.over || capacity < 0 || capacity > 4096 )
            return 0;
        p->size = capacity;
        p->obj_ids = (int*)malloc((size_t)capacity * sizeof(int));
        p->obj_counts = (int*)malloc((size_t)capacity * sizeof(int));
        if( !p->obj_ids || !p->obj_counts )
        {
            free(p->obj_ids);
            free(p->obj_counts);
            p->obj_ids = NULL;
            p->obj_counts = NULL;
            p->size = 0;
            return 0;
        }
        for( int i = 0; i < capacity; i++ )
        {
            int count = g1_alt3(&c);

            if( count == 255 )
                count = g4_alt3(&c);
            p->obj_counts[i] = count;
            p->obj_ids[i] = g2_alt1(&c) - 1;
        }
        if( c.over )
        {
            free(p->obj_ids);
            free(p->obj_counts);
            p->obj_ids = NULL;
            p->obj_counts = NULL;
            p->size = 0;
            return 0;
        }
        return 1;
    }

    case PKT_NAME_UPDATE_INV_PARTIAL:
    {
        struct PktUpdateInvPartial* p = &out->_update_inv_partial;
        int max_entries;
        int written = 0;

        p->component_id = g4(&c);
        p->inv_id = g2(&c);
        p->count = 0;
        p->entries = NULL;
        if( c.over )
            return 0;
        /* Smallest record is 4 bytes (1 slot + 2 obj + 1 count). */
        max_entries = (len - c.pos) / 4 + 1;
        p->entries = (struct PktUpdateInvPartialEntry*)malloc(
            (size_t)max_entries * sizeof(struct PktUpdateInvPartialEntry));
        if( !p->entries )
            return 0;
        while( c.pos < len && written < max_entries )
        {
            struct PktUpdateInvPartialEntry* entry = &p->entries[written];
            int count;

            entry->slot = gsmart1or2(&c);
            entry->obj_id = g2(&c) - 1;
            count = g1(&c);
            if( count == 255 )
                count = g4(&c);
            entry->count = count;
            if( c.over )
                break;
            written++;
        }
        if( c.over || c.pos != len )
        {
            free(p->entries);
            p->entries = NULL;
            p->count = 0;
            return 0;
        }
        p->count = written;
        return 1;
    }

    /*
     * RunClientScriptEncoder: pjstr types, the arguments in REVERSE order, then
     * p4 the script id.
     *
     * Two differences from the classic packet, both silent. The type string is
     * NUL-terminated here and newline-terminated at 230 -- a reader looking for
     * the wrong terminator consumes the first argument as part of the string --
     * and 's' arguments are NUL-terminated strings rather than newline ones.
     */
    case PKT_NAME_RUNCLIENTSCRIPT:
    {
        struct PktRunClientScript* p = &out->_runclientscript;
        char types[PKT_RUNCLIENTSCRIPT_ARG_MAX + 1];
        int argc = 0;
        int terminated = 0;

        for( ;; )
        {
            int ch = g1(&c);

            if( c.over )
                break;
            if( ch == 0 || ch == '\n' )
            {
                terminated = 1;
                break;
            }
            /* More arguments than the struct holds fails the packet: stopping
             * at the cap and decoding anyway leaves the unread type characters
             * in the stream, so every argument after it comes off the wrong
             * bytes and the parse still succeeds. */
            if( argc >= PKT_RUNCLIENTSCRIPT_ARG_MAX )
                return 0;
            types[argc++] = (char)ch;
        }
        types[argc] = '\0';
        if( c.over || !terminated )
            return 0;

        memset(p, 0, sizeof(*p));
        p->argc = argc;
        for( int i = argc - 1; i >= 0; i-- )
        {
            if( types[i] == 's' )
            {
                char* s = gjstr_nul(&c);
                int n = 0;

                if( s )
                {
                    while( s[n] && n < PKT_RUNCLIENTSCRIPT_STR_LEN - 1 )
                    {
                        p->strv[i][n] = s[n];
                        n++;
                    }
                    free(s);
                }
                p->strv[i][n] = '\0';
                p->str_mask |= 1u << i;
            }
            else
            {
                p->intv[i] = g4(&c);
            }
        }
        p->script_id = g4(&c);
        return c.over ? 0 : 1;
    }

    /*
     * Zone headers. Three fields, three orders, one per packet -- and a LEVEL
     * the classic pair does not carry:
     *
     *   FULL_FOLLOWS      p1Alt3 zoneZ, p1Alt2 zoneX, p1     level
     *   PARTIAL_FOLLOWS   p1Alt1 zoneZ, p1     zoneX, p1Alt2 level
     *
     * Reading two bytes here is what a 230 reader does, and the assert that
     * catches it (`buffer.position == data_size`) is the only reason it is not
     * silent: the third byte would otherwise be read as the next opcode.
     *
     * The level is dropped rather than carried: the executor takes the level
     * from the local player, which is the same value for every zone the server
     * addresses this client with.
     */
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        struct PktUpdateZoneFullFollows* p = &out->_update_zone_full_follows;
        p->base_z = g1_alt3(&c);
        p->base_x = g1_alt2(&c);
        (void)g1(&c); /* level */
        return c.over ? 0 : 1;
    }

    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        struct PktUpdateZonePartialFollows* p = &out->_update_zone_partial_follows;
        p->base_z = g1_alt1(&c);
        p->base_x = g1(&c);
        (void)g1_alt2(&c); /* level */
        return c.over ? 0 : 1;
    }

    /*
     * UPDATE_ZONE_PARTIAL_ENCLOSED: p1Alt2 level, p1Alt1 zoneZ, p1Alt1 zoneX,
     * then a stream of zone sub-packets addressed by an ENUM ORDINAL that is
     * neither the top-level opcode nor RSProt's own zone-prot id.
     */
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        struct PktUpdateZoneEnclosed* enc = &out->_update_zone_enclosed;
        int cap = 16;

        (void)g1_alt2(&c); /* level */
        enc->base_z = g1_alt1(&c);
        enc->base_x = g1_alt1(&c);
        enc->count = 0;
        enc->entries =
            (struct PktZoneSubPacket*)malloc((size_t)cap * sizeof(*enc->entries));
        if( !enc->entries )
            return 0;
        while( c.pos < len && !c.over )
        {
            int ordinal = g1(&c);
            enum GameProtoPktName sub = osrs239_zone_name(ordinal);

            if( enc->count == cap )
            {
                struct PktZoneSubPacket* grown = (struct PktZoneSubPacket*)realloc(
                    enc->entries, (size_t)(cap * 2) * sizeof(*enc->entries));

                if( !grown )
                    break;
                cap *= 2;
                enc->entries = grown;
            }
            if( !osrs239_read_zone_sub(&c, sub, &enc->entries[enc->count]) )
            {
                fprintf(
                    stderr,
                    "osrs239: ZONE_ENCLOSED unknown sub-ordinal %d at %d/%d\n",
                    ordinal,
                    c.pos,
                    len);
                break;
            }
            enc->count++;
        }
        return 1;
    }

    /*
     * The zone sub-packets in `follows` mode: same payloads, addressed by their
     * own top-level opcode instead of by an ordinal inside an enclosed blob.
     */
    case PKT_NAME_LOC_ADD_CHANGE:
    case PKT_NAME_LOC_DEL:
    case PKT_NAME_LOC_ANIM:
    case PKT_NAME_LOC_MERGE:
    case PKT_NAME_OBJ_ADD:
    case PKT_NAME_OBJ_DEL:
    case PKT_NAME_OBJ_REVEAL:
    case PKT_NAME_OBJ_COUNT:
    case PKT_NAME_MAP_ANIM:
    case PKT_NAME_MAP_PROJANIM:
    {
        struct PktZoneSubPacket sub;

        if( !osrs239_read_zone_sub(&c, (enum GameProtoPktName)pkt_name, &sub) || c.over )
            return 0;
        /* The canonical RevPacket keeps each sub-packet in its own union slot;
         * PktZoneSubPacket is that same union with a name beside it. */
        switch( pkt_name )
        {
        case PKT_NAME_LOC_ADD_CHANGE: out->_loc_add_change = sub._loc_add_change; break;
        case PKT_NAME_LOC_DEL:        out->_loc_del = sub._loc_del; break;
        case PKT_NAME_LOC_ANIM:       out->_loc_anim = sub._loc_anim; break;
        case PKT_NAME_LOC_MERGE:      out->_loc_merge = sub._loc_merge; break;
        case PKT_NAME_OBJ_ADD:        out->_obj_add = sub._obj_add; break;
        case PKT_NAME_OBJ_DEL:        out->_obj_del = sub._obj_del; break;
        case PKT_NAME_OBJ_REVEAL:     out->_obj_reveal = sub._obj_reveal; break;
        case PKT_NAME_OBJ_COUNT:      out->_obj_count = sub._obj_count; break;
        case PKT_NAME_MAP_ANIM:       out->_map_anim = sub._map_anim; break;
        default:                      out->_map_projanim = sub._map_projanim; break;
        }
        return 1;
    }

    /*
     * CamMoveToV2Encoder: p2Alt1 x, p1Alt2 rate, p2Alt1 height, p1Alt2 rate2,
     * p2 z.  CamLookAtV2Encoder: p2 z, p2Alt2 height, p1 rate2, p1Alt3 rate,
     * p2Alt1 x.
     *
     * Eight bytes at this revision against the classic six, and the coordinates
     * are two bytes each rather than one -- the classic packs a scene-local
     * tile into a byte, which cannot address a coordinate outside the scene.
     * The canonical struct's `local_x`/`local_z` keep their meaning; what
     * changed is only the width.
     */
    case PKT_NAME_CAM_MOVETO:
    {
        struct PktCamMoveTo* p = &out->_cam_moveto;
        p->local_x = g2_alt1(&c);
        p->rate = g1_alt2(&c);
        p->height = g2_alt1(&c);
        p->rate2 = g1_alt2(&c);
        p->local_z = g2(&c);
        return c.over ? 0 : 1;
    }

    case PKT_NAME_CAM_LOOKAT:
    {
        struct PktCamLookAt* p = &out->_cam_lookat;
        p->local_z = g2(&c);
        p->height = g2_alt2(&c);
        p->rate2 = g1(&c);
        p->rate = g1_alt3(&c);
        p->local_x = g2_alt1(&c);
        return c.over ? 0 : 1;
    }

    /* CamShakeEncoder: p1 axis, p1 random, p1 amplitude, p1 rate -- unchanged
     * in shape, stated here because an untranscribed packet is refused. */
    case PKT_NAME_CAM_SHAKE:
    {
        struct PktCamShake* p = &out->_cam_shake;
        p->axis = g1(&c);
        p->amplitude = g1(&c);
        p->frequency = g1(&c);
        p->speed = g1(&c);
        return c.over ? 0 : 1;
    }

    /*
     * Still refused, and each for its own reason rather than as a group:
     *
     *   IF_SETMODEL   V2 addresses the component by a 4-byte combined uid.
     *   REBUILD_REGION V2, like REBUILD_NORMAL, but its zone descriptor grid
     *                 has not been re-read against the V2 encoder.
     *
     * Returning 0 -- not -1 -- is what makes this a drop. `net.c` reads a
     * negative as "not mine, try the shared parser", which would decode both
     * with the classic layout: exactly the corruption this list exists to
     * avoid.
     *
     * PLAYER_INFO and NPC_INFO are deliberately NOT here. They delegate, and
     * that is not the shared parser decoding them -- it only copies the command
     * stream out of the frame. The v5 codec that reads it is a whole reader in
     * the rev table's `player_info_read` / `npc_info_read` slots, applied by
     * the exec task. Dropping them here instead, which is what this list did,
     * removes the local player and every npc while leaving the stream itself
     * perfectly well-formed.
     */
    case PKT_NAME_REBUILD_REGION:
    case PKT_NAME_IF_SETMODEL:
        return 0;

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
