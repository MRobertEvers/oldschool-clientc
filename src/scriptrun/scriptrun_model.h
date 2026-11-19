#ifndef SRC_SCRIPTRUN_SCRIPTRUN_MODEL_H
#define SRC_SCRIPTRUN_SCRIPTRUN_MODEL_H

/*
 * The records the packet model writes (game/rs_server_state.h), as one
 * headless client holds them: the real client's own modules (VarPManager,
 * RS_PlayerStats, InvManager, the interface text/hide/colour/event stores,
 * RS_Chat, RS_Social, the session records), bound into one RS_ServerState.
 *
 * Two owners hold one each:
 *   - struct ScriptrunCore (scriptrun_core.h): the bot's own. Every packet the
 *     core decodes goes through RS_ServerState_Apply into it, and the runner's
 *     readers (varps, stats, inventories, chat lines, interface texts, hides
 *     and IF_SETEVENTS, the event ring) answer from it. The core's World and
 *     RS_EntitySync are the bot's own; its events go to the core's ring.
 *   - struct ScriptrunShadow (scriptrun_shadow.h): a second one, with a World
 *     of its own, fed the same packets and nothing else, to compare against.
 *
 * Nothing here decides anything: the rules are RS_ServerState_Apply's. What
 * this file owns is the records' lifetime and the one binding every owner
 * makes the same way (InvManager's change callback stamps inv_changed per
 * change, as the real client binds it: app/app_ui_host.c
 * app_inv_ui_host_change).
 */

#include "game/rs_chat.h"
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
#include "net/session_end.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "varp/varp_manager.h"
#include "world/worldview.h"

struct RS_EntitySync;
struct World;

struct ScriptrunModel
{
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
    int chat_filter_mode[RS_UI_CHAT_FILTER_COUNT];
    /** Bound to the records above, the owner's World and entity sync, and the
     *  owner's event ring. Points into this struct: a model is never moved
     *  once initialised. */
    struct RS_ServerState state;
};

/**
 * Initialise `model` in place. `rev_number` is the wire revision's number;
 * `cycle` the owner's world cycle, which chat lines are stamped with; `world`
 * and `esync` the owner's entity records (UPDATE_PID, HIDE*OPS and the entity
 * setters write them); `events` the ring every event the model stamps goes to.
 */
void
ScriptrunModel_Init(
    struct ScriptrunModel* model,
    int rev_number,
    int const* cycle,
    struct World* world,
    struct RS_EntitySync* esync,
    struct RS_EventSink events);

/** Release what the records allocated. The World and entity sync stay the
 *  owner's. */
void
ScriptrunModel_Free(struct ScriptrunModel* model);

#endif
