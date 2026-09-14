/*
 * The engine's published flag list, as a Porcelain page.
 *
 * What this plugin did by hand and no longer does: it declared its rows in
 * on_ui_build, and then kept them true from on_ui_action with a set_options
 * call that it had to follow with `if refused, invalidate the whole page`.
 * That refusal was not rare -- an INT flag whose stored value is not one of
 * the published choices carries a synthetic extra option, and picking away
 * from it REMOVES that option, so the common edit changed the option COUNT
 * and the host answered INVALID. One dropdown edit rebuilt the page.
 *
 * Under Porcelain there is one description and no publish path at all: the
 * stored value is an input, the reconciler diffs the rows it produces, and a
 * changed option list -- count included, now that host fix H3 has landed --
 * is one setter on the row that changed.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FF_DEFAULT_LABEL "Revision default"
#define FF_MAX 32
#define FF_OPTION_MAX (TORIRS_FEATURE_VALUES_MAX + 2)

struct FeatureFlagsState
{
    struct ToriRS_FeatureInfo flags[FF_MAX];
    int flag_count;
    /* The describe function is handed only its `user`, so the api travels
     * with the state rather than through a file-scope pointer. */
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
};

/* Named by ff_on_start, which opens the layer against this plugin's own
 * definition; the definition itself is at the foot of the file. */
extern struct ToriRS_PluginDef const TORIRS_FEATURE_FLAGS;

static bool
ff_choice_at(char const* choices, int index, char* out, size_t out_size)
{
    char const* at = choices;
    char const* end;
    size_t length;
    for( int i = 0; i < index; i++ )
    {
        at = strchr(at, '|');
        if( !at ) return false;
        at++;
    }
    end = strchr(at, '|');
    length = end ? (size_t)(end - at) : strlen(at);
    if( length >= out_size ) length = out_size - 1;
    memcpy(out, at, length);
    out[length] = '\0';
    return true;
}

static int
ff_enum_value(struct ToriRS_FeatureInfo const* flag, char const* text)
{
    for( int i = 0; i < flag->value_count; i++ )
    {
        char choice[TORIRS_FEATURE_CHOICES_MAX];
        if( !ff_choice_at(flag->choices, i, choice, sizeof(choice)) ) break;
        if( strcmp(choice, text) == 0 ) return flag->values[i];
    }
    return TORIRS_FEATURE_UNSET;
}

static int
ff_value_index(struct ToriRS_FeatureInfo const* flag, int value)
{
    for( int i = 0; i < flag->value_count; i++ )
        if( flag->values[i] == value ) return i;
    return -1;
}

static char const*
ff_stored(struct ToriRS_Api* api, struct ToriRS_FeatureInfo const* flag)
{
    char const* value = "";
    if( !api->config.has(api, flag->key) ||
        !api->config.get_string(api, flag->key, &value) )
        return "";
    return value ? value : "";
}

static bool
ff_is_default(char const* stored)
{
    return !stored[0] || strcmp(stored, FF_DEFAULT_LABEL) == 0;
}

static int
ff_stored_value(
    struct ToriRS_FeatureInfo const* flag,
    char const* stored)
{
    if( ff_is_default(stored) ) return TORIRS_FEATURE_UNSET;
    return flag->kind == TORIRS_FEATURE_ENUM
               ? ff_enum_value(flag, stored)
               : atoi(stored);
}

static void
ff_refresh(struct ToriRS_Api* api, struct FeatureFlagsState* state)
{
    struct ToriRS_FeatureInfo flag;
    int iter = -1;
    state->flag_count = 0;
    while( (iter = api->client->feature_next(api, iter, &flag)) >= 0 )
    {
        if( state->flag_count >= FF_MAX )
        {
            api->core.log(api, "feature-flags: engine publishes more than %d flags", FF_MAX);
            break;
        }
        state->flags[state->flag_count++] = flag;
    }
}

static void
ff_apply_all(struct ToriRS_Api* api, struct FeatureFlagsState* state)
{
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_FeatureInfo const* flag = &state->flags[i];
        char const* stored = ff_stored(api, flag);
        if( api->client->feature_set(
                api, flag->key, ff_stored_value(flag, stored)) != TORIRS_RESULT_OK )
            api->core.log(api, "feature-flags: engine refused %s=%s",
                flag->key, stored[0] ? stored : FF_DEFAULT_LABEL);
    }
}

static int
ff_options(
    struct ToriRS_Api* api,
    struct ToriRS_FeatureInfo const* flag,
    struct ToriRS_SelectOption* options,
    char values[FF_OPTION_MAX][32],
    char labels[FF_OPTION_MAX][TORIRS_FEATURE_CHOICES_MAX],
    char const** out_selected)
{
    char const* stored = ff_stored(api, flag);
    int const wanted = ff_stored_value(flag, stored);
    int count = 0;
    int effective = flag->value;
    int named;

    if( flag->is_default )
        (void)api->client->feature_get(api, flag->key, &effective);
    named = ff_value_index(flag, effective);
    snprintf(values[count], sizeof(values[count]), "%s", FF_DEFAULT_LABEL);
    if( flag->is_default && named >= 0 )
    {
        char choice[TORIRS_FEATURE_CHOICES_MAX];
        (void)ff_choice_at(flag->choices, named, choice, sizeof(choice));
        snprintf(labels[count], sizeof(labels[count]), "%s (%s)", FF_DEFAULT_LABEL, choice);
    }
    else if( flag->is_default && flag->kind == TORIRS_FEATURE_INT )
        snprintf(labels[count], sizeof(labels[count]), "%s (%d)", FF_DEFAULT_LABEL, effective);
    else
        snprintf(labels[count], sizeof(labels[count]), "%s", FF_DEFAULT_LABEL);
    options[count] = (struct ToriRS_SelectOption){
        .struct_size = sizeof(options[count]),
        .value = values[count], .label = labels[count], .enabled = true,
    };
    count++;

    for( int i = 0; i < flag->value_count && count < FF_OPTION_MAX; i++, count++ )
    {
        (void)ff_choice_at(flag->choices, i, labels[count], sizeof(labels[count]));
        if( flag->kind == TORIRS_FEATURE_ENUM )
            snprintf(values[count], sizeof(values[count]), "%s", labels[count]);
        else
            snprintf(values[count], sizeof(values[count]), "%d", flag->values[i]);
        options[count] = (struct ToriRS_SelectOption){
            .struct_size = sizeof(options[count]),
            .value = values[count], .label = labels[count], .enabled = true,
        };
    }
    if( wanted != TORIRS_FEATURE_UNSET &&
        ff_value_index(flag, wanted) < 0 &&
        flag->kind == TORIRS_FEATURE_INT && count < FF_OPTION_MAX )
    {
        snprintf(values[count], sizeof(values[count]), "%d", wanted);
        snprintf(labels[count], sizeof(labels[count]), "%d", wanted);
        options[count] = (struct ToriRS_SelectOption){
            .struct_size = sizeof(options[count]),
            .value = values[count], .label = labels[count], .enabled = true,
        };
        count++;
    }
    *out_selected = ff_is_default(stored) ? FF_DEFAULT_LABEL : stored;
    return count;
}

/*
 * A pick, routed by key.
 *
 * It refuses before it writes, exactly as it did: an enum value that is not
 * one of the flag's choices, and an int outside [min, max], store nothing and
 * push nothing. What is gone is the publish afterwards -- the stored value is
 * an input to the description, so saying it moved is the whole of the work.
 */
static void
ff_pick(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct FeatureFlagsState* state = user;

    if( action->kind != TORIRS_PANEL_ACTION_PICK )
        return;
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_FeatureInfo const* flag = &state->flags[i];
        char const* stored = action->text;
        int value;
        if( strcmp(flag->key, action->key) != 0 ) continue;
        if( flag->kind == TORIRS_FEATURE_ENUM &&
            !ff_is_default(stored) && ff_enum_value(flag, stored) == TORIRS_FEATURE_UNSET )
            return;
        value = ff_stored_value(flag, stored);
        if( flag->kind == TORIRS_FEATURE_INT && value != TORIRS_FEATURE_UNSET &&
            (value < flag->min || value > flag->max) )
            return;
        /* The store IS the description's input, and writing it raises this
         * plugin's own on_config_changed, which notes it. An invalidate here
         * as well would be a second statement of the same fact -- and one no
         * test could distinguish from the first. */
        (void)api->config.set(api, flag->key, stored);
        if( api->client->feature_set(api, flag->key, value) != TORIRS_RESULT_OK )
        {
            api->core.log(api, "feature-flags: engine refused %s=%s", flag->key, stored);
            (void)api->config.set(api, flag->key, FF_DEFAULT_LABEL);
            (void)api->client->feature_set(api, flag->key, TORIRS_FEATURE_UNSET);
        }
        return;
    }
}

/*
 * The page, described.
 *
 * The flag list is re-read on every run, so a build that publishes a
 * different set produces a different page rather than a stale one -- and a
 * different set is a different KEY SEQUENCE, which is the one thing the
 * reconciler answers with a rebuild. Everything else a run can change -- a
 * sentinel label that now names the effective value, a synthetic option that
 * came or went, the selection itself -- is a property of a row the page
 * already has.
 */
static void
ff_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FeatureFlagsState* state = user;
    struct ToriRS_Api* api = state->api;
    char section[TORIRS_FEATURE_KEY_MAX] = "";
    /* The section's ORDINAL and not its name: a row key has the host's
     * 32-byte ceiling, a section name has nearly all of it, and a key built
     * by prefixing one would be refused for a heading. The name is the
     * heading's text, where it belongs and where it is patched. */
    char section_key[16];
    int section_index = 0;
    struct PorcelainRow row;

    ff_refresh(api, state);
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_FeatureInfo const* flag = &state->flags[i];
        struct ToriRS_SelectOption options[FF_OPTION_MAX];
        char values[FF_OPTION_MAX][32];
        char labels[FF_OPTION_MAX][TORIRS_FEATURE_CHOICES_MAX];
        char const* selected;
        int count;

        if( flag->section[0] && strcmp(section, flag->section) != 0 )
        {
            snprintf(section, sizeof(section), "%s", flag->section);
            snprintf(section_key, sizeof(section_key), "sec_%d", section_index++);
            memset(&row, 0, sizeof(row));
            row.key = section_key;
            row.kind = PORCELAIN_ROW_HEADING;
            row.label = section;
            Porcelain_Row(describe, &row);
        }
        count = ff_options(api, flag, options, values, labels, &selected);
        memset(&row, 0, sizeof(row));
        row.key = flag->key;
        row.kind = PORCELAIN_ROW_SELECT;
        row.label = flag->label;
        row.text = selected;
        row.options = options;
        row.option_count = count;
        row.on_action = ff_pick;
        row.user = state;
        Porcelain_Row(describe, &row);
    }
    if( state->flag_count == 0 )
    {
        /* An explicit empty state, not a blank page. */
        memset(&row, 0, sizeof(row));
        row.key = "empty";
        row.kind = PORCELAIN_ROW_LABEL;
        row.text = "This build publishes no feature flags.";
        Porcelain_Row(describe, &row);
    }
}

static void
ff_on_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct FeatureFlagsState* state = state_ptr;
    assert(api->client);
    /* An essential settings page with no layer is a page that cannot exist. */
    assert(api->porcelain);
    state->api = api;
    ff_refresh(api, state);
    ff_apply_all(api, state);
    state->porcelain = Porcelain_Open(api, &TORIRS_FEATURE_FLAGS, state);
    Porcelain_Panel(state->porcelain, NULL, TORIRS_PANEL_WIDTH_DEFAULT, PORCELAIN_FACE_BOTH);
    Porcelain_Describe(state->porcelain, ff_describe, state);
}

static void
ff_on_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)api;
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

static void
ff_on_frame_start(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)event;
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
}

static void
ff_on_ui_build(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)api;
    /* The same description on both faces: these ARE the settings. */
    Porcelain_PanelBuild(state->porcelain, panel, view);
}

static void
ff_on_ui_action(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelActionEvent const* event)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)api;
    if( !event )
        return;
    (void)Porcelain_PanelAction(state->porcelain, event);
}

static void
ff_on_config_changed(
    struct ToriRS_Api* api,
    void* state_ptr,
    char const* key)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)key;
    if( state->flag_count == 0 ) ff_refresh(api, state);
    ff_apply_all(api, state);
    /* The stored values ARE the description's input. */
    Porcelain_Note(state->porcelain, PORCELAIN_INPUT_CONFIG);
}

struct ToriRS_PluginDef const TORIRS_FEATURE_FLAGS = {
    .struct_size = sizeof(TORIRS_FEATURE_FLAGS),
    .id = "feature-flags",
    .title = "Feature Flags",
    .version = "2.0.0",
    .state_size = sizeof(struct FeatureFlagsState),
    .flags = TORIRS_PLUGIN_ESSENTIAL,
    .event_priority = 1000,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = ff_on_start,
        .on_stop = ff_on_stop,
        .on_frame_start = ff_on_frame_start,
        .on_config_changed = ff_on_config_changed,
        .on_ui_build = ff_on_ui_build,
        .on_ui_action = ff_on_ui_action,
    },
};
