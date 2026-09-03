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
 * This page is the client's own answer to that. Interface scaling writes THE
 * SAME store the cache's panel does. Not a second setting shadowing the first:
 * a lane that has both shows one value in two places, and the value survives a
 * switch between lanes because it lives in preferences.ini rather than in
 * this plugin's ini section.
 *
 * Gameframe selection belongs here for the same single-store reason. The host
 * owns the catalogue, resolver and `preferred_frame` preference; this page
 * only presents those facts and hands a canonical id back when a person picks
 * a row. Provider plugins therefore never acquire the screen by being toggled
 * or by happening to start first.
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
#define CS_ID_FRAME "gameframe"
#define CS_ID_FRAME_DETAIL "gameframe_detail"
#define CS_ID_SCALE "ui_scale"
#define CS_ID_FILTER "ui_scale_filter"

/*
 * win_set_options is the legacy, pipe-separated presentation API. Its host
 * record has room for 192 bytes, while the frame catalogue currently has a
 * hard ceiling of 32 plugin offers. Keep those implementation facts local to
 * this adapter: the ids below are NOT preferences, and a row number is never
 * sent to frame_select.
 */
#define CS_FRAME_ROWS_MAX 33
#define CS_FRAME_CHOICES_MAX 192

struct CsFrameRow
{
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
};

static struct ToriRS_PluginApi const* g_api;
static struct CsFrameRow g_frame_rows[CS_FRAME_ROWS_MAX];
static int g_frame_row_count;
static uint32_t g_frame_seen_revision;
static char g_frame_seen_requested[TORIRS_PLUGIN_FRAME_ID_MAX];
static int g_frame_page_built;

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
/* The host-owned gameframe setting                                          */
/* ------------------------------------------------------------------------ */

/**
 * Append one human label to the old pipe-separated dropdown format.
 *
 * A frame title is presentation data supplied by another plugin. `|` cannot
 * be allowed to manufacture an extra row whose index has no matching stable
 * id, and line breaks do not belong in a one-line native control, so both are
 * made harmless here. Refusing a label which does not fit keeps the visible
 * rows and g_frame_rows exactly aligned.
 */
static int
cs_frame_choice_append(
    char* choices,
    size_t choices_size,
    char const* title)
{
    char safe[TORIRS_PLUGIN_TITLE_MAX];
    size_t safe_n = 0;
    size_t used;

    assert(choices);
    assert(choices_size > 0);
    if( !title || !title[0] )
        title = "Unnamed gameframe";

    while( title[safe_n] && safe_n + 1 < sizeof(safe) )
    {
        char const c = title[safe_n];
        safe[safe_n] = c == '|' ? '/' : (c == '\r' || c == '\n') ? ' ' : c;
        safe_n++;
    }
    safe[safe_n] = '\0';

    used = strlen(choices);
    if( used + (used ? 1u : 0u) + safe_n >= choices_size )
        return 0;
    if( used )
        choices[used++] = '|';
    memcpy(choices + used, safe, safe_n + 1);
    return 1;
}

/** A stable id's readable spelling, using the current dropdown catalogue. */
static char const*
cs_frame_title(char const* id)
{
    if( !id || !id[0] )
        return "Unknown gameframe";
    if( strcmp(id, "auto") == 0 )
        return "Auto";
    if( strcmp(id, "core/native") == 0 )
        return "Native gameframe";
    for( int i = 0; i < g_frame_row_count; i++ )
        if( strcmp(g_frame_rows[i].id, id) == 0 )
            return g_frame_rows[i].title;
    /* A removed provider has no title left to show. Its canonical id is still
     * more useful than calling it "unknown": it tells the user what is
     * missing, and the host deliberately keeps that preference intact. */
    return id;
}

/**
 * Snapshot the catalogue into the rows the legacy dropdown can actually show.
 * Row zero is reserved for Auto; every other row keeps the canonical id beside
 * its title so a translated/renamed/duplicated title can never become state.
 */
static int
cs_frame_choices(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginFrameSelection const* selection,
    char* choices,
    size_t choices_size)
{
    struct ToriRS_PluginFrameInfo info;
    int iter = -1;
    int selected = 0;
    int omitted = 0;

    assert(ctx);
    assert(selection);
    assert(choices);
    assert(choices_size > 0);

    memset(g_frame_rows, 0, sizeof(g_frame_rows));
    g_frame_row_count = 0;
    choices[0] = '\0';

    if( !cs_frame_choice_append(choices, choices_size, "Auto") )
        return 0;
    snprintf(g_frame_rows[0].id, sizeof(g_frame_rows[0].id), "%s", "auto");
    snprintf(g_frame_rows[0].title, sizeof(g_frame_rows[0].title), "%s", "Auto");
    g_frame_row_count = 1;

    while( (iter = g_api->frame_offer_next(ctx, iter, &info)) >= 0 )
    {
        int const row = g_frame_row_count;

        /* Native is represented by Auto. The public iterator intentionally
         * omits it, but filtering here keeps this adapter sound if an older or
         * test host includes the core row. */
        if( !info.id[0] || strcmp(info.id, "core/native") == 0 )
            continue;
        if( row >= CS_FRAME_ROWS_MAX ||
            !cs_frame_choice_append(choices, choices_size, info.title) )
        {
            omitted++;
            continue;
        }

        snprintf(g_frame_rows[row].id, sizeof(g_frame_rows[row].id), "%s", info.id);
        snprintf(
            g_frame_rows[row].title,
            sizeof(g_frame_rows[row].title),
            "%s",
            info.title[0] ? info.title : info.id);
        g_frame_row_count++;
        if( strcmp(selection->requested, info.id) == 0 )
            selected = row;
    }

    /* Keep a saved choice visible even when its provider is temporarily
     * absent. Falling back to row zero made the control say "Auto" while the
     * retained preference still named another frame, so the dropdown and the
     * explanatory line contradicted each other. The synthetic row preserves
     * the canonical value; choosing Auto remains an explicit user action. */
    if( selected == 0 && strcmp(selection->requested, "auto") != 0 &&
        g_frame_row_count < CS_FRAME_ROWS_MAX )
    {
        char unavailable[TORIRS_PLUGIN_TITLE_MAX];
        int const row = g_frame_row_count;

        snprintf(unavailable, sizeof(unavailable), "Unavailable: %s", selection->requested);
        if( cs_frame_choice_append(choices, choices_size, unavailable) )
        {
            snprintf(
                g_frame_rows[row].id,
                sizeof(g_frame_rows[row].id),
                "%s",
                selection->requested);
            snprintf(
                g_frame_rows[row].title,
                sizeof(g_frame_rows[row].title),
                "%s",
                selection->requested);
            g_frame_row_count++;
            selected = row;
        }
    }

    if( omitted )
        g_api->log(
            ctx,
            "client-settings: %d gameframe choice%s did not fit the legacy dropdown",
            omitted,
            omitted == 1 ? "" : "s");
    return selected;
}

/** Explain the resolver state without making the user decode canonical ids. */
static void
cs_frame_detail(
    struct ToriRS_PluginFrameSelection const* selection,
    char* detail,
    size_t detail_size)
{
    char const* const requested = cs_frame_title(selection->requested);
    char const* const active = cs_frame_title(selection->active);
    char const* const reason = selection->reason;

    assert(selection);
    assert(detail);
    assert(detail_size > 0);

    if( selection->status == TORIRS_PLUGIN_FRAME_NATIVE &&
        strcmp(selection->requested, "auto") == 0 )
        snprintf(detail, detail_size, "Active: %s. Auto follows this lane.", active);
    else if( selection->status == TORIRS_PLUGIN_FRAME_ACTIVE &&
             strcmp(selection->requested, selection->active) == 0 )
        snprintf(detail, detail_size, "Active: %s.", active);
    else if( selection->status == TORIRS_PLUGIN_FRAME_LOADING )
        snprintf(
            detail,
            detail_size,
            "Loading %s. Active for now: %s.%s%s",
            requested,
            active,
            reason[0] ? " " : "",
            reason);
    else if( selection->status == TORIRS_PLUGIN_FRAME_FALLBACK )
        snprintf(
            detail,
            detail_size,
            "Could not use %s. Active fallback: %s.%s%s",
            requested,
            active,
            reason[0] ? " " : "",
            reason);
    else
        snprintf(
            detail,
            detail_size,
            "Switching to %s. Active for now: %s.",
            requested,
            active);
}

static void
cs_frame_remember(struct ToriRS_PluginFrameSelection const* selection)
{
    assert(selection);
    g_frame_seen_revision = selection->revision;
    snprintf(
        g_frame_seen_requested,
        sizeof(g_frame_seen_requested),
        "%s",
        selection->requested);
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

static enum ToriRS_PluginVerdict
cs_on_ui_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    struct ToriRS_PluginFrameSelection frame;
    char frame_choices[CS_FRAME_CHOICES_MAX];
    char frame_detail[CS_FRAME_CHOICES_MAX];
    int value = 0;
    int min = 0;
    int max = 0;

    (void)event;
    (void)userdata;
    assert(ctx);

    if( !g_api->win_request(ctx, "Client Settings") )
    {
        g_frame_page_built = 0;
        return TORIRS_PLUGIN_PASS;
    }
    g_frame_page_built = 1;

    g_api->frame_selection(ctx, &frame);
    {
        int const selected =
            cs_frame_choices(ctx, &frame, frame_choices, sizeof(frame_choices));
        g_api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, CS_ID_FRAME, "Gameframe");
        g_api->win_set_options(ctx, CS_ID_FRAME, frame_choices, selected);
    }
    cs_frame_detail(&frame, frame_detail, sizeof(frame_detail));
    g_api->win_widget(ctx, TORIRS_PLUGIN_W_LABEL, CS_ID_FRAME_DETAIL, NULL);
    g_api->win_set_text(ctx, CS_ID_FRAME_DETAIL, frame_detail);
    cs_frame_remember(&frame);

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

/** Rebuild only after this tab has existed; do not create it eagerly at boot. */
static void
cs_rebuild(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( !g_frame_page_built )
        return;
    g_api->win_clear(ctx);
    g_frame_page_built = 0;
    (void)cs_on_ui_build(ctx, NULL, NULL);
}

/** Keep loading/fallback detail live while the settings page remains open. */
static enum ToriRS_PluginVerdict
cs_on_frame_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    struct ToriRS_PluginFrameSelection selection;

    (void)event;
    (void)userdata;
    assert(ctx);

    g_api->frame_selection(ctx, &selection);
    if( selection.revision != g_frame_seen_revision ||
        strcmp(selection.requested, g_frame_seen_requested) != 0 )
    {
        cs_frame_remember(&selection);
        cs_rebuild(ctx);
    }
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

    if( strcmp(ev->widget_id, CS_ID_FRAME) == 0 )
    {
        /* The visible text is presentation only. The row is resolved through
         * the snapshot built beside the dropdown, and the canonical id is the
         * only value that reaches the host or preferences.ini. */
        if( ev->action != TORIRS_PLUGIN_UI_PICK ||
            ev->value < 0 || ev->value >= g_frame_row_count )
        {
            g_api->log(ctx, "client-settings: ignored invalid gameframe row %d", ev->value);
            cs_rebuild(ctx);
            return TORIRS_PLUGIN_PASS;
        }
        if( !g_api->frame_select(ctx, g_frame_rows[ev->value].id) )
            g_api->log(
                ctx,
                "client-settings: could not save gameframe '%s'",
                g_frame_rows[ev->value].id);
        /* The host updates the requested id immediately and resolves the
         * active provider at the next frame boundary. Rebuilding now gives a
         * failed write its old row back and makes a successful request visible
         * without ever persisting this row number. */
        cs_rebuild(ctx);
        return TORIRS_PLUGIN_PASS;
    }

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
    memset(g_frame_rows, 0, sizeof(g_frame_rows));
    g_frame_row_count = 0;
    g_frame_seen_revision = 0;
    g_frame_seen_requested[0] = '\0';
    g_frame_page_built = 0;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI_BUILD, cs_on_ui_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI, cs_on_ui, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_FRAME_START, cs_on_frame_start, NULL);
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
