#include "net/rev/gameproto_revisions.h"
#include "mem/torirs_mem.h"
#include <assert.h>
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"
#include "net/jbase37.h"
#include "net/wordpack.h"
#include "zoneprot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsprot_buf.h"
#include "rsbuffer.h"
#include "rsprot_exec.h"

#include "packets/loc_anim.h"
#include "packets/loc_del.h"
#include "packets/loc_merge.h"
#include "packets/map_anim.h"
#include "packets/map_anim_specific.h"
#include "packets/map_projanim_v2.h"
#include "packets/scriptedproj_add.h"
#include "packets/obj_count.h"
#include "packets/obj_del.h"
#include "packets/loc_add_change_v2.h"
#include "packets/rebuild_normal_v2.h"
#include "packets/rebuild_region_v2.h"
#include "packets/update_zone_partial_enclosed.h"
#include "packets/obj_add.h"
#include "packets/obj_enabled_ops.h"
#include "packets/sound_area.h"
#include "log/torirs_log.h"

/*
 * Revision-241 parse (server -> this client) -- a COPY of osrs239_parse.c,
 * changed to 241's wire. RSProt stops at 239, so every 241 layout here is
 * transcribed from RSProx's osrs-241 decoders (or runs a generated codec whose
 * 241 row RSProt ported from them), and each arm that
 * moved names the decoder it came from.
 *
 * WHAT IS NOT HERE: every packet `proto_decode_name` decodes at 241. The
 * bridge runs first (net.c), so the 239 copy's hand-written arms for those
 * would be dead -- and, worse, wrong, because they spell out 239 byte orders.
 * They were removed from this copy rather than carried.
 *
 * The `default` arm still hands the rest to `osrs230_parse`, under the same
 * rule as at 239: only for packets whose payload RSProx shows unchanged
 * (VARP_RESET, CAM_RESET, HINT_ARROW, UPDATE_RUNWEIGHT, MESSAGE_PRIVATE, ...).
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
 * The byte cursor is RsprotBuf's.
 *
 * This file used to define a private `struct Cur` and seventeen readers over
 * it — g1, g1_alt1..3, g2, g2_alt1..3, g3, g3_alt2, g4, g4_alt1..3, g1b,
 * gsmart1or2, gjstr_nul. Every one had a counterpart in the buffer library and
 * was a separate place for a byte order to be wrong; `gsmart1or2` alone was one
 * of five copies of that encoding in src/net. See
 * `docs/BUFFER_ACCESSOR_AUDIT.md` and `3rd/rsprot/src/rsprot_buf.h`.
 *
 * The names now carry the byte order instead of an `alt` ordinal, so a call
 * site states what it reads: `g4_alt3` was the 2,1,4,3 permutation and is
 * spelled `rsprot_g4_2143`. The comment that used to sit on that reader —
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

/* WORLDENTITY_INFO's 2-bit-per-axis delta: 0 → 0, 1 → i8, 2 → i16, 3 → i32
 * (SAILING.md §5.4). Big-endian, signed. */
static int
wev_delta(
    RsprotBuf* c,
    int code)
{
    switch( code & 3 )
    {
    case 0:
        return 0;
    case 1:
        return (int8_t)rsprot_g1(c);
    case 2:
        return (int16_t)rsprot_g2_12(c);
    default:
        return (int32_t)rsprot_g4_1234(c);
    }
}

/*
 * WORLDENTITY_INFO's updateFlags PAYLOAD (deob Statics.method12230).
 *
 * Two bits, and the 0x2 one is read FIRST: it carries the entity's 5-bit
 * op-enabled mask (class467.field5694) through class617.method13166, the
 * `(128 - b) & 0xFF` transform. Bit 0x1 then carries a u16 seq id (65535 =
 * clear, surfaced as -1) plus a u8 delay.
 *
 * The flags byte itself is NOT read here, because it is not always adjacent
 * to its payload: in the spawn trailer the deob reads the byte at field 2 —
 * right after the id — and consumes this payload only after the absolute
 * transform, at the very end of the record.
 */
static void
wev_flag_payload_defaults(
    int* out_has_op_mask,
    int* out_op_mask,
    int* out_has_seq,
    int* out_seq_id,
    int* out_seq_delay)
{
    *out_has_op_mask = 0;
    *out_op_mask = PKT_WEV_OP_MASK_ALL;
    *out_has_seq = 0;
    *out_seq_id = -1;
    *out_seq_delay = 0;
}

static int
wev_read_flag_payload(
    RsprotBuf* c,
    unsigned flags,
    int* out_has_op_mask,
    int* out_op_mask,
    int* out_has_seq,
    int* out_seq_id,
    int* out_seq_delay)
{
    wev_flag_payload_defaults(
        out_has_op_mask, out_op_mask, out_has_seq, out_seq_id, out_seq_delay);
    /*
     * Revision 241 (RSProx WorldEntityInfoClient.decodeWorldEntityInfo-
     * ExtendedInfoObfuscated) swapped the two: the 0x1 sequence block now comes
     * FIRST -- g2 id, g1Alt2 delay -- and the 0x2 op mask follows as g1Alt2.
     * At 239 the mask led, as g1Alt3, and the delay was a plain byte.
     */
    if( flags & 0x1 )
    {
        int seq = rsprot_g2_12(c);

        *out_has_seq = 1;
        *out_seq_id = (seq == 65535) ? -1 : seq;
        *out_seq_delay = rsprot_g1_neg(c);
    }
    if( flags & 0x2 )
    {
        *out_has_op_mask = 1;
        *out_op_mask = rsprot_g1_neg(c);
    }
    return c->err ? 0 : 1;
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
gjstr_nul(RsprotBuf* c)
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
    out = (char*)ToriMem_Alloc(ToriMem_Net(), (size_t)(end - start) + 1);
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
osrs241_zone_name(int ordinal)
{
    switch( ordinal )
    {
    case OSRS241_ZONE_LOC_DEL: return PKT_NAME_LOC_DEL;
    case OSRS241_ZONE_LOC_MERGE: return PKT_NAME_LOC_MERGE;
    case OSRS241_ZONE_OBJ_COUNT: return PKT_NAME_OBJ_COUNT;
    case OSRS241_ZONE_MAP_ANIM: return PKT_NAME_MAP_ANIM;
    case OSRS241_ZONE_LOC_ADD_CHANGE_V2: return PKT_NAME_LOC_ADD_CHANGE;
    case OSRS241_ZONE_OBJ_ADD: return PKT_NAME_OBJ_ADD;
    case OSRS241_ZONE_LOC_ANIM: return PKT_NAME_LOC_ANIM;
    case OSRS241_ZONE_OBJ_DEL: return PKT_NAME_OBJ_DEL;
    case OSRS241_ZONE_OBJ_ENABLED_OPS: return PKT_NAME_OBJ_REVEAL;
    case OSRS241_ZONE_MAP_PROJANIM_V2: return PKT_NAME_MAP_PROJANIM;
    case OSRS241_ZONE_SOUND_AREA: return PKT_NAME_SOUND_AREA;
    case OSRS241_ZONE_SCRIPTEDPROJ_ADD: return PKT_NAME_SCRIPTEDPROJ_ADD;
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
 * `proto_decode_name` like a top-level packet. It does not need to:
 * `RsprotBuf` IS `RsprotBuf`, so an executor can run a codec against this
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
zone_run(RsprotBuf* c, RsprotVersionRange const* ranges, int count, void* msg)
{
    RsprotExec x;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, rsprot_osrs(241));

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
osrs241_read_zone_sub(
    RsprotBuf* c,
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
     * and RSProt has no codec for its array of nested element structs. Skipping the
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
        p->source = sign24(m.source_index);
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

    /* No conversion at all: the deob subtracts these heights from the ground
     * as they are, and the indices keep MAP_PROJANIM_V2's convention. */
    case PKT_NAME_SCRIPTEDPROJ_ADD:
    {
        struct PktScriptedProjAdd* p = &out->_scriptedproj_add;

        ZONE_RUN(rsprot_scriptedproj_add_out, MsgScriptedProjAdd, m);
        p->pos = m.coord_in_zone_packed;
        p->source = sign24(m.source_index);
        p->source_offset_x = (int16_t)m.source_offset_x;
        p->source_offset_z = (int16_t)m.source_offset_z;
        p->source_height = (int16_t)m.source_height;
        p->target = sign24(m.target_index);
        p->target_level = (m.target_coord_packed >> 28) & 0x3;
        p->target_x = (m.target_coord_packed >> 14) & 0x3fff;
        p->target_z = m.target_coord_packed & 0x3fff;
        p->target_offset_x = (int16_t)m.target_offset_x;
        p->target_offset_z = (int16_t)m.target_offset_z;
        p->target_height = (int16_t)m.target_height;
        p->spotanim = m.id;
        p->slot = m.slot;
        p->start_delay = m.start_time;
        p->end_delay = m.end_time;
        p->curve_h = m.curve_script_h;
        p->curve_a = m.curve_script_a;
        p->curve_t = m.curve_script_t;
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
        out->_obj_add.time_until_public = m.time_until_public;
        out->_obj_add.time_until_despawn = m.time_until_despawn;
        out->_obj_add.ownership_type = m.ownership_type;
        out->_obj_add.never_becomes_public = m.never_becomes_public;
        return 1;
    }

    default:
        return 0;
    }
}

/* osrs241_entity_info.c */
void
osrs241_playerinfo_init(uint8_t const* data, int len);

int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

int
osrs241_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    RsprotBuf c;
    rsprot_buf_wrap_read(&c, data, len);

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
            osrs241_playerinfo_init(data, len - 6);
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
            /* The world-entity prefix (0 = root). Dropping it here is how a
             * boat rebuild would silently rebuild the main scene. */
            p->world_area = hdr.world_area;
            p->zonez = hdr.zone_z;
            p->zonex = hdr.zone_x;
        }
        p->region_count = 0;
        p->region_ids = NULL;
        p->region_keys = NULL;
        return c.err ? 0 : 1;
    }

    /*
     * The audio packets below are transcribed from RSProx's osrs-241 decoders
     * and written with RSProt's own accessor names (rsprot_g2_21_add128 is g2Alt3),
     * so each line reads against its Kotlin original. Every one of them moved
     * between 239 and 241.
     *
     * MidiSongWithSecondaryDecoder (241):
     *   g2Alt2 secondaryId, g2Alt2 fadeOutDelay, g2Alt3 fadeInSpeed,
     *   g2 fadeInDelay, g2Alt3 primaryId, g2Alt3 fadeOutSpeed
     */
    case PKT_NAME_MIDI_SONG_WITHSECONDARY:
    {
        struct PktMidiSongWithSecondary* p = &out->_midi_song_with_secondary;
        int fade_in_speed;
        int fade_out_speed;

        p->secondary_id = rsprot_g2_12_add128(&c);
        (void)rsprot_g2_12_add128(&c); /* fade-out delay */
        fade_in_speed = rsprot_g2_21_add128(&c);
        (void)rsprot_g2_12(&c); /* fade-in delay */
        p->primary_id = rsprot_g2_21_add128(&c);
        fade_out_speed = rsprot_g2_21_add128(&c);
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
     * MidiSwapDecoder (241):
     *   g2 fadeInDelay, g2 fadeOutDelay, g2Alt2 fadeOutSpeed, g2 fadeInSpeed
     * No id -- the target is whatever the last WITHSECONDARY pre-queued.
     */
    case PKT_NAME_MIDI_SWAP:
    {
        struct PktMidiSwap* p = &out->_midi_swap;

        (void)rsprot_g2_12(&c); /* fade-in delay */
        (void)rsprot_g2_12(&c); /* fade-out delay */
        p->fade_out_ms = rsprot_g2_12_add128(&c) * 20;
        p->fade_in_ms = rsprot_g2_12(&c) * 20;
        return c.err ? 0 : 1;
    }

    /*
     * MidiSongStopDecoder (241): g2Alt3 fadeOutDelay, g2Alt2 fadeOutSpeed. The
     * speed is the fade length; the delay is a pre-roll the player does not
     * need, because stopping already waits for nothing.
     */
    case PKT_NAME_MIDI_SONG_STOP:
    {
        struct PktMidiSongStop* p = &out->_midi_song_stop;
        (void)rsprot_g2_21_add128(&c); /* fade-out delay */
        p->fade_out_ms = rsprot_g2_12_add128(&c) * 20;
        return c.err ? 0 : 1;
    }

    /*
     * AmbientSoundStartDecoder (241): g2 id, g1Alt1 fade (== 1).
     *
     * The region's background bed -- a looping sound with no position, distinct
     * from the loc `ambient_sound_*` emitters the scene builder gathers.
     */
    case PKT_NAME_AMBIENTSOUND_START:
    {
        struct PktAmbientSoundStart* p = &out->_ambientsound_start;
        p->id = rsprot_g2_12(&c);
        p->fade_ms = rsprot_g1_add128(&c) * 20;
        return c.err ? 0 : 1;
    }

    /* AmbientSoundStopDecoder (241): g1Alt2 fade (== 1). */
    case PKT_NAME_AMBIENTSOUND_STOP:
    {
        struct PktAmbientSoundStop* p = &out->_ambientsound_stop;
        p->fade_ms = rsprot_g1_neg(&c) != 0 ? 500 : 0;
        return c.err ? 0 : 1;
    }

    /*
     * SetPlayerOpDecoder (241): g1Alt3 id, gjstr op, g1 priority (== 1).
     *
     * 239 has no arm for this and falls through to the classic parser; at 241
     * the slot leads, transformed, and the priority TRAILS the NUL-terminated
     * text, so the classic `slot, primary, newline-text` read would take the
     * transformed slot for a slot number and run the text to the frame's end.
     */
    case PKT_NAME_SET_PLAYER_OP:
    {
        struct PktSetPlayerOp* p = &out->_set_player_op;

        memset(p, 0, sizeof(*p));
        p->slot = rsprot_g1_sub128(&c);
        p->text = gjstr_nul(&c);
        p->primary = rsprot_g1(&c);
        if( c.err || !p->text )
        {
            ToriMem_Free(ToriMem_Net(), p->text);
            p->text = NULL;
            return 0;
        }
        return 1;
    }

    /*
     * UpdateRebootTimerV2Decoder (241): gjstr message, g2Alt2 gameCycles. The
     * canonical struct carries the tick count only; the message is consumed.
     */
    case PKT_NAME_UPDATE_REBOOT_TIMER:
    {
        char* message = gjstr_nul(&c);

        ToriMem_Free(ToriMem_Net(), message);
        out->_update_reboot_timer.ticks = rsprot_g2_12_add128(&c);
        return c.err ? 0 : 1;
    }

    /* SetActiveWorldV2Encoder: p2 world-entity id (0 = root), p1 plane —
     * the layout ToriRSServer_SendSetActiveWorld writes and the world
     * selftest pins byte-for-byte. */
    case PKT_NAME_SET_ACTIVE_WORLD:
        out->_set_active_world.world_id = rsprot_g2_12(&c);
        out->_set_active_world.level = rsprot_g1(&c);
        return c.err ? 0 : 1;

    /*
     * WORLDENTITY_INFO_V7 (op 122): hand-written from docs/SAILING.md §5.4
     * (deob Statics.method977) — RSProt's generator excludes the whole info
     * family, so unlike the rest of this file there is no vendored encoder to
     * transcribe from.
     *
     * Three things the deob pins that are easy to get wrong:
     *
     *  - The 2-bit axis codes are taken LSB-first (dx bits 0-1 .. dangle
     *    bits 6-7), each code selecting nothing / i8 / i16 BE / i32 BE.
     *  - updateFlags is NOT an atomic unit. The flag byte and its payload are
     *    adjacent only in the update records; in the spawn trailer the byte is
     *    read right after the id and the payload only after the transform.
     *    Op 0 (despawn) carries no flag byte at all.
     *  - The spawn scalars all use alt transforms: sizeByte through
     *    method13137 `(-b) & 0xFF`, ownerTypeIndex through method13164
     *    `(b - 128) & 0xFF`, configId through method13178 (signed LE u16).
     */
    case PKT_NAME_WORLDENTITY_INFO:
    {
        struct PktWorldEntityInfo* p = &out->_worldentity_info;

        memset(p, 0, sizeof(*p));
        p->count = rsprot_g1(&c);
        if( c.err || p->count > PKT_WEV_INFO_MAX )
            return 0;
        for( int i = 0; i < p->count; i++ )
        {
            struct PktWevUpdate* u = &p->updates[i];

            u->op = rsprot_g1(&c);
            if( c.err || u->op > PKT_WEV_OP_SNAP )
                return 0;
            u->update_flags = 0;
            wev_flag_payload_defaults(
                &u->has_op_mask, &u->op_mask, &u->has_seq, &u->seq_id, &u->seq_delay);
            /* Op 0 removes the entity; the deob's `if (op != 0)` guards every
             * remaining read of the record, flag byte included. */
            if( u->op == PKT_WEV_OP_DESPAWN )
                continue;
            if( u->op == PKT_WEV_OP_ENQUEUE || u->op == PKT_WEV_OP_SNAP )
            {
                int mask = rsprot_g1(&c);

                u->dx = wev_delta(&c, mask & 3);
                u->dy = wev_delta(&c, (mask >> 2) & 3);
                u->dz = wev_delta(&c, (mask >> 4) & 3);
                u->dangle = wev_delta(&c, (mask >> 6) & 3);
            }
            u->update_flags = (unsigned)rsprot_g1(&c);
            if( c.err )
                return 0;
            if( !wev_read_flag_payload(
                    &c,
                    u->update_flags,
                    &u->has_op_mask,
                    &u->op_mask,
                    &u->has_seq,
                    &u->seq_id,
                    &u->seq_delay) )
                return 0;
        }
        /* New entities, while bytes remain. */
        while( !c.err && c.rpos < len )
        {
            struct PktWevSpawn* s;
            int size_byte;
            int mask;

            if( p->spawn_count >= PKT_WEV_INFO_MAX )
                return 0;
            s = &p->spawns[p->spawn_count];
            /*
             * Revision 241 (RSProx decodeLowResolutionV5): g2 index,
             * g2sAlt1 config id, g1Alt3 flags, g1Alt2 priority, g1Alt3 packed
             * size. 239 put the config id LAST of the five and read the flags
             * plain, the size g1Alt2 and the priority g1Alt1.
             */
            s->id = rsprot_g2_12(&c);
            /* Id 0 is the root view and 16+ is past the registry: a frame
             * naming either is malformed, not a state to spawn. */
            if( c.err || s->id <= 0 || s->id > PKT_WEV_INFO_MAX )
                return 0;
            s->config_id = rsprot_g2s_21(&c);
            s->update_flags = (unsigned)rsprot_g1_sub128(&c);
            s->priority_group = rsprot_g1_neg(&c);
            size_byte = rsprot_g1_sub128(&c);
            s->size_x_tiles = ((size_byte >> 4) & 0xF) * 8;
            s->size_z_tiles = (size_byte & 0xF) * 8;
            /* A zero nibble is a zero-tile view — nothing can live in it. */
            if( s->size_x_tiles == 0 || s->size_z_tiles == 0 )
                return 0;
            /* The absolute transform arrives as the same 4-axis bitfield
             * applied to (0,0,0,0). */
            mask = rsprot_g1(&c);
            s->x = wev_delta(&c, mask & 3);
            s->y = wev_delta(&c, (mask >> 2) & 3);
            s->z = wev_delta(&c, (mask >> 4) & 3);
            s->angle = wev_delta(&c, (mask >> 6) & 3) & 0x7FF;
            if( c.err )
                return 0;
            /* The flag payload trails the record, not its own flag byte. */
            if( !wev_read_flag_payload(
                    &c,
                    s->update_flags,
                    &s->has_op_mask,
                    &s->op_mask,
                    &s->has_seq,
                    &s->seq_id,
                    &s->seq_delay) )
                return 0;
            p->spawn_count++;
        }
        return c.err ? 0 : (c.rpos == len);
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
            /* The codec carries the sub-packets as `payload`; walk them here. */
            c.rpos = len - hdr.payload_length;
        }
        enc->count = 0;
        enc->entries =
            (struct PktZoneSubPacket*)ToriMem_Alloc(ToriMem_Net(), (size_t)cap * sizeof(*enc->entries));
        if( !enc->entries )
            return 0;
        while( c.rpos < len && !c.err )
        {
            int ordinal = rsprot_g1(&c);
            enum GameProtoPktName sub = osrs241_zone_name(ordinal);

            if( enc->count == cap )
            {
                struct PktZoneSubPacket* grown = (struct PktZoneSubPacket*)ToriMem_Realloc(ToriMem_Net(), 
                    enc->entries, (size_t)(cap * 2) * sizeof(*enc->entries));

                if( !grown )
                    break;
                cap *= 2;
                enc->entries = grown;
            }
            if( !osrs241_read_zone_sub(&c, sub, &enc->entries[enc->count]) )
            {
                TORIRS_ERR("osrs241: ZONE_ENCLOSED unknown sub-ordinal %d at %d/%d\n",
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

        if( !osrs241_read_zone_sub(&c, (enum GameProtoPktName)pkt_name, &sub) || c.err )
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
     * MapAnimSpecificEncoder (the generated codec, as the zone subs use it):
     * p1Alt2 height, p2Alt2 id, p3Alt2 coordInBuildAreaPacked, p2Alt2 delay.
     * The packed coord is zoneX << 16 | zoneZ << 8 | xInZone << 4 | zInZone,
     * and the deob adds the halves exactly that way (client.java, prot 90:
     * `(v >> 4 & 7) + (v >> 16)`, `(v & 7) + (v >> 8 & 0xff)`).
     */
    case PKT_NAME_MAP_ANIM_SPECIFIC:
    {
        MsgMapAnimSpecific m;
        int packed;

        memset(&m, 0, sizeof(m));
        if( !zone_run(&c, rsprot_map_anim_specific_out, rsprot_map_anim_specific_out_count, &m) )
            return 0;
        packed = m.coord_in_build_area_packed;
        out->_map_anim_specific.x = ((packed >> 4) & 0x7) + (packed >> 16);
        out->_map_anim_specific.z = (packed & 0x7) + ((packed >> 8) & 0xff);
        out->_map_anim_specific.id = m.id;
        out->_map_anim_specific.height = m.height;
        out->_map_anim_specific.delay = m.delay;
        return 1;
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
        MsgRebuildRegionV2 hdr;

        memset(p, 0, sizeof(*p));
        /*
         * The codec reads the header, the source-square count and the
         * descriptor grid (rsprot_tail_zone_grid). `reload` is read and dropped
         * -- a region rebuild is forced downstream regardless.
         */
        memset(&hdr, 0, sizeof(hdr));
        if( !zone_run(&c, rsprot_rebuild_region_v2_out,
                      rsprot_rebuild_region_v2_out_count, &hdr) )
            return 0;
        p->zonez = hdr.zone_z;
        p->zonex = hdr.zone_x;
        if( c.rpos != len )
            return 0;
        p->zones = ToriMem_Calloc(ToriMem_Net(), PKT_MAP_REBUILD_ZONES, sizeof(*p->zones));
        assert(p->zones);
        for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
            p->zones[i] = hdr.zones[i] == -1 ? 0 : hdr.zones[i];
        return 1;
    }

    /*
     * RebuildWorldEntityV4Encoder: p2 baseX, p2 baseZ (absolute root-world
     * tiles of the ACTIVE view's SW staging corner), then the same
     * encodeRegionV2 grid REBUILD_REGION carries — p2 distinctSourceSquareCount
     * and pBit(1)/pBit(26) records — but over [4][viewZonesX][viewZonesZ],
     * dimensions the server reads off the view being rebuilt (deob
     * Statics.method6693: field1391/8 x field1412/8) and does NOT put on the
     * wire. This stateless layer validates and consumes what it can (header +
     * count; the count is a loading hint here exactly as in the region arm)
     * and carries the grid bytes raw for the exec layer, which knows the
     * active view's size (PktRebuildWev_DecodeZones).
     */
    case PKT_NAME_REBUILD_WORLDENTITY:
    {
        struct PktRebuildWev* p = &out->_rebuild_wev;
        int source_square_count;

        memset(p, 0, sizeof(*p));
        p->base_x = rsprot_g2_12(&c);
        p->base_z = rsprot_g2_12(&c);
        source_square_count = rsprot_g2_12(&c);
        if( c.err || source_square_count < 0 ||
            source_square_count > PKT_MAP_REBUILD_ZONES )
            return 0;
        /* The smallest legal view (1x1 zones) still carries 4 presence bits,
         * so a grid with no bytes at all is a malformed frame. */
        if( len - c.rpos < 1 )
            return 0;
        p->length = len - c.rpos;
        p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)p->length);
        memcpy(p->data, data + c.rpos, (size_t)p->length);
        return 1;
    }

    /*
     * The friends-chat and clan packets are carried raw: their stores decode
     * them against client state this layer does not have (see pktnames.h).
     * VARCLAN_ENABLE / VARCLAN_DISABLE are size 0, and an empty friends-chat
     * FULL is the leave, so an empty payload is a real packet here.
     */
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL:
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER:
    case PKT_NAME_VARCLAN:
    case PKT_NAME_VARCLAN_ENABLE:
    case PKT_NAME_VARCLAN_DISABLE:
    case PKT_NAME_CLANCHANNEL_FULL:
    case PKT_NAME_CLANCHANNEL_DELTA:
    case PKT_NAME_CLANSETTINGS_FULL:
    case PKT_NAME_CLANSETTINGS_DELTA:
    case PKT_NAME_UPDATE_TRADINGPOST:
    {
        struct PktRawPayload* p = &out->_raw_payload;

        memset(p, 0, sizeof(*p));
        if( len == 0 )
            return 1;
        p->length = len;
        p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
        memcpy(p->data, data, (size_t)len);
        return 1;
    }

    /*
     * The message text is the chat encoding every other chat path of this
     * client reads (MESSAGE_PRIVATE, the PLAYER_INFO chat block): the packed
     * tail after the header, to the end of the frame.
     */
    case PKT_NAME_MESSAGE_CLANCHANNEL:
    case PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM:
    {
        struct PktMessageClanChannel* p = &out->_message_clanchannel;
        struct RSCache_Buffer text_buffer;

        memset(p, 0, sizeof(*p));
        p->clan_type = rsprot_g1s(&c);
        p->crown = -1;
        if( pkt_name == PKT_NAME_MESSAGE_CLANCHANNEL )
        {
            int32_t sender_length = 0;
            char const* sender = rsprot_gjstr(&c, &sender_length);
            if( c.err || !sender || sender_length >= (int)sizeof(p->sender) )
                return 0;
            memcpy(p->sender, sender, (size_t)sender_length);
            p->sender[sender_length] = '\0';
        }
        p->world = rsprot_g2_12(&c);
        p->counter = rsprot_g3_123(&c);
        if( pkt_name == PKT_NAME_MESSAGE_CLANCHANNEL )
            p->crown = rsprot_g1(&c);
        if( c.err || c.rpos > len )
            return 0;
        RSCache_BufferInit(&text_buffer, (uint8_t*)data + c.rpos, len - c.rpos);
        p->text = wordpack_unpack(&text_buffer, len - c.rpos);
        assert(p->text);
        return 1;
    }

    case PKT_NAME_UPDATE_STOCKMARKET_SLOT:
    {
        struct PktUpdateStockmarketSlot* p = &out->_update_stockmarket_slot;

        memset(p, 0, sizeof(*p));
        p->slot = rsprot_g1(&c);
        p->status = rsprot_g1s(&c);
        /* An empty slot's remaining 18 bytes are padding. */
        if( p->status != 0 )
        {
            p->obj = rsprot_g2_12(&c);
            p->price = rsprot_g4_1234(&c);
            p->count = rsprot_g4_1234(&c);
            p->completed_count = rsprot_g4_1234(&c);
            p->completed_gold = rsprot_g4_1234(&c);
        }
        return c.err ? 0 : 1;
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
 * `3rd/rsprot/src/rsprot_buf.h`, complete at all four widths, whether this
 * file calls it or not. A set that cannot be half-present needs nothing to
 * keep it alive.
 */
