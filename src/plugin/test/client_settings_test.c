/*
 * The client-owned settings page, against a fake engine seam.
 *
 * What is under test is the SEAM: the plugin describes rows, the Porcelain
 * row reconciler turns that description into builder->node calls and panel
 * setters, and this file is the engine end of those calls. Every ledger row
 * for this plugin is a statement about what reaches the seam and what does
 * not -- which is why the checks below count calls rather than inspect the
 * plugin's own state, and why the fake is the real one's contract and not a
 * stub that says OK to everything.
 *
 * Three things the old hand-rolled version could not check, and now does:
 * that the same description serves both faces; that a catalogue which GROWS
 * or SHRINKS costs a setter and not a page (host fix H3); and that a settled
 * page costs nothing at all -- no builder call, no setter, no invalidate.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS;

static int checks;
static int failures;
#define CHECK(c, m) do { checks++; if( !(c) ) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (m)); } } while( 0 )

#define CS_FRAME_ROWS_MAX 33
#define FAKE_ROWS_MAX 16

/* ------------------------------------------------------------ fake engine */

struct FakeRow
{
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char label[TORIRS_UI_LABEL_MAX];
    char text[192];
    int kind;
    int option_count;
    char option_value[CS_FRAME_ROWS_MAX][TORIRS_PLUGIN_FRAME_ID_MAX];
    char option_label[CS_FRAME_ROWS_MAX][TORIRS_UI_LABEL_MAX];
    char option_detail[CS_FRAME_ROWS_MAX][TORIRS_FRAME_REASON_MAX];
    char selected_value[TORIRS_PLUGIN_FRAME_ID_MAX];
};

struct Fake
{
    struct ToriRS_FrameOfferInfo offers[6];
    int offer_count;
    struct ToriRS_FrameSelection selection;

    int requests;
    int invalidates;
    int option_sets;
    int text_sets;
    int label_sets;
    int scroll;
    int value_sets;
    int height_sets;
    int reidentifies;
    int redraws;
    int nodes;
    int selects;
    int config_writes;
    int logs;
    char last_log[256];

    char selected_id[TORIRS_PLUGIN_FRAME_ID_MAX];
    /** Whether frame.select is allowed to succeed. */
    bool select_fails;

    /* The page, as the host would hold it. */
    struct FakeRow rows[FAKE_ROWS_MAX];
    int row_count;

    int ui_scale;
    int ui_scale_filter;
    bool display_present;
};
static struct Fake fake;

static struct FakeRow*
fake_row(char const* id)
{
    for( int i = 0; i < fake.row_count; i++ )
        if( strcmp(fake.rows[i].id, id) == 0 ) return &fake.rows[i];
    return NULL;
}

static void
fake_log(struct ToriRS_Api* api, char const* format, ...)
{
    (void)api;
    fake.logs++;
    snprintf(fake.last_log, sizeof(fake.last_log), "%s", format);
}

static int
fake_offer_next(struct ToriRS_Api* api, int iterator, struct ToriRS_FrameOfferInfo* out)
{
    int const next = iterator + 1;
    (void)api;
    if( next < 0 || next >= fake.offer_count ) return -1;
    *out = fake.offers[next];
    return next;
}

static void
fake_selection(struct ToriRS_Api* api, struct ToriRS_FrameSelection* out)
{ (void)api; *out = fake.selection; }

static enum ToriRS_Result
fake_select(struct ToriRS_Api* api, char const* id)
{
    (void)api;
    if( fake.select_fails ) return TORIRS_RESULT_INVALID;
    fake.selects++;
    snprintf(fake.selected_id, sizeof(fake.selected_id), "%s", id);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id), "%s", id);
    fake.selection.revision++;
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_panel_request(struct ToriRS_Api* api, struct ToriRS_PanelDescriptor const* desc)
{ (void)api; (void)desc; fake.requests++; return TORIRS_RESULT_OK; }

/* A rebuild: the host clears the page and calls the plugin back for a fresh
 * declaration. The page really is emptied here, because a reconciler that
 * could not tell a rebuild from a patch is the whole point of the counters. */
static void
fake_panel_invalidate(struct ToriRS_Api* api)
{ (void)api; fake.invalidates++; }

static enum ToriRS_Result
fake_panel_set_text(struct ToriRS_Api* api, char const* id, char const* text)
{
    struct FakeRow* row = fake_row(id);
    (void)api;
    fake.text_sets++;
    if( !row ) return TORIRS_RESULT_NOT_FOUND;
    snprintf(row->text, sizeof(row->text), "%s", text ? text : "");
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_panel_set_label(struct ToriRS_Api* api, char const* id, char const* label)
{
    struct FakeRow* row = fake_row(id);
    (void)api;
    fake.label_sets++;
    if( !row ) return TORIRS_RESULT_NOT_FOUND;
    snprintf(row->label, sizeof(row->label), "%s", label ? label : "");
    return TORIRS_RESULT_OK;
}

/* The page is at the top and stays there: nothing in this plugin scrolls, and
 * a fake that answered 0 for "no page" would make the two indistinguishable. */
static int
fake_panel_scroll(struct ToriRS_Api* api)
{ (void)api; return fake.scroll; }

static enum ToriRS_Result
fake_panel_scroll_to(struct ToriRS_Api* api, int scroll)
{ (void)api; fake.scroll = scroll; return TORIRS_RESULT_OK; }

static enum ToriRS_Result
fake_panel_set_value(struct ToriRS_Api* api, char const* id, int value)
{ (void)api; (void)value; fake.value_sets++; return fake_row(id) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }

static enum ToriRS_Result
fake_panel_set_height(struct ToriRS_Api* api, char const* id, int height)
{ (void)api; (void)height; fake.height_sets++; return fake_row(id) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }

static void
fake_panel_redraw(struct ToriRS_Api* api, char const* id)
{ (void)api; (void)id; fake.redraws++; }

static enum ToriRS_Result
fake_panel_reidentify(struct ToriRS_Api* api, char const* id)
{ (void)api; (void)id; fake.reidentifies++; return TORIRS_RESULT_OK; }

static void
fake_store_options(
    struct FakeRow* row,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int count)
{
    row->option_count = count;
    snprintf(row->selected_value, sizeof(row->selected_value), "%s", value ? value : "");
    for( int i = 0; i < count && i < CS_FRAME_ROWS_MAX; i++ )
    {
        snprintf(row->option_value[i], sizeof(row->option_value[i]), "%s", options[i].value);
        snprintf(row->option_label[i], sizeof(row->option_label[i]), "%s", options[i].label);
        snprintf(row->option_detail[i], sizeof(row->option_detail[i]), "%s",
            options[i].detail ? options[i].detail : "");
    }
}

static enum ToriRS_Result
fake_panel_set_options(
    struct ToriRS_Api* api,
    char const* id,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int count)
{
    struct FakeRow* row = fake_row(id);
    (void)api;
    fake.option_sets++;
    if( !row ) return TORIRS_RESULT_NOT_FOUND;
    fake_store_options(row, value, options, count);
    return TORIRS_RESULT_OK;
}

/* The builder. `node` is the only verb Porcelain uses, so it is the only one
 * that has to be true; the shorthands are here because the host's struct has
 * them and a plugin that reached for one should fail loudly rather than
 * dereference a hole. */
static enum ToriRS_Result
fake_node(struct ToriRS_PanelBuilder* panel, struct ToriRS_PanelNode const* node)
{
    struct FakeRow* row;
    (void)panel;
    fake.nodes++;
    if( fake.row_count >= FAKE_ROWS_MAX ) return TORIRS_RESULT_BUDGET;
    row = &fake.rows[fake.row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->id, sizeof(row->id), "%s", node->id ? node->id : "");
    snprintf(row->label, sizeof(row->label), "%s", node->label ? node->label : "");
    snprintf(row->text, sizeof(row->text), "%s", node->text ? node->text : "");
    row->kind = node->kind;
    if( node->option_count > 0 )
        fake_store_options(row, node->text, node->options, node->option_count);
    return TORIRS_RESULT_OK;
}

static bool
fake_display_get(struct ToriRS_Api* api, int setting, int* value, int* min, int* max)
{
    (void)api;
    if( !fake.display_present ) return false;
    if( setting == TORIRS_DISPLAY_UI_SCALE )
    {
        if( value ) *value = fake.ui_scale;
        if( min ) *min = 100;
        if( max ) *max = 400;
        return true;
    }
    if( setting == TORIRS_DISPLAY_UI_SCALE_FILTER )
    {
        if( value ) *value = fake.ui_scale_filter;
        if( min ) *min = 10;
        if( max ) *max = 12;
        return true;
    }
    return false;
}

static enum ToriRS_Result
fake_display_set(struct ToriRS_Api* api, int setting, int value)
{
    (void)api;
    if( !fake.display_present ) return TORIRS_RESULT_NOT_FOUND;
    if( setting == TORIRS_DISPLAY_UI_SCALE )
    {
        fake.ui_scale = value;
        return TORIRS_RESULT_OK;
    }
    if( setting == TORIRS_DISPLAY_UI_SCALE_FILTER )
    {
        fake.ui_scale_filter = value;
        return TORIRS_RESULT_OK;
    }
    return TORIRS_RESULT_NOT_FOUND;
}

/* This plugin has no config schema at all, so a write here is the ledger row
 * "never writes plugin config" failing. */
static enum ToriRS_Result
fake_config_set(struct ToriRS_Api* api, char const* key, char const* value)
{ (void)api; (void)key; (void)value; fake.config_writes++; return TORIRS_RESULT_OK; }
static bool
fake_config_has(struct ToriRS_Api* api, char const* key)
{ (void)api; (void)key; return false; }

/* ------------------------------------------------------------------ setup */

static void
offer(int row, char const* id, char const* title, bool available)
{
    memset(&fake.offers[row], 0, sizeof(fake.offers[row]));
    fake.offers[row].struct_size = sizeof(fake.offers[row]);
    snprintf(fake.offers[row].id, sizeof(fake.offers[row].id), "%s", id);
    snprintf(fake.offers[row].title, sizeof(fake.offers[row].title), "%s", title);
    fake.offers[row].available = available;
}

static struct ToriRS_Api api;
static struct ToriRS_ClientApi client;
static struct ToriRS_PanelBuilder builder;

/** A frame in which the page is up: the fence, then the host's build if the
 *  reconciler asked for one. */
static void
frame_with_page(void* state, int view)
{
    int const invalidates = fake.invalidates;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_frame_start(&api, state, NULL);
    if( fake.invalidates != invalidates )
    {
        fake.row_count = 0;
        TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_build(&api, state, &builder, view);
    }
}

/** The host's own build: the page is cleared and re-declared. */
static void
build_page(void* state, int view)
{
    fake.row_count = 0;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_build(&api, state, &builder, view);
}

static void
pick(void* state, char const* id, char const* value)
{
    struct ToriRS_PanelActionEvent action;
    memset(&action, 0, sizeof(action));
    action.id = id;
    action.action = TORIRS_PANEL_ACTION_PICK;
    action.text = value;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_action(&api, state, &action);
}

static int
row_index(char const* id)
{
    for( int i = 0; i < fake.row_count; i++ )
        if( strcmp(fake.rows[i].id, id) == 0 ) return i;
    return -1;
}

static void
key_sequence(char* out, size_t out_size)
{
    size_t at = 0;
    out[0] = '\0';
    for( int i = 0; i < fake.row_count; i++ )
        at += (size_t)snprintf(out + at, out_size - at, "%s:%d|",
            fake.rows[i].id, fake.rows[i].kind);
}

int
main(void)
{
    void* state;
    int invalidates, option_sets, text_sets, nodes;
    char page_keys[512];
    char settings_keys[512];
    struct FakeRow const* row;

    memset(&fake, 0, sizeof(fake));
    Porcelain_ResetForTesting();

    offer(0, "core/native", "Native", true);
    offer(1, "gameframe-layout/classic-fixed", "Classic|Fixed", true);
    offer(2, "gameframe-layout/modern-resizable", "Modern Resizable", true);
    offer(3, "mobile-gameframe/stone-drawer", "Stone Drawer", true);
    fake.offer_count = 4;
    fake.selection.struct_size = sizeof(fake.selection);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "gameframe-layout/modern-resizable");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id),
        "%s", "gameframe-layout/modern-resizable");
    fake.selection.status = TORIRS_FRAME_STATUS_ACTIVE;
    fake.selection.revision = 7;

    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.display_get = fake_display_get;
    client.display_set = fake_display_set;

    memset(&builder, 0, sizeof(builder));
    builder.struct_size = sizeof(builder);
    builder.node = fake_node;

    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    api.major_version = TORIRS_PLUGIN_API_MAJOR;
    api.minor_version = TORIRS_PLUGIN_API_MINOR;
    api.core.log = fake_log;
    api.frame.offer_next = fake_offer_next;
    api.frame.selection = fake_selection;
    api.frame.select = fake_select;
    api.panel.request = fake_panel_request;
    api.panel.invalidate = fake_panel_invalidate;
    api.panel.set_text = fake_panel_set_text;
    api.panel.set_label = fake_panel_set_label;
    api.panel.set_value = fake_panel_set_value;
    api.panel.set_height = fake_panel_set_height;
    api.panel.set_options = fake_panel_set_options;
    api.panel.redraw = fake_panel_redraw;
    api.panel.reidentify = fake_panel_reidentify;
    api.panel.scroll = fake_panel_scroll;
    api.panel.scroll_to = fake_panel_scroll_to;
    api.config.set = fake_config_set;
    api.config.has = fake_config_has;
    api.client = &client;
    api.porcelain = ToriRS_PorcelainApiTable();
    /* api.widgets is left entirely NULL on purpose. This page owns no widget
     * -- every row of it is a panel declaration -- so a describe or a commit
     * that reached widgets.revalidate would crash here rather than quietly
     * add a revalidate to the frame's budget. */
    state = calloc(1, TORIRS_PLUGIN_CLIENT_SETTINGS.state_size);

    /* -------------------------------------------- essential registration */

    CHECK(TORIRS_PLUGIN_CLIENT_SETTINGS.flags & TORIRS_PLUGIN_ESSENTIAL,
        "client settings is essential");
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_start(&api, state);
    CHECK(fake.requests == 1, "start registers one shared panel");

    /* --------------------------------------------------- the page, built */

    build_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe");
    CHECK(row != NULL, "the gameframe row is declared");
    CHECK(row && row->option_count == 4, "Auto plus three plugin offers are shown");
    CHECK(row && strcmp(row->option_value[0], "auto") == 0, "Auto has a stable value");
    CHECK(row && strcmp(row->option_label[1], "Classic|Fixed") == 0,
        "structured labels preserve delimiter text");
    CHECK(row && strcmp(row->selected_value, "gameframe-layout/modern-resizable") == 0,
        "the requested stable id is selected");
    CHECK(row && strcmp(row->label, "Gameframe") == 0,
        "the dropdown keeps its declaration label");
    row = fake_row("gameframe_detail");
    CHECK(row && strcmp(row->text, "Active: Modern Resizable.") == 0,
        "the active frame is described by title");
    CHECK(row_index("gameframe") < row_index("gameframe_detail") &&
          row_index("gameframe_detail") < row_index("note"),
        "the status line follows its dropdown and the note comes last");
    row = fake_row("note");
    CHECK(row && strcmp(row->text,
              "Scaling draws the whole canvas larger, the 3D scene included.") == 0,
        "the explanatory note is a static label");
    CHECK(fake.config_writes == 0, "the page writes no plugin config");

    /* --------------------------- the same description serves both faces */

    key_sequence(page_keys, sizeof(page_keys));
    build_page(state, TORIRS_PANEL_VIEW_SETTINGS);
    key_sequence(settings_keys, sizeof(settings_keys));
    CHECK(strcmp(page_keys, settings_keys) == 0,
        "the settings face declares the same rows as the page face");
    build_page(state, TORIRS_PANEL_VIEW_PAGE);

    /* ---------------------------------------- a pick uses a stable value */

    pick(state, "gameframe", "gameframe-layout/classic-fixed");
    CHECK(fake.selects == 1 &&
          strcmp(fake.selected_id, "gameframe-layout/classic-fixed") == 0,
        "the action uses its stable value, not a row number");

    invalidates = fake.invalidates;
    option_sets = fake.option_sets;
    text_sets = fake.text_sets;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(fake.invalidates == invalidates, "a pick does not rebuild the page");
    CHECK(fake.option_sets == option_sets + 1 && fake.text_sets == text_sets + 1,
        "a pick patches the two retained rows and nothing else");
    row = fake_row("gameframe");
    CHECK(row && strcmp(row->selected_value, "gameframe-layout/classic-fixed") == 0,
        "the retained dropdown carries the new selection");

    /* ---------------------------------------- an unknown value is refused */

    /*
     * The restatement is the LAYER's, so it lands on the next fence and not
     * inside the action.
     *
     * This plugin used to do it by hand, reaching past Porcelain to
     * api->panel.set_options with the values the mirror already held, because
     * there was no verb for "the host's copy of this row drifted". There is
     * now: Porcelain_Restate marks the row and the reconciler states it, which
     * is one call on the row and not a page -- the same cost the hand-written
     * one paid, from the code that was supposed to be paying it.
     */
    option_sets = fake.option_sets;
    invalidates = fake.invalidates;
    pick(state, "gameframe", "forged/frame");
    CHECK(fake.selects == 1, "an unknown stable value is refused");
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(fake.option_sets == option_sets + 1,
        "the refused row is restated from the live selection");
    CHECK(fake.invalidates == invalidates,
        "by a setter on that row, not by rebuilding the page");
    row = fake_row("gameframe");
    CHECK(row && strcmp(row->selected_value, "gameframe-layout/classic-fixed") == 0,
        "the restatement snaps the control back to what is live");

    /* ------------------------- a save the client refuses snaps back too */

    option_sets = fake.option_sets;
    invalidates = fake.invalidates;
    fake.select_fails = true;
    pick(state, "gameframe", "mobile-gameframe/stone-drawer");
    fake.select_fails = false;
    CHECK(fake.selects == 1, "a refused save writes nothing");
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(fake.option_sets == option_sets + 1,
        "a refused save restates the row rather than leaving it wrong");
    CHECK(fake.invalidates == invalidates, "and still without a rebuild");
    row = fake_row("gameframe");
    CHECK(row && strcmp(row->selected_value, "gameframe-layout/classic-fixed") == 0,
        "the control does not keep a gameframe that was never saved");

    /* ------------------------------------- a resolver change is a patch */

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "mobile-gameframe/stone-drawer");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id), "%s", "core/native");
    fake.selection.status = TORIRS_FRAME_STATUS_LOADING;
    snprintf(fake.selection.reason, sizeof(fake.selection.reason), "%s", "Starting provider.");
    fake.selection.revision++;
    invalidates = fake.invalidates;
    option_sets = fake.option_sets;
    text_sets = fake.text_sets;
    nodes = fake.nodes;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(fake.invalidates == invalidates, "a resolver change does not rebuild the page");
    CHECK(fake.nodes == nodes, "a resolver change declares no row again");
    CHECK(fake.option_sets == option_sets + 1 && fake.text_sets == text_sets + 1,
        "a resolver change patches its two retained rows");
    row = fake_row("gameframe_detail");
    CHECK(row && strstr(row->text, "Loading Stone Drawer.") != NULL,
        "loading detail names the requested frame");
    CHECK(row && strstr(row->text, "Active for now: Native gameframe.") != NULL,
        "loading detail names the active frame");

    /* ------------------------------------------ the steady state is free */

    invalidates = fake.invalidates;
    option_sets = fake.option_sets;
    text_sets = fake.text_sets;
    nodes = fake.nodes;
    for( int i = 0; i < 8; i++ )
        frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(fake.invalidates == invalidates, "an unchanged selection is retained");
    CHECK(fake.option_sets == option_sets && fake.text_sets == text_sets &&
          fake.value_sets == 0 && fake.height_sets == 0,
        "eight settled frames cost zero panel setters");
    CHECK(fake.nodes == nodes && fake.reidentifies == 0 && fake.redraws == 0,
        "eight settled frames declare nothing and re-identify nothing");

    /* -------------------------- an unavailable saved id stays selectable */

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "removed-provider/favourite");
    fake.selection.status = TORIRS_FRAME_STATUS_FALLBACK;
    fake.selection.revision++;
    invalidates = fake.invalidates;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe");
    CHECK(row && row->option_count == 5, "an unavailable saved id remains visible");
    CHECK(row && strcmp(row->option_value[4], "removed-provider/favourite") == 0 &&
          strcmp(row->selected_value, "removed-provider/favourite") == 0,
        "the unavailable row retains and selects the exact saved id");
    CHECK(row && strcmp(row->option_label[4], "Unavailable: removed-provider/favourite") == 0,
        "the unavailable row says so in its label");
    CHECK(row && strcmp(row->option_detail[4], "Provider is not currently available") == 0,
        "the unavailable row carries its reason");
    CHECK(fake.invalidates == invalidates,
        "a catalogue that GREW by one row costs a setter, not a rebuild");
    /* The sentence names the frame, and a missing provider's only name is
     * the saved id. The row's LABEL says "Unavailable: <id>" and the sentence
     * must not repeat that judgement: it used to read "Could not use
     * Unavailable: <id>", a prefix that already means could not use. */
    row = fake_row("gameframe_detail");
    CHECK(row && strcmp(row->text,
              "Could not use removed-provider/favourite. "
              "Active fallback: Native gameframe. Starting provider.") == 0,
        "a fallback names what could not be used and what is up instead");
    CHECK(row && strstr(row->text, "Unavailable:") == NULL,
        "the sentence does not repeat the row label's judgement");

    /* ------------------------------- a catalogue that shrinks is a setter */

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "gameframe-layout/classic-fixed");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id),
        "%s", "gameframe-layout/classic-fixed");
    fake.selection.status = TORIRS_FRAME_STATUS_ACTIVE;
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    invalidates = fake.invalidates;
    option_sets = fake.option_sets;
    nodes = fake.nodes;
    /* A provider goes away, exactly as unplugging one does. */
    fake.offer_count = 3;
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe");
    CHECK(row && row->option_count == 3, "the departed provider is gone from the list");
    CHECK(fake.invalidates == invalidates && fake.nodes == nodes,
        "a catalogue that SHRANK costs a setter, not a rebuild");
    CHECK(fake.option_sets == option_sets + 1, "and costs exactly one setter");
    fake.offer_count = 4;
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);

    /* ------------------------- a saved native id is offered as itself */

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "core/native");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id),
        "%s", "core/native");
    fake.selection.status = TORIRS_FRAME_STATUS_NATIVE;
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe_detail");
    CHECK(row && strcmp(row->text, "Active: Native gameframe.") == 0,
        "an explicitly requested native gameframe reads as active, not switching");
    row = fake_row("gameframe");
    CHECK(row && strcmp(row->selected_value, "core/native") == 0 &&
          strcmp(row->option_label[row->option_count - 1], "Native gameframe") == 0,
        "the saved native gameframe is offered as itself, not as unavailable");

    /* The host answers a saved request for the native frame's own id with
     * FALLBACK -- "core/native" is in no catalogue -- while committing that
     * very frame as active. The ids agree, so there is no gap to report and
     * the resolver's catalogue reason describes nothing on screen. This read
     * "Could not use Native gameframe. Active fallback: Native gameframe."
     * over the drawn rs289lc frame. */
    fake.selection.status = TORIRS_FRAME_STATUS_FALLBACK;
    snprintf(fake.selection.reason, sizeof(fake.selection.reason), "%s",
        "The requested gameframe is not installed in this build.");
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe_detail");
    CHECK(row && strcmp(row->text, "Active: Native gameframe.") == 0,
        "a fallback whose active frame IS the requested one reads as active");
    fake.selection.reason[0] = '\0';
    fake.selection.status = TORIRS_FRAME_STATUS_NATIVE;

    /* ------------------------------- Auto under a native lane reads Auto */

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id), "%s", "auto");
    fake.selection.revision++;
    frame_with_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe_detail");
    CHECK(row && strcmp(row->text, "Active: Native gameframe. Auto follows this lane.") == 0,
        "Auto on a native lane says Auto follows the lane");

    /* ---------------------------------------------- the display controls */

    free(state);
    memset(&fake, 0, sizeof(fake));
    Porcelain_ResetForTesting();
    offer(0, "core/native", "Native", true);
    fake.offer_count = 1;
    fake.selection.struct_size = sizeof(fake.selection);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id), "%s", "auto");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id), "%s", "auto");
    fake.selection.status = TORIRS_FRAME_STATUS_NATIVE;
    fake.selection.revision = 1;
    fake.display_present = true;
    /* 138 and not a round number: it is 13 past 125 and 12 short of 150, so a
     * row that TRUNCATED rather than snapped would read 125 and a test that
     * used any multiple of 25 could not tell the two apart. */
    fake.ui_scale = 138;
    fake.ui_scale_filter = 11;
    state = calloc(1, TORIRS_PLUGIN_CLIENT_SETTINGS.state_size);
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_start(&api, state);
    build_page(state, TORIRS_PANEL_VIEW_PAGE);

    row = fake_row("ui_scale");
    CHECK(row && row->option_count == 13, "thirteen scaling steps");
    CHECK(row && strcmp(row->option_value[0], "100") == 0 &&
          strcmp(row->option_label[0], "100%") == 0,
        "scaling steps carry a percent label and an absolute value");
    CHECK(row && strcmp(row->option_value[12], "400") == 0, "the last step is 400%");
    CHECK(row && strcmp(row->selected_value, "150") == 0,
        "the live scale snaps to the nearest step");
    row = fake_row("ui_scale_filter");
    CHECK(row && row->option_count == 3, "three scaling filters");
    CHECK(row && strcmp(row->option_label[1], "Linear") == 0, "the filters are named");
    CHECK(row && strcmp(row->selected_value, "1") == 0,
        "the filter row is offset from the store's own minimum");

    pick(state, "ui_scale", "250");
    CHECK(fake.ui_scale == 250, "a scale pick writes the absolute value to the store");
    pick(state, "ui_scale_filter", "2");
    CHECK(fake.ui_scale_filter == 12,
        "a filter pick offsets its index by the store's minimum");
    CHECK(fake.config_writes == 0, "no display setting is written to plugin config");

    /* A build with no display store declares neither row -- the same page the
     * unported plugin built, and the reason the two reads are a gate. */
    free(state);
    memset(&fake.rows, 0, sizeof(fake.rows));
    fake.row_count = 0;
    fake.display_present = false;
    Porcelain_ResetForTesting();
    state = calloc(1, TORIRS_PLUGIN_CLIENT_SETTINGS.state_size);
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_start(&api, state);
    build_page(state, TORIRS_PANEL_VIEW_PAGE);
    CHECK(!fake_row("ui_scale") && !fake_row("ui_scale_filter"),
        "a build with no display store declares no display rows");
    CHECK(fake_row("gameframe") && fake_row("note"),
        "and still declares the gameframe row and the note");

    /* ------------------------ the widest id the frame API can carry fits */

    /*
     * This used to be the opposite test.
     *
     * Porcelain's option ceiling was 96 while the host accepted 192, so a
     * provider id of the frame API's own maximum was refused by the LAYER --
     * a refusal about the library and not about the lane, which is the one
     * thing a portability layer must not invent. Worse, the refusal poisoned
     * the whole describe, so one over-long id blanked the entire settings
     * page. The ceilings agree now, and the widest id that can reach this
     * plugin is narrower than both.
     */
    CHECK(TORIRS_PLUGIN_FRAME_ID_MAX <= PORCELAIN_OPTION_VALUE_MAX,
        "no id the frame API can carry is past the row model's option ceiling");
    CHECK(TORIRS_UI_LABEL_MAX <= PORCELAIN_OPTION_LABEL_MAX,
        "and no label it can carry is past the label one");

    free(state);
    memset(&fake, 0, sizeof(fake));
    Porcelain_ResetForTesting();
    fake.display_present = false;
    {
        char long_id[TORIRS_PLUGIN_FRAME_ID_MAX];
        memset(long_id, 'x', sizeof(long_id) - 1);
        long_id[sizeof(long_id) - 1] = '\0';
        memcpy(long_id, "provider/", 9);
        offer(0, "gameframe-layout/classic-fixed", "Classic Fixed", true);
        offer(1, long_id, "As long as an id can be", true);
        fake.offer_count = 2;
    }
    fake.selection.struct_size = sizeof(fake.selection);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id), "%s", "auto");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id), "%s", "auto");
    fake.selection.status = TORIRS_FRAME_STATUS_NATIVE;
    fake.selection.revision = 1;
    state = calloc(1, TORIRS_PLUGIN_CLIENT_SETTINGS.state_size);
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_start(&api, state);
    build_page(state, TORIRS_PANEL_VIEW_PAGE);
    row = fake_row("gameframe");
    CHECK(row && row->option_count == 3,
        "the longest id the frame API allows is offered, not dropped");
    CHECK(fake_row("gameframe_detail") && fake_row("note"),
        "and the rest of the page is declared with it");
    CHECK(fake.logs == 0, "with nothing to say, because nothing was refused");

    free(state);
    printf("client_settings_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
