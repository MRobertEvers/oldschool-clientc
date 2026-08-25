#include "net/rev/gameproto_revisions.h"
#include <assert.h>
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"
#include "net/jbase37.h"
#include "zoneprot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsprot_buffer.h"
#include "rsprot_exec.h"

#include "packets/loc_anim.h"
#include "packets/loc_del.h"
#include "packets/loc_merge.h"
#include "packets/map_anim.h"
#include "packets/map_projanim_v2.h"
#include "packets/obj_count.h"
#include "packets/obj_del.h"
#include "packets/loc_add_change_v2.h"
#include "packets/rebuild_normal_v2.h"
#include "packets/rebuild_region_v2.h"
#include "packets/update_zone_partial_enclosed.h"
#include "packets/obj_add.h"
#include "packets/obj_enabled_ops.h"
#include "packets/sound_area.h"

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
/*
 * The byte cursor is RSProt_Buffer's.
 *
 * This file used to define a private `struct Cur` and seventeen readers over
 * it — g1, g1_alt1..3, g2, g2_alt1..3, g3, g3_alt2, g4, g4_alt1..3, g1b,
 * gsmart1or2, gjstr_nul. Every one had a counterpart in the buffer library and
 * was a separate place for a byte order to be wrong; `gsmart1or2` alone was one
 * of five copies of that encoding in src/net. See
 * `docs/BUFFER_ACCESSOR_AUDIT.md` and `3rd/rsprot/src/rsprot_buffer.h`.
 *
 * The names now carry the byte order instead of an `alt` ordinal, so a call
 * site states what it reads: `g4_alt3` was the 2,1,4,3 permutation and is
 * spelled `RSProt_BufferG4_2143`. The comment that used to sit on that reader —
 * warning that swapping its two low bytes yields a combined id of
 * (interface, child << 8), which names a component the interface does not have,
 * so IF_OPENSUB skips it and IF_SETEVENTS arms nothing — is now a property of a
 * name that says 2143 rather than a note someone has to find.
 *
 * Contract is unchanged: a read past the end latches an error and returns 0
 * rather than running off the buffer, and the caller drops the packet instead
 * of applying half-decoded state. The cursor's fields are renamed with it —
 * `c.err` for the old `c.over`, `c.rpos` for `c.pos`, `c.cap` for `c.len`.
 */

static int
sign24(int v)
{
    return (v & 0x800000) ? v - 0x1000000 : v;
}

/* Bounds-checked MSB-first bit read used by the instanced-region descriptor
 * grid. Net_BitBuffer deliberately asserts on malformed input; a network
 * decoder must reject a short frame instead of terminating the client. */
static uint32_t
gbits_checked(
    uint8_t const* data,
    int len,
    int* bit_pos,
    int count,
    int* ok)
{
    uint32_t value = 0;

    if( !*ok || count < 0 || count > 32 || *bit_pos < 0 ||
        *bit_pos + count > len * 8 )
    {
        *ok = 0;
        return 0;
    }
    for( int i = 0; i < count; i++ )
    {
        int pos = (*bit_pos)++;
        value = (value << 1) | ((data[pos >> 3] >> (7 - (pos & 7))) & 1u);
    }
    return value;
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
gjstr_nul(RSProt_Buffer* c)
{
    int start = c->rpos;
    int end = start;
    char* out;

    while( end < c->cap && c->data[end] != 0 )
        end++;
    if( end >= c->cap )
    {
        c->err = 1;
        return NULL;
    }
    out = (char*)malloc((size_t)(end - start) + 1);
    assert(out);
    memcpy(out, c->data + start, (size_t)(end - start));
    out[end - start] = '\0';
    c->rpos = end + 1;
    return out;
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
    case OSRS239_ZONE_SOUND_AREA: return PKT_NAME_SOUND_AREA;
    default: return PKT_NAME_NONE;
    }
}

/*
 * One zone sub-packet, decoded by the GENERATED rsprot codec for its layout.
 *
 * The codecs in `3rd/rsprot/packets/` are the statement of what these bytes
 * are, per layout version, for every vendored revision. This function used to
 * re-transcribe all ten of them by hand, which is the thing the codec layer
 * exists to stop: a second transcription that can disagree with the first and
 * has nothing to say so.
 *
 * A zone sub-packet is decoded from the ENCLOSED stream's shared cursor rather
 * than from a payload of its own, which is why this does not go through
 * `rsprot_bridge_parse` like a top-level packet. It does not need to:
 * `RSProt_Buffer` IS `RsprotBuf`, so an executor can run a codec against this
 * cursor in place and the reads advance it exactly as the hand-written ones
 * did. That is the whole reason the buffer types were unified.
 *
 * What remains here is the mapping from RSProt's field names to this project's
 * canonical `Pkt*` structs, plus the handful of real conversions the client
 * needs (the projectile height scale, the target-index numbering). Those are
 * adapter decisions, not wire layout, and stating them beside the packet is
 * where they belong.
 *
 * Returns 1 on success, 0 for an ordinal this client has no decoder for --
 * which ends the enclosed walk, because the rest of the stream is unreadable
 * without knowing this record's length.
 */
static int
zone_run(RSProt_Buffer* c, RsprotVersionRange const* ranges, int count, void* msg)
{
    RsprotExec x;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, 239);

    if( !fn )
        return 0;
    rsprot_exec_decode(&x, c);
    fn(&x, msg);
    return rsprot_exec_ok(&x) && !c->err;
}

#define ZONE_RUN(table, type, var)                                            \
    type var;                                                                 \
    memset(&var, 0, sizeof(var));                                             \
    if( !zone_run(c, table, table##_count, &var) )                            \
    return 0

static int
osrs239_read_zone_sub(
    RSProt_Buffer* c,
    enum GameProtoPktName name,
    struct PktZoneSubPacket* out)
{
    out->name = name;
    switch( name )
    {
    /*
     * LOC_ADD_CHANGE is the one still decoded by hand. Its rev239 layout iterates a
     * MAP of (op index, replacement text) pairs and writes the index as `key - 1`,
     * so generating it needs two constructs the generator does not have: map
     * iteration (`for_each_pair`, parsed but never emitted) and an offset-encoded
     * field, which has no lvalue for the decode direction to write back through.
     * Both are features, not bugs; OBJ_ADD was the bug and is generated now.
     *
     * The old note follows: it is the one that
     * cannot be generated yet: its layout carries a COUNTED ARRAY of
     * `p1 op-1, pjstr text` pairs replacing the loctype's right-click options,
     * and the generator refuses array/nested element structs rather than emit a
     * struct union it cannot model (see tools/rsprot_gen_codec.py). Skipping the
     * pairs rather than assuming zero is what keeps the cursor honest for the
     * next record in the enclosed stream.
     *
     * LocAddChangeV2Encoder: p1Alt1 opCount, [ops], p1Alt3 opFlags,
     * p1Alt1 locProperties, p1Alt2 coordInZone, p2Alt3 id.
     */
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        /*
         * The op array decodes into a caller-provided buffer: the codec loops
         * `ops_count` times writing through `m.ops`, and nothing else allocates
         * it.
         *
         * 256 entries, and the size is the safety argument rather than a
         * guess. `opCount` is a single byte (`p1Alt1`), so it cannot exceed 255
         * whatever the wire says — the buffer cannot be overrun by a malformed
         * packet. Checking the count after the fact would be too late: the
         * codec reads it and loops on it in the same call.
         *
         * The op text is borrowed from the buffer, so it is COPIED into the
         * packet here rather than kept as a pointer: the payload outlives the
         * buffer by the time the exec layer reads it.
         */
        Rsprot_MsgLocAddChangeV2_opsElem ops[256];
        MsgLocAddChangeV2 m;

        memset(&m, 0, sizeof(m));
        m.ops = ops;
        if( !zone_run(c, rsprot_loc_add_change_v2_out,
                      rsprot_loc_add_change_v2_out_count, &m) )
            return 0;
        memset(&out->_loc_add_change, 0, sizeof(out->_loc_add_change));
        out->_loc_add_change.info = m.loc_properties_packed;
        out->_loc_add_change.pos = m.coord_in_zone_packed;
        out->_loc_add_change.loc_id = m.id;
        out->_loc_add_change.op_flags = m.op_flags;
        /*
         * `key` is the ONE-based op number, not the slot index. The wire byte
         * is the zero-based slot (the reference indexes its five-slot array
         * with it, unadjusted) and the codec's `RSPROT_XFORM(..., -1)` adds one
         * back on the way in, so the array index is `key - 1`. Taking `key`
         * for the index shifts every replacement one slot along, which reads
         * as the label landing on the wrong menu row rather than as a decode
         * fault. Anything outside the five slots is dropped, as the reference
         * drops it.
         */
        for( int i = 0; i < m.ops_count; i++ )
        {
            int slot = ops[i].key - 1;

            if( slot < 0 || slot >= 5 || !ops[i].value )
                continue;
            snprintf(out->_loc_add_change.ops[slot],
                     sizeof(out->_loc_add_change.ops[slot]), "%s", ops[i].value);
        }
        return 1;
    }

    case PKT_NAME_LOC_DEL:
    {
        ZONE_RUN(rsprot_loc_del_out, MsgLocDel, m);
        out->_loc_del.info = m.loc_properties_packed;
        out->_loc_del.pos = m.coord_in_zone_packed;
        return 1;
    }

    case PKT_NAME_LOC_ANIM:
    {
        ZONE_RUN(rsprot_loc_anim_out, MsgLocAnim, m);
        out->_loc_anim.info = m.loc_properties_packed;
        out->_loc_anim.pos = m.coord_in_zone_packed;
        out->_loc_anim.seq_id = m.id;
        return 1;
    }

    case PKT_NAME_MAP_ANIM:
    {
        ZONE_RUN(rsprot_map_anim_out, MsgMapAnim, m);
        out->_map_anim.height = m.height;
        out->_map_anim.id = m.id;
        out->_map_anim.delay = m.delay;
        out->_map_anim.pos = m.coord_in_zone_packed;
        return 1;
    }

    /*
     * Leaving this unmapped did more than lose the sound. An ordinal with no
     * case falls through to PKT_NAME_NONE, this function returns 0, and the
     * enclosed-zone loop `break`s -- so one area sound discarded every
     * *remaining* sub-packet in that batch: loc changes, obj spawns,
     * projectiles. It logged `unknown sub-ordinal 14` and nothing else.
     */
    case PKT_NAME_SOUND_AREA:
    {
        ZONE_RUN(rsprot_sound_area_out, MsgSoundArea, m);
        out->_sound_area.pos = m.coord_in_zone_packed;
        out->_sound_area.radius = m.range;
        out->_sound_area.inner = m.drop_off_range;
        out->_sound_area.loops = m.loops;
        out->_sound_area.id = m.id;
        out->_sound_area.delay = m.delay;
        return 1;
    }

    /*
     * The four min/max fields are signed bytes on the wire and the canonical
     * struct stores them signed; the codec hands back the raw byte, so the cast
     * is this adapter's job rather than the codec's.
     */
    case PKT_NAME_LOC_MERGE:
    {
        ZONE_RUN(rsprot_loc_merge_out, MsgLocMerge, m);
        out->_loc_merge.west = (int8_t)m.min_x;
        out->_loc_merge.pid = m.index;
        out->_loc_merge.info = m.loc_properties_packed;
        out->_loc_merge.south = (int8_t)m.min_z;
        out->_loc_merge.loc_id = m.id;
        out->_loc_merge.end = m.end;
        out->_loc_merge.start = m.start;
        out->_loc_merge.east = (int8_t)m.max_x;
        out->_loc_merge.pos = m.coord_in_zone_packed;
        out->_loc_merge.north = (int8_t)m.max_z;
        return 1;
    }

    /*
     * Two real conversions, and they are the reason this arm is longer than the
     * others. Neither is wire layout:
     *
     *  - the destination is an absolute packed CoordGrid where the classic
     *    packet sends a pair of signed tile offsets from the source. The
     *    offsets are what the executor consumes and it knows the zone base, so
     *    the absolute coord is carried through `dst_abs` and resolved there.
     *  - the heights lost their implicit *4 in the client at this revision, so
     *    the wire states them four times larger. `World_ProjectileSpawn` still
     *    applies the multiplier, so they are divided back here; leaving them
     *    scaled makes every projectile fly four times too high.
     */
    case PKT_NAME_MAP_PROJANIM:
    {
        struct PktMapProjAnim* p = &out->_map_projanim;

        ZONE_RUN(rsprot_map_projanim_v2_out, MsgMapProjAnimV2, m);
        p->start_delay = m.start_time;
        p->pos = m.coord_in_zone_packed;
        p->arc = m.progress;
        p->spotanim = m.id;
        p->end_delay = m.end_time;
        p->dst_height = m.end_height / 4;
        p->peak = m.angle;
        p->src_height = m.start_height / 4;
        p->target = sign24(m.target_index);
        /*
         * Keep the CLIENT index verbatim. The rev-239 PLAYER_INFO/GPI path
         * stores that same 1-based index in WorldEntity_Player.server_pid, and
         * World_ProjectileTrackTarget resolves a negative target as
         * `-target - 1`. Converting this to the mock's 0-based pool slot made
         * every player target miss the world entity it was meant to follow.
         */
        p->dst_abs = 1;
        p->dst_abs_level = (m.end_packed >> 28) & 0x3;
        p->dst_abs_x = (m.end_packed >> 14) & 0x3fff;
        p->dst_abs_z = m.end_packed & 0x3fff;
        p->dx_offset = 0;
        p->dz_offset = 0;
        return 1;
    }

    /* The classic OBJ_REVEAL's count and receiver do not exist at this
     * revision -- ObjEnabledOps only enables ops on an obj already on the
     * tile -- so they are stated as absent rather than left uninitialised. */
    case PKT_NAME_OBJ_REVEAL:
    {
        ZONE_RUN(rsprot_obj_enabled_ops_out, MsgObjEnabledOps, m);
        out->_obj_reveal.pos = m.coord_in_zone_packed;
        out->_obj_reveal.obj_id = m.id;
        out->_obj_reveal.count = 0;
        out->_obj_reveal.receiver = -1;
        return 1;
    }

    case PKT_NAME_OBJ_DEL:
    {
        ZONE_RUN(rsprot_obj_del_out, MsgObjDel, m);
        out->_obj_del.obj_id = m.id;
        out->_obj_del.pos = m.coord_in_zone_packed;
        return 1;
    }

    case PKT_NAME_OBJ_COUNT:
    {
        ZONE_RUN(rsprot_obj_count_out, MsgObjCount, m);
        out->_obj_count.old_count = m.old_quantity;
        out->_obj_count.pos = m.coord_in_zone_packed;
        out->_obj_count.new_count = m.new_quantity;
        out->_obj_count.obj_id = m.id;
        return 1;
    }

    /* Fourteen bytes against the classic five; most of them are fields this
     * client has no home for, which is why only three are copied out. */
    case PKT_NAME_OBJ_ADD:
    {
        ZONE_RUN(rsprot_obj_add_out, MsgObjAdd, m);
        out->_obj_add.pos = m.coord_in_zone_packed;
        out->_obj_add.obj_id = m.id;
        out->_obj_add.count = m.quantity;
        return 1;
    }

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
    RSProt_Buffer c;
    RSProt_BufferWrapRead(&c, data, len);

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
        /*
         * Seek to the trailing six bytes and run the codec there.
         *
         * The seek stays in this file rather than becoming a codec feature: the
         * fields are last because the LOGIN variant prepends a GPI init block
         * and shares this opcode, so "where the fields start" is a property of
         * the frame's length, not of the layout. Teaching a codec to seek
         * relative to the payload end would make every codec's start position a
         * question; doing it here keeps that to one packet.
         */
        c.rpos = len - 6;
        {
            MsgRebuildNormalV2 hdr;

            memset(&hdr, 0, sizeof(hdr));
            if( !zone_run(&c, rsprot_rebuild_normal_v2_out,
                          rsprot_rebuild_normal_v2_out_count, &hdr) )
                return 0;
            p->zonez = hdr.zone_z;
            p->zonex = hdr.zone_x;
        }
        p->region_count = 0;
        p->region_ids = NULL;
        p->region_keys = NULL;
        return c.err ? 0 : 1;
    }

    /*
     * IfSetEventsV2Encoder:
     *   p2Alt2 end, p4 events2, p4Alt2 events1, p2 start, pCombinedIdAlt3 uid
     *
     * V2 carries TWO 32-bit masks where the older packet carried one, and in a
     * different field order -- 12 bytes became 16. The classic interaction
     * mask is reconstructed exactly as the 239 client does for menu/input use.
     */
    case PKT_NAME_IF_SETEVENTS:
    {
        struct PktIfSetEvents* p = &out->_if_setevents;
        p->to = (int16_t)RSProt_BufferG2Be_add128(&c);
        p->events2 = (uint32_t)RSProt_BufferG4Be(&c);
        p->events1 = (uint32_t)RSProt_BufferG4_3412(&c);
        p->events = (int)(p->events1 | ((p->events2 & UINT32_C(0x3ff)) << 1));
        p->from = (int16_t)RSProt_BufferG2Be(&c);
        p->component_id = RSProt_BufferG4_2143(&c);
        return c.err ? 0 : 1;
    }

    case PKT_NAME_IF_RESYNC_V2:
    {
        struct PktIfResync* p = &out->_if_resync;
        int remaining;

        memset(p, 0, sizeof(*p));
        p->root_interface_id = RSProt_BufferG2Be(&c);
        if( p->root_interface_id == 65535 )
            p->root_interface_id = -1;
        p->mount_count = RSProt_BufferG2Be(&c);
        if( c.err || p->mount_count < 0 || p->mount_count > 4096 ||
            c.rpos + p->mount_count * 7 > len )
            return 0;
        if( p->mount_count )
        {
            p->mounts = malloc((size_t)p->mount_count * sizeof(*p->mounts));
            assert(p->mounts);
        }
        for( int i = 0; i < p->mount_count; i++ )
        {
            p->mounts[i].target_uid = RSProt_BufferG4Be(&c);
            p->mounts[i].interface_id = RSProt_BufferG2Be(&c);
            p->mounts[i].type = RSProt_BufferG1(&c);
        }
        remaining = len - c.rpos;
        if( c.err || remaining < 0 || remaining % 16 != 0 )
        {
            free(p->mounts);
            p->mounts = NULL;
            p->mount_count = 0;
            return 0;
        }
        p->event_count = remaining / 16;
        if( p->event_count )
        {
            p->events = malloc((size_t)p->event_count * sizeof(*p->events));
            assert(p->events);
        }
        for( int i = 0; i < p->event_count; i++ )
        {
            p->events[i].component_id = RSProt_BufferG4Be(&c);
            p->events[i].from = (int16_t)RSProt_BufferG2Be(&c);
            p->events[i].to = (int16_t)RSProt_BufferG2Be(&c);
            p->events[i].events1 = (uint32_t)RSProt_BufferG4Be(&c);
            p->events[i].events2 = (uint32_t)RSProt_BufferG4Be(&c);
        }
        return c.err ? 0 : 1;
    }

    /* IfOpenTopEncoder: p2Alt2 interfaceId. The 230 hybrid writes p2Alt1 --
     * same two bytes, different order, a different interface. */
    case PKT_NAME_IF_OPENTOP:
    {
        struct PktIfOpenTop* p = &out->_if_opentop;
        p->interface_id = RSProt_BufferG2Be_add128(&c);
        return c.err ? 0 : 1;
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
        p->interface_id = RSProt_BufferG2Le_add128(&c);
        p->target_uid = RSProt_BufferG4_2143(&c);
        p->type = RSProt_BufferG1(&c);
        return c.err ? 0 : 1;
    }

    /* IfCloseSubEncoder: pCombinedId (plain p4). */
    case PKT_NAME_IF_CLOSESUB:
    {
        struct PktIfCloseSub* p = &out->_if_closesub;
        p->target_uid = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;
    }

    case PKT_NAME_IF_MOVESUB:
        out->_if_movesub.dest_uid = RSProt_BufferG4Le(&c);
        out->_if_movesub.source_uid = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_CLEARINV:
        out->_if_clearinv.component_id = RSProt_BufferG4_3412(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETCOLOUR:
        out->_if_setcolour.colour = RSProt_BufferG2Be(&c);
        out->_if_setcolour.component_id = RSProt_BufferG4_3412(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETHIDE:
        out->_if_sethide.component_id = RSProt_BufferG4_2143(&c);
        out->_if_sethide.hide = RSProt_BufferG1_add128(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETMODEL:
        out->_if_setmodel.model_id = RSProt_BufferG4_3412(&c);
        out->_if_setmodel.component_id = RSProt_BufferG4Le(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETOBJECT:
        out->_if_setobject.component_id = RSProt_BufferG4_3412(&c);
        out->_if_setobject.obj_id = RSProt_BufferG2Be_add128(&c);
        out->_if_setobject.zoom = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETPOSITION:
        out->_if_setposition.z = (int16_t)RSProt_BufferG2Le_add128(&c);
        out->_if_setposition.component_id = RSProt_BufferG4_3412(&c);
        out->_if_setposition.x = (int16_t)RSProt_BufferG2Le_add128(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETROTATESPEED:
        out->_if_setrotatespeed.y_speed = (int16_t)RSProt_BufferG2Be(&c);
        out->_if_setrotatespeed.x_speed = (int16_t)RSProt_BufferG2Be_add128(&c);
        out->_if_setrotatespeed.component_id = RSProt_BufferG4_2143(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETANGLE:
        out->_if_setangle.component_id = RSProt_BufferG4_2143(&c);
        out->_if_setangle.zoom = RSProt_BufferG2Be(&c);
        out->_if_setangle.angle_x = RSProt_BufferG2Be(&c);
        out->_if_setangle.angle_y = RSProt_BufferG2Le_add128(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETNPCHEAD_ACTIVE:
        out->_if_setnpchead_active.index = RSProt_BufferG2Be_add128(&c);
        out->_if_setnpchead_active.component_id = RSProt_BufferG4_3412(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR:
        out->_if_setplayermodel.index = RSProt_BufferG1(&c);
        out->_if_setplayermodel.value = RSProt_BufferG1(&c);
        out->_if_setplayermodel.component_id = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE:
        out->_if_setplayermodel.component_id = RSProt_BufferG4_3412(&c);
        out->_if_setplayermodel.index = -1;
        out->_if_setplayermodel.value = RSProt_BufferG1(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETPLAYERMODEL_OBJ:
        out->_if_setplayermodel.component_id = RSProt_BufferG4_3412(&c);
        out->_if_setplayermodel.index = -1;
        out->_if_setplayermodel.value = RSProt_BufferG4_2143(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_IF_SETPLAYERMODEL_SELF:
        out->_if_setplayermodel.value = RSProt_BufferG1_sub128(&c) != 0;
        out->_if_setplayermodel.index = -1;
        out->_if_setplayermodel.component_id = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;

    /* VarpSmallEncoder: p1Alt1 value, p2Alt3 id. */
    case PKT_NAME_VARP_SMALL:
    {
        struct PktVarpSmall* p = &out->_varp_small;
        int v = RSProt_BufferG1_add128(&c);
        p->value = v > 127 ? v - 256 : v;
        p->variable = RSProt_BufferG2Le_add128(&c);
        return c.err ? 0 : 1;
    }

    /* VarpLargeEncoder: p2Alt2 id, p4Alt1 value. Note the id comes FIRST here
     * and second in the small form; there is no "id first" rule. */
    case PKT_NAME_VARP_LARGE:
    {
        struct PktVarpLarge* p = &out->_varp_large;
        p->variable = RSProt_BufferG2Be_add128(&c);
        p->value = RSProt_BufferG4Le(&c);
        return c.err ? 0 : 1;
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
        (void)RSProt_BufferG1(&c); /* invisibleBoostedLevel */
        p->level = RSProt_BufferG1(&c);
        p->stat = RSProt_BufferG1_add128(&c);
        p->xp = RSProt_BufferG4_3412(&c);
        return c.err ? 0 : 1;
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

        p->type = RSProt_BufferGSmart1or2(&c);
        if( RSProt_BufferG1(&c) != 0 )
            p->name = gjstr_nul(&c);
        p->text = gjstr_nul(&c);
        if( c.err || !p->text )
        {
            free(p->name);
            free(p->text);
            p->name = NULL;
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
        p->component_id = RSProt_BufferG4_3412(&c);
        if( c.err || !p->text )
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
        p->component_id = RSProt_BufferG4Be(&c);
        p->npc_id = RSProt_BufferG2Le_add128(&c);
        return c.err ? 0 : 1;
    }

    /* IfSetAnimEncoder: pCombinedIdAlt1 uid, p2Alt1 anim. */
    case PKT_NAME_IF_SETANIM:
    {
        struct PktIfSetAnim* p = &out->_if_setanim;
        p->component_id = RSProt_BufferG4Le(&c);
        p->anim_id = RSProt_BufferG2Le(&c);
        return c.err ? 0 : 1;
    }

    /* IfSetPlayerHeadEncoder: pCombinedIdAlt2 uid, and nothing else. */
    case PKT_NAME_IF_SETPLAYERHEAD:
    {
        struct PktIfSetPlayerHead* p = &out->_if_setplayerhead;
        p->component_id = RSProt_BufferG4_3412(&c);
        return c.err ? 0 : 1;
    }

    /* IfSetScrollPosEncoder: pCombinedIdAlt3 uid, p2 scrollPos. Six bytes —
     * the shared parser's g2+g2 layout is the classic four and asserts here. */
    case PKT_NAME_IF_SETSCROLLPOS:
    {
        struct PktIfSetScrollPos* p = &out->_if_setscrollpos;
        p->component_id = RSProt_BufferG4_2143(&c);
        p->pos = RSProt_BufferG2Be(&c);
        return c.err ? 0 : 1;
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
        int packed = RSProt_BufferG4Be(&c);

        if( c.err )
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
        p->chat_public_mode = RSProt_BufferG1_add128(&c);
        p->chat_trade_mode = RSProt_BufferG1_sub128(&c);
        p->chat_private_mode = 0;
        return c.err ? 0 : 1;
    }

    /* SynthSoundEncoder: p2 id, p1 loops, p2 delay -- the classic layout, which
     * is why it is stated rather than delegated: the 230 hybrid's SYNTH_SOUND
     * is lc254-shaped and reads `loops` as part of the id. */
    case PKT_NAME_SYNTH_SOUND:
    {
        struct PktSynthSound* p = &out->_synth_sound;
        p->id = RSProt_BufferG2Be(&c);
        p->loops = RSProt_BufferG1(&c);
        p->delay = RSProt_BufferG2Be(&c);
        return c.err ? 0 : 1;
    }

    /* MidiSongV2Encoder v14: keep the canonical executor's id while consuming
     * the complete envelope so fixed-length validation remains exact. */
    case PKT_NAME_MIDI_SONG:
    {
        struct PktMidiSong* p = &out->_midi_song;
        int fade_in_speed;
        int fade_out_speed;
        (void)RSProt_BufferG2Le(&c); /* fade-in delay */
        (void)RSProt_BufferG2Be(&c); /* fade-out delay */
        p->id = RSProt_BufferG2Le_add128(&c);
        fade_in_speed = RSProt_BufferG2Be(&c);
        fade_out_speed = RSProt_BufferG2Le_add128(&c);
        if( p->id == 65535 )
            p->id = -1;
        /* Speeds are client cycles; the player works in milliseconds.  Keep
         * decoding both delays for the fixed V2 layout, but the current
         * one-generator backend only represents ramp lengths.  Its mapped
         * region profile deliberately sends both delays as zero. */
        p->fade_in_ms = fade_in_speed * 20;
        p->fade_out_ms = fade_out_speed * 20;
        return c.err ? 0 : 1;
    }

    /*
     * MidiSongWithSecondaryEncoder (rev 239), field order straight off the
     * encoder:
     *   p2(primaryId) p2Alt3(fadeInDelay) p2Alt1(secondaryId)
     *   p2(fadeOutSpeed) p2Alt1(fadeOutDelay) p2Alt3(fadeInSpeed)
     * The ids are interleaved *between* the fade fields rather than adjacent,
     * which is why this is transcribed rather than guessed from the V2 layout.
     */
    case PKT_NAME_MIDI_SONG_WITHSECONDARY:
    {
        struct PktMidiSongWithSecondary* p = &out->_midi_song_with_secondary;
        int fade_in_speed;
        int fade_out_speed;

        p->primary_id = RSProt_BufferG2Be(&c);
        (void)RSProt_BufferG2Le_add128(&c); /* fade-in delay */
        p->secondary_id = RSProt_BufferG2Le(&c);
        fade_out_speed = RSProt_BufferG2Be(&c);
        (void)RSProt_BufferG2Le(&c); /* fade-out delay */
        fade_in_speed = RSProt_BufferG2Le_add128(&c);
        if( p->primary_id == 65535 )
            p->primary_id = -1;
        if( p->secondary_id == 65535 )
            p->secondary_id = -1;
        /* Speeds are client cycles; the player works in milliseconds. As with
         * MIDI_SONG_V2 the delays are a pre-roll the load already exceeds. */
        p->fade_in_ms = fade_in_speed * 20;
        p->fade_out_ms = fade_out_speed * 20;
        return c.err ? 0 : 1;
    }

    /*
     * MidiSwapEncoder (rev 239):
     *   p2(fadeOutSpeed) p2(fadeInDelay) p2Alt2(fadeInSpeed) p2(fadeOutDelay)
     * No id -- the target is whatever the last WITHSECONDARY pre-queued.
     */
    case PKT_NAME_MIDI_SWAP:
    {
        struct PktMidiSwap* p = &out->_midi_swap;

        p->fade_out_ms = RSProt_BufferG2Be(&c) * 20;
        (void)RSProt_BufferG2Be(&c); /* fade-in delay */
        p->fade_in_ms = RSProt_BufferG2Be_add128(&c) * 20;
        (void)RSProt_BufferG2Be(&c); /* fade-out delay */
        return c.err ? 0 : 1;
    }

    /*
     * MidiSongStopEncoder v12 (rev 239): p2Alt3 delay, p2Alt3 speed. The speed
     * is the fade length; the delay is a pre-roll the player does not need,
     * because stopping already waits for nothing.
     */
    case PKT_NAME_MIDI_SONG_STOP:
    {
        struct PktMidiSongStop* p = &out->_midi_song_stop;
        (void)RSProt_BufferG2Le_add128(&c); /* fade-out delay */
        p->fade_out_ms = RSProt_BufferG2Le_add128(&c) * 20;
        return c.err ? 0 : 1;
    }

    /*
     * AmbientSoundStartEncoder v1: p1Alt1 fade, p2 id.
     *
     * The region's background bed -- a looping sound with no position, distinct
     * from the loc `ambient_sound_*` emitters the scene builder gathers. An area
     * can have both, and this one is the reason a place sounds like somewhere
     * even when nothing in it is making a noise.
     */
    case PKT_NAME_AMBIENTSOUND_START:
    {
        struct PktAmbientSoundStart* p = &out->_ambientsound_start;
        p->fade_ms = RSProt_BufferG1_add128(&c) * 20;
        p->id = RSProt_BufferG2Be(&c);
        return c.err ? 0 : 1;
    }

    /* AmbientSoundStopEncoder v2 (rev 239): a single boolean fade flag. */
    case PKT_NAME_AMBIENTSOUND_STOP:
    {
        struct PktAmbientSoundStop* p = &out->_ambientsound_stop;
        p->fade_ms = RSProt_BufferG1(&c) != 0 ? 500 : 0;
        return c.err ? 0 : 1;
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

        p->component_id = RSProt_BufferG4Be(&c);
        p->inv_id = RSProt_BufferG2Be(&c);
        capacity = RSProt_BufferG2Be(&c);
        if( c.err || capacity < 0 || capacity > 4096 )
            return 0;
        p->size = capacity;
        p->obj_ids = (int*)malloc((size_t)capacity * sizeof(int));
        p->obj_counts = (int*)malloc((size_t)capacity * sizeof(int));
        assert(p->obj_ids);
        assert(p->obj_counts);
        for( int i = 0; i < capacity; i++ )
        {
            int count = RSProt_BufferG1_sub128(&c);

            if( count == 255 )
                count = RSProt_BufferG4_2143(&c);
            p->obj_counts[i] = count;
            p->obj_ids[i] = RSProt_BufferG2Le(&c) - 1;
        }
        if( c.err )
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

        p->component_id = RSProt_BufferG4Be(&c);
        p->inv_id = RSProt_BufferG2Be(&c);
        p->count = 0;
        p->entries = NULL;
        if( c.err )
            return 0;
        /* Empty records are only 3 bytes: slot + zero object sentinel. */
        max_entries = (len - c.rpos) / 3 + 1;
        p->entries = (struct PktUpdateInvPartialEntry*)malloc(
            (size_t)max_entries * sizeof(struct PktUpdateInvPartialEntry));
        if( !p->entries )
            return 0;
        while( c.rpos < len && written < max_entries )
        {
            struct PktUpdateInvPartialEntry* entry = &p->entries[written];
            int count;

            entry->slot = RSProt_BufferGSmart1or2(&c);
            entry->obj_id = RSProt_BufferG2Be(&c) - 1;
            if( entry->obj_id < 0 )
                count = 0;
            else
            {
                count = RSProt_BufferG1(&c);
                if( count == 255 )
                    count = RSProt_BufferG4Be(&c);
            }
            entry->count = count;
            if( c.err )
                break;
            written++;
        }
        if( c.err || c.rpos != len )
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
     * UpdateInvStopTransmitEncoder: p2Alt2(inventoryId) -- NOT a plain p2.
     *
     * This arm read it big-endian, which is 128 too high in the low byte: the
     * backpack (93) arrived as 221 and the client stopped transmitting a
     * container that does not exist. Found by src/net/rev/test/
     * rsprot_bridge_test.c when the generated codec was compared against this
     * arm -- the first time in this lane the HAND-WRITTEN side was the wrong
     * one, which is the direction the differential test was always as likely
     * to catch and had not yet.
     */
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        out->_update_inv_stop_transmit.component_id = -1;
        out->_update_inv_stop_transmit.inv_id = RSProt_BufferG2Be_add128(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_FRIENDLIST_LOADED:
        out->_friendlist_loaded.status = 1;
        return len == 0;

    case PKT_NAME_UPDATE_FRIENDLIST:
    {
        struct PktUpdateFriendList* p = &out->_update_friendlist;
        char* name;
        char* previous;
        char* extra = NULL;
        char* note;

        memset(p, 0, sizeof(*p));
        if( len == 0 )
            return 1;
        (void)RSProt_BufferG1(&c); /* renamed */
        name = gjstr_nul(&c);
        previous = gjstr_nul(&c);
        p->world = RSProt_BufferG2Be(&c);
        (void)RSProt_BufferG1(&c); /* rank */
        (void)RSProt_BufferG1(&c); /* flags */
        if( p->world > 0 )
        {
            extra = gjstr_nul(&c);
            (void)RSProt_BufferG1(&c);
            (void)RSProt_BufferG4Be(&c);
        }
        note = gjstr_nul(&c);
        if( !c.err && c.rpos == len && name && previous && note &&
            (p->world <= 0 || extra) )
        {
            p->name37 = (int64_t)strtobase37(name);
            p->present = 1;
        }
        free(name);
        free(previous);
        free(extra);
        free(note);
        return p->present;
    }

    case PKT_NAME_UPDATE_IGNORELIST:
    {
        struct PktUpdateIgnoreList* p = &out->_update_ignorelist;
        int cap = len / 4 + 1;

        p->count = 0;
        p->names37 = cap > 0 ? malloc((size_t)cap * sizeof(*p->names37)) : NULL;
        if( cap > 0 && !p->names37 )
            return 0;
        while( c.rpos < len && !c.err )
        {
            char* name;
            char* previous;
            char* note;

            (void)RSProt_BufferG1(&c); /* flags/renamed */
            name = gjstr_nul(&c);
            previous = gjstr_nul(&c);
            note = gjstr_nul(&c);
            if( !name || !previous || !note || c.err )
            {
                free(name);
                free(previous);
                free(note);
                break;
            }
            p->names37[p->count++] = (int64_t)strtobase37(name);
            free(name);
            free(previous);
            free(note);
        }
        if( c.err || c.rpos != len )
        {
            free(p->names37);
            p->names37 = NULL;
            p->count = 0;
            return 0;
        }
        return 1;
    }

    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
        out->_set_npc_update_origin.x = RSProt_BufferG1(&c);
        out->_set_npc_update_origin.z = RSProt_BufferG1(&c);
        return c.err ? 0 : 1;

    case PKT_NAME_SERVER_TICK_END:
        return len == 0;

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
        struct PktRunClientScript* p;
        char types[PKT_RUNCLIENTSCRIPT_ARG_MAX + 1];
        int argc = 0;
        int terminated = 0;

        for( ;; )
        {
            int ch = RSProt_BufferG1(&c);

            if( c.err )
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
        if( c.err || !terminated )
            return 0;

        p = pkt_runclientscript_reset(out);
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
                p->intv[i] = RSProt_BufferG4Be(&c);
            }
        }
        p->script_id = RSProt_BufferG4Be(&c);
        return c.err ? 0 : 1;
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
     * Keep the header plane. Revision 239 sends zone changes for every plane
     * in the current scene, not just the local player's plane.
     */
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        struct PktUpdateZoneFullFollows* p = &out->_update_zone_full_follows;
        p->base_z = RSProt_BufferG1_sub128(&c);
        p->base_x = RSProt_BufferG1_neg(&c);
        p->level = RSProt_BufferG1(&c);
        return c.err ? 0 : 1;
    }

    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        struct PktUpdateZonePartialFollows* p = &out->_update_zone_partial_follows;
        p->base_z = RSProt_BufferG1_add128(&c);
        p->base_x = RSProt_BufferG1(&c);
        p->level = RSProt_BufferG1_neg(&c);
        return c.err ? 0 : 1;
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

        /* Header via its codec; the sub-packet walk below is a stream and
         * stays hand-driven -- each record runs its OWN codec in zone_run. */
        {
            MsgDesktopUpdateZonePartialEnclosed hdr;

            memset(&hdr, 0, sizeof(hdr));
            if( !zone_run(&c, rsprot_update_zone_partial_enclosed_out,
                          rsprot_update_zone_partial_enclosed_out_count, &hdr) )
                return 0;
            enc->level = hdr.level;
            enc->base_z = hdr.zone_z;
            enc->base_x = hdr.zone_x;
        }
        enc->count = 0;
        enc->entries =
            (struct PktZoneSubPacket*)malloc((size_t)cap * sizeof(*enc->entries));
        if( !enc->entries )
            return 0;
        while( c.rpos < len && !c.err )
        {
            int ordinal = RSProt_BufferG1(&c);
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
                    c.rpos,
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
    case PKT_NAME_SOUND_AREA:
    {
        struct PktZoneSubPacket sub;

        if( !osrs239_read_zone_sub(&c, (enum GameProtoPktName)pkt_name, &sub) || c.err )
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
        case PKT_NAME_SOUND_AREA:     out->_sound_area = sub._sound_area; break;
        default:                      out->_map_projanim = sub._map_projanim; break;
        }
        return 1;
    }

    /*
     * CamMoveToV2Encoder: p2Alt1 x, p1Alt2 rate, p2Alt1 height, p1Alt2 rate2,
     * p2 z.  CamLookAtV2Encoder: p2 z, p2Alt2 height, p1 rate2, p1Alt3 rate,
     * p2Alt1 x.
     *
     * Eight bytes at this revision against the classic six, and the widening
     * is not cosmetic: the two-byte coordinates are ABSOLUTE world tiles ("a
     * specific coordinate in the root world"), where the classic byte pair is
     * scene-local 0..103 and cannot address anything outside the scene. The
     * parse has no scene base to subtract, so `absolute` records the meaning
     * and the exec converts against the world it is applying to.
     */
    case PKT_NAME_CAM_MOVETO:
    {
        struct PktCamMoveTo* p = &out->_cam_moveto;
        p->local_x = RSProt_BufferG2Le(&c);
        p->rate = RSProt_BufferG1_neg(&c);
        p->height = RSProt_BufferG2Le(&c);
        p->rate2 = RSProt_BufferG1_neg(&c);
        p->local_z = RSProt_BufferG2Be(&c);
        p->absolute = 1;
        return c.err ? 0 : 1;
    }

    case PKT_NAME_CAM_LOOKAT:
    {
        struct PktCamLookAt* p = &out->_cam_lookat;
        p->local_z = RSProt_BufferG2Be(&c);
        p->height = RSProt_BufferG2Be_add128(&c);
        p->rate2 = RSProt_BufferG1(&c);
        p->rate = RSProt_BufferG1_sub128(&c);
        p->local_x = RSProt_BufferG2Le(&c);
        p->absolute = 1;
        return c.err ? 0 : 1;
    }

    /* CamShakeEncoder: p1 axis, p1 random, p1 amplitude, p1 rate -- unchanged
     * in shape, stated here because an untranscribed packet is refused. */
    case PKT_NAME_CAM_SHAKE:
    {
        struct PktCamShake* p = &out->_cam_shake;
        p->axis = RSProt_BufferG1(&c);
        p->amplitude = RSProt_BufferG1(&c);
        p->frequency = RSProt_BufferG1(&c);
        p->speed = RSProt_BufferG1(&c);
        return c.err ? 0 : 1;
    }

    /*
     * RebuildRegionV2Encoder plus the client's encodeRegionV2 grid:
     *   p2 zoneZ, p2 zoneX, p1Alt1 reload, p2 distinctSourceSquareCount,
     *   then 4*13*13 records in bit mode: pBit(1) present and, when present,
     *   pBit(26) descriptor. V2 has no trailing XTEA keys.
     *
     * The source-square count is a loading hint in the Java client. The C
     * loader derives the distinct source squares directly from the retained
     * descriptors, so it only needs to consume and validate the field.
     */
    case PKT_NAME_REBUILD_REGION:
    {
        struct PktMapRebuild* p = &out->_map_rebuild;
        int source_square_count;
        int bit_pos;
        int ok = 1;

        MsgRebuildRegionV2 hdr;

        memset(p, 0, sizeof(*p));
        /*
         * The three-field HEADER runs its generated codec against this cursor;
         * what follows is a bit-packed descriptor grid, which is not part of the
         * payload codec and stays here. `reload` is read and dropped -- a region
         * rebuild is forced downstream regardless.
         */
        memset(&hdr, 0, sizeof(hdr));
        if( !zone_run(&c, rsprot_rebuild_region_v2_out,
                      rsprot_rebuild_region_v2_out_count, &hdr) )
            return 0;
        p->zonez = hdr.zone_z;
        p->zonex = hdr.zone_x;
        source_square_count = RSProt_BufferG2Be(&c);
        if( c.err || source_square_count < 0 ||
            source_square_count > PKT_MAP_REBUILD_ZONES )
            return 0;

        p->zones = calloc(PKT_MAP_REBUILD_ZONES, sizeof(*p->zones));
        assert(p->zones);
        bit_pos = c.rpos * 8;
        for( int i = 0; i < PKT_MAP_REBUILD_ZONES && ok; i++ )
        {
            if( gbits_checked(data, len, &bit_pos, 1, &ok) )
                p->zones[i] = (int32_t)gbits_checked(data, len, &bit_pos, 26, &ok);
        }
        if( !ok || (bit_pos + 7) / 8 != len )
        {
            free(p->zones);
            p->zones = NULL;
            return 0;
        }
        return 1;
    }

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
 * There used to be a table here holding references to the file's unused
 * readers, marked `__attribute__((used))`, so that keeping a half-present set
 * of byte orders did not become how the wrong one gets picked.
 *
 * It is gone because the readers are: every order lives in
 * `3rd/rsprot/src/rsprot_buffer.h`, complete at all four widths, whether this
 * file calls it or not. A set that cannot be half-present needs nothing to
 * keep it alive.
 */
