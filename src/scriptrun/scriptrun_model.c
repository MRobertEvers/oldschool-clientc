/*
 * The packet model's records as a headless client holds them
 * (scriptrun_model.h).
 */

#include "scriptrun/scriptrun_model.h"

#include <assert.h>
#include <string.h>

/* InvManager's change callback, bound as the real client binds it
 * (app/app_ui_host.c app_inv_ui_host_change stamps inv_changed per change). */
static void
model_inv_changed(
    void* user,
    int container_id)
{
    struct ScriptrunModel* model = (struct ScriptrunModel*)user;

    RS_EventSink_Stamp(&model->state.events, RS_EVENT_INV_CHANGED, container_id, 0, 0, 0);
}

void
ScriptrunModel_Init(
    struct ScriptrunModel* model,
    int rev_number,
    int const* cycle,
    struct World* world,
    struct RS_EntitySync* esync,
    struct RS_EventSink events)
{
    assert(model);
    assert(cycle);
    assert(world);
    assert(esync);
    assert(events.ring);
    memset(model, 0, sizeof(*model));
    VarPManager_Init(&model->varps);
    RS_PlayerStats_Init(&model->stats);
    InvManager_Init(&model->invs);
    RS_Chat_Init(&model->chat, "");
    RS_Chat_SetEventSink(&model->chat, events);
    RS_Social_Init(&model->social);
    RS_SessionInfo_Reset(&model->session_info);
    RS_InteractionMode_Reset(&model->interaction_mode);
    WorldviewRegistry_Init(&model->worldviews);
    model->state = (struct RS_ServerState){
        .varps = &model->varps,
        .stats = &model->stats,
        .invs = &model->invs,
        .if_texts = &model->if_texts,
        .if_hides = &model->if_hides,
        .if_colours = &model->if_colours,
        .if_events = &model->if_events,
        .chat = &model->chat,
        .chat_clock = cycle,
        .social = &model->social,
        .chat_filter_mode = model->chat_filter_mode,
        .session_info = &model->session_info,
        .session_end = &model->session_end,
        .server_tick = &model->server_tick,
        .player_ops = &model->player_ops,
        .hint_arrow = &model->hint_arrow,
        .npc_update_origin = &model->npc_update_origin,
        .interaction_mode = &model->interaction_mode,
        .rev_number = rev_number,
        .worldviews = &model->worldviews,
        .world = world,
        .esync = esync,
        /* No npc configs: NPC_HEADICON_SPECIFIC's first write is the only
         * reader, and an owner whose World holds npcs names its own source
         * of the type's icon (scriptrun_core.c core_npc_head_icon). */
        .provider = NULL,
        .events = events,
    };
    InvManager_SetChangeCallback(&model->invs, model_inv_changed, model);
}

void
ScriptrunModel_Free(struct ScriptrunModel* model)
{
    if( !model )
        return;
    UIIfTextStore_Free(&model->if_texts);
    UIIfIntStore_Free(&model->if_hides);
    UIIfIntStore_Free(&model->if_colours);
    UIIfEventTable_Free(&model->if_events);
    RS_Chat_Free(&model->chat);
    InvManager_Free(&model->invs);
    VarPManager_Free(&model->varps);
}
