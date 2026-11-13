/*
 * The world ledger: scenes, zone-op intervals and entity keys, built in one
 * pass over an RSProx recording. See replay_ledger.h; the entity fold is
 * replay_ledger_entity.c.
 *
 * ## What ends a zone op's interval
 *
 * Each rule mirrors what the client does with the packet (file:line as of
 * d4e6b1b0d). A zone op is alive at frame f when born < f <= died.
 *
 *   LOC_ADD_CHANGE, LOC_DEL   keyed (tile, layer): the next LOC_ADD_CHANGE or
 *                             LOC_DEL on that key ends it. Both replace the
 *                             loc in the shape's layer (rs_gameproto_exec.c:1201
 *                             and :1217 -> App_WorldLocChangeOps; the layer is
 *                             World_LocShapeToLayer, world/world.c:3874, and a
 *                             record's identity is level, x, z, layer,
 *                             rs_gameproto_exec.c:487). A LOC_DEL stays alive:
 *                             it is what removes the map's loc.
 *   OBJ_ADD (count > 0)       creates a ground-item stack, one row per add
 *                             (app/app_world_rebuild.c:301-:318); the stack's
 *                             ops live until the stack goes.
 *   OBJ_REVEAL                for this player (receiver = the local index, or
 *                             no index known: rs_gameproto_exec.c:1195) also
 *                             creates a stack; for any other receiver it does
 *                             nothing (born == died). Its OBJ_ENABLED_OPS form
 *                             (has_enabled_ops, :1189) sets the first stack of
 *                             the id's op mask, replacing the last mask op on
 *                             that stack.
 *   OBJ_DEL                   ends every op of the FIRST stack of the id on the
 *                             tile, in arrival order (App_WorldObjStackDel,
 *                             app_world_rebuild.c:849, World_ObjStackFind); the
 *                             OBJ_DEL itself leaves nothing (born == died).
 *   OBJ_COUNT                 sets the first stack of the id's count
 *                             (App_WorldObjStackSetCount, :879) and so ends
 *                             that stack's previous OBJ_COUNT -- unless an
 *                             OBJ_(UN)CUSTOMISE still alive on the tile was
 *                             born between the two: those find their stack by
 *                             id AND count (World_ObjStackFindExact,
 *                             world/world.c:3828), so the count they saw must
 *                             be re-created before they run.
 *   OBJ_ADD (count <= 0)      sets the ownership of the first stack of the id
 *                             (App_WorldObjStackAdd :348 returns the find,
 *                             rs_gameproto_exec.c:1157 sets it); lives with it.
 *   OBJ_CUSTOMISE             lives with the stack FindExact names (:468); the
 *                             fields accumulate, so a later one does not end it.
 *   OBJ_UNCUSTOMISE           resets the stack's customisation (:515): it ends
 *                             every OBJ_CUSTOMISE on that stack, and itself
 *                             leaves nothing.
 *   UPDATE_ZONE_FULL_FOLLOWS  ends every op in its zone on the header's plane:
 *                             the zone's locs go back to the map and its stacks
 *                             are cleared (rs_gameproto_exec.c:2548-:2566,
 *                             zone_full_reset_locs :555, App_WorldObjStackClearTile).
 *   REBUILD_NORMAL / _REGION  ends every op whose tile is outside the new build
 *                             area: the client shifts its loc-change records
 *                             and stacks by the base delta and drops what falls
 *                             off the scene (task_gameproto_exec.c:291 ->
 *                             App_WorldRebuildShift app_world_rebuild.c:710,
 *                             World_ShiftEntities world/world.c:1629-:1665).
 *                             A rebuild the client skips (same zone,
 *                             App_WorldRebuildBegin :834) has the same base, so
 *                             the rule ends nothing there either.
 *   RECONNECT                 ends nothing: the replay ends the connection
 *                             there and the client reconnects, its RECONNECT_OK
 *                             re-seeding the player table (net.c
 *                             loginproto_drive); the forced REBUILD after it is
 *                             a rebuild like any other, and the server's
 *                             UPDATE_ZONE_FULL_FOLLOWS that follow clear zones.
 *
 * LOC_ANIM, LOC_MERGE, MAP_ANIM, MAP_PROJANIM, SOUND_AREA and the scripted
 * projectiles are transient (the design's "transient" class): not ledger ops.
 * The *_SPECIFIC obj packets are filed under the zone arm the executor turns
 * them into (exec_obj_specific :970, exec_obj_customise_specific :797), and
 * refused outside the scene as there.
 *
 * Zone packets while SET_ACTIVE_WORLD names a world entity go to that entity's
 * view (exec_zone_sub_packet_at :1142), so they are not the root's and are not
 * kept; the tick is flagged.
 */
#include "replay/replay_ledger_internal.h"

#include "net/rev/pktnames.h"
#include "rsprot_rev.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- small helpers ------------------------------------------------------------ */

static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* World_LocShapeToLayer (world/world.c:3874). */
static int
shape_layer(int shape)
{
    if( shape < 0 )
        return 0xff;
    if( shape <= 3 )
        return 0;
    if( shape <= 8 )
        return 1;
    if( shape <= 21 )
        return 2;
    if( shape == 22 )
        return 3;
    return 0xff;
}

int
ledger_frame_name(
    struct ReplayLedger const* ledger,
    uint32_t f)
{
    int opcode;

    assert(ledger);
    assert(f < (uint32_t)ledger->rec->frame_count);
    opcode = ledger->rec->frames[f].opcode;
    if( opcode == RSPROX_RECORDING_RECONNECT || opcode == RSPROX_RECORDING_PACKET_GROUP_END )
        return PKT_NAME_NONE;
    return ledger->rev->packetin_code(opcode);
}

int
ledger_parse(
    struct ReplayLedger const* ledger,
    uint32_t f,
    struct RevPacket* out)
{
    struct RsproxRecordingFrame const* fr;
    int name = ledger_frame_name(ledger, f);

    assert(out);
    /* A login REBUILD's parse seeds the session's player table. */
    assert(name != PKT_NAME_REBUILD_NORMAL);
    assert(name != PKT_NAME_REBUILD_REGION);
    fr = &ledger->rec->frames[f];
    memset(out, 0, sizeof(*out));
    if( ledger->rev->parse_wire(ledger->rev, fr->opcode, ledger->rec->data + fr->offset,
                                (int)fr->length, out) == 1 &&
        (int)out->packet_type == name )
        return 1;
    gameproto_free(out);
    memset(out, 0, sizeof(*out));
    return 0;
}

/*
 * A REBUILD through rev->parse_wire, with the session's player table put back
 * after it. The login variant carries the player-info init block, and its
 * adapter seeds the session table with it (proto_adapt.c adapt_rebuild_normal,
 * _v1, _v2: proto_info_init). That table is the live client's (net/proto/
 * proto_info.c g_pi), so it is saved first and loaded back after; what the
 * parse seeded is handed to the caller as `seeded` (login: 1) instead.
 */
static int
ledger_parse_rebuild(
    struct ReplayLedger const* ledger,
    uint32_t f,
    int local_index,
    struct RevPacket* out,
    RsprotPlayerInfo* seeded,
    int* login)
{
    struct GameProtoRevTable const* rev = ledger->rev;
    struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];
    RsprotPlayerInfo* saved;
    int ok;

    assert(rev->player_info_state_size() == sizeof(RsprotPlayerInfo));
    saved = malloc(sizeof(*saved));
    assert(saved);
    rev->player_info_state_save(saved);
    rev->player_info_set_local(local_index);
    memset(out, 0, sizeof(*out));
    ok = rev->parse_wire(rev, fr->opcode, ledger->rec->data + fr->offset, (int)fr->length, out) ==
         1;
    rev->player_info_state_save(seeded);
    rev->player_info_state_load(saved);
    free(saved);
    *login = seeded->seeded;
    if( !ok )
    {
        gameproto_free(out);
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

int
ledger_scene_at(
    struct ReplayLedger const* ledger,
    uint32_t f)
{
    int lo = 0;
    int hi;

    assert(ledger);
    hi = ledger->scene_count;
    /* The last scene whose frame < f. */
    while( lo < hi )
    {
        int mid = lo + (hi - lo) / 2;

        if( ledger->scenes[mid].frame < f )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo - 1;
}

/* --- the zone fold --------------------------------------------------------- */

struct ZoneRow
{
    uint32_t serial; /* the creating op */
    uint32_t tile;
    int obj_id;
    int count;
    uint32_t count_op;
    uint32_t enabled_op;
    int alive;
};

struct ZoneFold
{
    struct ReplayLedger* ledger;
    struct ReplayEntityState const* entities;
    int has_header;
    int hdr_x;
    int hdr_z;
    int hdr_level;
    int scene;
    int local_pid;
    int active_root;
    int we_count;
    int tick_flag;
    uint32_t* live;
    int live_count;
    int live_cap;
    struct ZoneRow* rows;
    int row_count;
    int row_cap;
    int dead_rows;
};

static int
is_obj_name(int name)
{
    return name == PKT_NAME_OBJ_ADD || name == PKT_NAME_OBJ_DEL || name == PKT_NAME_OBJ_COUNT ||
           name == PKT_NAME_OBJ_REVEAL || name == PKT_NAME_OBJ_CUSTOMISE ||
           name == PKT_NAME_OBJ_UNCUSTOMISE;
}

static void
live_push(
    struct ZoneFold* z,
    uint32_t op)
{
    if( z->live_count == z->live_cap )
    {
        z->live_cap = z->live_cap ? z->live_cap * 2 : 1024;
        z->live = realloc(z->live, (size_t)z->live_cap * sizeof(*z->live));
        assert(z->live);
    }
    z->live[z->live_count++] = op;
    if( z->live_count > z->ledger->live_max )
        z->ledger->live_max = z->live_count;
}

static struct ZoneRow*
row_by_serial(
    struct ZoneFold* z,
    uint32_t serial)
{
    for( int i = 0; i < z->row_count; i++ )
        if( z->rows[i].alive && z->rows[i].serial == serial )
            return &z->rows[i];
    return NULL;
}

static void
rows_compact(struct ZoneFold* z)
{
    int w = 0;

    if( z->dead_rows < 256 || z->dead_rows * 2 < z->row_count )
        return;
    for( int i = 0; i < z->row_count; i++ )
        if( z->rows[i].alive )
            z->rows[w++] = z->rows[i];
    z->row_count = w;
    z->dead_rows = 0;
}

/* End live op at position `at` of the live list at frame `f`. */
static void
live_kill_at(
    struct ZoneFold* z,
    int at,
    uint32_t f)
{
    uint32_t idx = z->live[at];
    struct ReplayZoneOp* op = &z->ledger->ops[idx];

    op->died = f;
    if( is_obj_name(op->name) && op->key == idx )
    {
        struct ZoneRow* row = row_by_serial(z, idx);

        if( row )
        {
            row->alive = 0;
            z->dead_rows++;
        }
    }
    z->live[at] = z->live[--z->live_count];
}

static void
live_kill(
    struct ZoneFold* z,
    uint32_t idx,
    uint32_t f)
{
    for( int i = 0; i < z->live_count; i++ )
        if( z->live[i] == idx )
        {
            live_kill_at(z, i, f);
            return;
        }
    assert(!"live_kill: the op is not alive");
}

/* Every live op of stack `serial`. */
static void
kill_row(
    struct ZoneFold* z,
    uint32_t serial,
    uint32_t f)
{
    for( int i = 0; i < z->live_count; )
    {
        struct ReplayZoneOp const* op = &z->ledger->ops[z->live[i]];

        if( is_obj_name(op->name) && op->key == serial )
            live_kill_at(z, i, f);
        else
            i++;
    }
}

static struct ZoneRow*
row_first(
    struct ZoneFold* z,
    uint32_t tile,
    int obj_id)
{
    for( int i = 0; i < z->row_count; i++ )
        if( z->rows[i].alive && z->rows[i].tile == tile && z->rows[i].obj_id == obj_id )
            return &z->rows[i];
    return NULL;
}

static struct ZoneRow*
row_exact(
    struct ZoneFold* z,
    uint32_t tile,
    int obj_id,
    int count)
{
    for( int i = 0; i < z->row_count; i++ )
        if( z->rows[i].alive && z->rows[i].tile == tile && z->rows[i].obj_id == obj_id &&
            z->rows[i].count == count )
            return &z->rows[i];
    return NULL;
}

static void
row_new(
    struct ZoneFold* z,
    uint32_t serial,
    uint32_t tile,
    int obj_id,
    int count)
{
    struct ZoneRow* row;

    rows_compact(z);
    if( z->row_count == z->row_cap )
    {
        z->row_cap = z->row_cap ? z->row_cap * 2 : 256;
        z->rows = realloc(z->rows, (size_t)z->row_cap * sizeof(*z->rows));
        assert(z->rows);
    }
    row = &z->rows[z->row_count++];
    row->serial = serial;
    row->tile = tile;
    row->obj_id = obj_id;
    row->count = count;
    row->count_op = REPLAY_LEDGER_NEVER;
    row->enabled_op = REPLAY_LEDGER_NEVER;
    row->alive = 1;
}

/* A live OBJ_(UN)CUSTOMISE on `tile` born after op `after`. */
static int
exact_live_after(
    struct ZoneFold* z,
    uint32_t tile,
    uint32_t after)
{
    for( int i = 0; i < z->live_count; i++ )
    {
        struct ReplayZoneOp const* op = &z->ledger->ops[z->live[i]];

        if( z->live[i] > after && op->tile == tile &&
            (op->name == PKT_NAME_OBJ_CUSTOMISE || op->name == PKT_NAME_OBJ_UNCUSTOMISE) )
            return 1;
    }
    return 0;
}

static int
sub_pos(struct PktZoneSubPacket const* sp)
{
    switch( sp->name )
    {
    case PKT_NAME_OBJ_ADD:
        return sp->_obj_add.pos;
    case PKT_NAME_OBJ_DEL:
        return sp->_obj_del.pos;
    case PKT_NAME_OBJ_COUNT:
        return sp->_obj_count.pos;
    case PKT_NAME_OBJ_REVEAL:
        return sp->_obj_reveal.pos;
    case PKT_NAME_LOC_DEL:
        return sp->_loc_del.pos;
    case PKT_NAME_LOC_ADD_CHANGE:
        return sp->_loc_add_change.pos;
    case PKT_NAME_OBJ_CUSTOMISE:
        return sp->_obj_customise.pos;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        return sp->_obj_uncustomise.pos;
    default:
        return -1;
    }
}

static uint32_t
op_append(
    struct ReplayLedger* ledger,
    uint32_t f,
    uint16_t sub,
    int name,
    uint32_t tile)
{
    struct ReplayZoneOp* op;

    if( ledger->op_count == ledger->op_cap )
    {
        ledger->op_cap = ledger->op_cap ? ledger->op_cap * 2 : 4096;
        ledger->ops = realloc(ledger->ops, (size_t)ledger->op_cap * sizeof(*ledger->ops));
        assert(ledger->ops);
    }
    op = &ledger->ops[ledger->op_count];
    op->born = f;
    op->died = REPLAY_LEDGER_NEVER;
    op->tile = tile;
    op->key = 0;
    op->sub = sub;
    op->name = (uint16_t)name;
    return (uint32_t)ledger->op_count++;
}

/* One loc / ground-item change, with its zone's south-west tile absolute. */
static void
zone_op(
    struct ZoneFold* z,
    uint32_t f,
    uint16_t sub,
    struct PktZoneSubPacket const* sp,
    int base_x,
    int base_z,
    int level)
{
    struct ReplayLedger* ledger = z->ledger;
    int pos = sub_pos(sp);
    uint32_t tile;
    uint32_t idx;
    struct ReplayZoneOp* op;
    struct ZoneRow* row;

    if( pos < 0 )
        return; /* transient: not a ledger op */
    tile = REPLAY_TILE(base_x + ((pos >> 4) & 7), base_z + (pos & 7), level);
    idx = op_append(ledger, f, sub, sp->name, tile);
    op = &ledger->ops[idx];

    switch( sp->name )
    {
    case PKT_NAME_LOC_ADD_CHANGE:
    case PKT_NAME_LOC_DEL:
    {
        int info = sp->name == PKT_NAME_LOC_DEL ? sp->_loc_del.info : sp->_loc_add_change.info;

        op->key = (uint32_t)shape_layer(info >> 2);
        for( int i = 0; i < z->live_count; i++ )
        {
            struct ReplayZoneOp const* other = &ledger->ops[z->live[i]];

            if( other->tile == tile && other->key == op->key &&
                (other->name == PKT_NAME_LOC_ADD_CHANGE || other->name == PKT_NAME_LOC_DEL) )
            {
                live_kill_at(z, i, f);
                break;
            }
        }
        live_push(z, idx);
        return;
    }
    case PKT_NAME_OBJ_ADD:
        if( sp->_obj_add.count > 0 )
        {
            op->key = idx;
            row_new(z, idx, tile, sp->_obj_add.obj_id, sp->_obj_add.count);
            live_push(z, idx);
            return;
        }
        row = row_first(z, tile, sp->_obj_add.obj_id);
        if( !row )
            break;
        op->key = row->serial;
        live_push(z, idx);
        return;
    case PKT_NAME_OBJ_REVEAL:
        if( sp->_obj_reveal.has_enabled_ops )
        {
            row = row_first(z, tile, sp->_obj_reveal.obj_id);
            if( !row )
                break;
            op->key = row->serial;
            if( row->enabled_op != REPLAY_LEDGER_NEVER )
                live_kill(z, row->enabled_op, f);
            row->enabled_op = idx;
            live_push(z, idx);
            return;
        }
        if( z->local_pid >= 0 && sp->_obj_reveal.receiver != z->local_pid )
            break;
        if( sp->_obj_reveal.count <= 0 )
            break;
        op->key = idx;
        row_new(z, idx, tile, sp->_obj_reveal.obj_id, sp->_obj_reveal.count);
        live_push(z, idx);
        return;
    case PKT_NAME_OBJ_DEL:
        row = row_first(z, tile, sp->_obj_del.obj_id);
        if( row )
        {
            op->key = row->serial;
            kill_row(z, row->serial, f);
        }
        break;
    case PKT_NAME_OBJ_COUNT:
        row = row_first(z, tile, sp->_obj_count.obj_id);
        if( !row )
            break;
        op->key = row->serial;
        if( row->count_op != REPLAY_LEDGER_NEVER && !exact_live_after(z, tile, row->count_op) )
            live_kill(z, row->count_op, f);
        row->count_op = idx;
        row->count = sp->_obj_count.new_count;
        live_push(z, idx);
        return;
    case PKT_NAME_OBJ_CUSTOMISE:
        row = row_exact(z, tile, sp->_obj_customise.obj_id, sp->_obj_customise.count);
        if( !row )
            break;
        op->key = row->serial;
        live_push(z, idx);
        return;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        row = row_exact(z, tile, sp->_obj_uncustomise.obj_id, sp->_obj_uncustomise.count);
        if( !row )
            break;
        op->key = row->serial;
        for( int i = 0; i < z->live_count; )
        {
            struct ReplayZoneOp const* other = &ledger->ops[z->live[i]];

            if( other->name == PKT_NAME_OBJ_CUSTOMISE && other->key == row->serial )
                live_kill_at(z, i, f);
            else
                i++;
        }
        break;
    default:
        assert(!"zone_op: a name sub_pos accepts and the switch does not");
    }
    /* Left nothing behind. */
    op->died = f;
}

/* The header's plane; a header with none is the local player's
 * (zone_header_level, rs_gameproto_exec.c:253). */
static int
header_level(
    struct ZoneFold const* z,
    int level)
{
    RsprotPlayerInfo const* pi;
    int local;

    if( level >= 0 )
        return level & 3;
    pi = &z->entities->pi;
    local = pi->local_index;
    if( local <= 0 || local >= RSPROT_INFO_PLAYER_SLOTS )
        return 0;
    return REPLAY_TILE_LEVEL((uint32_t)pi->coord[local]);
}

/* The bare zone packets' payloads as the sub-packet the zone arm takes. */
static int
bare_sub(
    struct RevPacket const* p,
    struct PktZoneSubPacket* sp)
{
    memset(sp, 0, sizeof(*sp));
    sp->name = p->packet_type;
    switch( p->packet_type )
    {
    case PKT_NAME_OBJ_ADD:
        sp->_obj_add = p->_obj_add;
        return 1;
    case PKT_NAME_OBJ_DEL:
        sp->_obj_del = p->_obj_del;
        return 1;
    case PKT_NAME_OBJ_COUNT:
        sp->_obj_count = p->_obj_count;
        return 1;
    case PKT_NAME_OBJ_REVEAL:
        sp->_obj_reveal = p->_obj_reveal;
        return 1;
    case PKT_NAME_LOC_DEL:
        sp->_loc_del = p->_loc_del;
        return 1;
    case PKT_NAME_LOC_ADD_CHANGE:
        sp->_loc_add_change = p->_loc_add_change;
        return 1;
    case PKT_NAME_OBJ_CUSTOMISE:
        sp->_obj_customise = p->_obj_customise;
        return 1;
    case PKT_NAME_OBJ_UNCUSTOMISE:
        sp->_obj_uncustomise = p->_obj_uncustomise;
        return 1;
    default:
        return 0;
    }
}

/* exec_obj_specific / exec_obj_customise_specific's conversion: the sub-packet
 * an *_SPECIFIC frame becomes, with its absolute tile in `x`, `z`, `level`. */
static int
specific_sub(
    struct RevPacket const* p,
    struct PktZoneSubPacket* sp,
    int* x,
    int* z,
    int* level)
{
    memset(sp, 0, sizeof(*sp));
    switch( p->packet_type )
    {
    case PKT_NAME_OBJ_ADD_SPECIFIC:
    case PKT_NAME_OBJ_DEL_SPECIFIC:
    case PKT_NAME_OBJ_COUNT_SPECIFIC:
    case PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC:
    {
        struct PktObjSpecific const* s = &p->_obj_specific;
        int pos = ((s->x & 7) << 4) | (s->z & 7);

        *x = s->x;
        *z = s->z;
        *level = s->level;
        if( p->packet_type == PKT_NAME_OBJ_ADD_SPECIFIC )
        {
            sp->name = PKT_NAME_OBJ_ADD;
            sp->_obj_add.pos = pos;
            sp->_obj_add.obj_id = s->obj_id;
            sp->_obj_add.count = s->count;
            sp->_obj_add.time_until_public = s->time_until_public;
            sp->_obj_add.time_until_despawn = s->time_until_despawn;
            sp->_obj_add.ownership_type = s->ownership_type;
            sp->_obj_add.never_becomes_public = s->never_becomes_public;
        }
        else if( p->packet_type == PKT_NAME_OBJ_DEL_SPECIFIC )
        {
            sp->name = PKT_NAME_OBJ_DEL;
            sp->_obj_del.pos = pos;
            sp->_obj_del.obj_id = s->obj_id;
        }
        else if( p->packet_type == PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC )
        {
            sp->name = PKT_NAME_OBJ_REVEAL;
            sp->_obj_reveal.pos = pos;
            sp->_obj_reveal.obj_id = s->obj_id;
            sp->_obj_reveal.receiver = -1;
            sp->_obj_reveal.has_enabled_ops = 1;
            sp->_obj_reveal.enabled_ops = s->op_flags;
        }
        else
        {
            sp->name = PKT_NAME_OBJ_COUNT;
            sp->_obj_count.pos = pos;
            sp->_obj_count.obj_id = s->obj_id;
            sp->_obj_count.old_count = s->old_count;
            sp->_obj_count.new_count = s->count;
        }
        return 1;
    }
    case PKT_NAME_OBJ_CUSTOMISE_SPECIFIC:
    case PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC:
    {
        struct PktObjCustomiseSpecific const* s = &p->_obj_customise_specific;
        int pos = ((s->x & 7) << 4) | (s->z & 7);

        *x = s->x;
        *z = s->z;
        *level = s->level;
        if( p->packet_type == PKT_NAME_OBJ_CUSTOMISE_SPECIFIC )
        {
            sp->name = PKT_NAME_OBJ_CUSTOMISE;
            sp->_obj_customise.pos = pos;
            sp->_obj_customise.obj_id = s->obj_id;
            sp->_obj_customise.count = s->count;
            sp->_obj_customise.fields = s->fields;
        }
        else
        {
            sp->name = PKT_NAME_OBJ_UNCUSTOMISE;
            sp->_obj_uncustomise.pos = pos;
            sp->_obj_uncustomise.obj_id = s->obj_id;
            sp->_obj_uncustomise.count = s->count;
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int
is_bare_zone(int name)
{
    return name == PKT_NAME_OBJ_ADD || name == PKT_NAME_OBJ_DEL || name == PKT_NAME_OBJ_COUNT ||
           name == PKT_NAME_OBJ_REVEAL || name == PKT_NAME_LOC_DEL ||
           name == PKT_NAME_LOC_ADD_CHANGE || name == PKT_NAME_OBJ_CUSTOMISE ||
           name == PKT_NAME_OBJ_UNCUSTOMISE;
}

static int
is_specific(int name)
{
    return name == PKT_NAME_OBJ_ADD_SPECIFIC || name == PKT_NAME_OBJ_DEL_SPECIFIC ||
           name == PKT_NAME_OBJ_COUNT_SPECIFIC || name == PKT_NAME_OBJ_ENABLED_OPS_SPECIFIC ||
           name == PKT_NAME_OBJ_CUSTOMISE_SPECIFIC || name == PKT_NAME_OBJ_UNCUSTOMISE_SPECIFIC;
}

static void
scene_push(
    struct ReplayLedger* ledger,
    struct ReplayLedgerScene const* row,
    RsprotPlayerInfo const* seeded)
{
    if( ledger->scene_count == ledger->scene_cap )
    {
        ledger->scene_cap = ledger->scene_cap ? ledger->scene_cap * 2 : 64;
        ledger->scenes = realloc(ledger->scenes, (size_t)ledger->scene_cap * sizeof(*ledger->scenes));
        ledger->login_tables = realloc(
            ledger->login_tables, (size_t)ledger->scene_cap * sizeof(*ledger->login_tables));
        assert(ledger->scenes);
        assert(ledger->login_tables);
    }
    ledger->scenes[ledger->scene_count] = *row;
    ledger->login_tables[ledger->scene_count] = NULL;
    if( row->login )
    {
        RsprotPlayerInfo* copy = malloc(sizeof(*copy));

        assert(copy);
        memcpy(copy, seeded, sizeof(*copy));
        ledger->login_tables[ledger->scene_count] = copy;
    }
    ledger->scene_count++;
}

static void
zone_rebuild(
    struct ZoneFold* z,
    uint32_t f,
    int name)
{
    struct ReplayLedger* ledger = z->ledger;
    struct RevPacket pkt;
    RsprotPlayerInfo* seeded = malloc(sizeof(*seeded));
    struct ReplayLedgerScene row;
    int login = 0;

    assert(seeded);
    if( !ledger_parse_rebuild(ledger, f, z->entities->pi.local_index, &pkt, seeded, &login) )
    {
        free(seeded);
        return;
    }
    memset(&row, 0, sizeof(row));
    row.frame = f;
    row.tick = ledger->rec->frames[f].tick;
    row.name = name;
    row.zone_x = pkt._map_rebuild.zonex;
    row.zone_z = pkt._map_rebuild.zonez;
    row.size = REPLAY_LEDGER_SCENE_SIZE;
    row.instanced = pkt._map_rebuild.zones != NULL;
    row.login = login;
    if( !ledger->rev->scene_base ||
        !ledger->rev->scene_base(&pkt, &row.base_x, &row.base_z) )
    {
        row.base_x = (row.zone_x - 6) * 8;
        row.base_z = (row.zone_z - 6) * 8;
    }
    gameproto_free(&pkt);
    /* A rebuild with the scene already loaded shifts what the client kept and
     * drops what left the area (App_WorldRebuildShift). */
    if( z->scene >= 0 )
    {
        for( int i = 0; i < z->live_count; )
        {
            uint32_t t = ledger->ops[z->live[i]].tile;
            int x = REPLAY_TILE_X(t);
            int tz = REPLAY_TILE_Z(t);

            if( x < row.base_x || tz < row.base_z || x >= row.base_x + row.size ||
                tz >= row.base_z + row.size )
                live_kill_at(z, i, f);
            else
                i++;
        }
    }
    scene_push(ledger, &row, seeded);
    z->scene = ledger->scene_count - 1;
    free(seeded);
}

static void
zone_clear(
    struct ZoneFold* z,
    uint32_t f,
    int base_x,
    int base_z,
    int level)
{
    struct ReplayLedger* ledger = z->ledger;
    struct ReplayLedgerZoneClear* c;

    for( int i = 0; i < z->live_count; )
    {
        uint32_t t = z->ledger->ops[z->live[i]].tile;

        if( (REPLAY_TILE_X(t) >> 3) == (base_x >> 3) && (REPLAY_TILE_Z(t) >> 3) == (base_z >> 3) &&
            REPLAY_TILE_LEVEL(t) == level )
            live_kill_at(z, i, f);
        else
            i++;
    }
    ledger->clears = realloc(ledger->clears, (size_t)(ledger->clear_count + 1) * sizeof(*c));
    assert(ledger->clears);
    c = &ledger->clears[ledger->clear_count++];
    c->frame = f;
    c->tile = REPLAY_TILE(base_x, base_z, level);
}

static void
zone_frame(
    struct ZoneFold* z,
    uint32_t f)
{
    struct ReplayLedger* ledger = z->ledger;
    struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];
    struct ReplayLedgerScene const* scene;
    struct RevPacket pkt;
    int name;

    if( fr->opcode == RSPROX_RECORDING_RECONNECT )
    {
        z->local_pid = fr->reconnect_index;
        return;
    }
    name = ledger_frame_name(ledger, f);
    if( name == PKT_NAME_REBUILD_NORMAL || name == PKT_NAME_REBUILD_REGION )
    {
        zone_rebuild(z, f, name);
        return;
    }
    switch( name )
    {
    case PKT_NAME_SERVER_TICK_END:
        z->active_root = 1;
        return;
    case PKT_NAME_REBUILD_WORLDENTITY:
        z->tick_flag = 1;
        return;
    case PKT_NAME_SET_ACTIVE_WORLD:
        if( ledger_parse(ledger, f, &pkt) )
        {
            z->active_root = pkt._set_active_world.world_id == -1;
            gameproto_free(&pkt);
        }
        if( !z->active_root )
            z->tick_flag = 1;
        return;
    case PKT_NAME_CLEAR_ENTITIES:
        z->active_root = 1;
        return;
    case PKT_NAME_WORLDENTITY_INFO:
        if( ledger_parse(ledger, f, &pkt) )
        {
            z->we_count = pkt._worldentity_info.count + pkt._worldentity_info.spawn_count;
            gameproto_free(&pkt);
        }
        if( z->we_count > 0 )
            z->tick_flag = 1;
        return;
    case PKT_NAME_UPDATE_PID:
        if( ledger_parse(ledger, f, &pkt) )
        {
            z->local_pid = pkt._update_pid.local_player_index;
            gameproto_free(&pkt);
        }
        return;
    default:
        break;
    }
    if( name != PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS && name != PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS &&
        name != PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED && !is_bare_zone(name) && !is_specific(name) )
        return;
    if( !z->active_root )
    {
        ledger->stats.zone_world_entity++;
        z->tick_flag = 1;
        return;
    }
    if( !ledger_parse(ledger, f, &pkt) )
        return;
    scene = z->scene >= 0 ? &ledger->scenes[z->scene] : NULL;
    if( name == PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS )
    {
        z->has_header = 1;
        z->hdr_x = pkt._update_zone_partial_follows.base_x;
        z->hdr_z = pkt._update_zone_partial_follows.base_z;
        z->hdr_level = pkt._update_zone_partial_follows.level;
    }
    else if( name == PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS )
    {
        z->has_header = 1;
        z->hdr_x = pkt._update_zone_full_follows.base_x;
        z->hdr_z = pkt._update_zone_full_follows.base_z;
        z->hdr_level = pkt._update_zone_full_follows.level;
        if( scene )
            zone_clear(z, f, scene->base_x + z->hdr_x, scene->base_z + z->hdr_z,
                       header_level(z, z->hdr_level));
    }
    else if( name == PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED )
    {
        struct PktUpdateZoneEnclosed const* enc = &pkt._update_zone_enclosed;

        z->has_header = 1;
        z->hdr_x = enc->base_x;
        z->hdr_z = enc->base_z;
        z->hdr_level = enc->level;
        if( !scene )
            ledger->stats.zone_no_scene++;
        else
            for( int i = 0; i < enc->count; i++ )
                zone_op(z, f, (uint16_t)i, &enc->entries[i], scene->base_x + z->hdr_x,
                        scene->base_z + z->hdr_z, header_level(z, z->hdr_level));
    }
    else if( is_bare_zone(name) )
    {
        struct PktZoneSubPacket sp;

        if( !scene || !z->has_header )
            ledger->stats.zone_no_scene++;
        else if( bare_sub(&pkt, &sp) )
            zone_op(z, f, 0, &sp, scene->base_x + z->hdr_x, scene->base_z + z->hdr_z,
                    header_level(z, z->hdr_level));
    }
    else
    {
        struct PktZoneSubPacket sp;
        int x, tz, level;

        if( !scene )
            ledger->stats.zone_no_scene++;
        else if( specific_sub(&pkt, &sp, &x, &tz, &level) )
        {
            /* exec_obj_specific :997: refused outside the scene. */
            if( x < scene->base_x || tz < scene->base_z || x >= scene->base_x + scene->size ||
                tz >= scene->base_z + scene->size )
                ledger->stats.zone_outside_scene++;
            else
                zone_op(z, f, 0, &sp, x & ~7, tz & ~7, level);
        }
    }
    gameproto_free(&pkt);
}

/* --- checkpoints and the stabbing query ------------------------------------ */

static void
build_checkpoints(struct ReplayLedger* ledger)
{
    int const n = REPLAY_LEDGER_CHECKPOINT_OPS;
    int count = ledger->op_count / n + 1;
    uint32_t* prev = NULL;
    int prev_count = 0;
    size_t cap = 0;
    size_t used = 0;

    ledger->checkpoint_count = count;
    ledger->cp_first = calloc((size_t)count + 1, sizeof(*ledger->cp_first));
    assert(ledger->cp_first);
    ledger->cp_ops = NULL;
    for( int c = 0; c < count; c++ )
    {
        uint32_t* cur = NULL;
        int cur_count = 0;

        ledger->cp_first[c] = (uint32_t)used;
        if( c > 0 )
        {
            uint32_t t = ledger->ops[c * n - 1].born;
            int cand = prev_count + n;

            cur = malloc((size_t)cand * sizeof(*cur));
            assert(cur);
            for( int i = 0; i < prev_count; i++ )
                if( ledger->ops[prev[i]].died > t )
                    cur[cur_count++] = prev[i];
            for( int i = (c - 1) * n; i < c * n; i++ )
                if( ledger->ops[i].died > t )
                    cur[cur_count++] = (uint32_t)i;
        }
        if( used + (size_t)cur_count > cap )
        {
            while( used + (size_t)cur_count > cap )
                cap = cap ? cap * 2 : 4096;
            ledger->cp_ops = realloc(ledger->cp_ops, cap * sizeof(*ledger->cp_ops));
            assert(ledger->cp_ops);
        }
        if( cur_count > 0 )
            memcpy(ledger->cp_ops + used, cur, (size_t)cur_count * sizeof(*cur));
        used += (size_t)cur_count;
        free(prev);
        prev = cur;
        prev_count = cur_count;
    }
    ledger->cp_first[count] = (uint32_t)used;
    free(prev);
}

int
ReplayLedger_ZoneOpsAlive(
    struct ReplayLedger const* ledger,
    uint32_t frame,
    uint32_t* out,
    int cap)
{
    int lo = 0;
    int hi;
    int c;
    int count = 0;

    assert(ledger);
    assert(out || cap == 0);
    (void)cap; /* read only by the asserts below */
    hi = ledger->op_count;
    /* K: ops born before `frame`. */
    while( lo < hi )
    {
        int mid = lo + (hi - lo) / 2;

        if( ledger->ops[mid].born < frame )
            lo = mid + 1;
        else
            hi = mid;
    }
    c = lo / REPLAY_LEDGER_CHECKPOINT_OPS;
    assert(c < ledger->checkpoint_count);
    for( uint32_t i = ledger->cp_first[c]; i < ledger->cp_first[c + 1]; i++ )
    {
        uint32_t idx = ledger->cp_ops[i];

        if( ledger->ops[idx].died >= frame )
        {
            assert(count < cap);
            out[count++] = idx;
        }
    }
    for( int i = c * REPLAY_LEDGER_CHECKPOINT_OPS; i < lo; i++ )
        if( ledger->ops[i].died >= frame )
        {
            assert(count < cap);
            out[count++] = (uint32_t)i;
        }
    return count;
}

int
ReplayLedger_ZoneOpsAliveMax(struct ReplayLedger const* ledger)
{
    assert(ledger);
    return ledger->live_max;
}

int
ReplayLedger_ZoneOpDecode(
    struct ReplayLedger const* ledger,
    int index,
    int scene_base_x,
    int scene_base_z,
    struct ReplayWorldZoneOp* out)
{
    struct ReplayZoneOp const* op;
    struct RevPacket pkt;
    int name;
    int ok = 0;

    assert(ledger);
    assert(out);
    assert(index >= 0);
    assert(index < ledger->op_count);
    op = &ledger->ops[index];
    memset(out, 0, sizeof(*out));
    if( !ledger_parse(ledger, op->born, &pkt) )
        return 0;
    name = pkt.packet_type;
    if( name == PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED )
    {
        if( op->sub < pkt._update_zone_enclosed.count )
        {
            out->pkt = pkt._update_zone_enclosed.entries[op->sub];
            ok = 1;
        }
    }
    else if( is_bare_zone(name) )
        ok = bare_sub(&pkt, &out->pkt);
    else
    {
        int x, z, level;

        ok = specific_sub(&pkt, &out->pkt, &x, &z, &level);
    }
    gameproto_free(&pkt);
    if( !ok || (int)out->pkt.name != (int)op->name )
        return 0;
    out->base_x = (REPLAY_TILE_X(op->tile) & ~7) - scene_base_x;
    out->base_z = (REPLAY_TILE_Z(op->tile) & ~7) - scene_base_z;
    out->level = REPLAY_TILE_LEVEL(op->tile);
    out->op = (uint32_t)index;
    return 1;
}

/* --- the build ----------------------------------------------------------- */

struct ReplayLedger*
ReplayLedger_Build(
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev)
{
    struct ReplayLedger* ledger;
    struct ReplayEntityState* state;
    struct ZoneFold z;
    RsprotRev rr;
    double t0 = now_ms();
    uint32_t f = 0;

    assert(rec);
    assert(rev);
    assert(rec->frames || rec->frame_count == 0);
    assert(rev->parse_wire);
    assert(rev->player_info_state_size);
    assert(rev->player_info_state_save);
    assert(rev->player_info_state_load);
    assert(rev->player_info_set_local);
    assert(rev->npc_info_read);
    assert(rev->player_info_state_size() == sizeof(RsprotPlayerInfo));

    ledger = calloc(1, sizeof(*ledger));
    assert(ledger);
    ledger->rec = rec;
    ledger->rev = rev;
    rr = rsprot_rev_parse(rev->name);
    ledger->player_read =
        rsprot_rev_family(rr) == RSPROT_FAMILY_OSRSM ? player_info_osrsm(rr) : player_info_osrs(rr);
    /* An OSRS table names its rsprot entry point; the ledger reads its own
     * table through it. */
    assert(ledger->player_read);

    ledger->tick_count = rec->tick_count;
    ledger->first_frame = calloc((size_t)rec->tick_count + 1, sizeof(*ledger->first_frame));
    ledger->local_tile = calloc((size_t)rec->tick_count + 1, sizeof(*ledger->local_tile));
    ledger->world_entity = calloc((size_t)rec->tick_count + 1, 1);
    assert(ledger->first_frame);
    assert(ledger->local_tile);
    assert(ledger->world_entity);
    for( int t = 0; t <= rec->tick_count; t++ )
    {
        while( f < (uint32_t)rec->frame_count && rec->frames[f].tick < t )
            f++;
        ledger->first_frame[t] = f;
    }

    state = ReplayLedger_EntityNew(ledger);
    memset(&z, 0, sizeof(z));
    z.ledger = ledger;
    z.entities = state;
    z.scene = -1;
    z.local_pid = rec->local_player_index;
    z.active_root = 1;

    ledger->key_count = rec->tick_count / REPLAY_LEDGER_KEY_TICKS + 1;
    ledger->keys = calloc((size_t)ledger->key_count, sizeof(*ledger->keys));
    assert(ledger->keys);
    ledger_key_take(state, &ledger->keys[0]);

    for( int t = 0; t < rec->tick_count; t++ )
    {
        uint32_t coord = 0;

        if( state->pi.local_index > 0 && state->pi.local_index < RSPROT_INFO_PLAYER_SLOTS )
            coord = (uint32_t)state->pi.coord[state->pi.local_index];
        ledger->local_tile[t] = coord;
        z.tick_flag = z.we_count > 0;
        for( f = ledger->first_frame[t]; f < ledger->first_frame[t + 1]; f++ )
        {
            zone_frame(&z, f);
            ledger_entity_step(ledger, ledger, state, f);
        }
        ledger->world_entity[t] = (uint8_t)z.tick_flag;
        state->tick = t + 1;
        if( (t + 1) % REPLAY_LEDGER_KEY_TICKS == 0 )
            ledger_key_take(state, &ledger->keys[(t + 1) / REPLAY_LEDGER_KEY_TICKS]);
    }
    if( state->pi.local_index > 0 && state->pi.local_index < RSPROT_INFO_PLAYER_SLOTS )
        ledger->local_tile[rec->tick_count] = (uint32_t)state->pi.coord[state->pi.local_index];

    for( int i = 1; i < ledger->op_count; i++ )
        assert(ledger->ops[i - 1].born < ledger->ops[i].born ||
               (ledger->ops[i - 1].born == ledger->ops[i].born &&
                ledger->ops[i - 1].sub < ledger->ops[i].sub));
    build_checkpoints(ledger);

    free(z.live);
    free(z.rows);
    ReplayEntityState_Free(state);

    ledger->stats.build_ms = now_ms() - t0;
    {
        struct ReplayLedgerStats* s = &ledger->stats;
        size_t keys = 0;

        s->frames = rec->frame_count;
        s->ticks = rec->tick_count;
        s->scenes = ledger->scene_count;
        s->zone_ops = ledger->op_count;
        s->zone_ops_live_max = ledger->live_max;
        s->zone_clears = ledger->clear_count;
        s->checkpoints = ledger->checkpoint_count;
        s->keys = ledger->key_count;
        for( int k = 0; k < ledger->key_count; k++ )
            keys += ledger_key_bytes(&ledger->keys[k]);
        s->bytes_per_key = ledger->key_count ? keys / (size_t)ledger->key_count : 0;
        s->table_bytes = sizeof(RsprotPlayerInfo);
        s->player_sticky_copies = ledger->player_op_count;
        for( int t = 0; t < rec->tick_count; t++ )
            s->world_entity_ticks += ledger->world_entity[t];
        s->total_bytes = sizeof(*ledger) + keys +
                         (size_t)ledger->op_count * sizeof(*ledger->ops) +
                         ((size_t)ledger->checkpoint_count + 1) * sizeof(*ledger->cp_first) +
                         (size_t)ledger->cp_first[ledger->checkpoint_count] * sizeof(uint32_t) +
                         (size_t)ledger->scene_count * sizeof(*ledger->scenes) +
                         (size_t)ledger->clear_count * sizeof(*ledger->clears) +
                         (size_t)ledger->player_op_count * sizeof(*ledger->player_ops) +
                         ledger->blob_used +
                         ((size_t)rec->tick_count + 1) * (2 * sizeof(uint32_t) + 1);
        for( int i = 0; i < ledger->scene_count; i++ )
            if( ledger->login_tables[i] )
                s->total_bytes += sizeof(RsprotPlayerInfo);
    }
    return ledger;
}

void
ReplayLedger_Free(struct ReplayLedger* ledger)
{
    if( !ledger )
        return;
    for( int k = 0; k < ledger->key_count; k++ )
        ledger_key_release(&ledger->keys[k]);
    for( int i = 0; i < ledger->scene_count; i++ )
        free(ledger->login_tables[i]);
    free(ledger->keys);
    free(ledger->login_tables);
    free(ledger->scenes);
    free(ledger->ops);
    free(ledger->cp_first);
    free(ledger->cp_ops);
    free(ledger->clears);
    free(ledger->player_ops);
    free(ledger->blobs);
    free(ledger->first_frame);
    free(ledger->local_tile);
    free(ledger->world_entity);
    free(ledger);
}

void
ReplayLedger_Stats(
    struct ReplayLedger const* ledger,
    struct ReplayLedgerStats* out)
{
    assert(ledger);
    assert(out);
    *out = ledger->stats;
}

int
ReplayLedger_TickCount(struct ReplayLedger const* ledger)
{
    assert(ledger);
    return ledger->tick_count;
}

uint32_t
ReplayLedger_FirstFrame(
    struct ReplayLedger const* ledger,
    int tick)
{
    assert(ledger);
    assert(tick >= 0);
    assert(tick <= ledger->tick_count);
    return ledger->first_frame[tick];
}

int
ReplayLedger_SceneCount(struct ReplayLedger const* ledger)
{
    assert(ledger);
    return ledger->scene_count;
}

struct ReplayLedgerScene const*
ReplayLedger_Scene(
    struct ReplayLedger const* ledger,
    int index)
{
    assert(ledger);
    assert(index >= 0);
    assert(index < ledger->scene_count);
    return &ledger->scenes[index];
}

int
ReplayLedger_SceneAt(
    struct ReplayLedger const* ledger,
    uint32_t frame)
{
    return ledger_scene_at(ledger, frame);
}

int
ReplayLedger_ZoneOpCount(struct ReplayLedger const* ledger)
{
    assert(ledger);
    return ledger->op_count;
}

struct ReplayZoneOp const*
ReplayLedger_ZoneOp(
    struct ReplayLedger const* ledger,
    int index)
{
    assert(ledger);
    assert(index >= 0);
    assert(index < ledger->op_count);
    return &ledger->ops[index];
}

uint32_t
ReplayLedger_LocalTile(
    struct ReplayLedger const* ledger,
    int tick)
{
    assert(ledger);
    assert(tick >= 0);
    assert(tick <= ledger->tick_count);
    return ledger->local_tile[tick];
}

int
ReplayLedger_InWorldEntitySpan(
    struct ReplayLedger const* ledger,
    int tick)
{
    assert(ledger);
    assert(tick >= 0);
    assert(tick <= ledger->tick_count);
    return ledger->world_entity[tick];
}

int
ReplayLedger_KeyCount(struct ReplayLedger const* ledger)
{
    assert(ledger);
    return ledger->key_count;
}

/* --- the keyframe ---------------------------------------------------------- */

uintptr_t
ledger_kf_put(
    struct ReplayWorldKeyframe* kf,
    void const* src,
    size_t len)
{
    size_t at;

    assert(kf);
    if( len == 0 )
        return 0;
    assert(src);
    /* Keep every block int-aligned: the body arrays are int32. */
    at = ((size_t)kf->bytes_used + 7u) & ~(size_t)7u;
    if( at + len > (size_t)kf->bytes_cap )
    {
        size_t cap = kf->bytes_cap ? (size_t)kf->bytes_cap : 4096;

        while( at + len > cap )
            cap *= 2;
        kf->bytes = realloc(kf->bytes, cap);
        assert(kf->bytes);
        kf->bytes_cap = (int)cap;
    }
    memcpy(kf->bytes + at, src, len);
    kf->bytes_used = (int)(at + len);
    return (uintptr_t)at + 1u;
}

static void const*
kf_ptr(
    struct ReplayWorldKeyframe const* kf,
    void const* off)
{
    uintptr_t v = (uintptr_t)off;

    return v ? kf->bytes + (v - 1u) : NULL;
}

void
ledger_kf_fixup(struct ReplayWorldKeyframe* kf)
{
    assert(kf);
    for( int i = 0; i < kf->op_count; i++ )
    {
        RsprotInfoOp* op = &kf->ops[i];

        if( op->kind == RSPROT_OP_APPEARANCE )
            op->appearance.appearance = kf_ptr(kf, op->appearance.appearance);
        else if( op->kind == RSPROT_OP_NAME_CHANGE )
            op->name_change.name = kf_ptr(kf, op->name_change.name);
        else if( op->kind == RSPROT_OP_BODY_CUSTOM )
        {
            op->body.models = kf_ptr(kf, op->body.models);
            op->body.recolours = kf_ptr(kf, op->body.recolours);
            op->body.retextures = kf_ptr(kf, op->body.retextures);
            op->body.identkits = kf_ptr(kf, op->body.identkits);
        }
    }
}

RsprotInfoOp*
ledger_kf_op(struct ReplayWorldKeyframe* kf)
{
    assert(kf);
    if( kf->op_count == kf->ops_cap )
    {
        kf->ops_cap = kf->ops_cap ? kf->ops_cap * 2 : 256;
        kf->ops = realloc(kf->ops, (size_t)kf->ops_cap * sizeof(*kf->ops));
        assert(kf->ops);
    }
    memset(&kf->ops[kf->op_count], 0, sizeof(kf->ops[0]));
    return &kf->ops[kf->op_count++];
}

int
ReplayLedger_WorldAt(
    struct ReplayLedger const* ledger,
    int tick,
    struct ReplayWorldKeyframe* out)
{
    struct ReplayEntityState* state;
    uint32_t* alive;
    int count;
    int key;
    int ok = 1;

    assert(ledger);
    assert(out);
    assert(tick >= 0);
    assert(tick <= ledger->tick_count);
    memset(out, 0, sizeof(*out));
    out->tick = tick;
    out->frame = ledger->first_frame[tick];
    out->world_entity = ledger->world_entity[tick];
    out->local_tile = ledger->local_tile[tick];
    out->scene = ledger_scene_at(ledger, out->frame);
    if( out->scene >= 0 )
        out->scene_row = ledger->scenes[out->scene];

    alive = malloc((size_t)(ledger->live_max > 0 ? ledger->live_max : 1) * sizeof(*alive));
    assert(alive);
    count = ReplayLedger_ZoneOpsAlive(ledger, out->frame, alive, ledger->live_max);
    out->zone_ops = calloc((size_t)(count > 0 ? count : 1), sizeof(*out->zone_ops));
    assert(out->zone_ops);
    for( int i = 0; i < count; i++ )
    {
        if( !ReplayLedger_ZoneOpDecode(ledger, (int)alive[i], out->scene_row.base_x,
                                       out->scene_row.base_z, &out->zone_ops[out->zone_op_count]) )
        {
            ok = 0;
            continue;
        }
        out->zone_op_count++;
    }
    free(alive);

    key = tick / REPLAY_LEDGER_KEY_TICKS;
    if( key >= ledger->key_count )
        key = ledger->key_count - 1;
    state = ReplayLedger_EntityNew(ledger);
    ReplayLedger_EntityLoadKey(ledger, key, state);
    ReplayLedger_EntityAdvance(ledger, state, tick);
    ok &= ledger_entity_keyframe(ledger, state, out);
    ReplayEntityState_Free(state);
    return ok;
}

void
ReplayWorldKeyframe_Free(struct ReplayWorldKeyframe* kf)
{
    if( !kf )
        return;
    free(kf->zone_ops);
    free(kf->player_table);
    free(kf->players);
    free(kf->npcs);
    free(kf->npc_headicons);
    free(kf->ops);
    free(kf->bytes);
    memset(kf, 0, sizeof(*kf));
}
