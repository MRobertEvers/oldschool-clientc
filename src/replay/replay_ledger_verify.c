/*
 * The ledger's equalities. See replay_ledger_verify.h.
 *
 * The zone model here is written from the client's side, not the ledger's: it
 * keeps the STATE the executor would (a loc per tile and layer, ground-item
 * stacks per tile in arrival order) and applies packets to it, where the ledger
 * reasons about which packets outlive which. The two agree only if every
 * interval rule matches what the state does.
 */
#include "replay/replay_ledger_verify.h"
#include "replay/replay_ledger_internal.h"

#include "net/rev/pktnames.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_CUSTOM_SLOTS 32

struct ModelLoc
{
    uint32_t tile;
    uint32_t layer;
    int loc_id;
    int shape;
    int angle;
    int op_flags;
};

struct ModelRow
{
    uint32_t tile;
    int obj_id;
    int count;
    int own[4];
    int enabled_ops;
    int has_enabled_ops;
    int customised;
    int model;
    int tint;
    uint32_t recol_mask;
    uint32_t retex_mask;
    int recol[MODEL_CUSTOM_SLOTS];
    int retex[MODEL_CUSTOM_SLOTS];
};

struct Model
{
    struct ModelLoc* locs;
    int loc_count;
    int loc_cap;
    struct ModelRow* rows;
    int row_count;
    int row_cap;
};

static void
model_free(struct Model* m)
{
    free(m->locs);
    free(m->rows);
    memset(m, 0, sizeof(*m));
}

static struct ModelRow*
model_first(
    struct Model* m,
    uint32_t tile,
    int obj_id)
{
    for( int i = 0; i < m->row_count; i++ )
        if( m->rows[i].tile == tile && m->rows[i].obj_id == obj_id )
            return &m->rows[i];
    return NULL;
}

static struct ModelRow*
model_exact(
    struct Model* m,
    uint32_t tile,
    int obj_id,
    int count)
{
    for( int i = 0; i < m->row_count; i++ )
        if( m->rows[i].tile == tile && m->rows[i].obj_id == obj_id && m->rows[i].count == count )
            return &m->rows[i];
    return NULL;
}

static void
model_row_add(
    struct Model* m,
    uint32_t tile,
    int obj_id,
    int count)
{
    struct ModelRow* row;

    if( m->row_count == m->row_cap )
    {
        m->row_cap = m->row_cap ? m->row_cap * 2 : 64;
        m->rows = realloc(m->rows, (size_t)m->row_cap * sizeof(*m->rows));
        assert(m->rows);
    }
    row = &m->rows[m->row_count++];
    memset(row, 0, sizeof(*row));
    row->tile = tile;
    row->obj_id = obj_id;
    row->count = count;
    row->model = -1;
    row->tint = -1;
}

static void
model_row_remove(
    struct Model* m,
    struct ModelRow* row)
{
    int at = (int)(row - m->rows);

    memmove(&m->rows[at], &m->rows[at + 1], (size_t)(m->row_count - at - 1) * sizeof(*row));
    m->row_count--;
}

static int
layer_of(int shape)
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

static void
model_loc(
    struct Model* m,
    uint32_t tile,
    int loc_id,
    int info,
    int op_flags)
{
    uint32_t layer = (uint32_t)layer_of(info >> 2);
    struct ModelLoc* loc = NULL;

    for( int i = 0; i < m->loc_count && !loc; i++ )
        if( m->locs[i].tile == tile && m->locs[i].layer == layer )
            loc = &m->locs[i];
    if( !loc )
    {
        if( m->loc_count == m->loc_cap )
        {
            m->loc_cap = m->loc_cap ? m->loc_cap * 2 : 64;
            m->locs = realloc(m->locs, (size_t)m->loc_cap * sizeof(*m->locs));
            assert(m->locs);
        }
        loc = &m->locs[m->loc_count++];
    }
    loc->tile = tile;
    loc->layer = layer;
    loc->loc_id = loc_id;
    loc->shape = info >> 2;
    loc->angle = info & 3;
    loc->op_flags = op_flags;
}

/* The executor's zone arms, on the model (rs_gameproto_exec.c:1144-:1395 and
 * the App stack functions they call). `accepted_reveal`: whether an
 * OBJ_REVEAL's receiver is this client. */
static void
model_apply(
    struct Model* m,
    uint32_t tile,
    struct PktZoneSubPacket const* sp,
    int accepted_reveal)
{
    struct ModelRow* row;

    switch( sp->name )
    {
    case PKT_NAME_LOC_ADD_CHANGE:
        model_loc(m, tile, sp->_loc_add_change.loc_id, sp->_loc_add_change.info,
                  sp->_loc_add_change.op_flags);
        break;
    case PKT_NAME_LOC_DEL:
        model_loc(m, tile, -1, sp->_loc_del.info, 0);
        break;
    case PKT_NAME_OBJ_ADD:
        if( sp->_obj_add.count > 0 )
        {
            model_row_add(m, tile, sp->_obj_add.obj_id, sp->_obj_add.count);
            row = &m->rows[m->row_count - 1];
        }
        else
            row = model_first(m, tile, sp->_obj_add.obj_id);
        if( row )
        {
            row->own[0] = sp->_obj_add.time_until_public;
            row->own[1] = sp->_obj_add.time_until_despawn;
            row->own[2] = sp->_obj_add.ownership_type;
            row->own[3] = sp->_obj_add.never_becomes_public;
        }
        break;
    case PKT_NAME_OBJ_REVEAL:
        if( sp->_obj_reveal.has_enabled_ops )
        {
            row = model_first(m, tile, sp->_obj_reveal.obj_id);
            if( row )
            {
                row->enabled_ops = sp->_obj_reveal.enabled_ops;
                row->has_enabled_ops = 1;
            }
        }
        else if( accepted_reveal && sp->_obj_reveal.count > 0 )
            model_row_add(m, tile, sp->_obj_reveal.obj_id, sp->_obj_reveal.count);
        break;
    case PKT_NAME_OBJ_DEL:
        row = model_first(m, tile, sp->_obj_del.obj_id);
        if( row )
            model_row_remove(m, row);
        break;
    case PKT_NAME_OBJ_COUNT:
        row = model_first(m, tile, sp->_obj_count.obj_id);
        if( row )
            row->count = sp->_obj_count.new_count;
        break;
    case PKT_NAME_OBJ_CUSTOMISE:
    {
        struct PktObjCustomiseFields const* c = &sp->_obj_customise.fields;

        row = model_exact(m, tile, sp->_obj_customise.obj_id, sp->_obj_customise.count);
        if( !row )
            break;
        row->customised = 1;
        if( c->model > -1 )
            row->model = c->model;
        row->tint = c->tint;
        for( int i = 0; i < c->recol_count && i < PKT_OBJ_CUSTOMISE_MAX_PAIRS; i++ )
            if( c->recol_index[i] >= 0 && c->recol_index[i] < MODEL_CUSTOM_SLOTS )
            {
                row->recol[c->recol_index[i]] = c->recol_value[i];
                row->recol_mask |= 1u << c->recol_index[i];
            }
        for( int i = 0; i < c->retex_count && i < PKT_OBJ_CUSTOMISE_MAX_PAIRS; i++ )
            if( c->retex_index[i] >= 0 && c->retex_index[i] < MODEL_CUSTOM_SLOTS )
            {
                row->retex[c->retex_index[i]] = c->retex_value[i];
                row->retex_mask |= 1u << c->retex_index[i];
            }
        break;
    }
    case PKT_NAME_OBJ_UNCUSTOMISE:
        row = model_exact(m, tile, sp->_obj_uncustomise.obj_id, sp->_obj_uncustomise.count);
        if( !row )
            break;
        row->customised = 0;
        row->model = -1;
        row->tint = -1;
        row->recol_mask = 0;
        row->retex_mask = 0;
        memset(row->recol, 0, sizeof(row->recol));
        memset(row->retex, 0, sizeof(row->retex));
        break;
    default:
        break;
    }
}

/* Drop everything `keep` says no to. */
static void
model_filter(
    struct Model* m,
    int (*keep)(uint32_t tile, void const* arg),
    void const* arg)
{
    int w = 0;

    for( int i = 0; i < m->loc_count; i++ )
        if( keep(m->locs[i].tile, arg) )
            m->locs[w++] = m->locs[i];
    m->loc_count = w;
    w = 0;
    for( int i = 0; i < m->row_count; i++ )
        if( keep(m->rows[i].tile, arg) )
            m->rows[w++] = m->rows[i];
    m->row_count = w;
}

static int
keep_outside_zone(
    uint32_t tile,
    void const* arg)
{
    uint32_t zone = *(uint32_t const*)arg;

    return !((REPLAY_TILE_X(tile) >> 3) == (REPLAY_TILE_X(zone) >> 3) &&
             (REPLAY_TILE_Z(tile) >> 3) == (REPLAY_TILE_Z(zone) >> 3) &&
             REPLAY_TILE_LEVEL(tile) == REPLAY_TILE_LEVEL(zone));
}

static int
keep_inside_scene(
    uint32_t tile,
    void const* arg)
{
    struct ReplayLedgerScene const* s = arg;
    int x = REPLAY_TILE_X(tile);
    int z = REPLAY_TILE_Z(tile);

    return x >= s->base_x && z >= s->base_z && x < s->base_x + s->size && z < s->base_z + s->size;
}

static int
cmp_loc(
    void const* pa,
    void const* pb)
{
    struct ModelLoc const* a = pa;
    struct ModelLoc const* b = pb;

    if( a->tile != b->tile )
        return a->tile < b->tile ? -1 : 1;
    if( a->layer != b->layer )
        return a->layer < b->layer ? -1 : 1;
    return 0;
}

static int
model_equal(
    struct Model* a,
    struct Model* b,
    char* why,
    int why_cap)
{
    qsort(a->locs, (size_t)a->loc_count, sizeof(*a->locs), cmp_loc);
    qsort(b->locs, (size_t)b->loc_count, sizeof(*b->locs), cmp_loc);
    if( a->loc_count != b->loc_count )
    {
        snprintf(why, (size_t)why_cap, "%d loc changes from the history, %d from the live ops",
                 a->loc_count, b->loc_count);
        return 0;
    }
    for( int i = 0; i < a->loc_count; i++ )
        if( memcmp(&a->locs[i], &b->locs[i], sizeof(a->locs[i])) != 0 )
        {
            snprintf(why, (size_t)why_cap, "loc at %d,%d,l%d layer %u: %d vs %d",
                     REPLAY_TILE_X(a->locs[i].tile), REPLAY_TILE_Z(a->locs[i].tile),
                     REPLAY_TILE_LEVEL(a->locs[i].tile), a->locs[i].layer, a->locs[i].loc_id,
                     b->locs[i].loc_id);
            return 0;
        }
    if( a->row_count != b->row_count )
    {
        snprintf(why, (size_t)why_cap, "%d ground stacks from the history, %d from the live ops",
                 a->row_count, b->row_count);
        return 0;
    }
    for( int i = 0; i < a->row_count; i++ )
        if( memcmp(&a->rows[i], &b->rows[i], sizeof(a->rows[i])) != 0 )
        {
            snprintf(why, (size_t)why_cap, "stack %d (obj %d at %d,%d): count %d vs %d", i,
                     a->rows[i].obj_id, REPLAY_TILE_X(a->rows[i].tile),
                     REPLAY_TILE_Z(a->rows[i].tile), a->rows[i].count, b->rows[i].count);
            return 0;
        }
    return 1;
}

/* --- intervals -------------------------------------------------------------- */

static int
is_clear_of(
    struct ReplayLedger const* ledger,
    uint32_t f,
    uint32_t tile)
{
    for( int i = 0; i < ledger->clear_count; i++ )
        if( ledger->clears[i].frame == f && !keep_outside_zone(tile, &ledger->clears[i].tile) )
            return 1;
    return 0;
}

static int
is_scene_frame(
    struct ReplayLedger const* ledger,
    uint32_t f)
{
    int s = ledger_scene_at(ledger, f + 1);

    return s >= 0 && ledger->scenes[s].frame == f;
}

/* An op born at `f` about the same tile and key as `op`. */
static int
has_successor(
    struct ReplayLedger const* ledger,
    struct ReplayZoneOp const* op,
    uint32_t f)
{
    int lo = 0;
    int hi = ledger->op_count;

    while( lo < hi )
    {
        int mid = lo + (hi - lo) / 2;

        if( ledger->ops[mid].born < f )
            lo = mid + 1;
        else
            hi = mid;
    }
    for( int i = lo; i < ledger->op_count && ledger->ops[i].born == f; i++ )
        if( &ledger->ops[i] != op && ledger->ops[i].tile == op->tile &&
            ledger->ops[i].key == op->key )
            return 1;
    return 0;
}

int
ReplayLedgerVerify_Intervals(
    struct ReplayLedger const* ledger,
    char* why,
    int why_cap)
{
    assert(ledger);
    assert(why);
    assert(why_cap > 0);
    why[0] = '\0';
    for( int i = 0; i < ledger->op_count; i++ )
    {
        struct ReplayZoneOp const* op = &ledger->ops[i];

        if( i > 0 && !(ledger->ops[i - 1].born < op->born ||
                       (ledger->ops[i - 1].born == op->born && ledger->ops[i - 1].sub < op->sub)) )
        {
            snprintf(why, (size_t)why_cap, "op %d is out of birth order", i);
            return 0;
        }
        if( op->born >= (uint32_t)ledger->rec->frame_count )
        {
            snprintf(why, (size_t)why_cap, "op %d born past the last frame", i);
            return 0;
        }
        if( op->died == REPLAY_LEDGER_NEVER || op->died == op->born )
            continue;
        if( op->died < op->born || op->died >= (uint32_t)ledger->rec->frame_count )
        {
            snprintf(why, (size_t)why_cap, "op %d dies at %u, born %u", i, op->died, op->born);
            return 0;
        }
        if( !is_scene_frame(ledger, op->died) && !is_clear_of(ledger, op->died, op->tile) &&
            !has_successor(ledger, op, op->died) )
        {
            snprintf(why, (size_t)why_cap,
                     "op %d (name %d at %d,%d) dies at frame %u with nothing there to end it", i,
                     op->name, REPLAY_TILE_X(op->tile), REPLAY_TILE_Z(op->tile), op->died);
            return 0;
        }
    }
    return 1;
}

/* --- zones -------------------------------------------------------------------- */

/* The local index at each frame, as the executor knows it for OBJ_REVEAL. */
static int
local_pid_step(
    struct ReplayLedger const* ledger,
    uint32_t f,
    int pid)
{
    struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];

    if( fr->opcode == RSPROX_RECORDING_RECONNECT )
        return fr->reconnect_index;
    if( ledger_frame_name(ledger, f) == PKT_NAME_UPDATE_PID )
    {
        struct RevPacket pkt;

        if( ledger_parse(ledger, f, &pkt) )
        {
            pid = pkt._update_pid.local_player_index;
            gameproto_free(&pkt);
        }
    }
    return pid;
}

int
ReplayLedgerVerify_Zones(
    struct ReplayLedger const* ledger,
    uint32_t const* frames,
    int count,
    char* why,
    int why_cap)
{
    struct Model history;
    uint32_t* alive;
    uint32_t* brute;
    int next_op = 0;
    int next_clear = 0;
    int next_scene = 0;
    uint32_t f = 0;
    int pid;
    int ok = 1;

    assert(ledger);
    assert(frames || count == 0);
    assert(why);
    assert(why_cap > 0);
    why[0] = '\0';
    memset(&history, 0, sizeof(history));
    pid = ledger->rec->local_player_index;
    alive = malloc((size_t)(ledger->live_max + 1) * sizeof(*alive));
    brute = malloc((size_t)(ledger->op_count + 1) * sizeof(*brute));
    assert(alive);
    assert(brute);

    for( int s = 0; s < count && ok; s++ )
    {
        uint32_t target = frames[s];
        int n;
        int bn = 0;
        struct Model live;

        assert(s == 0 || frames[s - 1] <= target);
        assert(target <= (uint32_t)ledger->rec->frame_count);
        /* The history: every event of frames [f, target), in order. */
        for( ; f < target; f++ )
        {
            int scene_frame = next_scene < ledger->scene_count && ledger->scenes[next_scene].frame == f;

            if( scene_frame )
            {
                /* A rebuild with a scene already up drops what left it. */
                if( next_scene > 0 )
                    model_filter(&history, keep_inside_scene, &ledger->scenes[next_scene]);
                next_scene++;
            }
            while( next_clear < ledger->clear_count && ledger->clears[next_clear].frame == f )
            {
                model_filter(&history, keep_outside_zone, &ledger->clears[next_clear].tile);
                next_clear++;
            }
            while( next_op < ledger->op_count && ledger->ops[next_op].born == f )
            {
                struct ReplayWorldZoneOp d;
                struct ReplayZoneOp const* op = &ledger->ops[next_op];

                if( !ReplayLedger_ZoneOpDecode(ledger, next_op, 0, 0, &d) )
                {
                    snprintf(why, (size_t)why_cap, "op %d no longer decodes", next_op);
                    ok = 0;
                    break;
                }
                model_apply(&history, op->tile, &d.pkt,
                            pid < 0 || d.pkt._obj_reveal.receiver == pid);
                next_op++;
            }
            pid = local_pid_step(ledger, f, pid);
            if( !ok )
                break;
        }
        if( !ok )
            break;

        /* The stabbing query against a scan of every interval. */
        n = ReplayLedger_ZoneOpsAlive(ledger, target, alive, ledger->live_max + 1);
        for( int i = 0; i < ledger->op_count; i++ )
            if( ledger->ops[i].born < target && ledger->ops[i].died >= target )
                brute[bn++] = (uint32_t)i;
        if( n != bn || (n > 0 && memcmp(alive, brute, (size_t)n * sizeof(*alive)) != 0) )
        {
            snprintf(why, (size_t)why_cap,
                     "frame %u: the query returns %d ops, a scan of the intervals %d", target, n,
                     bn);
            ok = 0;
            break;
        }
        if( n > ledger->live_max )
        {
            snprintf(why, (size_t)why_cap, "frame %u: %d alive, max says %d", target, n,
                     ledger->live_max);
            ok = 0;
            break;
        }
        /* One loc op per key. */
        for( int i = 0; i < n && ok; i++ )
            for( int j = i + 1; j < n; j++ )
            {
                struct ReplayZoneOp const* a = &ledger->ops[alive[i]];
                struct ReplayZoneOp const* b = &ledger->ops[alive[j]];
                int loc_a = a->name == PKT_NAME_LOC_ADD_CHANGE || a->name == PKT_NAME_LOC_DEL;
                int loc_b = b->name == PKT_NAME_LOC_ADD_CHANGE || b->name == PKT_NAME_LOC_DEL;

                if( loc_a && loc_b && a->tile == b->tile && a->key == b->key )
                {
                    snprintf(why, (size_t)why_cap, "frame %u: two loc ops alive on one key",
                             target);
                    ok = 0;
                    break;
                }
            }
        if( !ok )
            break;

        /* The world from the live ops alone. */
        memset(&live, 0, sizeof(live));
        for( int i = 0; i < n; i++ )
        {
            struct ReplayWorldZoneOp d;

            if( !ReplayLedger_ZoneOpDecode(ledger, (int)alive[i], 0, 0, &d) )
            {
                snprintf(why, (size_t)why_cap, "op %u no longer decodes", alive[i]);
                ok = 0;
                break;
            }
            /* Alive means it was accepted when it came. */
            model_apply(&live, ledger->ops[alive[i]].tile, &d.pkt, 1);
        }
        if( ok && !model_equal(&history, &live, why, why_cap) )
        {
            char detail[256];

            snprintf(detail, sizeof(detail), "frame %u: %s", target, why);
            snprintf(why, (size_t)why_cap, "%s", detail);
            ok = 0;
        }
        model_free(&live);
    }
    model_free(&history);
    free(alive);
    free(brute);
    return ok;
}

/* --- entities -------------------------------------------------------------- */

int
ReplayLedgerVerify_Entities(
    struct ReplayLedger const* ledger,
    int const* ticks,
    int count,
    char* why,
    int why_cap)
{
    struct ReplayEntityState* fold;
    struct ReplayEntityState* keyed;
    int ok = 1;

    assert(ledger);
    assert(ticks || count == 0);
    assert(why);
    assert(why_cap > 0);
    why[0] = '\0';
    fold = ReplayLedger_EntityNew(ledger);
    keyed = ReplayLedger_EntityNew(ledger);
    for( int s = 0; s < count && ok; s++ )
    {
        int tick = ticks[s];
        int key = tick / REPLAY_LEDGER_KEY_TICKS;
        char detail[256];

        assert(s == 0 || ticks[s - 1] <= tick);
        if( key >= ledger->key_count )
            key = ledger->key_count - 1;
        ReplayLedger_EntityAdvance(ledger, fold, tick);
        ReplayLedger_EntityLoadKey(ledger, key, keyed);
        ReplayLedger_EntityAdvance(ledger, keyed, tick);
        if( !ReplayEntityState_Equal(fold, keyed, detail, (int)sizeof(detail)) )
        {
            snprintf(why, (size_t)why_cap, "tick %d (key %d): %s", tick, key, detail);
            ok = 0;
        }
    }
    ReplayEntityState_Free(fold);
    ReplayEntityState_Free(keyed);
    return ok;
}
