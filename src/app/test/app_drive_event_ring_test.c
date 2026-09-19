/*
 * The drive event ring (docs/QUEST_DRIVER_PLAN.md section 3, gate 5.1):
 * App_DriveEvent appends in stamp order, App_DriveEventsRead hands them back
 * in that same order by cursor, a cursor at the returned serial sees nothing
 * new, and a cursor older than the oldest surviving entry is refused rather
 * than silently skipping the events it missed.
 *
 * Part 2 drives the mechanism through real production call sites -- the dat1
 * lane's chat_opened (rs_ui_slots.c) and slot_mounted (task_slot_mount.c) --
 * against a real dat1 cache + RevConfig build, the same fixture shape
 * game/test/rs_ui_slots_test.c uses. It stands in for the cache lane's
 * sub_opened/sub_mounted/sub_closed family (App_OpenSubInterface reaches an
 * async CreateTask_InterfaceOpenSub mount this fixture has no packed
 * osrs239 assets to resolve); dat1's RS_UISlots_OpenChat/CloseModal reach the
 * same App_PluginLayoutTick-drained ring through the same App_DriveEvent, and
 * unlike the cache lane's mount, their close path (RS_UISlots_CloseModal)
 * needs no async pack load at all, so it is the cheaper, no-less-faithful way
 * to exercise "opens then closes something, and the close-not-mounted case".
 */
#include "app.h"
#include "game/rs_ui_slots.h"
#include "rscache_profile.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * dat1's multi2 chat interface (docs/QUEST_DRIVER_PLAN.md section 3: "static,
 * one interface per option count: multi2 id(2459)..."). ARCHITECT.md's
 * no-numeric-ids rule wants this resolved through a name, but this fixture
 * builds a bare dat1 App (App_Init straight onto a dat1 cache, no server
 * content pack, no osrs239 revconfig [iface:] table), so DriveSymbol_Lookup
 * has nothing to query -- ToriRSServer_ContentSymbol needs a server pack this
 * fixture never loads. Hoisted here, once, rather than left as the bare
 * literal at three call sites. */
#define TEST_DAT1_MULTI2_INTERFACE_ID 2459

static void
test_ring_order_and_second_read_empty(void)
{
    struct App app;
    struct App_DriveEvent out[8];
    int count = -1;
    uint32_t serial = 0;
    enum DriveResult r;

    printf("drive ring: stamp order and a re-poll from the returned serial\n");

    memset(&app, 0, sizeof(app));

    App_DriveEvent(&app, DRIVE_EVENT_SUB_OPENED, 100, 5, 0, 0);
    App_DriveEvent(&app, DRIVE_EVENT_SUB_MOUNTED, 100, 5, 0, 0);
    App_DriveEvent(&app, DRIVE_EVENT_SUB_CLOSED, 100, 7, 0, 0);

    r = App_DriveEventsRead(&app, 0, out, 8, &count, &serial);
    assert(r == DRIVE_OK);
    assert(count == 3);
    assert(out[0].kind == DRIVE_EVENT_SUB_OPENED);
    assert(out[0].a == 100 && out[0].b == 5);
    assert(out[1].kind == DRIVE_EVENT_SUB_MOUNTED);
    assert(out[2].kind == DRIVE_EVENT_SUB_CLOSED);
    assert(out[2].a == 100 && out[2].b == 7);
    assert(out[0].serial < out[1].serial);
    assert(out[1].serial < out[2].serial);
    assert(serial == out[2].serial);

    {
        struct App_DriveEvent out2[8];
        int count2 = -1;
        uint32_t serial2 = 0;

        r = App_DriveEventsRead(&app, serial, out2, 8, &count2, &serial2);
        assert(r == DRIVE_OK);
        assert(count2 == 0);
        assert(serial2 == serial);
    }
    printf("PASS: ring preserves stamp order; a re-poll from the returned serial is empty\n");
}

static void
test_ring_wraparound_refuses_stale_cursor(void)
{
    struct App app;
    struct App_DriveEvent out[8];
    int count = -1;
    uint32_t serial_after_first;
    uint32_t serial = 0;
    enum DriveResult r;
    int i;

    printf("drive ring: a cursor the ring outran is refused, not silently skipped\n");

    memset(&app, 0, sizeof(app));

    App_DriveEvent(&app, DRIVE_EVENT_SERVER_TICK, 1, 0, 0, 0);
    serial_after_first = app.drive_events.newest_serial;
    assert(serial_after_first != 0);

    /* One more than capacity: the first entry is guaranteed gone. */
    for( i = 0; i < APP_DRIVE_RING_CAPACITY + 1; i++ )
        App_DriveEvent(&app, DRIVE_EVENT_SERVER_TICK, i + 2, 0, 0, 0);
    assert(app.drive_events.dropped > 0);

    r = App_DriveEventsRead(&app, serial_after_first, out, 8, &count, &serial);
    assert(r == DRIVE_REFUSED);
    printf(
        "PASS: %u dropped entries later, the first reader's cursor is refused\n",
        app.drive_events.dropped);

    /* A cursor of 0 ("everything still held") is never refused, even once
     * the ring has wrapped -- it is a fresh reader's start, not a stale one. */
    r = App_DriveEventsRead(&app, 0, out, 8, &count, &serial);
    assert(r == DRIVE_OK);
    assert(count == 8);
    printf("PASS: cursor 0 still reads the survivors after the wrap\n");
}

static int
drive_ring_find(
    struct App_DriveEvent const* events,
    int count,
    enum App_DriveEventKind kind)
{
    int i;
    for( i = 0; i < count; i++ )
        if( events[i].kind == (int32_t)kind )
            return i;
    return -1;
}

static void
test_dat1_chat_open_close_stamps_the_ring(
    char const* cache_dir,
    char const* ui_ini,
    char const* cache_ini)
{
    struct App app;
    struct AppConfig cfg = { 0 };
    struct App_DriveEvent out[32];
    int count = -1;
    uint32_t serial = 0;
    enum DriveResult r;

    printf("drive ring: RS_UISlots_OpenChat/CloseModal against a real dat1 cache\n");

    cfg.cache_dir = cache_dir;
    cfg.cache_kind = APP_CACHE_DAT1;
    cfg.cache_game = RSCACHE_GAME_RS2;
    cfg.cache_epoch = RSCACHE_EPOCH_DAT1;
    cfg.cache_revision = 254;
    cfg.cache_quirks = RSCACHE_QUIRK_NONE;
    cfg.cache_identity_set = 1;
    cfg.config_dir = "../config";
    cfg.script_dir = "../script";
    cfg.interface_id = 84;
    cfg.revconfig_ui_ini = ui_ini;
    cfg.revconfig_cache_ini = cache_ini;

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);
    App_BootWait(&app);

    /* The boot itself mounts slots (task_slot_mount runs under the initial
     * RevConfig build), so start this test's read from whatever the boot
     * already stamped, not from serial 0. */
    r = App_DriveEventsRead(&app, 0, out, 32, &count, &serial);
    assert(r == DRIVE_OK);

    RS_UISlots_OpenChat(&app, TEST_DAT1_MULTI2_INTERFACE_ID);
    App_BootWait(&app);

    r = App_DriveEventsRead(&app, serial, out, 32, &count, &serial);
    assert(r == DRIVE_OK);
    {
        int chat_opened_index = drive_ring_find(out, count, DRIVE_EVENT_CHAT_OPENED);
        int slot_mounted_index = drive_ring_find(out, count, DRIVE_EVENT_SLOT_MOUNTED);

        assert(chat_opened_index >= 0);
        assert(out[chat_opened_index].a == TEST_DAT1_MULTI2_INTERFACE_ID);
        assert(slot_mounted_index >= 0);
        /* chat_opened is stamped synchronously inside RS_UISlots_OpenChat,
         * before slot_mount enqueues the async mount task -- the QUEUED edge
         * has to land in the ring first, the same relationship sub_opened
         * has to sub_mounted on the cache lane. */
        assert(chat_opened_index < slot_mounted_index);
    }
    printf("PASS: OpenChat(multi2) stamps chat_opened before slot_mounted, in ring order\n");

    RS_UISlots_CloseModal(&app);
    App_BootWait(&app);

    r = App_DriveEventsRead(&app, serial, out, 32, &count, &serial);
    assert(r == DRIVE_OK);
    {
        int chat_opened_index = drive_ring_find(out, count, DRIVE_EVENT_CHAT_OPENED);
        int slot_mounted_index = drive_ring_find(out, count, DRIVE_EVENT_SLOT_MOUNTED);

        /* The close-not-mounted case, mirrored on this lane: clearing the
         * chat slot rides the same slot_mounted event a real mount does
         * (both ui.await_open and ui.await_close watch it), but chat_opened
         * is gated on iface_id > 0 (app_boot.c's sub_mounted uses the same
         * gate) and must never fire for a close. */
        assert(chat_opened_index < 0);
        assert(slot_mounted_index >= 0);
    }
    printf("PASS: CloseModal stamps slot_mounted only, never chat_opened\n");

    App_Shutdown(&app);
}

int
main(
    int argc,
    char** argv)
{
    char const* cache_dir = argc > 1 ? argv[1] : "../cache254";
    char const* ui_ini = argc > 2 ? argv[2] : "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";
    char const* cache_ini =
        argc > 3 ? argv[3] : "../revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini";

    test_ring_order_and_second_read_empty();
    test_ring_wraparound_refuses_stale_cursor();
    test_dat1_chat_open_close_stamps_the_ring(cache_dir, ui_ini, cache_ini);

    printf("app_drive_event_ring_test: all cases passed\n");
    return 0;
}
