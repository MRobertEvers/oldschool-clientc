/*
 * The character-design panel's arrows, answered before the server does.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * The rule itself — which child is which arrow, and where the arrow lands —
 * is game/rs_design_panel.c, which knows nothing about an App and is tested
 * on its own. This file is the wiring: which interface the panel IS on this
 * cache, whose appearance to step, and the asset fetch the step needs.
 *
 * @see game/rs_design_panel.h for why a client-side step is not a second
 * authority over the design.
 */

#include "app/app_internal.h"

#include "game/rs_design_panel.h"
#include "game/task_entity_assets.h"

/* The seven stance sequences PLAYER_INFO carries beside the appearance. A
 * design step changes none of them, but EntityAssets_PlayerBodyResident asks
 * about the whole body, so they are passed through from the entity. */
enum
{
    APP_DESIGN_SEQ_COUNT = 7
};

int
App_DesignPredictIfButton(
    struct App* app,
    int com_id,
    int op_num)
{
    struct RS_DesignArrow arrow;
    struct WorldEntity_Player* lp;
    int iface;

    assert(app);

    /* op1 is the arrows' own op (`op1=*` on every one of them, and nothing
     * else on the panel carries a numbered op). */
    if( op_num != 1 || com_id < 0 )
        return 0;

    iface = RevConfigRefs_Get(&app->revconfig_refs, "iface", "player_design");
    if( iface < 0 || ((com_id >> 16) & 0xffff) != iface )
        return 0;
    if( !RS_DesignPanel_Arrow(com_id & 0xffff, &arrow) )
        return 0;

    lp = app_local_player(app);
    if( !lp )
        return 0; /* the panel without a body to dress: offline, or pre-spawn */

    if( !RS_DesignPanel_Apply(
            app->provider,
            &arrow,
            lp->gender,
            lp->appearance.slots,
            lp->appearance.identkit,
            lp->appearance.colors) )
        return 0;

    /*
     * Everything downstream is keyed on the appearance's CONTENT — the widget
     * figure (app_player_model_poll), the body in the viewport
     * (app_world_reconcile_player_body) and the composite cache alike — so
     * the step is the whole of the local half. Both figures move on the next
     * logic tick, 20 ms away.
     *
     * Except that neither builds from parts that are not resident, and
     * nothing has asked for this kit's models: the fetch is queued by the
     * PLAYER_INFO exec that normally brings an appearance in
     * (task_exec_entity_info.c), which for this step has not happened yet.
     * Queue it here for the same reason it is queued there, and the tick the
     * prediction saves is spent on the IO instead of after it.
     */
    {
        int seqs[APP_DESIGN_SEQ_COUNT] = {
            lp->idle_animations.readyanim,  lp->idle_animations.turnanim,
            lp->idle_animations.walkanim,   lp->idle_animations.walkanim_b,
            lp->idle_animations.walkanim_l, lp->idle_animations.walkanim_r,
            lp->idle_animations.runanim,
        };
        if( !EntityAssets_PlayerBodyResident(
                app, lp->appearance.slots, lp->gender, seqs, APP_DESIGN_SEQ_COUNT) )
            ToriRS_TaskQueue_Add(
                app->runner.queue,
                CreateTask_PlayerBodyLand(
                    app, lp->appearance.slots, lp->gender, seqs, APP_DESIGN_SEQ_COUNT));
    }

    app->need_redraw = 1;
    return 1;
}
