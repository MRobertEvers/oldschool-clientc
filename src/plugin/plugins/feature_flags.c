#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Feature Flags -- the client's own knobs, in one place.
 *
 * The first row of the plugin roster and the only one with no switch, because
 * it is not a feature: it is where the CLIENT's behaviour is edited, and
 * "switch the settings off" is not a state anyone means to be in. @see
 * ToriRS_PluginDef::essential.
 *
 * WHAT IT SHOWS IS THE ENGINE'S LIST, NOT ITS OWN. Every row comes from
 * api->feature_next, so the page grows a control when the engine publishes a
 * flag and loses one when it stops -- no schema here to keep in step with
 * src/features/features.h, and no way for this file to name a flag the engine
 * decided not to publish.
 *
 * That is also where the "server agreement" rule is enforced, and it has to
 * be: the pathing model, the approach model, the ground-click nearest model
 * and its unbounded extension, the route window, symmetric PvP line of sight,
 * the run-energy model and the era itself are all read by BOTH halves of this
 * tree. A client holding a different value from the server it is talking to
 * does not get a different experience, it gets a wrong one -- tiles flagged
 * inside a boss, routes the server will not honour, an energy bar nothing
 * else believes. None of those are published, so none of them can appear
 * here, whatever this file did.
 *
 * Every flag has one extra choice this plugin adds and the engine does not:
 * FF_DEFAULT_LABEL, which sets the flag back to whatever the boot resolved
 * from the era table, the manifest and the revconfig. The engine restores it
 * from its own snapshot (TORIRS_PLUGIN_FEATURE_UNSET); nothing here has to
 * know what the value was.
 *
 * State lives in this plugin's ini section, one key per flag, holding the
 * TEXT of the choice or the number -- so the file reads as what the user
 * picked rather than as a column of enum ordinals. A key absent, or holding
 * the default label, means "leave the revision alone" and the engine is not
 * called at all for it.
 */

/** The choice every flag carries, standing for TORIRS_PLUGIN_FEATURE_UNSET. */
#define FF_DEFAULT_LABEL "Revision default"

/** Flags this plugin will render. The engine publishes far fewer; this is the
 *  ceiling on the walk, not a statement about the list. */
#define FF_MAX 32

static struct ToriRS_PluginApi const* g_api;

/** The engine's list, as of the last build. Held so the UI handler can map a
 *  control id back to the flag it belongs to without walking again. */
static struct ToriRS_PluginFeature g_flags[FF_MAX];
static int g_flag_count;

/* ------------------------------------------------------------------------ */

/** Copy option `index` out of a '|'-separated list. False past the end. */
static bool
ff_choice_at(
    char const* choices,
    int index,
    char* out,
    size_t out_size)
{
    assert(choices);
    assert(out);
    assert(out_size > 0);

    char const* at = choices;
    for( int i = 0; i < index; i++ )
    {
        at = strchr(at, '|');
        if( !at )
            return false;
        at++;
    }

    char const* end = strchr(at, '|');
    size_t len = end ? (size_t)(end - at) : strlen(at);
    if( len >= out_size )
        len = out_size - 1;
    memcpy(out, at, len);
    out[len] = '\0';
    return true;
}

/**
 * Value of the choice whose text is `text`, or TORIRS_PLUGIN_FEATURE_UNSET.
 *
 * Matched on the TEXT and not on an index, because the index is a property of
 * the engine's list at the moment it was read and the ini outlives that: a
 * flag that grows a choice would silently reinterpret every saved ordinal
 * after it, and the value that came back would be a neighbour of the one the
 * user picked.
 */
static int
ff_enum_value(
    struct ToriRS_PluginFeature const* flag,
    char const* text)
{
    assert(flag);
    assert(text);

    for( int i = 0; i < flag->value_count; i++ )
    {
        char choice[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
        if( !ff_choice_at(flag->choices, i, choice, sizeof(choice)) )
            break;
        if( strcmp(choice, text) == 0 )
            return flag->values[i];
    }
    return TORIRS_PLUGIN_FEATURE_UNSET;
}

/** This flag's stored choice, or "" when the key has never been written.
 *
 *  Guarded, because there is no schema to have declared it: the reader asserts
 *  a declared key and this plugin's keys are the engine's list, so a flag
 *  nobody has ever touched has no slot to read. */
static char const*
ff_stored(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginFeature const* flag)
{
    assert(ctx);
    assert(flag);
    if( !g_api->cfg_has(ctx, flag->key) )
        return "";
    return g_api->cfg_str(ctx, flag->key);
}

/** Is the stored value the "leave the revision alone" one? */
static bool
ff_stored_is_default(char const* stored)
{
    assert(stored);
    return stored[0] == '\0' || strcmp(stored, FF_DEFAULT_LABEL) == 0;
}

/* ------------------------------------------------------------------------ */

/** Re-read the engine's list into g_flags. */
static void
ff_refresh(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    struct ToriRS_PluginFeature flag;
    int iter = -1;

    g_flag_count = 0;
    while( (iter = g_api->feature_next(ctx, iter, &flag)) >= 0 )
    {
        if( g_flag_count >= FF_MAX )
        {
            g_api->log(ctx, "feature-flags: engine publishes more than %d flags", FF_MAX);
            break;
        }
        g_flags[g_flag_count++] = flag;
    }
}

/**
 * Push every stored override into the engine.
 *
 * Both halves matter. A key holding a choice is applied; a key holding the
 * default -- or absent -- is pushed as TORIRS_PLUGIN_FEATURE_UNSET, which is
 * what puts a flag BACK after the user changes their mind. Without that
 * second half "Revision default" would be a choice you could pick and never
 * see take effect until the next launch.
 */
static void
ff_apply_all(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    for( int i = 0; i < g_flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &g_flags[i];
        char const* stored = ff_stored(ctx, flag);
        int value = TORIRS_PLUGIN_FEATURE_UNSET;

        if( !ff_stored_is_default(stored) )
        {
            value = flag->kind == TORIRS_PLUGIN_FEATURE_ENUM ? ff_enum_value(flag, stored)
                                                             : atoi(stored);
        }

        if( !g_api->feature_set(ctx, flag->key, value) )
            g_api->log(
                ctx,
                "feature-flags: engine refused %s=%s",
                flag->key,
                stored[0] ? stored : FF_DEFAULT_LABEL);
    }
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * Build the tab from the engine's list.
 *
 * Controls rather than a config schema, because the schema is a static array
 * on the def and the list is not: it is whatever this build of the engine
 * publishes on this revision's cache, which is not a thing a table in this
 * file could state without being wrong on one of them.
 */
static enum ToriRS_PluginVerdict
ff_on_ui_build(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);

    if( !g_api->win_request(ctx, "Feature Flags") )
        return TORIRS_PLUGIN_PASS;

    ff_refresh(ctx);

    for( int i = 0; i < g_flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &g_flags[i];
        char const* stored = ff_stored(ctx, flag);

        if( flag->kind == TORIRS_PLUGIN_FEATURE_ENUM )
        {
            /*
             * FF_DEFAULT_LABEL first and always, because it is the one choice
             * every flag has and the engine states none of them: a flag says
             * what its values ARE, and "the one this boot resolved" is not one
             * of them -- it is the absence of a choice.
             */
            char choices[TORIRS_PLUGIN_FEATURE_CHOICES_MAX + sizeof(FF_DEFAULT_LABEL) + 1];
            char current[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
            int selected = 0;

            snprintf(choices, sizeof(choices), "%s|%s", FF_DEFAULT_LABEL, flag->choices);
            if( ff_stored_is_default(stored) )
                snprintf(current, sizeof(current), "%s", FF_DEFAULT_LABEL);
            else
                snprintf(current, sizeof(current), "%s", stored);

            for( int c = 0;; c++ )
            {
                char option[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
                if( !ff_choice_at(choices, c, option, sizeof(option)) )
                    break;
                if( strcmp(option, current) == 0 )
                {
                    selected = c;
                    break;
                }
            }

            g_api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, flag->key, flag->label);
            g_api->win_set_options(ctx, flag->key, choices, selected);
        }
        else
        {
            /*
             * A number, edited as text, with the sentinel spelled out rather
             * than written as -1: the range a flag states is its own, and
             * every one of them would otherwise have to explain separately
             * what a negative number meant.
             */
            char label[TORIRS_PLUGIN_FEATURE_LABEL_MAX + 48];
            char text[32];

            snprintf(
                label,
                sizeof(label),
                "%s (%d..%d, or %s)",
                flag->label,
                flag->min,
                flag->max,
                FF_DEFAULT_LABEL);
            if( ff_stored_is_default(stored) )
                snprintf(text, sizeof(text), "%s", FF_DEFAULT_LABEL);
            else
                snprintf(text, sizeof(text), "%d", atoi(stored));

            g_api->win_widget(ctx, TORIRS_PLUGIN_W_INPUT, flag->key, label);
            g_api->win_set_text(ctx, flag->key, text);
        }
    }

    if( g_flag_count == 0 )
        g_api->win_widget(
            ctx, TORIRS_PLUGIN_W_LABEL, "empty", "This build publishes no feature flags.");

    return TORIRS_PLUGIN_PASS;
}

/** A control was used: store the choice and push it straight through. */
static enum ToriRS_PluginVerdict
ff_on_ui(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvUi* ev = (struct ToriRS_PluginEvUi*)event;

    assert(ctx);
    assert(ev);

    if( !ev->widget_id )
        return TORIRS_PLUGIN_PASS;

    for( int i = 0; i < g_flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &g_flags[i];
        if( strcmp(flag->key, ev->widget_id) != 0 )
            continue;

        /*
         * Stored as the user's own text -- the choice, or the number, or the
         * default label -- and only then turned into a value. The ini is a
         * record of what was picked; the engine's number is derived from it
         * every time, so a flag whose choices change between builds re-reads
         * as the same words rather than as the same ordinal.
         */
        char const* text = ev->text;
        if( !text || !text[0] )
            text = FF_DEFAULT_LABEL;
        g_api->cfg_set(ctx, flag->key, text);

        if( ff_stored_is_default(text) )
        {
            g_api->feature_set(ctx, flag->key, TORIRS_PLUGIN_FEATURE_UNSET);
            return TORIRS_PLUGIN_PASS;
        }

        int const value =
            flag->kind == TORIRS_PLUGIN_FEATURE_ENUM ? ff_enum_value(flag, text) : atoi(text);
        if( !g_api->feature_set(ctx, flag->key, value) )
        {
            /*
             * A typed number the flag will not take. Said out loud and put
             * back, rather than left in the field looking accepted -- the
             * whole failure mode of a text field over a bounded value.
             */
            g_api->notify(ctx, "That value is outside what this setting accepts.");
            g_api->cfg_set(ctx, flag->key, FF_DEFAULT_LABEL);
            g_api->feature_set(ctx, flag->key, TORIRS_PLUGIN_FEATURE_UNSET);
            g_api->win_set_text(ctx, flag->key, FF_DEFAULT_LABEL);
        }
        return TORIRS_PLUGIN_PASS;
    }
    return TORIRS_PLUGIN_PASS;
}

/** A key was edited from somewhere other than this tab -- a hand-edited ini,
 *  or the panel's own form. Re-push the lot; there is no cheaper answer that
 *  is also right about a key going back to its default. */
static enum ToriRS_PluginVerdict
ff_on_config(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    if( g_flag_count == 0 )
        ff_refresh(ctx);
    ff_apply_all(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
ff_on_start(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    ff_refresh(ctx);
    ff_apply_all(ctx);
    return TORIRS_PLUGIN_PASS;
}

static void
ff_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, ff_on_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI_BUILD, ff_on_ui_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI, ff_on_ui, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CONFIG_CHANGED, ff_on_config, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_FEATURE_FLAGS = {
    .name = "feature-flags",
    .title = "Feature Flags",
    .version = "1.0",
    /*
     * Above every other plugin's default, so the overrides are in force before
     * anything that reads a flag has run its own START. A gameframe that
     * claimed the layout under one draw distance and a feature flag that
     * changed it afterwards would be a frame built against a number that no
     * longer holds.
     */
    .priority = 1000,
    /* No static schema: the keys are the engine's published flags, and a key
     * an ini carries that the engine no longer publishes is kept by the store
     * anyway (PluginConfigSlot::schema_index) -- so a flag that comes back
     * comes back with its value. */
    .config = NULL,
    .essential = true,
    .init = ff_init,
};
