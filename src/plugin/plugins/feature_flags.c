#include "plugin/torirs_plugin_v2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FF_DEFAULT_LABEL "Revision default"
#define FF_MAX 32
#define FF_OPTION_MAX (TORIRS_PLUGIN_FEATURE_VALUES_MAX + 2)

struct FeatureFlagsState
{
    struct ToriRS_PluginFeature flags[FF_MAX];
    int flag_count;
};

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
ff_enum_value(struct ToriRS_PluginFeature const* flag, char const* text)
{
    for( int i = 0; i < flag->value_count; i++ )
    {
        char choice[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
        if( !ff_choice_at(flag->choices, i, choice, sizeof(choice)) ) break;
        if( strcmp(choice, text) == 0 ) return flag->values[i];
    }
    return TORIRS_PLUGIN_FEATURE_UNSET;
}

static int
ff_value_index(struct ToriRS_PluginFeature const* flag, int value)
{
    for( int i = 0; i < flag->value_count; i++ )
        if( flag->values[i] == value ) return i;
    return -1;
}

static char const*
ff_stored(struct ToriRS_ApiV2* api, struct ToriRS_PluginFeature const* flag)
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
    struct ToriRS_PluginFeature const* flag,
    char const* stored)
{
    if( ff_is_default(stored) ) return TORIRS_PLUGIN_FEATURE_UNSET;
    return flag->kind == TORIRS_PLUGIN_FEATURE_ENUM
               ? ff_enum_value(flag, stored)
               : atoi(stored);
}

static void
ff_refresh(struct ToriRS_ApiV2* api, struct FeatureFlagsState* state)
{
    struct ToriRS_PluginFeature flag;
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
ff_apply_all(struct ToriRS_ApiV2* api, struct FeatureFlagsState* state)
{
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &state->flags[i];
        char const* stored = ff_stored(api, flag);
        if( api->client->feature_set(
                api, flag->key, ff_stored_value(flag, stored)) != TORIRS_RESULT_OK )
            api->core.log(api, "feature-flags: engine refused %s=%s",
                flag->key, stored[0] ? stored : FF_DEFAULT_LABEL);
    }
}

static int
ff_options(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginFeature const* flag,
    struct ToriRS_SelectOption* options,
    char values[FF_OPTION_MAX][32],
    char labels[FF_OPTION_MAX][TORIRS_PLUGIN_FEATURE_CHOICES_MAX],
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
        char choice[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
        (void)ff_choice_at(flag->choices, named, choice, sizeof(choice));
        snprintf(labels[count], sizeof(labels[count]), "%s (%s)", FF_DEFAULT_LABEL, choice);
    }
    else if( flag->is_default && flag->kind == TORIRS_PLUGIN_FEATURE_INT )
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
        if( flag->kind == TORIRS_PLUGIN_FEATURE_ENUM )
            snprintf(values[count], sizeof(values[count]), "%s", labels[count]);
        else
            snprintf(values[count], sizeof(values[count]), "%d", flag->values[i]);
        options[count] = (struct ToriRS_SelectOption){
            .struct_size = sizeof(options[count]),
            .value = values[count], .label = labels[count], .enabled = true,
        };
    }
    if( wanted != TORIRS_PLUGIN_FEATURE_UNSET &&
        ff_value_index(flag, wanted) < 0 &&
        flag->kind == TORIRS_PLUGIN_FEATURE_INT && count < FF_OPTION_MAX )
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

static void
ff_publish_option(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PluginFeature* flag)
{
    struct ToriRS_SelectOption options[FF_OPTION_MAX];
    char values[FF_OPTION_MAX][32];
    char labels[FF_OPTION_MAX][TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
    char const* selected;
    int count;
    int effective;

    flag->is_default = ff_is_default(ff_stored(api, flag));
    if( api->client->feature_get(api, flag->key, &effective) )
        flag->value = effective;
    count = ff_options(api, flag, options, values, labels, &selected);
    if( api->panel.set_options(api, flag->key, selected, options, count) !=
        TORIRS_RESULT_OK )
        api->panel.invalidate(api);
}

static void
ff_on_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct FeatureFlagsState* state = state_ptr;
    struct ToriRS_PluginPanelDesc panel = { NULL, TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT };
    assert(api->client);
    ff_refresh(api, state);
    ff_apply_all(api, state);
    (void)api->panel.request(api, &panel);
}

static void
ff_on_ui_build(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct FeatureFlagsState* state = state_ptr;
    char section[TORIRS_PLUGIN_FEATURE_KEY_MAX] = "";
    (void)view;
    ff_refresh(api, state);
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &state->flags[i];
        struct ToriRS_SelectOption options[FF_OPTION_MAX];
        char values[FF_OPTION_MAX][32];
        char labels[FF_OPTION_MAX][TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
        char const* selected;
        int count;
        if( flag->section[0] && strcmp(section, flag->section) != 0 )
        {
            snprintf(section, sizeof(section), "%s", flag->section);
            panel->heading(panel, section);
        }
        count = ff_options(api, flag, options, values, labels, &selected);
        panel->select(panel, flag->key, flag->label, selected, options, count);
    }
    if( state->flag_count == 0 )
        panel->label(panel, "empty", "This build publishes no feature flags.");
}

static void
ff_on_ui_action(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PluginEvPanelAction const* event)
{
    struct FeatureFlagsState* state = state_ptr;
    if( !event || event->action != TORIRS_PLUGIN_UI_PICK || !event->id || !event->text )
        return;
    for( int i = 0; i < state->flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &state->flags[i];
        char const* stored = event->text;
        int value;
        if( strcmp(flag->key, event->id) != 0 ) continue;
        if( flag->kind == TORIRS_PLUGIN_FEATURE_ENUM &&
            !ff_is_default(stored) && ff_enum_value(flag, stored) == TORIRS_PLUGIN_FEATURE_UNSET )
            return;
        value = ff_stored_value(flag, stored);
        if( flag->kind == TORIRS_PLUGIN_FEATURE_INT && value != TORIRS_PLUGIN_FEATURE_UNSET &&
            (value < flag->min || value > flag->max) )
            return;
        (void)api->config.set(api, flag->key, stored);
        if( api->client->feature_set(api, flag->key, value) != TORIRS_RESULT_OK )
        {
            api->core.log(api, "feature-flags: engine refused %s=%s", flag->key, stored);
            (void)api->config.set(api, flag->key, FF_DEFAULT_LABEL);
            (void)api->client->feature_set(
                api, flag->key, TORIRS_PLUGIN_FEATURE_UNSET);
        }
        ff_publish_option(api, &state->flags[i]);
        return;
    }
}

static void
ff_on_config_changed(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    char const* key)
{
    struct FeatureFlagsState* state = state_ptr;
    (void)key;
    if( state->flag_count == 0 ) ff_refresh(api, state);
    ff_apply_all(api, state);
}

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_FEATURE_FLAGS = {
    .struct_size = sizeof(TORIRS_PLUGIN_FEATURE_FLAGS),
    .id = "feature-flags",
    .title = "Feature Flags",
    .version = "2.0.0",
    .state_size = sizeof(struct FeatureFlagsState),
    .flags = TORIRS_PLUGIN_V2_ESSENTIAL,
    .event_priority = 1000,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = ff_on_start,
        .on_config_changed = ff_on_config_changed,
        .on_ui_build = ff_on_ui_build,
        .on_ui_action = ff_on_ui_action,
    },
};
