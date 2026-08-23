#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Feature Flags -- the client's own knobs, in one place.
 *
 * The first row of the plugin roster and one of the two with no switch,
 * because it is not a feature: it is where the CLIENT's behaviour is edited,
 * and "switch the settings off" is not a state anyone means to be in. @see
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
 * ## Every row is a dropdown, and none is a text field
 *
 * This page used to put the numeric flags in text boxes captioned with their
 * range, and it was the worst control on the screen. A number typed into a box
 * is a value nobody has checked until it is committed, so the row's whole
 * vocabulary became refusing and explaining -- and the refusal came after the
 * caption had already been sliced in half by the field it did not fit beside.
 *
 * So the engine names the values that MEAN something (@see
 * ToriRS_PluginFeatureKind) and every row is a list of them. Nothing is typed,
 * nothing is out of range, and the choice says what it does -- "70 tiles
 * (deob)" rather than an integer in 0..104. Sections and short captions do the
 * rest: a caption belongs to a heading, not to a parenthesis.
 *
 * ## The default is a choice, not an absence
 *
 * Every flag carries one extra entry this plugin adds and the engine does not:
 * FF_DEFAULT_LABEL, which puts the flag back to whatever the boot resolved
 * from the era table, the manifest and the revconfig. The engine restores it
 * from its own snapshot (TORIRS_PLUGIN_FEATURE_UNSET); nothing here has to
 * know what the value was -- and because it does not, the entry also SHOWS
 * that value, so a page of untouched rows still says what the client is
 * actually doing instead of saying "default" fifteen times.
 *
 * ## What the ini holds
 *
 * An ENUM flag stores the choice TEXT, so a settings file survives the list
 * gaining an entry -- an ordinal would silently become its neighbour. An INT
 * flag stores the NUMBER, because its named values are suggestions rather than
 * its legal set: a hand-edited file may carry an unnamed one, and this page
 * shows it as an extra entry rather than dropping it. A key that is absent, or
 * holds FF_DEFAULT_LABEL, means "leave the revision alone".
 */

/** The choice every flag carries, standing for TORIRS_PLUGIN_FEATURE_UNSET. */
#define FF_DEFAULT_LABEL "Revision default"

/** Flags this plugin will render. The engine publishes far fewer; this is the
 *  ceiling on the walk, not a statement about the list. */
#define FF_MAX 32

/**
 * One row's assembled choice list: FF_DEFAULT_LABEL, the engine's names, and
 * -- for an INT carrying a value none of them names -- that value too.
 *
 * Sized for the default entry (which shows the effective value, so it is the
 * longest single entry), the engine's whole list, and the one appended custom
 * number.
 */
#define FF_CHOICES_MAX (TORIRS_PLUGIN_FEATURE_CHOICES_MAX + 96)

static struct ToriRS_PluginApi const* g_api;

/** The engine's list, as of the last build. Held so the UI handler can map a
 *  control id back to the flag it belongs to without walking again. */
static struct ToriRS_PluginFeature g_flags[FF_MAX];
static int g_flag_count;

/* ------------------------------------------------------------------------ */
/* Choice lists                                                              */
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

/** Index in `flag->values` of `value`, or -1 when nothing names it. */
static int
ff_value_index(
    struct ToriRS_PluginFeature const* flag,
    int value)
{
    assert(flag);

    for( int i = 0; i < flag->value_count; i++ )
    {
        if( flag->values[i] == value )
            return i;
    }
    return -1;
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

/** The value a stored string asks for, or the sentinel for "the revision's". */
static int
ff_stored_value(
    struct ToriRS_PluginFeature const* flag,
    char const* stored)
{
    assert(flag);
    assert(stored);

    if( ff_stored_is_default(stored) )
        return TORIRS_PLUGIN_FEATURE_UNSET;
    if( flag->kind == TORIRS_PLUGIN_FEATURE_ENUM )
        return ff_enum_value(flag, stored);
    return atoi(stored);
}

/**
 * Append to a bounded buffer and return the new length, never past `size - 1`.
 *
 * `at += snprintf(...)` is the shape this replaces, and it is wrong the moment
 * anything truncates: snprintf returns what it WOULD have written, so `at`
 * walks past the buffer and the next call is handed a negative length that a
 * size_t reads as most of the address space.
 */
static size_t
ff_append(
    char* out,
    size_t size,
    size_t at,
    char const* fmt,
    ...)
{
    assert(out);
    assert(size > 0);
    assert(fmt);

    if( at >= size - 1 )
        return size - 1;

    va_list args;
    va_start(args, fmt);
    int const wrote = vsnprintf(out + at, size - at, fmt, args);
    va_end(args);

    if( wrote < 0 )
        return at;
    if( (size_t)wrote >= size - at )
        return size - 1;
    return at + (size_t)wrote;
}

/**
 * Build one row's list, and say which entry is showing.
 *
 * Entry 0 is always the default, and it carries the value in force with it --
 * "Revision default (per frame)" rather than a word that could mean anything.
 * A reader who has changed nothing can still see what the client is doing,
 * which is what a settings page is for and what a bare "default" refuses to
 * say.
 *
 * @param out_custom receives the value of the appended unnamed entry, or the
 *        sentinel when none was appended.
 * @return the index to show.
 */
static int
ff_build_choices(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginFeature const* flag,
    char* out,
    size_t out_size,
    int* out_custom)
{
    assert(ctx);
    assert(flag);
    assert(out);
    assert(out_custom);

    char const* stored = ff_stored(ctx, flag);
    int const wanted = ff_stored_value(flag, stored);
    char in_force_text[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
    size_t at = 0;
    int selected = 0;

    *out_custom = TORIRS_PLUGIN_FEATURE_UNSET;

    /*
     * What entry 0 promises, in words where the flag names the value and as a
     * number where it does not.
     *
     * Read from the ENGINE rather than remembered, because a restore is the
     * engine's snapshot and this plugin never sees the number it would put
     * back. Only worth saying while the flag IS at its default: naming the
     * boot value beside a row that is overriding it would be two answers to
     * "what is this set to" on one line.
     */
    in_force_text[0] = '\0';
    if( flag->is_default )
    {
        int const in_force = g_api->feature_get(ctx, flag->key);
        int const named = ff_value_index(flag, in_force);

        if( named >= 0 )
            (void)ff_choice_at(flag->choices, named, in_force_text, sizeof(in_force_text));
        else if( flag->kind == TORIRS_PLUGIN_FEATURE_INT )
            snprintf(in_force_text, sizeof(in_force_text), "%d", in_force);
    }

    if( in_force_text[0] )
        at = ff_append(out, out_size, at, "%s (%s)", FF_DEFAULT_LABEL, in_force_text);
    else
        at = ff_append(out, out_size, at, "%s", FF_DEFAULT_LABEL);

    for( int i = 0; i < flag->value_count; i++ )
    {
        char choice[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
        if( !ff_choice_at(flag->choices, i, choice, sizeof(choice)) )
            break;
        at = ff_append(out, out_size, at, "|%s", choice);
        if( wanted != TORIRS_PLUGIN_FEATURE_UNSET && flag->values[i] == wanted )
            selected = i + 1;
    }

    /*
     * A number the list does not name, kept rather than dropped.
     *
     * An INT's named values are the ones worth offering, not its legal set, so
     * a hand-edited ini may carry any number in range. Showing it as an entry
     * is what stops opening this page from silently rewriting a file somebody
     * edited on purpose -- the alternative is a dropdown that reads "Revision
     * default" over a client that is not at its default.
     */
    if( wanted != TORIRS_PLUGIN_FEATURE_UNSET && selected == 0 &&
        flag->kind == TORIRS_PLUGIN_FEATURE_INT )
    {
        (void)ff_append(out, out_size, at, "|%d", wanted);
        *out_custom = wanted;
        selected = flag->value_count + 1;
    }
    return selected;
}

/* ------------------------------------------------------------------------ */
/* Applying                                                                  */
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
        int const value = ff_stored_value(flag, stored);

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

    char section[TORIRS_PLUGIN_FEATURE_KEY_MAX] = { 0 };
    int heading = 0;

    for( int i = 0; i < g_flag_count; i++ )
    {
        struct ToriRS_PluginFeature const* flag = &g_flags[i];
        char choices[FF_CHOICES_MAX];
        int custom;
        int const selected = ff_build_choices(ctx, flag, choices, sizeof(choices), &custom);

        /*
         * A heading whenever the engine's section changes, with a rule above
         * every one but the first. Fifteen rows in one column is a list to
         * search; four groups of four is a page to read, and the grouping is
         * the engine's own -- it is the order the walk hands them over in.
         */
        if( flag->section[0] && strcmp(flag->section, section) != 0 )
        {
            char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

            snprintf(section, sizeof(section), "%s", flag->section);
            if( heading++ )
            {
                snprintf(id, sizeof(id), "rule_%d", heading);
                g_api->win_widget(ctx, TORIRS_PLUGIN_W_SEPARATOR, id, NULL);
            }
            snprintf(id, sizeof(id), "head_%d", heading);
            g_api->win_widget(ctx, TORIRS_PLUGIN_W_LABEL, id, flag->section);
        }

        if( !g_api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, flag->key, flag->label) )
        {
            /* Out of control budget. Said out loud, because the alternative is
             * a settings page that is simply missing its last few rows with
             * nothing on screen to suggest they exist. */
            g_api->log(
                ctx, "feature-flags: no room on the tab for '%s' and what follows", flag->key);
            break;
        }
        g_api->win_set_options(ctx, flag->key, choices, selected);
    }

    if( g_flag_count == 0 )
        g_api->win_widget(
            ctx, TORIRS_PLUGIN_W_LABEL, "empty", "This build publishes no feature flags.");

    return TORIRS_PLUGIN_PASS;
}

/** Rebuild the page so every row reads back what it now holds. Cheap, and the
 *  only thing that is right about the default entry: it carries the value in
 *  force, so a change to ANY row can change what another row's entry 0 says. */
static void
ff_rebuild(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    g_api->win_clear(ctx);
    ff_on_ui_build(ctx, NULL, NULL);
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
         * By INDEX, not by the text shown.
         *
         * The opposite of how the ini is keyed, and for the opposite reason:
         * the list was assembled by ff_build_choices moments ago and is still
         * the one on screen, whereas entry 0's text carries the value in force
         * and is therefore not a name that means anything later. Index 0 is
         * the default, 1..value_count are the engine's, and one past them is
         * the unnamed number an ini carried -- which stays exactly as it was.
         */
        int const choice = ev->value;
        char stored[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];

        if( choice <= 0 )
            snprintf(stored, sizeof(stored), "%s", FF_DEFAULT_LABEL);
        else if( choice <= flag->value_count )
        {
            if( flag->kind == TORIRS_PLUGIN_FEATURE_ENUM )
            {
                if( !ff_choice_at(flag->choices, choice - 1, stored, sizeof(stored)) )
                    return TORIRS_PLUGIN_PASS;
            }
            else
                snprintf(stored, sizeof(stored), "%d", flag->values[choice - 1]);
        }
        else
        {
            /* The appended unnamed number: chosen means "keep it", so the
             * store is already right and rewriting it could only round it. */
            return TORIRS_PLUGIN_PASS;
        }

        g_api->cfg_set(ctx, flag->key, stored);
        if( !g_api->feature_set(ctx, flag->key, ff_stored_value(flag, stored)) )
        {
            /* Nothing on this page can produce one -- every entry came from the
             * engine's own list -- so a refusal means the list moved under us.
             * Say so and put the row back rather than leaving it showing a
             * choice that did not take. */
            g_api->log(ctx, "feature-flags: engine refused %s=%s", flag->key, stored);
            g_api->cfg_set(ctx, flag->key, FF_DEFAULT_LABEL);
            g_api->feature_set(ctx, flag->key, TORIRS_PLUGIN_FEATURE_UNSET);
        }
        ff_rebuild(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    return TORIRS_PLUGIN_PASS;
}

/** A key was edited from somewhere other than this tab -- a hand-edited ini.
 *  Re-push the lot; there is no cheaper answer that is also right about a key
 *  going back to its default. */
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
    .version = "1.1",
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
