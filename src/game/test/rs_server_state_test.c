/*
 * The packet model (game/rs_server_state.h) with no App and no UI tree: a
 * parsed packet in, the records and the stamped events out, for each family
 * RS_ServerState_Apply owns. The real client's presentation of the same
 * packets is test-net-exec's (game/test/rs_gameproto_exec_test.c).
 */
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "game/rs_event_ring.h"
#include "game/rs_hint_arrow.h"
#include "game/rs_interaction_mode.h"
#include "game/rs_npc_update_origin.h"
#include "game/rs_player_ops.h"
#include "game/rs_player_stats.h"
#include "game/rs_server_state.h"
#include "game/rs_server_tick.h"
#include "game/rs_session_info.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "inv/inv_manager.h"
#include "net/jbase37.h"
#include "net/rev/revpacket.h"
#include "net/session_end.h"
#include "test_harness.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "varp/varp_manager.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

enum
{
    TEST_RING_CAP = 64,
};

/* Everything the model writes, owned by the test. */
struct Fixture
{
    struct RS_Event entries[TEST_RING_CAP];
    struct RS_EventRing ring;
    uint32_t cursor;
    int clock;
    int chat_filter_mode[RS_UI_CHAT_FILTER_COUNT];

    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct InvManager invs;
    struct UIIfTextStore if_texts;
    struct UIIfIntStore if_hides;
    struct UIIfIntStore if_colours;
    struct UIIfEventTable if_events;
    struct RS_Chat chat;
    struct RS_Social social;
    struct RS_SessionInfo session_info;
    struct SessionEnd session_end;
    struct RS_ServerTick server_tick;
    struct RS_PlayerOps player_ops;
    struct RS_HintArrow hint_arrow;
    struct RS_NpcUpdateOrigin npc_update_origin;
    struct RS_InteractionMode interaction_mode;
    struct WorldviewRegistry worldviews;
    struct World* world;
    struct RS_EntitySync esync;

    struct RS_ServerState state;
};

static struct Fixture*
fixture_new(void)
{
    struct Fixture* fx = calloc(1, sizeof(*fx));
    struct RS_EventSink sink;

    assert(fx);
    VarPManager_Init(&fx->varps);
    RS_PlayerStats_Init(&fx->stats);
    InvManager_Init(&fx->invs);
    RS_Chat_Init(&fx->chat, "Player");
    RS_Social_Init(&fx->social);
    RS_SessionInfo_Reset(&fx->session_info);
    RS_InteractionMode_Reset(&fx->interaction_mode);
    WorldviewRegistry_Init(&fx->worldviews);
    fx->world = World_TestMakeReady(104);
    RS_EntitySync_Init(&fx->esync);
    for( int i = 0; i < RS_UI_CHAT_FILTER_COUNT; i++ )
        fx->chat_filter_mode[i] = 7;
    fx->clock = 4321;

    sink = (struct RS_EventSink){
        .ring = &fx->ring,
        .entries = fx->entries,
        .capacity = TEST_RING_CAP,
        .full = RS_EVENT_RING_COUNT_DROPS,
        .cycle = &fx->world->cycle,
    };
    /* The chat store stamps chat_message into the same ring, as the real
     * client binds it (app.c, RS_Chat_SetEventSink). */
    RS_Chat_SetEventSink(&fx->chat, sink);
    fx->state = (struct RS_ServerState){
        .varps = &fx->varps,
        .stats = &fx->stats,
        .invs = &fx->invs,
        .if_texts = &fx->if_texts,
        .if_hides = &fx->if_hides,
        .if_colours = &fx->if_colours,
        .if_events = &fx->if_events,
        .chat = &fx->chat,
        .chat_clock = &fx->clock,
        .social = &fx->social,
        .chat_filter_mode = fx->chat_filter_mode,
        .session_info = &fx->session_info,
        .session_end = &fx->session_end,
        .server_tick = &fx->server_tick,
        .player_ops = &fx->player_ops,
        .hint_arrow = &fx->hint_arrow,
        .npc_update_origin = &fx->npc_update_origin,
        .interaction_mode = &fx->interaction_mode,
        .rev_number = 242,
        .worldviews = &fx->worldviews,
        .world = fx->world,
        .esync = &fx->esync,
        .provider = NULL,
        .events = sink,
    };
    return fx;
}

static void
fixture_free(struct Fixture* fx)
{
    RS_EntitySync_Free(&fx->esync);
    World_Free(fx->world);
    UIIfTextStore_Free(&fx->if_texts);
    UIIfIntStore_Free(&fx->if_hides);
    UIIfIntStore_Free(&fx->if_colours);
    UIIfEventTable_Free(&fx->if_events);
    RS_Chat_Free(&fx->chat);
    InvManager_Free(&fx->invs);
    VarPManager_Free(&fx->varps);
    free(fx);
}

static enum RS_ServerStateApply
apply(
    struct Fixture* fx,
    struct RevPacket const* p)
{
    return RS_ServerState_Apply(&fx->state, p);
}

/* The events stamped since the last call, oldest first. */
static int
take_events(
    struct Fixture* fx,
    struct RS_Event* out,
    int cap)
{
    int n = 0;
    int ok = RS_EventRing_Read(
        &fx->ring, fx->entries, TEST_RING_CAP, fx->cursor, out, cap, &n, &fx->cursor);
    CHECK(ok, "the test's ring never overflows");
    return n;
}

static struct RevPacket
packet(enum GameProtoPktName name)
{
    struct RevPacket p;
    memset(&p, 0, sizeof(p));
    p.packet_type = name;
    return p;
}

static void
test_varps(void)
{
    struct Fixture* fx = fixture_new();
    struct RS_Event ev[8];
    struct RevPacket p;
    int n;

    p = packet(PKT_NAME_VARP_SMALL);
    p._varp_small.variable = 42;
    p._varp_small.value = 5;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "VARP_SMALL is a model packet");
    CHECK(VarPManager_GetVarp(&fx->varps, 42) == 5, "VARP_SMALL writes the varp");

    p = packet(PKT_NAME_VARP_LARGE);
    p._varp_large.variable = 43;
    p._varp_large.value = 0x12345678;
    apply(fx, &p);
    CHECK(VarPManager_GetVarp(&fx->varps, 43) == 0x12345678, "VARP_LARGE writes the varp");

    /* A resend of the same value is still a server update, and still stamps:
     * the reference rings it outside its equality check. */
    p = packet(PKT_NAME_VARP_SMALL);
    p._varp_small.variable = 42;
    p._varp_small.value = 5;
    apply(fx, &p);

    n = take_events(fx, ev, 8);
    CHECK(n == 3, "one varp_changed per applied update");
    CHECK(ev[0].kind == RS_EVENT_VARP_CHANGED && ev[0].a == 42 && ev[0].b == 5,
          "varp_changed carries the id and the value written");
    CHECK(ev[1].a == 43 && ev[1].b == 0x12345678, "in arrival order");
    CHECK(ev[2].a == 42, "the resend stamps again");
    CHECK(ev[0].serial + 1 == ev[1].serial, "consecutive serials");

    /* VARP_SYNC restores the server's copy over a script's optimistic write and
     * stamps each varp it restored -- only those. */
    VarPManager_SetVarpOptimistic(&fx->varps, 42, 9);
    p = packet(PKT_NAME_VARP_SYNC);
    apply(fx, &p);
    CHECK(VarPManager_GetVarp(&fx->varps, 42) == 5, "VARP_SYNC restores the server value");
    n = take_events(fx, ev, 8);
    CHECK(n == 1 && ev[0].a == 42 && ev[0].b == 5, "VARP_SYNC stamps the one varp it restored");

    p = packet(PKT_NAME_VARP_RESET);
    apply(fx, &p);
    CHECK(VarPManager_GetVarp(&fx->varps, 43) == 0, "VARP_RESET zeroes the varps");
    n = take_events(fx, ev, 8);
    CHECK(n == 1 && ev[0].a == -1, "VARP_RESET stamps id -1, every varp");

    fixture_free(fx);
    printf("ok - varps: values, and varp_changed per server update in arrival order\n");
}

static void
test_stats(void)
{
    struct Fixture* fx = fixture_new();
    struct RevPacket p;

    p = packet(PKT_NAME_UPDATE_STAT);
    p._update_stat.stat = 0;
    p._update_stat.xp = 37224;
    p._update_stat.level = 40;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "UPDATE_STAT applied");
    CHECK(fx->stats.current_level[0] == 40, "the boosted level is the wire's");
    CHECK(fx->stats.xp[0] == 37224, "and the xp");
    CHECK(fx->stats.base_level[0] == 40, "the base level is derived from the xp");

    p._update_stat.stat = RS_PLAYER_STATS_SKILL_COUNT;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "a stat past the table is not held");

    p = packet(PKT_NAME_UPDATE_RUNENERGY);
    p._update_run_energy.run_energy = 73;
    p._update_run_energy.run_energy_raw = 7300;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a new run energy is a change");
    CHECK(fx->stats.run_energy == 73, "UPDATE_RUNENERGY applied");
    p._update_run_energy.run_energy_raw = 7350;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "the same percentage is not");
    CHECK(fx->stats.run_energy_raw == 7350, "but the hundredths are kept either way");

    p = packet(PKT_NAME_UPDATE_RUNWEIGHT);
    p._update_runweight.run_weight = 12;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a new weight is a change");
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "a repeat is not");
    CHECK(fx->stats.run_weight == 12, "UPDATE_RUNWEIGHT applied");

    fixture_free(fx);
    printf("ok - stats: levels, xp, energy and weight, with the unchanged cases told apart\n");
}

static void
test_inventories(void)
{
    struct Fixture* fx = fixture_new();
    struct RS_Event ev[16];
    struct RevPacket p;
    int n;

    /* Keyed by component id on a wire with no inventory id. */
    {
        int obj_ids[3] = { 995, 1333, 4151 };
        int obj_counts[3] = { 10000, 1, 1 };

        p = packet(PKT_NAME_UPDATE_INV_FULL);
        p._update_inv_full.component_id = 3214;
        p._update_inv_full.size = 3;
        p._update_inv_full.obj_ids = obj_ids;
        p._update_inv_full.obj_counts = obj_counts;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "UPDATE_INV_FULL applied");
        CHECK(InvManager_GetObj(&fx->invs, 3214, 0) == 995, "slot 0");
        CHECK(InvManager_GetObj(&fx->invs, 3214, 2) == 4151, "slot 2");
        n = take_events(fx, ev, 16);
        CHECK(n == 1 && ev[0].kind == RS_EVENT_INV_PACKET && ev[0].a == 3214,
              "inv_packet names the container");
    }

    /* A partial to an inv whose id is also a small SOURCE index lands in that
     * inv, not in the source's container (recording
     * mrobertevers/20261115-170422). */
    {
        int const worn_source = InvManager_ResolveSource(&fx->invs, INV_MANAGER_SOURCE_NAME_WORN);
        int const shop_inv = worn_source;
        int worn_ids[4] = { -1, -1, -1, 1351 };
        int worn_counts[4] = { 0, 0, 0, 1 };
        int shop_ids[4] = { 1351, 1349, 1353, 1353 };
        int shop_counts[4] = { 5, 3, 2, 3 };
        struct PktUpdateInvPartialEntry entry = { .slot = 3, .obj_id = 1353, .count = 2 };

        CHECK(worn_source >= 0, "the worn source resolves");
        p = packet(PKT_NAME_UPDATE_INV_FULL);
        p._update_inv_full.inv_id = INV_MANAGER_CONTAINER_WORN;
        p._update_inv_full.size = 4;
        p._update_inv_full.obj_ids = worn_ids;
        p._update_inv_full.obj_counts = worn_counts;
        apply(fx, &p);
        p = packet(PKT_NAME_UPDATE_INV_FULL);
        p._update_inv_full.inv_id = shop_inv;
        p._update_inv_full.size = 4;
        p._update_inv_full.obj_ids = shop_ids;
        p._update_inv_full.obj_counts = shop_counts;
        apply(fx, &p);
        (void)take_events(fx, ev, 16);

        p = packet(PKT_NAME_UPDATE_INV_PARTIAL);
        p._update_inv_partial.inv_id = shop_inv;
        p._update_inv_partial.count = 1;
        p._update_inv_partial.entries = &entry;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "UPDATE_INV_PARTIAL applied");
        CHECK(InvManager_GetObj(&fx->invs, INV_MANAGER_CONTAINER_WORN, 3) == 1351,
              "the weapon slot keeps the bronze axe the server put there");
        CHECK(InvManager_GetNum(&fx->invs, shop_inv, 3) == 2, "the shop's stock went 3 -> 2");
        n = take_events(fx, ev, 16);
        CHECK(n == 1 && ev[0].kind == RS_EVENT_INV_PACKET && ev[0].a == shop_inv,
              "the partial stamps inv_packet once");

        p = packet(PKT_NAME_UPDATE_INV_STOP_TRANSMIT);
        p._update_inv_stop_transmit.inv_id = shop_inv;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "UPDATE_INV_STOP_TRANSMIT applied");
        CHECK(InvManager_GetObj(&fx->invs, shop_inv, 0) <= INV_MANAGER_EMPTY_OBJ_ID,
              "STOP_TRANSMIT empties the container");
    }

    fixture_free(fx);
    printf("ok - inventories: full, partial and stop-transmit, inv_packet per packet\n");
}

static void
test_interface_stores(void)
{
    struct Fixture* fx = fixture_new();
    int const com = (12 << 16) | 3;
    char text[] = "Congratulations";
    struct RevPacket p;
    unsigned events = 0;

    /* Nothing is mounted -- there is no tree at all -- and every value is
     * still held: the record is written at packet time. */
    p = packet(PKT_NAME_IF_SETTEXT);
    p._if_settext.component_id = com;
    p._if_settext.text = text;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "IF_SETTEXT applied");
    CHECK(UIIfTextStore_Get(&fx->if_texts, com) &&
              strcmp(UIIfTextStore_Get(&fx->if_texts, com), "Congratulations") == 0,
          "IF_SETTEXT is stored for an unmounted component");

    p = packet(PKT_NAME_IF_SETHIDE);
    p._if_sethide.component_id = com;
    p._if_sethide.hide = 5;
    apply(fx, &p);
    CHECK(UIIfIntStore_Get(&fx->if_hides, com, -1) == 1, "IF_SETHIDE stores a 0/1");

    p = packet(PKT_NAME_IF_SETCOLOUR);
    p._if_setcolour.component_id = com;
    p._if_setcolour.colour = (31 << 10) | (0 << 5) | 16;
    apply(fx, &p);
    CHECK(UIIfIntStore_Get(&fx->if_colours, com, -1) == 0xf80080,
          "IF_SETCOLOUR stores RGB888 from the 15-bit wire colour");

    p = packet(PKT_NAME_IF_SETEVENTS);
    p._if_setevents.component_id = com;
    p._if_setevents.from = 0;
    p._if_setevents.to = 27;
    p._if_setevents.events = 0x3e;
    apply(fx, &p);
    CHECK(UIIfEventTable_Lookup(&fx->if_events, com, 5, &events) && events == 0x3e,
          "IF_SETEVENTS arms the slot range");

    fixture_free(fx);
    printf("ok - interface stores: text, hide, colour and events held with no tree\n");
}

/* RS_ServerState_SlotGroupLeaves: the reference's close (class415.method9520)
 * drops a group's arming every time, its unload (Statics.method7193) drops its
 * texts, hides and colours only when another group takes the slot. Both
 * clients call it: the real client's app_close_slot, the headless client's
 * mount table (scriptrun_core.c IF_OPENSUB / IF_CLOSESUB). */
static void
test_slot_group_leaves(void)
{
    struct Fixture* fx = fixture_new();
    int const com = (233 << 16) | 36;
    int const other = (162 << 16) | 5;
    char text[] = "Congratulations";
    struct RevPacket p;
    unsigned events = 0;

    p = packet(PKT_NAME_IF_SETTEXT);
    p._if_settext.component_id = com;
    p._if_settext.text = text;
    apply(fx, &p);
    p._if_settext.component_id = other;
    apply(fx, &p);
    p = packet(PKT_NAME_IF_SETHIDE);
    p._if_sethide.component_id = com;
    p._if_sethide.hide = 1;
    apply(fx, &p);
    p = packet(PKT_NAME_IF_SETCOLOUR);
    p._if_setcolour.component_id = com;
    p._if_setcolour.colour = 31;
    apply(fx, &p);
    p = packet(PKT_NAME_IF_SETEVENTS);
    p._if_setevents.component_id = com;
    p._if_setevents.from = 0;
    p._if_setevents.to = 28;
    p._if_setevents.events = 0x1;
    apply(fx, &p);

    /* The same group reopened in its own slot: the arming goes, the stores
     * stay. */
    CHECK(RS_ServerState_SlotGroupLeaves(&fx->state, 233, 233) == 0,
          "a same-group remount unloads nothing");
    CHECK(!UIIfEventTable_Lookup(&fx->if_events, com, 0, &events), "but the arming is dropped");
    CHECK(UIIfTextStore_Get(&fx->if_texts, com) != NULL, "the text stays");
    CHECK(UIIfIntStore_Get(&fx->if_hides, com, -1) == 1, "the hide stays");
    CHECK(UIIfIntStore_Get(&fx->if_colours, com, -1) >= 0, "the colour stays");

    /* Another group takes the slot (or a close, incoming 0): all of it goes,
     * and only that group's. */
    CHECK(RS_ServerState_SlotGroupLeaves(&fx->state, 233, 0) == 1, "a close unloads the group");
    CHECK(UIIfTextStore_Get(&fx->if_texts, com) == NULL, "its text is dropped");
    CHECK(UIIfIntStore_Get(&fx->if_hides, com, -1) == -1, "its hide is dropped");
    CHECK(UIIfIntStore_Get(&fx->if_colours, com, -1) == -1, "its colour is dropped");
    CHECK(UIIfTextStore_Get(&fx->if_texts, other) != NULL, "another group's text is kept");

    fixture_free(fx);
    printf("ok - slot group leaves: arming always, stores only for another group\n");
}

static void
test_chat(void)
{
    struct Fixture* fx = fixture_new();
    struct RS_Event ev[8];
    struct RevPacket p;
    char text[] = "A goblin appears.";
    char echo[] = "a <b> tag";
    int n;

    p = packet(PKT_NAME_MESSAGE_GAME);
    p._message_game.text = text;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "MESSAGE_GAME applied");
    CHECK(fx->chat.message_count == 1, "one line in the store");
    CHECK(strcmp(fx->chat.messages[0].text, "A goblin appears.") == 0, "the text");
    CHECK(fx->chat.newest && fx->chat.newest->clock == 4321, "stamped with the owner's clock");
    n = take_events(fx, ev, 8);
    CHECK(n == 1 && ev[0].kind == RS_EVENT_CHAT_MESSAGE, "chat_message from the store");

    /* A line as long as a VARU8 MESSAGE_GAME allows is kept whole: the
     * reference keeps the String (class76.method2620). The store's buffer was
     * 200 and cut a 200-character chat-head line to 199 (doric, runemysteries:
     * the headless shadow's report 1, M1). */
    {
        char long_text[256];

        memset(long_text, 'a', 254);
        long_text[0] = 'D';
        long_text[254] = '\0';
        p = packet(PKT_NAME_MESSAGE_GAME);
        p._message_game.type = 114;
        p._message_game.text = long_text;
        apply(fx, &p);
        CHECK(strlen(fx->chat.messages[0].text) == 254, "a 254-character line keeps every character");
        CHECK(strlen(fx->chat.newest->text) == 254, "in the per-type store too");
        n = take_events(fx, ev, 8);
        CHECK(n == 1, "one chat_message for it");
    }

    /* A private message is deduped by id: the second copy writes nothing. */
    p = packet(PKT_NAME_MESSAGE_PRIVATE);
    snprintf(p._message_private.sender, sizeof(p._message_private.sender), "bob");
    p._message_private.message_id = 77;
    p._message_private.text = text;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "the first copy is a line");
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "the second is a duplicate");
    CHECK(fx->chat.newest->type == RS_CHAT_TYPE_PRIVATE_FROM, "typed as a private message");
    n = take_events(fx, ev, 8);
    CHECK(n == 1, "and stamps one chat_message");

    p = packet(PKT_NAME_MESSAGE_PRIVATE_ECHO);
    p._message_private_echo.text = echo;
    snprintf(p._message_private_echo.recipient, sizeof(p._message_private_echo.recipient), "%s",
             "bob");
    apply(fx, &p);
    /* Sentence-cased, then escaped (deob class243.field2949:
     * class671.method14451(Statics.method10915(text))). This pinned the
     * uncased "a <lt>b<gt> tag". */
    CHECK(strcmp(fx->chat.newest->text, "A <lt>b<gt> tag") == 0,
          "the echo is sentence-cased and its tags draw as characters");
    {
        char shouty[] = "hI tHERE. wHAT? oK";

        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "bob");
        p._message_private.message_id = 79;
        p._message_private.text = shouty;
        apply(fx, &p);
        CHECK(strcmp(fx->chat.newest->text, "Hi there. What? Ok") == 0,
              "a private line is sentence-cased (class243.field2953)");
        (void)take_events(fx, ev, 8);
    }

    /* The reference's tutorial gate (deob Statics.method6894 -> field765):
     * with the local player on Tutorial Island no friend-channel or private
     * line is shown, nor remembered as seen; off it, the same line is. */
    {
        struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
        char fc_text[] = "hello channel";
        int const count = fx->chat.message_count;
        int self_idx;

        CHECK(RS_Chat_InTutorialArea(3094, 3107), "the island's start");
        CHECK(RS_Chat_InTutorialArea(3100, 9510), "the cave under it");
        CHECK(!RS_Chat_InTutorialArea(3222, 3218), "Lumbridge is not");
        CHECK(!RS_Chat_InTutorialArea(3150, 3060), "nor the mainland corner the box reaches");
        fx->world->_base_tile_x = 3048;
        fx->world->_base_tile_z = 3072;
        self_idx = World_PlayerSpawn(fx->world, 40, 0, 46, 35, idle);
        RS_EntitySync_RegisterPlayer(&fx->esync, 5, 40, self_idx);
        fx->esync.local_pid = 5;
        p = packet(PKT_NAME_MESSAGE_FRIENDCHANNEL);
        snprintf(p._message_friendchannel.sender, sizeof(p._message_friendchannel.sender), "%s", "carol");
        p._message_friendchannel.channel_name = strtobase37("chan");
        p._message_friendchannel.world = 301;
        p._message_friendchannel.counter = 9;
        p._message_friendchannel.text = fc_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "on the island the line is not shown");
        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "dave");
        p._message_private.message_id = 78;
        p._message_private.text = fc_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "nor a private one");
        CHECK(fx->chat.message_count == count, "nothing reached the store");
        /* PLAYER_INFO's public chat, the real client's rule (deob
         * class95.method3131): nobody is heard on the island either. */
        CHECK(!RS_ServerState_PublicChatShown(&fx->state, "carol", true), "nor a public line on the island");
        fx->world->_base_tile_x = 3176;
        fx->world->_base_tile_z = 3176;
        p = packet(PKT_NAME_MESSAGE_FRIENDCHANNEL);
        snprintf(p._message_friendchannel.sender, sizeof(p._message_friendchannel.sender), "%s", "carol");
        p._message_friendchannel.channel_name = strtobase37("chan");
        p._message_friendchannel.world = 301;
        p._message_friendchannel.counter = 9;
        p._message_friendchannel.text = fc_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "off it the same line is, not seen before");
        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "dave");
        p._message_private.message_id = 78;
        p._message_private.text = fc_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "and the private one");
        CHECK(RS_ServerState_PublicChatShown(&fx->state, "carol", true), "and a public line off it");
        CHECK(!RS_ServerState_PublicChatShown(&fx->state, NULL, true), "but not from a player with no name yet");
        RS_Social_AddIgnore(&fx->social, "Carol");
        CHECK(!RS_ServerState_PublicChatShown(&fx->state, "carol", true),
            "an ignored speaker with an ignorable crown is not heard");
        CHECK(RS_ServerState_PublicChatShown(&fx->state, "carol", false),
            "a crown the ignore list cannot silence is (class278.field3449)");
        RS_Social_DelIgnore(&fx->social, "carol");
        (void)take_events(fx, ev, 8);
    }

    p = packet(PKT_NAME_P_COUNTDIALOG);
    fx->chat.dialog_input[0] = 'x';
    apply(fx, &p);
    CHECK(fx->chat.dialog_input_open == 1 && fx->chat.dialog_input[0] == '\0',
          "P_COUNTDIALOG opens an empty count prompt");

    fixture_free(fx);
    printf("ok - chat: lines, the clock, dedupe, escaping and the count prompt\n");
}

static void
test_social(void)
{
    struct Fixture* fx = fixture_new();
    struct RevPacket p;
    struct PktUpdateIgnoreListEntry ignores[2];
    struct PktUpdateFriendListEntry friend_entry;

    memset(&friend_entry, 0, sizeof(friend_entry));
    snprintf(friend_entry.name, sizeof(friend_entry.name), "alice");
    friend_entry.world = 302;
    friend_entry.rank = 2;
    p = packet(PKT_NAME_UPDATE_FRIENDLIST);
    p._update_friendlist.count = 1;
    p._update_friendlist.entries = &friend_entry;
    apply(fx, &p);
    CHECK(fx->social.server_status == 2, "a friend update marks the list loaded");
    CHECK(fx->social.friend_count == 1, "the friend is added");
    friend_entry.world = 330;
    apply(fx, &p);
    CHECK(fx->social.friend_count == 1, "a second update for her is an update");
    CHECK(RS_Social_FriendWorld(&fx->social, 0) == 330, "of her world");

    memset(ignores, 0, sizeof(ignores));
    snprintf(ignores[0].name, sizeof(ignores[0].name), "carl");
    snprintf(ignores[1].name, sizeof(ignores[1].name), "dave");
    p = packet(PKT_NAME_UPDATE_IGNORELIST);
    p._update_ignorelist.count = 2;
    p._update_ignorelist.entries = ignores;
    apply(fx, &p);
    CHECK(fx->social.ignore_count == 2, "both ignores are added");

    /* A private message from an ignored name is not shown, and its id is
     * not remembered (deob client.java class243.field2953: the ignore test,
     * Statics.field818.method1168, sets the same `seen` flag the id ring does,
     * for any crown). Off the list, the same message is a line. */
    {
        char pm_text[] = "psst";
        int const count = fx->chat.message_count;

        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "dave");
        p._message_private.message_id = 91;
        p._message_private.text = pm_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "an ignored sender's private line is dropped");
        CHECK(fx->chat.message_count == count, "nothing reached the store");
        /* Dave taken off the list: a removal entry (class428.method9746's
         * leading 4). */
        ignores[1].removed = 1;
        p = packet(PKT_NAME_UPDATE_IGNORELIST);
        p._update_ignorelist.count = 1;
        p._update_ignorelist.entries = &ignores[1];
        apply(fx, &p);
        CHECK(fx->social.ignore_count == 1, "a removal entry takes one name off");
        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "dave");
        p._message_private.message_id = 91;
        p._message_private.text = pm_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "unignored, the same message is shown: its id was not kept");
        ignores[1].removed = 0;
        p = packet(PKT_NAME_UPDATE_IGNORELIST);
        p._update_ignorelist.count = 1;
        p._update_ignorelist.entries = &ignores[1];
        apply(fx, &p);
        CHECK(fx->social.ignore_count == 2, "and an add puts it back beside the other");
    }

    /* A private line as the reference files it (deob client.java
     * class243.field2953): typed 7 only for a staff crown (class278.field3430:
     * crowns 1 and 2), 3 for every other crown; the name carries the crown's
     * image (method6324(field3451) + name); the text's tags are escaped
     * (class671.method14451), as the echo's are. */
    {
        char pm_text[] = "a <col=ff0000>red</col> word";

        p = packet(PKT_NAME_MESSAGE_PRIVATE);
        snprintf(p._message_private.sender, sizeof(p._message_private.sender), "iron_erin");
        p._message_private.message_id = 92;
        p._message_private.staff_mod = 3; /* an ironman's crown */
        p._message_private.text = pm_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "an ironman's private line is shown");
        CHECK(fx->chat.newest->type == RS_CHAT_TYPE_PRIVATE_FROM, "typed as a private line, not a mod's");
        CHECK(strncmp(fx->chat.newest->name, "<img=2>", 7) == 0, "named with the crown's image");
        CHECK(strcmp(fx->chat.newest->text, "A <lt>col=ff0000<gt>red<lt>/col<gt> word") == 0,
            "its tags draw as characters");
        p._message_private.message_id = 93;
        p._message_private.staff_mod = 2; /* a Jagex moderator's */
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a moderator's private line is shown");
        CHECK(fx->chat.newest->type == RS_CHAT_TYPE_PRIVATE_FROM_MOD, "typed as a mod's");
        CHECK(strncmp(fx->chat.newest->name, "<img=1>", 7) == 0, "with the moderator's crown");
    }

    /* A game message naming an ignored player is not shown either (deob
     * client.java class243.field2978: the name, when the packet has one, is
     * put to the same ignore test); one with no name, or another name, is. */
    {
        char gm_text[] = "wishes to trade with you.";
        char ignored_name[] = "Dave";
        char other_name[] = "Erin";
        int const count = fx->chat.message_count;

        p = packet(PKT_NAME_MESSAGE_GAME);
        p._message_game.type = 101;
        p._message_game.name = ignored_name;
        p._message_game.text = gm_text;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "a game message from an ignored name is dropped");
        CHECK(fx->chat.message_count == count, "nothing reached the store");
        p._message_game.name = other_name;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "another name's is shown");
        p._message_game.name = NULL;
        CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "and one with no name");
    }

    p = packet(PKT_NAME_FRIENDLIST_LOADED);
    p._friendlist_loaded.status = 1;
    apply(fx, &p);
    CHECK(fx->social.server_status == 1, "FRIENDLIST_LOADED sets the status");

    /* A revision that sends no private byte marks it absent: the client's
     * own copy stands. */
    p = packet(PKT_NAME_CHAT_FILTER_SETTINGS);
    p._chat_filter_settings.chat_public_mode = 1;
    p._chat_filter_settings.chat_private_mode = -1;
    p._chat_filter_settings.chat_trade_mode = 2;
    apply(fx, &p);
    CHECK(fx->chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC] == 1, "public");
    CHECK(fx->chat_filter_mode[RS_UI_CHAT_FILTER_TRADE] == 2, "trade");
    CHECK(fx->chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE] == 7, "private left alone");

    fixture_free(fx);
    printf("ok - social: friends, ignores, the loaded status and the filters\n");
}

static void
test_session(void)
{
    struct Fixture* fx = fixture_new();
    struct RevPacket p;
    char op[] = "Follow";

    p = packet(PKT_NAME_UPDATE_PID);
    p._update_pid.local_player_index = 9;
    apply(fx, &p);
    CHECK(fx->esync.local_pid == 9, "UPDATE_PID");

    p = packet(PKT_NAME_LAST_LOGIN_INFO);
    p._last_login_info.last_ip = 0x7f000001;
    p._last_login_info.days_since_recovery = 201;
    p._last_login_info.member_warning = 1;
    apply(fx, &p);
    CHECK(fx->session_info.welcome.last_ip == 0x7f000001, "LAST_LOGIN_INFO address");
    CHECK(fx->session_info.welcome.member_warning == 1, "and member warning");

    p = packet(PKT_NAME_UPDATE_REBOOT_TIMER);
    p._update_reboot_timer.ticks = 100;
    apply(fx, &p);
    CHECK(fx->session_info.reboot_timer == 100 * APP_SERVER_TICK_LOGIC_CYCLES,
          "UPDATE_REBOOT_TIMER counts client cycles");

    p = packet(PKT_NAME_SET_MULTIWAY);
    p._set_multiway.multiway = 1;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "multiway changed");
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "multiway held");

    p = packet(PKT_NAME_ENABLE_TRACKING);
    apply(fx, &p);
    CHECK(fx->session_info.tracking_enabled == 1, "ENABLE_TRACKING");

    p = packet(PKT_NAME_SET_PLAYER_OP);
    p._set_player_op.slot = 2;
    p._set_player_op.text = op;
    apply(fx, &p);
    CHECK(strcmp(fx->player_ops.text[1], "Follow") == 0, "SET_PLAYER_OP slot 2 is row 1");
    /* The text "null" (any case) is no op: the reference clears the slot
     * (deob client.java class243.field2940: equalsIgnoreCase(class280.field3641,
     * "null") -> null), which is how Jagex takes "Attack" away again. */
    {
        char null_op[] = "Null";

        p._set_player_op.text = null_op;
        apply(fx, &p);
        CHECK(fx->player_ops.text[1][0] == '\0', "SET_PLAYER_OP \"null\" clears the row");
        p._set_player_op.text = op;
    }
    p._set_player_op.slot = 6;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "slot 6 is not a row");

    p = packet(PKT_NAME_LOGOUT_TRANSFER);
    p._logout_transfer.world_id = 330;
    apply(fx, &p);
    CHECK(fx->session_end.hop_pending && fx->session_end.hop_world == 330,
          "LOGOUT_TRANSFER arms the hop");

    p = packet(PKT_NAME_SET_NPC_UPDATE_ORIGIN);
    p._set_npc_update_origin.x = 40;
    p._set_npc_update_origin.z = 41;
    apply(fx, &p);
    CHECK(fx->npc_update_origin.valid && fx->npc_update_origin.x == 40,
          "SET_NPC_UPDATE_ORIGIN");

    p = packet(PKT_NAME_HIDENPCOPS);
    p._hide_ops.hidden = 1;
    apply(fx, &p);
    p = packet(PKT_NAME_HIDEOBJOPS);
    p._hide_ops.hidden = 1;
    apply(fx, &p);
    CHECK(fx->world->hidden_ops.npc && fx->world->hidden_ops.obj && !fx->world->hidden_ops.loc,
          "HIDE*OPS each set their own flag");

    /* HINT_ARROW: the wire's own type numbers, folded on the way in. */
    p = packet(PKT_NAME_HINT_ARROW);
    p._hint_arrow.type = 1;
    p._hint_arrow.id = 41;
    apply(fx, &p);
    CHECK(fx->hint_arrow.type == RS_HINT_ARROW_NPC && fx->hint_arrow.target == 41,
          "type 1 is an npc slot");
    p._hint_arrow.type = 2;
    p._hint_arrow.id = 3200;
    p._hint_arrow.z = 3300;
    p._hint_arrow.height = 60;
    apply(fx, &p);
    CHECK(fx->hint_arrow.type == RS_HINT_ARROW_COORD && fx->hint_arrow.tile_z == 3300 &&
              fx->hint_arrow.offset_x == 64 && fx->hint_arrow.offset_z == 64,
          "type 2 is an absolute tile anchored at its centre");
    p._hint_arrow.type = 255;
    apply(fx, &p);
    CHECK(fx->hint_arrow.type == 0, "255 is the clear");

    fixture_free(fx);
    printf("ok - session: pid, welcome, reboot, multiway, ops, hop, origin, hidden ops, hint\n");
}

static void
test_active_world_and_fence(void)
{
    struct Fixture* fx = fixture_new();
    struct RS_Event ev[8];
    struct RevPacket p;
    static char dummy_world[1];
    static char dummy_builder[1];
    int n;

    /* The registry holds dummy pointers: the model only checks liveness. */
    WorldviewRegistry_RegisterRoot(
        &fx->worldviews, (struct World*)dummy_world, (struct WorldBuilder*)dummy_builder);
    WorldviewRegistry_Register(
        &fx->worldviews,
        3,
        2009,
        (struct World*)dummy_world,
        (struct WorldBuilder*)dummy_builder,
        6400,
        6400,
        8,
        8,
        WORLDVIEW_ROOT);

    p = packet(PKT_NAME_SET_ACTIVE_WORLD);
    p._set_active_world.world_id = 2009;
    p._set_active_world.level = 1;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a live hull is accepted");
    CHECK(WorldviewRegistry_Active(&fx->worldviews) == 3, "the cursor is its slot");
    CHECK(WorldviewRegistry_ActiveLevel(&fx->worldviews) == 1, "with its plane");
    p._set_active_world.world_id = 3;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "a slot number is not a wire id");
    CHECK(WorldviewRegistry_Active(&fx->worldviews) == 3, "the cursor stays");

    /* The interaction modes address views the same way. */
    p = packet(PKT_NAME_SET_INTERACTION_MODE);
    p._interaction_mode.world_id = 2009;
    p._interaction_mode.tile_mode = 2;
    p._interaction_mode.entity_mode = 3;
    apply(fx, &p);
    CHECK(RS_InteractionMode_Tile(&fx->interaction_mode, 3) == 2, "the hull's tile mode");
    CHECK(RS_InteractionMode_Entity(&fx->interaction_mode, 3) == 3, "and entity mode");

    /* A hull the client has not spawned yet: the pair is kept by its wire id
     * (deob class61.method1382 keys by world id) and the view takes it when it
     * is created, instead of the packet being refused. */
    p = packet(PKT_NAME_SET_INTERACTION_MODE);
    p._interaction_mode.world_id = 2010;
    p._interaction_mode.tile_mode = 0;
    p._interaction_mode.entity_mode = 2;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a mode for a hull not spawned yet is kept");
    CHECK(fx->interaction_mode.held_count == 1, "held by its wire id");
    WorldviewRegistry_Register(
        &fx->worldviews,
        4,
        2010,
        (struct World*)dummy_world,
        (struct WorldBuilder*)dummy_builder,
        6464,
        6400,
        8,
        8,
        WORLDVIEW_ROOT);
    CHECK(RS_InteractionMode_TakeHeld(&fx->interaction_mode, 2010, 4) == 1, "the new view takes it");
    CHECK(RS_InteractionMode_Tile(&fx->interaction_mode, 4) == 0, "the hull's tile mode, held");
    CHECK(RS_InteractionMode_Entity(&fx->interaction_mode, 4) == 2, "and its entity mode");
    CHECK(fx->interaction_mode.held_count == 0, "and nothing is held any more");
    /* A reset for a hull not spawned yet forgets what was held for it. */
    p = packet(PKT_NAME_SET_INTERACTION_MODE);
    p._interaction_mode.world_id = 2011;
    p._interaction_mode.tile_mode = 2;
    p._interaction_mode.entity_mode = 0;
    apply(fx, &p);
    p = packet(PKT_NAME_RESET_INTERACTION_MODE);
    p._interaction_mode.world_id = 2011;
    apply(fx, &p);
    CHECK(fx->interaction_mode.held_count == 0, "a reset before the spawn forgets the held pair");
    CHECK(RS_InteractionMode_TakeHeld(&fx->interaction_mode, 2011, 5) == 0, "so the view takes nothing");
    CHECK(RS_InteractionMode_Tile(&fx->interaction_mode, 5) == 1, "and follows the defaults");

    fx->world->cycle = 900;
    (void)take_events(fx, ev, 8);
    p = packet(PKT_NAME_SERVER_TICK_END);
    apply(fx, &p);
    CHECK(WorldviewRegistry_Active(&fx->worldviews) == WORLDVIEW_ROOT,
          "the fence returns the cursor to the root");
    CHECK(RS_ServerTick_FenceSeen(&fx->server_tick), "and fences the tick");
    n = take_events(fx, ev, 8);
    CHECK(n == 1 && ev[0].kind == RS_EVENT_SERVER_TICK && ev[0].a == 900 && ev[0].cycle == 900,
          "server_tick carries the world cycle");

    fixture_free(fx);
    printf("ok - SET_ACTIVE_WORLD, interaction modes and the tick fence\n");
}

static void
test_entity_setters(void)
{
    struct Fixture* fx = fixture_new();
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct RS_Event ev[8];
    struct WorldEntity_NPC* npc;
    struct WorldEntity_Player* player;
    struct RevPacket p;
    int npc_idx;
    int player_idx;
    int group[WORLD_NPC_HEADICON_SLOTS];
    int index[WORLD_NPC_HEADICON_SLOTS];
    int n;

    npc_idx = World_NpcSpawn(fx->world, 11, 500, 0, 30, 30, 1, idle);
    RS_EntitySync_RegisterNpc(&fx->esync, 17, 11, npc_idx);
    player_idx = World_PlayerSpawn(fx->world, 12, 0, 25, 25, idle);
    RS_EntitySync_RegisterPlayer(&fx->esync, 7, 12, player_idx);
    fx->esync.local_pid = 7;
    fx->world->cycle = 300;

    p = packet(PKT_NAME_NPC_ANIM_SPECIFIC);
    p._npc_anim_specific.index = 17;
    p._npc_anim_specific.id = 808;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "a tracked npc takes the seq");
    npc = World_EntityPoolAt(&fx->world->entities.npc, npc_idx);
    CHECK(npc->seq_sent_id == 808, "NPC_ANIM_SPECIFIC sets the npc's seq");
    n = take_events(fx, ev, 8);
    CHECK(n == 1 && ev[0].kind == RS_EVENT_NPC_SEQ && ev[0].a == 17 && ev[0].b == 808 &&
              ev[0].c == 300 / APP_SERVER_TICK_LOGIC_CYCLES,
          "npc_seq names the server slot, the seq and the tick");
    p._npc_anim_specific.index = 18;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "an npc the record lacks is skipped");
    CHECK(take_events(fx, ev, 8) == 0, "and stamps nothing");

    p = packet(PKT_NAME_ANIM_SPECIFIC);
    p._anim_specific.id = 866;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "the local player takes ANIM_SPECIFIC");
    player = World_EntityPoolAt(&fx->world->entities.player, player_idx);
    CHECK(player->seq_sent_id == 866, "on the local pid");

    p = packet(PKT_NAME_NPC_SPOTANIM_SPECIFIC);
    p._entity_spotanim_specific.index = 17;
    p._entity_spotanim_specific.id = 85;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "NPC_SPOTANIM_SPECIFIC applied");
    CHECK(npc->spotanim_packet_id == 85, "the npc carries the spotanim");
    p = packet(PKT_NAME_PLAYER_SPOTANIM_SPECIFIC);
    p._entity_spotanim_specific.index = 99;
    p._entity_spotanim_specific.id = 85;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "an untracked player is skipped");

    /* A head icon slot written over icons the npc already carries keeps the
     * others (the type's icons are copied in only on the first write). */
    for( int i = 0; i < WORLD_NPC_HEADICON_SLOTS; i++ )
    {
        group[i] = 400 + i;
        index[i] = i;
    }
    World_NpcSetHeadiconCustom(fx->world, npc_idx, true, group, index);
    p = packet(PKT_NAME_NPC_HEADICON_SPECIFIC);
    p._npc_headicon_specific.npc_index = 17;
    p._npc_headicon_specific.slot = 1;
    p._npc_headicon_specific.group = 777;
    p._npc_headicon_specific.index = 3;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_APPLIED, "NPC_HEADICON_SPECIFIC applied");
    CHECK(npc->headicon_group[1] == 777 && npc->headicon_index[1] == 3, "the slot written");
    CHECK(npc->headicon_group[0] == 400, "the other slots kept");
    p._npc_headicon_specific.slot = WORLD_NPC_HEADICON_SLOTS;
    CHECK(apply(fx, &p) == RS_SERVER_STATE_UNCHANGED, "a slot past the table is refused");

    fixture_free(fx);
    printf("ok - entity setters: npc and player seqs, spotanims, head icons\n");
}

static void
test_not_the_models(void)
{
    struct Fixture* fx = fixture_new();
    enum GameProtoPktName const views[] = {
        PKT_NAME_IF_SETPOSITION, PKT_NAME_RUNCLIENTSCRIPT, PKT_NAME_IF_OPENSUB,
        PKT_NAME_LOC_ADD_CHANGE, PKT_NAME_OBJ_ADD,        PKT_NAME_CAM_MOVETO,
        PKT_NAME_UNSET_MAP_FLAG, PKT_NAME_MIDI_SONG,
    };

    for( size_t i = 0; i < sizeof(views) / sizeof(views[0]); i++ )
    {
        struct RevPacket p = packet(views[i]);
        CHECK(apply(fx, &p) == RS_SERVER_STATE_PASS, "a view or view-held packet passes");
    }
    CHECK(fx->ring.newest_serial == 0, "and stamps nothing");
    fixture_free(fx);
    printf("ok - presentation-only and view-held packets pass through untouched\n");
}

int
main(void)
{
    test_varps();
    test_stats();
    test_inventories();
    test_interface_stores();
    test_slot_group_leaves();
    test_chat();
    test_social();
    test_session();
    test_active_world_and_fence();
    test_entity_setters();
    test_not_the_models();
    printf("rs-server-state: all tests passed\n");
    return 0;
}
