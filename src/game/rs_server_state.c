/*
 * The packet model (rs_server_state.h): one copy of the rules for what a
 * server packet writes. The real client's presentation of the same packets is
 * game/rs_gameproto_exec.c.
 */

#include "game/rs_server_state.h"

#include "engine/cache_provider.h"
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "game/rs_hint_arrow.h"
#include "game/rs_interaction_mode.h"
#include "game/rs_npc_update_origin.h"
#include "game/rs_player_ops.h"
#include "game/rs_player_stats.h"
#include "game/rs_server_tick.h"
#include "game/rs_session_info.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "inv/inv_manager.h"
#include "log/torirs_log.h"
#include "net/jbase37.h"
#include "net/login/login_osrs.h"
#include "net/rev/revpacket.h"
#include "net/session_end.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "varp/varp_manager.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The slot PLAYER_INFO files the local player under until the server has named
 * its pid (task_exec_entity_info.c, ENTITY_INFO_LOCAL_SENTINEL). */
enum
{
    RS_SERVER_STATE_LOCAL_PLAYER_SENTINEL = 2047,
};

int
RS_ServerState_SlotGroupLeaves(
    struct RS_ServerState const* state,
    int group,
    int incoming_group)
{
    assert(state);
    assert(state->if_events);
    assert(state->if_texts);
    assert(state->if_hides);
    assert(state->if_colours);
    assert(group > 0);
    UIIfEventTable_DropGroup(state->if_events, group);
    if( group == incoming_group )
        return 0;
    UIIfTextStore_DropGroup(state->if_texts, group);
    UIIfIntStore_DropGroup(state->if_hides, group);
    UIIfIntStore_DropGroup(state->if_colours, group);
    return 1;
}

int
RS_ServerState_InvContainerId(
    int inv_id,
    int component_id)
{
    return inv_id > 0 ? inv_id : component_id;
}

int
RS_ServerState_Rs15ToRgb(int c)
{
    int r = (c >> 10) & 0x1f;
    int g = (c >> 5) & 0x1f;
    int b = c & 0x1f;
    return ((r << 3) << 16) | ((g << 3) << 8) | (b << 3);
}

void
RS_ServerState_EscapeTags(
    char const* text,
    char* out,
    size_t cap)
{
    size_t written = 0;

    assert(text);
    assert(out);
    assert(cap > 0);
    for( ; *text; text++ )
    {
        char const* piece = *text == '<' ? "<lt>" : *text == '>' ? "<gt>" : NULL;
        size_t const piece_len = piece ? 4 : 1;
        if( written + piece_len >= cap )
            break;
        if( piece )
            memcpy(out + written, piece, piece_len);
        else
            out[written] = *text;
        written += piece_len;
    }
    out[written] = '\0';
}

int
RS_ServerState_LocalPid(struct RS_EntitySync const* esync)
{
    assert(esync);
    return esync->local_pid >= 0 ? esync->local_pid : RS_SERVER_STATE_LOCAL_PLAYER_SENTINEL;
}

static void
state_stamp(
    struct RS_ServerState const* state,
    int kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    assert(state->events.ring);
    RS_EventSink_Stamp(&state->events, kind, a, b, c, d);
}

/* ---- varps ---------------------------------------------------------------- */

/* DRIVE_STAMP: varp_changed -- a=varp_id b=new value. The server-confirmed
 * seam: every server-applied varp (VarPManager's server-update callback fires
 * for exactly these), right after its write. var.await wakes here plus on
 * server_tick, and a varbit name is translated to its base varp before
 * matching. */
static void
state_stamp_varp(
    struct RS_ServerState const* state,
    int varp_id)
{
    state_stamp(
        state, RS_EVENT_VARP_CHANGED, varp_id, VarPManager_GetVarp(state->varps, varp_id), 0, 0);
}

static void
state_varp_synced(
    void* user,
    int varp_id)
{
    state_stamp_varp((struct RS_ServerState const*)user, varp_id);
}

/* ---- inventories ---------------------------------------------------------- */

/* Wire data: an inventory is named by its inv id, or for an old-format
 * component by the component's uid, and neither is negative. A packet that
 * names neither is refused here with a line, the one wire refusal the two
 * inventory writes share. */
static int
state_inv_container_named(int container)
{
    if( container >= 0 )
        return 1;
    TORIRS_LOG("exec: inventory packet for container %d refused\n", container);
    return 0;
}

static enum RS_ServerStateApply
state_update_inv_full(
    struct RS_ServerState const* state,
    struct PktUpdateInvFull const* p)
{
    int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

    assert(state->invs);
    if( !state_inv_container_named(container) )
        return RS_SERVER_STATE_UNCHANGED;
    {
        int const source = InvManager_EnsureContainer(state->invs, container, p->size, "server-inv");
        /* Full is the table being too small for a session, not a packet to drop. */
        assert(source >= 0);
        (void)source;
    }
    InvManager_ApplyFull(state->invs, container, p->obj_ids, p->obj_counts, p->size);
    /* DRIVE_STAMP: inv_packet -- a=container_id. After the write, whose
     * change callback has already stamped inv_changed. */
    state_stamp(state, RS_EVENT_INV_PACKET, container, 0, 0, 0);
    if( getenv("TORIRS_INV_DEBUG") )
    {
        TORIRS_LOG("inv-full: container=%d (com 0x%08x) size=%d\n",
            container,
            (unsigned)p->component_id,
            p->size);
        for( int i = 0; i < p->size; i++ )
            if( p->obj_ids[i] > 0 )
                TORIRS_LOG("  slot %2d obj=%d x%d\n", i, p->obj_ids[i], p->obj_counts[i]);
    }
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_inv_partial(
    struct RS_ServerState const* state,
    struct PktUpdateInvPartial const* p)
{
    int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

    assert(state->invs);
    if( !state_inv_container_named(container) )
        return RS_SERVER_STATE_UNCHANGED;
    {
        int const source = InvManager_EnsureContainer(state->invs, container, 0, "server-inv");
        /* Full is the table being too small for a session, not a packet to drop. */
        assert(source >= 0);
        (void)source;
    }
    /* DRIVE_STAMP: inv_packet -- a=container_id. Before the slots, so it
     * precedes their inv_changed stamps, as it always has. */
    state_stamp(state, RS_EVENT_INV_PACKET, container, 0, 0, 0);
    if( getenv("TORIRS_INV_DEBUG") )
        TORIRS_LOG("inv-partial: container=%d (com 0x%08x) slots=%d\n",
            container,
            (unsigned)p->component_id,
            p->count);
    for( int i = 0; i < p->count; i++ )
    {
        /* Addressed by CONTAINER id, as UPDATE_INV_FULL is. This used to go
         * through InvManager_SetSlot, which takes a SOURCE index and reads any
         * small number as one: a shop's stock is inv 1, sources[1] is the worn
         * equipment, so buying a steel axe at Bob's wrote "steel axe x2" (the
         * shop's new stock line) into the weapon slot (recording
         * mrobertevers/20261115-170422, tick 178). ApplyPartial also marks a
         * changed slot iconless (INV_MANAGER_NO_SCENE_ID) for the reconcile. */
        int const slot = p->entries[i].slot;
        int const obj_id = p->entries[i].obj_id;
        int const obj_count = p->entries[i].count;
        InvManager_ApplyPartial(state->invs, container, &slot, &obj_id, &obj_count, 1);
        if( getenv("TORIRS_INV_DEBUG") )
            TORIRS_LOG("  slot %2d obj=%d x%d\n", slot, obj_id, obj_count);
    }
    return RS_SERVER_STATE_APPLIED;
}

/* ---- chat ------------------------------------------------------------------ */

/* One line into the store, stamped with the owner's client clock. The store
 * stamps chat_message itself (RS_Chat_AddMessage), so every insertion -- this
 * one and a client script's -- raises it once. */
static void
state_chat_add(
    struct RS_ServerState const* state,
    int type,
    char const* name,
    char const* sender,
    char const* text)
{
    assert(state->chat);
    assert(state->chat_clock);
    RS_Chat_AddMessage(state->chat, type, name, sender, text, *state->chat_clock);
}

/* The reference's tutorial gate on friend-channel and private lines
 * (RS_Chat_InTutorialArea): computed from the local player's tile, scene-local
 * plus the root's base as the deob's (fine >> 7) + field3266's base. No local
 * player yet is outside (method6894 clears the flag and returns). */
static int
state_local_player_in_tutorial_area(struct RS_ServerState const* state)
{
    struct WorldEntity_Player const* self;
    int world_idx;

    /* No world yet is a real state: no local player either. */
    if( !state->world )
        return 0;
    if( state->esync->local_pid < 0 ||
        !RS_EntitySync_FindPlayer(state->esync, state->esync->local_pid, &world_idx, NULL) )
        return 0;
    self = World_EntityPoolGet(&state->world->entities.player, world_idx);
    if( !self )
        return 0;
    return RS_Chat_InTutorialArea(
        state->world->_base_tile_x + self->grid_position.x,
        state->world->_base_tile_z + self->grid_position.z);
}

bool
RS_ServerState_PublicChatShown(
    struct RS_ServerState const* state,
    char const* sender,
    bool ignorable)
{
    assert(state);
    assert(state->social);
    /* class95.method3131: a speaker with no name yet (field531) says
     * nothing; an ignorable crown's wearer on the ignore list is silent;
     * and nobody is heard while the local player is on Tutorial Island
     * (client.field765). */
    if( !sender )
        return false;
    if( ignorable && RS_Social_IsIgnored(state->social, sender) )
        return false;
    return !state_local_player_in_tutorial_area(state);
}

/* MESSAGE_FRIENDCHANNEL: deob client.java class259.field3189. Shares the message
 * id ring with the other chat prots, and the crown decides whether the sender's
 * ignore applies. The channel's name is what the store keeps in `sender`, as a
 * clan channel line does. */
static enum RS_ServerStateApply
state_message_friendchannel(
    struct RS_ServerState const* state,
    struct PktMessageFriendChannel const* message)
{
    char channel[16];
    char escaped[RS_CHAT_TEXT_LEN];
    char name[RS_CHAT_SENDER_LEN];
    int ignorable;
    int64_t const key = ((int64_t)message->world << 32) + message->counter;

    assert(state->social);
    if( RS_Social_MessageSeen(state->social, key) )
        return RS_SERVER_STATE_UNCHANGED;
    RS_Chat_CrownImage(message->crown, &ignorable);
    if( ignorable && RS_Social_IsIgnored(state->social, message->sender) )
        return RS_SERVER_STATE_UNCHANGED;
    /* The gate this used to lack (deob client.java class243.field2995,
     * `!seen && field765 == 0`): on Tutorial Island no friend-channel line is
     * shown, nor remembered as seen. */
    if( state_local_player_in_tutorial_area(state) )
        return RS_SERVER_STATE_UNCHANGED;
    RS_Social_MessageRemember(state->social, key);

    base37tostr((uint64_t)message->channel_name, channel, sizeof(channel));
    RS_ServerState_EscapeTags(message->text ? message->text : "", escaped, sizeof(escaped));
    RS_Chat_CrownedName(message->crown, message->sender, name, sizeof(name));
    state_chat_add(state, RS_CHAT_TYPE_FRIENDS_CHAT, name, channel, escaped);
    return RS_SERVER_STATE_APPLIED;
}

/* ---- social ---------------------------------------------------------------- */

/* deob Statics.method10091: each entry finds its friend by name (the
 * Username, so case and space/underscore/NBSP do not tell two apart) and,
 * when its leading byte is 1, by its previous name too: found under both as
 * two entries, the previous one goes (Statics.method10612); found under
 * either, the entry takes the wire's names (a rename, class473.method10601).
 * Found under neither, it is added, and a new friend says nothing. An entry
 * already on the list whose world changes queues the login or logout notice
 * (RS_Social_FriendWorldNotice), stamped with the client clock. */
static void
state_update_friendlist(
    struct RS_ServerState const* state,
    struct PktUpdateFriendList const* p)
{
    struct RS_Social* social = state->social;

    assert(social);
    assert(state->chat_clock);
    social->server_status = 2;
    if( p->count == 0 )
        return;
    assert(p->entries);
    for( int e = 0; e < p->count; e++ )
    {
        struct PktUpdateFriendListEntry const* entry = &p->entries[e];
        int index = RS_Social_FriendIndex(social, entry->name);

        if( entry->match_previous && entry->previous_name[0] )
        {
            int const previous = RS_Social_FriendIndex(social, entry->previous_name);

            if( previous >= 0 && previous != index )
            {
                if( index < 0 )
                    index = previous;
                else
                {
                    RS_Social_DelFriendAt(social, previous);
                    if( previous < index )
                        index--;
                }
            }
        }
        if( index >= 0 )
        {
            RS_Social_SetFriendNames(social, index, entry->name, entry->previous_name);
            if( social->friend_world[index] != entry->world )
                RS_Social_FriendWorldNotice(social, entry->name, entry->world, *state->chat_clock);
            RS_Social_SetFriendWorld(social, index, entry->world);
        }
        else if( RS_Social_AddFriend(social, entry->name, entry->world) )
        {
            index = social->friend_count - 1;
            RS_Social_SetFriendNames(social, index, entry->name, entry->previous_name);
        }
        else
            continue;
        social->friend_rank[index] = entry->rank;
    }
    /* The client re-applies the scripts' sort chain on every friend update
     * (class476.java:207), so a world change re-orders the list without the
     * panel asking. */
    RS_Social_SortFriends(social);
}

/* The login and logout notices due by now, each a type-5 line with no name
 * (deob class58.method1165, run every client cycle: Statics.method7544(5,
 * "", name + " has logged in.")). A client calls this from its own clock. */
void
RS_ServerState_FriendNotices(
    struct RS_Social* social,
    struct RS_Chat* chat,
    int clock)
{
    char text[RS_CHAT_TEXT_LEN];

    assert(social);
    assert(chat);
    while( RS_Social_TakeDueNotice(social, clock, text, (int)sizeof(text)) )
        RS_Chat_AddMessage(chat, RS_CHAT_TYPE_PRIVATE_SYSTEM, "", NULL, text, clock);
}

/* ---- session --------------------------------------------------------------- */

/* The deob's CRC-32 (class643.field7000). */
static uint32_t
state_crc32(
    uint8_t const* data,
    size_t length)
{
    uint32_t crc = 0xffffffffu;

    for( size_t i = 0; i < length; i++ )
    {
        crc ^= data[i];
        for( int bit = 0; bit < 8; bit++ )
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void
state_interaction_mode(
    struct RS_ServerState const* state,
    struct PktInteractionMode const* p,
    int reset)
{
    int world_id;
    int slot;

    assert(state->interaction_mode);
    assert(state->worldviews);
    /* Wire data, guarded like SET_ACTIVE_WORLD's: a mode the tables do not
     * have, or a view past the registry, is refused. */
    if( !reset &&
        (p->tile_mode < 0 || p->tile_mode > 2 || p->entity_mode < 0 || p->entity_mode > 3) )
    {
        TORIRS_LOG("exec: interaction modes %d/%d refused\n", p->tile_mode, p->entity_mode);
        return;
    }
    /* The id arrives as the wire's unsigned u16: the deob reads it as a short,
     * so -2 (every view) is 65534 here and -1 is 65535. */
    world_id = (int16_t)p->world_id;
    if( world_id == -2 )
    {
        /* class61.method1414: the defaults; a reset of them is moot. */
        if( !reset )
            RS_InteractionMode_SetDefaults(state->interaction_mode, p->tile_mode, p->entity_mode);
        return;
    }
    /* The root's id is the revision's: -1 before 237, 0 from it (the same move
     * SET_ACTIVE_WORLD's adapter folds). */
    assert(state->rev_number >= 0);
    slot = WorldviewRegistry_SlotForWire(
        state->worldviews,
        (world_id == 0 && state->rev_number >= 237) ? WORLDVIEW_WIRE_ROOT : world_id);
    if( slot == WORLDVIEW_SLOT_NONE )
    {
        /* A view the client has not created yet: kept by the server's id and
         * taken by the view when it is (deob class61.method1382 / method1412
         * key their maps by world id, whether that world exists or not). */
        if( reset )
            RS_InteractionMode_ForgetHeld(state->interaction_mode, world_id);
        else if( RS_InteractionMode_Hold(
                     state->interaction_mode, world_id, p->tile_mode, p->entity_mode) )
            TORIRS_LOG("exec: interaction modes held for %d views; the oldest is forgotten\n",
                RS_INTERACTION_MODE_HELD_MAX);
        return;
    }
    if( reset )
        RS_InteractionMode_ResetView(state->interaction_mode, slot);
    else
        RS_InteractionMode_SetView(state->interaction_mode, slot, p->tile_mode, p->entity_mode);
}

/* ---- entities -------------------------------------------------------------- */

/*
 * ANIM_SPECIFIC: a seq on the local player only (deob Statics.method6644, the
 * player SEQUENCE block's rule, which World_PlayerSetPrimaryAnimation is). The
 * client lands the held models it needs (presentation).
 */
static enum RS_ServerStateApply
state_anim_specific(
    struct RS_ServerState const* state,
    struct PktAnimSpecific const* pkt)
{
    int idx = -1;
    int element = -1;

    /* app->world NULL is a real state (fa5680f77): nothing to set. */
    if( !state->world )
        return RS_SERVER_STATE_UNCHANGED;
    RS_EntitySync_FindPlayer(state->esync, RS_ServerState_LocalPid(state->esync), &idx, &element);
    if( idx < 0 )
        return RS_SERVER_STATE_UNCHANGED;
    World_PlayerSetPrimaryAnimation(state->world, idx, pkt->id, pkt->delay);
    return RS_SERVER_STATE_APPLIED;
}

/* NPC_ANIM_SPECIFIC: the npc SEQUENCE block's rule for one player (deob
 * client.java class259.field3183); an npc this client has not been told about
 * is ignored, as the deob's null check is. */
static enum RS_ServerStateApply
state_npc_anim_specific(
    struct RS_ServerState const* state,
    struct PktNpcAnimSpecific const* pkt)
{
    int idx = -1;
    int element = -1;

    /* app->world NULL is a real state (fa5680f77): nothing to set. */
    if( !state->world )
        return RS_SERVER_STATE_UNCHANGED;
    RS_EntitySync_FindNpc(state->esync, pkt->index, &idx, &element);
    if( idx < 0 )
        return RS_SERVER_STATE_UNCHANGED;
    World_NpcSetPrimaryAnimation(state->world, idx, pkt->id, pkt->delay);
    /* DRIVE_STAMP: npc_seq -- a=npc server slot b=seq c=server tick. */
    state_stamp(
        state,
        RS_EVENT_NPC_SEQ,
        pkt->index,
        pkt->id,
        (int32_t)(state->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES),
        0);
    return RS_SERVER_STATE_APPLIED;
}

/* NPC_SPOTANIM_SPECIFIC / PLAYER_SPOTANIM_SPECIFIC: class83.method2252, the
 * spotanim block's setter, for one player. */
static enum RS_ServerStateApply
state_spotanim_specific(
    struct RS_ServerState const* state,
    bool npc,
    struct PktEntitySpotanimSpecific const* pkt)
{
    int idx = -1;
    int element = -1;

    /* app->world NULL is a real state (fa5680f77): nothing to set. */
    if( !state->world )
        return RS_SERVER_STATE_UNCHANGED;
    if( npc )
        RS_EntitySync_FindNpc(state->esync, pkt->index, &idx, &element);
    else
        RS_EntitySync_FindPlayer(state->esync, pkt->index, &idx, &element);
    if( idx < 0 )
        return RS_SERVER_STATE_UNCHANGED;
    if( npc )
        World_NpcSetSpotanim(state->world, idx, pkt->slot, pkt->id, pkt->height, pkt->delay, false);
    else
        World_PlayerSetSpotanim(
            state->world, idx, pkt->slot, pkt->id, pkt->height, pkt->delay, false);
    return RS_SERVER_STATE_APPLIED;
}

/*
 * NPC_HEADICON_SPECIFIC: one slot of one npc's head icons, for this player
 * (deob class86.method2936). The first write copies the type's icons in
 * (`new class629(type)`); an npc this client does not track is skipped, as the
 * reference's lookup misses.
 */
static enum RS_ServerStateApply
state_npc_headicon_specific(
    struct RS_ServerState const* state,
    struct PktNpcHeadiconSpecific const* pkt)
{
    int group[WORLD_NPC_HEADICON_SLOTS];
    int index[WORLD_NPC_HEADICON_SLOTS];
    struct WorldEntity_NPC* npc;
    int idx;

    /* app->world NULL is a real state (fa5680f77): nothing to set. */
    if( !state->world )
        return RS_SERVER_STATE_UNCHANGED;
    if( pkt->slot < 0 || pkt->slot >= WORLD_NPC_HEADICON_SLOTS )
    {
        TORIRS_LOG("exec: NPC_HEADICON_SPECIFIC slot %d refused\n", pkt->slot);
        return RS_SERVER_STATE_UNCHANGED;
    }
    if( !RS_EntitySync_FindNpc(state->esync, pkt->npc_index, &idx, NULL) )
        return RS_SERVER_STATE_UNCHANGED;
    npc = World_EntityPoolAt(&state->world->entities.npc, idx);
    for( int i = 0; i < WORLD_NPC_HEADICON_SLOTS; i++ )
    {
        group[i] = npc->headicon_custom ? npc->headicon_group[i] : -1;
        index[i] = npc->headicon_custom ? npc->headicon_index[i] : -1;
    }
    if( !npc->headicon_custom )
    {
        struct ToriRS_Npctype* type;

        assert(state->provider);
        type = CacheProvider_NpctypeGet(state->provider, npc->npc_id);
        if( type )
        {
            group[0] = type->head_icon_group;
            index[0] = type->head_icon_index;
        }
    }
    group[pkt->slot] = pkt->group;
    index[pkt->slot] = pkt->index;
    World_NpcSetHeadiconCustom(state->world, idx, true, group, index);
    return RS_SERVER_STATE_APPLIED;
}

/* ---- the switch -------------------------------------------------------------- */

static enum RS_ServerStateApply
state_varp_small(
    struct RS_ServerState const* state,
    struct PktVarpSmall const* p)
{
    if( VarPManager_ApplySmall(
            state->varps, p->variable, p->value) )
        state_stamp_varp(state, p->variable);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_varp_large(
    struct RS_ServerState const* state,
    struct PktVarpLarge const* p)
{
    if( VarPManager_ApplyLarge(
            state->varps, p->variable, p->value) )
        state_stamp_varp(state, p->variable);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_varp_long(
    struct RS_ServerState const* state,
    struct PktVarpLong const* p)
{
    if( VarPManager_ApplyLong(
            state->varps, p->variable, p->value) )
        state_stamp_varp(state, p->variable);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_varp_sync(
    struct RS_ServerState const* state)
{
    /* Restore the client-visible copy from the server-authoritative set
     * (rev-230 client.java VARP_SYNC / LostCity Client.ts:7390). Distinct
     * from VARP_RESET, which zeroes both. */
    VarPManager_ApplySyncEach(state->varps, state_varp_synced, (void*)state);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_varp_reset(
    struct RS_ServerState const* state)
{
    VarPManager_ResetAll(state->varps);
    /* The reset's one server update is id -1, "every varp". */
    state_stamp_varp(state, -1);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_stat(
    struct RS_ServerState const* state,
    struct PktUpdateStat const* p)
{
    if( p->stat < 0 ||
        p->stat >= RS_PLAYER_STATS_SKILL_COUNT )
        return RS_SERVER_STATE_UNCHANGED;
    RS_PlayerStats_SetXp(state->stats, p->stat, p->xp);
    state->stats->current_level[p->stat] = p->level;
    RS_PlayerStats_RecomputeCombatLevel(state->stats);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_runenergy(
    struct RS_ServerState const* state,
    struct PktUpdateRunEnergy const* p)
{
    state->stats->run_energy_raw = p->run_energy_raw;
    if( state->stats->run_energy == p->run_energy )
        return RS_SERVER_STATE_UNCHANGED;
    state->stats->run_energy = p->run_energy;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_runweight(
    struct RS_ServerState const* state,
    struct PktUpdateRunWeight const* p)
{
    if( state->stats->run_weight == p->run_weight )
        return RS_SERVER_STATE_UNCHANGED;
    state->stats->run_weight = p->run_weight;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_inv_stop_transmit(
    struct RS_ServerState const* state,
    struct PktUpdateInvStopTransmit const* p)
{
    /* Reference clears the bound inventory's slots (linkObjType[i] = -1).
     * ApplyFull with count 0 empties every slot; a no-op if the container
     * was never transmitted. */
    InvManager_ApplyFull(
        state->invs,
        RS_ServerState_InvContainerId(
            p->inv_id,
            p->component_id),
        NULL,
        NULL,
        0);
    return RS_SERVER_STATE_APPLIED;
}

/* Persisting setters, not one-shot applies: each of these routinely lands
 * before the interface it targets has finished mounting (the mount is an
 * async task), and the reference keeps the value on IfType.list where it
 * survives. The store is the record; the tree is the client's. */
static enum RS_ServerStateApply
state_if_settext(
    struct RS_ServerState const* state,
    struct PktIfSetText const* p)
{
    UIIfTextStore_Set(
        state->if_texts, p->component_id, p->text);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_if_setevents(
    struct RS_ServerState const* state,
    struct PktIfSetEvents const* p)
{
    UIIfEventTable_Set(
        state->if_events,
        p->component_id,
        p->from,
        p->to,
        p->events);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_if_sethide(
    struct RS_ServerState const* state,
    struct PktIfSetHide const* p)
{
    UIIfIntStore_Set(
        state->if_hides, p->component_id, p->hide ? 1 : 0);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_if_setcolour(
    struct RS_ServerState const* state,
    struct PktIfSetColour const* p)
{
    UIIfIntStore_Set(
        state->if_colours,
        p->component_id,
        RS_ServerState_Rs15ToRgb(p->colour));
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_message_game(
    struct RS_ServerState const* state,
    struct PktMessageGame const* p)
{
    /* A line naming a player is dropped when that player is ignored (deob
     * client.java class243.field2978: the name, when sent, goes to the
     * ignore test, Statics.field818.method1168). */
    assert(state->social);
    if( p->name && RS_Social_IsIgnored(state->social, p->name) )
        return RS_SERVER_STATE_UNCHANGED;
    /* The TYPE is the server's, not a constant: it picks the filter tab,
     * the colour and the prefix the chatbox script draws the line with. */
    state_chat_add(
        state,
        p->type,
        p->name,
        p->name,
        p->text);
    /*
     * Printed by default, and that is a deliberate inversion.
     *
     * This used to be gated on TORIRS_NET_DEBUG -- the whole packet
     * firehose -- which made a script's own `mes` output invisible to
     * anyone who had not already guessed that variable. A headless run
     * paints no chatbox, so stderr is the only channel content has, and
     * `mes` is how a debugproc reports what it found. The switch is an
     * opt-OUT for a caller that genuinely wants silence.
     */
    {
        char const* quiet = getenv("TORIRS_MES");

        if( !quiet || strcmp(quiet, "0") != 0 )
            TORIRS_LOG("message_game: %s\n", p->text);
    }
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_message_private(
    struct RS_ServerState const* state,
    struct PktMessagePrivate const* p)
{
    char crowned[RS_CHAT_SENDER_LEN];
    char escaped[RS_CHAT_TEXT_LEN];

    /* deob client.java class243.field2953: the line is shown, and its id
     * remembered, only while the local player is off Tutorial Island
     * (field765 == 0), when the id is not in the ring (reference messageIds
     * ring) and the sender is not ignored (Statics.field818.method1168, for
     * any crown: unlike a friend-channel line's, no crown exempts it). */
    if( state_local_player_in_tutorial_area(state) )
        return RS_SERVER_STATE_UNCHANGED;
    if( RS_Social_MessageSeen(state->social, p->message_id) )
        return RS_SERVER_STATE_UNCHANGED;
    if( RS_Social_IsIgnored(state->social, p->sender) )
        return RS_SERVER_STATE_UNCHANGED;
    RS_Social_MessageRemember(state->social, p->message_id);
    /* As the reference files it: typed 7 for a staff crown only
     * (class278.field3430), the crown's image before the name
     * (method6324(field3451) + name), the text's tags escaped
     * (class671.method14451). `staff_mod` is the wire's crown index. The
     * sender, the line's friend and ignore key, is the name as sent: the
     * reference keys the line by Username(name without its crown)
     * (class76.method2620), so a friend whose name has capitals or an NBSP
     * still reads as one to the private filter (script 91, pm_filtertest). */
    RS_Chat_CrownedName(p->staff_mod, p->sender, crowned, sizeof(crowned));
    RS_ServerState_EscapeTags(p->text ? p->text : "", escaped, sizeof(escaped));
    state_chat_add(
        state,
        RS_Chat_CrownIsStaff(p->staff_mod) ? RS_CHAT_TYPE_PRIVATE_FROM_MOD
                                           : RS_CHAT_TYPE_PRIVATE_FROM,
        crowned,
        p->sender,
        escaped);
    return RS_SERVER_STATE_APPLIED;
}

/* deob client.java class259.field3170: the line the sender sees for a
 * private message they sent. */
static enum RS_ServerStateApply
state_message_private_echo(
    struct RS_ServerState const* state,
    struct PktMessagePrivateEcho const* p)
{
    char escaped[RS_CHAT_TEXT_LEN];

    RS_ServerState_EscapeTags(
        p->text ? p->text : "",
        escaped,
        sizeof(escaped));
    state_chat_add(
        state,
        RS_CHAT_TYPE_PRIVATE_TO,
        p->recipient,
        p->recipient,
        escaped);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_p_countdialog(
    struct RS_ServerState const* state)
{
    state->chat->dialog_input_open = 1;
    state->chat->dialog_input[0] = '\0';
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_friendlist_apply(
    struct RS_ServerState const* state,
    struct PktUpdateFriendList const* p)
{
    state_update_friendlist(state, p);
    return RS_SERVER_STATE_APPLIED;
}

/* One rev-221+ entry, as deob class428.method9746 applies it: a removal
 * deletes the name (class58.method1171 -> method10596); otherwise the entry
 * is found by its name and, when the low bit is set, by its previous name
 * too. Found under both as two entries, the previous one goes (Statics
 * .method10612); found under either, it takes the wire's names (a rename,
 * class473.method10601); found under neither, it is added. */
static void
state_ignore_entry(
    struct RS_Social* social,
    struct PktUpdateIgnoreListEntry const* entry)
{
    int index;

    if( entry->removed )
    {
        (void)RS_Social_DelIgnore(social, entry->name);
        return;
    }
    index = RS_Social_IgnoreIndex(social, entry->name);
    if( entry->match_previous && entry->previous_name[0] )
    {
        int const previous = RS_Social_IgnoreIndex(social, entry->previous_name);

        if( previous >= 0 && previous != index )
        {
            if( index < 0 )
                index = previous;
            else
            {
                RS_Social_DelIgnoreAt(social, previous);
                if( previous < index )
                    index--;
            }
        }
    }
    if( index < 0 )
    {
        if( !RS_Social_AddIgnore(social, entry->name) )
            return;
        index = social->ignore_count - 1;
    }
    RS_Social_SetIgnoreNames(social, index, entry->name, entry->previous_name);
}

/* UPDATE_IGNORELIST. On rev 221+ each entry changes the list held (an add
 * from the server is one entry, not the list); the rs-family wires send the
 * whole list, which replaces it. Names are the wire's spelling: an ignore is
 * found and shown like a friend (RS_Social's key), never through base37
 * text, which lost the capitals and the NBSP. */
static enum RS_ServerStateApply
state_update_ignorelist(
    struct RS_ServerState const* state,
    struct PktUpdateIgnoreList const* p)
{
    struct RS_Social* social = state->social;

    assert(social);
    if( p->replaces )
        social->ignore_count = 0;
    if( p->count == 0 )
        return RS_SERVER_STATE_APPLIED;
    assert(p->entries);
    for( int i = 0; i < p->count; i++ )
        state_ignore_entry(social, &p->entries[i]);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_friendlist_loaded(
    struct RS_ServerState const* state,
    struct PktFriendListLoaded const* p)
{
    /* This is what ends the panel's "Loading friends list" state:
     * RS_Social_FriendCount answers -1 until the status reads 2. */
    state->social->server_status = p->status;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_chat_filter_settings(
    struct RS_ServerState const* state,
    struct PktChatFilterSettings const* p)
{
    /*
     * Only the modes the packet actually carried.
     *
     * The classic wire sends three bytes and the private mode is one of
     * them. Revision 239 sends TWO -- public and trade -- because the
     * private-chat filter moved to a packet of its own: the deob's packet
     * table has `field3058 = new class243(124, 2)`, whose handler writes
     * only the public and trade fields (client.java:2820), beside
     * `field2939 = new class243(5, 1)`, whose handler is the ONLY thing
     * that writes the private filter the CS2 side reads back through
     * chat_getfilter_private (Statics.field5072, opcode 5005).
     *
     * So a revision whose CHAT_FILTER_SETTINGS has no private byte marks
     * the field absent with a negative value -- the same convention the
     * zone headers use for a plane a revision does not send -- and the
     * client's own copy stands. Writing the missing field through was what
     * put "Private On" back under the player the instant after they chose
     * Show friends, and then fed that stale 0 back to the server on their
     * next filter change. CHAT_FILTER_SETTINGS_PRIVATECHAT arrives through
     * this arm with only the private mode present.
     */
    if( p->chat_public_mode >= 0 )
        state->chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC] =
            p->chat_public_mode;
    if( p->chat_private_mode >= 0 )
        state->chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE] =
            p->chat_private_mode;
    if( p->chat_trade_mode >= 0 )
        state->chat_filter_mode[RS_UI_CHAT_FILTER_TRADE] =
            p->chat_trade_mode;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_pid(
    struct RS_ServerState const* state,
    struct PktUpdatePid const* p)
{
    state->esync->local_pid = p->local_player_index;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_last_login_info(
    struct RS_ServerState const* state,
    struct PktLastLoginInfo const* p)
{
    /* The welcome rows are clientCode components RS_ClientCode_Tick
     * refreshes off this struct; opening the screen they sit on is the
     * client's. */
    state->session_info->welcome.last_ip = p->last_ip;
    state->session_info->welcome.days_since_login = p->days_since_login;
    state->session_info->welcome.days_since_recovery =
        p->days_since_recovery;
    state->session_info->welcome.unread_messages = p->unread_messages;
    state->session_info->welcome.member_warning = p->member_warning;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_reboot_timer(
    struct RS_ServerState const* state,
    struct PktUpdateRebootTimer const* p)
{
    /* The wire carries SERVER ticks; the countdown runs on the 20ms client
     * cycle, so it is converted once here rather than at every read
     * (reference `rebootTimer = g2() * 30`, where 30 is the same ratio
     * spelled as a literal). */
    RS_SessionInfo_SetRebootTimer(
        state->session_info, p->ticks, APP_SERVER_TICK_LOGIC_CYCLES);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_multiway(
    struct RS_ServerState const* state,
    struct PktSetMultiway const* p)
{
    return RS_SessionInfo_SetMultiway(state->session_info, p->multiway)
               ? RS_SERVER_STATE_APPLIED
               : RS_SERVER_STATE_UNCHANGED;
}

static enum RS_ServerStateApply
state_enable_tracking(
    struct RS_ServerState const* state)
{
    state->session_info->tracking_enabled = 1;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_finish_tracking(
    struct RS_ServerState const* state)
{
    state->session_info->tracking_enabled = 0;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_unknown_string(
    struct RS_ServerState const* state,
    struct PktText const* p)
{
    snprintf(state->session_info->unknown_string,
             sizeof(state->session_info->unknown_string),
             "%s",
             p->text);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_website_token(
    struct RS_ServerState const* state,
    struct PktText const* p)
{
    snprintf(state->session_info->website_token,
             sizeof(state->session_info->website_token),
             "%s",
             p->text);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_site_settings(
    struct RS_ServerState const* state,
    struct PktText const* p)
{
    snprintf(state->session_info->site_settings,
             sizeof(state->session_info->site_settings),
             "%s",
             p->text);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_logout_transfer(
    struct RS_ServerState const* state,
    struct PktLogoutTransfer const* p)
{
    /* The session moves to another world (deob client.java, the
     * LOGOUT_TRANSFER arm: gameState 45, the socket closed, the world
     * switched; state 45 then logs in with GAMELOGIN and the account's
     * credentials). The move is the link watch's, at the head of the next
     * frame (app_net_link_watch): not here, in the middle of the packet
     * pipeline whose session it ends. */
    TORIRS_LOG("exec: LOGOUT_TRANSFER to world %d (%s)\n", p->world_id,
               p->host);
    SessionEnd_Hop(state->session_end, p->world_id);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_update_uid192(
    struct RS_ServerState const* state,
    struct PktUid192 const* p)
{
    (void)state;
    if( state_crc32(p->data, sizeof(p->data)) == (uint32_t)p->crc )
        login_osrs_set_uid192(p->data);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_player_op(
    struct RS_ServerState const* state,
    struct PktSetPlayerOp const* p)
{
    char const* text = p->text ? p->text : "";

    if( p->slot < 1 || p->slot > 5 )
        return RS_SERVER_STATE_UNCHANGED;
    /* The text "null", in any case, is no op (deob client.java
     * class243.field2940: equalsIgnoreCase(class280.field3641) -> null). */
    if( strcasecmp(text, "null") == 0 )
        text = "";
    RS_PlayerOps_Set(
        state->player_ops,
        p->slot - 1,
        text,
        p->primary);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_interaction_mode(
    struct RS_ServerState const* state,
    struct RevPacket const* packet)
{
    state_interaction_mode(
        state,
        &packet->_interaction_mode,
        packet->packet_type == PKT_NAME_RESET_INTERACTION_MODE);
    return RS_SERVER_STATE_APPLIED;
}

/* deob client.java: Statics.field2218 / field3892 / field4203 = the byte as a
 * boolean; the world menu builders read them (rs_minimenu_world.c). */
static enum RS_ServerStateApply
state_hide_ops(
    struct RS_ServerState const* state,
    struct RevPacket const* packet)
{
    bool const hidden = packet->_hide_ops.hidden != 0;

    if( !state->world )
        return RS_SERVER_STATE_UNCHANGED;
    if( packet->packet_type == PKT_NAME_HIDENPCOPS )
        state->world->hidden_ops.npc = hidden;
    else if( packet->packet_type == PKT_NAME_HIDELOCOPS )
        state->world->hidden_ops.loc = hidden;
    else
        state->world->hidden_ops.obj = hidden;
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_hint_arrow(
    struct RS_ServerState const* state,
    struct PktHintArrow const* p)
{
    /* The type and the tile anchor are folded on the way in
     * (game/rs_hint_arrow.h), so every pass that draws it reads one form. */
    RS_HintArrow_Set(
        state->hint_arrow,
        p->type,
        p->id,
        p->z,
        p->height);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_npc_update_origin(
    struct RS_ServerState const* state,
    struct PktSetNpcUpdateOrigin const* p)
{
    RS_NpcUpdateOrigin_Set(
        state->npc_update_origin,
        p->x,
        p->z);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_set_active_world(
    struct RS_ServerState const* state,
    struct PktSetActiveWorld const* p)
{
    /* Points the zone/rebuild applicators at a world view until the tick
     * fence resets it (deob class100 activeWorld). The id and level are
     * WIRE data -- a raw g2 and a raw byte -- so a value outside the
     * registry, a dead view, or an impossible plane is a malformed or
     * lagging server, guarded rather than asserted: an assert-only check is
     * an abort in debug and an OOB registry/heightmap index in the NDEBUG
     * release lane. The deob throws here; our release build has to refuse
     * instead. */
    int const world_id = p->world_id;
    int slot;

    slot = WorldviewRegistry_SlotForWire(state->worldviews, world_id);
    if( slot == WORLDVIEW_SLOT_NONE )
    {
        TORIRS_LOG("exec: SET_ACTIVE_WORLD refused, world %d not live\n", world_id);
        return RS_SERVER_STATE_UNCHANGED;
    }
    /* The deob snapshots the plane alongside the id, and writes it onto the
     * addressed view itself (class100.field1400), where it outlives the
     * tick fence's return to the root. Clamped to the heightmap's planes:
     * it feeds height sampling. */
    WorldviewRegistry_SetActiveWorld(
        state->worldviews, slot, p->level);
    return RS_SERVER_STATE_APPLIED;
}

static enum RS_ServerStateApply
state_server_tick_end(
    struct RS_ServerState const* state)
{
    /* The fence: every packet of this tick has been applied. */
    RS_ServerTick_Fence(state->server_tick);
    /* SET_ACTIVE_WORLD only lasts until the fence: the deob resets the
     * cursor to the root view at end-of-tick, so a tick that never re-arms
     * it cannot leak zone updates into a boat. */
    WorldviewRegistry_ResetActive(state->worldviews);
    /* DRIVE_STAMP: server_tick -- a=world cycle. Every await's deadline is
     * counted in these, and every level predicate is re-checked on one. */
    state_stamp(
        state, RS_EVENT_SERVER_TICK, state->events.cycle ? *state->events.cycle : 0, 0, 0, 0);
    return RS_SERVER_STATE_APPLIED;
}

enum RS_ServerStateApply
RS_ServerState_Apply(
    struct RS_ServerState const* state,
    struct RevPacket const* packet)
{
    assert(state);
    assert(packet);
    /* Every record is required, asserted once here and tested by no arm.
     * The two that may be absent are real states, guarded where read: no
     * world yet (app->world), and no npc configs (the headless client). */
    assert(state->varps);
    assert(state->stats);
    assert(state->invs);
    assert(state->if_texts);
    assert(state->if_hides);
    assert(state->if_colours);
    assert(state->if_events);
    assert(state->chat);
    assert(state->chat_clock);
    assert(state->social);
    assert(state->chat_filter_mode);
    assert(state->session_info);
    assert(state->session_end);
    assert(state->server_tick);
    assert(state->player_ops);
    assert(state->hint_arrow);
    assert(state->npc_update_origin);
    assert(state->interaction_mode);
    assert(state->worldviews);
    assert(state->esync);
    assert(state->events.ring);

    if( packet->_control )
        return RS_SERVER_STATE_PASS;

    switch( packet->packet_type )
    {
    /* ---- varps ---- */
    case PKT_NAME_VARP_SMALL:
        return state_varp_small(state, &packet->_varp_small);
    case PKT_NAME_VARP_LARGE:
        return state_varp_large(state, &packet->_varp_large);
    case PKT_NAME_VARP_LONG:
        return state_varp_long(state, &packet->_varp_long);
    case PKT_NAME_VARP_SYNC:
        return state_varp_sync(state);
    case PKT_NAME_VARP_RESET:
        return state_varp_reset(state);

    /* ---- player stats ---- */
    case PKT_NAME_UPDATE_STAT:
        return state_update_stat(state, &packet->_update_stat);
    case PKT_NAME_UPDATE_RUNENERGY:
        return state_update_runenergy(state, &packet->_update_run_energy);
    case PKT_NAME_UPDATE_RUNWEIGHT:
        return state_update_runweight(state, &packet->_update_runweight);

    /* ---- inventories ---- */
    case PKT_NAME_UPDATE_INV_FULL:
        return state_update_inv_full(state, &packet->_update_inv_full);
    case PKT_NAME_UPDATE_INV_PARTIAL:
        return state_update_inv_partial(state, &packet->_update_inv_partial);
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        return state_update_inv_stop_transmit(state, &packet->_update_inv_stop_transmit);

    /* ---- the interface stores ---- */
    case PKT_NAME_IF_SETTEXT:
        return state_if_settext(state, &packet->_if_settext);
    case PKT_NAME_IF_SETEVENTS:
        return state_if_setevents(state, &packet->_if_setevents);
    case PKT_NAME_IF_SETHIDE:
        return state_if_sethide(state, &packet->_if_sethide);
    case PKT_NAME_IF_SETCOLOUR:
        return state_if_setcolour(state, &packet->_if_setcolour);

    /* ---- chat ---- */
    case PKT_NAME_MESSAGE_GAME:
        return state_message_game(state, &packet->_message_game);
    case PKT_NAME_MESSAGE_PRIVATE:
        return state_message_private(state, &packet->_message_private);
    case PKT_NAME_MESSAGE_PRIVATE_ECHO:
        return state_message_private_echo(state, &packet->_message_private_echo);
    case PKT_NAME_MESSAGE_FRIENDCHANNEL:
        return state_message_friendchannel(state, &packet->_message_friendchannel);
    case PKT_NAME_P_COUNTDIALOG:
        return state_p_countdialog(state);

    /* ---- social ---- */
    case PKT_NAME_UPDATE_FRIENDLIST:
        return state_update_friendlist_apply(state, &packet->_update_friendlist);
    case PKT_NAME_UPDATE_IGNORELIST:
        return state_update_ignorelist(state, &packet->_update_ignorelist);
    case PKT_NAME_FRIENDLIST_LOADED:
        return state_friendlist_loaded(state, &packet->_friendlist_loaded);
    case PKT_NAME_CHAT_FILTER_SETTINGS:
        return state_chat_filter_settings(state, &packet->_chat_filter_settings);

    /* ---- session ---- */
    case PKT_NAME_UPDATE_PID:
        return state_update_pid(state, &packet->_update_pid);
    case PKT_NAME_LAST_LOGIN_INFO:
        return state_last_login_info(state, &packet->_last_login_info);
    case PKT_NAME_UPDATE_REBOOT_TIMER:
        return state_update_reboot_timer(state, &packet->_update_reboot_timer);
    case PKT_NAME_SET_MULTIWAY:
        return state_set_multiway(state, &packet->_set_multiway);
    case PKT_NAME_ENABLE_TRACKING:
        return state_enable_tracking(state);
    case PKT_NAME_FINISH_TRACKING:
        return state_finish_tracking(state);
    case PKT_NAME_UNKNOWN_STRING:
        return state_unknown_string(state, &packet->_text);
    case PKT_NAME_SET_WEBSITE_TOKEN:
        return state_set_website_token(state, &packet->_text);
    case PKT_NAME_SITE_SETTINGS:
        return state_site_settings(state, &packet->_text);
    case PKT_NAME_LOGOUT_TRANSFER:
        return state_logout_transfer(state, &packet->_logout_transfer);
    case PKT_NAME_UPDATE_UID192:
        return state_update_uid192(state, &packet->_uid192);
    case PKT_NAME_SET_PLAYER_OP:
        return state_set_player_op(state, &packet->_set_player_op);
    case PKT_NAME_SET_INTERACTION_MODE:
    case PKT_NAME_RESET_INTERACTION_MODE:
        return state_set_interaction_mode(state, packet);
    case PKT_NAME_HIDENPCOPS:
    case PKT_NAME_HIDELOCOPS:
    case PKT_NAME_HIDEOBJOPS:
        return state_hide_ops(state, packet);
    case PKT_NAME_HINT_ARROW:
        return state_hint_arrow(state, &packet->_hint_arrow);
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
        return state_set_npc_update_origin(state, &packet->_set_npc_update_origin);
    case PKT_NAME_SET_ACTIVE_WORLD:
        return state_set_active_world(state, &packet->_set_active_world);
    case PKT_NAME_SERVER_TICK_END:
        return state_server_tick_end(state);

    /* ---- per-player entity setters ---- */
    case PKT_NAME_ANIM_SPECIFIC:
        return state_anim_specific(state, &packet->_anim_specific);
    case PKT_NAME_NPC_ANIM_SPECIFIC:
        return state_npc_anim_specific(state, &packet->_npc_anim_specific);
    case PKT_NAME_NPC_SPOTANIM_SPECIFIC:
        return state_spotanim_specific(state, true, &packet->_entity_spotanim_specific);
    case PKT_NAME_PLAYER_SPOTANIM_SPECIFIC:
        return state_spotanim_specific(state, false, &packet->_entity_spotanim_specific);
    case PKT_NAME_NPC_HEADICON_SPECIFIC:
        return state_npc_headicon_specific(state, &packet->_npc_headicon_specific);

    default:
        return RS_SERVER_STATE_PASS;
    }
}
