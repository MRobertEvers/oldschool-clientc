/*
 * Exec seam against bare managers (no App / cache / disk): builds parsed
 * packets by hand and asserts they land in VarPManager / InvManager /
 * RS_PlayerStats / UITree / RS_Chat. The app-only branches (IF_OPEN*, world)
 * are exercised by the ui-slots and loopback tests.
 */
#include "game/rs_chat.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_player_stats.h"
#include "inv/inv_manager.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    printf("net-exec: all tests passed\n");
    return 0;
}
