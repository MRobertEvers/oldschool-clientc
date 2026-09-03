#include "plugin/torirs_plugin_v2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client-owned display and gameframe settings. These are live controls over
 * the application's authoritative stores, never a second plugin config. */

#define CS_ID_FRAME "gameframe"
#define CS_ID_FRAME_DETAIL "gameframe_detail"
#define CS_ID_SCALE "ui_scale"
#define CS_ID_FILTER "ui_scale_filter"
#define CS_FRAME_ROWS_MAX 33

struct CsFrameRow
{
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
    char label[TORIRS_UI_LABEL_MAX];
    char detail[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_SelectOption option;
};

struct ClientSettingsState
{
    struct CsFrameRow frame_rows[CS_FRAME_ROWS_MAX];
    int frame_row_count;
    uint32_t frame_seen_revision;
    char frame_seen_requested[TORIRS_PLUGIN_FRAME_ID_MAX];
    bool page_built;
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

static char const*
cs_frame_title(struct ClientSettingsState const* state, char const* id)
{
    if( !id || !id[0] ) return "Unknown gameframe";
    if( strcmp(id, "auto") == 0 ) return "Auto";
    if( strcmp(id, "core/native") == 0 ) return "Native gameframe";
    for( int i = 0; i < state->frame_row_count; i++ )
        if( strcmp(state->frame_rows[i].id, id) == 0 )
            return state->frame_rows[i].title;
    return id;
}

static void
cs_frame_row(
    struct ClientSettingsState* state,
    char const* id,
    char const* title,
    bool enabled,
    char const* detail)
{
    struct CsFrameRow* row;
    if( state->frame_row_count >= CS_FRAME_ROWS_MAX ) return;
    row = &state->frame_rows[state->frame_row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->id, sizeof(row->id), "%s", id ? id : "");
    snprintf(row->title, sizeof(row->title), "%s", title && title[0] ? title : row->id);
    snprintf(row->label, sizeof(row->label), "%s", row->title);
    snprintf(row->detail, sizeof(row->detail), "%s", detail ? detail : "");
    row->option.struct_size = sizeof(row->option);
    row->option.value = row->id;
    row->option.label = row->label;
    row->option.enabled = enabled;
    row->option.detail = row->detail;
}

static void
cs_frame_choices(
    struct ToriRS_ApiV2* api,
    struct ClientSettingsState* state,
    struct ToriRS_FrameSelection const* selection)
{
    struct ToriRS_FrameOfferInfo info = { .struct_size = sizeof(info) };
    bool selected_present = strcmp(selection->requested_id, "auto") == 0;
    int iter = -1;

    memset(state->frame_rows, 0, sizeof(state->frame_rows));
    state->frame_row_count = 0;
    cs_frame_row(state, "auto", "Auto", true, "Follow this lane's native gameframe");
    while( (iter = api->frame.offer_next(api, iter, &info)) >= 0 )
    {
        if( !info.id[0] || strcmp(info.id, "core/native") == 0 )
            continue;
        cs_frame_row(
            state,
            info.id,
            info.title[0] ? info.title : info.id,
            info.available,
            info.detail);
        if( strcmp(selection->requested_id, info.id) == 0 )
            selected_present = true;
        info.struct_size = sizeof(info);
    }
    if( !selected_present && selection->requested_id[0] )
    {
        char label[TORIRS_UI_LABEL_MAX];
        snprintf(label, sizeof(label), "Unavailable: %s", selection->requested_id);
        cs_frame_row(
            state,
            selection->requested_id,
            label,
            true,
            "Provider is not currently available");
    }
}

static void
cs_frame_detail(
    struct ClientSettingsState const* state,
    struct ToriRS_FrameSelection const* selection,
    char* out,
    size_t out_size)
{
    char const* requested = cs_frame_title(state, selection->requested_id);
    char const* active = cs_frame_title(state, selection->active_id);
    if( selection->status == TORIRS_FRAME_STATUS_NATIVE &&
        strcmp(selection->requested_id, "auto") == 0 )
        snprintf(out, out_size, "Active: %s. Auto follows this lane.", active);
    else if( selection->status == TORIRS_FRAME_STATUS_ACTIVE &&
             strcmp(selection->requested_id, selection->active_id) == 0 )
        snprintf(out, out_size, "Active: %s.", active);
    else if( selection->status == TORIRS_FRAME_STATUS_LOADING )
        snprintf(out, out_size, "Loading %s. Active for now: %s.%s%s",
            requested, active, selection->reason[0] ? " " : "", selection->reason);
    else if( selection->status == TORIRS_FRAME_STATUS_FALLBACK )
        snprintf(out, out_size, "Could not use %s. Active fallback: %s.%s%s",
            requested, active, selection->reason[0] ? " " : "", selection->reason);
    else
        snprintf(out, out_size, "Switching to %s. Active for now: %s.", requested, active);
}

static void
cs_remember(
    struct ClientSettingsState* state,
    struct ToriRS_FrameSelection const* selection)
{
    state->frame_seen_revision = selection->revision;
    snprintf(state->frame_seen_requested, sizeof(state->frame_seen_requested),
        "%s", selection->requested_id);
}

/* The frame catalogue and status are retained properties of two existing
 * rows. Updating them must not rebuild the whole page: the host journals these
 * two row mutations and the browser executor consumes only those entries. */
static void
cs_publish_frame(
    struct ToriRS_ApiV2* api,
    struct ClientSettingsState* state,
    struct ToriRS_FrameSelection const* selection)
{
    struct ToriRS_SelectOption options[CS_FRAME_ROWS_MAX];
    char detail[192];
    enum ToriRS_Result options_result;
    enum ToriRS_Result detail_result;

    cs_frame_choices(api, state, selection);
    cs_frame_detail(state, selection, detail, sizeof(detail));
    cs_remember(state, selection);
    if( !state->page_built ) return;
    for( int i = 0; i < state->frame_row_count; i++ )
        options[i] = state->frame_rows[i].option;
    options_result = api->panel.set_options(
        api, CS_ID_FRAME, selection->requested_id, options, state->frame_row_count);
    detail_result = api->panel.set_text(api, CS_ID_FRAME_DETAIL, detail);
    if( options_result != TORIRS_RESULT_OK || detail_result != TORIRS_RESULT_OK )
        api->panel.invalidate(api);
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
cs_on_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct ClientSettingsState* state = state_ptr;
    struct ToriRS_PanelDescriptor panel = { NULL, TORIRS_PANEL_WIDTH_DEFAULT };
    assert(api);
    assert(state);
    assert(api->client);
    (void)state;
    (void)api->panel.request(api, &panel);
}

static void
cs_on_ui_build(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct ClientSettingsState* state = state_ptr;
    struct ToriRS_FrameSelection frame = { .struct_size = sizeof(frame) };
    struct ToriRS_SelectOption frame_options[CS_FRAME_ROWS_MAX];
    struct ToriRS_SelectOption scale_options[13];
    struct ToriRS_SelectOption filter_options[3];
    char detail[192];
    int value = 0, min = 0, max = 0;

    (void)view;
    assert(api);
    assert(state);
    assert(panel);
    assert(api->client);
    state->page_built = true;
    api->frame.selection(api, &frame);
    cs_frame_choices(api, state, &frame);
    for( int i = 0; i < state->frame_row_count; i++ )
        frame_options[i] = state->frame_rows[i].option;
    panel->select(panel, CS_ID_FRAME, "Gameframe", frame.requested_id,
        frame_options, state->frame_row_count);
    cs_frame_detail(state, &frame, detail, sizeof(detail));
    panel->label(panel, CS_ID_FRAME_DETAIL, detail);
    cs_remember(state, &frame);

    if( api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE, &value, &min, &max) )
    {
        int const row = cs_nearest_row(value, min, 25, 13);
        (void)max;
        cs_static_options(scale_options, CS_SCALE_VALUE, CS_SCALE_LABEL, 13);
        panel->select(panel, CS_ID_SCALE, "Interface scaling",
            CS_SCALE_VALUE[row], scale_options, 13);
    }
    if( api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, &value, &min, &max) )
    {
        int const row = cs_nearest_row(value, min, 1, 3);
        (void)max;
        cs_static_options(filter_options, CS_FILTER_VALUE, CS_FILTER_LABEL, 3);
        panel->select(panel, CS_ID_FILTER, "Scaling filter",
            CS_FILTER_VALUE[row], filter_options, 3);
    }
    panel->label(panel, "note",
        "Scaling draws the whole canvas larger, the 3D scene included.");
}

static bool
cs_frame_known(struct ClientSettingsState const* state, char const* id)
{
    for( int i = 0; i < state->frame_row_count; i++ )
        if( strcmp(state->frame_rows[i].id, id) == 0 ) return true;
    return false;
}

static void
cs_on_ui_action(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelActionEvent const* event)
{
    struct ClientSettingsState* state = state_ptr;
    int min = 0;

    assert(api);
    assert(state);
    assert(event);
    if( event->action != TORIRS_PANEL_ACTION_PICK || !event->id || !event->text ) return;
    if( strcmp(event->id, CS_ID_FRAME) == 0 )
    {
        struct ToriRS_FrameSelection selection = { .struct_size = sizeof(selection) };
        if( !cs_frame_known(state, event->text) )
        {
            api->core.log(api, "client-settings: ignored unknown gameframe '%s'", event->text);
            api->frame.selection(api, &selection);
            cs_publish_frame(api, state, &selection);
            return;
        }
        if( api->frame.select(api, event->text) != TORIRS_RESULT_OK )
            api->core.log(api, "client-settings: could not save gameframe '%s'", event->text);
        api->frame.selection(api, &selection);
        cs_publish_frame(api, state, &selection);
        return;
    }
    if( strcmp(event->id, CS_ID_SCALE) == 0 )
    {
        if( api->client->display_get(
                api, TORIRS_DISPLAY_UI_SCALE, NULL, &min, NULL) )
            (void)api->client->display_set(
                api, TORIRS_DISPLAY_UI_SCALE, atoi(event->text));
        return;
    }
    if( strcmp(event->id, CS_ID_FILTER) == 0 &&
        api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, NULL, &min, NULL) )
        (void)api->client->display_set(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, min + atoi(event->text));
}

static void
cs_on_frame_start(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct ClientSettingsState* state = state_ptr;
    struct ToriRS_FrameSelection selection = { .struct_size = sizeof(selection) };
    (void)event;
    api->frame.selection(api, &selection);
    if( selection.revision == state->frame_seen_revision &&
        strcmp(selection.requested_id, state->frame_seen_requested) == 0 )
        return;
    cs_publish_frame(api, state, &selection);
}

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_CLIENT_SETTINGS = {
    .struct_size = sizeof(TORIRS_PLUGIN_CLIENT_SETTINGS),
    .id = "client-settings",
    .title = "Client Settings",
    .version = "2.0.0",
    .state_size = sizeof(struct ClientSettingsState),
    .flags = TORIRS_PLUGIN_V2_ESSENTIAL,
    .event_priority = 999,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = cs_on_start,
        .on_frame_start = cs_on_frame_start,
        .on_ui_build = cs_on_ui_build,
        .on_ui_action = cs_on_ui_action,
    },
};
