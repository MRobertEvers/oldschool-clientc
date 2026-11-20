/*
 * The headless client's core (scriptrun/scriptrun_core.h) fed the server's own
 * osrs242 wire bytes: every packet here is encoded by the RSProt codec the
 * server's 242 writer uses (torirsserver/wire/osrs242/osrs242_wire.c
 * w242_run) and handed to ScriptrunCore_Packet under the revision's opcode, so
 * the core's parse, its model and its presentation all run as in a bot.
 *
 *   - IF_MOVESUB moves a mount: the destination holds the group, the source
 *     nothing, and an IF_CLOSESUB at the destination closes it (sub_closed).
 *   - The models the server sets on MODEL components (IF_SETNPCHEAD) are kept
 *     until another group takes the slot.
 *   - The per-player entity setters (ANIM_SPECIFIC, NPC_ANIM_SPECIFIC,
 *     NPC_SPOTANIM_SPECIFIC, PLAYER_SPOTANIM_SPECIFIC, NPC_HEADICON_SPECIFIC)
 *     reach the core's World through the packet model, as on the real client.
 *
 * Links the server's objects (the core needs its npc records and its wire).
 * Asserts on.
 *
 *   make -C src test-scriptrun-core
 */
#include "game/rs_entity_sync.h"
#include "game/rs_event_ring.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/rev_list.h"
#include "scriptrun/scriptrun_core.h"
#include "world/world.h"

#include "rsprot_buf.h"
#include "rsprot_exec.h"
#include "packets/anim_specific.h"
#include "packets/if_closesub.h"
#include "packets/if_movesub.h"
#include "packets/if_opensub.h"
#include "packets/if_setnpchead.h"
#include "packets/npc_anim_specific.h"
#include "packets/npc_headicon_specific.h"
#include "packets/npc_spotanim_specific.h"
#include "packets/player_spotanim_specific.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define UID(group, child) (((group) << 16) | (child))

/* The revision's wire opcode for `pkt_name`, read off the core's own table
 * (the server's lists only what it sends). */
static int
wire_of(
    struct ScriptrunCore const* core,
    int pkt_name)
{
    for( int wire = 0; wire < 256; wire++ )
        if( core->rev->packetin_code(wire) == pkt_name )
            return wire;
    assert(0 && "no osrs242 opcode for the packet");
    return -1;
}

/* One packet: encoded by its RSProt table at revision 242 (as w242_run does)
 * and handed to the core under the revision's opcode for it. */
static void
send(
    struct ScriptrunCore* core,
    int pkt_name,
    RsprotVersionRange const* ranges,
    int count,
    void* msg)
{
    uint8_t bytes[256];
    RsprotBuf b;
    RsprotExec x;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, rsprot_osrs(242));

    assert(fn);
    rsprot_buf_wrap(&b, bytes, (int32_t)sizeof(bytes));
    rsprot_exec_encode(&x, &b);
    fn(&x, msg);
    assert(rsprot_exec_ok(&x));
    assert(!b.err);
    ScriptrunCore_Packet(core, wire_of(core, pkt_name), bytes, (int)b.wpos);
}

#define SEND(core, pkt_name, table, msg) send((core), (pkt_name), table, table##_count, &(msg))

/* An event of `kind` with these first two fields was stamped. */
static int
has_event(
    struct ScriptrunCore const* core,
    int kind,
    int a,
    int b)
{
    for( int i = 0; i < SCRIPTRUN_EVENTS; i++ )
    {
        struct RS_Event const* ev = &core->event_entries[i];
        if( ev->serial != 0 && ev->kind == kind && ev->a == a && ev->b == b )
            return 1;
    }
    return 0;
}

static struct WorldEntityFacet_IdleAnimations
no_idle(void)
{
    struct WorldEntityFacet_IdleAnimations idle;

    memset(&idle, 0xff, sizeof(idle));
    return idle;
}

static void
open_sub(
    struct ScriptrunCore* core,
    int target,
    int group)
{
    MsgIfOpenSub m = { .type = 1, .interface_id = group, .destination_combined_id = target };
    SEND(core, PKT_NAME_IF_OPENSUB, rsprot_if_opensub_out, m);
}

static void
test_movesub(void)
{
    struct ScriptrunCore* core = ScriptrunCore_New(GameProtoRev_OSRS242());
    int const source = UID(161, 5);
    int const dest = UID(161, 9);

    printf("TEST: IF_MOVESUB moves the mount, and the destination closes it\n");
    open_sub(core, source, 300);
    CHECK(ScriptrunCore_MountedAt(core, source) == 300, "the group is mounted at the source");
    {
        MsgIfMoveSub m = { .source_combined_id = source, .destination_combined_id = dest };
        SEND(core, PKT_NAME_IF_MOVESUB, rsprot_if_movesub_out, m);
    }
    CHECK(ScriptrunCore_MountedAt(core, dest) == 300, "after IF_MOVESUB the destination holds the group");
    CHECK(ScriptrunCore_MountedAt(core, source) == -1, "after IF_MOVESUB the source holds nothing");
    CHECK(has_event(core, RS_EVENT_SUB_OPENED, dest, 300), "the move raised sub_opened at the destination");
    CHECK(ScriptrunCore_InterfaceOpen(core, 300), "the moved group is still open");
    {
        MsgIfCloseSub m = { .combined_id = dest };
        SEND(core, PKT_NAME_IF_CLOSESUB, rsprot_if_closesub_out, m);
    }
    CHECK(has_event(core, RS_EVENT_SUB_CLOSED, dest, 300),
        "IF_CLOSESUB at the destination closes the moved group (sub_closed)");
    CHECK(!ScriptrunCore_InterfaceOpen(core, 300), "the moved group is closed");

    /* A move onto an occupied slot closes what was there, and a move from an
     * empty slot still closes what it lands on (class415.method9481). */
    open_sub(core, source, 300);
    open_sub(core, dest, 301);
    {
        MsgIfMoveSub m = { .source_combined_id = source, .destination_combined_id = dest };
        SEND(core, PKT_NAME_IF_MOVESUB, rsprot_if_movesub_out, m);
    }
    CHECK(ScriptrunCore_MountedAt(core, dest) == 300, "the moved group replaces the destination's");
    CHECK(!ScriptrunCore_InterfaceOpen(core, 301), "the destination's old group is closed");
    {
        MsgIfMoveSub m = { .source_combined_id = UID(161, 12), .destination_combined_id = dest };
        SEND(core, PKT_NAME_IF_MOVESUB, rsprot_if_movesub_out, m);
    }
    CHECK(ScriptrunCore_MountedAt(core, dest) == -1, "a move from an empty slot closes the destination");
    CHECK(!ScriptrunCore_InterfaceOpen(core, 300), "and the group that was there");
    ScriptrunCore_Free(core);
}

static void
test_chathead(void)
{
    struct ScriptrunCore* core = ScriptrunCore_New(GameProtoRev_OSRS242());
    int const slot = UID(162, 567);
    int const head = UID(231, 2);
    struct UIIfHead const* h;

    printf("TEST: IF_SETNPCHEAD is kept until another group takes the slot\n");
    open_sub(core, slot, 231);
    {
        MsgIfSetNpcHead m = { .npc = 4626, .combined_id = head };
        SEND(core, PKT_NAME_IF_SETNPCHEAD, rsprot_if_setnpchead_out, m);
    }
    h = ScriptrunCore_Head(core, head);
    CHECK(h && h->kind == UI_IFHEAD_NPC && h->npc_id == 4626, "the chathead is the cook's");
    open_sub(core, slot, 231);
    h = ScriptrunCore_Head(core, head);
    CHECK(h && h->npc_id == 4626, "the same group reopened in its slot keeps it");
    open_sub(core, slot, 217);
    CHECK(ScriptrunCore_Head(core, head) == NULL, "another group in the slot unloads it");
    ScriptrunCore_Free(core);
}

static void
test_entity_setters(void)
{
    struct ScriptrunCore* core = ScriptrunCore_New(GameProtoRev_OSRS242());
    struct World* world = core->world;
    int const pid = 7;
    int const slot = 33;
    int pi;
    int ni;
    struct WorldEntity_Player* player;
    struct WorldEntity_NPC* npc;

    printf("TEST: the per-player entity setters reach the World through the model\n");
    ScriptrunCore_SetLocalIndex(core, pid);
    pi = World_PlayerSpawn(world, 1, 0, 50, 50, no_idle());
    player = World_EntityPoolGet(&world->entities.player, pi);
    player->server_pid = pid;
    RS_EntitySync_RegisterPlayer(core->esync, pid, 1, pi);
    ni = World_NpcSpawn(world, 2, 3106, 0, 52, 50, 1, no_idle());
    npc = World_EntityPoolGet(&world->entities.npc, ni);
    RS_EntitySync_RegisterNpc(core->esync, slot, 2, ni);

    {
        MsgAnimSpecific m = { .delay = 0, .id = 808 };
        SEND(core, PKT_NAME_ANIM_SPECIFIC, rsprot_anim_specific_out, m);
    }
    CHECK(player->animation.primary.anim_id == 808, "ANIM_SPECIFIC set the local player's seq");
    {
        MsgNpcAnimSpecific m = { .index = slot, .id = 422, .delay = 0 };
        SEND(core, PKT_NAME_NPC_ANIM_SPECIFIC, rsprot_npc_anim_specific_out, m);
    }
    CHECK(npc->animation.primary.anim_id == 422, "NPC_ANIM_SPECIFIC set the npc's seq");
    CHECK(has_event(core, RS_EVENT_NPC_SEQ, slot, 422), "NPC_ANIM_SPECIFIC stamped npc_seq");
    {
        MsgNpcSpotanimSpecific m = { .id = 86, .index = slot, .slot = 0, .packed = (92 << 16) | 0 };
        SEND(core, PKT_NAME_NPC_SPOTANIM_SPECIFIC, rsprot_npc_spotanim_specific_out, m);
    }
    CHECK(npc->spotanims.count > 0 && npc->spotanims.items[0].id == 86,
        "NPC_SPOTANIM_SPECIFIC put the graphic on the npc");
    {
        MsgPlayerSpotanimSpecific m = { .slot = 0, .id = 87, .index = pid, .packed = (92 << 16) | 0 };
        SEND(core, PKT_NAME_PLAYER_SPOTANIM_SPECIFIC, rsprot_player_spotanim_specific_out, m);
    }
    CHECK(player->spotanims.count > 0 && player->spotanims.items[0].id == 87,
        "PLAYER_SPOTANIM_SPECIFIC put the graphic on the player");
    {
        MsgNpcHeadIconSpecific m = { .head_icon_slot = 1, .sprite_group = 440, .sprite_index = 3, .index = slot };
        SEND(core, PKT_NAME_NPC_HEADICON_SPECIFIC, rsprot_npc_headicon_specific_out, m);
    }
    CHECK(npc->headicon_custom, "NPC_HEADICON_SPECIFIC customised the npc's icons");
    CHECK(npc->headicon_group[1] == 440 && npc->headicon_index[1] == 3, "the slot it named holds the icon");
    ScriptrunCore_Free(core);
}

int
main(void)
{
    test_movesub();
    test_chathead();
    test_entity_setters();
    if( g_failures )
    {
        printf("scriptrun_core_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("scriptrun_core_test: OK\n");
    return 0;
}
