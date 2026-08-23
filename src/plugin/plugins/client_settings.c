#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Client Settings -- how the client is DISPLAYED, on any lane.
 *
 * The first row of the plugin roster and, like Feature Flags beside it, one
 * with no switch: "switch the display settings off" is not a state anyone
 * means to be in. @see ToriRS_PluginDef::essential.
 *
 * ## Why a page here at all
 *
 * The rev-239 cache carries an All Settings panel and edits interface scaling
 * in it -- device option 27, the row `~script3967` case 79 writes. A 2004 dat1
 * cache carries no such interface and never will: there is nothing to add a
 * row to. So on rs254lc, rs289lc and rs377lc the scale was unreachable, and on
 * a high-density display that means the whole frame is drawn at 1:1 device
 * pixels -- a 765-wide gameframe in a 3024-wide window, which is a client you
 * have to lean towards to read.
 *
 * This page is the client's own answer to that, and it writes THE SAME store
 * the cache's panel does. Not a second setting shadowing the first: a lane
 * that has both shows one value in two places, and the value survives a switch
 * between lanes because it lives in preferences.ini rather than in this
 * plugin's ini section.
 *
 * ## Why nothing here is a config schema
 *
 * A schema key would be a THIRD copy -- plugin ini, preferences.ini, and the
 * option store -- and the three would disagree the first time the All Settings
 * panel wrote one of them. Every row below is a control reading and writing
 * the client's store live, so there is exactly one place the value is.
 *
 * ## What interface scaling actually does
 *
 * The canvas becomes the window divided by the scale and the present stretches
 * it back, so 200% is half as many pixels each drawn twice the size. The 3D
 * scene is inside that canvas, so it renders at the reduced resolution too --
 * the same trade the reference's mobile client makes, and the reason the
 * filter row is next to it rather than buried: a nearest-neighbour stretch of
 * a scene looks like a different decision from a bicubic one.
 */

/** Steps the scale dropdown offers, as the reference's own row does: `100 +
 *  choice*25`, capped at 400 (script_3054). Spelled out rather than generated
 *  so the labels and the values are one list that cannot drift. */
#define CS_SCALE_CHOICES "100%|125%|150%|175%|200%|225%|250%|275%|300%|325%|350%|375%|400%"
#define CS_SCALE_STEP 25

/** enum_4033, in its own order: the filter the stretch uses. */
#define CS_FILTER_CHOICES "Nearest|Linear|Bicubic"

/** Widget ids. Also the only strings the UI handler matches on. */
#define CS_ID_SCALE "ui_scale"
#define CS_ID_FILTER "ui_scale_filter"

static struct ToriRS_PluginApi const* g_api;

/* ------------------------------------------------------------------------ */

/**
 * Which dropdown row a value lands on, clamped into the list.
 *
 * Clamped rather than asserted: the value comes from the option store, which a
 * hand-edited preferences.ini and the cache's own scripts both write, and a
 * scale of 137 is a perfectly legal thing for either to have left there. It
 * shows as the nearest row it could have been picked from, and picking that
 * row is what makes it one.
 */
static int
cs_row_of(
    int value,
    int base,
    int step,
    int rows)
{
    int row;

    assert(step > 0);
    assert(rows > 0);
    row = (value - base + step / 2) / step;
    if( row < 0 )
        row = 0;
    if( row >= rows )
        row = rows - 1;
    return row;
}

/** How many '|'-separated entries a choice list has. */
static int
cs_choice_count(char const* choices)
{
    int n = 1;

    assert(choices);
    if( !choices[0] )
        return 0;
    for( char const* at = choices; *at; at++ )
        if( *at == '|' )
            n++;
    return n;
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

static enum ToriRS_PluginVerdict
cs_on_ui_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    int value = 0;
    int min = 0;
    int max = 0;

    (void)event;
    (void)userdata;
    assert(ctx);

    if( !g_api->win_request(ctx, "Client Settings") )
        return TORIRS_PLUGIN_PASS;

    /*
     * A row per setting the BUILD has, and none for one it does not.
     *
     * display_setting answering 0 is how a build that dropped a preference
     * costs this page its row rather than giving it a control over nothing --
     * the same rule Feature Flags renders its list by, for the same reason.
     */
    if( g_api->display_setting(ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE, &value, &min, &max) )
    {
        (void)max;
        g_api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, CS_ID_SCALE, "Interface scaling");
        g_api->win_set_options(
            ctx,
            CS_ID_SCALE,
            CS_SCALE_CHOICES,
            cs_row_of(value, min, CS_SCALE_STEP, cs_choice_count(CS_SCALE_CHOICES)));
    }

    if( g_api->display_setting(
            ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER, &value, &min, &max) )
    {
        g_api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, CS_ID_FILTER, "Scaling filter");
        g_api->win_set_options(
            ctx, CS_ID_FILTER, CS_FILTER_CHOICES, cs_row_of(value, min, 1, cs_choice_count(CS_FILTER_CHOICES)));
    }

    g_api->win_widget(
        ctx,
        TORIRS_PLUGIN_W_LABEL,
        "note",
        "Scaling draws the whole canvas larger, the 3D scene included.");

    return TORIRS_PLUGIN_PASS;
}

/*
 * A row was used.
 *
 * Applied on the spot and not staged behind a Save, because this is the one
 * page whose settings you judge by LOOKING at the result: a scale you cannot
 * see until you commit it is a scale you pick by arithmetic.
 *
 * `ev->value` is the row, and the value is derived from it here rather than
 * parsed back out of the label -- "150%" is a string this file wrote for a
 * person to read, and reading it back would make the label the store.
 */
static enum ToriRS_PluginVerdict
cs_on_ui(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    struct ToriRS_PluginEvUi* ev = (struct ToriRS_PluginEvUi*)event;
    int min = 0;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( !ev->widget_id )
        return TORIRS_PLUGIN_PASS;

    if( strcmp(ev->widget_id, CS_ID_SCALE) == 0 )
    {
        if( !g_api->display_setting(ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE, NULL, &min, NULL) )
            return TORIRS_PLUGIN_PASS;
        g_api->display_setting_set(
            ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE, min + ev->value * CS_SCALE_STEP);
        return TORIRS_PLUGIN_PASS;
    }

    if( strcmp(ev->widget_id, CS_ID_FILTER) == 0 )
    {
        if( !g_api->display_setting(
                ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER, NULL, &min, NULL) )
            return TORIRS_PLUGIN_PASS;
        g_api->display_setting_set(
            ctx, TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER, min + ev->value);
        return TORIRS_PLUGIN_PASS;
    }

    return TORIRS_PLUGIN_PASS;
}

static void
cs_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI_BUILD, cs_on_ui_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI, cs_on_ui, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS = {
    .name = "client-settings",
    .title = "Client Settings",
    .version = "1.0",
    /*
     * Below Feature Flags on purpose. Both start before the ordinary plugins,
     * and a feature flag has to be in force before anything that reads one has
     * run; nothing reads a display setting at START at all, so this one has no
     * claim on going first.
     */
    .priority = 999,
    /* No schema: every value here lives in the client's own option store, and
     * a key in this plugin's ini would be a second copy of it. @see the header
     * comment. */
    .config = NULL,
    .essential = true,
    .init = cs_init,
};
