/*
 * quest-driver: the drive event ring.
 *
 * One fixed array on struct App, appended by App_DriveEvent from the stamp
 * sites marked `DRIVE_STAMP:` across the tree, read by cursor by the driver's
 * scheduler. See src/plugin/torirs_plugin_drive.h for the payload table and
 * docs/ARCHITECT.md for who owns what.
 *
 * Present in EVERY build, not just a content-test one. A stamp is a bounds
 * check and a struct store; making it conditional on a test being live would
 * buy back nothing measurable and would guarantee that the stamps rot -- the
 * failure mode this whole mechanism exists to avoid is an await that never
 * fires because the event stopped being raised and nothing said so.
 *
 * Owner: core-events. The ring below is written; the stamp sites and the
 * layout-fence drain are not.
 */

#include "app.h"

#include "plugin/torirs_plugin_drive.h"
#include "world/world.h"

#include <assert.h>
#include <string.h>

static char const* const DRIVE_RESULT_NAMES[DRIVE_RESULT_COUNT] = {
    "ok",
    "timeout",
    "not_found",
    "refused",
    "covered",
    "no_row",
    "not_visible",
    "closed",
    "unsupported",
};

char const*
DriveResultName(enum DriveResult result)
{
    assert(result >= 0);
    assert(result < DRIVE_RESULT_COUNT);
    return DRIVE_RESULT_NAMES[result];
}

struct RS_EventSink
App_DriveEventSink(struct App* app)
{
    assert(app);
    /* A full ring overwrites its oldest entry and counts it rather than
     * dropping it silently: a reader whose cursor is now older than
     * oldest_serial is told DRIVE_REFUSED and can say "the pump stopped",
     * which is a diagnosable failure. Skipping the event and carrying on is
     * the one thing that is not. */
    return (struct RS_EventSink){
        .ring = &app->drive_events.ring,
        .entries = app->drive_events.entries,
        .capacity = APP_DRIVE_RING_CAPACITY,
        .full = RS_EVENT_RING_COUNT_DROPS,
        .cycle = app->world ? &app->world->cycle : NULL,
    };
}

void
App_DriveEvent(
    struct App* app,
    enum RS_EventKind kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    struct RS_EventSink sink;

    assert(app);
    assert(kind > DRIVE_EVENT_NONE);
    assert(kind < DRIVE_EVENT_KIND_COUNT);
    sink = App_DriveEventSink(app);
    RS_EventSink_Stamp(&sink, (int)kind, a, b, c, d);
}

#ifndef NDEBUG
/* Is effect-log entry `ev` (with `next`, the entry after it, or NULL) still
 * alive at world cycle `now`? By the lifetimes of game/rs_effect_rules.h, on
 * the world cycle (the reader's own clock is the server tick; two ticks of
 * slack cover the drift between them). */
static int
app_drive_effect_alive(
    struct RS_Event const* ev,
    struct RS_Event const* next,
    int now)
{
    int const slack = 2 * APP_SERVER_TICK_LOGIC_CYCLES;

    if( ev->kind == RS_EVENT_MAP_ANIM )
        return now - ev->cycle <= RS_EFFECT_MAP_ANIM_LIFE_CYCLES + slack;
    if( ev->kind == RS_EVENT_PROJECTILE )
        return next && next->kind == RS_EVENT_PROJECTILE_FLIGHT && app_drive_effect_alive(next, NULL, now);
    assert(ev->kind == RS_EVENT_PROJECTILE_FLIGHT);
    return now - ev->cycle <= (int)((uint32_t)ev->c >> 16) + slack;
}
#endif

/* Append to the effect log. The log is sized past every list's cap together,
 * so the entry a full log overwrites is dead: one still alive means a list is
 * over its cap, a contract violation (rs_effect_rules.h). */
static void
app_drive_effect_push(
    struct App* app,
    int kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    struct App_DriveRing* ring;
    struct RS_Event ev;
    int now;

    assert(app);
    ring = &app->drive_events;
    now = app->world ? (int)app->world->cycle : 0;
#ifndef NDEBUG
    if( ring->effects.count == APP_DRIVE_EFFECT_LOG_CAPACITY )
    {
        uint32_t const oldest = ring->effects.oldest_serial;
        struct RS_Event const* old =
            RS_EventRing_At(&ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, oldest);
        struct RS_Event const* after =
            RS_EventRing_At(&ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, oldest + 1);
        assert(!app_drive_effect_alive(old, after, now));
    }
#endif
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.cycle = now;
    ev.a = a;
    ev.b = b;
    ev.c = c;
    ev.d = d;
    (void)RS_EventRing_Push(
        &ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, RS_EVENT_RING_OVERWRITE, &ev);
}

int
App_DriveEffectProjectiles(
    struct App const* app,
    struct App_DriveProjectile* out,
    int cap)
{
    struct App_DriveRing const* ring;
    int n = 0;

    assert(app);
    assert(out);
    assert(cap >= 0);
    ring = &app->drive_events;
    if( ring->effects.count == 0 )
        return 0;
    for( uint32_t s = ring->effects.oldest_serial; s < ring->effects.newest_serial && n < cap; s++ )
    {
        struct RS_Event const* p =
            RS_EventRing_At(&ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, s);
        struct RS_Event const* f;

        if( p->kind != RS_EVENT_PROJECTILE )
            continue;
        f = RS_EventRing_At(&ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, s + 1);
        assert(f->kind == RS_EVENT_PROJECTILE_FLIGHT);
        out[n].src_x = p->a;
        out[n].src_z = p->b;
        out[n].dst_x = p->c;
        out[n].dst_z = p->d;
        out[n].spotanim = f->a;
        out[n].target = f->b;
        out[n].start_delay = (int32_t)((uint32_t)f->c & 0xffffu);
        out[n].end_delay = (int32_t)((uint32_t)f->c >> 16);
        out[n].level = f->d;
        out[n].cycle = p->cycle;
        n++;
    }
    return n;
}

int
App_DriveEffectMapAnims(
    struct App const* app,
    struct App_DriveMapAnim* out,
    int cap)
{
    struct App_DriveRing const* ring;
    int n = 0;

    assert(app);
    assert(out);
    assert(cap >= 0);
    ring = &app->drive_events;
    if( ring->effects.count == 0 )
        return 0;
    for( uint32_t s = ring->effects.oldest_serial; s <= ring->effects.newest_serial && n < cap; s++ )
    {
        struct RS_Event const* e =
            RS_EventRing_At(&ring->effects, ring->effect_entries, APP_DRIVE_EFFECT_LOG_CAPACITY, s);

        if( e->kind != RS_EVENT_MAP_ANIM )
            continue;
        out[n].x = e->a;
        out[n].z = e->b;
        out[n].level = e->c;
        out[n].spotanim = e->d;
        out[n].cycle = e->cycle;
        n++;
    }
    return n;
}

void
App_DriveProjectileNote(
    struct App* app, int src_x, int src_z, int dst_x, int dst_z, int level, int spotanim,
    int target, int start_delay, int end_delay)
{
    struct App_DriveRing* ring;
    struct App_DriveProjectile* p;
    int now, kept, i;

    assert(app);
    ring = &app->drive_events;
    now = app->world ? (int)app->world->cycle : 0;
    /* The ones a client would still draw: scriptrun_core.c's rule. */
    kept = 0;
    for( i = 0; i < ring->projectile_count; i++ )
        if( ring->projectiles[i].cycle + ring->projectiles[i].end_delay >= now )
            ring->projectiles[kept++] = ring->projectiles[i];
    ring->projectile_count = kept;
    /* The ring holds every projectile a client would still draw; more than
     * its capacity in flight at once is a contract violation, not a drop. */
    assert(ring->projectile_count < APP_DRIVE_PROJECTILE_CAP);
    p = &ring->projectiles[ring->projectile_count++];
    p->src_x = src_x;
    p->src_z = src_z;
    p->dst_x = dst_x;
    p->dst_z = dst_z;
    p->level = level;
    p->spotanim = spotanim;
    p->target = target;
    p->start_delay = start_delay;
    p->end_delay = end_delay;
    p->cycle = now;

    /* The effect log: the pair, back to back. */
    assert(start_delay >= 0 && start_delay <= 0xffff);
    assert(end_delay >= 0 && end_delay <= 0xffff);
    app_drive_effect_push(app, RS_EVENT_PROJECTILE, src_x, src_z, dst_x, dst_z);
    app_drive_effect_push(
        app,
        RS_EVENT_PROJECTILE_FLIGHT,
        spotanim,
        target,
        (int32_t)((uint32_t)start_delay | ((uint32_t)end_delay << 16)),
        level);
}

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER
int
App_DriveMapAnimElapsed(struct App_DriveMapAnim const* a)
{
    /*
     * Cycles since the apply, as scriptrun counts them (d_spotanims: its
     * core's cycle moves exactly 30 a tick): whole ticks from the tick that
     * SENT it to the tick perceived now, so a row read on the tick its packet
     * is perceived has elapsed 30 on both lanes -- DriveUi_Projectiles' rule.
     * The client's own cycle is not that clock: a watched client runs on the
     * wall clock, two reads a tick apart were 29 or 31 cycles apart, and a
     * solver that keyed a new graphic on cycles_left took an old crystal
     * shadow for a new one every few ticks (Olm, coxolmso17, 2026-10-13).
     */
    assert(a);
    return (PluginDrive_ServerTick() - PluginDrive_ServerTickOfCycle(a->cycle)) * APP_SERVER_TICK_LOGIC_CYCLES;
}

void
App_DriveMapAnimNote(struct App* app, int x, int z, int level, int spotanim)
{
    struct App_DriveRing* ring;
    struct App_DriveMapAnim* a;
    int now, kept, i;

    assert(app);
    ring = &app->drive_events;
    now = app->world ? (int)app->world->cycle : 0;
    /* The ones scriptrun still lists: scriptrun_core.c's retire rule. */
    kept = 0;
    for( i = 0; i < ring->map_anim_count; i++ )
        if( App_DriveMapAnimElapsed(&ring->map_anims[i]) <= APP_DRIVE_MAP_ANIM_LIFE )
            ring->map_anims[kept++] = ring->map_anims[i];
    ring->map_anim_count = kept;
    /* Scriptrun holds the same count (SCRIPTRUN_MAP_ANIMS) and asserts it. */
    assert(ring->map_anim_count < APP_DRIVE_MAP_ANIM_CAP);
    a = &ring->map_anims[ring->map_anim_count++];
    a->x = x;
    a->z = z;
    a->level = level;
    a->spotanim = spotanim;
    a->cycle = now;
    app_drive_effect_push(app, RS_EVENT_MAP_ANIM, x, z, level, spotanim);
}
#else /* !TORIRS_EMBED_SERVER */
/*
 * The map-anim ring has one reader, the quest driver (DriveUi_*, embedded
 * server only), and its age is counted in server ticks the driver tracks
 * (PluginDrive_ServerTick*). A client without the embedded server has
 * neither: the packet goes to the effect log alone.
 */
void
App_DriveMapAnimNote(struct App* app, int x, int z, int level, int spotanim)
{
    assert(app);
    app_drive_effect_push(app, RS_EVENT_MAP_ANIM, x, z, level, spotanim);
}
#endif /* TORIRS_EMBED_SERVER */

void
App_DriveLocAnimNote(struct App* app, int scene_x, int scene_z, int level, int shape, int seq)
{
    struct App_DriveRing* ring;
    struct App_DriveLocAnim* a;
    int now, kept, i;

    assert(app);
    assert(app->world);
    ring = &app->drive_events;
    now = (int)app->world->cycle;
    /* Retire by cycles with two ticks of slack (RS_EFFECT_LOC_ANIM_CLIENT_KEEP_TICKS):
     * the rows themselves are judged by the server tick (DriveUi_LocAnims),
     * scriptrun's rule. */
    kept = 0;
    for( i = 0; i < ring->loc_anim_count; i++ )
        if( now - ring->loc_anims[i].cycle <= RS_EFFECT_LOC_ANIM_CLIENT_KEEP_TICKS * APP_SERVER_TICK_LOGIC_CYCLES )
            ring->loc_anims[kept++] = ring->loc_anims[i];
    ring->loc_anim_count = kept;
    /* Scriptrun holds the same count (SCRIPTRUN_LOC_ANIMS) and asserts it. */
    assert(ring->loc_anim_count < APP_DRIVE_LOC_ANIM_CAP);
    a = &ring->loc_anims[ring->loc_anim_count++];
    a->x = app->world->_base_tile_x + scene_x;
    a->z = app->world->_base_tile_z + scene_z;
    a->level = level;
    a->shape = shape;
    a->seq = seq;
    a->cycle = now;
}

/* Drop the loc changes the scenery now shows (the task landed: the new loc in
 * that tile and layer, or none there for a delete) and those from another
 * scene base (a rebuild dropped their task and the server re-sends the zone).
 * Never one the loc lane has not released yet (lane_ticket): an add and the
 * delete behind it on one tile and layer would otherwise retire on the empty
 * layer the add has yet to fill. */
void
App_DriveLocChangesRetire(struct App* app)
{
    struct App_DriveRing* ring;
    struct World_EntityPool* pool;
    int base_x, base_z, kept;

    assert(app);
    assert(app->world);
    ring = &app->drive_events;
    pool = &app->world->entities.scenery;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    kept = 0;

    for( int k = 0; k < ring->loc_change_count; k++ )
    {
        struct App_DriveLocChange const* c = &ring->loc_changes[k];
        int shown = 0;

        if( c->base_x != base_x || c->base_z != base_z )
            continue;
        if( LocLane_Pending(&app->loc_lane, c->lane_ticket) )
        {
            ring->loc_changes[kept++] = *c;
            continue;
        }
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery const* sc = World_EntityPoolGet(pool, i);
            if( !sc || base_x + sc->grid_position.x != c->x || base_z + sc->grid_position.z != c->z ||
                sc->grid_position.level != c->level || World_LocShapeToLayer(sc->shape) != c->layer )
                continue;
            shown = sc->loc_id == c->loc_id ? 1 : -1;
            if( shown > 0 )
                break;
        }
        /* An add is shown by its loc; a delete by nothing in the layer. */
        if( (c->loc_id >= 0 && shown > 0) || (c->loc_id < 0 && shown == 0) )
            continue;
        ring->loc_changes[kept++] = *c;
    }
    if( kept != ring->loc_change_count )
        ring->loc_change_serial++;
    ring->loc_change_count = kept;
}

void
App_DriveLocChangeNote(
    struct App* app, int scene_x, int scene_z, int level, int loc_id, int shape, int angle)
{
    struct App_DriveRing* ring;
    struct App_DriveLocChange* c = NULL;
    int base_x, base_z, x, z, layer, i;

    assert(app);
    assert(app->world);
    ring = &app->drive_events;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    x = base_x + scene_x;
    z = base_z + scene_z;
    layer = World_LocShapeToLayer(shape);
    for( i = 0; i < ring->loc_change_count; i++ )
    {
        struct App_DriveLocChange* e = &ring->loc_changes[i];
        if( e->x == x && e->z == z && e->level == level && e->layer == layer )
        {
            c = e;
            break;
        }
    }
    if( !c )
    {
        /* Only changes the scenery has not caught up with are held. The
         * drive's reads retire the rest, but a client nobody drives never
         * reads, and the ring grew without end: 6,436 in a Zebak fight, every
         * one past the 256th written beyond it (UBSan, 2026-10-19). So a full
         * ring retires here too; more than this many still waiting on their
         * models at once is a contract violation, not a drop. */
        if( ring->loc_change_count >= APP_DRIVE_LOC_CHANGE_CAP )
            App_DriveLocChangesRetire(app);
        assert(ring->loc_change_count < APP_DRIVE_LOC_CHANGE_CAP);
        c = &ring->loc_changes[ring->loc_change_count++];
    }
    c->x = x;
    c->z = z;
    c->level = level;
    c->layer = layer;
    c->loc_id = loc_id;
    c->shape = shape;
    c->angle = angle;
    c->base_x = base_x;
    c->base_z = base_z;
    /* Every note follows the App_WorldLocChange that queued its change, so
     * the newest ticket is that change's. */
    c->lane_ticket = LocLane_Newest(&app->loc_lane);
    ring->loc_change_serial++;
}

enum DriveResult
App_DriveEventsRead(
    struct App* app,
    uint32_t after_serial,
    struct RS_Event* out,
    int cap,
    int* out_count,
    uint32_t* out_serial)
{
    assert(app);
    assert(out);
    assert(out_count);
    assert(out_serial);
    assert(cap > 0);

    if( !RS_EventRing_Read(
            &app->drive_events.ring, app->drive_events.entries, APP_DRIVE_RING_CAPACITY, after_serial,
            out, cap, out_count, out_serial) )
        return DRIVE_REFUSED;
    return DRIVE_OK;
}
