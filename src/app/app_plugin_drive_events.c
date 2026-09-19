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
