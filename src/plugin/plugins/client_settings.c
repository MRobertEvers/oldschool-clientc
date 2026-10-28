/*
 * The client's own display knobs, as a Porcelain page: renderer, interface
 * scaling and its filter, stretch, the pixel limit, the frame filter and high
 * DPI. Every row is a live control over the application's own stores.
 *
 * A pick the client refuses restates its row through Porcelain_Restate: the
 * host commits a PICK to its widget model BEFORE dispatching it, so without
 * the restatement the control keeps a value nobody could honour.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client-owned display settings. These are live controls over the
 * application's authoritative stores, never a second plugin config. */

#define CS_ID_RENDERER "renderer"
#define CS_ID_RENDERER_NOW "renderer_now"
#define CS_ID_SCALE "ui_scale"
#define CS_ID_FILTER "ui_scale_filter"
#define CS_ID_INTEGER_SCALING "integer_scaling"
#define CS_ID_STRETCH "stretch_mode"
#define CS_ID_PIXEL_LIMIT "max_pixel_height"
#define CS_ID_FRAME_FILTER "frame_filter"
#define CS_ID_HIGH_DPI "high_dpi"
#define CS_ID_HIGH_DPI_NOW "high_dpi_now"
#define CS_ID_SCALING_NOW "scaling_now"
#define CS_ID_SCALING_HOW "scaling_how"
#define CS_SCALE_ROWS 13
#define CS_FILTER_ROWS 3
#define CS_STRETCH_ROWS 3
/* Device option 30's integer value: offered as the Integer scaling toggle. */
#define CS_STRETCH_INTEGER 1
/* The listed resolutions, plus one slot for a value only preferences.ini holds. */
#define CS_PIXEL_LIMIT_LISTED 19
#define CS_PIXEL_LIMIT_ROWS (CS_PIXEL_LIMIT_LISTED + 1)
#define CS_HIGH_DPI_ROWS 3
#define CS_FRAME_FILTER_ROWS 4

struct ClientSettingsState
{
    /* What the client is doing with the scaling settings, as last described.
     * on_frame_start rebuilds it and re-describes only when it moved -- a
     * window drag changes it without any setting changing. */
    char scaling_now[PORCELAIN_ROW_TEXT_MAX];
    /* Every display value the page shows, as last described. A value written
     * by someone else -- the cache's own settings panel, a preferences load --
     * moves no Porcelain input, so it is compared here instead. */
    char display_seen[PORCELAIN_ROW_TEXT_MAX];
    /* A pixel limit the listed options do not have, labelled for its row. */
    char pixel_limit_value[32];
    char pixel_limit_label[48];
    /* "Automatic" names what it resolved to, so its label is built per run. */
    char high_dpi_auto_label[64];
    /* The detected density, as last described. @see cs_high_dpi_now. */
    char high_dpi_now[PORCELAIN_ROW_TEXT_MAX];
    /* A renderer pick the client has not honoured, as last described. */
    char renderer_now[PORCELAIN_ROW_TEXT_MAX];
    /* The describe function is handed only its `user`, so the api travels
     * with the state rather than through a file-scope pointer. */
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
};

static char const* const CS_SCALE_VALUE[] = {
    "100", "125", "150", "175", "200", "225", "250",
    "275", "300", "325", "350", "375", "400",
};

static char const* const CS_SCALE_LABEL[] = {
    "100%", "125%", "150%", "175%", "200%", "225%", "250%",
    "275%", "300%", "325%", "350%", "375%", "400%",
};

static char const* const CS_FILTER_VALUE[] = { "0", "1", "2" };
static char const* const CS_FILTER_LABEL[] = { "Nearest", "Linear", "Bicubic" };

/* Integer is not here: it changes the buffer as well as its placement, so it
 * is the Integer scaling toggle beside interface scaling. */
static char const* const CS_STRETCH_VALUE[] = { "0", "2", "3" };
static char const* const CS_STRETCH_LABEL[] = {
    "Keep aspect ratio",
    "Stretch to fill",
    "Don't stretch",
};

/* Common resolutions, smallest to largest by pixel count. The value is the
 * pair the two store options take; "0" clears both. A limit is a box the
 * render buffer fits inside keeping its own shape, so a 4:3 entry on a 16:9
 * window caps by width and a 16:9 one on an ultrawide caps by height. */
static char const* const CS_PIXEL_LIMIT_VALUE[CS_PIXEL_LIMIT_LISTED] = {
    "0",         "640x360",   "854x480",   "800x600",   "1024x768",
    "1280x720",  "1280x800",  "1366x768",  "1440x900",  "1600x900",
    "1680x1050", "1920x1080", "1920x1200", "2560x1440", "2560x1600",
    "3200x1800", "3840x2160", "5120x2880", "7680x4320",
};
static char const* const CS_PIXEL_LIMIT_LABEL[CS_PIXEL_LIMIT_LISTED] = {
    "Match window",
    "640x360 (360p)",
    "854x480 (480p)",
    "800x600 (SVGA)",
    "1024x768 (XGA)",
    "1280x720 (720p)",
    "1280x800 (WXGA)",
    "1366x768",
    "1440x900",
    "1600x900 (900p)",
    "1680x1050",
    "1920x1080 (1080p)",
    "1920x1200 (WUXGA)",
    "2560x1440 (1440p)",
    "2560x1600 (WQXGA)",
    "3200x1800",
    "3840x2160 (4K)",
    "5120x2880 (5K)",
    "7680x4320 (8K)",
};

/* Device option 34's values. Row 0's label is built at describe time. */
static char const* const CS_HIGH_DPI_VALUE[CS_HIGH_DPI_ROWS] = { "0", "1", "2" };
static char const* const CS_HIGH_DPI_LABEL[CS_HIGH_DPI_ROWS] = {
    "Automatic",
    "Device pixels",
    "Window points",
};
/* The resolved modes, by option value (1..2), as the readout names them. */
static char const* const CS_HIGH_DPI_NAME[CS_HIGH_DPI_ROWS] = {
    "automatic", "device pixels", "window points",
};

/* By TORIRS_RENDERER_*. A row's stable value is the store's: the kind + 1. */
/* kind + 1, one per kind: a short table left the WebGL and GLES3 kinds' values
 * NULL, so the browser's and the phone's renderer rows had nothing to pick. */
static char const* const CS_RENDERER_VALUE[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
};
_Static_assert(
    sizeof(CS_RENDERER_VALUE) / sizeof(CS_RENDERER_VALUE[0]) == TORIRS_RENDERER_COUNT,
    "one renderer value per kind");
static char const* const CS_RENDERER_LABEL[TORIRS_RENDERER_COUNT] = TORIRS_RENDERER_LABELS;

static char const* const CS_FRAME_FILTER_VALUE[] = { "0", "1", "2", "3" };
static char const* const CS_FRAME_FILTER_LABEL[] = {
    "Same as interface filter", "Nearest", "Linear", "Bicubic",
};

/* Named by cs_on_start, which opens the layer against this plugin's own
 * definition; the definition itself is at the foot of the file. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS;

static int
cs_nearest_row(int value, int base, int step, int count)
{
    int row;
    assert(step > 0);
    assert(count > 0);
    row = (value - base + step / 2) / step;
    if( row < 0 ) row = 0;
    if( row >= count ) row = count - 1;
    return row;
}

static void
cs_static_options(
    struct ToriRS_SelectOption* out,
    char const* const* values,
    char const* const* labels,
    int count)
{
    for( int i = 0; i < count; i++ )
    {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].struct_size = sizeof(out[i]);
        out[i].value = values[i];
        out[i].label = labels[i];
        out[i].enabled = true;
    }
}

static void
cs_pick_scale(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    int min = 0;
    (void)user;
    assert(api);
    assert(api->client);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    /* The read is the gate: a build with no ui-scale store has no row to pick
     * and no value to write. */
    if( api->client->display_get(api, TORIRS_DISPLAY_UI_SCALE, NULL, &min, NULL) )
        (void)api->client->display_set(api, TORIRS_DISPLAY_UI_SCALE, atoi(action->text));
}

static void
cs_pick_filter(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    int min = 0;
    (void)user;
    assert(api);
    assert(api->client);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    /* Offset from the store's own minimum, so the plugin never assumes the
     * enum's base. */
    if( api->client->display_get(api, TORIRS_DISPLAY_UI_SCALE_FILTER, NULL, &min, NULL) )
        (void)api->client->display_set(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, min + atoi(action->text));
}

/* A client-scaling select, written straight through: each row's stable value
 * IS the store's value, and the store clamps. */
static void
cs_pick_display(struct ToriRS_Api* api, int setting, struct PorcelainRowAction const* action)
{
    assert(api);
    assert(api->client);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    if( api->client->display_get(api, setting, NULL, NULL, NULL) )
        (void)api->client->display_set(api, setting, atoi(action->text));
}

/* Integer scaling on is stretch mode integer; off is keep aspect ratio, the
 * placement integer itself falls back to. */
static void
cs_toggle_integer_scaling(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    (void)user;
    assert(api);
    assert(api->client);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_TOGGLE )
        return;
    if( api->client->display_get(api, TORIRS_DISPLAY_STRETCH_MODE, NULL, NULL, NULL) )
        (void)api->client->display_set(
            api, TORIRS_DISPLAY_STRETCH_MODE, action->value ? CS_STRETCH_INTEGER : 0);
}

static void
cs_pick_stretch(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    (void)user;
    cs_pick_display(api, TORIRS_DISPLAY_STRETCH_MODE, action);
}

/* "WxH" into its two halves; "0" is both 0. False for anything else. */
static bool
cs_parse_resolution(char const* text, int* out_w, int* out_h)
{
    char* end = NULL;
    long w;
    long h;

    assert(text);
    assert(out_w);
    assert(out_h);
    if( strcmp(text, "0") == 0 )
    {
        *out_w = 0;
        *out_h = 0;
        return true;
    }
    w = strtol(text, &end, 10);
    if( end == text || *end != 'x' || w <= 0 || w > INT32_MAX )
        return false;
    text = end + 1;
    h = strtol(text, &end, 10);
    if( end == text || *end != '\0' || h <= 0 || h > INT32_MAX )
        return false;
    *out_w = (int)w;
    *out_h = (int)h;
    return true;
}

static void
cs_pick_pixel_limit(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    int w = 0;
    int h = 0;

    (void)user;
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    /* Every value this row offers is one it wrote itself. */
    {
        bool const parsed = cs_parse_resolution(action->text, &w, &h);
        assert(parsed);
        (void)parsed;
    }
    if( !api->client->display_get(api, TORIRS_DISPLAY_MAX_PIXEL_WIDTH, NULL, NULL, NULL) ||
        !api->client->display_get(api, TORIRS_DISPLAY_MAX_PIXEL_HEIGHT, NULL, NULL, NULL) )
        return;
    /* Both in the same frame: the client applies the pair at its next sync. */
    (void)api->client->display_set(api, TORIRS_DISPLAY_MAX_PIXEL_WIDTH, w);
    (void)api->client->display_set(api, TORIRS_DISPLAY_MAX_PIXEL_HEIGHT, h);
}

static void
cs_pick_high_dpi(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    (void)user;
    cs_pick_display(api, TORIRS_DISPLAY_HIGH_DPI, action);
}

static void
cs_pick_frame_filter(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    (void)user;
    cs_pick_display(api, TORIRS_DISPLAY_FRAME_FILTER, action);
}

static bool
cs_display_value(struct ToriRS_Api* api, int setting, int* out)
{
    return api->client->display_get(api, setting, out, NULL, NULL);
}

/*
 * One sentence saying what the settings add up to right now: the layout and
 * the percent it really uses, the pixels rendered, the pixels shown, and why
 * the percent is not the one picked. This is how the interactions are made
 * visible rather than described -- pick Integer at 150% and the line says
 * 100%, and why.
 *
 * Empty when the client has not presented a frame yet.
 */
static void
cs_scaling_now(struct ToriRS_Api* api, char* out, size_t out_size)
{
    int layout_w = 0, layout_h = 0, render_w = 0, render_h = 0, output_w = 0, output_h = 0;
    int effective = 0, chosen = 0, adjusted = 0;
    int used;

    assert(api);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !cs_display_value(api, TORIRS_DISPLAY_EFFECTIVE_UI_SCALE, &effective) ||
        !cs_display_value(api, TORIRS_DISPLAY_UI_SCALE, &chosen) ||
        !cs_display_value(api, TORIRS_DISPLAY_LAYOUT_WIDTH, &layout_w) ||
        !cs_display_value(api, TORIRS_DISPLAY_LAYOUT_HEIGHT, &layout_h) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDER_WIDTH, &render_w) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDER_HEIGHT, &render_h) ||
        !cs_display_value(api, TORIRS_DISPLAY_OUTPUT_WIDTH, &output_w) ||
        !cs_display_value(api, TORIRS_DISPLAY_OUTPUT_HEIGHT, &output_h) ||
        !cs_display_value(api, TORIRS_DISPLAY_SCALE_ADJUSTED, &adjusted) )
        return;

    /* Two sizes now: the world is drawn at the render size, and the interface
     * is laid out at the layout size and scaled into it. */
    used = snprintf(out, out_size, "Now: world %dx%d, interface %dx%d at %d%%, shown %dx%d.",
        render_w, render_h, layout_w, layout_h, effective, output_w, output_h);
    if( used < 0 || (size_t)used >= out_size )
        return;
    if( (adjusted & TORIRS_DISPLAY_ADJUSTED_LOWERED_TO_FIT) &&
        (adjusted & TORIRS_DISPLAY_ADJUSTED_LIMIT_RAISED) )
        used += snprintf(out + used, out_size - (size_t)used,
            " Lowered from %d%%: the render resolution cannot fit the frame.", chosen);
    else if( adjusted & TORIRS_DISPLAY_ADJUSTED_LOWERED_TO_FIT )
        used += snprintf(out + used, out_size - (size_t)used,
            " Lowered from %d%%: the screen cannot fit the frame.", chosen);
    else if( adjusted & TORIRS_DISPLAY_ADJUSTED_LIMIT_RAISED )
        used += snprintf(out + used, out_size - (size_t)used,
            " %d%% of the render resolution, not of the window.", chosen);
    else if( adjusted & TORIRS_DISPLAY_ADJUSTED_INTEGER_ROUNDED )
        used += snprintf(out + used, out_size - (size_t)used,
            " %d%% rounded down for integer scaling.", chosen);
    if( used < 0 || (size_t)used >= out_size )
        return;
    if( adjusted & TORIRS_DISPLAY_ADJUSTED_INTEGER_FELL_BACK )
        (void)snprintf(out + used, out_size - (size_t)used,
            " Window too small for integer scaling: keeping aspect.");
}

/*
 * The display the client detected and what HighDPI makes of it. Its own row
 * rather than a clause of the readout above, which is already near the row
 * text ceiling once it has a reason to give. Empty before a frame.
 */
static void
cs_high_dpi_now(struct ToriRS_Api* api, char* out, size_t out_size)
{
    int density = 0;
    int in_force = 0;

    assert(api);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !cs_display_value(api, TORIRS_DISPLAY_DENSITY, &density) ||
        !cs_display_value(api, TORIRS_DISPLAY_HIGH_DPI_IN_FORCE, &in_force) )
        return;
    assert(in_force >= 1);
    assert(in_force < CS_HIGH_DPI_ROWS);
    (void)snprintf(out, out_size, "Display density %d.%02dx detected; using %s.", density / 100,
        density % 100, CS_HIGH_DPI_NAME[in_force]);
}

static void
cs_display_signature(struct ToriRS_Api* api, char* out, size_t out_size)
{
    size_t used = 0;

    assert(api);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    for( int setting = 0; setting <= TORIRS_DISPLAY_EFFECTIVE_UI_SCALE; setting++ )
    {
        /* The window mode stands in the readout's slot: it decides whether
         * Stretch mode is shown, and it moves without any setting moving. */
        int const read = setting == TORIRS_DISPLAY_EFFECTIVE_UI_SCALE ? TORIRS_DISPLAY_WINDOW_FIXED
                                                                      : setting;
        int value = -1;
        int wrote;
        if( !cs_display_value(api, read, &value) )
            value = -1;
        wrote = snprintf(out + used, out_size - used, "%d,", value);
        if( wrote < 0 || (size_t)wrote >= out_size - used )
            return;
        used += (size_t)wrote;
    }
    /* The renderer moves a frame after its pick, and on its own when a start
     * fails, so it is compared rather than assumed. */
    for( int setting = TORIRS_DISPLAY_RENDERER; setting <= TORIRS_DISPLAY_RENDERER_REFUSED; setting++ )
    {
        int value = -1;
        int wrote;
        if( !cs_display_value(api, setting, &value) )
            value = -1;
        wrote = snprintf(out + used, out_size - used, "%d,", value);
        if( wrote < 0 || (size_t)wrote >= out_size - used )
            return;
        used += (size_t)wrote;
    }
}

/* A select row whose stable values are the store's values. */
static void
cs_select_row(
    struct ToriRS_PorcelainDescribe* describe,
    struct ClientSettingsState* state,
    char const* key,
    char const* label,
    char const* selected,
    struct ToriRS_SelectOption const* options,
    int option_count,
    PorcelainRowActionFn on_action)
{
    struct PorcelainRow row;

    memset(&row, 0, sizeof(row));
    row.key = key;
    row.kind = PORCELAIN_ROW_SELECT;
    row.label = label;
    row.text = selected;
    row.options = options;
    row.option_count = option_count;
    row.on_action = on_action;
    row.user = state;
    Porcelain_Row(describe, &row);
}

/*
 * A renderer pick. The client swaps renderers between two frames and keeps
 * the game running, so there is nothing to wait for here: the row follows the
 * store, and the status line below it follows the renderer that came up.
 */
static void
cs_pick_renderer(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct ClientSettingsState* state = user;

    assert(api);
    assert(api->client);
    assert(state);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    /* The host refuses a renderer this lane cannot start; the control then
     * shows what was asked for until it is told otherwise. */
    if( api->client->display_set(api, TORIRS_DISPLAY_RENDERER, atoi(action->text)) != TORIRS_RESULT_OK )
    {
        api->core.log(api, "client-settings: renderer '%s' is not available", action->text);
        Porcelain_Restate(state->porcelain, CS_ID_RENDERER);
        return;
    }
    Porcelain_Note(state->porcelain, PORCELAIN_INPUT_EXPLICIT);
}

/*
 * What stands between the pick and the renderer drawing, in a sentence. Empty
 * when the renderer drawing is the one picked.
 */
static void
cs_renderer_now(struct ToriRS_Api* api, char* out, size_t out_size)
{
    int request = 0;
    int active = 0;
    int refused = 0;

    assert(api);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !cs_display_value(api, TORIRS_DISPLAY_RENDERER, &request) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDERER_ACTIVE, &active) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDERER_REFUSED, &refused) )
        return;
    assert(active >= 0);
    assert(active < TORIRS_RENDERER_COUNT);
    if( refused > 0 && refused == request )
    {
        assert(refused <= TORIRS_RENDERER_COUNT);
        (void)snprintf(out, out_size, "Could not start %s. Using %s.",
            CS_RENDERER_LABEL[refused - 1], CS_RENDERER_LABEL[active]);
    }
    else if( request > 0 && request - 1 != active )
    {
        assert(request <= TORIRS_RENDERER_COUNT);
        (void)snprintf(out, out_size, "Switching to %s.", CS_RENDERER_LABEL[request - 1]);
    }
}

/*
 * The Renderer row: every renderer this lane can start, and nothing when that
 * is only the one already drawing -- a choice of one is not a setting.
 */
static void
cs_renderer_row(
    struct ToriRS_PorcelainDescribe* describe,
    struct ClientSettingsState* state,
    struct ToriRS_Api* api,
    struct ToriRS_SelectOption* options)
{
    int request = 0;
    int active = 0;
    int available = 0;
    int count = 0;
    char const* selected = NULL;

    if( !cs_display_value(api, TORIRS_DISPLAY_RENDERER, &request) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDERER_ACTIVE, &active) ||
        !cs_display_value(api, TORIRS_DISPLAY_RENDERERS_AVAILABLE, &available) )
        return;
    assert(active >= 0);
    assert(active < TORIRS_RENDERER_COUNT);
    for( int kind = 0; kind < TORIRS_RENDERER_COUNT; kind++ )
    {
        if( !(available & (1 << kind)) )
            continue;
        memset(&options[count], 0, sizeof(options[count]));
        options[count].struct_size = sizeof(options[count]);
        options[count].value = CS_RENDERER_VALUE[kind];
        options[count].label = CS_RENDERER_LABEL[kind];
        options[count].enabled = true;
        count++;
        /* The launch's own choice is not an option of its own: it shows as
         * the renderer it resolved to. */
        if( kind + 1 == request || (request == 0 && kind == active) )
            selected = CS_RENDERER_VALUE[kind];
    }
    if( count < 2 )
        return;
    /* A saved pick this lane does not offer: show what is drawing. */
    if( !selected )
        selected = CS_RENDERER_VALUE[active];
    cs_select_row(describe, state, CS_ID_RENDERER, "Renderer", selected, options, count,
        cs_pick_renderer);

    cs_renderer_now(api, state->renderer_now, sizeof(state->renderer_now));
    if( state->renderer_now[0] )
    {
        struct PorcelainRow row;
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_RENDERER_NOW;
        row.kind = PORCELAIN_ROW_LABEL;
        row.text = state->renderer_now;
        Porcelain_Row(describe, &row);
    }
}

/* The index of `value` among `values`, or -1. */
static int
cs_value_row(char const* const* values, int count, int value)
{
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    for( int i = 0; i < count; i++ )
        if( strcmp(values[i], text) == 0 ) return i;
    return -1;
}

/*
 * The page, described.
 *
 * `view` is not read. The page and the settings face show the same rows,
 * because these ARE the settings; PORCELAIN_FACE_BOTH is where that is said.
 */
static void
cs_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct ClientSettingsState* state = user;
    struct ToriRS_Api* api = state->api;
    struct ToriRS_SelectOption scale_options[CS_SCALE_ROWS];
    struct ToriRS_SelectOption filter_options[CS_FILTER_ROWS];
    struct ToriRS_SelectOption stretch_options[CS_STRETCH_ROWS];
    struct ToriRS_SelectOption limit_options[CS_PIXEL_LIMIT_ROWS];
    struct ToriRS_SelectOption frame_filter_options[CS_FRAME_FILTER_ROWS];
    struct ToriRS_SelectOption high_dpi_options[CS_HIGH_DPI_ROWS];
    struct ToriRS_SelectOption renderer_options[TORIRS_RENDERER_COUNT];
    struct PorcelainRow row;
    int value = 0, min = 0, max = 0, width = 0;
    int stretch = -1;

    assert(describe);
    assert(state);
    assert(api);
    assert(api->client);
    state->renderer_now[0] = '\0';
    cs_renderer_row(describe, state, api, renderer_options);

    if( api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE, &value, &min, &max) )
    {
        int const at = cs_nearest_row(value, min, 25, CS_SCALE_ROWS);
        (void)max;
        cs_static_options(scale_options, CS_SCALE_VALUE, CS_SCALE_LABEL, CS_SCALE_ROWS);
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_SCALE;
        row.kind = PORCELAIN_ROW_SELECT;
        row.label = "Interface scaling";
        row.text = CS_SCALE_VALUE[at];
        row.options = scale_options;
        row.option_count = CS_SCALE_ROWS;
        row.on_action = cs_pick_scale;
        row.user = state;
        Porcelain_Row(describe, &row);
    }
    if( api->client->display_get(api, TORIRS_DISPLAY_STRETCH_MODE, &stretch, NULL, NULL) )
    {
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_INTEGER_SCALING;
        row.kind = PORCELAIN_ROW_TOGGLE;
        row.label = "Integer scaling";
        row.value = stretch == CS_STRETCH_INTEGER;
        row.on_action = cs_toggle_integer_scaling;
        row.user = state;
        Porcelain_Row(describe, &row);
    }
    if( api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, &value, &min, &max) )
    {
        int const at = cs_nearest_row(value, min, 1, CS_FILTER_ROWS);
        (void)max;
        cs_static_options(filter_options, CS_FILTER_VALUE, CS_FILTER_LABEL, CS_FILTER_ROWS);
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_FILTER;
        row.kind = PORCELAIN_ROW_SELECT;
        row.label = "Interface filter";
        row.text = CS_FILTER_VALUE[at];
        row.options = filter_options;
        row.option_count = CS_FILTER_ROWS;
        row.on_action = cs_pick_filter;
        row.user = state;
        Porcelain_Row(describe, &row);
    }

    /*
     * Stretch mode only where it can be seen.
     *
     * A resizable buffer is the window's own shape -- the pixel limit and the
     * interface scale shrink both axes by one factor -- so keep aspect and
     * stretch draw the same picture there. They differ only when the buffer
     * could not follow the window: a fixed frame, or a resizable one raised
     * to the frame's minimum because the window could not hold it. Whole
     * pixels decides the placement itself, so the row is not offered then.
     */
    if( stretch >= 0 && stretch != CS_STRETCH_INTEGER )
    {
        int fixed = 0;
        int adjusted = 0;
        bool const shape_can_differ =
            (cs_display_value(api, TORIRS_DISPLAY_WINDOW_FIXED, &fixed) && fixed) ||
            (cs_display_value(api, TORIRS_DISPLAY_SCALE_ADJUSTED, &adjusted) &&
             (adjusted & TORIRS_DISPLAY_ADJUSTED_LOWERED_TO_FIT));
        if( shape_can_differ )
        {
            int at = cs_value_row(CS_STRETCH_VALUE, CS_STRETCH_ROWS, stretch);
            assert(at >= 0);
            cs_static_options(stretch_options, CS_STRETCH_VALUE, CS_STRETCH_LABEL, CS_STRETCH_ROWS);
            cs_select_row(describe, state, CS_ID_STRETCH, "Stretch mode", CS_STRETCH_VALUE[at],
                stretch_options, CS_STRETCH_ROWS, cs_pick_stretch);
        }
    }
    if( api->client->display_get(api, TORIRS_DISPLAY_MAX_PIXEL_HEIGHT, &value, &min, &max) &&
        api->client->display_get(api, TORIRS_DISPLAY_MAX_PIXEL_WIDTH, &width, NULL, NULL) )
    {
        char current[32];
        int count = CS_PIXEL_LIMIT_LISTED;
        int at = -1;
        char const* selected;

        if( value == 0 && width == 0 )
            snprintf(current, sizeof(current), "0");
        else
            snprintf(current, sizeof(current), "%dx%d", width, value);
        for( int i = 0; i < CS_PIXEL_LIMIT_LISTED; i++ )
            if( strcmp(CS_PIXEL_LIMIT_VALUE[i], current) == 0 ) at = i;
        cs_static_options(
            limit_options, CS_PIXEL_LIMIT_VALUE, CS_PIXEL_LIMIT_LABEL, CS_PIXEL_LIMIT_LISTED);
        if( at >= 0 )
            selected = CS_PIXEL_LIMIT_VALUE[at];
        else
        {
            /* A value only preferences.ini could hold -- or one half of a
             * pair, which a file written before the limit was a resolution
             * holds. Shown as itself rather than snapped to a neighbour, which
             * would claim a limit the client is not applying. */
            snprintf(state->pixel_limit_value, sizeof(state->pixel_limit_value), "%s", current);
            if( width == 0 )
                snprintf(state->pixel_limit_label, sizeof(state->pixel_limit_label),
                    "Any x %d", value);
            else if( value == 0 )
                snprintf(state->pixel_limit_label, sizeof(state->pixel_limit_label),
                    "%d x any", width);
            else
                snprintf(state->pixel_limit_label, sizeof(state->pixel_limit_label),
                    "%dx%d", width, value);
            memset(&limit_options[count], 0, sizeof(limit_options[count]));
            limit_options[count].struct_size = sizeof(limit_options[count]);
            limit_options[count].value = state->pixel_limit_value;
            limit_options[count].label = state->pixel_limit_label;
            limit_options[count].enabled = true;
            count++;
            selected = state->pixel_limit_value;
        }
        cs_select_row(describe, state, CS_ID_PIXEL_LIMIT, "Render resolution", selected, limit_options,
            count, cs_pick_pixel_limit);
    }
    if( api->client->display_get(api, TORIRS_DISPLAY_FRAME_FILTER, &value, &min, &max) )
    {
        int at = cs_value_row(CS_FRAME_FILTER_VALUE, CS_FRAME_FILTER_ROWS, value);
        assert(at >= 0);
        cs_static_options(
            frame_filter_options, CS_FRAME_FILTER_VALUE, CS_FRAME_FILTER_LABEL, CS_FRAME_FILTER_ROWS);
        cs_select_row(describe, state, CS_ID_FRAME_FILTER, "Frame filter", CS_FRAME_FILTER_VALUE[at],
            frame_filter_options, CS_FRAME_FILTER_ROWS, cs_pick_frame_filter);
    }
    if( api->client->display_get(api, TORIRS_DISPLAY_HIGH_DPI, &value, &min, &max) )
    {
        int at = cs_value_row(CS_HIGH_DPI_VALUE, CS_HIGH_DPI_ROWS, value);
        int in_force = 0;
        assert(at >= 0);
        cs_static_options(high_dpi_options, CS_HIGH_DPI_VALUE, CS_HIGH_DPI_LABEL, CS_HIGH_DPI_ROWS);
        /* Automatic names what it means on this lane, so choosing it is not a
         * guess. */
        if( cs_display_value(api, TORIRS_DISPLAY_HIGH_DPI_IN_FORCE, &in_force) && in_force >= 1 &&
            in_force < CS_HIGH_DPI_ROWS )
        {
            snprintf(state->high_dpi_auto_label, sizeof(state->high_dpi_auto_label),
                "Automatic (%s)", CS_HIGH_DPI_NAME[in_force]);
            if( value == 0 )
                high_dpi_options[0].label = state->high_dpi_auto_label;
            else
                high_dpi_options[0].label = CS_HIGH_DPI_LABEL[0];
        }
        cs_select_row(describe, state, CS_ID_HIGH_DPI, "HighDPI", CS_HIGH_DPI_VALUE[at],
            high_dpi_options, CS_HIGH_DPI_ROWS, cs_pick_high_dpi);
        cs_high_dpi_now(api, state->high_dpi_now, sizeof(state->high_dpi_now));
        if( state->high_dpi_now[0] )
        {
            memset(&row, 0, sizeof(row));
            row.key = CS_ID_HIGH_DPI_NOW;
            row.kind = PORCELAIN_ROW_LABEL;
            row.text = state->high_dpi_now;
            Porcelain_Row(describe, &row);
        }
    }

    cs_display_signature(api, state->display_seen, sizeof(state->display_seen));
    cs_scaling_now(api, state->scaling_now, sizeof(state->scaling_now));
    if( state->scaling_now[0] )
    {
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_SCALING_NOW;
        row.kind = PORCELAIN_ROW_LABEL;
        row.text = state->scaling_now;
        Porcelain_Row(describe, &row);

        memset(&row, 0, sizeof(row));
        row.key = CS_ID_SCALING_HOW;
        row.kind = PORCELAIN_ROW_PARAGRAPH;
        row.text = "The world is drawn at the window's resolution, or the render resolution if "
                   "smaller. Interface scaling sizes only the interface drawn over it; the frame "
                   "filter fits the result to the window.";
        Porcelain_Row(describe, &row);
    }

    memset(&row, 0, sizeof(row));
    row.key = "note";
    row.kind = PORCELAIN_ROW_LABEL;
    row.text = "Interface scaling sizes the interface; the 3D world keeps its resolution.";
    Porcelain_Row(describe, &row);
}

static void
cs_on_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct ClientSettingsState* state = state_ptr;
    assert(api);
    assert(state);
    assert(api->client);
    /* An essential settings page with no layer is a page that cannot exist. */
    assert(api->porcelain);
    state->api = api;
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_CLIENT_SETTINGS, state);
    Porcelain_Panel(state->porcelain, NULL, TORIRS_PANEL_WIDTH_DEFAULT, PORCELAIN_FACE_BOTH);
    /*
     * There was a Porcelain_ExpectUnsupported("gameframe_id_ceiling") here,
     * and it is gone with the limitation it declared. A declaration has to be
     * true in both directions or it is not evidence: the row model's option
     * strings reach the host's own 192 now, which is wider than any id, label
     * or reason the frame API can carry, so nothing is ever dropped for it.
     * @see the static assertions at the top of this file.
     */
    Porcelain_Describe(state->porcelain, cs_describe, state);
}

static void
cs_on_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct ClientSettingsState* state = state_ptr;
    (void)api;
    assert(state);
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

/*
 * The display readouts, watched: none of them is one of Porcelain's inputs,
 * so the plugin compares them and forwards a change.
 */
static void
cs_on_frame_start(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct ClientSettingsState* state = state_ptr;
    (void)event;
    assert(api);
    assert(state);
    {
        /* The readout moves without any setting moving -- a window drag, the
         * pixel limit raising the scale -- and a setting can move without this
         * page, so both are compared, not assumed. */
        char now[PORCELAIN_ROW_TEXT_MAX];
        char seen[PORCELAIN_ROW_TEXT_MAX];
        char dpi[PORCELAIN_ROW_TEXT_MAX];
        cs_scaling_now(api, now, sizeof(now));
        cs_display_signature(api, seen, sizeof(seen));
        cs_high_dpi_now(api, dpi, sizeof(dpi));
        if( strcmp(now, state->scaling_now) != 0 || strcmp(seen, state->display_seen) != 0 ||
            strcmp(dpi, state->high_dpi_now) != 0 )
            Porcelain_Note(state->porcelain, PORCELAIN_INPUT_EXPLICIT);
    }
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
}

static void
cs_on_ui_build(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct ClientSettingsState* state = state_ptr;
    (void)api;
    assert(state);
    assert(panel);
    /* The same description on both faces: these ARE the settings. */
    Porcelain_PanelBuild(state->porcelain, panel, view);
}

static void
cs_on_ui_action(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelActionEvent const* event)
{
    struct ClientSettingsState* state = state_ptr;
    (void)api;
    assert(state);
    assert(event);
    (void)Porcelain_PanelAction(state->porcelain, event);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS = {
    .struct_size = sizeof(TORIRS_PLUGIN_CLIENT_SETTINGS),
    .id = "client-settings",
    .title = "Client Settings",
    .version = "2.0.0",
    .state_size = sizeof(struct ClientSettingsState),
    .flags = TORIRS_PLUGIN_ESSENTIAL,
    .event_priority = 999,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = cs_on_start,
        .on_stop = cs_on_stop,
        .on_frame_start = cs_on_frame_start,
        .on_ui_build = cs_on_ui_build,
        .on_ui_action = cs_on_ui_action,
    },
};
