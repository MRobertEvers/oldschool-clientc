/*
 * Client Settings against a hand-built API table.
 *
 * The frame selector is deliberately tested without PluginHost: this is the
 * public edge that must turn a legacy dropdown row back into a stable frame
 * id. A fake host also lets the test put misleading text on the click and
 * prove the plugin never treats that presentation string as state.
 */

#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_CLIENT_SETTINGS;

static int g_checks;
static int g_failures;

#define CHECK(condition, message)                                                     \
    do                                                                                \
    {                                                                                 \
        g_checks++;                                                                   \
        if( !(condition) )                                                            \
        {                                                                             \
            g_failures++;                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));               \
        }                                                                             \
    } while( 0 )

#define FAKE_WIDGETS_MAX 8
#define FAKE_CHOICES_MAX 192

struct FakeWidget
{
    int kind;
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char label[64];
    char text[FAKE_CHOICES_MAX];
    char choices[FAKE_CHOICES_MAX];
    int selected;
};

static struct
{
    ToriRS_PluginHandler handlers[TORIRS_PLUGIN_EV_COUNT];
    struct FakeWidget widgets[FAKE_WIDGETS_MAX];
    int widget_count;
    int clear_count;
    int log_count;
    int select_count;
    int select_accept;
    char selected_id[TORIRS_PLUGIN_FRAME_ID_MAX];
    struct ToriRS_PluginFrameInfo offers[5];
    int offer_count;
    struct ToriRS_PluginFrameSelection selection;
} g_fake;

static int g_ctx_storage;

static struct ToriRS_PluginCtx*
fake_ctx(void)
{
    return (struct ToriRS_PluginCtx*)(void*)&g_ctx_storage;
}

static void
fake_subscribe(
    struct ToriRS_PluginCtx* ctx,
    enum ToriRS_PluginEvent event,
    ToriRS_PluginHandler handler,
    void* userdata)
{
    (void)ctx;
    (void)userdata;
    assert(event >= 0 && event < TORIRS_PLUGIN_EV_COUNT);
    g_fake.handlers[event] = handler;
}

static void
fake_log(struct ToriRS_PluginCtx* ctx, char const* fmt, ...)
{
    (void)ctx;
    (void)fmt;
    g_fake.log_count++;
}

static struct FakeWidget*
fake_widget_find(char const* id)
{
    for( int i = 0; i < g_fake.widget_count; i++ )
        if( strcmp(g_fake.widgets[i].id, id) == 0 )
            return &g_fake.widgets[i];
    return NULL;
}

static bool
fake_win_request(struct ToriRS_PluginCtx* ctx, char const* title)
{
    (void)ctx;
    CHECK(strcmp(title, "Client Settings") == 0, "the tab keeps its public title");
    return true;
}

static bool
fake_win_widget(
    struct ToriRS_PluginCtx* ctx,
    int kind,
    char const* id,
    char const* label)
{
    struct FakeWidget* widget;

    (void)ctx;
    widget = fake_widget_find(id);
    if( widget )
        return true;
    if( g_fake.widget_count >= FAKE_WIDGETS_MAX )
        return false;
    widget = &g_fake.widgets[g_fake.widget_count++];
    memset(widget, 0, sizeof(*widget));
    widget->kind = kind;
    widget->selected = -1;
    snprintf(widget->id, sizeof(widget->id), "%s", id);
    snprintf(widget->label, sizeof(widget->label), "%s", label ? label : "");
    return true;
}

static bool
fake_win_set_text(
    struct ToriRS_PluginCtx* ctx,
    char const* id,
    char const* text)
{
    struct FakeWidget* const widget = fake_widget_find(id);
    (void)ctx;
    if( !widget )
        return false;
    snprintf(widget->text, sizeof(widget->text), "%s", text ? text : "");
    return true;
}

static bool
fake_win_set_options(
    struct ToriRS_PluginCtx* ctx,
    char const* id,
    char const* choices,
    int selected)
{
    struct FakeWidget* const widget = fake_widget_find(id);
    (void)ctx;
    if( !widget )
        return false;
    snprintf(widget->choices, sizeof(widget->choices), "%s", choices ? choices : "");
    widget->selected = selected;
    return true;
}

static void
fake_win_clear(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    memset(g_fake.widgets, 0, sizeof(g_fake.widgets));
    g_fake.widget_count = 0;
    g_fake.clear_count++;
}

static int
fake_display_setting(
    struct ToriRS_PluginCtx* ctx,
    int setting,
    int* out_value,
    int* out_min,
    int* out_max)
{
    (void)ctx;
    (void)setting;
    (void)out_value;
    (void)out_min;
    (void)out_max;
    return 0;
}

static int
fake_display_setting_set(
    struct ToriRS_PluginCtx* ctx,
    int setting,
    int value)
{
    (void)ctx;
    (void)setting;
    (void)value;
    return 0;
}

static int
fake_frame_offer_next(
    struct ToriRS_PluginCtx* ctx,
    int iter,
    struct ToriRS_PluginFrameInfo* out)
{
    int const next = iter + 1;
    (void)ctx;
    if( next < 0 || next >= g_fake.offer_count )
        return -1;
    *out = g_fake.offers[next];
    return next;
}

static void
fake_frame_selection(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginFrameSelection* out)
{
    (void)ctx;
    *out = g_fake.selection;
}

static int
fake_frame_select(struct ToriRS_PluginCtx* ctx, char const* id)
{
    (void)ctx;
    g_fake.select_count++;
    snprintf(g_fake.selected_id, sizeof(g_fake.selected_id), "%s", id);
    if( !g_fake.select_accept )
        return 0;
    snprintf(
        g_fake.selection.requested,
        sizeof(g_fake.selection.requested),
        "%s",
        id);
    return 1;
}

static void
fake_offer(
    int row,
    char const* id,
    char const* title,
    char const* provider)
{
    struct ToriRS_PluginFrameInfo* const offer = &g_fake.offers[row];

    memset(offer, 0, sizeof(*offer));
    snprintf(offer->id, sizeof(offer->id), "%s", id);
    snprintf(offer->title, sizeof(offer->title), "%s", title);
    snprintf(offer->provider, sizeof(offer->provider), "%s", provider);
    offer->available = 1;
}

static void
fake_build(void)
{
    struct ToriRS_PluginEvUi ev;

    memset(&ev, 0, sizeof(ev));
    CHECK(g_fake.handlers[TORIRS_PLUGIN_EV_UI_BUILD] != NULL, "UI build is subscribed");
    g_fake.handlers[TORIRS_PLUGIN_EV_UI_BUILD](fake_ctx(), &ev, NULL);
}

static void
fake_pick(int row, char const* text)
{
    struct ToriRS_PluginEvUi ev;

    memset(&ev, 0, sizeof(ev));
    ev.widget_id = "gameframe";
    ev.action = TORIRS_PLUGIN_UI_PICK;
    ev.value = row;
    ev.text = text;
    g_fake.handlers[TORIRS_PLUGIN_EV_UI](fake_ctx(), &ev, NULL);
}

static void
fake_frame_start(void)
{
    struct ToriRS_PluginEvFrame ev;

    memset(&ev, 0, sizeof(ev));
    CHECK(g_fake.handlers[TORIRS_PLUGIN_EV_FRAME_START] != NULL, "frame start is subscribed");
    g_fake.handlers[TORIRS_PLUGIN_EV_FRAME_START](fake_ctx(), &ev, NULL);
}

int
main(void)
{
    struct ToriRS_PluginApi api;
    struct FakeWidget* widget;
    int clears;
    int selects;

    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.select_accept = 1;

    /* A core row from a compatibility host must not become a second Auto. */
    fake_offer(0, "core/native", "Native", "core");
    /* The pipe is hostile to the compatibility transport, not a new row. */
    fake_offer(1, "gameframe-layout/classic-fixed", "Classic|Fixed", "gameframe-layout");
    fake_offer(
        2,
        "gameframe-layout/modern-resizable",
        "Modern Resizable",
        "gameframe-layout");
    fake_offer(3, "mobile-gameframe/stone-drawer", "Stone Drawer", "mobile-gameframe");
    g_fake.offer_count = 4;

    snprintf(
        g_fake.selection.requested,
        sizeof(g_fake.selection.requested),
        "%s",
        "gameframe-layout/modern-resizable");
    snprintf(
        g_fake.selection.active,
        sizeof(g_fake.selection.active),
        "%s",
        "gameframe-layout/modern-resizable");
    g_fake.selection.status = TORIRS_PLUGIN_FRAME_ACTIVE;
    g_fake.selection.revision = 7;

    memset(&api, 0, sizeof(api));
    api.subscribe = fake_subscribe;
    api.log = fake_log;
    api.win_request = fake_win_request;
    api.win_widget = fake_win_widget;
    api.win_set_text = fake_win_set_text;
    api.win_set_options = fake_win_set_options;
    api.win_clear = fake_win_clear;
    api.display_setting = fake_display_setting;
    api.display_setting_set = fake_display_setting_set;
    api.frame_offer_next = fake_frame_offer_next;
    api.frame_selection = fake_frame_selection;
    api.frame_select = fake_frame_select;

    TORIRS_PLUGIN_CLIENT_SETTINGS.init(fake_ctx(), &api);
    CHECK(g_fake.handlers[TORIRS_PLUGIN_EV_UI] != NULL, "UI actions are subscribed");
    fake_build();

    widget = fake_widget_find("gameframe");
    CHECK(widget != NULL, "the page has one Gameframe dropdown");
    CHECK(
        widget && strcmp(widget->choices, "Auto|Classic/Fixed|Modern Resizable|Stone Drawer") == 0,
        "Auto and dynamic plugin offers are shown, with delimiter-safe titles");
    CHECK(widget && widget->selected == 2, "the requested stable id selects its current row");
    widget = fake_widget_find("gameframe_detail");
    CHECK(widget && strcmp(widget->text, "Active: Modern Resizable.") == 0,
        "the active frame is described by its human title");

    /* `text` is intentionally wrong: only the row's canonical-id map counts. */
    fake_pick(1, "Stone Drawer");
    CHECK(g_fake.select_count == 1, "a valid row makes one selection request");
    CHECK(strcmp(g_fake.selected_id, "gameframe-layout/classic-fixed") == 0,
        "the selected row resolves to its canonical id, not its visible text");
    widget = fake_widget_find("gameframe");
    CHECK(widget && widget->selected == 1, "the accepted request is selected after rebuild");

    selects = g_fake.select_count;
    fake_pick(99, "forged");
    CHECK(g_fake.select_count == selects, "an out-of-range row cannot select arbitrary state");

    snprintf(
        g_fake.selection.requested,
        sizeof(g_fake.selection.requested),
        "%s",
        "mobile-gameframe/stone-drawer");
    snprintf(
        g_fake.selection.active,
        sizeof(g_fake.selection.active),
        "%s",
        "core/native");
    g_fake.selection.status = TORIRS_PLUGIN_FRAME_LOADING;
    snprintf(
        g_fake.selection.reason,
        sizeof(g_fake.selection.reason),
        "%s",
        "Starting the requested gameframe provider.");
    g_fake.selection.revision++;
    clears = g_fake.clear_count;
    fake_frame_start();
    CHECK(g_fake.clear_count == clears + 1, "a resolver revision refreshes an open page");
    widget = fake_widget_find("gameframe_detail");
    CHECK(widget && strstr(widget->text, "Loading Stone Drawer.") != NULL,
        "loading state names the requested frame");
    CHECK(widget && strstr(widget->text, "Active for now: Native gameframe.") != NULL,
        "loading state names the currently active frame");
    CHECK(widget && strstr(widget->text, g_fake.selection.reason) != NULL,
        "loading state includes the resolver's reason");

    clears = g_fake.clear_count;
    fake_frame_start();
    CHECK(g_fake.clear_count == clears, "an unchanged selection does not rebuild every frame");

    snprintf(
        g_fake.selection.requested,
        sizeof(g_fake.selection.requested),
        "%s",
        "gameframe-layout/classic-fixed");
    g_fake.selection.status = TORIRS_PLUGIN_FRAME_FALLBACK;
    snprintf(
        g_fake.selection.reason,
        sizeof(g_fake.selection.reason),
        "%s",
        "The requested gameframe is unavailable on this lane.");
    g_fake.selection.revision++;
    fake_frame_start();
    widget = fake_widget_find("gameframe_detail");
    CHECK(widget && strstr(widget->text, "Could not use Classic|Fixed.") != NULL,
        "fallback state names the requested frame without changing its stored title");
    CHECK(widget && strstr(widget->text, "Active fallback: Native gameframe.") != NULL,
        "fallback state names the safe active frame");
    CHECK(widget && strstr(widget->text, g_fake.selection.reason) != NULL,
        "fallback state includes the resolver's reason");

    /* An unavailable saved id remains the selected row. Showing Auto here
     * would imply the fallback rewrote the user's preference when it did not. */
    snprintf(
        g_fake.selection.requested,
        sizeof(g_fake.selection.requested),
        "%s",
        "removed-provider/favourite");
    g_fake.selection.revision++;
    fake_frame_start();
    widget = fake_widget_find("gameframe");
    CHECK(
        widget && strstr(widget->choices, "Unavailable: removed-provider/favourite") != NULL,
        "a missing saved frame stays visible in the dropdown");
    CHECK(widget && widget->selected == 4, "the missing saved frame remains selected");
    fake_pick(4, "forged display value");
    CHECK(
        strcmp(g_fake.selected_id, "removed-provider/favourite") == 0,
        "the unavailable row still carries the exact stable id");

    printf("client_settings_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
