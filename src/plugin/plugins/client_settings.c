/*
 * The client's own display knobs, as a Porcelain page.
 *
 * What this plugin did by hand and no longer does: it held a `page_built`
 * flag, and every frame the frame resolver moved it pushed the gameframe
 * catalogue and the status line itself -- one set_options, one set_text, and
 * `if either was refused, invalidate the whole page`. That refusal was the
 * common case, not the rare one: the catalogue GROWS and SHRINKS in normal
 * use (a provider becomes available, the saved-but-unavailable row appears or
 * goes), and the host refused a set_options whose option COUNT differed. So
 * the ordinary event -- plugging in a gameframe provider -- rebuilt the page:
 * a DOM remove/add per row on the browser executor, and a PanelClearWidgets
 * on the buffer one that resets scroll_y and retires every retained run.
 *
 * Under Porcelain there is one description and no publish path. The live
 * selection is an INPUT to that description; the reconciler diffs the rows it
 * produces; and a changed catalogue -- count included, now that host fix H3
 * has landed -- is one setter on the row that changed. The frame resolver is
 * not one of Porcelain's six inputs, so on_frame_start still compares
 * (revision, requested id) against the last seen pair and says so; that
 * comparison is all an unchanged frame costs.
 *
 * Two things the row model could not do, both reported rather than worked
 * around silently:
 *
 *  - Porcelain's option strings stop at 96 bytes and a frame id may be 127,
 *    so an id the host would have shown is REFUSED here. A refusal poisons
 *    the whole describe run, which would blank the settings page over one
 *    long id, so the catalogue drops that one row instead and the limitation
 *    is declared with Porcelain_ExpectUnsupported.
 *
 *  - The reconciler's mirror of the declaration is advanced only by
 *    Porcelain's own setters, and the host commits a PICK to its widget model
 *    BEFORE dispatching it. So when this plugin refuses a pick the host
 *    accepted, the described row is unchanged, no setter fires, and the
 *    control keeps the value nobody could honour. There is no verb for
 *    "restate this row", so the two refusal arms restate it through
 *    api->panel.set_options directly -- with exactly the values Porcelain's
 *    mirror already holds, which is what keeps the mirror true.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

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
#define CS_SCALE_ROWS 13
#define CS_FILTER_ROWS 3

/*
 * Every string this page puts in a row option fits, by construction.
 *
 * Asserted here rather than tested per row, because it is a question about
 * two constants with one answer for the life of the build. It used to be
 * false -- the layer's option strings stopped at 96 while the host accepted
 * 192 -- and the way it was false was a runtime drop plus a declared
 * limitation, both of which went stale the moment the ceilings were fixed.
 * Narrow one of these again and this stops the build instead.
 */
_Static_assert(TORIRS_PLUGIN_FRAME_ID_MAX <= PORCELAIN_OPTION_VALUE_MAX,
               "a frame id must fit a row option's stable value");
_Static_assert(TORIRS_UI_LABEL_MAX <= PORCELAIN_OPTION_LABEL_MAX,
               "a frame title must fit a row option's label");
_Static_assert(TORIRS_FRAME_REASON_MAX <= PORCELAIN_OPTION_DETAIL_MAX,
               "a frame status reason must fit a row option's detail");

/*
 * A row's NAME is either a provider's own title or, for a provider that is
 * not there to be asked, the bare id the reader saved. So it is sized for
 * whichever of those two is longer -- a name truncated to the shorter one
 * would name a different frame in the status sentence below.
 */
#define CS_FRAME_NAME_MAX                                                      \
    (TORIRS_PLUGIN_FRAME_ID_MAX > TORIRS_PLUGIN_TITLE_MAX                      \
         ? TORIRS_PLUGIN_FRAME_ID_MAX                                          \
         : TORIRS_PLUGIN_TITLE_MAX)

struct CsFrameRow
{
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char title[CS_FRAME_NAME_MAX];
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
    /* The status line, held rather than built on the stack: PorcelainRow
     * borrows its strings for the length of the Porcelain_Row call. */
    char detail[PORCELAIN_ROW_TEXT_MAX];
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

/*
 * One catalogue row, or a logged refusal.
 *
 * The host would take a 127-byte frame id and a 191-byte label; Porcelain
 * stops both at 96 and REFUSES what does not fit rather than truncating it,
 * because a truncated stable value names a different option. A refusal inside
 * Porcelain_Row poisons the whole run and leaves the page as it was, so an id
 * this long would cost the reader every other setting on the page. Dropping
 * the one row it names is the smaller loss, and it is said out loud.
 *
 * The detail is a subtitle and not an identity: an over-long one costs its
 * own string and not the row.
 */
static void
cs_frame_row(
    struct ToriRS_Api* api,
    struct ClientSettingsState* state,
    char const* id,
    char const* title,
    char const* label,
    bool enabled,
    char const* detail)
{
    struct CsFrameRow* row;
    assert(api);
    assert(state);
    if( state->frame_row_count >= CS_FRAME_ROWS_MAX ) return;
    row = &state->frame_rows[state->frame_row_count];
    memset(row, 0, sizeof(*row));
    snprintf(row->id, sizeof(row->id), "%s", id ? id : "");
    /*
     * The NAME and the LABEL are two different strings and the row keeps
     * both. The name is what the frame is called, and it is what the status
     * sentence puts in "Could not use %s." The label is what the control
     * shows, which may say something ABOUT the frame as well as name it
     * ("Unavailable: <id>"). They were one field, so the sentence read
     * "Could not use Unavailable: <id>" -- a prefix that already means could
     * not use, with the frame's own name nowhere in it.
     */
    snprintf(row->title, sizeof(row->title), "%s", title && title[0] ? title : row->id);
    snprintf(row->label, sizeof(row->label), "%s", label && label[0] ? label : row->title);
    snprintf(row->detail, sizeof(row->detail), "%s", detail ? detail : "");
    /*
     * No length test here any more, and the static assertions above are why.
     *
     * The row model's option strings used to stop at 96 while the host
     * accepted 192, so a provider id of the frame API's own maximum was
     * refused -- by the LIBRARY, about itself, with nothing about this lane to
     * justify it -- and the refusal poisoned the whole describe, so one long
     * id blanked the settings page. The ceilings agree now and every string
     * that can reach this function is narrower than both.
     *
     * A compile-time check and not a runtime one, because that is what the
     * question actually is: it is about two constants, it has one answer for
     * the life of the build, and a runtime guard for it could only ever be a
     * branch that never runs behind a declaration that is no longer true.
     */
    row->option.struct_size = sizeof(row->option);
    row->option.value = row->id;
    row->option.label = row->label;
    row->option.enabled = enabled;
    row->option.detail = row->detail;
    state->frame_row_count++;
}

static void
cs_frame_choices(
    struct ToriRS_Api* api,
    struct ClientSettingsState* state,
    struct ToriRS_FrameSelection const* selection)
{
    struct ToriRS_FrameOfferInfo info = { .struct_size = sizeof(info) };
    bool selected_present = strcmp(selection->requested_id, "auto") == 0;
    int iter = -1;

    memset(state->frame_rows, 0, sizeof(state->frame_rows));
    state->frame_row_count = 0;
    cs_frame_row(
        api, state, "auto", "Auto", "Auto", true, "Follow this lane's native gameframe");
    while( (iter = api->frame.offer_next(api, iter, &info)) >= 0 )
    {
        if( !info.id[0] || strcmp(info.id, "core/native") == 0 )
            continue;
        cs_frame_row(
            api,
            state,
            info.id,
            info.title[0] ? info.title : info.id,
            info.title[0] ? info.title : info.id,
            info.available,
            info.detail);
        if( strcmp(selection->requested_id, info.id) == 0 )
            selected_present = true;
        info.struct_size = sizeof(info);
    }
    /* The lane's own gameframe is filtered out of the offers above because
     * Auto already means it -- but a saved choice of exactly "core/native"
     * (rs289lc saves it) is a real, available frame, not a missing provider.
     * Found reading "Unavailable: core/native" in the rs289lc panel capture. */
    if( !selected_present && strcmp(selection->requested_id, "core/native") == 0 )
    {
        cs_frame_row(api, state, "core/native", "Native gameframe", "Native gameframe",
            true, "This lane's own gameframe");
        selected_present = true;
    }
    if( !selected_present && selection->requested_id[0] )
    {
        char label[TORIRS_UI_LABEL_MAX];
        snprintf(label, sizeof(label), "Unavailable: %s", selection->requested_id);
        /* The id is the only name a missing provider has: nothing is left in
         * the build to ask for a title. It is the NAME; the "Unavailable:"
         * prefix belongs to the control's label alone. */
        cs_frame_row(
            api,
            state,
            selection->requested_id,
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
    else if( selection->status == TORIRS_FRAME_STATUS_LOADING )
        snprintf(out, out_size, "Loading %s. Active for now: %s.%s%s",
            requested, active, selection->reason[0] ? " " : "", selection->reason);
    /*
     * The IDS decide this sentence, not the status.
     *
     * active_id is the frame that is drawing; status is what the resolver
     * called the transition that got there, and the two can disagree. A saved
     * request for the native frame's OWN id -- "core/native", which rs289lc
     * writes -- resolves to FALLBACK, because that id names no catalogue
     * provider, and carries the reason "The requested gameframe is not
     * installed in this build". Reading the status first made the panel say
     * "Could not use Native gameframe. Active fallback: Native gameframe."
     * over a fully drawn rs289 native frame: it called the frame rendering
     * the screen a failure and then named it as its own fallback.
     *
     * When the active id IS the requested id there is no gap between what was
     * asked for and what is up, and every phrasing that describes a gap --
     * "could not use", "switching to", and the reason string, which exists to
     * explain a gap and here describes a catalogue lookup rather than
     * anything on screen -- is false. So this arm covers NATIVE and ACTIVE as
     * it always did, and FALLBACK and any status the host adds as well.
     */
    else if( strcmp(selection->requested_id, selection->active_id) == 0 )
        snprintf(out, out_size, "Active: %s.", active);
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

static bool
cs_frame_known(struct ClientSettingsState const* state, char const* id)
{
    for( int i = 0; i < state->frame_row_count; i++ )
        if( strcmp(state->frame_rows[i].id, id) == 0 ) return true;
    return false;
}

/*
 * A gameframe pick.
 *
 * It is refused twice -- once by the host's own fence, which requires the
 * text to equal an option it declared, and once here against the catalogue
 * this plugin described. The second refusal is not redundant: the host fences
 * against what it was LAST DECLARED, and a pick queued across a rebuild can
 * name a row this description no longer offers.
 */
static void
cs_pick_frame(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct ClientSettingsState* state = user;

    assert(api);
    assert(state);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    /*
     * A refusal restates the row, and that is a LAYER verb now.
     *
     * The host commits a PICK to its widget model before it dispatches, so by
     * the time this plugin says no the control already shows the value it
     * asked for -- while this plugin's description has not moved, because
     * nothing it describes from has. There used to be no verb for that, so the
     * restatement reached around the layer to api->panel.set_options with
     * exactly the catalogue and selection the mirror already held: shipped
     * code going around the library on purpose, with a comment saying so.
     *
     * Porcelain_Restate says the same thing to the reconciler that was meant
     * to be doing it, and costs the same one call.
     */
    if( !cs_frame_known(state, action->text) )
    {
        api->core.log(api, "client-settings: ignored unknown gameframe '%s'", action->text);
        Porcelain_Restate(state->porcelain, CS_ID_FRAME);
        return;
    }
    if( api->frame.select(api, action->text) != TORIRS_RESULT_OK )
    {
        api->core.log(api, "client-settings: could not save gameframe '%s'", action->text);
        Porcelain_Restate(state->porcelain, CS_ID_FRAME);
        return;
    }
    /* The resolver IS the description's input, and it has just moved. Saying
     * so here rather than waiting for the next on_frame_start is what makes
     * the row and its status line change on the fence the click landed in. */
    Porcelain_Note(state->porcelain, PORCELAIN_INPUT_EXPLICIT);
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

/*
 * The page, described.
 *
 * The catalogue and the status line are re-derived on every run, so the row
 * SET is whatever the resolver currently offers -- and a different set is a
 * different KEY SEQUENCE, which is the reconciler's one legitimate rebuild.
 * Everything a run can change short of that -- an offer that came or went,
 * the selection, the sentence under it -- is a property of a row the page
 * already has.
 *
 * `view` is not read. The page and the settings face show the same rows,
 * because these ARE the settings; PORCELAIN_FACE_BOTH is where that is said.
 */
static void
cs_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct ClientSettingsState* state = user;
    struct ToriRS_Api* api = state->api;
    struct ToriRS_FrameSelection frame = { .struct_size = sizeof(frame) };
    struct ToriRS_SelectOption frame_options[CS_FRAME_ROWS_MAX];
    struct ToriRS_SelectOption scale_options[CS_SCALE_ROWS];
    struct ToriRS_SelectOption filter_options[CS_FILTER_ROWS];
    struct PorcelainRow row;
    int value = 0, min = 0, max = 0;

    assert(describe);
    assert(state);
    assert(api);
    assert(api->client);
    api->frame.selection(api, &frame);
    cs_frame_choices(api, state, &frame);
    cs_frame_detail(state, &frame, state->detail, sizeof(state->detail));
    cs_remember(state, &frame);
    for( int i = 0; i < state->frame_row_count; i++ )
        frame_options[i] = state->frame_rows[i].option;

    memset(&row, 0, sizeof(row));
    row.key = CS_ID_FRAME;
    row.kind = PORCELAIN_ROW_SELECT;
    row.label = "Gameframe";
    row.text = frame.requested_id;
    row.options = frame_options;
    row.option_count = state->frame_row_count;
    row.on_action = cs_pick_frame;
    row.user = state;
    Porcelain_Row(describe, &row);

    memset(&row, 0, sizeof(row));
    row.key = CS_ID_FRAME_DETAIL;
    row.kind = PORCELAIN_ROW_LABEL;
    row.text = state->detail;
    Porcelain_Row(describe, &row);

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
    if( api->client->display_get(
            api, TORIRS_DISPLAY_UI_SCALE_FILTER, &value, &min, &max) )
    {
        int const at = cs_nearest_row(value, min, 1, CS_FILTER_ROWS);
        (void)max;
        cs_static_options(filter_options, CS_FILTER_VALUE, CS_FILTER_LABEL, CS_FILTER_ROWS);
        memset(&row, 0, sizeof(row));
        row.key = CS_ID_FILTER;
        row.kind = PORCELAIN_ROW_SELECT;
        row.label = "Scaling filter";
        row.text = CS_FILTER_VALUE[at];
        row.options = filter_options;
        row.option_count = CS_FILTER_ROWS;
        row.on_action = cs_pick_filter;
        row.user = state;
        Porcelain_Row(describe, &row);
    }

    memset(&row, 0, sizeof(row));
    row.key = "note";
    row.kind = PORCELAIN_ROW_LABEL;
    row.text = "Scaling draws the whole canvas larger, the 3D scene included.";
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
 * The resolver, watched.
 *
 * It is not one of Porcelain's six inputs -- nothing in the layer can see a
 * gameframe load finish -- so the plugin forwards it. An unchanged pair costs
 * this read and the fence's own hash compare, and nothing else.
 */
static void
cs_on_frame_start(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct ClientSettingsState* state = state_ptr;
    struct ToriRS_FrameSelection selection = { .struct_size = sizeof(selection) };
    (void)event;
    assert(api);
    assert(state);
    api->frame.selection(api, &selection);
    if( selection.revision != state->frame_seen_revision ||
        strcmp(selection.requested_id, state->frame_seen_requested) != 0 )
    {
        cs_remember(state, &selection);
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
