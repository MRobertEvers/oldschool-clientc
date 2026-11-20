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

#include "game/rs_effect_log.h"
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
    assert(kind > RS_EVENT_NONE);
    assert(kind < RS_EVENT_KIND_COUNT);
    sink = App_DriveEventSink(app);
    RS_EventSink_Stamp(&sink, (int)kind, a, b, c, d);
}

/* THE EFFECT LOG is game/rs_effect_log.h's, the one push, decode and alive
 * rule both clients keep: here with two ticks of slack on the world cycle
 * (the reader's own clock is the server tick, and this client's cycle runs
 * on the wall clock). */
enum
{
    APP_DRIVE_EFFECT_SLACK = 2 * APP_SERVER_TICK_LOGIC_CYCLES,
};

static int
app_drive_effect_now(struct App const* app)
{
    return app->world ? (int)app->world->cycle : 0;
}

int
App_DriveEffectProjectiles(
    struct App const* app,
    struct App_DriveProjectile* out,
    int cap)
{
    static struct RS_EffectProjectile shots[APP_DRIVE_EFFECT_LOG_CAPACITY / 2];
    int n;

    assert(app);
    assert(out);
    assert(cap >= 0);
    n = RS_EffectLog_Projectiles(&app->drive_events.effects, app->drive_events.effect_entries, shots,
        cap < APP_DRIVE_EFFECT_LOG_CAPACITY / 2 ? cap : APP_DRIVE_EFFECT_LOG_CAPACITY / 2);
    for( int i = 0; i < n; i++ )
    {
        out[i].src_x = shots[i].src_x;
        out[i].src_z = shots[i].src_z;
        out[i].dst_x = shots[i].dst_x;
        out[i].dst_z = shots[i].dst_z;
        out[i].level = shots[i].level;
        out[i].spotanim = shots[i].spotanim;
        out[i].target = shots[i].target;
        out[i].start_delay = shots[i].start_delay;
        out[i].end_delay = shots[i].end_delay;
        out[i].cycle = shots[i].cycle;
    }
    return n;
}

int
App_DriveEffectMapAnims(
    struct App const* app,
    struct App_DriveMapAnim* out,
    int cap)
{
    static struct RS_EffectMapAnim anims[APP_DRIVE_EFFECT_LOG_CAPACITY];
    int n;

    assert(app);
    assert(out);
    assert(cap >= 0);
    n = RS_EffectLog_MapAnims(&app->drive_events.effects, app->drive_events.effect_entries, anims,
        cap < APP_DRIVE_EFFECT_LOG_CAPACITY ? cap : APP_DRIVE_EFFECT_LOG_CAPACITY);
    for( int i = 0; i < n; i++ )
    {
        out[i].x = anims[i].x;
        out[i].z = anims[i].z;
        out[i].level = anims[i].level;
        out[i].spotanim = anims[i].spotanim;
        out[i].cycle = anims[i].cycle;
    }
    return n;
}

void
App_DriveProjectileNote(
    struct App* app, int src_x, int src_z, int dst_x, int dst_z, int level, int spotanim,
    int target, int start_delay, int end_delay)
{
    struct RS_EffectProjectile p = {
        .src_x = src_x,
        .src_z = src_z,
        .dst_x = dst_x,
        .dst_z = dst_z,
        .level = level,
        .spotanim = spotanim,
        .target = target,
        .start_delay = start_delay,
        .end_delay = end_delay,
    };

    assert(app);
    /* Every projectile a client would still draw is listed; more than the
     * cap in flight at once is a contract violation, not a drop (scriptrun
     * keeps the same log, scriptrun/scriptrun_zone.h). */
    RS_EffectLog_NoteProjectile(&app->drive_events.effects, app->drive_events.effect_entries,
        app_drive_effect_now(app), APP_DRIVE_EFFECT_SLACK, &p);
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

#endif /* TORIRS_EMBED_SERVER */

/* A client without the embedded server has no reader of the map graphics
 * (DriveUi_*), but the log is the packet's record in every build: a stamp
 * guarded by a build is a stamp nobody notices has rotted. */
void
App_DriveMapAnimNote(struct App* app, int x, int z, int level, int spotanim)
{
    assert(app);
    /* More than the cap alive at once is an assert (rs_effect_log.h). */
    RS_EffectLog_NoteMapAnim(&app->drive_events.effects, app->drive_events.effect_entries,
        app_drive_effect_now(app), APP_DRIVE_EFFECT_SLACK, x, z, level, spotanim);
}

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
