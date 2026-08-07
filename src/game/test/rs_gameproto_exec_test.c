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
#include "game/rs_gameproto_exec.h"
#include "game/rs_player_stats.h"
#include "game/task_gameproto_exec.h"
#include "inv/inv_manager.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        struct App event_app;
        int const component = 0x12340005; /* synthetic; not a cache component */
        int const mask = 0x0033f8fc;

        memset(&event_app, 0, sizeof(event_app));
        App_IfEventsSet(&event_app, component, 0, 27, mask);
        assert(App_IfEventsGet(&event_app, component) == 0);
        assert(App_IfEventsGetAt(&event_app, component, 0) == mask);
        assert(App_IfEventsGetAt(&event_app, component, 27) == mask);
        assert(App_IfEventsGetAt(&event_app, component, 28) == 0);
        free(event_app.if_events);
        printf("ok - IF_SETEVENTS ranged sub-id lookup\n");
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
        assert(rev->parse(rev, PKT_NAME_REBUILD_REGION, body, sizeof(body), &p));
        assert(p._map_rebuild.zonez == 0x0123);
        assert(p._map_rebuild.zonex == 0x0234);
        assert(p._map_rebuild.zones != NULL);
        assert((uint32_t)p._map_rebuild.zones[0] == descriptor);
        assert(p._map_rebuild.zones[1] == 0);
        free(p._map_rebuild.zones);

        memset(&p, 0, sizeof(p));
        assert(!rev->parse(
            rev, PKT_NAME_REBUILD_REGION, body, (int)sizeof(body) - 1, &p));
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
        assert(rev->parse(
            rev, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS, partial, (int)sizeof(partial), &p));
        assert(p._update_zone_partial_follows.base_z == 41);
        assert(p._update_zone_partial_follows.base_x == 55);
        assert(p._update_zone_partial_follows.level == 1);

        uint8_t full[3] = { (uint8_t)(128 - 42), (uint8_t)-56, 2 };
        memset(&p, 0, sizeof(p));
        assert(rev->parse(
            rev, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, full, (int)sizeof(full), &p));
        assert(p._update_zone_full_follows.base_z == 42);
        assert(p._update_zone_full_follows.base_x == 56);
        assert(p._update_zone_full_follows.level == 2);

        uint8_t enclosed[3] = { (uint8_t)-3, (uint8_t)(43 + 128), (uint8_t)(57 + 128) };
        memset(&p, 0, sizeof(p));
        assert(rev->parse(
            rev,
            PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
            enclosed,
            (int)sizeof(enclosed),
            &p));
        assert(p._update_zone_enclosed.base_z == 43);
        assert(p._update_zone_enclosed.base_x == 57);
        assert(p._update_zone_enclosed.level == 3);
        free(p._update_zone_enclosed.entries);
        printf("ok - revision-239 zone header planes retained\n");
    }

    /* SET_MAP_FLAG: wire tiles are classic-scene local = our-scene tiles. */
    {
        struct App app;
        memset(&app, 0, sizeof(app));
        app.minimap_flag_x = -1;
        app.minimap_flag_z = -1;
        ctx.app = &app;

        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 40;
            p._set_map_flag.z = 50;
            p._set_map_flag.clear = 0;
            RS_GameProto_Exec(&ctx, &p);
            assert(app.minimap_flag_x == 40);
            assert(app.minimap_flag_z == 50);
            assert(app.need_redraw == 1);
        }
        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 255;
            p._set_map_flag.z = 255;
            p._set_map_flag.clear = 1;
            RS_GameProto_Exec(&ctx, &p);
            assert(app.minimap_flag_x == -1);
            assert(app.minimap_flag_z == -1);
        }
        ctx.app = NULL;
        printf("ok - SET_MAP_FLAG stores scene-local tiles\n");
    }

    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    printf("net-exec: all tests passed\n");
    return 0;
}
