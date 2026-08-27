/*
 * Exec seam against bare managers (no App / cache / disk): builds parsed
 * packets by hand and asserts they land in VarPManager / InvManager /
 * RS_PlayerStats / UITree / RS_Chat. The app-only branches (IF_OPEN*, world)
 * are exercised by the ui-slots and loopback tests.
 *
 * Also pins the REBUILD_NORMAL map-square residency predicate that used to
 * compare only the SW square and skip loads that needed an eastern square.
 */
#include "app.h"
#include "game/rs_chat.h"
#include "game/rs_clientcode.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_player_stats.h"
#include "game/task_gameproto_exec.h"
#include "inv/inv_manager.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A row the tick left alone keeps the NULL a fresh spec starts with, and a row
 * it blanked holds "". Both are the same thing to the emit walk, so the test
 * says so once here rather than at every assert. */
/* assert() with the sentence attached: this file's later blocks assert several
 * near-identical conditions, and "assertion failed: x == y" names none of
 * them. Not TEST_ASSERT-with-a-counter, because the rest of the file aborts on
 * the first failure and a half-run exec test proves nothing. */
#define TEST_EXEC_ASSERT(cond, msg)                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

static char const*
test_row_text(struct UITree const* tree, int32_t idx)
{
    char const* text = tree->components[idx].u.rs_text.text;
    return text ? text : "";
}

static void
test_pbits(uint8_t* data, int* bit_pos, int count, uint32_t value)
{
    for( int shift = count - 1; shift >= 0; shift-- )
    {
        int pos = (*bit_pos)++;
        data[pos >> 3] |= (uint8_t)(((value >> shift) & 1u) << (7 - (pos & 7)));
    }
}

int
main(void)
{
    struct UITree* tree = UITree_New(16);
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_Chat chat;

    InvManager_Init(&invs);
    VarPManager_Init(&varps);
    RS_PlayerStats_Init(&stats);
    RS_Chat_Init(&chat, "Player");

    /* IF_SETEVENTS ranges are addressed as (static component, sub-id). A
     * parent-only query must not accidentally arm the parent, while inventory
     * slot queries must retain the slot that arrived beside the parent uid. */
    {
        struct App* event_app = calloc(1, sizeof(*event_app));
        int const component = 0x12340005; /* synthetic; not a cache component */
        int const mask = 0x0033f8fc;

        assert(event_app);
        App_IfEventsSet(event_app, component, 0, 27, mask);
        assert(App_IfEventsGet(event_app, component) == 0);
        assert(App_IfEventsGetAt(event_app, component, 0) == mask);
        assert(App_IfEventsGetAt(event_app, component, 27) == mask);
        assert(App_IfEventsGetAt(event_app, component, 28) == 0);
        free(event_app->if_events);
        free(event_app);
        printf("ok - IF_SETEVENTS ranged sub-id lookup\n");
    }

    /* Rev239's effective WidgetFlags lookup falls back to the cache clickmask
     * only when no server override exists. In particular, an explicit zero is
     * an override, not another spelling of "not found". */
    {
        struct App* event_app = calloc(1, sizeof(*event_app));
        struct UITree* event_tree = UITree_New(2);
        struct UITreeNodeSpec event_spec;
        struct UITreeBehavior event_behavior;
        int const component = 0x23450006;

        assert(event_app);
        memset(&event_spec, 0, sizeof(event_spec));
        memset(&event_behavior, 0, sizeof(event_behavior));
        event_spec.type = UIELEM_RS_GRAPHIC;
        event_spec.component_id = component;
        event_spec.width = 27;
        event_spec.height = 27;
        event_behavior.click_mask = 6;
        event_spec.behavior = &event_behavior;
        TEST_EXEC_ASSERT(
            UITree_Push(event_tree, -1, &event_spec) >= 0,
            "UITree_Push must install the event component");
        event_app->tree = event_tree;

        assert(App_IfEventsGetEffective(event_app, component) == 6);
        App_IfEventsSet(event_app, component, -1, -1, 0);
        assert(App_IfEventsGetEffective(event_app, component) == 0);
        App_IfEventsSet(event_app, component, -1, -1, 2);
        assert(App_IfEventsGetEffective(event_app, component) == 2);

        free(event_app->if_events);
        free(event_app);
        UITree_Free(event_tree);
        printf("ok - rev239 WidgetFlags override/fallback lookup\n");
    }

    /* A TEXT node to receive IF_SETTEXT. */
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = 0x1234;
    spec.width = 100;
    spec.height = 14;
    spec.u.rs_text.font_id = 1;
    spec.u.rs_text.text = "before";
    int32_t node = UITree_Push(tree, -1, &spec);
    assert(node >= 0);

    struct RS_GameProtoCtx ctx = {
        .tree = tree,
        .invs = &invs,
        .varps = &varps,
        .stats = &stats,
        .chat = &chat,
        .app = NULL,
    };

    /* VARP_SMALL 42 = 5 */
    {
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_VARP_SMALL;
        p._varp_small.variable = 42;
        p._varp_small.value = 5;
        RS_GameProto_Exec(&ctx, &p);
        assert(VarPManager_GetVarp(&varps, 42) == 5);
        printf("ok - VARP_SMALL applied\n");
    }

    /* UPDATE_RUNENERGY 73 */
    {
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_RUNENERGY;
        p._update_run_energy.run_energy = 73;
        RS_GameProto_Exec(&ctx, &p);
        assert(stats.run_energy == 73);
        printf("ok - UPDATE_RUNENERGY applied\n");
    }

    /* UPDATE_STAT: attack (0) level 40. */
    {
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_STAT;
        p._update_stat.stat = 0;
        p._update_stat.xp = 37224;
        p._update_stat.level = 40;
        RS_GameProto_Exec(&ctx, &p);
        assert(stats.current_level[0] == 40);
        printf("ok - UPDATE_STAT applied\n");
    }

    /* UPDATE_INV_FULL into container keyed by component id 3214. */
    {
        int obj_ids[3] = { 995, 1333, 4151 };
        int obj_counts[3] = { 10000, 1, 1 };
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_INV_FULL;
        p._update_inv_full.component_id = 3214;
        p._update_inv_full.size = 3;
        p._update_inv_full.obj_ids = obj_ids;
        p._update_inv_full.obj_counts = obj_counts;
        RS_GameProto_Exec(&ctx, &p);
        assert(InvManager_GetObj(&invs, 3214, 0) == 995);
        assert(InvManager_GetObj(&invs, 3214, 2) == 4151);
        printf("ok - UPDATE_INV_FULL applied\n");
    }

    /* IF_SETTEXT rewrites the TEXT node. */
    {
        char text[] = "after";
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_IF_SETTEXT;
        p._if_settext.component_id = 0x1234;
        p._if_settext.text = text;
        RS_GameProto_Exec(&ctx, &p);
        {
            int32_t idx = UITree_FindByComponentId(tree, 0x1234);
            assert(idx >= 0);
            assert(strcmp(tree->components[idx].u.rs_text.text, "after") == 0);
        }
        printf("ok - IF_SETTEXT applied\n");
    }

    /* MESSAGE_GAME appends a chat line. */
    {
        char text[] = "A goblin appears.";
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_MESSAGE_GAME;
        p._message_game.text = text;
        RS_GameProto_Exec(&ctx, &p);
        assert(chat.message_count == 1);
        assert(strcmp(chat.messages[0].text, "A goblin appears.") == 0);
        printf("ok - MESSAGE_GAME applied\n");
    }

    /* REBUILD_NORMAL square rect: classic (zone-6)*8 scene covers at most 3x3
     * map squares. Zone centre 50,50 -> base 352 -> squares 5..7. */
    {
        int mx0, mz0, mx1, mz1;

        rebuild_square_rect(50, 50, &mx0, &mz0, &mx1, &mz1);
        assert(mx0 == 5 && mz0 == 5 && mx1 == 7 && mz1 == 7);

        rebuild_square_rect(391, 403, &mx0, &mz0, &mx1, &mz1);
        assert(mx0 == 48 && mz0 == 49 && mx1 == 49 && mz1 == 51);

        rebuild_square_rect(396, 403, &mx0, &mz0, &mx1, &mz1);
        assert(mx0 == 48 && mz0 == 49 && mx1 == 50 && mz1 == 51);

        printf("ok - REBUILD_NORMAL square rect\n");
    }

    /* REBUILD_REGION_V2: exact 239 header followed by a bit-packed 4x13x13
     * descriptor grid, with no obsolete trailing XTEA block. */
    {
        uint8_t body[95] = { 0 };
        struct RevPacket p;
        struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
        int bit_pos = 7 * 8;
        uint32_t descriptor = UINT32_C(0x0123456);

        body[0] = 0x01; /* zoneZ = 0x0123 */
        body[1] = 0x23;
        body[2] = 0x02; /* zoneX = 0x0234 */
        body[3] = 0x34;
        body[4] = 0x81; /* p1Alt1(reload=true) */
        body[5] = 0x00; /* one distinct source square */
        body[6] = 0x01;
        test_pbits(body, &bit_pos, 1, 1);
        test_pbits(body, &bit_pos, 26, descriptor);
        bit_pos += PKT_MAP_REBUILD_ZONES - 1; /* remaining absent-bit records */
        assert((bit_pos + 7) / 8 == (int)sizeof(body));

        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse(rev, PKT_NAME_REBUILD_REGION, body, sizeof(body), &p),
            "REBUILD_REGION_V2 must parse");
        assert(p._map_rebuild.zonez == 0x0123);
        assert(p._map_rebuild.zonex == 0x0234);
        assert(p._map_rebuild.zones != NULL);
        assert((uint32_t)p._map_rebuild.zones[0] == descriptor);
        assert(p._map_rebuild.zones[1] == 0);
        free(p._map_rebuild.zones);

        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            !rev->parse(rev, PKT_NAME_REBUILD_REGION, body, (int)sizeof(body) - 1, &p),
            "truncated REBUILD_REGION_V2 must be rejected");
        assert(p._map_rebuild.zones == NULL);
        printf("ok - REBUILD_REGION_V2 parsed and bounds checked\n");
    }

    /* Revision-239 zone headers carry a plane. The client must retain it: zone
     * events may describe level-1 scenery while the player remains on level 0. */
    {
        struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
        struct RevPacket p;

        uint8_t partial[3] = { (uint8_t)(41 + 128), 55, (uint8_t)-1 };
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse(
                rev,
                PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
                partial,
                (int)sizeof(partial),
                &p),
            "UPDATE_ZONE_PARTIAL_FOLLOWS must parse");
        assert(p._update_zone_partial_follows.base_z == 41);
        assert(p._update_zone_partial_follows.base_x == 55);
        assert(p._update_zone_partial_follows.level == 1);

        uint8_t full[3] = { (uint8_t)(128 - 42), (uint8_t)-56, 2 };
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse(
                rev, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, full, (int)sizeof(full), &p),
            "UPDATE_ZONE_FULL_FOLLOWS must parse");
        assert(p._update_zone_full_follows.base_z == 42);
        assert(p._update_zone_full_follows.base_x == 56);
        assert(p._update_zone_full_follows.level == 2);

        uint8_t enclosed[3] = { (uint8_t)-3, (uint8_t)(43 + 128), (uint8_t)(57 + 128) };
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse(
                rev,
                PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
                enclosed,
                (int)sizeof(enclosed),
                &p),
            "UPDATE_ZONE_PARTIAL_ENCLOSED must parse");
        assert(p._update_zone_enclosed.base_z == 43);
        assert(p._update_zone_enclosed.base_x == 57);
        assert(p._update_zone_enclosed.level == 3);
        free(p._update_zone_enclosed.entries);
        printf("ok - revision-239 zone header planes retained\n");
    }

    /* SET_ACTIVE_WORLD off the wire: SetActiveWorldV2Encoder writes p2 the
     * world-entity id big-endian, then p1 the plane — the layout the server's
     * SendSetActiveWorld emits. A mis-sized or reordered read shows up here. */
    {
        struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
        struct RevPacket p;

        uint8_t const wire[3] = { 0x02, 0x9A, 0x01 };
        memset(&p, 0, sizeof(p));
        assert(gameproto_parse(rev, PKT_NAME_SET_ACTIVE_WORLD, (uint8_t*)wire, 3, &p));
        assert(p._set_active_world.world_id == 666);
        assert(p._set_active_world.level == 1);

        /* Root re-select, the id every SERVER_TICK_END implies. */
        uint8_t const root_wire[3] = { 0x00, 0x00, 0x00 };
        memset(&p, 0, sizeof(p));
        assert(gameproto_parse(rev, PKT_NAME_SET_ACTIVE_WORLD, (uint8_t*)root_wire, 3, &p));
        assert(p._set_active_world.world_id == 0);
        assert(p._set_active_world.level == 0);
        printf("ok - SET_ACTIVE_WORLD decodes id then plane\n");
    }

    /* Exec half: the packet arms the worldview cursor (id AND plane), and the
     * tick fence disarms it back to the root. The registry holds dummy
     * world/builder pointers — the handler only checks liveness, and nothing
     * here dereferences or frees them. */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        static char dummy_world[1];
        static char dummy_builder[1];

        assert(app);
        WorldviewRegistry_Init(&app->worldviews);
        WorldviewRegistry_RegisterRoot(
            &app->worldviews,
            (struct World*)dummy_world,
            (struct WorldBuilder*)dummy_builder);
        WorldviewRegistry_Register(
            &app->worldviews,
            3,
            (struct World*)dummy_world,
            (struct WorldBuilder*)dummy_builder,
            6400,
            6400,
            8,
            8,
            WORLDVIEW_ROOT);
        ctx.app = app;

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SET_ACTIVE_WORLD;
        p._set_active_world.world_id = 3;
        p._set_active_world.level = 1;
        RS_GameProto_Exec(&ctx, &p);
        assert(app->active_world == 3);
        assert(app->active_world_level == 1);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SERVER_TICK_END;
        RS_GameProto_Exec(&ctx, &p);
        assert(app->active_world == WORLDVIEW_ROOT);
        assert(app->active_world_level == 0);

        ctx.app = NULL;
        free(app);
        printf("ok - SET_ACTIVE_WORLD arms the cursor and the fence resets it\n");
    }

    /* SET_MAP_FLAG: wire tiles are classic-scene local = our-scene tiles. */
    {
        struct App* app = calloc(1, sizeof(*app));
        assert(app);
        app->minimap_flag_x = -1;
        app->minimap_flag_z = -1;
        ctx.app = app;

        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 40;
            p._set_map_flag.z = 50;
            p._set_map_flag.clear = 0;
            RS_GameProto_Exec(&ctx, &p);
            assert(app->minimap_flag_x == 40);
            assert(app->minimap_flag_z == 50);
            assert(app->need_redraw == 1);
        }
        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 255;
            p._set_map_flag.z = 255;
            p._set_map_flag.clear = 1;
            RS_GameProto_Exec(&ctx, &p);
            assert(app->minimap_flag_x == -1);
            assert(app->minimap_flag_z == -1);
        }
        ctx.app = NULL;
        free(app);
        printf("ok - SET_MAP_FLAG stores scene-local tiles\n");
    }

    /*
     * A LOC_ANIM in the same zone update as a LOC_ADD_CHANGE for the same tile
     * must apply AFTER it.
     *
     * The reference client applies a zone loc change to the scene the moment it
     * reads it, so the two ops work in the order they were written. This client
     * cannot: a change waits for its loc config and models to become resident,
     * so `App_WorldLocChange` enqueues on the serial exec FIFO. While the
     * animation was applied synchronously beside that, it ran FIRST - it looked
     * up the scenery before the pending change had built it, found the old loc
     * or nothing, and dropped the sequence with no error. The loc then stood on
     * whatever frame its config `anim=` left it on, which is every "the loc is
     * stuck in a frame" report there has ever been here.
     *
     * The property under test is ORDER, so that is what is asserted: both ops
     * are on the one queue and the animation is behind the change. Reverting
     * `App_WorldSceneryAnim` to a direct call leaves one task here, not two.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct ToriRS_Task* head;

        assert(app);
        app->exec_runner.queue = ToriRS_TaskQueue_New();

        App_WorldLocChange(app, 12, 34, 0, 32744 /* loc */, 22 /* grounddecor */, 0);
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 8068 /* seq */);

        head = app->exec_runner.queue->head;
        assert(head != NULL);
        assert(head->next != NULL);
        assert(head->next->next == NULL);
        /* Both are AppSpawn tasks; the change was enqueued first and a FIFO
         * keeps it there. `ToriRS_TaskQueue_Run` runs the head until it yields
         * and leaves the rest queued in order, so this ordering IS the fix. */
        assert(strcmp(head->name, "AppSpawn") == 0);
        assert(strcmp(head->next->name, "AppSpawn") == 0);

        /* The queue's own teardown: each task's vtable Free owns its
         * allocation, so hand-freeing them here double-frees. */
        ToriRS_TaskQueue_Free(app->exec_runner.queue);
        free(app);
        printf("ok - LOC_ANIM queues behind a same-tile LOC_ADD_CHANGE\n");
    }

    /*
     * The four packets that were decoded-but-inert: each one's state has to be
     * both WRITTEN by exec and READ by something, so these assert the write and
     * the reader tests below assert the read.
     */
    {
        /* Heap, not stack: struct App is large enough that a third instance in
         * this function overflows the 8MB main stack outright. */
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();

        assert(app);
        ctx.app = app;

        /* MINIMAP_TOGGLE had no parser at all; parse it off the wire rather
         * than hand-filling the struct, so a mis-sized field shows up here. */
        {
            uint8_t const wire[1] = { 2 };
            memset(&p, 0, sizeof(p));
            TEST_EXEC_ASSERT(
                gameproto_parse(rev, PKT_NAME_MINIMAP_TOGGLE, (uint8_t*)wire, 1, &p),
                "MINIMAP_TOGGLE must parse");
            assert(p._minimap_toggle.state == 2);
            RS_GameProto_Exec(&ctx, &p);
            assert(app->minimap_state == APP_MINIMAP_STATE_HIDDEN_UNCLICKABLE);
            assert(app->need_redraw == 1);
        }
        printf("ok - MINIMAP_TOGGLE decodes and reaches the client\n");

        /* Server ticks in, client cycles out. A countdown left in server ticks
         * would run 30x fast and read as seconds when it means half-minutes. */
        {
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UPDATE_REBOOT_TIMER;
            p._update_reboot_timer.ticks = 100;
            RS_GameProto_Exec(&ctx, &p);
            assert(app->reboot_timer == 100 * APP_SERVER_TICK_LOGIC_CYCLES);
            /* 100 server ticks = 60s = one minute exactly. */
            assert(app->reboot_timer / APP_LOGIC_CYCLES_PER_SECOND == 60);
        }
        printf("ok - UPDATE_REBOOT_TIMER converts server ticks to client cycles\n");

        {
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_SET_MULTIWAY;
            p._set_multiway.multiway = 1;
            app->need_redraw = 0;
            RS_GameProto_Exec(&ctx, &p);
            assert(app->multiway == 1);
            assert(app->need_redraw == 1);
        }
        printf("ok - SET_MULTIWAY reaches the client\n");

        /* The trailing member-warning byte was parsed and then dropped by exec;
         * without it the recovery rows cannot tell "nothing to say" from "you
         * are a member on a free world". */
        {
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_LAST_LOGIN_INFO;
            p._last_login_info.last_ip = 0x01020304;
            p._last_login_info.days_since_login = 2;
            p._last_login_info.days_since_recovery = RS_CC_RECOVERY_DAYS_SILENT;
            p._last_login_info.unread_messages = 3;
            p._last_login_info.member_warning = 1;
            RS_GameProto_Exec(&ctx, &p);
            assert(app->welcome.last_ip == 0x01020304);
            assert(app->welcome.days_since_login == 2);
            assert(app->welcome.unread_messages == 3);
            assert(app->welcome.member_warning == 1);
        }
        printf("ok - LAST_LOGIN_INFO carries the member warning\n");

        ctx.app = NULL;
        free(app);
    }

    /*
     * The reader half of the welcome screen: clientCode rows built over a bare
     * tree, ticked, and read back. Placeholder text ("earlier today" whatever
     * the packet said, "0 unread messages" whatever the count) is exactly what
     * this catches.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct UITree* wtree = UITree_New(16);
        int32_t rows[6];
        static int const k_codes[6] = { 650, 651, 652, 653, 654, 655 };

        assert(app);
        for( int i = 0; i < 6; i++ )
        {
            struct UITreeNodeSpec spec;
            struct UITreeBehavior behavior;
            memset(&spec, 0, sizeof(spec));
            memset(&behavior, 0, sizeof(behavior));
            spec.type = UIELEM_RS_TEXT;
            spec.component_id = 1000 + i;
            behavior.client_code = k_codes[i];
            spec.behavior = &behavior;
            rows[i] = UITree_Push(wtree, -1, &spec);
            assert(rows[i] >= 0);
        }

        /* Before any packet: last_ip 0 means "no login info", and the reference
         * blanks the row rather than printing 0.0.0.0. */
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        assert(strcmp(test_row_text(wtree, rows[0]), "") == 0);

        app->welcome.last_ip = 0x0a000205; /* 10.0.2.5 */
        app->welcome.days_since_login = 3;
        app->welcome.days_since_recovery = 5;
        app->welcome.unread_messages = 2;
        app->welcome.member_warning = 0;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        assert(
            strcmp(
                test_row_text(wtree, rows[0]),
                "You last logged in 3 days ago from: 10.0.2.5") == 0);
        assert(strcmp(test_row_text(wtree, rows[1]), "2 unread messages") == 0);
        assert(wtree->components[rows[1]].u.rs_text.color == RS_CC_WELCOME_COLOUR_GREEN);
        assert(
            strcmp(
                test_row_text(wtree, rows[2]),
                "5 days ago you changed your recovery questions") == 0);
        /* 655 is the same row as 650 on the other welcome screen. */
        assert(
            strcmp(
                test_row_text(wtree, rows[5]),
                "You last logged in 3 days ago from: 10.0.2.5") == 0);

        /* Loopback carries no information, so the reference drops the clause
         * instead of showing 127.0.0.1 to every account on a local server. */
        app->welcome.last_ip = 0x7f000001;
        app->welcome.days_since_login = 0;
        app->welcome.unread_messages = 0;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        assert(
            strcmp(test_row_text(wtree, rows[0]), "You last logged in earlier today.") ==
            0);
        assert(strcmp(test_row_text(wtree, rows[1]), "0 unread messages") == 0);
        assert(wtree->components[rows[1]].u.rs_text.color == RS_CC_WELCOME_COLOUR_YELLOW);

        /* 201 = nothing to say about recovery questions; the three rows go to
         * the member warning when there is one, and blank when there is not. */
        app->welcome.days_since_recovery = RS_CC_RECOVERY_DAYS_SILENT;
        app->welcome.member_warning = 0;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        for( int i = 2; i <= 4; i++ )
            assert(strcmp(test_row_text(wtree, rows[i]), "") == 0);

        app->welcome.member_warning = 1;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        for( int i = 2; i <= 4; i++ )
            assert(test_row_text(wtree, rows[i])[0] != '\0');

        /* 200 = never set any, which is its own paragraph and not a day count. */
        app->welcome.days_since_recovery = RS_CC_RECOVERY_DAYS_NEVER_SET;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        assert(
            strcmp(
                test_row_text(wtree, rows[2]),
                "You have not yet set any password recovery questions.") == 0);

        UITree_Free(wtree);
        free(app);
        printf("ok - welcome rows read LAST_LOGIN_INFO\n");
    }

    /*
     * LAST_LOGIN_INFO also OPENS the welcome screen -- nothing else in the
     * client does, so without this the rows above are correct on a screen no
     * one ever sees.
     *
     * Reads the SHIPPED profile rather than a fixture: the two interface ids
     * are the answer a reference client gets by scanning its whole interface
     * archive for clientCode 650/655, and the point of stating them in
     * revconfig is that they are checkable there.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        int const iface_plain = 5993;  /* clientCode 650 lives on this layer */
        int const iface_notice = 6073; /* ...and 655 (plus 652/653/654) here */

        assert(app);
        RevConfigRefs_Init(&app->revconfig_refs);
        RevConfigRefs_LoadSources(
            &app->revconfig_refs,
            "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini",
            "../revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini",
            NULL);
        TEST_EXEC_ASSERT(
            RevConfigRefs_Get(&app->revconfig_refs, "iface", "welcome_screen") == iface_plain,
            "profile declares the plain welcome screen");
        TEST_EXEC_ASSERT(
            RevConfigRefs_Get(&app->revconfig_refs, "iface", "welcome_screen_notice") ==
                iface_notice,
            "profile declares the welcome screen with the notice paragraph");

        RS_UISlots_Init(&app->slots);
        ctx.app = app;

        /* Nothing to say about recovery questions and no members warning: the
         * short screen, which has no paragraph rows on it. */
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_LAST_LOGIN_INFO;
        p._last_login_info.last_ip = 0x0a000205;
        p._last_login_info.days_since_login = 3;
        p._last_login_info.days_since_recovery = RS_CC_RECOVERY_DAYS_SILENT;
        p._last_login_info.unread_messages = 0;
        p._last_login_info.member_warning = 0;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_plain, "plain welcome screen opens");

        /* A second one must not push in front of the screen already up --
         * checked with a packet that would otherwise select the OTHER screen,
         * so "left alone" is distinguishable from "reopened identically". */
        p._last_login_info.member_warning = 1;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_plain,
            "a welcome screen already open is left alone");
        p._last_login_info.member_warning = 0;

        /* A members warning turns the same packet into the taller screen: it
         * is the only one carrying rows 652/653/654 to put the warning on. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.member_warning = 1;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_notice,
            "a members warning opens the screen that can show it");

        /* So does a real recovery-question age, for the same reason. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.member_warning = 0;
        p._last_login_info.days_since_recovery = 5;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_notice,
            "a recovery-question notice opens the taller screen");

        /* No address = the server is not offering one. Opening anything here
         * would put an empty last-login row in front of the player. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.last_ip = 0;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == -1, "no address opens no welcome screen");

        ctx.app = NULL;
        RevConfigRefs_Free(&app->revconfig_refs);
        free(app);
        printf("ok - LAST_LOGIN_INFO opens the welcome screen the profile names\n");
    }

    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    printf("net-exec: all tests passed\n");
    return 0;
}
