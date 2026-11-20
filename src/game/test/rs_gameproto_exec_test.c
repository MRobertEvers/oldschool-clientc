/*
 * Exec seam against bare managers (no App / cache / disk): builds parsed
 * packets by hand and asserts they land in VarPManager / InvManager /
 * RS_PlayerStats / UITree / RS_Chat. The app-only branches (IF_OPEN*, world)
 * are exercised by the ui-slots and loopback tests.
 *
 * Also pins the REBUILD_NORMAL map-square residency predicate that used to
 * compare only the SW square and skip loads that needed an eastern square.
 */
#include "mem/torirs_mem.h"
#include "app.h"
#include "app/app_internal.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "game/rs_chat.h"
#include "game/rs_clientcode.h"
#include "app/app_gameproto_exec.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_player_stats.h"
#include "game/rs_server_state.h"
#include "game/rs_social.h"
#include "net/chat_huffman.h"
#include "net/jbase37.h"
#include "game/task_gameproto_exec.h"
#include "inv/inv_manager.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "render/cam_script.h"
#include "toridraw_math.h"
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

/* A mount of `group` into `target` was queued: its SUB_OPENED stamp. */
static int
queued_open(
    struct App const* app,
    int target,
    int group)
{
    for( uint32_t i = 0; i < app->drive_events.ring.count; i++ )
    {
        struct RS_Event const* e = &app->drive_events.entries[i];

        if( e->kind == RS_EVENT_SUB_OPENED && e->a == target && e->b == group )
            return 1;
    }
    return 0;
}

/* Wire bytes, as a rev-242 server writes them. */
static int
test_put_jstr(uint8_t* out, int at, char const* text)
{
    size_t const n = strlen(text);

    memcpy(out + at, text, n);
    out[at + (int)n] = 0;
    return at + (int)n + 1;
}

/* One UPDATE_FRIENDLIST entry (rsprot update_friendlist.c, deob
 * Statics.method10091): added, name, previous name, world, rank, properties,
 * the online-only world name, platform and flags, the notes. */
static int
test_put_friend_renamed(uint8_t* out, int at, int lead, char const* name, char const* previous, int world)
{
    out[at++] = (uint8_t)lead;
    at = test_put_jstr(out, at, name);
    at = test_put_jstr(out, at, previous);
    out[at++] = (uint8_t)(world >> 8);
    out[at++] = (uint8_t)world;
    out[at++] = 0;
    out[at++] = 0;
    if( world > 0 )
    {
        at = test_put_jstr(out, at, "");
        out[at++] = 0;
        memset(out + at, 0, 4);
        at += 4;
    }
    return test_put_jstr(out, at, "");
}

static int
test_put_friend(uint8_t* out, int at, char const* name, int world)
{
    return test_put_friend_renamed(out, at, 0, name, "", world);
}

/* One UPDATE_FRIENDLIST packet through the rev-242 reader and the model. */
static void
test_friend_packet(
    struct App* app,
    struct RS_ServerState* state,
    uint8_t const* wire,
    int len)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(242);
    struct RevPacket p;

    memset(&p, 0, sizeof(p));
    TEST_EXEC_ASSERT(
        rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_FRIENDLIST), (uint8_t*)wire, len, &p),
        "an UPDATE_FRIENDLIST parses");
    App_GameProto_Exec(app, state, &p);
    gameproto_free(&p);
}

/* The client cycles from `*clock` to `to`, the notices drained every one. */
static void
test_notice_cycles(struct RS_ServerState* state, int* clock, int to)
{
    while( *clock < to )
    {
        (*clock)++;
        RS_ServerState_FriendNotices(state->social, state->chat, *clock);
    }
}

/* One UPDATE_IGNORELIST entry (rsprot update_ignorelist.h, deob
 * class428.method9746): the flags byte, the name, and unless the flags say
 * "removed" (4), the previous name and the note. */
static int
test_put_ignore(uint8_t* out, int at, int flags, char const* name, char const* previous)
{
    out[at++] = (uint8_t)flags;
    at = test_put_jstr(out, at, name);
    if( flags & 0x4 )
        return at;
    at = test_put_jstr(out, at, previous);
    return test_put_jstr(out, at, "");
}

/* MESSAGE_PRIVATE (rsprot message_private.c): sender, world, the world's
 * message counter, the crown, the huffman text. */
static int
test_put_private(uint8_t* out, int cap, char const* sender, int counter, int crown, char const* text)
{
    int at = test_put_jstr(out, 0, sender);

    out[at++] = 0;
    out[at++] = 1;
    out[at++] = (uint8_t)(counter >> 16);
    out[at++] = (uint8_t)(counter >> 8);
    out[at++] = (uint8_t)counter;
    out[at++] = (uint8_t)crown;
    return at + chat_huffman_pack(out + at, cap - at, text);
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
        UIIfEventTable_Free(&event_app->if_events);
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

        UIIfEventTable_Free(&event_app->if_events);
        free(event_app);
        UITree_Free(event_tree);
        printf("ok - rev239 WidgetFlags override/fallback lookup\n");
    }

    /*
     * Closing a group out of a slot drops its IF_SETEVENTS, as rev239's one
     * close (class415.method9520 -> Statics.method12095) does on IF_CLOSESUB,
     * on IF_OPENSUB onto an occupied slot -- the SAME group included -- and on
     * the destination of IF_MOVESUB. Keeping them made this client answer
     * clicks the official one ignores: a quest list armed once at login was
     * dead in RuneLite after a journal tab switch and fine here.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        int const slot = (629 << 16) | 43;  /* side_journal:tab_container */
        int const list = (399 << 16) | 7;   /* questlist:list */
        int const icon = (629 << 16) | 8;   /* side_journal's own tab icon */
        int const source = (161 << 16) | 70;
        int const dest = (161 << 16) | 71;
        int const moved = (500 << 16) | 2;
        int const landed_on = (501 << 16) | 2;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->tree = UITree_New(2);
        UITree_InterfaceParentSet(app->tree, slot, 399, 1);
        App_IfEventsSet(app, list, 1, 200, 4);
        App_IfEventsSet(app, icon, 0, 0, 2);

        App_OpenSubInterface(app, slot, 399, 1);
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, list, 5) == 0,
                         "a same-group remount must drop the outgoing copy's arming");
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, icon, 0) == 2,
                         "a remount must not touch another group's arming");

        /* The server's re-arm is the next packet; the mount task still
         * queued from the open must not take it away again. */
        App_IfEventsSet(app, list, 1, 200, 4);
        App_CloseSubInterface(app, slot);
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, list, 5) == 0,
                         "IF_CLOSESUB must drop the closed group's arming");

        /* The tree still records 399 in the slot, but the close is queued:
         * the slot is empty in wire order, so opening into it closes nothing
         * and an arming for 399 that arrives meanwhile is left alone. */
        App_IfEventsSet(app, list, 1, 200, 4);
        App_OpenSubInterface(app, slot, 712, 1);
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, list, 5) == 4,
                         "an open into a slot whose close is queued must not drop the tree's stale group");

        /* IF_MOVESUB closes only what it lands on; the moved group is
         * relinked and keeps its arming (class415.method9481). */
        UITree_InterfaceParentSet(app->tree, source, 500, 1);
        UITree_InterfaceParentSet(app->tree, dest, 501, 1);
        App_IfEventsSet(app, moved, 0, 0, 2);
        App_IfEventsSet(app, landed_on, 0, 0, 2);
        App_MoveSubInterface(app, source, dest);
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, moved, 0) == 2,
                         "IF_MOVESUB must keep the moved group's arming");
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, landed_on, 0) == 0,
                         "IF_MOVESUB must drop the arming of the group it lands on");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        free(app);
        printf("ok - closing a group out of a slot drops its IF_SETEVENTS in wire order\n");
    }

    /*
     * A close that UNLOADS a group drops what the server set on it -- hides,
     * texts, colours -- as rev239's unload flag does (class415.method9520 ->
     * Statics.method7193 nulls the group's IfType.list): IF_CLOSESUB, an
     * IF_OPENSUB of a different group over it, the destination of an
     * IF_MOVESUB. A same-group remount keeps them. Keeping them past a close
     * drew the woodcutting level-up's tree in every later level-up box: the
     * server hides the icon again only after Continue, and a box closed by the
     * next tree click left "shown" stored for good.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        int const slot = (162 << 16) | 567;      /* synthetic chat modal slot */
        int const other_slot = (162 << 16) | 568;
        int const source = (161 << 16) | 70;
        int const dest = (161 << 16) | 71;
        int const wc_icon = (233 << 16) | 53;    /* levelup_display:woodcutting */
        int const title = (233 << 16) | 1;       /* levelup_display:text1 */
        int const moved = (500 << 16) | 2;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->tree = UITree_New(2);
        UITree_InterfaceParentSet(app->tree, slot, 233, 0);
        App_IfHideSet(app, wc_icon, 0);
        App_IfTextSet(app, title, "Congratulations, you just advanced a Woodcutting level.");
        App_IfColourSet(app, title, 0x000080);

        App_OpenSubInterface(app, slot, 233, 0);
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_hides, wc_icon, -1) == 0,
                         "a same-group remount must keep the group's hides");
        TEST_EXEC_ASSERT(UIIfTextStore_Get(&app->if_texts, title) != NULL,
                         "a same-group remount must keep the group's texts");

        App_CloseSubInterface(app, slot);
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_hides, wc_icon, -1) == -1,
                         "IF_CLOSESUB must drop the closed group's hides");
        TEST_EXEC_ASSERT(UIIfTextStore_Get(&app->if_texts, title) == NULL,
                         "IF_CLOSESUB must drop the closed group's texts");
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_colours, title, -1) == -1,
                         "IF_CLOSESUB must drop the closed group's colours");

        UITree_InterfaceParentSet(app->tree, other_slot, 233, 0);
        App_IfHideSet(app, wc_icon, 0);
        App_OpenSubInterface(app, other_slot, 231, 0);
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_hides, wc_icon, -1) == -1,
                         "an IF_OPENSUB of a different group must drop the outgoing group's hides");

        UITree_InterfaceParentSet(app->tree, source, 500, 1);
        UITree_InterfaceParentSet(app->tree, dest, 233, 1);
        App_IfHideSet(app, wc_icon, 0);
        App_IfHideSet(app, moved, 1);
        App_MoveSubInterface(app, source, dest);
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_hides, wc_icon, -1) == -1,
                         "IF_MOVESUB must drop the hides of the group it lands on");
        TEST_EXEC_ASSERT(UIIfIntStore_Get(&app->if_hides, moved, -1) == 1,
                         "IF_MOVESUB must keep the moved group's hides");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        UIIfIntStore_Free(&app->if_hides);
        UIIfIntStore_Free(&app->if_colours);
        UIIfTextStore_Free(&app->if_texts);
        free(app);
        printf("ok - a close that unloads a group drops its stored IF_SET* values\n");
    }

    /*
     * IF_OPENTOP, then IF_MOVESUB from the old top's slots. The deob keeps its
     * open subs across a top change and the move relinks each into the new
     * top (class415.method9481); App_OpenRootInterface keeps the outgoing
     * tree's mounts in kept_mounts for it. RSProx index 4377: 165 -> 164
     * moved the chatbox, the journal and the rest, and this client found none
     * of them -- every dialogue after it had no slot to open into.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        int const chat_src = (165 << 16) | 2;
        int const chat_dest = (164 << 16) | 93;
        int const journal_src = (165 << 16) | 19;
        int const journal_dest = (164 << 16) | 75;
        int const quest_slot = (629 << 16) | 43; /* side_journal:tab_container */
        int const modal_src = (165 << 16) | 14;
        int const modal_dest = (164 << 16) | 16;
        int const welcome = (378 << 16) | 5;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->tree = UITree_New(2);
        UITreeKeptMounts_Clear(&app->kept_mounts);
        UITreeKeptMounts_Add(&app->kept_mounts, chat_src, 162, 1);
        UITreeKeptMounts_Add(&app->kept_mounts, journal_src, 629, 1);
        UITreeKeptMounts_Add(&app->kept_mounts, quest_slot, 399, 1);

        App_MoveSubInterface(app, chat_src, chat_dest);
        App_MoveSubInterface(app, journal_src, journal_dest);
        TEST_EXEC_ASSERT(queued_open(app, chat_dest, 162),
                         "IF_MOVESUB after IF_OPENTOP must reopen the chatbox in the new top");
        TEST_EXEC_ASSERT(queued_open(app, journal_dest, 629),
                         "IF_MOVESUB after IF_OPENTOP must reopen the journal in the new top");
        TEST_EXEC_ASSERT(queued_open(app, quest_slot, 399),
                         "what was open inside a moved group must reopen with it");
        TEST_EXEC_ASSERT(UITreeKeptMounts_Count(&app->kept_mounts) == 0,
                         "a moved mount must leave the kept records");

        /* A move out of an empty slot still closes what it lands on. */
        UITree_InterfaceParentSet(app->tree, modal_dest, 378, 0);
        App_IfEventsSet(app, welcome, 0, 0, 2);
        App_MoveSubInterface(app, modal_src, modal_dest);
        TEST_EXEC_ASSERT(App_IfEventsGetAt(app, welcome, 0) == 0,
                         "IF_MOVESUB from an empty slot must close (disarm) its destination");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        free(app);
        printf("ok - IF_MOVESUB relinks the subs of a top IF_OPENTOP replaced\n");
    }

    /*
     * A mount the re-root does not move is kept, and comes back with its top.
     * The phone's hotkey column sits in toplevel_osm:hotkeys (601:40), a slot
     * toplevel_display does not have. RSProx index 4398: opened once at [36];
     * the world map switches to toplevel_display at [44] and back at [73], and
     * Jagex sends neither a close nor a second open -- its client keeps the
     * mount by component. This client dropped it at the first switch, so the
     * column was gone after the first look at the world map.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        int const hotkeys = (601 << 16) | 40;
        int const chat_osm = (601 << 16) | 49;
        int const chat_display = (165 << 16) | 2;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->tree = UITree_New(2);

        /* toplevel_osm is up; the switch to toplevel_display. */
        UITree_InterfaceParentSet(app->tree, chat_osm, 162, 1);
        UITree_InterfaceParentSet(app->tree, hotkeys, 892, 1);
        App_RootMountsDetach(app);
        UITree_Free(app->tree);
        app->tree = UITree_New(2);
        App_RootMountsReturn(app, 165);
        App_MoveSubInterface(app, chat_osm, chat_display);
        TEST_EXEC_ASSERT(UITreeKeptMounts_Count(&app->kept_mounts) == 1 &&
                             UITreeKeptMounts_At(&app->kept_mounts, 0)->container_uid == hotkeys &&
                             UITreeKeptMounts_At(&app->kept_mounts, 0)->group_id == 892,
                         "a mount the re-root does not move must stay kept");
        TEST_EXEC_ASSERT(!queued_open(app, hotkeys, 892),
                         "a kept mount must not open under a top that has no such slot");

        /* And back: the tree now holds what the moves put in it. */
        UITree_InterfaceParentSet(app->tree, chat_display, 162, 1);
        App_RootMountsDetach(app);
        UITree_Free(app->tree);
        app->tree = UITree_New(2);
        App_RootMountsReturn(app, 601);
        TEST_EXEC_ASSERT(queued_open(app, hotkeys, 892),
                         "a kept mount must reopen when its top is the root again");
        App_MoveSubInterface(app, chat_display, chat_osm);
        TEST_EXEC_ASSERT(queued_open(app, chat_osm, 162), "the moved mount must still follow its IF_MOVESUB");
        TEST_EXEC_ASSERT(UITreeKeptMounts_Count(&app->kept_mounts) == 0, "nothing is kept once both are back in the tree");

        /* A close names the slot whichever top is up. */
        UITree_InterfaceParentSet(app->tree, hotkeys, 892, 1);
        App_RootMountsDetach(app);
        UITree_Free(app->tree);
        app->tree = UITree_New(2);
        App_RootMountsReturn(app, 165);
        App_CloseSubInterface(app, hotkeys);
        TEST_EXEC_ASSERT(UITreeKeptMounts_Count(&app->kept_mounts) == 0, "IF_CLOSESUB must close a kept mount too");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        free(app);
        printf("ok - a mount a re-root does not move is kept and returns with its top\n");
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

    /* App_GameProto_Exec requires an App (fa5680f77). A section that needs
     * more than a zeroed one makes its own; this one has the tree and the
     * chat bound as app.c binds them. Its records are the App's
     * (App_ServerStateBind), with the managers, the chat and an event ring of
     * the test's own in place of the zeroed App's, so the checks below read
     * them. */
    static struct RS_Event bare_events[64];
    struct RS_EventRing bare_ring;
    struct RS_ServerState bare_state;
    struct RS_ServerState app_state;
    struct App* base_app = calloc(1, sizeof(*base_app));

    assert(base_app);
    base_app->tree = tree;
    RS_CS2Host_SetChat(&base_app->host, &chat);
    memset(&bare_ring, 0, sizeof(bare_ring));
    App_ServerStateBind(base_app, &bare_state);
    bare_state.varps = &varps;
    bare_state.stats = &stats;
    bare_state.invs = &invs;
    bare_state.chat = &chat;
    bare_state.rev_number = -1;
    bare_state.events = (struct RS_EventSink){
        .ring = &bare_ring,
        .entries = bare_events,
        .capacity = 64,
        .full = RS_EVENT_RING_OVERWRITE,
    };

    /* The App-free half (game/rs_gameproto_exec.c): the model, and the
     * tree-only arms. An interface setter's value is the model's store; with
     * no App nothing puts it on the node. */
    {
        struct RS_GameProtoCtx ctx = {
            .state = &bare_state,
            .tree = tree,
        };
        char text[] = "stored";
        struct RevPacket p;
        int32_t idx;

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_VARP_SMALL;
        p._varp_small.variable = 43;
        p._varp_small.value = 7;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(VarPManager_GetVarp(&varps, 43) == 7, "the model half applies a varp");

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_IF_SETTEXT;
        p._if_settext.component_id = 0x1234;
        p._if_settext.text = text;
        RS_GameProto_Exec(&ctx, &p);
        TEST_EXEC_ASSERT(
            strcmp(UIIfTextStore_Get(bare_state.if_texts, 0x1234), "stored") == 0,
            "IF_SETTEXT is the model's store");
        idx = UITree_FindByComponentId(tree, 0x1234);
        assert(idx >= 0);
        TEST_EXEC_ASSERT(
            strcmp(tree->components[idx].u.rs_text.text, "before") == 0,
            "with no App the node is left to the App's setter");
        printf("ok - the App-free half writes the model and the tree-only arms\n");
    }

    /* VARP_SMALL 42 = 5 */
    {
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_VARP_SMALL;
        p._varp_small.variable = 42;
        p._varp_small.value = 5;
        App_GameProto_Exec(base_app, &bare_state, &p);
        assert(VarPManager_GetVarp(&varps, 42) == 5);
        printf("ok - VARP_SMALL applied\n");
    }

    /* UPDATE_RUNENERGY 73 */
    {
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_RUNENERGY;
        p._update_run_energy.run_energy = 73;
        App_GameProto_Exec(base_app, &bare_state, &p);
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
        App_GameProto_Exec(base_app, &bare_state, &p);
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
        App_GameProto_Exec(base_app, &bare_state, &p);
        assert(InvManager_GetObj(&invs, 3214, 0) == 995);
        assert(InvManager_GetObj(&invs, 3214, 2) == 4151);
        printf("ok - UPDATE_INV_FULL applied\n");
    }

    /* UPDATE_INV_PARTIAL to an inv whose id is also a small SOURCE index lands
     * in that inv, not in the source's container. Recording
     * mrobertevers/20261115-170422: Bob's stock is inv 1, sources[1] was the
     * worn equipment, and buying a steel axe drew "steel axe x2" in the
     * weapon slot over the bronze axe the server still had there. */
    {
        int const worn_source = InvManager_ResolveSource(&invs, INV_MANAGER_SOURCE_NAME_WORN);
        int const shop_inv = worn_source; /* the collision, whatever the index */
        int worn_ids[4] = { -1, -1, -1, 1351 };
        int worn_counts[4] = { 0, 0, 0, 1 };
        int shop_ids[4] = { 1351, 1349, 1353, 1353 };
        int shop_counts[4] = { 5, 3, 2, 3 };
        struct PktUpdateInvPartialEntry entry = { .slot = 3, .obj_id = 1353, .count = 2 };
        struct RevPacket p;

        assert(worn_source >= 0);
        assert(shop_inv < INV_MANAGER_CONTAINER_BACKPACK);
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_INV_FULL;
        p._update_inv_full.inv_id = INV_MANAGER_CONTAINER_WORN;
        p._update_inv_full.size = 4;
        p._update_inv_full.obj_ids = worn_ids;
        p._update_inv_full.obj_counts = worn_counts;
        App_GameProto_Exec(base_app, &bare_state, &p);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_INV_FULL;
        p._update_inv_full.inv_id = shop_inv;
        p._update_inv_full.size = 4;
        p._update_inv_full.obj_ids = shop_ids;
        p._update_inv_full.obj_counts = shop_counts;
        App_GameProto_Exec(base_app, &bare_state, &p);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_INV_PARTIAL;
        p._update_inv_partial.inv_id = shop_inv;
        p._update_inv_partial.count = 1;
        p._update_inv_partial.entries = &entry;
        App_GameProto_Exec(base_app, &bare_state, &p);

        TEST_EXEC_ASSERT(InvManager_GetObj(&invs, INV_MANAGER_CONTAINER_WORN, 3) == 1351,
            "the weapon slot keeps the bronze axe the server put there");
        TEST_EXEC_ASSERT(InvManager_GetNum(&invs, INV_MANAGER_CONTAINER_WORN, 3) == 1,
            "with no stock count drawn over it");
        TEST_EXEC_ASSERT(InvManager_GetObj(&invs, shop_inv, 3) == 1353,
            "the shop's line is the steel axe");
        TEST_EXEC_ASSERT(InvManager_GetNum(&invs, shop_inv, 3) == 2,
            "and its stock went 3 -> 2");
        printf("ok - UPDATE_INV_PARTIAL to a small inv id stays in that inv\n");
    }

    /* IF_SETTEXT rewrites the TEXT node. */
    {
        char text[] = "after";
        struct RevPacket p;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_IF_SETTEXT;
        p._if_settext.component_id = 0x1234;
        p._if_settext.text = text;
        App_GameProto_Exec(base_app, &bare_state, &p);
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
        App_GameProto_Exec(base_app, &bare_state, &p);
        assert(chat.message_count == 1);
        assert(strcmp(chat.messages[0].text, "A goblin appears.") == 0);
        printf("ok - MESSAGE_GAME applied\n");
    }

    /* A private message from a friend reaches the private filter as a
     * friend's line. The recording-96 regression: a login's UPDATE_FRIENDLIST
     * carries the whole list (94 friends there) and only its first entry was
     * kept, so a non-staff crowned friend's line (type 3, since 98aca9e6b)
     * read as a stranger's and script 91 (pm_filtertest) hid it under
     * "Friends". The sender is the wire's spelling, capitals and NBSP kept
     * (deob client.java class243.field2953): "From <img=22>Eden Cosine:". */
    {
        struct GameProtoRevTable const* rev = GameProtoRev_Osrs(242);
        struct RS_ServerState pm_state = bare_state;
        struct RS_Social* pm_social = calloc(1, sizeof(*pm_social));
        struct RS_Chat* pm_chat = calloc(1, sizeof(*pm_chat));
        int pm_filters[3] = { 0, 1, 0 }; /* public on, private friends, trade on */
        uint8_t wire[512];
        int len = 0;
        struct RevPacket p;
        struct RS_ChatFilters filters;

        assert(rev);
        assert(pm_social);
        assert(pm_chat);
        RS_Social_Init(pm_social);
        RS_Chat_Init(pm_chat, "Player");
        pm_state.social = pm_social;
        pm_state.chat = pm_chat;
        pm_state.chat_filter_mode = pm_filters;

        /* The friend is the list's second entry, online. */
        len = test_put_friend(wire, len, "FaravinIron", 0);
        len = test_put_friend(wire, len, "Eden\xa0" "Cosine", 435);
        len = test_put_friend(wire, len, "Bills\xa0" "Late", 0);
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_FRIENDLIST), wire, len, &p),
            "a three-friend UPDATE_FRIENDLIST parses");
        TEST_EXEC_ASSERT(p._update_friendlist.count == 3, "every entry of the payload is carried");
        App_GameProto_Exec(base_app, &pm_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(pm_social->friend_count == 3, "the whole list is the friend list");
        TEST_EXEC_ASSERT(RS_Social_IsFriend(pm_social, "eden_cosine"), "she is a friend");
        {
            int const i = RS_Social_FriendIndex(pm_social, "Eden Cosine");

            TEST_EXEC_ASSERT(i >= 0, "found by any spelling of her name");
            TEST_EXEC_ASSERT(strcmp(pm_social->friend_name[i], "Eden\xa0" "Cosine") == 0,
                "kept as the wire spells her");
            TEST_EXEC_ASSERT(RS_Social_FriendWorld(pm_social, i) == 435, "with her world");
        }

        len = test_put_ignore(wire, 0, 0x1, "Spam\xa0" "Bot", "");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, len, &p),
            "a one-entry UPDATE_IGNORELIST parses");
        App_GameProto_Exec(base_app, &pm_state, &p);
        gameproto_free(&p);

        filters = (struct RS_ChatFilters){ .public_mode = 0, .private_mode = 1, .trade_mode = 0, .social = pm_social };

        /* Crown 6 (image 22) is not a staff crown: typed 3, the crown's image
         * before the name, the friend's line under "Friends". */
        len = test_put_private(wire, (int)sizeof(wire), "Eden\xa0" "Cosine", 101, 6, "hi there");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_MESSAGE_PRIVATE), wire, len, &p),
            "MESSAGE_PRIVATE parses");
        TEST_EXEC_ASSERT(strcmp(p._message_private.sender, "Eden\xa0" "Cosine") == 0,
            "the sender is the name as sent, not its base37 key");
        App_GameProto_Exec(base_app, &pm_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(pm_chat->message_count == 1, "the friend's line is stored");
        TEST_EXEC_ASSERT(pm_chat->newest->type == RS_CHAT_TYPE_PRIVATE_FROM, "typed 3: crown 6 is not staff");
        TEST_EXEC_ASSERT(strcmp(pm_chat->newest->name, "<img=22>Eden\xa0" "Cosine") == 0,
            "shown as From <img=22>Eden Cosine");
        TEST_EXEC_ASSERT(RS_Chat_NodeFriendState(pm_chat->newest, pm_social) == 1,
            "script 91 reads the line as a friend's");
        TEST_EXEC_ASSERT(RS_Chat_MessagePasses(&filters, &pm_chat->messages[0]),
            "shown under the friends-only private filter");

        /* The same from an ignored sender is dropped (class243.field2953's
         * ignore test, for any crown). */
        len = test_put_private(wire, (int)sizeof(wire), "Spam\xa0" "Bot", 102, 6, "buy gold");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_MESSAGE_PRIVATE), wire, len, &p),
            "the ignored sender's MESSAGE_PRIVATE parses");
        App_GameProto_Exec(base_app, &pm_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(pm_chat->message_count == 1, "an ignored sender's line is hidden");

        /* The same from a stranger: stored, hidden under "Friends", shown
         * under "On". */
        len = test_put_private(wire, (int)sizeof(wire), "Stranger\xa0" "Ed", 103, 6, "hello");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_MESSAGE_PRIVATE), wire, len, &p),
            "the stranger's MESSAGE_PRIVATE parses");
        App_GameProto_Exec(base_app, &pm_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(pm_chat->message_count == 2, "a stranger's line is stored");
        TEST_EXEC_ASSERT(RS_Chat_NodeFriendState(pm_chat->newest, pm_social) == 0,
            "script 91 reads a stranger's line as neither friend nor ignored");
        TEST_EXEC_ASSERT(!RS_Chat_MessagePasses(&filters, &pm_chat->messages[0]),
            "a stranger's line is hidden under the friends-only private filter");
        filters.private_mode = 0;
        TEST_EXEC_ASSERT(RS_Chat_MessagePasses(&filters, &pm_chat->messages[0]),
            "and shown under \"On\"");

        RS_Chat_Free(pm_chat);
        free(pm_chat);
        free(pm_social);
        printf("ok - a private message from a crowned friend passes the friends-only filter\n");
    }

    /* B4: the friend login/logout notices and the rename rule, on recording
     * 96's own sequence (index 236, rev 227). Its login list holds every
     * friend and the client says nothing about any of them. Later updates
     * are single entries: "Bills Late" goes offline at [439] and nothing
     * follows (a logout: "Bills Late has logged out." ten to eleven seconds
     * on); "Xarpos" goes offline at [995] and is back on world 442 at [1007],
     * twelve ticks (7.2 s) later (a world hop: the logout notice is still
     * queued, the login cancels it, nothing is said); "Eden Cosine" comes
     * online on 435 at [7960] (logged in). The rules are deob
     * Statics.method10091 (queue or cancel; only an entry already on the
     * list) and class58.method1165 (print once stamp+5 < now-5, in seconds). */
    {
        struct RS_ServerState fs = bare_state;
        struct RS_Social* fsoc = calloc(1, sizeof(*fsoc));
        struct RS_Chat* fchat = calloc(1, sizeof(*fchat));
        uint8_t wire[512];
        int clock = 0;
        int len;

        assert(fsoc);
        assert(fchat);
        RS_Social_Init(fsoc);
        RS_Chat_Init(fchat, "Player");
        fs.social = fsoc;
        fs.chat = fchat;
        fs.chat_clock = &clock;

        len = test_put_friend(wire, 0, "Bills\xa0" "Late", 443);
        len = test_put_friend(wire, len, "Xarpos", 457);
        len = test_put_friend(wire, len, "Eden\xa0" "Cosine", 0);
        test_friend_packet(base_app, &fs, wire, len);
        TEST_EXEC_ASSERT(fsoc->friend_count == 3, "the login list is the friend list");
        test_notice_cycles(&fs, &clock, 1000);
        TEST_EXEC_ASSERT(fchat->message_count == 0, "the first list after login says nothing");

        /* [439] Bills Late logs out, at clock 1000 (second 20, stamp 25). */
        len = test_put_friend(wire, 0, "Bills\xa0" "Late", 0);
        test_friend_packet(base_app, &fs, wire, len);
        test_notice_cycles(&fs, &clock, 1549);
        TEST_EXEC_ASSERT(fchat->message_count == 0, "nothing ten seconds on (second 30: 25 < 25 is false)");
        test_notice_cycles(&fs, &clock, 1550);
        TEST_EXEC_ASSERT(fchat->message_count == 1, "the logout notice at second 31");
        TEST_EXEC_ASSERT(fchat->newest->type == RS_CHAT_TYPE_PRIVATE_SYSTEM, "typed 5");
        TEST_EXEC_ASSERT(fchat->newest->name[0] == '\0', "with no name");
        TEST_EXEC_ASSERT(strcmp(fchat->newest->text, "Bills\xa0" "Late has logged out.") == 0,
            "\"Bills Late has logged out.\", as the wire spells him");

        /* [995] .. [1007] Xarpos hops: out, then in on 442 7.2 s later. */
        len = test_put_friend(wire, 0, "Xarpos", 0);
        test_friend_packet(base_app, &fs, wire, len);
        test_notice_cycles(&fs, &clock, clock + 360);
        len = test_put_friend(wire, 0, "Xarpos", 442);
        test_friend_packet(base_app, &fs, wire, len);
        test_notice_cycles(&fs, &clock, clock + 1500);
        TEST_EXEC_ASSERT(fchat->message_count == 1, "a world hop says nothing");
        TEST_EXEC_ASSERT(RS_Social_FriendWorld(fsoc, RS_Social_FriendIndex(fsoc, "xarpos")) == 442,
            "and the friend is on the new world");

        /* [7960] Eden Cosine logs in on 435. */
        len = test_put_friend(wire, 0, "Eden\xa0" "Cosine", 435);
        test_friend_packet(base_app, &fs, wire, len);
        test_notice_cycles(&fs, &clock, clock + 600);
        TEST_EXEC_ASSERT(fchat->message_count == 2, "a login is said");
        TEST_EXEC_ASSERT(strcmp(fchat->newest->text, "Eden\xa0" "Cosine has logged in.") == 0,
            "\"Eden Cosine has logged in.\"");

        /* A rename: the leading byte 1 finds the friend by the previous name;
         * the entry keeps its place and world and takes the new names. */
        len = test_put_friend_renamed(wire, 0, 1, "Eden\xa0" "Sine", "Eden\xa0" "Cosine", 435);
        test_friend_packet(base_app, &fs, wire, len);
        TEST_EXEC_ASSERT(fsoc->friend_count == 3, "a rename is the same friend, not a new one");
        TEST_EXEC_ASSERT(!RS_Social_IsFriend(fsoc, "Eden Cosine") && RS_Social_IsFriend(fsoc, "Eden Sine"),
            "under the new name");
        {
            char prev[RS_SOCIAL_NAME_LEN];
            int const i = RS_Social_FriendIndex(fsoc, "eden_sine");

            RS_Social_FriendPreviousName(fsoc, i, prev, (int)sizeof(prev));
            TEST_EXEC_ASSERT(strcmp(prev, "Eden\xa0" "Cosine") == 0, "remembering the old one");
        }
        /* Both names on the list as two entries: the previous one goes. */
        len = test_put_friend(wire, 0, "Old\xa0" "Pal", 0);
        len = test_put_friend(wire, len, "New\xa0" "Pal", 0);
        test_friend_packet(base_app, &fs, wire, len);
        len = test_put_friend_renamed(wire, 0, 1, "New\xa0" "Pal", "Old\xa0" "Pal", 0);
        test_friend_packet(base_app, &fs, wire, len);
        TEST_EXEC_ASSERT(fsoc->friend_count == 4 && !RS_Social_IsFriend(fsoc, "Old Pal"),
            "a rename onto a name already listed drops the previous entry");

        /* A relog: the session reset empties the list, and the new login's
         * list, every world different, says nothing. */
        {
            int const lines = fchat->message_count;

            RS_Social_ResetForLogin(fsoc);
            TEST_EXEC_ASSERT(RS_Social_FriendCount(fsoc) < 0, "loading again after the reset");
            len = test_put_friend(wire, 0, "Bills\xa0" "Late", 301);
            len = test_put_friend(wire, len, "Xarpos", 0);
            test_friend_packet(base_app, &fs, wire, len);
            test_notice_cycles(&fs, &clock, clock + 1500);
            TEST_EXEC_ASSERT(fchat->message_count == lines, "the first list after a relog says nothing");
            TEST_EXEC_ASSERT(fsoc->friend_count == 2, "and is the whole list");
        }

        RS_Chat_Free(fchat);
        free(fchat);
        free(fsoc);
        printf("ok - friend login/logout notices and renames follow the reference\n");
    }

    /* B2: the ignore list, as recording 96 (index 236, rev 227) sends it at
     * login: [update_ignorelist] added "60ibis2142", added "Kill\xa0777"
     * (an NBSP, as the wire bytes show). Each rev-221+ entry is a change to
     * the list held (deob class428.method9746): a later one-entry add keeps
     * the others, a leading 4 removes, and the low bit finds a renamed entry
     * by its previous name. The names are kept as spelled and matched by the
     * Username key, like the friend list. */
    {
        struct GameProtoRevTable const* rev = GameProtoRev_Osrs(242);
        struct RS_ServerState ig_state = bare_state;
        struct RS_Social* ig = calloc(1, sizeof(*ig));
        uint8_t wire[256];
        char name[RS_SOCIAL_NAME_LEN];
        int len;
        struct RevPacket p;

        assert(rev);
        assert(ig);
        RS_Social_Init(ig);
        ig_state.social = ig;

        len = test_put_ignore(wire, 0, 0, "60ibis2142", "");
        len = test_put_ignore(wire, len, 0, "Kill\xa0" "777", "");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, len, &p),
            "recording 96's login UPDATE_IGNORELIST parses");
        App_GameProto_Exec(base_app, &ig_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(ig->ignore_count == 2, "both ignores are held");
        RS_Social_IgnoreName(ig, 1, name, (int)sizeof(name));
        TEST_EXEC_ASSERT(strcmp(name, "Kill\xa0" "777") == 0, "an ignore is named as the wire spells it");
        TEST_EXEC_ASSERT(RS_Social_IsIgnored(ig, "kill 777"), "and found by any spelling of it");

        /* The server's answer to an ignore add is that one entry. */
        len = test_put_ignore(wire, 0, 0x1, "Spam\xa0" "Bot", "");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, len, &p),
            "a one-entry add parses");
        App_GameProto_Exec(base_app, &ig_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(ig->ignore_count == 3, "a one-entry update adds to the list, it does not replace it");

        /* A removal: the flags byte 4 and the name alone. */
        len = test_put_ignore(wire, 0, 0x4, "60ibis2142", "");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, len, &p),
            "a removal parses");
        App_GameProto_Exec(base_app, &ig_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(!RS_Social_IsIgnored(ig, "60ibis2142"), "a removal entry unignores the name");
        TEST_EXEC_ASSERT(ig->ignore_count == 2, "and leaves the rest");

        /* A rename: found by the previous name, it takes the new one. */
        len = test_put_ignore(wire, 0, 0x1, "Kill Seven", "Kill\xa0" "777");
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, len, &p),
            "a rename parses");
        App_GameProto_Exec(base_app, &ig_state, &p);
        gameproto_free(&p);
        TEST_EXEC_ASSERT(ig->ignore_count == 2, "a rename is the same entry");
        TEST_EXEC_ASSERT(RS_Social_IsIgnored(ig, "Kill Seven") && !RS_Social_IsIgnored(ig, "Kill 777"),
            "under its new name only");
        {
            int const i = RS_Social_IgnoreIndex(ig, "kill_seven");

            RS_Social_IgnorePreviousName(ig, i, name, (int)sizeof(name));
            TEST_EXEC_ASSERT(strcmp(name, "Kill\xa0" "777") == 0, "remembering the previous name");
        }

        /* The rs-family wires send the whole list as base-37 keys, and the
         * list is replaced. */
        {
            struct GameProtoRevTable const* rs = GameProtoRev_ByName("rs254");
            uint64_t const key = strtobase37("bob_smith");

            assert(rs);
            for( int i = 0; i < 8; i++ )
                wire[i] = (uint8_t)(key >> (56 - 8 * i));
            memset(&p, 0, sizeof(p));
            TEST_EXEC_ASSERT(
                rs->parse_wire(rs, rs->packetin_wire(PKT_NAME_UPDATE_IGNORELIST), wire, 8, &p),
                "an rs254 UPDATE_IGNORELIST parses");
            App_GameProto_Exec(base_app, &ig_state, &p);
            gameproto_free(&p);
            TEST_EXEC_ASSERT(ig->ignore_count == 1, "an rs-wire list replaces the list");
            RS_Social_IgnoreName(ig, 0, name, (int)sizeof(name));
            TEST_EXEC_ASSERT(strcmp(name, "Bob Smith") == 0, "named as its client showed the key");
        }

        free(ig);
        printf("ok - the ignore list is keyed and matched like the friend list, on every wire\n");
    }

    /* B3: a name is shown as the wire spells it. A rev-221+ wire spells it
     * (the store keeps "Eden\xa0Cosine", checked above); an rs-family wire
     * sends only its base-37 key, and its 2004 client showed that key as
     * TextClass.fixName does: "bob_smith" is "Bob Smith". The store no longer
     * title-cases on the way out, so the rs adapter writes that form. */
    {
        struct GameProtoRevTable const* rs = GameProtoRev_ByName("rs254");
        uint64_t const key = strtobase37("bob_smith");
        uint8_t wire[9];
        struct RevPacket p;

        assert(rs);
        for( int i = 0; i < 8; i++ )
            wire[i] = (uint8_t)(key >> (56 - 8 * i));
        wire[8] = 2;
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rs->parse_wire(rs, rs->packetin_wire(PKT_NAME_UPDATE_FRIENDLIST), wire, (int)sizeof(wire), &p),
            "an rs254 UPDATE_FRIENDLIST parses");
        TEST_EXEC_ASSERT(p._update_friendlist.count == 1, "one friend per packet on the rs wire");
        TEST_EXEC_ASSERT(strcmp(p._update_friendlist.entries[0].name, "Bob Smith") == 0,
            "an rs wire's friend is named as its client showed the key");
        TEST_EXEC_ASSERT(p._update_friendlist.entries[0].world == 2, "with the world");
        gameproto_free(&p);
        printf("ok - an rs-wire friend is named as its client showed it\n");
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
            rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_REBUILD_REGION), body, sizeof(body), &p),
            "REBUILD_REGION_V2 must parse");
        assert(p._map_rebuild.zonez == 0x0123);
        assert(p._map_rebuild.zonex == 0x0234);
        assert(p._map_rebuild.zones != NULL);
        assert((uint32_t)p._map_rebuild.zones[0] == descriptor);
        assert(p._map_rebuild.zones[1] == 0);
        ToriMem_Free(ToriMem_Net(), p._map_rebuild.zones);

        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            !rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_REBUILD_REGION), body, (int)sizeof(body) - 1, &p),
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
            rev->parse_wire(
                rev,
                rev->packetin_wire(PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS),
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
            rev->parse_wire(
                rev, rev->packetin_wire(PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS), full, (int)sizeof(full), &p),
            "UPDATE_ZONE_FULL_FOLLOWS must parse");
        assert(p._update_zone_full_follows.base_z == 42);
        assert(p._update_zone_full_follows.base_x == 56);
        assert(p._update_zone_full_follows.level == 2);

        uint8_t enclosed[3] = { (uint8_t)-3, (uint8_t)(43 + 128), (uint8_t)(57 + 128) };
        memset(&p, 0, sizeof(p));
        TEST_EXEC_ASSERT(
            rev->parse_wire(
                rev,
                rev->packetin_wire(PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED),
                enclosed,
                (int)sizeof(enclosed),
                &p),
            "UPDATE_ZONE_PARTIAL_ENCLOSED must parse");
        assert(p._update_zone_enclosed.base_z == 43);
        assert(p._update_zone_enclosed.base_x == 57);
        assert(p._update_zone_enclosed.level == 3);
        ToriMem_Free(ToriMem_Net(), p._update_zone_enclosed.entries);
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
        assert(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_SET_ACTIVE_WORLD), wire, 3, &p) > 0);
        assert(p._set_active_world.world_id == 666);
        assert(p._set_active_world.level == 1);

        /* Root re-select, the id every SERVER_TICK_END implies: 0 on the
         * wire from 237, folded to the canonical WORLDVIEW_WIRE_ROOT. */
        uint8_t const root_wire[3] = { 0x00, 0x00, 0x00 };
        memset(&p, 0, sizeof(p));
        assert(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_SET_ACTIVE_WORLD), root_wire, 3, &p) > 0);
        assert(p._set_active_world.world_id == WORLDVIEW_WIRE_ROOT);
        assert(p._set_active_world.level == 0);
        printf("ok - SET_ACTIVE_WORLD decodes id then plane\n");
    }

    /* Before 237 the root went out as -1 and a hull could be id 0 (RSProx
     * 4281, rev 235, spawns one): the same three bytes mean a hull there. */
    {
        struct GameProtoRevTable const* rev = GameProtoRev_Osrs(235);
        struct RevPacket p;
        uint8_t const root_wire[3] = { 0xFF, 0xFF, 0x00 };
        uint8_t const hull0_wire[3] = { 0x00, 0x00, 0x01 };

        assert(rev);
        memset(&p, 0, sizeof(p));
        assert(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_SET_ACTIVE_WORLD), root_wire, 3, &p) > 0);
        assert(p._set_active_world.world_id == WORLDVIEW_WIRE_ROOT);
        memset(&p, 0, sizeof(p));
        assert(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_SET_ACTIVE_WORLD), hull0_wire, 3, &p) > 0);
        assert(p._set_active_world.world_id == 0);
        assert(p._set_active_world.level == 1);
        printf("ok - SET_ACTIVE_WORLD before 237: root -1, hull 0 is a hull\n");
    }

    /* Exec half: the packet arms the worldview cursor (slot AND plane), and
     * the tick fence disarms it back to the root. The wire id is the server's
     * global hull index; the cursor is the local slot it was registered in.
     * The registry holds dummy world/builder pointers — the handler only
     * checks liveness, and nothing here dereferences or frees them. */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        static char dummy_world[1];
        static char dummy_builder[1];

        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        /* A real App's world lives from App_Init to shutdown, and the fence
         * reads its cycle (the plugin tick). */
        app->world = World_New();
        WorldviewRegistry_Init(&app->worldviews);
        WorldviewRegistry_RegisterRoot(
            &app->worldviews,
            (struct World*)dummy_world,
            (struct WorldBuilder*)dummy_builder);
        WorldviewRegistry_Register(
            &app->worldviews,
            3,
            2009,
            (struct World*)dummy_world,
            (struct WorldBuilder*)dummy_builder,
            6400,
            6400,
            8,
            8,
            WORLDVIEW_ROOT);
        App_ServerStateBind(app, &app_state);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SET_ACTIVE_WORLD;
        p._set_active_world.world_id = 2009;
        p._set_active_world.level = 1;
        App_GameProto_Exec(app, &app_state, &p);
        assert(WorldviewRegistry_Active(&app->worldviews) == 3);
        assert(WorldviewRegistry_ActiveLevel(&app->worldviews) == 1);

        /* The slot number is not a wire id: naming it is naming a hull that
         * does not exist, refused with the cursor left where it was. */
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SET_ACTIVE_WORLD;
        p._set_active_world.world_id = 3;
        p._set_active_world.level = 2;
        App_GameProto_Exec(app, &app_state, &p);
        assert(WorldviewRegistry_Active(&app->worldviews) == 3);
        assert(WorldviewRegistry_ActiveLevel(&app->worldviews) == 1);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SET_ACTIVE_WORLD;
        p._set_active_world.world_id = WORLDVIEW_WIRE_ROOT;
        p._set_active_world.level = 0;
        App_GameProto_Exec(app, &app_state, &p);
        assert(WorldviewRegistry_Active(&app->worldviews) == WORLDVIEW_ROOT);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SET_ACTIVE_WORLD;
        p._set_active_world.world_id = 2009;
        p._set_active_world.level = 1;
        App_GameProto_Exec(app, &app_state, &p);
        assert(WorldviewRegistry_Active(&app->worldviews) == 3);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_SERVER_TICK_END;
        App_GameProto_Exec(app, &app_state, &p);
        assert(WorldviewRegistry_Active(&app->worldviews) == WORLDVIEW_ROOT);
        assert(WorldviewRegistry_ActiveLevel(&app->worldviews) == 0);

        World_Free(app->world);
        free(app);
        printf("ok - SET_ACTIVE_WORLD arms the cursor and the fence resets it\n");
    }

    /* SET_MAP_FLAG: wire tiles are classic-scene local = our-scene tiles. */
    {
        struct App* app = calloc(1, sizeof(*app));
        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        app->minimap.flag_tile_x = -1;
        app->minimap.flag_tile_z = -1;
        App_ServerStateBind(app, &app_state);

        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 40;
            p._set_map_flag.z = 50;
            p._set_map_flag.clear = 0;
            App_GameProto_Exec(app, &app_state, &p);
            assert(app->minimap.flag_tile_x == 40);
            assert(app->minimap.flag_tile_z == 50);
            assert(RedrawRequest_Pending(&app->redraw_request));
        }
        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 255;
            p._set_map_flag.z = 255;
            p._set_map_flag.clear = 1;
            App_GameProto_Exec(app, &app_state, &p);
            assert(app->minimap.flag_tile_x == -1);
            assert(app->minimap.flag_tile_z == -1);
        }
        /* SetMapFlagV2's absolute coord goes through the root world's base,
         * the origin the camera packets and the flag's reader use, not the
         * rebuild record's (which could disagree with the built scene). */
        {
            struct RevPacket p;
            memset(&p, 0, sizeof(p));
            app->world = World_New();
            app->world->_base_tile_x = 3200;
            app->world->_base_tile_z = 3184;
            app->rebuild_region.zone_x = 1;
            app->rebuild_region.zone_z = 1;
            p.packet_type = PKT_NAME_UNSET_MAP_FLAG;
            p._set_map_flag.x = 3222;
            p._set_map_flag.z = 3218;
            p._set_map_flag.absolute = 1;
            App_GameProto_Exec(app, &app_state, &p);
            TEST_EXEC_ASSERT(app->minimap.flag_tile_x == 22, "absolute x against the world's base");
            TEST_EXEC_ASSERT(app->minimap.flag_tile_z == 34, "absolute z against the world's base");
            TEST_EXEC_ASSERT(app->minimap.flag_view == 0, "a root tile is the root's");
            {
                int abs_x = -1;
                int abs_z = -1;

                TEST_EXEC_ASSERT(app_minimap_flag_abs(app, &abs_x, &abs_z) == 1, "a root flag reads back");
                TEST_EXEC_ASSERT(abs_x == 3222 && abs_z == 3218, "against the root's base");
            }

            /* A coord inside a world entity's staging rectangle is that
             * deck's tile, in the deck's own terms (deob client.java
             * class243.field3077: field748.method1423 picks the view whose
             * rectangle holds it, class100.method3310 subtracts ITS base),
             * and it reads back as the absolute tile the server sent. */
            {
                struct Worldview* deck = &app->worldviews.views[3];
                int abs_x = -1;
                int abs_z = -1;

                WorldviewRegistry_Init(&app->worldviews);
                deck->id = 3;
                deck->wire_id = 77;
                deck->base_x = 2048;
                deck->base_z = 5120;
                deck->size_x_tiles = 16;
                deck->size_z_tiles = 16;
                deck->parent_id = WORLDVIEW_ROOT;
                deck->live = true;
                p._set_map_flag.x = 2048 + 5;
                p._set_map_flag.z = 5120 + 9;
                App_GameProto_Exec(app, &app_state, &p);
                TEST_EXEC_ASSERT(app->minimap.flag_view == 3, "a deck coord is the deck's flag");
                TEST_EXEC_ASSERT(app->minimap.flag_tile_x == 5, "deck-local x");
                TEST_EXEC_ASSERT(app->minimap.flag_tile_z == 9, "deck-local z");
                TEST_EXEC_ASSERT(app_minimap_flag_abs(app, &abs_x, &abs_z) == 1, "it reads back");
                TEST_EXEC_ASSERT(abs_x == 2053 && abs_z == 5129, "as the absolute tile sent");
                /* The root's scene moving does not move a deck's tile. */
                TEST_EXEC_ASSERT(
                    MinimapView_RebaseFlag(&app->minimap, 8, -8, 104), "a root rebuild keeps it");
                TEST_EXEC_ASSERT(app->minimap.flag_tile_x == 5 && app->minimap.flag_tile_z == 9,
                    "where it was on the deck");
                p._set_map_flag.clear = 1;
                App_GameProto_Exec(app, &app_state, &p);
                TEST_EXEC_ASSERT(!MinimapView_HasFlag(&app->minimap), "a clear clears it");
                TEST_EXEC_ASSERT(app->minimap.flag_view == 0, "and forgets the deck");
                TEST_EXEC_ASSERT(app_minimap_flag_abs(app, &abs_x, &abs_z) == 0, "no flag reads back");
                p._set_map_flag.clear = 0;
                WorldviewRegistry_Init(&app->worldviews);
            }
            World_Free(app->world);
            app->world = NULL;
        }
        free(app);
        printf("ok - SET_MAP_FLAG stores scene-local tiles\n");
    }

    /* UPDATE_INV_STOP_TRANSMIT: the reference unlinks the inventory and rings
     * its id on the same changed-inventory ring UPDATE_INV_FULL feeds (deob
     * client.java class243.field2979), so the transmit hooks redraw the
     * emptied slots. This client emptied the slots and told no hook. */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        unsigned serial;

        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        App_ServerStateBind(app, &app_state);
        serial = app->host.inv_change_serial;
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_UPDATE_INV_STOP_TRANSMIT;
        p._update_inv_stop_transmit.inv_id = 516;
        p._update_inv_stop_transmit.component_id = (300 << 16) | 16;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(app->host.inv_change_serial == serial + 1, "the inventory transmit is told");
        TEST_EXEC_ASSERT(app->host.inv_transmit_dirty, "and its hooks are due");
        TEST_EXEC_ASSERT(app->host.inv_changed_count == 1 && app->host.inv_changed_ids[0] == 516,
            "naming the inventory");

        /* CAM_TARGET: the follow camera's focus, until a logout puts the
         * local player back (deob client.java:1734). */
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_CAM_TARGET;
        p._cam_target.type = CAM_SCRIPT_TARGET_NPC;
        p._cam_target.index = 1234;
        App_GameProto_Exec(app, &app_state, &p);
        {
            int type = -1;
            int index = -1;

            TEST_EXEC_ASSERT(CamScript_Target(&app->cam_script, &type, &index) == 1, "a target is set");
            TEST_EXEC_ASSERT(type == CAM_SCRIPT_TARGET_NPC && index == 1234, "the npc it names");
            p._cam_target.index = -1;
            App_GameProto_Exec(app, &app_state, &p);
            TEST_EXEC_ASSERT(CamScript_Target(&app->cam_script, &type, &index) == 0,
                "an index of none is the local player again");
            p._cam_target.type = CAM_SCRIPT_TARGET_PLAYER;
            p._cam_target.index = 77;
            App_GameProto_Exec(app, &app_state, &p);
            CamScript_Logout(&app->cam_script);
            TEST_EXEC_ASSERT(CamScript_Target(&app->cam_script, &type, &index) == 0,
                "a logout forgets it");
        }
        free(app);
        printf("ok - UPDATE_INV_STOP_TRANSMIT tells the transmit; CAM_TARGET sets the focus\n");
    }

    /* CAM_TARGET's world-entity and coordinate targets move the follow camera
     * (deob client.java method1936 / method1700 cases 2 and 3, the focus step
     * method1605): a hull in the root by its wire id, a root coordinate on the
     * centre of its tile, and a coordinate inside a hull's staging rectangle on
     * that deck tile pushed out through the hull. The anchor snaps to a focus
     * more than 500 fine units away, so each step reads the focus directly. */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct ToriRS_Network* net = calloc(1, sizeof(*net));
        struct World* deck_world = World_New();
        struct WevConfig config;
        struct WorldEntityFacet_IdleAnimations idle;
        struct Worldview* deck;
        struct Wev* wev;
        struct RevPacket p;
        int self_idx;

        assert(app);
        assert(net);
        memset(&idle, 0xff, sizeof(idle)); /* every stance -1: none */
        ToriDraw_InitMath(); /* the hull transform's trig tables */
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        App_ServerStateBind(app, &app_state);
        app->net = net;
        app->world_views.frame = &app->world_views.views[0];
        app->world = World_New();
        app->world->_base_tile_x = 3200;
        app->world->_base_tile_z = 3136;
        RS_EntitySync_Init(&app->esync);
        self_idx = World_PlayerSpawn(app->world, 40, 0, 50, 50, idle);
        RS_EntitySync_RegisterPlayer(&app->esync, 5, 40, self_idx);
        app->esync.local_pid = 5;
        app->world->local_pid = 5;

        /* One hull in the root: wire id 900 in slot 1, its deck a 16x16
         * staging rectangle at (6400, 6400), the hull at root tile
         * (3220, 3150), unturned. */
        WorldviewRegistry_Init(&app->worldviews);
        Wevs_Init(&app->wevs);
        deck = &app->worldviews.views[1];
        deck->id = 1;
        deck->wire_id = 900;
        deck->world = deck_world;
        deck->base_x = 6400;
        deck->base_z = 6400;
        deck->size_x_tiles = 16;
        deck->size_z_tiles = 16;
        deck->parent_id = WORLDVIEW_ROOT;
        deck->live = true;
        WevConfig_Init(&config, 3);
        wev = Wevs_Spawn(&app->wevs, 1, WORLDVIEW_ROOT, &config, 3, 3220 << 7, 3150 << 7, 0, 0, WEV_OP_MASK_ALL);
        assert(wev);

        app_world_camera_follow(app);
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_x == (50 << 7) + 64,
            "with no target the camera follows the local player");

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_CAM_TARGET;
        p._cam_target.type = CAM_SCRIPT_TARGET_WORLDENTITY;
        p._cam_target.index = 900;
        App_GameProto_Exec(app, &app_state, &p);
        app_world_camera_follow(app);
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_x == (3220 - 3200) << 7,
            "a world entity target: the hull's x in the root scene");
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_z == (3150 - 3136) << 7,
            "and its z");

        p._cam_target.type = CAM_SCRIPT_TARGET_COORD;
        p._cam_target.index = (1 << 28) | (3210 << 14) | 3140;
        App_GameProto_Exec(app, &app_state, &p);
        app_world_camera_follow(app);
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_x == (10 << 7) + 64,
            "a root coordinate target: the centre of its tile");
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_z == (4 << 7) + 64, "on both axes");

        /* Deck tile (3, 5) is (448, 704) on the deck; recentred by half the
         * 16-tile rectangle (-1024) and carried to the hull at (2560, 1792). */
        p._cam_target.index = (6400 + 3) << 14 | (6400 + 5);
        App_GameProto_Exec(app, &app_state, &p);
        app_world_camera_follow(app);
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_x == 2560 + 448 - 1024,
            "a deck coordinate target: the deck tile pushed out through the hull");
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_z == 1792 + 704 - 1024, "on both axes");

        /* A wire id no hull has: the local player again. */
        p._cam_target.type = CAM_SCRIPT_TARGET_WORLDENTITY;
        p._cam_target.index = 901;
        App_GameProto_Exec(app, &app_state, &p);
        app_world_camera_follow(app);
        TEST_EXEC_ASSERT((int)app->world_views.frame->orbit.anchor_x == (50 << 7) + 64,
            "a hull that is not there leaves the local player's focus");

        World_Free(deck_world);
        World_Free(app->world);
        RS_EntitySync_Free(&app->esync);
        free(net);
        free(app);
        printf("ok - CAM_TARGET follows a world entity and a coordinate, on a deck too\n");
    }

    /*
     * HINT_ARROW: the type byte says what the other three fields MEAN, so a
     * client holding the wrong numbers does not draw a wrong-looking arrow --
     * it reads an npc slot as a tile coordinate and points at the map's
     * corner. Nothing on screen says which of the two it did.
     *
     * The values are the wire's (LostCity's `HintArrowEncoder` writes them,
     * rev 239's `class268.method6676` reads them) and this client had 1 and 2
     * the other way round, agreeing only with its own server.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        App_ServerStateBind(app, &app_state);

        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_HINT_ARROW;
        p._hint_arrow.type = 1;
        p._hint_arrow.id = 41;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->hint_arrow.type == RS_HINT_ARROW_NPC, "type 1 is an npc slot");
        TEST_EXEC_ASSERT(app->hint_arrow.target == 41, "and the slot is the id");

        p._hint_arrow.type = 10;
        p._hint_arrow.id = 7;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->hint_arrow.type == RS_HINT_ARROW_PLAYER, "type 10 is a player pid");

        /* The five tile forms are one form with five anchors. Left as five
         * types, every pass that draws a hint would have to know all five --
         * so the exec folds them here and hands on a fine-unit offset. */
        p._hint_arrow.type = 2;
        p._hint_arrow.id = 3200;
        p._hint_arrow.z = 3300;
        p._hint_arrow.height = 60;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->hint_arrow.type == RS_HINT_ARROW_COORD, "type 2 is an absolute tile");
        TEST_EXEC_ASSERT(
            app->hint_arrow.target == 3200 && app->hint_arrow.tile_z == 3300,
            "carried as x and z, not as one packed coord");
        TEST_EXEC_ASSERT(app->hint_arrow.height == 60, "with the height byte");
        TEST_EXEC_ASSERT(
            app->hint_arrow.offset_x == 64 && app->hint_arrow.offset_z == 64,
            "and anchored at the tile's centre");

        p._hint_arrow.type = 3;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->hint_arrow.type == RS_HINT_ARROW_COORD, "type 3 folds to the same form");
        TEST_EXEC_ASSERT(
            app->hint_arrow.offset_x == 0 && app->hint_arrow.offset_z == 64,
            "anchored to the tile's west edge");

        p._hint_arrow.type = 6;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->hint_arrow.offset_x == 64 && app->hint_arrow.offset_z == 128,
            "and type 6 to its north one");

        /* 255 clears. Normalised on the way in so 0 means "no arrow" for every
         * pass downstream, rather than each one knowing the sentinel. */
        p._hint_arrow.type = 255;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(app->hint_arrow.type == 0, "255 is the clear");

        free(app);
        printf("ok - HINT_ARROW reads the wire's own type numbers\n");
    }

    /*
     * A LOC_ANIM in the same zone update as a LOC_ADD_CHANGE for its tile does
     * what the rev-239 client does with it, which is NOT "animate the new loc".
     *
     * Deob Statics, the LOC_ANIM decode: the change is only a PENDING SPAWN
     * (method5669) until the pending pass after the packet loop builds it, so
     * the sequence goes at once to whatever stands on the tile's layer
     * (method1508). The pending
     * spawn keeps it (class69.field666) for the loc it will place only when
     * nothing stood there, or when the NEW loctype has deferanimchange
     * (class393.field4940). This client used to queue the animation behind the
     * change, so content that animated a loc on the tick it placed it worked
     * here and never on RuneLite.
     *
     * Pinned here: the anim queues no task (it is applied at once); the change
     * pending for the tile and LAYER is found through the pending index and
     * handed the sequence under the deob's two conditions; a newer change for
     * the key replaces the pending one (the deob keeps one pending spawn per
     * tile and layer and overwrites it) and is the one that holds it; and the
     * dat1 lanes, whose 2004-era loc changes carry no sequence, never hand it
     * over. The scene half (app_world_scenery_anim_apply) needs a built world
     * and is the same call the change's task makes (app/test/
     * app_loc_tasks_test.c runs it on one).
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        static struct World loc_world;
        static char loc_builder[1];
        struct Task_LocChange* change;
        struct Task_LocChange* newer;
        struct Task_LocChange* newest;
        struct Task_LocChange probe;
        int seq = -1;
        int carried;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->frame_runner.queue = ToriRS_TaskQueue_New();
        App_AssetsInit(app);
        App_PendingInit(app);
        /* A root view whose world is not built: nothing stands on any tile. */
        memset(&loc_world, 0, sizeof(loc_world));
        WorldviewRegistry_Init(&app->worldviews);
        WorldviewRegistry_RegisterRoot(
            &app->worldviews, &loc_world, (struct WorldBuilder*)loc_builder);

        App_WorldLocChange(app, 12, 34, 0, 32744 /* loc */, 22 /* grounddecor */, 0);
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 8068 /* seq */);
        TEST_EXEC_ASSERT(app->packet_runner.queue->head == NULL, "nothing on the packet FIFO");
        TEST_EXEC_ASSERT(app->frame_runner.queue->head != NULL, "the change is a parallel task");
        TEST_EXEC_ASSERT(
            app->frame_runner.queue->head->next == NULL, "and the anim queued no task behind it");
        change = app_loc_change_pending(app, WORLDVIEW_ROOT, 12, 34, 0, 3);
        TEST_EXEC_ASSERT(
            change == (struct Task_LocChange*)app->frame_runner.queue->head,
            "the pending index finds it by view, tile, level and layer");
        TEST_EXEC_ASSERT(change->carry.unfound_set == 1, "nothing stood: the change keeps it");
        TEST_EXEC_ASSERT(change->carry.unfound_seq == 8068, "the sequence it keeps");
        carried = app_loc_change_carried(change, &seq);
        TEST_EXEC_ASSERT(carried == 1, "a plain loctype places its loc on it");
        TEST_EXEC_ASSERT(seq == 8068, "on that sequence");

        /* Another layer of the same tile (a centrepiece), and another tile:
         * neither is this change's pending spawn. */
        App_WorldSceneryAnim(app, 12, 34, 0, 10, 1111);
        App_WorldSceneryAnim(app, 13, 34, 0, 22, 2222);
        TEST_EXEC_ASSERT(change->carry.unfound_seq == 8068, "another layer is not its tile");
        TEST_EXEC_ASSERT(change->carry.any_seq == 8068, "nor is another tile");

        /* A second change for the tile replaces the first: one pending spawn
         * per tile and layer, overwritten. */
        App_WorldLocChange(app, 12, 34, 0, 32743, 22, 0);
        newer = (struct Task_LocChange*)app->frame_runner.queue->head->next;
        TEST_EXEC_ASSERT(newer != NULL, "the second change is queued");
        TEST_EXEC_ASSERT(change->par.cancelled, "and the first is cancelled");
        TEST_EXEC_ASSERT(
            app_loc_change_pending(app, WORLDVIEW_ROOT, 12, 34, 0, 3) == newer,
            "the newest change stands for the tile");
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 8065);
        TEST_EXEC_ASSERT(newer->carry.unfound_seq == 8065, "the newer change keeps it");
        TEST_EXEC_ASSERT(change->carry.unfound_seq == 8068, "the older one is untouched");
        TEST_EXEC_ASSERT(!newer->carry.take_set, "read this tick: it is not taken");

        /* A tick later it is still pending (its models are loading), but the
         * deob built it in the cycle that read it: the anim is its own,
         * whatever its loctype -- not the stale loc's, and not judged by
         * deferanimchange. */
        RS_ServerTick_EndUnfenced(&app->server_tick);
        newer->defers_anim = 0;
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 8070);
        TEST_EXEC_ASSERT(newer->carry.take_set == 1, "the landed change takes it");
        TEST_EXEC_ASSERT(newer->carry.unfound_seq == 8065, "not as a carry");
        carried = app_loc_change_carried(newer, &seq);
        TEST_EXEC_ASSERT(carried == 1, "a plain loctype starts on it");
        TEST_EXEC_ASSERT(seq == 8070, "the later sequence over the carried one");

        /* A change read in THIS tick replaces the landed one; nothing stands
         * in this unbuilt scene, so the new change carries the anim. */
        App_WorldLocChange(app, 12, 34, 0, 32742, 22, 0);
        newest = app_loc_change_pending(app, WORLDVIEW_ROOT, 12, 34, 0, 3);
        TEST_EXEC_ASSERT(newest != newer, "the third change is the pending spawn");
        TEST_EXEC_ASSERT(newer->par.cancelled, "the landed one is replaced");
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 8071);
        TEST_EXEC_ASSERT(newest->carry.unfound_seq == 8071, "nothing stood: an unfound carry");
        TEST_EXEC_ASSERT(newer->carry.take_seq == 8070, "the replaced change gets nothing");

        /* The two conditions, on a bare change. A loc that STOOD took the
         * sequence itself: a loctype without deferanimchange keeps whatever it
         * held before (here nothing), one with it takes the sequence too. */
        memset(&probe, 0, sizeof(probe));
        probe.change.loc_id = 1;
        app_loc_change_carry(&probe, 7559, 1);
        probe.defers_anim = 0;
        TEST_EXEC_ASSERT(
            app_loc_change_carried(&probe, &seq) == 0, "a loc stood: a plain loctype loses it");
        probe.defers_anim = 1;
        carried = app_loc_change_carried(&probe, &seq);
        TEST_EXEC_ASSERT(carried == 1, "deferanimchange keeps it anyway");
        TEST_EXEC_ASSERT(seq == 7559, "that sequence");
        /* field666 is one slot, written last-wins under those conditions: an
         * unfound 7560 then a found 7561 leaves 7560 for a plain loctype and
         * 7561 for a deferring one. */
        app_loc_change_carry(&probe, 7560, 0);
        app_loc_change_carry(&probe, 7561, 1);
        probe.defers_anim = 0;
        carried = app_loc_change_carried(&probe, &seq);
        TEST_EXEC_ASSERT(carried == 1, "the unfound write stands for a plain loctype");
        TEST_EXEC_ASSERT(seq == 7560, "the last unfound one");
        probe.defers_anim = 1;
        carried = app_loc_change_carried(&probe, &seq);
        TEST_EXEC_ASSERT(carried == 1, "a deferring loctype took every write");
        TEST_EXEC_ASSERT(seq == 7561, "the last of them");

        /* The 2004-era lanes: the anim reaches the standing loc or nothing. */
        app->cfg.cache_kind = APP_CACHE_DAT1;
        App_WorldSceneryAnim(app, 12, 34, 0, 22, 4444);
        TEST_EXEC_ASSERT(newest->carry.unfound_seq == 8071, "dat1 never hands it over");
        TEST_EXEC_ASSERT(newest->carry.any_seq == 8071, "under either condition");
        app->cfg.cache_kind = APP_CACHE_DAT2;

        /* (An EFFECT -- a spotanim, a projectile -- is a parallel task of its
         * own now, with its World row made at the packet:
         * app/test/app_tasks_effects_test.c.) */

        /* Freeing the queue with the changes unrun unlists every one of them
         * from the pending index. */
        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        ToriRS_TaskQueue_Free(app->frame_runner.queue);
        TEST_EXEC_ASSERT(RS_Pending_Listed(app->pending) == 0, "freed unrun, every change unlisted");
        App_PendingFree(app);
        App_AssetsFree(app);
        free(app);
        printf("ok - a LOC_ANIM goes to the standing loc; a pending LOC_ADD_CHANGE keeps it only as rev 239 does\n");
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
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        App_ServerStateBind(app, &app_state);

        /* MINIMAP_TOGGLE had no parser at all; parse it off the wire rather
         * than hand-filling the struct, so a mis-sized field shows up here. */
        {
            uint8_t const wire[1] = { 2 };
            memset(&p, 0, sizeof(p));
            TEST_EXEC_ASSERT(
                rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_MINIMAP_TOGGLE), wire, 1, &p) > 0,
                "MINIMAP_TOGGLE must parse");
            assert(p._minimap_toggle.state == 2);
            App_GameProto_Exec(app, &app_state, &p);
            assert(MinimapView_ToggleState(&app->minimap) == APP_MINIMAP_STATE_HIDDEN);
            assert(RedrawRequest_Pending(&app->redraw_request));
        }
        printf("ok - MINIMAP_TOGGLE decodes and reaches the client\n");

        /* Server ticks in, client cycles out. A countdown left in server ticks
         * would run 30x fast and read as seconds when it means half-minutes. */
        {
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_UPDATE_REBOOT_TIMER;
            p._update_reboot_timer.ticks = 100;
            App_GameProto_Exec(app, &app_state, &p);
            assert(app->session_info.reboot_timer == 100 * APP_SERVER_TICK_LOGIC_CYCLES);
            /* 100 server ticks = 60s = one minute exactly. */
            assert(app->session_info.reboot_timer / APP_LOGIC_CYCLES_PER_SECOND == 60);
        }
        printf("ok - UPDATE_REBOOT_TIMER converts server ticks to client cycles\n");

        {
            memset(&p, 0, sizeof(p));
            p.packet_type = PKT_NAME_SET_MULTIWAY;
            p._set_multiway.multiway = 1;
            RedrawRequest_Clear(&app->redraw_request);
            App_GameProto_Exec(app, &app_state, &p);
            assert(RS_SessionInfo_Multiway(&app->session_info) == 1);
            assert(RedrawRequest_Pending(&app->redraw_request));
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
            App_GameProto_Exec(app, &app_state, &p);
            assert(app->session_info.welcome.last_ip == 0x01020304);
            assert(app->session_info.welcome.days_since_login == 2);
            assert(app->session_info.welcome.unread_messages == 3);
            assert(app->session_info.welcome.member_warning == 1);
        }
        printf("ok - LAST_LOGIN_INFO carries the member warning\n");

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

        app->session_info.welcome.last_ip = 0x0a000205; /* 10.0.2.5 */
        app->session_info.welcome.days_since_login = 3;
        app->session_info.welcome.days_since_recovery = 5;
        app->session_info.welcome.unread_messages = 2;
        app->session_info.welcome.member_warning = 0;
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
        app->session_info.welcome.last_ip = 0x7f000001;
        app->session_info.welcome.days_since_login = 0;
        app->session_info.welcome.unread_messages = 0;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        assert(
            strcmp(test_row_text(wtree, rows[0]), "You last logged in earlier today.") ==
            0);
        assert(strcmp(test_row_text(wtree, rows[1]), "0 unread messages") == 0);
        assert(wtree->components[rows[1]].u.rs_text.color == RS_CC_WELCOME_COLOUR_YELLOW);

        /* 201 = nothing to say about recovery questions; the three rows go to
         * the member warning when there is one, and blank when there is not. */
        app->session_info.welcome.days_since_recovery = RS_CC_RECOVERY_DAYS_SILENT;
        app->session_info.welcome.member_warning = 0;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        for( int i = 2; i <= 4; i++ )
            assert(strcmp(test_row_text(wtree, rows[i]), "") == 0);

        app->session_info.welcome.member_warning = 1;
        RS_ClientCode_Tick(app, wtree, &app->social, 0);
        for( int i = 2; i <= 4; i++ )
            assert(test_row_text(wtree, rows[i])[0] != '\0');

        /* 200 = never set any, which is its own paragraph and not a day count. */
        app->session_info.welcome.days_since_recovery = RS_CC_RECOVERY_DAYS_NEVER_SET;
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
     * GROUP_FULL fills a slot from the group's config, GROUP_VAR writes one
     * variable, and a record of -1 empties the slot. OBJUNLOCK_UPDATE's entries
     * name their words by delta.
     */
    {
        struct ToriRS_GroupTypeVar var = { .type_id = 0, .base = 0 };
        struct ToriRS_GroupType type = { .var_count = { 1, 0, 0 }, .vars = { &var, NULL, NULL } };
        struct ToriRS_GroupTypes types = { .types = &type, .count = 1 };
        struct RS_GroupStore store;
        /* slot 3, config 0 (id byte 1), uid 9, value zigzag 21 -> -11 */
        uint8_t const full[] = { 3, 1, 0, 0, 0, 0, 0, 0, 0, 9, 21 };
        uint8_t const gone[] = { 3, 0 };
        struct App* app = calloc(1, sizeof(*app));
        struct RevPacket p;
        struct PktObjUnlockEntry entries[2] = { { 2, 5 }, { 3, 1 } };

        memset(&store, 0, sizeof(store));
        RS_GroupStore_ApplyFull(&store, &types, full, sizeof(full), NULL, NULL);
        assert(store.slots[3]);
        assert(store.slots[3]->uid == 9);
        assert(store.slots[3]->values[0][0].int_value == -11);
        assert(RS_GroupStore_FindUid(&store, 9) == store.slots[3]);
        RS_GroupStore_ApplyVar(&store, 3, 0, 44, 0, NULL);
        assert(store.slots[3]->values[0][0].int_value == 44);
        RS_GroupStore_ApplyFull(&store, &types, gone, sizeof(gone), NULL, NULL);
        assert(!store.slots[3]);
        RS_GroupStore_Free(&store);

        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        App_ServerStateBind(app, &app_state);
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_OBJUNLOCK_UPDATE;
        p._objunlock_update.count = 2;
        p._objunlock_update.entries = entries;
        App_GameProto_Exec(app, &app_state, &p);
        assert(app->host.client.objunlock_present);
        assert(app->host.client.objunlock_words[2] == 5);
        assert(app->host.client.objunlock_words[5] == 1);
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_OBJUNLOCK_RESET;
        App_GameProto_Exec(app, &app_state, &p);
        assert(!app->host.client.objunlock_present);
        free(app);
        printf("ok - group store and objunlock\n");
    }

    /*
     * CAM_ROTATEBY / CAM_ROTATETO each restart only the axis they name, and
     * URL_OPEN leaves the url where main.c collects it.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct App_WorldView view;
        struct RevPacket p;
        struct CamScriptRotateCycles const* r = &app->cam_script.rotate_cycles;

        assert(app);
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
        memset(&view, 0, sizeof(view));
        view.world_camera.pitch = 300;
        view.world_camera.yaw = 100;
        app->world_views.frame = &view;

        App_CinemaCameraRotateBy(app, 50, 0, 30, 0);
        assert(r->active);
        assert(r->yaw.active);
        assert(r->yaw.from == 100);
        assert(r->yaw.to == 150);
        assert(!r->pitch.active);
        assert(CamScript_IsScripted(&app->cam_script));

        /* A negative pitch leaves the axis; the yaw goes the short way. */
        App_CinemaCameraRotateTo(app, 1900, -1, 30, 0);
        assert(r->yaw.to == 1900 - 2048);
        assert(!r->pitch.active);

        App_ServerStateBind(app, &app_state);
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_URL_OPEN;
        p._text.text = (char*)"https://oldschool.runescape.com/";
        App_GameProto_Exec(app, &app_state, &p);
        assert(strcmp(app->host.client.pending_open_url, "https://oldschool.runescape.com/") == 0);

        free(app);
        printf("ok - timed camera turns and URL_OPEN\n");
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
        app->tree = tree; /* required (App_GameProto_Exec asserts it) */
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
        App_ServerStateBind(app, &app_state);

        /* Nothing to say about recovery questions and no members warning: the
         * short screen, which has no paragraph rows on it. */
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_LAST_LOGIN_INFO;
        p._last_login_info.last_ip = 0x0a000205;
        p._last_login_info.days_since_login = 3;
        p._last_login_info.days_since_recovery = RS_CC_RECOVERY_DAYS_SILENT;
        p._last_login_info.unread_messages = 0;
        p._last_login_info.member_warning = 0;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_plain, "plain welcome screen opens");

        /* A second one must not push in front of the screen already up --
         * checked with a packet that would otherwise select the OTHER screen,
         * so "left alone" is distinguishable from "reopened identically". */
        p._last_login_info.member_warning = 1;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_plain,
            "a welcome screen already open is left alone");
        p._last_login_info.member_warning = 0;

        /* A members warning turns the same packet into the taller screen: it
         * is the only one carrying rows 652/653/654 to put the warning on. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.member_warning = 1;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_notice,
            "a members warning opens the screen that can show it");

        /* So does a real recovery-question age, for the same reason. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.member_warning = 0;
        p._last_login_info.days_since_recovery = 5;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == iface_notice,
            "a recovery-question notice opens the taller screen");

        /* No address = the server is not offering one. Opening anything here
         * would put an empty last-login row in front of the player. */
        RS_UISlots_Init(&app->slots);
        p._last_login_info.last_ip = 0;
        App_GameProto_Exec(app, &app_state, &p);
        TEST_EXEC_ASSERT(
            app->slots.main_modal_id == -1, "no address opens no welcome screen");

        RevConfigRefs_Free(&app->revconfig_refs);
        free(app);
        printf("ok - LAST_LOGIN_INFO opens the welcome screen the profile names\n");
    }

    /*
     * U4: a server IF_SETHIDE that unhides a component resumes the transmit
     * hooks that deferred while it was hidden, as a script's if_sethide
     * unhide does (widgets_loaded_dirty). It did not, so a hook that missed an
     * inventory change while hidden waited for some unrelated unhide.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct UITreeNodeSpec spec;
        struct UITreeBehavior behavior;
        int const com = (233 << 16) | 53;

        assert(app);
        app->tree = UITree_New(2);
        memset(&spec, 0, sizeof(spec));
        memset(&behavior, 0, sizeof(behavior));
        spec.type = UIELEM_RS_GRAPHIC;
        spec.component_id = com;
        spec.width = 10;
        spec.height = 10;
        behavior.hide = 1;
        spec.behavior = &behavior;
        TEST_EXEC_ASSERT(UITree_Push(app->tree, -1, &spec) >= 0, "the hidden component is in the tree");

        App_IfHideSet(app, com, 1);
        TEST_EXEC_ASSERT(!app->host.widgets_loaded_dirty, "hiding a hidden component resumes nothing");
        App_IfHideSet(app, com, 0);
        TEST_EXEC_ASSERT(app->host.widgets_loaded_dirty,
                         "a server unhide resumes the hooks deferred while it was hidden");
        app->host.widgets_loaded_dirty = 0;
        App_IfHideSet(app, com, 0);
        TEST_EXEC_ASSERT(!app->host.widgets_loaded_dirty, "unhiding a shown component resumes nothing");

        UIIfIntStore_Free(&app->if_hides);
        UITree_Free(app->tree);
        free(app);
        printf("ok - a server unhide resumes the transmit hooks deferred while hidden\n");
    }

    /*
     * U3: a mount applies the server's stored hides, texts and colours for
     * the group it opened onto the group's nodes BEFORE its load scripts run
     * (Task_InterfaceOpen step 4b, InterfaceOpen_ApplyStoredValues). They were
     * re-applied only when a frame was published, after onLoad and the
     * transmits had read the pack's values. Another group's entries are not
     * touched.
     */
    {
        struct UITree* t = UITree_New(4);
        struct RS_CS2Host* host = calloc(1, sizeof(*host));
        struct UIIfTextStore texts = { 0 };
        struct UIIfIntStore hides = { 0 };
        struct UIIfIntStore colours = { 0 };
        struct UITreeNodeSpec spec;
        struct UITreeBehavior hidden;
        int const icon = (233 << 16) | 53;
        int const other = (234 << 16) | 1;
        int32_t icon_idx;
        int32_t other_idx;

        assert(host);
        memset(&spec, 0, sizeof(spec));
        memset(&hidden, 0, sizeof(hidden));
        hidden.hide = 1;
        spec.type = UIELEM_RS_GRAPHIC;
        spec.width = 10;
        spec.height = 10;
        spec.behavior = &hidden;
        spec.component_id = icon;
        icon_idx = UITree_Push(t, -1, &spec);
        spec.component_id = other;
        other_idx = UITree_Push(t, -1, &spec);
        TEST_EXEC_ASSERT(icon_idx >= 0 && other_idx >= 0, "both groups' nodes are in the tree");
        UIIfIntStore_Set(&hides, icon, 0);
        UIIfIntStore_Set(&hides, other, 0);

        InterfaceOpen_ApplyStoredValues(t, host, 233, &texts, &hides, &colours);
        TEST_EXEC_ASSERT(!t->components[icon_idx].behavior.hide,
                         "the opened group's stored unhide is on its node before its scripts run");
        TEST_EXEC_ASSERT(host->widgets_loaded_dirty, "and the unhide resumes the deferred hooks");
        TEST_EXEC_ASSERT(t->components[other_idx].behavior.hide,
                         "another group's stored value is left for its own mount");

        UIIfIntStore_Free(&hides);
        UIIfIntStore_Free(&colours);
        UIIfTextStore_Free(&texts);
        free(host);
        UITree_Free(t);
        printf("ok - a mount applies the group's stored values before its load scripts\n");
    }

    /*
     * U6: an IF_OPENSUB of a group the cache does not have mounts nothing.
     * The slot's outgoing group closes all the same (the reference closes it
     * before it opens), and no SUB_MOUNTED is stamped for a mount that did
     * not happen. Before, the outgoing group stayed and SUB_MOUNTED fired.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct CacheProvider* provider = calloc(1, sizeof(*provider));
        struct CacheProviderVTable* no_loads = calloc(1, sizeof(*no_loads));
        struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
        struct UITreeNodeSpec spec;
        struct UITreeBehavior layer_behavior;
        int const slot = (161 << 16) | 70;
        int const old_root = (500 << 16) | 0;
        int mounted = 0;
        int closed = 0;
        int32_t slot_idx;

        assert(app);
        assert(provider);
        assert(no_loads);
        provider->vtable = no_loads;
        CacheProvider_InitEngineCaches(provider);
        app->provider = provider;
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->frame_runner.queue = ToriRS_TaskQueue_New();
        app->assets = Assets_New();
        App_PendingInit(app);
        app->phase.app_state = APP_STATE_READY;
        app->tree = UITree_New(4);
        /* The host's tree is bound, as App init binds it (ba95a2734). */
        app->host.tree = app->tree;
        memset(&spec, 0, sizeof(spec));
        memset(&layer_behavior, 0, sizeof(layer_behavior));
        spec.behavior = &layer_behavior;
        spec.type = UIELEM_RS_LAYER;
        spec.width = 100;
        spec.height = 100;
        spec.component_id = slot;
        slot_idx = UITree_Push(app->tree, -1, &spec);
        spec.component_id = old_root;
        TEST_EXEC_ASSERT(slot_idx >= 0 && UITree_Push(app->tree, slot_idx, &spec) >= 0,
                         "the slot and its outgoing group are in the tree");
        UITree_InterfaceParentSet(app->tree, slot, 500, 1);

        App_OpenSubInterface(app, slot, 777, 1);
        (void)ToriRS_TaskQueue_Run(app->packet_runner.queue, io);
        for( uint32_t i = 0; i < app->drive_events.ring.count; i++ )
        {
            struct RS_Event const* e = &app->drive_events.entries[i];
            mounted |= e->kind == RS_EVENT_SUB_MOUNTED && e->a == slot;
            closed |= e->kind == RS_EVENT_SUB_CLOSED && e->a == slot && e->b == 500;
        }
        TEST_EXEC_ASSERT(!mounted, "a group the cache lacks is not reported mounted");
        TEST_EXEC_ASSERT(closed, "the slot's outgoing group is closed all the same");
        TEST_EXEC_ASSERT(UITree_FindByComponentId(app->tree, old_root) < 0, "and unloaded");
        TEST_EXEC_ASSERT(UITree_InterfaceParentFind(app->tree, slot) < 0, "the slot records nothing");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        ToriRS_TaskQueue_Free(app->frame_runner.queue);
        App_PendingFree(app);
        Assets_Free(app->assets);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        CacheProvider_FreeEngineCaches(provider);
        free(no_loads);
        free(provider);
        ToriRS_IOBatch_Free(io);
        free(app);
        printf("ok - an IF_OPENSUB whose pack is missing closes the slot and reports no mount\n");
    }

    /*
     * U7: IF_MOVESUB relinks the moved group (class415.method9481): the
     * source's record moves to the destination and the group's nodes go with
     * it. It used to queue a close of the source -- which unloaded the group
     * -- and an open at the destination, which re-baked it from the pack, so
     * whatever its scripts had built was lost.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
        struct UITreeNodeSpec spec;
        struct UITreeBehavior layer_behavior;
        int const source = (161 << 16) | 70;
        int const dest = (161 << 16) | 71;
        int const moved_root = (500 << 16) | 0;
        int const moved_child = (500 << 16) | 1;
        int32_t dest_idx;
        int32_t src_idx;
        int32_t root_idx;
        int32_t child_idx;
        int rec;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->frame_runner.queue = ToriRS_TaskQueue_New();
        app->assets = Assets_New();
        App_PendingInit(app);
        app->phase.app_state = APP_STATE_READY;
        app->tree = UITree_New(8);
        memset(&spec, 0, sizeof(spec));
        memset(&layer_behavior, 0, sizeof(layer_behavior));
        spec.behavior = &layer_behavior;
        spec.type = UIELEM_RS_LAYER;
        spec.width = 100;
        spec.height = 100;
        spec.component_id = source;
        src_idx = UITree_Push(app->tree, -1, &spec);
        spec.component_id = dest;
        dest_idx = UITree_Push(app->tree, -1, &spec);
        spec.component_id = moved_root;
        root_idx = UITree_Push(app->tree, src_idx, &spec);
        spec.component_id = moved_child;
        child_idx = UITree_Push(app->tree, root_idx, &spec);
        TEST_EXEC_ASSERT(src_idx >= 0 && dest_idx >= 0 && root_idx >= 0 && child_idx >= 0,
                         "the slots and the moved group are in the tree");
        UITree_InterfaceParentSet(app->tree, source, 500, 1);

        App_MoveSubInterface(app, source, dest);
        TEST_EXEC_ASSERT(app->packet_runner.queue->head && !app->packet_runner.queue->head->next,
                         "IF_MOVESUB queues one relink, not a close and an open");
        TEST_EXEC_ASSERT(App_SlotMountQueued(app, dest, 500, 1), "the destination's queued mount is the moved group");
        TEST_EXEC_ASSERT(!App_SlotMountQueued(app, source, 500, 1), "and the source is empty in wire order");

        (void)ToriRS_TaskQueue_Run(app->packet_runner.queue, io);
        TEST_EXEC_ASSERT(app->packet_runner.queue->head == NULL, "the relink ran");
        TEST_EXEC_ASSERT(UITree_FindByComponentId(app->tree, moved_child) == child_idx,
                         "the moved group's nodes are kept, not unloaded and re-baked");
        TEST_EXEC_ASSERT(app->tree->components[root_idx].parent == dest_idx,
                         "the moved group hangs under the destination");
        rec = UITree_InterfaceParentFind(app->tree, dest);
        TEST_EXEC_ASSERT(rec >= 0 && app->tree->interface_parents[rec].group_id == 500,
                         "the destination records the moved group");
        TEST_EXEC_ASSERT(UITree_InterfaceParentFind(app->tree, source) < 0, "the source records nothing");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        ToriRS_TaskQueue_Free(app->frame_runner.queue);
        App_PendingFree(app);
        Assets_Free(app->assets);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        ToriRS_IOBatch_Free(io);
        free(app);
        printf("ok - IF_MOVESUB relinks the moved group instead of closing and re-baking it\n");
    }

    /*
     * U7: an IF_RESYNC whose top changed restates mounts App_RootMountsReturn
     * has already queued. The live snapshot the top emptied cannot say so, and
     * each such slot got a second mount task (a remount that also dropped the
     * group's arming). A queued mount of the same group counts as mounted.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        int const slot = (0 << 16) | 9; /* a slot of the root (id 0 here) */
        struct PktIfResyncMount mount = { slot, 399, 1 };
        struct RevPacket p;
        int queued = 0;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->tree = UITree_New(2);
        App_ServerStateBind(app, &app_state);

        /* What the root change queued for its kept mount. */
        App_OpenSubInterface(app, slot, 399, 1);
        memset(&p, 0, sizeof(p));
        p.packet_type = PKT_NAME_IF_RESYNC_V2;
        p._if_resync.root_interface_id = 0;
        p._if_resync.mount_count = 1;
        p._if_resync.mounts = &mount;
        App_GameProto_Exec(app, &app_state, &p);
        for( struct ToriRS_Task* t = app->packet_runner.queue->head; t; t = t->next )
            queued++;
        TEST_EXEC_ASSERT(queued == 1, "a restated mount already queued is not queued again");

        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        UITree_Free(app->tree);
        UIIfEventTable_Free(&app->if_events);
        free(app);
        printf("ok - IF_RESYNC does not mount a slot twice after a root change\n");
    }

    /*
     * IF_SETPLAYERMODEL_*: the build is a parallel task (it holds nothing on
     * the packet queue), keyed on the widget and its close serial, and a
     * newer composition's build replaces an older one still loading.
     */
    {
        struct App* app = calloc(1, sizeof(*app));
        struct UIIfPlayerModel* model;
        struct RS_PendingKey key;

        assert(app);
        app->packet_runner.queue = ToriRS_TaskQueue_New();
        app->frame_runner.queue = ToriRS_TaskQueue_New();
        app->assets = Assets_New();
        App_PendingInit(app);
        model = UIIfModels_ClaimPlayerModel(
            &app->if_models, (84 << 16) | 4, UITREE_SCENE_IF_PLAYER_MODEL_BASE, 0);
        model->composition = ++app->if_models.composition_serial;
        App_IfPlayerModelBuildAdd(app, model);
        key = RS_PendingKey_Component((84 << 16) | 4, 0);
        TEST_EXEC_ASSERT(RS_Pending_Find(app->pending, APP_TASK_IF_PLAYER_MODEL_BUILD, key) != NULL,
                         "the build is a pending parallel task");
        TEST_EXEC_ASSERT(app->packet_runner.queue->head == NULL, "and nothing waits on the packet queue");
        TEST_EXEC_ASSERT(app->frame_runner.queue->head != NULL, "it is on the parallel queue");
        model->composition = ++app->if_models.composition_serial;
        App_IfPlayerModelBuildAdd(app, model);
        TEST_EXEC_ASSERT(RS_Pending_Count(app->pending, APP_TASK_IF_PLAYER_MODEL_BUILD) == 1,
                         "a newer composition's build replaces the older one");

        ToriRS_TaskQueue_Free(app->frame_runner.queue);
        ToriRS_TaskQueue_Free(app->packet_runner.queue);
        App_PendingFree(app);
        Assets_Free(app->assets);
        UIIfModels_Free(&app->if_models);
        free(app);
        printf("ok - a player-model build is a parallel task, newest composition wins\n");
    }

    UIIfTextStore_Free(&base_app->if_texts);
    UIIfIntStore_Free(&base_app->if_hides);
    UIIfIntStore_Free(&base_app->if_colours);
    UIIfEventTable_Free(&base_app->if_events);
    free(base_app);
    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    printf("net-exec: all tests passed\n");
    return 0;
}
