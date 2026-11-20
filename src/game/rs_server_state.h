#ifndef SRC_GAME_RS_SERVER_STATE_H
#define SRC_GAME_RS_SERVER_STATE_H

/*
 * The packet model: what a server packet says, written into the records that
 * hold it, with nothing shown.
 *
 * ONE COPY OF THE RULES. Each server packet goes through three steps: the npc
 * configs it names are fetched (the only wait the model has), the model
 * applies it here, synchronously, and then each client presents what the
 * model wrote -- the real client (game/rs_gameproto_exec.c, RS_GameProto_Exec)
 * pokes the UI tree, raises CS2 transmits, queues assets and redraws; a client
 * that shows nothing stops after this step.
 *
 * The rules this file keeps:
 *   - it never names the client's app struct, and never reads a scene, a UI tree or a CS2
 *     host to decide what to write;
 *   - a record is written at packet time, whether or not it can be shown yet
 *     (a text for a component that is not mounted is stored anyway);
 *   - an event a test awaits on a packet's arrival (varp_changed, inv_packet,
 *     chat_message, server_tick, npc_seq) is stamped here, into the one ring
 *     `events` names, in the order the packets arrive.
 *
 * WHAT IS HERE. The model half of every handler whose model write and view
 * effect were already separate calls: varps, stats, inventories, the
 * interface text/hide/colour/event stores (and the rule that drops a group's
 * entries when it leaves its slot, RS_ServerState_SlotGroupLeaves), chat
 * lines and the count prompt,
 * the friend and ignore lists and chat filters, the session strings and
 * timers, the player ops, the hint arrow, the npc update origin, the active
 * world cursor and interaction modes, the hidden-ops flags, and the four
 * per-player entity setters (ANIM_SPECIFIC, NPC_ANIM_SPECIFIC, the two
 * SPOTANIM_SPECIFICs) plus NPC_HEADICON_SPECIFIC. The zone, loc, ground-item,
 * projectile, interface-mount and world-entity families are not: their record
 * still lives in the scene or the UI tree (see exec_sort.tsv's
 * MIXED-entangled rows), and the clan, friends-chat, Grand Exchange, group
 * and objunlock stores live inside the CS2 host.
 *
 * THE STATE. Pointers to the modules that hold packet-derived state, bound by
 * the owner per packet (the real client: App_ServerStateBind). The model
 * never allocates them and never frees them. Each packet family asserts the
 * pointers it writes; a family whose pointer is not bound is a caller bug.
 */

#include "game/rs_event_ring.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CacheProvider;
struct InvManager;
struct RevPacket;
struct RS_Chat;
struct RS_EntitySync;
struct RS_HintArrow;
struct RS_InteractionMode;
struct RS_NpcUpdateOrigin;
struct RS_PlayerOps;
struct RS_PlayerStats;
struct RS_ServerTick;
struct RS_SessionInfo;
struct RS_Social;
struct SessionEnd;
struct UIIfEventTable;
struct UIIfIntStore;
struct UIIfTextStore;
struct VarPManager;
struct World;
struct WorldviewRegistry;

struct RS_ServerState
{
    /* ---- vars, stats, inventories ---- */
    struct VarPManager* varps;
    struct RS_PlayerStats* stats;
    struct InvManager* invs;

    /* ---- the interface stores: what the server set, by component, kept
     * across mounts (ui/uitree_if_store.h, ui/uitree_if_events.h) ---- */
    struct UIIfTextStore* if_texts;
    struct UIIfIntStore* if_hides;
    struct UIIfIntStore* if_colours;
    struct UIIfEventTable* if_events;

    /* ---- chat and social ---- */
    struct RS_Chat* chat;
    /** The clock a chat line is stamped with (RS_ChatNode.clock): the
     *  owner's client clock, read at the write. */
    int const* chat_clock;
    struct RS_Social* social;
    /** The three chat filter modes (RS_UI_CHAT_FILTER_*), the same ints the
     *  CS2 host's CHAT_GETFILTER_* read. */
    int* chat_filter_mode;

    /* ---- session ---- */
    struct RS_SessionInfo* session_info;
    struct SessionEnd* session_end;
    struct RS_ServerTick* server_tick;
    struct RS_PlayerOps* player_ops;
    struct RS_HintArrow* hint_arrow;
    struct RS_NpcUpdateOrigin* npc_update_origin;
    struct RS_InteractionMode* interaction_mode;
    /** The revision's number: SET_INTERACTION_MODE's root id moved from -1
     *  to 0 at 237. -1 when no revision is bound. */
    int rev_number;

    /* ---- the world: its views, its entities, the npc configs ---- */
    struct WorldviewRegistry* worldviews;
    struct World* world;
    struct RS_EntitySync* esync;
    /** Npc configs, for NPC_HEADICON_SPECIFIC's first write (the type's own
     *  icons). A client that reads its npc records elsewhere (the headless
     *  client: the server's) names `npc_head_icon` instead: true with the
     *  type's first head icon (sprite group and index, -1 for none) when it
     *  knows the type. */
    struct CacheProvider* provider;
    int (*npc_head_icon)(void* ctx, int npc_id, int* out_group, int* out_index);
    void* npc_head_icon_ctx;

    /** The one event ring the packet events are stamped into. */
    struct RS_EventSink events;
};

/** What RS_ServerState_Apply did with a packet. Zero means "not a packet the
 *  model applies"; anything else means it was one. */
enum RS_ServerStateApply
{
    /** Not a model packet: presentation only, or a family whose record still
     *  lives in the view (the executor applies it whole). */
    RS_SERVER_STATE_PASS = 0,
    /** Written; whatever the client presents for it follows. */
    RS_SERVER_STATE_APPLIED,
    /** A model packet that left the record as it was -- a value already held
     *  (run energy, run weight, multiway), a message already seen, a stat or
     *  container the record cannot hold, an entity the record does not have:
     *  there is nothing new to present. */
    RS_SERVER_STATE_UNCHANGED,
};

/**
 * Apply one parsed packet's model half to `state`. Synchronous, never
 * blocks. Returns RS_SERVER_STATE_PASS for a packet that is not the model's.
 */
enum RS_ServerStateApply
RS_ServerState_Apply(
    struct RS_ServerState const* state,
    struct RevPacket const* packet);

/**
 * A group leaves the slot it was mounted in: IF_CLOSESUB (`incoming_group`
 * 0), an IF_OPENSUB onto an occupied slot (`incoming_group` the group it
 * opens, which may be the same one), the destination of an IF_MOVESUB (the
 * group moved there). `group` is the group the slot held, which only the
 * client knows (the real client: its queued mount tasks and its tree; the
 * headless client: its mount table), so the client asks this at the call.
 *
 * The reference's one close (class415.method9520, ending in
 * Statics.method12095) unlinks every IF_SETEVENTS node on the group's
 * components, the same group reopened included. Its unload flag
 * (Statics.method7193) drops the group's IfType.list, and with it the texts,
 * hides and colours the server set on it, only when the incoming group is a
 * different one: a group reopened in its own slot keeps them.
 *
 * Returns 1 when the texts, hides and colours went too, 0 when only the
 * arming did.
 */
int
RS_ServerState_SlotGroupLeaves(
    struct RS_ServerState const* state,
    int group,
    int incoming_group);

/** Container key for an inv packet: the revision's own inventory id when it
 *  carries one (rev 230+), else the component id the older revisions key by.
 *  Zero counts as absent (a zero-initialised packet keeps the legacy keying). */
int
RS_ServerState_InvContainerId(
    int inv_id,
    int component_id);

/** 15-bit RS colour (r<<10|g<<5|b) to RGB888: what IF_SETCOLOUR stores. */
int
RS_ServerState_Rs15ToRgb(int colour);

/** Is a public line (PLAYER_INFO's chat block) from player `sender` (NULL:
 *  the player has no name yet) shown, overhead and chatbox both? Not when
 *  the crown is `ignorable` and the sender is on the ignore list, nor while
 *  the local player is on Tutorial Island (deob class95.method3131). The
 *  real client's answer to RS_EntityApplyHost.public_chat_shown. */
bool
RS_ServerState_PublicChatShown(
    struct RS_ServerState const* state,
    char const* sender,
    bool ignorable);

/** File every friend login/logout notice due at client clock `clock` into
 *  `chat` as a type-5 line (deob class58.method1165). The headless client
 *  calls it once a tick; the real client drains the same queue every client
 *  cycle in RS_CS2Host_Tick, through its chat-transmit path. */
void
RS_ServerState_FriendNotices(
    struct RS_Social* social,
    struct RS_Chat* chat,
    int clock);

/** Escape a player's text for display (class2.method12): `<` and `>` become
 *  `<lt>` and `<gt>`, truncated to fit `cap`. */
void
RS_ServerState_EscapeTags(
    char const* text,
    char* out,
    size_t cap);

/** The pid ANIM_SPECIFIC addresses: the server-named local pid, or the slot
 *  player info files the local player under until it has one (2047). */
int
RS_ServerState_LocalPid(struct RS_EntitySync const* esync);

#endif
