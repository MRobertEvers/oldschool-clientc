/*
 * The data-driven revisions' parse (proto.h): one wire opcode in, one
 * canonical RevPacket out, at any revision 227-241.
 *
 * The prot is looked up by opcode in the revision's table, and decoded by
 * NAME:
 *
 *   1. a prot whose layout is not a codec -- the info streams (carried raw to
 *      the entity-info executor), the rebuild grids, the enclosed zone walk,
 *      the social payloads the stores decode, WORLDENTITY_INFO -- is a case of
 *      parse_special below;
 *   2. a prot an existing rsprot_bridge adapter runs EXACTLY is decoded there
 *      (proto_in_adapt), so the 239 / 241 adapters are shared, not
 *      copied;
 *   3. every other prot is an adapter in k_adapters: its codec, found through
 *      the rsprot registry at this revision, then a mapping into the canonical
 *      arm.
 *
 * A prot with no canonical name still has its payload run through its codec
 * (proto_validate) -- it is decodable, the client just has nothing to do
 * with it yet -- and parses as dropped.
 */
#include "proto.h"

#include "rsprot_storage.h"
#include "net/proto/proto_info.h"

#include "mem/torirs_mem.h"
#include "net/jbase37.h"
#include "net/proto/proto_in_adapt.h"
#include "net/wordpack.h"
#include "rsprot_exec.h"
#include "rsprot_registry.h"

#include "packets/cam_lookat.h"
#include "packets/cam_lookat_v1.h"
#include "packets/cam_moveto.h"
#include "packets/cam_moveto_cycles.h"
#include "packets/cam_moveto_cycles_v1.h"
#include "packets/cam_moveto_cycles_v2.h"
#include "packets/cam_moveto_v1.h"
#include "packets/cam_rotateto_coordinate.h"
#include "packets/cam_rotateto_coordinate_v1.h"
#include "packets/cam_rotateto_coordinate_v2.h"
#include "packets/ambientsound_start.h"
#include "packets/ambientsound_stop.h"
#include "packets/hint_arrow.h"
#include "packets/if_resync.h"
#include "packets/if_resync_v1.h"
#include "packets/if_setevents.h"
#include "packets/if_setevents_v1.h"
#include "packets/if_setmodel.h"
#include "packets/if_setmodel_v1.h"
#include "packets/loc_add_change.h"
#include "packets/loc_add_change_v1.h"
#include "packets/loc_add_change_v2.h"
#include "packets/loc_anim.h"
#include "packets/loc_del.h"
#include "packets/loc_merge.h"
#include "packets/map_anim.h"
#include "packets/map_anim_specific.h"
#include "packets/map_projanim.h"
#include "packets/map_projanim_v1.h"
#include "packets/map_projanim_v2.h"
#include "packets/midi_song_stop.h"
#include "packets/midi_song_withsecondary.h"
#include "packets/midi_swap.h"
#include "packets/obj_add.h"
#include "packets/obj_count.h"
#include "packets/obj_del.h"
#include "packets/obj_enabled_ops.h"
#include "packets/rebuild_normal.h"
#include "packets/rebuild_normal_v1.h"
#include "packets/rebuild_normal_v2.h"
#include "packets/rebuild_region.h"
#include "packets/rebuild_region_v1.h"
#include "packets/rebuild_region_v2.h"
#include "packets/rebuild_worldentity_v2.h"
#include "packets/rebuild_worldentity_v3.h"
#include "packets/rebuild_worldentity_v4.h"
#include "packets/message_clanchannel.h"
#include "packets/message_clanchannel_system.h"
#include "packets/message_private.h"
#include "packets/update_stockmarket_slot_v1.h"
#include "packets/scriptedproj_add.h"
#include "packets/set_active_world_v1.h"
#include "packets/set_active_world_v2.h"
#include "packets/set_map_flag.h"
#include "packets/set_map_flag_v1.h"
#include "packets/set_player_op.h"
#include "packets/sound_area.h"
#include "packets/update_reboot_timer.h"
#include "packets/update_reboot_timer_v1.h"
#include "packets/update_reboot_timer_v2.h"
#include "packets/update_runweight.h"
#include "packets/update_zone_partial_enclosed.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "log/torirs_log.h"

static int
sign24(int v)
{
    return (v & 0x800000) ? v - 0x1000000 : v;
}

/* --- WORLDENTITY_INFO ------------------------------------------------------ */

static void
we_ext(
    RsprotInfoWeExt const* e,
    unsigned* flags,
    int* has_op_mask,
    int* op_mask,
    int* has_seq,
    int* seq_id,
    int* seq_delay)
{
    *flags = (unsigned)e->flags;
    *has_op_mask = e->has_ops;
    *op_mask = e->has_ops ? e->ops : PKT_WEV_OP_MASK_ALL;
    *has_seq = e->has_sequence;
    *seq_id = !e->has_sequence || e->sequence_id == 65535 ? -1 : e->sequence_id;
    *seq_delay = e->has_sequence ? e->sequence_delay : 0;
}

/*
 * The stream's list into the canonical arm, which was shaped by 239+ (struct
 * PktWorldEntityInfo): V3 / V4 spawns carry no config id or priority (-1 here,
 * which the executor refuses to spawn) and their level and V4's centre offsets
 * have no field. 1 when the stream read whole and fits the arm.
 */
static int
worldentity_info_convert(
    RsprotWorldEntityInfoReadFn read,
    uint8_t const* data,
    int len,
    struct PktWorldEntityInfo* out)
{
    /* A g1 count of entries and spawns of at least ten bytes each: the arm's
     * fifteen run out long before these do. */
    static RsprotInfoOp ops[512];
    static uint8_t bytes[1];
    RsprotInfoList list;

    assert(read);
    assert(out);
    memset(out, 0, sizeof(*out));
    rsprot_info_list_init(&list, ops, (int)(sizeof(ops) / sizeof(ops[0])), bytes, sizeof(bytes));
    read(data, len, &list);
    for( int i = 0; i < list.count; i++ )
    {
        RsprotInfoOp const* l = &ops[i];
        struct PktWevUpdate* u;
        struct PktWevSpawn* sp;

        switch( l->kind )
        {
        case RSPROT_OP_WE_COUNT:
            /* At most 15 entities exist client-side: a longer list is a
             * malformed frame (struct PktWorldEntityInfo). */
            if( l->value > PKT_WEV_INFO_MAX )
                return 0;
            out->count = (int)l->value;
            break;
        case RSPROT_OP_WE_REMOVE:
            out->updates[l->who].op = PKT_WEV_OP_DESPAWN;
            out->updates[l->who].op_mask = PKT_WEV_OP_MASK_ALL;
            out->updates[l->who].seq_id = -1;
            break;
        case RSPROT_OP_WE_IDLE:
        case RSPROT_OP_WE_MOVE:
            u = &out->updates[l->who];
            u->op = l->kind == RSPROT_OP_WE_IDLE ? PKT_WEV_OP_FLAGS
                    : l->we_move.teleport        ? PKT_WEV_OP_SNAP
                                                 : PKT_WEV_OP_ENQUEUE;
            u->dx = l->we_move.dx;
            u->dy = l->we_move.dy;
            u->dz = l->we_move.dz;
            u->dangle = l->we_move.dangle;
            we_ext(&l->we_move.ext, &u->update_flags, &u->has_op_mask, &u->op_mask, &u->has_seq,
                   &u->seq_id, &u->seq_delay);
            break;
        case RSPROT_OP_WE_ADD:
            if( out->spawn_count >= PKT_WEV_INFO_MAX )
                return 0;
            sp = &out->spawns[out->spawn_count++];
            sp->id = l->who;
            sp->config_id = l->we_add.id;
            sp->priority_group = l->we_add.priority;
            sp->size_x_tiles = l->we_add.size_x;
            sp->size_z_tiles = l->we_add.size_z;
            sp->x = l->we_add.x;
            sp->y = l->we_add.y;
            sp->z = l->we_add.z;
            sp->angle = l->we_add.angle & 0x7ff;
            we_ext(&l->we_add.ext, &sp->update_flags, &sp->has_op_mask, &sp->op_mask, &sp->has_seq,
                   &sp->seq_id, &sp->seq_delay);
            break;
        default:
            break;
        }
    }
    return rsprot_info_list_complete(&list);
}

/** Run `prot`'s codec at `revision` on the cursor. 1 when it decoded cleanly. */
static int
run(int revision, char const* prot, RsprotBuf* c, void* msg)
{
    RsprotExec x;
    RsprotCodecFn fn = rsprot_registry_resolve(prot, RSPROT_REG_OUT, rsprot_osrs(revision));

    if( !fn )
        return 0;
    rsprot_exec_decode(&x, c);
    fn(&x, msg);
    return rsprot_exec_ok(&x) && !c->err;
}

#define RUN(PROT, TYPE, VAR)                                                                      \
    TYPE VAR;                                                                                      \
    memset(&VAR, 0, sizeof(VAR));                                                                  \
    if( !run(revision, PROT, c, &VAR) )                                                            \
    return 0

static char*
dup_borrowed(char const* s)
{
    size_t n;
    char* out;

    assert(s);
    n = strlen(s);
    out = (char*)ToriMem_Alloc(ToriMem_Net(), n + 1);
    assert(out);
    memcpy(out, s, n + 1);
    return out;
}

/* ------------------------------------------------------------------ */
/* Zone sub-packets                                                    */
/* ------------------------------------------------------------------ */

/*
 * One zone sub-packet, from the cursor, by prot. 1 decoded into `out`; 2
 * consumed, with no canonical arm to put it in (the walk carries on past it);
 * 0 failed, which ends the enclosed walk -- the stream has no record lengths,
 * so nothing after an unreadable record can be found.
 */
static int
zone_sub(int revision, char const* prot, RsprotBuf* c, struct PktZoneSubPacket* out)
{
    memset(out, 0, sizeof(*out));

    if( strcmp(prot, "LOC_ADD_CHANGE_V2") == 0 )
    {
        /* One-byte op count, so 256 entries cannot overflow -- the codec reads
         * the count and loops on it in the same call. */
        Rsprot_MsgLocAddChangeV2_opsElem ops[256];
        MsgLocAddChangeV2 m;

        memset(&m, 0, sizeof(m));
        m.ops = ops;
        if( !run(revision, prot, c, &m) )
            return 0;
        out->name = PKT_NAME_LOC_ADD_CHANGE;
        out->_loc_add_change.info = m.loc_properties_packed;
        out->_loc_add_change.pos = m.coord_in_zone_packed;
        out->_loc_add_change.loc_id = m.id;
        out->_loc_add_change.op_flags = m.op_flags;
        /* `key` is the one-based op number (the codec's XFORM -1). */
        for( int i = 0; i < m.ops_count; i++ )
        {
            int slot = ops[i].key - 1;

            if( slot < 0 || slot >= 5 || !ops[i].value )
                continue;
            snprintf(out->_loc_add_change.ops[slot], sizeof(out->_loc_add_change.ops[slot]),
                     "%s", ops[i].value);
        }
        return 1;
    }
    /* 227 and 228's forms: the same four fields, no op overrides. */
    if( strcmp(prot, "LOC_ADD_CHANGE") == 0 || strcmp(prot, "LOC_ADD_CHANGE_V1") == 0 )
    {
        MsgLocAddChange m;

        memset(&m, 0, sizeof(m));
        if( !run(revision, prot, c, &m) )
            return 0;
        out->name = PKT_NAME_LOC_ADD_CHANGE;
        out->_loc_add_change.info = m.loc_properties_packed;
        out->_loc_add_change.pos = m.coord_in_zone_packed;
        out->_loc_add_change.loc_id = m.id;
        out->_loc_add_change.op_flags = m.op_flags;
        return 1;
    }
    if( strcmp(prot, "LOC_DEL") == 0 )
    {
        RUN(prot, MsgLocDel, m);
        out->name = PKT_NAME_LOC_DEL;
        out->_loc_del.info = m.loc_properties_packed;
        out->_loc_del.pos = m.coord_in_zone_packed;
        return 1;
    }
    if( strcmp(prot, "LOC_ANIM") == 0 )
    {
        RUN(prot, MsgLocAnim, m);
        out->name = PKT_NAME_LOC_ANIM;
        out->_loc_anim.info = m.loc_properties_packed;
        out->_loc_anim.pos = m.coord_in_zone_packed;
        out->_loc_anim.seq_id = m.id;
        return 1;
    }
    if( strcmp(prot, "MAP_ANIM") == 0 )
    {
        RUN(prot, MsgMapAnim, m);
        out->name = PKT_NAME_MAP_ANIM;
        out->_map_anim.height = m.height;
        out->_map_anim.id = m.id;
        out->_map_anim.delay = m.delay;
        out->_map_anim.pos = m.coord_in_zone_packed;
        return 1;
    }
    if( strcmp(prot, "SOUND_AREA") == 0 )
    {
        RUN(prot, MsgSoundArea, m);
        out->name = PKT_NAME_SOUND_AREA;
        out->_sound_area.pos = m.coord_in_zone_packed;
        out->_sound_area.radius = m.range;
        out->_sound_area.inner = m.drop_off_range;
        out->_sound_area.loops = m.loops;
        out->_sound_area.id = m.id;
        out->_sound_area.delay = m.delay;
        return 1;
    }
    if( strcmp(prot, "LOC_MERGE") == 0 )
    {
        RUN(prot, MsgLocMerge, m);
        out->name = PKT_NAME_LOC_MERGE;
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
     * 231+: an absolute destination and heights four times the classic scale
     * (World_ProjectileSpawn still applies the classic *4, so they are divided
     * back), as at 239 / 241.
     */
    if( strcmp(prot, "MAP_PROJANIM_V2") == 0 )
    {
        struct PktMapProjAnim* p = &out->_map_projanim;

        RUN(prot, MsgMapProjAnimV2, m);
        out->name = PKT_NAME_MAP_PROJANIM;
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
        p->dst_abs = 1;
        p->dst_abs_level = (m.end_packed >> 28) & 0x3;
        p->dst_abs_x = (m.end_packed >> 14) & 0x3fff;
        p->dst_abs_z = m.end_packed & 0x3fff;
        return 1;
    }
    /*
     * 227-231's forms: a signed per-axis offset from the source tile and
     * one-byte heights at the classic scale -- what PktMapProjAnim was
     * written for. Indices keep V2's 24-bit signed encoding.
     */
    if( strcmp(prot, "MAP_PROJANIM") == 0 || strcmp(prot, "MAP_PROJANIM_V1") == 0 )
    {
        struct PktMapProjAnim* p = &out->_map_projanim;
        MsgMapProjAnim m;

        memset(&m, 0, sizeof(m));
        if( !run(revision, prot, c, &m) )
            return 0;
        out->name = PKT_NAME_MAP_PROJANIM;
        p->start_delay = m.start_time;
        p->pos = m.coord_in_zone_packed;
        p->arc = m.progress;
        p->spotanim = m.id;
        p->end_delay = m.end_time;
        p->dst_height = m.end_height;
        p->peak = m.angle;
        p->src_height = m.start_height;
        p->target = sign24(m.target_index);
        p->source = sign24(m.source_index);
        p->dx_offset = (int8_t)m.delta_x;
        p->dz_offset = (int8_t)m.delta_z;
        return 1;
    }
    if( strcmp(prot, "SCRIPTEDPROJ_ADD") == 0 )
    {
        struct PktScriptedProjAdd* p = &out->_scriptedproj_add;

        RUN(prot, MsgScriptedProjAdd, m);
        out->name = PKT_NAME_SCRIPTEDPROJ_ADD;
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
    if( strcmp(prot, "OBJ_ENABLED_OPS") == 0 )
    {
        RUN(prot, MsgObjEnabledOps, m);
        out->name = PKT_NAME_OBJ_REVEAL;
        out->_obj_reveal.pos = m.coord_in_zone_packed;
        out->_obj_reveal.obj_id = m.id;
        out->_obj_reveal.receiver = -1;
        return 1;
    }
    if( strcmp(prot, "OBJ_DEL") == 0 )
    {
        RUN(prot, MsgObjDel, m);
        out->name = PKT_NAME_OBJ_DEL;
        out->_obj_del.obj_id = m.id;
        out->_obj_del.pos = m.coord_in_zone_packed;
        return 1;
    }
    if( strcmp(prot, "OBJ_COUNT") == 0 )
    {
        RUN(prot, MsgObjCount, m);
        out->name = PKT_NAME_OBJ_COUNT;
        out->_obj_count.old_count = m.old_quantity;
        out->_obj_count.pos = m.coord_in_zone_packed;
        out->_obj_count.new_count = m.new_quantity;
        out->_obj_count.obj_id = m.id;
        return 1;
    }
    if( strcmp(prot, "OBJ_ADD") == 0 )
    {
        RUN(prot, MsgObjAdd, m);
        out->name = PKT_NAME_OBJ_ADD;
        out->_obj_add.pos = m.coord_in_zone_packed;
        out->_obj_add.obj_id = m.id;
        out->_obj_add.count = m.quantity;
        out->_obj_add.time_until_public = m.time_until_public;
        out->_obj_add.time_until_despawn = m.time_until_despawn;
        out->_obj_add.ownership_type = m.ownership_type;
        out->_obj_add.never_becomes_public = m.never_becomes_public;
        return 1;
    }
    /* Anything else the revision's zone index names: decodable, no arm yet.
     * Its codec steps the cursor over it so the records after it survive. */
    {
        union
        {
            long double align;
            uint8_t bytes[4096];
        } scratch;

        memset(&scratch, 0, sizeof(scratch));
        return run(revision, prot, c, &scratch) ? 2 : 0;
    }
}

static int
parse_zone_enclosed(
    struct ProtoRev const* r,
    int revision,
    char const* prot,
    RsprotBuf* c,
    struct RevPacket* out)
{
    struct PktUpdateZoneEnclosed* enc = &out->_update_zone_enclosed;
    RsprotBuf subs;
    int cap = 16;

    {
        RUN(prot, MsgDesktopUpdateZonePartialEnclosed, hdr);
        enc->level = hdr.level;
        enc->base_z = hdr.zone_z;
        enc->base_x = hdr.zone_x;
        rsprot_buf_wrap_read(&subs, hdr.payload, hdr.payload_length);
    }
    enc->count = 0;
    enc->entries = (struct PktZoneSubPacket*)ToriMem_Alloc(
        ToriMem_Net(), (size_t)cap * sizeof(*enc->entries));
    assert(enc->entries);
    for( ;; )
    {
        int ordinal;
        char const* sub = rsprot_zone_enclosed_next(&subs, r->prots, &ordinal);
        int got;

        if( ordinal < 0 )
            break;
        if( enc->count == cap )
        {
            cap *= 2;
            enc->entries = (struct PktZoneSubPacket*)ToriMem_Realloc(
                ToriMem_Net(), enc->entries, (size_t)cap * sizeof(*enc->entries));
            assert(enc->entries);
        }
        got = sub ? zone_sub(revision, sub, &subs, &enc->entries[enc->count]) : 0;
        if( got == 0 )
        {
            TORIRS_ERR("%s: ZONE_ENCLOSED sub-ordinal %d (%s) unreadable at %d/%d\n", r->name,
                       ordinal, sub ? sub : "?", subs.rpos, subs.wpos);
            break;
        }
        if( got == 1 )
            enc->count++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Map rebuilds                                                        */
/* ------------------------------------------------------------------ */

/* A rebuild's XTEA keys, from its codec, into the canonical arm. */
static void
take_keys(int32_t const* keys, int count, struct PktMapRebuild* p)
{
    assert(count >= 0);
    assert(count <= PKT_MAP_REBUILD_ZONES);
    p->region_count = count;
    if( count == 0 )
        return;
    p->region_keys =
        (int32_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)count * 4 * sizeof(*p->region_keys));
    assert(p->region_keys);
    memcpy(p->region_keys, keys, (size_t)count * 4 * sizeof(*p->region_keys));
}

/* A region grid, from its codec (-1: no zone) into the canonical arm (0). */
static void
take_grid(int32_t const* zones, struct PktMapRebuild* p)
{
    p->zones = ToriMem_Calloc(ToriMem_Net(), PKT_MAP_REBUILD_ZONES, sizeof(*p->zones));
    assert(p->zones);
    for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
        p->zones[i] = zones[i] == -1 ? 0 : zones[i];
}

/*
 * REBUILD_NORMAL (227-236), _V1 (237), _V2 (237+). The login variant puts the
 * player-info init block first and shares the opcode; the codec tells the two
 * apart by length (rsprot_tail_gpi_init) and hands the block back, which seeds
 * the player table here as REBUILD_LOGIN does in the deob.
 */
static int
parse_rebuild_normal(
    int revision,
    char const* prot,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;
    RsprotBuf b;
    RsprotBuf* c = &b;

    memset(p, 0, sizeof(*p));
    out->packet_type = PKT_NAME_REBUILD_NORMAL;
    rsprot_buf_wrap_read(c, data, len);
    if( strcmp(prot, "REBUILD_NORMAL_V2") == 0 )
    {
        RUN(prot, MsgRebuildNormalV2, m);
        if( m.gpi_init_length > 0 )
            proto_info_init(m.gpi_init, m.gpi_init_length);
        p->world_area = (int16_t)m.world_area;
        p->zonex = m.zone_x;
        p->zonez = m.zone_z;
        return c->rpos == len;
    }
    {
        MsgRebuildNormal m;

        memset(&m, 0, sizeof(m));
        if( !run(revision, prot, c, &m) || c->rpos != len )
            return 0;
        if( m.gpi_init_length > 0 )
            proto_info_init(m.gpi_init, m.gpi_init_length);
        p->world_area = (int16_t)m.world_area;
        p->zonex = m.zone_x;
        p->zonez = m.zone_z;
        take_keys(&m.keys[0][0], m.xtea_count, p);
        return 1;
    }
}

/*
 * REBUILD_REGION (227-236) / _V1 (237): header, g2 XTEA count, the 4x13x13
 * descriptor grid, then the keys. _V2 (237+): header, g2 source-square count
 * (a loading hint), the grid, no keys.
 */
static int
parse_rebuild_region(int revision, char const* prot, RsprotBuf* c, int len, struct RevPacket* out)
{
    struct PktMapRebuild* p = &out->_map_rebuild;

    memset(p, 0, sizeof(*p));
    out->packet_type = PKT_NAME_REBUILD_REGION;
    if( strcmp(prot, "REBUILD_REGION_V2") == 0 )
    {
        RUN(prot, MsgRebuildRegionV2, m);
        p->zonex = m.zone_x;
        p->zonez = m.zone_z;
        take_grid(m.zones, p);
    }
    else if( strcmp(prot, "REBUILD_REGION_V1") == 0 )
    {
        RUN(prot, MsgRebuildRegionV1, m);
        p->zonex = m.zone_x;
        p->zonez = m.zone_z;
        take_grid(m.zones, p);
        take_keys(m.keys, m.xtea_count, p);
    }
    else
    {
        RUN(prot, MsgRebuildRegion, m);
        p->zonex = m.zone_x;
        p->zonez = m.zone_z;
        take_grid(m.zones, p);
        take_keys(m.keys, m.xtea_count, p);
    }
    return c->rpos == len;
}

/*
 * REBUILD_WORLDENTITY_V2 (227-228: a view index leads), _V3 (229-237: keys
 * trail the grid), _V4 (237+). The grid's size is the ACTIVE view's, which this
 * layer does not know, so it is carried raw for the exec layer
 * (PktRebuildWev_DecodeZones), keys and all: the V3 / V4 codecs, run without a
 * size, hand it back unread (grid_raw). V2's grid is a fixed 13x13 that the
 * codec decodes whole; its raw bytes follow the 8-byte head.
 */
static int
parse_rebuild_wev(
    int revision, char const* prot, RsprotBuf* c, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktRebuildWev* p = &out->_rebuild_wev;

    memset(p, 0, sizeof(*p));
    out->packet_type = PKT_NAME_REBUILD_WORLDENTITY;
    /* Run with the view's size unknown (0): the codec reads the header and
     * either the whole grid (V2's fixed 13x13) or carries it unread. */
    if( strcmp(prot, "REBUILD_WORLDENTITY_V2") == 0 )
    {
        RUN(prot, MsgRebuildWorldEntityV2, m);
        p->base_x = m.base_x;
        p->base_z = m.base_z;
    }
    else if( strcmp(prot, "REBUILD_WORLDENTITY_V3") == 0 )
    {
        RUN(prot, MsgRebuildWorldEntityV3, m);
        if( m.grid_raw_length <= 0 )
            return 0;
        p->base_x = m.base_x;
        p->base_z = m.base_z;
    }
    else
    {
        RUN(prot, MsgRebuildWorldEntityV4, m);
        if( m.grid_raw_length <= 0 )
            return 0;
        p->base_x = m.base_x;
        p->base_z = m.base_z;
    }
    /* The whole payload, and the codec that reads it: the exec layer decodes
     * the grid once it knows the view's size (PktRebuildWev_DecodeZones). */
    p->prot = rsprot_registry_find(prot, RSPROT_REG_OUT)->prot;
    p->rev_id = rsprot_osrs(revision).id;
    p->length = len;
    p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
    assert(p->data);
    memcpy(p->data, data, (size_t)len);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The rest                                                            */
/* ------------------------------------------------------------------ */

/* A packet the store decodes against client state, carried raw once its
 * codec has read it whole: a malformed one stops here, not in the store. */
static int
parse_raw(struct ProtoRev const* r, char const* prot, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktRawPayload* p = &out->_raw_payload;
    int valid = proto_validate(&r->table, prot, data, len);

    /* Every raw prot has a codec (3rd/rsprot/packets); none is a build that
     * dropped one. */
    assert(valid >= 0);
    memset(p, 0, sizeof(*p));
    if( !valid )
        return 0;
    if( len == 0 )
        return 1;
    p->length = len;
    p->data = ToriMem_Alloc(ToriMem_Net(), (size_t)len);
    assert(p->data);
    memcpy(p->data, data, (size_t)len);
    return 1;
}

/* The huffman text a chat message ends with. */
static char*
unpack_text(uint8_t const* huffman, int32_t length)
{
    struct RSCache_Buffer text;

    RSCache_BufferInit(&text, (uint8_t*)huffman, (uint32_t)length);
    return wordpack_unpack(&text, length);
}

static int
parse_message_private(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktMessagePrivate* p = &out->_message_private;

    memset(p, 0, sizeof(*p));
    {
        RUN(prot, MsgMessagePrivate, m);
        if( !m.sender )
            return 0;
        p->from = (int64_t)strtobase37(m.sender);
        p->message_id = m.world_message_counter;
        p->staff_mod = m.chat_crown_type;
        p->text = unpack_text(m.huffman, m.huffman_length);
    }
    return p->text != NULL;
}

static int
parse_message_clanchannel(int revision, char const* prot, RsprotBuf* c, int system, struct RevPacket* out)
{
    struct PktMessageClanChannel* p = &out->_message_clanchannel;

    memset(p, 0, sizeof(*p));
    p->crown = -1;
    if( system )
    {
        RUN(prot, MsgMessageClanchannelSystem, m);
        p->clan_type = (int8_t)m.clan_type;
        p->world = m.world_id;
        p->counter = m.world_message_counter;
        p->text = unpack_text(m.huffman, m.huffman_length);
    }
    else
    {
        RUN(prot, MsgMessageClanchannel, m);
        size_t n;

        if( !m.name )
            return 0;
        n = strlen(m.name);
        if( n >= sizeof(p->sender) )
            return 0;
        memcpy(p->sender, m.name, n + 1);
        p->clan_type = (int8_t)m.clan_type;
        p->world = m.world_id;
        p->counter = m.world_message_counter;
        p->crown = m.chat_crown_type;
        p->text = unpack_text(m.huffman, m.huffman_length);
    }
    return p->text != NULL;
}

static int
parse_stockmarket_slot(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktUpdateStockmarketSlot* p = &out->_update_stockmarket_slot;

    memset(p, 0, sizeof(*p));
    {
        RUN(prot, MsgUpdateStockmarketSlotV1, m);
        p->slot = m.slot;
        p->status = (int8_t)m.status;
        /* An empty slot's remaining 18 bytes are padding. */
        if( p->status != 0 )
        {
            p->obj = m.obj;
            p->price = m.price;
            p->count = m.count;
            p->completed_count = m.completed_count;
            p->completed_gold = m.completed_gold;
        }
    }
    return 1;
}

/* Returns as `parse`, or -2 when `prot` is not a special case. */
static int
parse_special(
    struct ProtoRev const* r,
    char const* prot,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    int revision = r->revision;
    RsprotBuf b;
    RsprotBuf* c = &b;

    rsprot_buf_wrap_read(c, data, len);

    /* The info streams are command streams the exec layer decodes later,
     * against session state; here they are only copied. */
    if( strcmp(prot, "PLAYER_INFO") == 0 )
    {
        out->packet_type = PKT_NAME_PLAYER_INFO;
        out->_player_info.length = len;
        out->_player_info.data = ToriMem_Alloc(ToriMem_Net(), (size_t)(len > 0 ? len : 1));
        assert(out->_player_info.data);
        memcpy(out->_player_info.data, data, (size_t)len);
        return 1;
    }
    if( out->packet_type == PKT_NAME_NPC_INFO || out->packet_type == PKT_NAME_NPC_INFO_LARGE )
    {
        out->_npc_info.length = len;
        out->_npc_info.large = out->packet_type == PKT_NAME_NPC_INFO_LARGE;
        out->_npc_info.data = ToriMem_Alloc(ToriMem_Net(), (size_t)(len > 0 ? len : 1));
        assert(out->_npc_info.data);
        memcpy(out->_npc_info.data, data, (size_t)len);
        return 1;
    }
    if( out->packet_type == PKT_NAME_WORLDENTITY_INFO )
    {
        /* WORLDENTITY_INFO_V<n>: the prot table and the stream files are
         * generated from the same RSProx, so a prot with no reader is a
         * build that regenerated one without the other. */
        RsprotWorldEntityInfoReadFn read = r->worldentity_read[prot[strlen(prot) - 1] - '0'];

        assert(read);
        return worldentity_info_convert(read, data, len, &out->_worldentity_info);
    }
    if( strncmp(prot, "REBUILD_NORMAL", 14) == 0 )
        return parse_rebuild_normal(revision, prot, data, len, out);
    if( strncmp(prot, "REBUILD_REGION", 14) == 0 )
        return parse_rebuild_region(revision, prot, c, len, out);
    if( strncmp(prot, "REBUILD_WORLDENTITY", 19) == 0 )
        return parse_rebuild_wev(revision, prot, c, data, len, out);
    if( strcmp(prot, "UPDATE_ZONE_PARTIAL_ENCLOSED") == 0 )
    {
        out->packet_type = PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED;
        return parse_zone_enclosed(r, revision, prot, c, out);
    }
    if( strcmp(prot, "MESSAGE_PRIVATE") == 0 )
    {
        out->packet_type = PKT_NAME_MESSAGE_PRIVATE;
        return parse_message_private(revision, prot, c, out);
    }
    if( strcmp(prot, "MESSAGE_CLANCHANNEL") == 0 ||
        strcmp(prot, "MESSAGE_CLANCHANNEL_SYSTEM") == 0 )
    {
        int system = strcmp(prot, "MESSAGE_CLANCHANNEL_SYSTEM") == 0;

        out->packet_type = system ? PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM : PKT_NAME_MESSAGE_CLANCHANNEL;
        return parse_message_clanchannel(revision, prot, c, system, out);
    }
    if( strcmp(prot, "UPDATE_STOCKMARKET_SLOT") == 0 )
    {
        out->packet_type = PKT_NAME_UPDATE_STOCKMARKET_SLOT;
        return parse_stockmarket_slot(revision, prot, c, out);
    }
    {
        static char const* const k_raw[] = {
            "UPDATE_FRIENDCHAT_CHANNEL_FULL_V2", "UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER",
            "VARCLAN", "VARCLAN_ENABLE", "VARCLAN_DISABLE", "CLANCHANNEL_FULL",
            "CLANCHANNEL_DELTA", "CLANSETTINGS_FULL", "CLANSETTINGS_DELTA",
            "UPDATE_TRADINGPOST",
        };

        for( size_t i = 0; i < sizeof(k_raw) / sizeof(k_raw[0]); i++ )
        {
            if( strcmp(prot, k_raw[i]) == 0 )
            {
                out->packet_type = (enum GameProtoPktName)proto_canon_server(prot);
                return parse_raw(r, prot, data, len, out);
            }
        }
    }
    return -2;
}

/* ------------------------------------------------------------------ */
/* Codec adapters for the prots no bridge adapter runs                */
/* ------------------------------------------------------------------ */

typedef int (*AdaptFn)(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out);

static int
a_cam_lookat_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamLookAt, m);
    out->_cam_lookat.local_x = m.destination_x_in_build_area;
    out->_cam_lookat.local_z = m.destination_z_in_build_area;
    out->_cam_lookat.height = m.height;
    out->_cam_lookat.rate = m.rate;
    out->_cam_lookat.rate2 = m.rate2;
    return 1;
}

static int
a_cam_lookat_v1(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamLookAtV1, m);
    out->_cam_lookat.local_x = m.destination_x_in_build_area;
    out->_cam_lookat.local_z = m.destination_z_in_build_area;
    out->_cam_lookat.height = m.height;
    out->_cam_lookat.rate = m.rate;
    out->_cam_lookat.rate2 = m.rate2;
    return 1;
}

static int
a_cam_moveto_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamMoveTo, m);
    out->_cam_moveto.local_x = m.destination_x_in_build_area;
    out->_cam_moveto.local_z = m.destination_z_in_build_area;
    out->_cam_moveto.height = m.height;
    out->_cam_moveto.rate = m.rate;
    out->_cam_moveto.rate2 = m.rate2;
    return 1;
}

static int
a_cam_moveto_v1(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamMoveToV1, m);
    out->_cam_moveto.local_x = m.destination_x_in_build_area;
    out->_cam_moveto.local_z = m.destination_z_in_build_area;
    out->_cam_moveto.height = m.height;
    out->_cam_moveto.rate = m.rate;
    out->_cam_moveto.rate2 = m.rate2;
    return 1;
}

/*
 * The cycle-timed camera before V3 (227-240) addressed a BUILD-AREA tile; the
 * canonical struct's coordinates are absolute. The exec layer knows the base;
 * this marks the coordinate local by keeping it under 104 and leaving the
 * conversion to it (PktCamMoveToCycles carries no flag, so the build-area
 * forms are reported as they are -- see the struct).
 */
static int
a_cam_moveto_cycles_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamMoveToCycles, m);
    out->_cam_moveto_cycles.x = m.destination_x_in_build_area;
    out->_cam_moveto_cycles.z = m.destination_z_in_build_area;
    out->_cam_moveto_cycles.height = m.height;
    out->_cam_moveto_cycles.cycles = m.cycles;
    out->_cam_moveto_cycles.ignore_terrain = m.ignore_terrain;
    out->_cam_moveto_cycles.easing = m.easing_id;
    return 1;
}

static int
a_cam_moveto_cycles_v1(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamMoveToCyclesV1, m);
    out->_cam_moveto_cycles.x = m.destination_x_in_build_area;
    out->_cam_moveto_cycles.z = m.destination_z_in_build_area;
    out->_cam_moveto_cycles.height = m.height;
    out->_cam_moveto_cycles.cycles = m.cycles;
    out->_cam_moveto_cycles.ignore_terrain = m.ignore_terrain;
    out->_cam_moveto_cycles.easing = m.easing_id;
    return 1;
}

static int
a_cam_moveto_cycles_v2(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamMoveToCyclesV2, m);
    out->_cam_moveto_cycles.x = m.x;
    out->_cam_moveto_cycles.z = m.z;
    out->_cam_moveto_cycles.height = m.height;
    out->_cam_moveto_cycles.cycles = m.cycles;
    out->_cam_moveto_cycles.ignore_terrain = m.ignore_terrain;
    out->_cam_moveto_cycles.easing = m.easing_id;
    return 1;
}

static int
a_cam_rotateto_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamRotateToCoordinate, m);
    out->_cam_rotateto_coordinate.x = m.destination_x_in_build_area;
    out->_cam_rotateto_coordinate.z = m.destination_z_in_build_area;
    out->_cam_rotateto_coordinate.height = m.height;
    out->_cam_rotateto_coordinate.cycles = m.cycles;
    out->_cam_rotateto_coordinate.easing = m.easing_id;
    return 1;
}

static int
a_cam_rotateto_v1(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamRotateToCoordinateV1, m);
    out->_cam_rotateto_coordinate.x = m.destination_x_in_build_area;
    out->_cam_rotateto_coordinate.z = m.destination_z_in_build_area;
    out->_cam_rotateto_coordinate.height = m.height;
    out->_cam_rotateto_coordinate.cycles = m.cycles;
    out->_cam_rotateto_coordinate.easing = m.easing_id;
    return 1;
}

static int
a_cam_rotateto_v2(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgCamRotateToCoordinateV2, m);
    out->_cam_rotateto_coordinate.x = m.x;
    out->_cam_rotateto_coordinate.z = m.z;
    out->_cam_rotateto_coordinate.height = m.height;
    out->_cam_rotateto_coordinate.cycles = m.cycles;
    out->_cam_rotateto_coordinate.easing = m.easing_id;
    return 1;
}

/* IF_SETEVENTS before V2 (227-231): one 32-bit events word. */
static int
a_if_setevents_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktIfSetEvents* p = &out->_if_setevents;

    if( strcmp(prot, "IF_SETEVENTS_V1") == 0 )
    {
        RUN(prot, MsgIfSetEventsV1, m);
        p->component_id = m.combined_id;
        p->from = (int16_t)m.start;
        p->to = (int16_t)m.end;
        p->events = m.events;
    }
    else
    {
        RUN(prot, MsgIfSetEvents, m);
        p->component_id = m.combined_id;
        p->from = (int16_t)m.start;
        p->to = (int16_t)m.end;
        p->events = m.events;
    }
    p->events1 = (uint32_t)p->events;
    p->events2 = 0;
    return 1;
}

static int
a_if_setmodel_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    if( strcmp(prot, "IF_SETMODEL_V1") == 0 )
    {
        RUN(prot, MsgIfSetModelV1, m);
        out->_if_setmodel.component_id = m.combined_id;
        out->_if_setmodel.model_id = m.model;
    }
    else
    {
        RUN(prot, MsgIfSetModel, m);
        out->_if_setmodel.component_id = m.combined_id;
        out->_if_setmodel.model_id = m.model;
    }
    return 1;
}

/* IF_RESYNC (227-230) / _V1 (231): V2's shape with a single events word. */
#define RESYNC_OLD(TYPE, SUB, EVT)                                                                 \
    {                                                                                              \
        static SUB subs[4096];                                                                     \
        static EVT evts[256];                                                                      \
        TYPE m;                                                                                    \
        memset(&m, 0, sizeof(m));                                                                  \
        m.sub_interfaces = subs;                                                                   \
        m.events = evts;                                                                           \
        if( !run(revision, prot, c, &m) )                                                          \
            return 0;                                                                              \
        p->root_interface_id = m.top_level_interface == 65535 ? -1 : m.top_level_interface;        \
        p->mount_count = m.sub_interfaces_count;                                                   \
        p->event_count = m.events_count;                                                           \
        if( m.sub_interfaces_count > 0 )                                                           \
        {                                                                                          \
            p->mounts = (struct PktIfResyncMount*)ToriMem_Alloc(                                   \
                ToriMem_Net(), (size_t)m.sub_interfaces_count * sizeof(*p->mounts));               \
            assert(p->mounts);                                                                     \
            for( int i = 0; i < m.sub_interfaces_count; i++ )                                      \
            {                                                                                      \
                p->mounts[i].target_uid = subs[i].destination_combined_id;                         \
                p->mounts[i].interface_id = subs[i].interface_id;                                  \
                p->mounts[i].type = subs[i].type;                                                  \
            }                                                                                      \
        }                                                                                          \
        if( m.events_count > 0 )                                                                   \
        {                                                                                          \
            p->events = (struct PktIfResyncEvent*)ToriMem_Alloc(                                   \
                ToriMem_Net(), (size_t)m.events_count * sizeof(*p->events));                       \
            assert(p->events);                                                                     \
            for( int i = 0; i < m.events_count; i++ )                                              \
            {                                                                                      \
                p->events[i].component_id = evts[i].combined_id;                                   \
                p->events[i].from = (int16_t)evts[i].start;                                        \
                p->events[i].to = (int16_t)evts[i].end;                                            \
                p->events[i].events1 = (uint32_t)evts[i].events;                                   \
                p->events[i].events2 = 0;                                                          \
            }                                                                                      \
        }                                                                                          \
    }

static int
a_if_resync_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktIfResync* p = &out->_if_resync;

    memset(p, 0, sizeof(*p));
    if( strcmp(prot, "IF_RESYNC_V1") == 0 )
        RESYNC_OLD(MsgIfResyncV1, Rsprot_MsgIfResyncV1_sub_interfacesElem,
                   Rsprot_MsgIfResyncV1_eventsElem)
    else
        RESYNC_OLD(MsgIfResync, Rsprot_MsgIfResync_sub_interfacesElem,
                   Rsprot_MsgIfResync_eventsElem)
    return 1;
}

/* SET_MAP_FLAG before V2 (227-236): a build-area tile; 255,255 clears. */
static int
a_set_map_flag_old(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    MsgSetMapFlag m;

    memset(&m, 0, sizeof(m));
    if( !run(revision, prot, c, &m) )
        return 0;
    out->_set_map_flag.x = m.x_in_build_area;
    out->_set_map_flag.z = m.z_in_build_area;
    out->_set_map_flag.clear = m.x_in_build_area == 255 && m.z_in_build_area == 255;
    out->_set_map_flag.absolute = 0;
    return 1;
}

static int
a_reboot_timer(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    if( strcmp(prot, "UPDATE_REBOOT_TIMER_V2") == 0 )
    {
        RUN(prot, MsgUpdateRebootTimerV2, m);
        out->_update_reboot_timer.ticks = m.game_cycles;
    }
    else
    {
        RUN(prot, MsgUpdateRebootTimer, m);
        out->_update_reboot_timer.ticks = m.game_cycles;
    }
    return 1;
}

/*
 * SET_ACTIVE_WORLD: 227-228's form names the root by a type byte; 229+ by
 * index, -1 for the root below 237 and 0 from it (RSProx SetActiveWorldV2
 * folds 0 to -1). The canonical struct says root with 0.
 */
static int
a_set_active_world(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    if( strcmp(prot, "SET_ACTIVE_WORLD_V2") == 0 )
    {
        RUN(prot, MsgSetActiveWorldV2, m);
        int index = (int16_t)m.index;

        out->_set_active_world.world_id = index < 0 ? 0 : index;
        out->_set_active_world.level = m.active_level;
    }
    else
    {
        RUN(prot, MsgSetActiveWorldV1, m);
        out->_set_active_world.world_id = m.type == 0 ? 0 : m.index;
        out->_set_active_world.level = m.active_level;
    }
    return 1;
}

static int
a_midi_song_withsecondary(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktMidiSongWithSecondary* p = &out->_midi_song_with_secondary;

    RUN(prot, MsgMidiSongWithSecondary, m);
    p->primary_id = m.primary_id == 65535 ? -1 : m.primary_id;
    p->secondary_id = m.secondary_id == 65535 ? -1 : m.secondary_id;
    /* Speeds are client cycles; the player works in milliseconds. */
    p->fade_in_ms = m.fade_in_speed * 20;
    p->fade_out_ms = m.fade_out_speed * 20;
    return 1;
}

static int
a_midi_swap(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgMidiSwap, m);
    out->_midi_swap.fade_out_ms = m.fade_out_speed * 20;
    out->_midi_swap.fade_in_ms = m.fade_in_speed * 20;
    return 1;
}

static int
a_midi_song_stop(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgMidiSongStop, m);
    out->_midi_song_stop.fade_out_ms = m.fade_out_speed * 20;
    return 1;
}

static int
a_ambientsound_start(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgAmbientSoundStart, m);
    out->_ambientsound_start.id = m.id;
    out->_ambientsound_start.fade_ms = m.fade * 20;
    return 1;
}

static int
a_ambientsound_stop(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgAmbientSoundStop, m);
    out->_ambientsound_stop.fade_ms = m.fade != 0 ? 500 : 0;
    return 1;
}

static int
a_set_player_op(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktSetPlayerOp* p = &out->_set_player_op;

    RUN(prot, MsgSetPlayerOp, m);
    memset(p, 0, sizeof(*p));
    p->slot = m.id;
    p->primary = m.priority;
    p->text = dup_borrowed(m.op ? m.op : "");
    return 1;
}

/* HintArrowDecoder: types 1 / 10 / 11 name an entity, 2..6 a tile. */
static int
a_hint_arrow(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgHintArrow, m);
    out->_hint_arrow.type = m.type;
    if( m.type >= 2 && m.type <= 6 )
    {
        out->_hint_arrow.id = m.x;
        out->_hint_arrow.z = m.z;
    }
    else
    {
        out->_hint_arrow.id = m.index;
    }
    out->_hint_arrow.height = m.height;
    return 1;
}

static int
a_update_runweight(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgUpdateRunWeight, m);
    out->_update_runweight.run_weight = (int16_t)m.runweight;
    return 1;
}

/* MapAnimSpecific: the build-area packed coord is zone << 8 | (in-zone) halves
 * (deob client.java, prot 90), as at 239 / 241. */
static int
a_map_anim_specific(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    RUN(prot, MsgMapAnimSpecific, m);
    int packed = m.coord_in_build_area_packed;

    out->_map_anim_specific.x = ((packed >> 4) & 0x7) + (packed >> 16);
    out->_map_anim_specific.z = (packed & 0x7) + ((packed >> 8) & 0xff);
    out->_map_anim_specific.id = m.id;
    out->_map_anim_specific.height = m.height;
    out->_map_anim_specific.delay = m.delay;
    return 1;
}

/* The zone sub-prots, sent as top-level packets in `follows` mode. */
static int
a_zone_follows(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    struct PktZoneSubPacket sub;
    int got = zone_sub(revision, prot, c, &sub);

    if( got != 1 )
        return 0;
    out->packet_type = sub.name;
    switch( sub.name )
    {
    case PKT_NAME_LOC_ADD_CHANGE: out->_loc_add_change = sub._loc_add_change; break;
    case PKT_NAME_LOC_DEL: out->_loc_del = sub._loc_del; break;
    case PKT_NAME_LOC_ANIM: out->_loc_anim = sub._loc_anim; break;
    case PKT_NAME_LOC_MERGE: out->_loc_merge = sub._loc_merge; break;
    case PKT_NAME_OBJ_ADD: out->_obj_add = sub._obj_add; break;
    case PKT_NAME_OBJ_DEL: out->_obj_del = sub._obj_del; break;
    case PKT_NAME_OBJ_REVEAL: out->_obj_reveal = sub._obj_reveal; break;
    case PKT_NAME_OBJ_COUNT: out->_obj_count = sub._obj_count; break;
    case PKT_NAME_MAP_ANIM: out->_map_anim = sub._map_anim; break;
    case PKT_NAME_SOUND_AREA: out->_sound_area = sub._sound_area; break;
    case PKT_NAME_SCRIPTEDPROJ_ADD: out->_scriptedproj_add = sub._scriptedproj_add; break;
    default: out->_map_projanim = sub._map_projanim; break;
    }
    return 1;
}

/* No payload: the packet is its arrival. The codec still runs, which is what
 * says the payload really is empty at this revision. */
static int
a_empty(int revision, char const* prot, RsprotBuf* c, struct RevPacket* out)
{
    union
    {
        long double align;
        uint8_t bytes[64];
    } scratch;

    (void)out;
    memset(&scratch, 0, sizeof(scratch));
    return run(revision, prot, c, &scratch);
}

struct AdaptRow
{
    char const* prot;
    AdaptFn fn;
};

static struct AdaptRow const k_adapters[] = {
    { "CAM_LOOKAT", a_cam_lookat_old },
    { "CAM_LOOKAT_V1", a_cam_lookat_v1 },
    { "CAM_MOVETO", a_cam_moveto_old },
    { "CAM_MOVETO_V1", a_cam_moveto_v1 },
    { "CAM_MOVETO_CYCLES", a_cam_moveto_cycles_old },
    { "CAM_MOVETO_CYCLES_V1", a_cam_moveto_cycles_v1 },
    { "CAM_MOVETO_CYCLES_V2", a_cam_moveto_cycles_v2 },
    { "CAM_ROTATETO_COORDINATE", a_cam_rotateto_old },
    { "CAM_ROTATETO_COORDINATE_V1", a_cam_rotateto_v1 },
    { "CAM_ROTATETO_COORDINATE_V2", a_cam_rotateto_v2 },
    { "IF_SETEVENTS", a_if_setevents_old },
    { "IF_SETEVENTS_V1", a_if_setevents_old },
    { "IF_SETMODEL", a_if_setmodel_old },
    { "IF_SETMODEL_V1", a_if_setmodel_old },
    { "IF_RESYNC", a_if_resync_old },
    { "IF_RESYNC_V1", a_if_resync_old },
    { "SET_MAP_FLAG", a_set_map_flag_old },
    { "SET_MAP_FLAG_V1", a_set_map_flag_old },
    { "UPDATE_REBOOT_TIMER", a_reboot_timer },
    { "UPDATE_REBOOT_TIMER_V1", a_reboot_timer },
    { "UPDATE_REBOOT_TIMER_V2", a_reboot_timer },
    { "SET_ACTIVE_WORLD", a_set_active_world },
    { "SET_ACTIVE_WORLD_V1", a_set_active_world },
    { "SET_ACTIVE_WORLD_V2", a_set_active_world },
    { "MIDI_SONG_WITHSECONDARY", a_midi_song_withsecondary },
    { "MIDI_SWAP", a_midi_swap },
    { "MIDI_SONG_STOP", a_midi_song_stop },
    { "AMBIENTSOUND_START", a_ambientsound_start },
    { "AMBIENTSOUND_STOP", a_ambientsound_stop },
    { "SET_PLAYER_OP", a_set_player_op },
    { "HINT_ARROW", a_hint_arrow },
    { "UPDATE_RUNWEIGHT", a_update_runweight },
    { "MAP_ANIM_SPECIFIC", a_map_anim_specific },
    { "LOC_ADD_CHANGE", a_zone_follows },
    { "LOC_ADD_CHANGE_V1", a_zone_follows },
    { "LOC_ADD_CHANGE_V2", a_zone_follows },
    { "LOC_DEL", a_zone_follows },
    { "LOC_ANIM", a_zone_follows },
    { "LOC_MERGE", a_zone_follows },
    { "OBJ_ADD", a_zone_follows },
    { "OBJ_DEL", a_zone_follows },
    { "OBJ_COUNT", a_zone_follows },
    { "OBJ_ENABLED_OPS", a_zone_follows },
    { "MAP_ANIM", a_zone_follows },
    { "MAP_PROJANIM", a_zone_follows },
    { "MAP_PROJANIM_V1", a_zone_follows },
    { "MAP_PROJANIM_V2", a_zone_follows },
    { "SOUND_AREA", a_zone_follows },
    { "SCRIPTEDPROJ_ADD", a_zone_follows },
    { "VARP_RESET", a_empty },
    { "VARP_SYNC", a_empty },
    { "CAM_RESET", a_empty },
    { "LOGOUT", a_empty },
    { "RESET_ANIMS", a_empty },
    { "TRIGGER_ONDIALOGABORT", a_empty },
};

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

int
proto_validate(
    struct GameProtoRevTable const* rev,
    char const* prot,
    uint8_t const* data,
    int len)
{
    struct ProtoRev const* r = proto_rev_state(rev);
    RsprotCodecFn fn = rsprot_registry_resolve(prot, RSPROT_REG_OUT, rsprot_osrs(r->revision));
    static union
    {
        long double align;
        uint8_t bytes[1 << 20];
    } scratch;
    RsprotExec x;
    RsprotBuf b;

    if( !fn )
        return -1;
    memset(&scratch, 0, sizeof(scratch));
    rsprot_storage_provide(prot, &scratch, 0);
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_exec_decode(&x, &b);
    fn(&x, &scratch);
    return rsprot_exec_ok(&x) && !b.err && b.rpos == len;
}

int
proto_decode_wire(
    struct GameProtoRevTable const* rev,
    int wire_opcode,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    struct ProtoRev const* r = proto_rev_state(rev);
    RsprotProtDef const* d = proto_server_prot(r, wire_opcode);
    int got;

    if( !d )
        return 0;
    out->packet_type = (enum GameProtoPktName)r->server_name[wire_opcode];

    got = parse_special(r, d->name, data, len, out);
    if( got != -2 )
        return got;

    got = proto_in_adapt(r->revision, d->name, data, len, out);
    if( got >= 0 )
        return got;

    for( size_t i = 0; i < sizeof(k_adapters) / sizeof(k_adapters[0]); i++ )
    {
        if( strcmp(k_adapters[i].prot, d->name) == 0 )
        {
            RsprotBuf b;

            rsprot_buf_wrap_read(&b, data, len);
            got = k_adapters[i].fn(r->revision, d->name, &b, out);
            return got;
        }
    }

    /* No canonical home: decodable or not, nothing reads it. */
    return 0;
}
