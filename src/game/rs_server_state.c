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

static enum RS_ServerStateApply
state_update_inv_full(
    struct RS_ServerState const* state,
    struct PktUpdateInvFull const* p)
{
    int const container = RS_ServerState_InvContainerId(p->inv_id, p->component_id);

    assert(state->invs);
    if( InvManager_EnsureContainer(state->invs, container, p->size, "server-inv") < 0 )
        return RS_SERVER_STATE_UNCHANGED;
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
    if( InvManager_EnsureContainer(state->invs, container, 0, "server-inv") < 0 )
        return RS_SERVER_STATE_UNCHANGED;
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
    RS_Social_MessageRemember(state->social, key);

    base37tostr((uint64_t)message->channel_name, channel, sizeof(channel));
    RS_ServerState_EscapeTags(message->text ? message->text : "", escaped, sizeof(escaped));
    RS_Chat_CrownedName(message->crown, message->sender, name, sizeof(name));
    state_chat_add(state, RS_CHAT_TYPE_FRIENDS_CHAT, name, channel, escaped);
    return RS_SERVER_STATE_APPLIED;
}

/* ---- social ---------------------------------------------------------------- */

static void
state_update_friendlist(
    struct RS_ServerState const* state,
    struct PktUpdateFriendList const* p)
{
    char name[16];
    struct RS_Social* social = state->social;
    int found = 0;

    assert(social);
    social->server_status = 2;
    if( !p->present )
        return;
    base37tostr((uint64_t)p->name37, name, sizeof(name));
    for( int i = 0; i < social->friend_count; i++ )
    {
        if( strcmp(social->friend_name[i], name) == 0 )
        {
            RS_Social_SetFriendWorld(social, i, p->world);
            social->friend_rank[i] = p->rank;
            snprintf(social->friend_previous_name[i], RS_SOCIAL_NAME_LEN, "%s", p->previous_name);
            found = 1;
            break;
        }
    }
    if( !found && RS_Social_AddFriend(social, name, p->world) )
    {
        int const added = social->friend_count - 1;
        social->friend_rank[added] = p->rank;
        snprintf(social->friend_previous_name[added], RS_SOCIAL_NAME_LEN, "%s", p->previous_name);
    }
    /* The client re-applies the scripts' sort chain on every friend update
     * (class476.java:207), so a world change re-orders the list without the
     * panel asking. */
    RS_Social_SortFriends(social);
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
        TORIRS_LOG("exec: interaction mode for world %d refused\n", world_id);
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

    assert(state->world);
    assert(state->esync);
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

    assert(state->world);
    assert(state->esync);
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

    assert(state->world);
    assert(state->esync);
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

    assert(state->world);
    assert(state->esync);
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

enum RS_ServerStateApply
RS_ServerState_Apply(
    struct RS_ServerState const* state,
    struct RevPacket const* packet)
{
    assert(state);
    assert(packet);

    if( packet->_control )
        return RS_SERVER_STATE_PASS;

    switch( packet->packet_type )
    {
    /* ---- varps ---- */
    case PKT_NAME_VARP_SMALL:
        assert(state->varps);
        if( VarPManager_ApplySmall(
                state->varps, packet->_varp_small.variable, packet->_varp_small.value) )
            state_stamp_varp(state, packet->_varp_small.variable);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_VARP_LARGE:
        assert(state->varps);
        if( VarPManager_ApplyLarge(
                state->varps, packet->_varp_large.variable, packet->_varp_large.value) )
            state_stamp_varp(state, packet->_varp_large.variable);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_VARP_LONG:
        assert(state->varps);
        if( VarPManager_ApplyLong(
                state->varps, packet->_varp_long.variable, packet->_varp_long.value) )
            state_stamp_varp(state, packet->_varp_long.variable);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_VARP_SYNC:
        /* Restore the client-visible copy from the server-authoritative set
         * (rev-230 client.java VARP_SYNC / LostCity Client.ts:7390). Distinct
         * from VARP_RESET, which zeroes both. */
        assert(state->varps);
        VarPManager_ApplySyncEach(state->varps, state_varp_synced, (void*)state);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_VARP_RESET:
        assert(state->varps);
        VarPManager_ResetAll(state->varps);
        /* The reset's one server update is id -1, "every varp". */
        state_stamp_varp(state, -1);
        return RS_SERVER_STATE_APPLIED;

    /* ---- player stats ---- */
    case PKT_NAME_UPDATE_STAT:
        assert(state->stats);
        if( packet->_update_stat.stat < 0 ||
            packet->_update_stat.stat >= RS_PLAYER_STATS_SKILL_COUNT )
            return RS_SERVER_STATE_UNCHANGED;
        RS_PlayerStats_SetXp(state->stats, packet->_update_stat.stat, packet->_update_stat.xp);
        state->stats->current_level[packet->_update_stat.stat] = packet->_update_stat.level;
        RS_PlayerStats_RecomputeCombatLevel(state->stats);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UPDATE_RUNENERGY:
        assert(state->stats);
        state->stats->run_energy_raw = packet->_update_run_energy.run_energy_raw;
        if( state->stats->run_energy == packet->_update_run_energy.run_energy )
            return RS_SERVER_STATE_UNCHANGED;
        state->stats->run_energy = packet->_update_run_energy.run_energy;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UPDATE_RUNWEIGHT:
        assert(state->stats);
        if( state->stats->run_weight == packet->_update_runweight.run_weight )
            return RS_SERVER_STATE_UNCHANGED;
        state->stats->run_weight = packet->_update_runweight.run_weight;
        return RS_SERVER_STATE_APPLIED;

    /* ---- inventories ---- */
    case PKT_NAME_UPDATE_INV_FULL:
        return state_update_inv_full(state, &packet->_update_inv_full);
    case PKT_NAME_UPDATE_INV_PARTIAL:
        return state_update_inv_partial(state, &packet->_update_inv_partial);
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        /* Reference clears the bound inventory's slots (linkObjType[i] = -1).
         * ApplyFull with count 0 empties every slot; a no-op if the container
         * was never transmitted. */
        assert(state->invs);
        InvManager_ApplyFull(
            state->invs,
            RS_ServerState_InvContainerId(
                packet->_update_inv_stop_transmit.inv_id,
                packet->_update_inv_stop_transmit.component_id),
            NULL,
            NULL,
            0);
        return RS_SERVER_STATE_APPLIED;

    /* ---- the interface stores ---- */
    /* Persisting setters, not one-shot applies: each of these routinely lands
     * before the interface it targets has finished mounting (the mount is an
     * async task), and the reference keeps the value on IfType.list where it
     * survives. The store is the record; the tree is the client's. */
    case PKT_NAME_IF_SETTEXT:
        assert(state->if_texts);
        UIIfTextStore_Set(
            state->if_texts, packet->_if_settext.component_id, packet->_if_settext.text);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_IF_SETEVENTS:
        assert(state->if_events);
        UIIfEventTable_Set(
            state->if_events,
            packet->_if_setevents.component_id,
            packet->_if_setevents.from,
            packet->_if_setevents.to,
            packet->_if_setevents.events);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_IF_SETHIDE:
        assert(state->if_hides);
        UIIfIntStore_Set(
            state->if_hides, packet->_if_sethide.component_id, packet->_if_sethide.hide ? 1 : 0);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_IF_SETCOLOUR:
        assert(state->if_colours);
        UIIfIntStore_Set(
            state->if_colours,
            packet->_if_setcolour.component_id,
            RS_ServerState_Rs15ToRgb(packet->_if_setcolour.colour));
        return RS_SERVER_STATE_APPLIED;

    /* ---- chat ---- */
    case PKT_NAME_MESSAGE_GAME:
        /* The TYPE is the server's, not a constant: it picks the filter tab,
         * the colour and the prefix the chatbox script draws the line with. */
        state_chat_add(
            state,
            packet->_message_game.type,
            packet->_message_game.name,
            packet->_message_game.name,
            packet->_message_game.text);
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
                TORIRS_LOG("message_game: %s\n", packet->_message_game.text);
        }
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_MESSAGE_PRIVATE:
    {
        char name[16];

        /* Dedupe by message id (reference messageIds ring). */
        assert(state->social);
        if( RS_Social_MessageSeenOrRemember(state->social, packet->_message_private.message_id) )
            return RS_SERVER_STATE_UNCHANGED;
        base37tostr((uint64_t)packet->_message_private.from, name, sizeof(name));
        state_chat_add(
            state,
            packet->_message_private.staff_mod ? RS_CHAT_TYPE_PRIVATE_FROM_MOD
                                               : RS_CHAT_TYPE_PRIVATE_FROM,
            name,
            name,
            packet->_message_private.text);
        return RS_SERVER_STATE_APPLIED;
    }
    /* deob client.java class259.field3170: the line the sender sees for a
     * private message they sent. */
    case PKT_NAME_MESSAGE_PRIVATE_ECHO:
    {
        char escaped[RS_CHAT_TEXT_LEN];

        RS_ServerState_EscapeTags(
            packet->_message_private_echo.text ? packet->_message_private_echo.text : "",
            escaped,
            sizeof(escaped));
        state_chat_add(
            state,
            RS_CHAT_TYPE_PRIVATE_TO,
            packet->_message_private_echo.recipient,
            packet->_message_private_echo.recipient,
            escaped);
        return RS_SERVER_STATE_APPLIED;
    }
    case PKT_NAME_MESSAGE_FRIENDCHANNEL:
        return state_message_friendchannel(state, &packet->_message_friendchannel);
    case PKT_NAME_P_COUNTDIALOG:
        assert(state->chat);
        state->chat->dialog_input_open = 1;
        state->chat->dialog_input[0] = '\0';
        return RS_SERVER_STATE_APPLIED;

    /* ---- social ---- */
    case PKT_NAME_UPDATE_FRIENDLIST:
        state_update_friendlist(state, &packet->_update_friendlist);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UPDATE_IGNORELIST:
        assert(state->social);
        state->social->ignore_count = 0;
        for( int i = 0; i < packet->_update_ignorelist.count; i++ )
        {
            char name[16];
            base37tostr((uint64_t)packet->_update_ignorelist.names37[i], name, sizeof(name));
            RS_Social_AddIgnore(state->social, name);
        }
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_FRIENDLIST_LOADED:
        /* This is what ends the panel's "Loading friends list" state:
         * RS_Social_FriendCount answers -1 until the status reads 2. */
        assert(state->social);
        state->social->server_status = packet->_friendlist_loaded.status;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_CHAT_FILTER_SETTINGS:
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
        assert(state->chat_filter_mode);
        if( packet->_chat_filter_settings.chat_public_mode >= 0 )
            state->chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC] =
                packet->_chat_filter_settings.chat_public_mode;
        if( packet->_chat_filter_settings.chat_private_mode >= 0 )
            state->chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE] =
                packet->_chat_filter_settings.chat_private_mode;
        if( packet->_chat_filter_settings.chat_trade_mode >= 0 )
            state->chat_filter_mode[RS_UI_CHAT_FILTER_TRADE] =
                packet->_chat_filter_settings.chat_trade_mode;
        return RS_SERVER_STATE_APPLIED;

    /* ---- session ---- */
    case PKT_NAME_UPDATE_PID:
        assert(state->esync);
        state->esync->local_pid = packet->_update_pid.local_player_index;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_LAST_LOGIN_INFO:
        /* The welcome rows are clientCode components RS_ClientCode_Tick
         * refreshes off this struct; opening the screen they sit on is the
         * client's. */
        assert(state->session_info);
        state->session_info->welcome.last_ip = packet->_last_login_info.last_ip;
        state->session_info->welcome.days_since_login = packet->_last_login_info.days_since_login;
        state->session_info->welcome.days_since_recovery =
            packet->_last_login_info.days_since_recovery;
        state->session_info->welcome.unread_messages = packet->_last_login_info.unread_messages;
        state->session_info->welcome.member_warning = packet->_last_login_info.member_warning;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UPDATE_REBOOT_TIMER:
        /* The wire carries SERVER ticks; the countdown runs on the 20ms client
         * cycle, so it is converted once here rather than at every read
         * (reference `rebootTimer = g2() * 30`, where 30 is the same ratio
         * spelled as a literal). */
        assert(state->session_info);
        RS_SessionInfo_SetRebootTimer(
            state->session_info, packet->_update_reboot_timer.ticks, APP_SERVER_TICK_LOGIC_CYCLES);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_MULTIWAY:
        assert(state->session_info);
        return RS_SessionInfo_SetMultiway(state->session_info, packet->_set_multiway.multiway)
                   ? RS_SERVER_STATE_APPLIED
                   : RS_SERVER_STATE_UNCHANGED;
    case PKT_NAME_ENABLE_TRACKING:
        assert(state->session_info);
        state->session_info->tracking_enabled = 1;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_FINISH_TRACKING:
        assert(state->session_info);
        state->session_info->tracking_enabled = 0;
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UNKNOWN_STRING:
        assert(state->session_info);
        snprintf(state->session_info->unknown_string,
                 sizeof(state->session_info->unknown_string),
                 "%s",
                 packet->_text.text);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_WEBSITE_TOKEN:
        assert(state->session_info);
        snprintf(state->session_info->website_token,
                 sizeof(state->session_info->website_token),
                 "%s",
                 packet->_text.text);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SITE_SETTINGS:
        assert(state->session_info);
        snprintf(state->session_info->site_settings,
                 sizeof(state->session_info->site_settings),
                 "%s",
                 packet->_text.text);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_LOGOUT_TRANSFER:
        /* The session moves to another world (deob client.java, the
         * LOGOUT_TRANSFER arm: gameState 45, the socket closed, the world
         * switched; state 45 then logs in with GAMELOGIN and the account's
         * credentials). The move is the link watch's, at the head of the next
         * frame (app_net_link_watch): not here, in the middle of the packet
         * pipeline whose session it ends. */
        TORIRS_LOG("exec: LOGOUT_TRANSFER to world %d (%s)\n", packet->_logout_transfer.world_id,
                   packet->_logout_transfer.host);
        assert(state->session_end);
        SessionEnd_Hop(state->session_end, packet->_logout_transfer.world_id);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_UPDATE_UID192:
        if( state_crc32(packet->_uid192.data, sizeof(packet->_uid192.data)) ==
            (uint32_t)packet->_uid192.crc )
            login_osrs_set_uid192(packet->_uid192.data);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_PLAYER_OP:
        assert(state->player_ops);
        if( packet->_set_player_op.slot < 1 || packet->_set_player_op.slot > 5 )
            return RS_SERVER_STATE_UNCHANGED;
        RS_PlayerOps_Set(
            state->player_ops,
            packet->_set_player_op.slot - 1,
            packet->_set_player_op.text ? packet->_set_player_op.text : "",
            packet->_set_player_op.primary);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_INTERACTION_MODE:
    case PKT_NAME_RESET_INTERACTION_MODE:
        state_interaction_mode(
            state,
            &packet->_interaction_mode,
            packet->packet_type == PKT_NAME_RESET_INTERACTION_MODE);
        return RS_SERVER_STATE_APPLIED;
    /* deob client.java: Statics.field2218 / field3892 / field4203 = the byte as a
     * boolean; the world menu builders read them (rs_minimenu_world.c). */
    case PKT_NAME_HIDENPCOPS:
    case PKT_NAME_HIDELOCOPS:
    case PKT_NAME_HIDEOBJOPS:
    {
        bool const hidden = packet->_hide_ops.hidden != 0;

        assert(state->world);
        if( packet->packet_type == PKT_NAME_HIDENPCOPS )
            state->world->hidden_ops.npc = hidden;
        else if( packet->packet_type == PKT_NAME_HIDELOCOPS )
            state->world->hidden_ops.loc = hidden;
        else
            state->world->hidden_ops.obj = hidden;
        return RS_SERVER_STATE_APPLIED;
    }
    case PKT_NAME_HINT_ARROW:
        /* The type and the tile anchor are folded on the way in
         * (game/rs_hint_arrow.h), so every pass that draws it reads one form. */
        assert(state->hint_arrow);
        RS_HintArrow_Set(
            state->hint_arrow,
            packet->_hint_arrow.type,
            packet->_hint_arrow.id,
            packet->_hint_arrow.z,
            packet->_hint_arrow.height);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
        assert(state->npc_update_origin);
        RS_NpcUpdateOrigin_Set(
            state->npc_update_origin,
            packet->_set_npc_update_origin.x,
            packet->_set_npc_update_origin.z);
        return RS_SERVER_STATE_APPLIED;
    case PKT_NAME_SET_ACTIVE_WORLD:
    {
        /* Points the zone/rebuild applicators at a world view until the tick
         * fence resets it (deob class100 activeWorld). The id and level are
         * WIRE data -- a raw g2 and a raw byte -- so a value outside the
         * registry, a dead view, or an impossible plane is a malformed or
         * lagging server, guarded rather than asserted: an assert-only check is
         * an abort in debug and an OOB registry/heightmap index in the NDEBUG
         * release lane. The deob throws here; our release build has to refuse
         * instead. */
        int const world_id = packet->_set_active_world.world_id;
        int slot;

        assert(state->worldviews);
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
            state->worldviews, slot, packet->_set_active_world.level);
        return RS_SERVER_STATE_APPLIED;
    }
    case PKT_NAME_SERVER_TICK_END:
        /* The fence: every packet of this tick has been applied. */
        assert(state->server_tick);
        assert(state->worldviews);
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
