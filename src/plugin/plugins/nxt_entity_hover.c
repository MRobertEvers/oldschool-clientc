#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>

/*
 * All Settings > Activities > General, setting 190:
 *
 *   "Highlight entities on mouse-over -- Adds a highlight to entities when you
 *    hover the mouse cursor over them."
 *
 * One entity, not every candidate under the pointer. api->hover_entity answers
 * with the nearest non-terrain pick of the frame, which is the one the
 * client's own left-click would act on -- so the outline and the click agree,
 * which is the entire use of the feature. Outlining the whole pickset would
 * light up the wall behind the npc and say nothing about which of them is
 * about to be clicked.
 *
 * The row has no colour of its own -- there is no `param_1230` on struct_3738
 * and no colour row beside it -- so the colour is this client's to choose. It
 * is white: the highlight has to read against every model in the game and
 * against grass, sand, stone and water underneath them, and white is the one
 * value that does not disappear into a family of them. It is also what the
 * outline the loc editor uses for its hover is, which keeps one "the pointer
 * is on this" appearance in the client rather than two.
 *
 * MESH rather than BOUNDS. The bounds box is a cylinder in eight corners, and
 * around a long thin thing -- a fence, a ladder, a fishing spot -- it wraps
 * empty air the click will not act on. The mesh hull is linear in the model
 * and is drawn once per frame for exactly one entity, which is the cheapest
 * case the hull builder has.
 */

/** Outline only. A wash over the model under the pointer would hide the model,
 *  and this is a pointer indicator rather than a selection. */
#define NXT_HOVER_FILL_ALPHA 0
#define NXT_HOVER_RGB 0xFFFFFFu

static struct ToriRS_PluginApi const* g_api;

static enum ToriRS_PluginVerdict
nxt_entity_hover_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    struct ToriRS_PluginHoverEntity hit;

    assert(ctx);
    assert(ev);

    if( !g_api->varbit(ctx, NXT_VARBIT_HOVER_ENTITY) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->hover_entity(ctx, &hit) )
        return TORIRS_PLUGIN_PASS;
    if( hit.element_id < 0 )
        return TORIRS_PLUGIN_PASS;

    g_api->draw_hull(
        ctx,
        ev->surface,
        hit.element_id,
        NXT_HOVER_RGB,
        NXT_HOVER_FILL_ALPHA,
        TORIRS_PLUGIN_HULL_MESH);
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_entity_hover_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_entity_hover_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_ENTITY_HOVER = {
    .name = "nxt-entity-hover",
    .title = "Highlight entities on mouse-over (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_entity_hover_init,
    .shutdown = NULL,
};
