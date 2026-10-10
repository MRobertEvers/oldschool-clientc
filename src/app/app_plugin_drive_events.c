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

static char const* const DRIVE_EVENT_NAMES[DRIVE_EVENT_KIND_COUNT] = {
    "none",
    "sub_opened",
    "sub_mounted",
    "sub_closed",
    "chat_opened",
    "slot_mounted",
    "resume_answered",
    "varp_changed",
    "inv_changed",
    "obj_added",
    "obj_removed",
    "map_flag",
    "chat_message",
    "server_tick",
    "npc_spawn",
    "npc_despawn",
    "npc_retype",
    "inv_packet",
    "npc_seq",
    "npc_face",
};

char const*
DriveEventKindName(enum App_DriveEventKind kind)
{
    assert(kind >= 0);
    assert(kind < DRIVE_EVENT_KIND_COUNT);
    return DRIVE_EVENT_NAMES[kind];
}

int
DriveEventKindFromName(char const* name)
{
    assert(name);
    for( int kind = 0; kind < DRIVE_EVENT_KIND_COUNT; kind++ )
        if( strcmp(name, DRIVE_EVENT_NAMES[kind]) == 0 )
            return kind;
    /* A test's typo in an await descriptor: a runtime answer, not a contract
     * violation, and the caller turns it into a Lua error naming the name. */
    return -1;
}

void
App_DriveEvent(
    struct App* app,
    enum App_DriveEventKind kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    struct App_DriveRing* ring;
    struct App_DriveEvent* slot;

    assert(app);
    assert(kind > DRIVE_EVENT_NONE);
    assert(kind < DRIVE_EVENT_KIND_COUNT);

    ring = &app->drive_events;
    slot = &ring->entries[ring->write_index];
    if( ring->count == APP_DRIVE_RING_CAPACITY )
    {
        /* The entry about to be overwritten is the oldest one. Losing it is
         * recorded rather than silent: a reader whose cursor is now older than
         * oldest_serial is told DRIVE_REFUSED and can say "the pump stopped",
         * which is a diagnosable failure. Skipping the event and carrying on
         * is the one thing that is not. */
        ring->dropped++;
        ring->oldest_serial = slot->serial + 1;
    }
    else
    {
        ring->count++;
    }

    memset(slot, 0, sizeof(*slot));
    slot->serial = ++ring->newest_serial;
    slot->kind = (int32_t)kind;
    slot->cycle = app->world ? (int32_t)app->world->cycle : 0;
    slot->a = a;
    slot->b = b;
    slot->c = c;
    slot->d = d;

    if( ring->oldest_serial == 0 )
        ring->oldest_serial = slot->serial;
    ring->write_index = (ring->write_index + 1) % APP_DRIVE_RING_CAPACITY;
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
        if( now - ring->map_anims[i].cycle <= APP_DRIVE_MAP_ANIM_LIFE )
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
        /* Only changes the scenery has not caught up with are held
         * (DriveUi_Locs retires the rest); more than this many waiting on
         * their models at once is a contract violation, not a drop. */
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
    c->lane_ticket = app->loc_lane_enqueued;
    ring->loc_change_serial++;
}

enum DriveResult
App_DriveEventsRead(
    struct App* app,
    uint32_t after_serial,
    struct App_DriveEvent* out,
    int cap,
    int* out_count,
    uint32_t* out_serial)
{
    struct App_DriveRing* ring;
    int written = 0;
    int first;

    assert(app);
    assert(out);
    assert(out_count);
    assert(out_serial);
    assert(cap > 0);

    ring = &app->drive_events;
    *out_count = 0;
    *out_serial = ring->newest_serial;
    if( ring->count == 0 )
        return DRIVE_OK;
    /* A cursor of 0 is "everything you still hold", which is how a reader
     * that has never polled starts. Any other cursor below the oldest
     * surviving serial has missed entries. */
    if( after_serial != 0 && after_serial + 1 < ring->oldest_serial )
        return DRIVE_REFUSED;

    first = ring->write_index - ring->count;
    if( first < 0 )
        first += APP_DRIVE_RING_CAPACITY;
    for( int i = 0; i < ring->count && written < cap; i++ )
    {
        struct App_DriveEvent const* entry =
            &ring->entries[(first + i) % APP_DRIVE_RING_CAPACITY];
        if( entry->serial <= after_serial )
            continue;
        out[written++] = *entry;
    }
    *out_count = written;
    /* Deliberately the batch's last serial and not the ring's newest: a
     * truncated read must be re-polled from where it stopped, or the entries
     * `cap` cut off would be skipped for good. */
    if( written > 0 )
        *out_serial = out[written - 1].serial;
    return DRIVE_OK;
}
