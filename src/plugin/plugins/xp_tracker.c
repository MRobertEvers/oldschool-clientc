#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * XP Tracker -- a port of RuneLite's `xptracker` plugin.
 *
 * What it answers, per skill and for the session as a whole: how much xp you
 * have gained, how fast you are gaining it, how many actions that took, how
 * many are left, and how long until the next level at the rate you are going.
 *
 * It is a PAGE and not an overlay, which is the one structural difference from
 * the reference. RuneLite's plugin is two halves -- a side panel and a set of
 * opt-in canvas info boxes -- and this client already has the second half:
 * `xp-drop-orbs` (XP Globes) draws the on-canvas progress orb and the floating
 * drop. Writing a second thing that paints xp over the viewport would give the
 * player two readouts of one number, drawn by two plugins, that disagree the
 * first time either is configured. So this one owns the panel and nothing on
 * the canvas, and the two compose rather than overlap.
 *
 * ---- the arithmetic is XpStateSingle's, restated ----
 *
 * Every formula below is the reference's, and the comments name which:
 *
 *   xp/hr           (3600 / elapsed) * xp_gained_since_reset, with elapsed
 *                   floored at 60 seconds -- a skill that started a moment ago
 *                   would otherwise divide by near zero and report billions.
 *   actions/hr      the same extrapolation over actions_since_reset.
 *   actions left    xp_remaining / the MEAN of the last ten action gains,
 *                   rounded up. Unknown until ten have been seen, because one
 *                   sample of a skill that grants 5 and 60 alternately is a
 *                   number worth less than no number.
 *   time to level   (xp_remaining * elapsed) / xp_gained_since_reset.
 *   progress        (xp - level_xp) / (next_xp - level_xp).
 *
 * `skill_time_ms` is not wall-clock: it only advances while the skill has
 * gained xp since its last per-hour reset, so a rate is measured over the time
 * you were TRAINING and not over the time the client was open. That is what
 * makes "pause" and "reset rate" meaningful knobs rather than cosmetic ones.
 *
 * ---- what is not ported, and why ----
 *
 * The GOAL varps (XPDROPS_<SKILL>_START / _END, which the stats tab's own
 * goal-setting writes) are OldSchool-only and this client boots 2004 caches as
 * well. Every "remaining" figure here is therefore measured against the next
 * LEVEL, which is the thing every revision agrees exists -- api->stat_xp hands
 * back the two thresholds the current level runs between, so the client's own
 * table is the one answer rather than a second copy of it here.
 *
 * Wise Old Man, the skill-tab right-click entries and the info-box label
 * permutations are all about a UI this client does not have.
 */

/** Skills this plugin will track. The client's table is 25 long (sailing and
 *  summoning at the top of it); the walk stops at whatever api->skill_name
 *  answers NULL for, and this is only the ceiling on the walk. */
#define XT_SKILLS_MAX 32

/** Action gains kept per skill for the "actions left" mean. The reference's
 *  ten, and the number is load-bearing: it is what the estimate is a mean of. */
#define XT_ACTION_HISTORY 10

/** Skill rows the page will draw. The panel's own budget is 48 controls for
 *  every plugin, and the detail block below the list needs ten of them. */
#define XT_ROWS_MAX 30

/** How often the page's numbers are rewritten, in ms. Every readout on it is
 *  derived from a clock, so it would otherwise be reformatted 50 times a
 *  second to say the same thing. */
#define XT_PANEL_REFRESH_MS 500

/** The per-second cadence the reference accumulates skill time on. */
#define XT_SECOND_MS 1000

static struct ToriRS_PluginApi const* g_api;

/**
 * One skill's tracking state -- RuneLite's XpStateSingle, field for field.
 *
 * `start_xp` is -1 until the skill has been SEEN, and seeing is not the same
 * as training: every stat arrives at once on login, and a client that treated
 * that as 25 simultaneous gains would open the session with every skill
 * claiming an action.
 */
struct XtSkill
{
    /** Xp when tracking began, or -1 when the skill has not been seen yet. */
    int start_xp;
    /** Last xp read, so a poll can tell a gain from a re-read. */
    int last_xp;
    /** Gained before the last per-hour reset; the rate ignores it and the
     *  session total does not. */
    int gained_before_reset;
    /** Gained since it: the numerator of every rate on the page. */
    int gained_since_reset;
    int actions;
    int actions_since_reset;
    /** The last XT_ACTION_HISTORY gains, oldest overwritten first. */
    int action_xp[XT_ACTION_HISTORY];
    int action_at;
    /** True once the ring has been filled and its mean means something. */
    bool action_history_full;
    /** Milliseconds this skill has been TRAINING -- see the header comment. */
    uint64_t skill_time_ms;
    /** When it last gained, for the auto-pause and auto-reset timers. */
    uint64_t last_change_ms;
    bool paused;
};

static struct XtSkill g_skill[XT_SKILLS_MAX];
static int g_skill_count;

/** Which skill's detail block is open, or -1 for the list alone. */
static int g_detail = -1;
/** The row set the page was built from, so a skill starting to train rebuilds
 *  it rather than silently not appearing. */
static char g_built_rows[XT_SKILLS_MAX];
static int g_built_detail = -1;
static bool g_page_built;

/** Wall clock of the last per-second tick, and of the session's start. */
static uint64_t g_last_second_ms;
static uint64_t g_session_start_ms;
static uint64_t g_next_panel_ms;
/** Whether a local player existed last poll, so arriving and leaving are
 *  edges rather than states polled every frame. */
static bool g_logged_in;

/* ------------------------------------------------------------------------ */
/* Formatting                                                                */
/* ------------------------------------------------------------------------ */

/** "1,234,567" -- the reference's QuantityFormatter.formatNumber. */
static void
xt_commas(long long value, char* out, size_t out_size)
{
    char digits[32];
    int len;
    size_t at = 0;
    bool negative = value < 0;

    assert(out);
    assert(out_size > 0);

    if( negative )
        value = -value;
    len = snprintf(digits, sizeof(digits), "%lld", value);
    if( len < 0 )
        len = 0;

    if( negative && at + 1 < out_size )
        out[at++] = '-';
    for( int i = 0; i < len; i++ )
    {
        int const remaining = len - i;
        if( i > 0 && remaining % 3 == 0 && at + 1 < out_size )
            out[at++] = ',';
        if( at + 1 < out_size )
            out[at++] = digits[i];
    }
    out[at] = '\0';
}

/**
 * "12.3K" / "4.5M" -- the reference's quantityToRSDecimalStack, which is what
 * a row wide enough for a skill name and a rate can actually hold.
 *
 * One decimal place and only below 100 of a unit, so "9.9K" and "45K" are both
 * four characters and a column of them lines up.
 */
static void
xt_short(long long value, char* out, size_t out_size)
{
    long long unit = 1;
    char suffix = '\0';

    assert(out);
    assert(out_size > 0);

    if( value < 0 )
    {
        snprintf(out, out_size, "0");
        return;
    }
    if( value >= 1000000000LL )
    {
        unit = 1000000000LL;
        suffix = 'B';
    }
    else if( value >= 1000000LL )
    {
        unit = 1000000LL;
        suffix = 'M';
    }
    else if( value >= 1000LL )
    {
        unit = 1000LL;
        suffix = 'K';
    }

    if( !suffix )
    {
        snprintf(out, out_size, "%lld", value);
        return;
    }
    if( value / unit < 100 )
    {
        long long const whole = value / unit;
        long long const tenth = (value % unit) * 10 / unit;
        snprintf(out, out_size, "%lld.%lld%c", whole, tenth, suffix);
    }
    else
        snprintf(out, out_size, "%lld%c", value / unit, suffix);
}

/** "1:02:03" past an hour, "02:03" below it. A duration, never a clock time. */
static void
xt_duration(long long seconds, char* out, size_t out_size)
{
    assert(out);
    assert(out_size > 0);

    if( seconds < 0 )
    {
        snprintf(out, out_size, "—");
        return;
    }
    if( seconds >= 24 * 3600 )
    {
        /* Past a day the reference switches units entirely rather than
         * printing a three-digit hour nobody reads as a duration. */
        snprintf(
            out, out_size, "%lldd %lldh", seconds / (24 * 3600),
            (seconds % (24 * 3600)) / 3600);
        return;
    }
    if( seconds >= 3600 )
        snprintf(
            out, out_size, "%lld:%02lld:%02lld", seconds / 3600,
            (seconds % 3600) / 60, seconds % 60);
    else
        snprintf(out, out_size, "%02lld:%02lld", seconds / 60, seconds % 60);
}

/* ------------------------------------------------------------------------ */
/* The state machine                                                         */
/* ------------------------------------------------------------------------ */

/** Seconds `skill` has been training, floored at 60. @see the header. */
static long long
xt_elapsed_seconds(struct XtSkill const* skill)
{
    long long const seconds = (long long)(skill->skill_time_ms / 1000u);

    assert(skill);
    return seconds < 60 ? 60 : seconds;
}

/** Extrapolate a count over the training time to an hourly rate. */
static long long
xt_hourly(struct XtSkill const* skill, long long value)
{
    assert(skill);
    return value * 3600 / xt_elapsed_seconds(skill);
}

/** Everything gained this session, reset or not. */
static int
xt_gained(struct XtSkill const* skill)
{
    assert(skill);
    return skill->gained_before_reset + skill->gained_since_reset;
}

/**
 * Mean xp of the last ten actions, or 0 when fewer than ten have been seen.
 *
 * Zero and not "the mean of what we have": a skill whose actions alternate
 * between 5 and 60 xp gives a wildly wrong estimate off one sample, and the
 * page says "--" rather than a number it would have to retract.
 */
static int
xt_mean_action_xp(struct XtSkill const* skill)
{
    long long total = 0;

    assert(skill);
    if( !skill->action_history_full )
        return 0;
    for( int i = 0; i < XT_ACTION_HISTORY; i++ )
        total += skill->action_xp[i];
    if( total <= 0 )
        return 0;
    return (int)(total / XT_ACTION_HISTORY);
}

/** Put the per-hour figures back to zero without losing the session total. */
static void
xt_reset_rate(struct XtSkill* skill)
{
    assert(skill);
    skill->gained_before_reset += skill->gained_since_reset;
    skill->gained_since_reset = 0;
    skill->actions_since_reset = 0;
    skill->skill_time_ms = 0;
}

/** Forget everything about one skill and re-seed it from the client. */
static void
xt_reset_skill(struct ToriRS_PluginCtx* ctx, int index)
{
    struct XtSkill* skill;
    int xp = 0;

    assert(ctx);
    assert(index >= 0);
    assert(index < g_skill_count);

    skill = &g_skill[index];
    memset(skill, 0, sizeof(*skill));
    skill->start_xp = -1;
    if( g_api->stat_xp(ctx, index, &xp, NULL, NULL) )
    {
        skill->start_xp = xp;
        skill->last_xp = xp;
    }
}

static void
xt_reset_all(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    for( int i = 0; i < g_skill_count; i++ )
        xt_reset_skill(ctx, i);
    g_session_start_ms = g_api->frame_ms(ctx);
    g_detail = -1;
}

/** Is this skill worth a row? Trained at all, and not hidden by hide_maxed. */
static bool
xt_row_wanted(struct ToriRS_PluginCtx* ctx, int index)
{
    int level = 0;

    assert(ctx);
    assert(index >= 0);
    assert(index < g_skill_count);

    if( xt_gained(&g_skill[index]) <= 0 )
        return false;
    if( !g_api->cfg_bool(ctx, "hide_maxed") )
        return true;
    g_api->stat(ctx, index, NULL, &level);
    return level < 99;
}

/**
 * One xp reading for one skill.
 *
 * The gain is measured against `last_xp` rather than against the start,
 * because the start does not move and the ACTION does: a plugin that computed
 * "gained since start" would have no way to say how big the last action was,
 * which is the number the mean is built from.
 */
static void
xt_observe(int index, int xp, uint64_t now)
{
    struct XtSkill* skill;
    int gain;

    assert(index >= 0);
    assert(index < g_skill_count);

    skill = &g_skill[index];

    /* The first sight SEEDS. Every stat arrives at once on login. */
    if( skill->start_xp < 0 )
    {
        skill->start_xp = xp;
        skill->last_xp = xp;
        skill->last_change_ms = now;
        return;
    }
    if( xp < skill->last_xp )
    {
        /* Backwards is a different character's table, or a correction. Re-seed
         * rather than report a negative gain -- the reference clears its saved
         * state on exactly this condition. */
        skill->start_xp = xp;
        skill->last_xp = xp;
        skill->gained_before_reset = 0;
        skill->gained_since_reset = 0;
        return;
    }
    if( xp == skill->last_xp )
        return;

    gain = xp - skill->last_xp;
    skill->last_xp = xp;
    skill->gained_since_reset = xp - (skill->start_xp + skill->gained_before_reset);
    skill->actions++;
    skill->actions_since_reset++;
    skill->last_change_ms = now;
    skill->paused = false;

    skill->action_xp[skill->action_at] = gain;
    skill->action_at = (skill->action_at + 1) % XT_ACTION_HISTORY;
    if( skill->action_at == 0 )
        skill->action_history_full = true;
}

/**
 * The per-second half: accumulate training time, and apply the two timers.
 *
 * Separate from the poll above because both timers are about xp NOT arriving,
 * which no xp event can announce.
 */
static void
xt_tick_second(struct ToriRS_PluginCtx* ctx, uint64_t now, uint64_t delta_ms)
{
    int const pause_after_min = g_api->cfg_int(ctx, "pause_skill_after");
    int const reset_after_min = g_api->cfg_int(ctx, "reset_rate_after");

    assert(ctx);

    for( int i = 0; i < g_skill_count; i++ )
    {
        struct XtSkill* skill = &g_skill[i];
        uint64_t idle_ms;

        if( skill->start_xp < 0 )
            continue;
        idle_ms = now > skill->last_change_ms ? now - skill->last_change_ms : 0;

        if( pause_after_min > 0 && !skill->paused &&
            idle_ms >= (uint64_t)pause_after_min * 60u * 1000u )
            skill->paused = true;

        if( reset_after_min > 0 && skill->gained_since_reset > 0 &&
            idle_ms >= (uint64_t)reset_after_min * 60u * 1000u )
            xt_reset_rate(skill);

        /* A skill only accrues time while it is TRAINING: nothing gained since
         * the last reset means the rate is not measuring anything yet, and a
         * clock that ran anyway would drive every idle skill's xp/hr to zero. */
        if( skill->paused || skill->gained_since_reset <= 0 )
            continue;
        skill->skill_time_ms += delta_ms;
    }
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */

/** The saved-state file. One line per skill; see xt_state_save. */
#define XT_STATE_ASSET "session.txt"
#define XT_STATE_MAX 4096

/**
 * Write the session out so it survives a restart.
 *
 * Text, one `skill start_xp last_xp before since actions time_ms` line each,
 * because the alternative -- a packed struct -- is a file that silently means
 * something else the day the struct grows a field. The skill is written by
 * NAME for the same reason the item-stats table is keyed by one: the index is
 * stable within a revision and this client boots several.
 */
static void
xt_state_save(struct ToriRS_PluginCtx* ctx)
{
    char buf[XT_STATE_MAX];
    int at = 0;

    assert(ctx);
    if( !g_api->cfg_bool(ctx, "save_state") )
        return;

    for( int i = 0; i < g_skill_count && at < (int)sizeof(buf); i++ )
    {
        struct XtSkill const* skill = &g_skill[i];
        char const* name = g_api->skill_name(ctx, i);
        int written;

        if( skill->start_xp < 0 || xt_gained(skill) <= 0 || !name )
            continue;
        written = snprintf(
            buf + at, sizeof(buf) - (size_t)at, "%s %d %d %d %d %d %llu\n", name,
            skill->start_xp, skill->last_xp, skill->gained_before_reset,
            skill->gained_since_reset, skill->actions,
            (unsigned long long)skill->skill_time_ms);
        if( written <= 0 || written >= (int)sizeof(buf) - at )
            break;
        at += written;
    }
    g_api->asset_save(ctx, XT_STATE_ASSET, buf, at);
}

/** Index of the skill this client calls `name`, or -1. */
static int
xt_skill_by_name(struct ToriRS_PluginCtx* ctx, char const* name)
{
    assert(ctx);
    assert(name);

    for( int i = 0; i < g_skill_count; i++ )
    {
        char const* have = g_api->skill_name(ctx, i);
        if( have && strcmp(have, name) == 0 )
            return i;
    }
    return -1;
}

/**
 * Read the saved session back and reconcile it with the client's xp.
 *
 * The reconciliation is the whole point, and it is the reference's "offline
 * gains" handling: xp earned while this client was not running is not
 * something the session did, so the difference between the saved `last_xp` and
 * the live one is added to `start_xp` rather than reported as a gain. Without
 * it, logging back in after a night on another client opens the panel claiming
 * you just earned a million xp in no time at all.
 */
static void
xt_state_apply(struct ToriRS_PluginCtx* ctx)
{
    void const* data;
    int size = 0;
    char const* at;
    char const* end;

    assert(ctx);
    if( !g_api->cfg_bool(ctx, "save_state") )
        return;

    data = g_api->asset_data(ctx, XT_STATE_ASSET, &size);
    if( !data || size <= 0 )
        return;

    at = (char const*)data;
    end = at + size;
    while( at < end )
    {
        char const* line_end = memchr(at, '\n', (size_t)(end - at));
        char line[192];
        char name[64];
        int start_xp = 0;
        int last_xp = 0;
        int before = 0;
        int since = 0;
        int actions = 0;
        unsigned long long time_ms = 0;
        size_t len = line_end ? (size_t)(line_end - at) : (size_t)(end - at);
        int index;
        int live_xp = 0;

        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, at, len);
        line[len] = '\0';
        at = line_end ? line_end + 1 : end;

        if( sscanf(
                line, "%63s %d %d %d %d %d %llu", name, &start_xp, &last_xp, &before,
                &since, &actions, &time_ms) != 7 )
            continue;
        index = xt_skill_by_name(ctx, name);
        if( index < 0 )
            continue;
        if( !g_api->stat_xp(ctx, index, &live_xp, NULL, NULL) )
            continue;

        /* Gone BACKWARDS since the save: a different account. Nothing of this
         * session belongs to it, so the row is dropped rather than rebased. */
        if( live_xp < last_xp )
            continue;

        g_skill[index].start_xp = start_xp + (live_xp - last_xp);
        g_skill[index].last_xp = live_xp;
        g_skill[index].gained_before_reset = before;
        g_skill[index].gained_since_reset = since;
        g_skill[index].actions = actions;
        g_skill[index].actions_since_reset = 0;
        g_skill[index].skill_time_ms = time_ms;
        g_skill[index].last_change_ms = g_api->frame_ms(ctx);
    }
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

/** Widget id of the list row for `index`. */
static void
xt_row_id(int index, char* out, size_t out_size)
{
    assert(out);
    snprintf(out, out_size, "sk%d", index);
}

/** The skill a row id names, or -1 when the id is not a row. */
static int
xt_row_index(char const* id)
{
    int index;

    assert(id);
    if( sscanf(id, "sk%d", &index) != 1 )
        return -1;
    if( index < 0 || index >= g_skill_count )
        return -1;
    return index;
}

/** The one-line summary a skill's list row carries. */
static void
xt_row_text(int index, char* out, size_t out_size)
{
    struct XtSkill const* skill = &g_skill[index];
    char gained[24];
    char rate[24];

    assert(out);

    xt_short(xt_gained(skill), gained, sizeof(gained));
    xt_short(xt_hourly(skill, skill->gained_since_reset), rate, sizeof(rate));
    if( skill->paused )
        snprintf(out, out_size, "%s xp  (paused)", gained);
    else
        snprintf(out, out_size, "%s xp  %s/hr", gained, rate);
}

/** Rewrite every readout on the built page. Cheap: panel_set_text compares. */
static void
xt_page_refresh(struct ToriRS_PluginCtx* ctx)
{
    long long total_gained = 0;
    long long total_rate = 0;
    uint64_t const now = g_api->frame_ms(ctx);
    char text[96];
    char scratch[24];

    assert(ctx);
    if( !g_page_built )
        return;

    for( int i = 0; i < g_skill_count; i++ )
    {
        total_gained += xt_gained(&g_skill[i]);
        if( !g_skill[i].paused )
            total_rate += xt_hourly(&g_skill[i], g_skill[i].gained_since_reset);
    }

    xt_commas(total_gained, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_xp", text);
    xt_commas(total_rate, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_hr", text);
    xt_duration(
        (long long)((now - g_session_start_ms) / 1000u), text, sizeof(text));
    g_api->panel_set_text(ctx, "total_time", text);

    for( int i = 0; i < g_skill_count; i++ )
    {
        char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

        if( !g_built_rows[i] )
            continue;
        xt_row_id(i, id, sizeof(id));
        xt_row_text(i, text, sizeof(text));
        g_api->panel_set_text(ctx, id, text);
        g_api->panel_set_value(ctx, id, g_skill[i].paused ? 0 : 1);
    }

    if( g_detail >= 0 && g_detail < g_skill_count )
    {
        struct XtSkill const* skill = &g_skill[g_detail];
        int xp = 0;
        int level_xp = 0;
        int next_xp = 0;
        int mean;
        long long remaining;

        g_api->stat_xp(ctx, g_detail, &xp, &level_xp, &next_xp);
        remaining = next_xp > 0 ? next_xp - xp : 0;
        mean = xt_mean_action_xp(skill);

        /* next_xp is 0 at the top of the client's table -- level 99, which has
         * no next level to progress towards. A meter there reads FULL. */
        if( next_xp > level_xp )
            g_api->panel_set_value(
                ctx, "d_prog", (int)((long long)(xp - level_xp) * 100 / (next_xp - level_xp)));
        else
            g_api->panel_set_value(ctx, "d_prog", 100);

        xt_commas(xt_gained(skill), text, sizeof(text));
        g_api->panel_set_text(ctx, "d_gained", text);

        xt_commas(xt_hourly(skill, skill->gained_since_reset), text, sizeof(text));
        g_api->panel_set_text(ctx, "d_hr", text);

        if( next_xp > 0 )
            xt_commas(remaining, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "—");
        g_api->panel_set_text(ctx, "d_left", text);

        xt_commas(skill->actions, scratch, sizeof(scratch));
        snprintf(
            text, sizeof(text), "%s  (%lld/hr)", scratch,
            xt_hourly(skill, skill->actions_since_reset));
        g_api->panel_set_text(ctx, "d_actions", text);

        if( mean > 0 && next_xp > 0 )
            xt_commas((remaining + mean - 1) / mean, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "—");
        g_api->panel_set_text(ctx, "d_actleft", text);

        /* The reference's formula, and its two refusals: no time has been
         * measured, or nothing has been gained to measure a rate from. */
        if( skill->skill_time_ms >= XT_SECOND_MS && skill->gained_since_reset > 0 &&
            next_xp > 0 )
            xt_duration(
                (remaining * (long long)(skill->skill_time_ms / 1000u)) /
                    skill->gained_since_reset,
                text,
                sizeof(text));
        else
            snprintf(text, sizeof(text), "—");
        g_api->panel_set_text(ctx, "d_ttl", text);

        g_api->panel_set_text(ctx, "d_pause", skill->paused ? "Resume" : "Pause");
    }
}

/**
 * Declare the page.
 *
 * The dispatch is the whole declaration -- the host empties the model before
 * calling -- so this states the page it wants rather than the difference from
 * the page it had, and there is no path by which the two can disagree.
 */
static enum ToriRS_PluginVerdict
xt_panel_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    int rows = 0;

    assert(ctx);

    memset(g_built_rows, 0, sizeof(g_built_rows));

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_session", "Session");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_xp", "XP gained");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_hr", "XP/hr");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_time", "Session time");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "reset_all", "Reset all");

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_skills", "Skills");
    for( int i = 0; i < g_skill_count && rows < XT_ROWS_MAX; i++ )
    {
        char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
        char const* name = g_api->skill_name(ctx, i);

        if( !xt_row_wanted(ctx, i) || !name )
            continue;
        xt_row_id(i, id, sizeof(id));
        if( !g_api->panel_widget(ctx, TORIRS_PLUGIN_W_LIST_ROW, id, name) )
        {
            /* Said out loud: the alternative is a page quietly missing its
             * last few skills with nothing on screen to suggest they exist. */
            g_api->log(ctx, "xp-tracker: no room on the page for '%s' and what follows", name);
            break;
        }
        g_built_rows[i] = 1;
        rows++;
    }
    if( rows == 0 )
        g_api->panel_widget(
            ctx,
            TORIRS_PLUGIN_W_PARAGRAPH,
            "empty",
            "No XP gained yet this session.");

    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_skill_count && g_built_rows[g_detail] )
    {
        char const* name = g_api->skill_name(ctx, g_detail);

        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_detail", name);
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_PROGRESS, "d_prog", "Level progress");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_gained", "XP gained");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_hr", "XP/hr");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_left", "XP to level");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_actions", "Actions");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_actleft", "Actions to level");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_ttl", "Time to level");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_pause", "Pause");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_reset", "Reset skill");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_close", "Close details");
    }
    else
        g_built_detail = -1;

    g_page_built = true;
    xt_page_refresh(ctx);
    return TORIRS_PLUGIN_PASS;
}

/** Does the built page still show the rows the state now wants? */
static bool
xt_page_stale(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( !g_page_built )
        return false;
    if( g_built_detail != g_detail )
        return true;
    for( int i = 0; i < g_skill_count; i++ )
        if( (g_built_rows[i] != 0) != xt_row_wanted(ctx, i) )
            return true;
    return false;
}

static enum ToriRS_PluginVerdict
xt_panel_action(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelAction const* ev = event;
    int index;

    assert(ctx);
    assert(ev);
    assert(ev->id);

    if( strcmp(ev->id, "reset_all") == 0 )
    {
        xt_reset_all(ctx);
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    if( strcmp(ev->id, "d_close") == 0 )
    {
        g_detail = -1;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    if( g_detail >= 0 && strcmp(ev->id, "d_reset") == 0 )
    {
        xt_reset_skill(ctx, g_detail);
        g_detail = -1;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    if( g_detail >= 0 && strcmp(ev->id, "d_pause") == 0 )
    {
        g_skill[g_detail].paused = !g_skill[g_detail].paused;
        g_skill[g_detail].last_change_ms = g_api->frame_ms(ctx);
        xt_page_refresh(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    index = xt_row_index(ev->id);
    if( index < 0 )
        return TORIRS_PLUGIN_PASS;

    /*
     * A list row has two outcomes and the shell says which one fired: the row
     * itself opens the detail block, its switch pauses the skill. That split
     * is the reference's own -- a skill box there is expandable AND has a
     * pause -- and it costs no second control.
     */
    if( ev->action == TORIRS_PLUGIN_UI_TOGGLE )
    {
        g_skill[index].paused = ev->value == 0;
        g_skill[index].last_change_ms = g_api->frame_ms(ctx);
        xt_page_refresh(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    g_detail = g_detail == index ? -1 : index;
    g_api->panel_clear(ctx);
    return TORIRS_PLUGIN_PASS;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

static enum ToriRS_PluginVerdict
xt_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    struct ToriRS_PluginPanelDesc desc;

    assert(ctx);

    g_skill_count = 0;
    while( g_skill_count < XT_SKILLS_MAX && g_api->skill_name(ctx, g_skill_count) )
        g_skill_count++;
    assert(g_skill_count > 0);

    for( int i = 0; i < g_skill_count; i++ )
    {
        memset(&g_skill[i], 0, sizeof(g_skill[i]));
        g_skill[i].start_xp = -1;
    }
    memset(g_built_rows, 0, sizeof(g_built_rows));
    g_detail = -1;
    g_built_detail = -1;
    g_page_built = false;
    g_logged_in = false;
    g_session_start_ms = g_api->frame_ms(ctx);
    g_last_second_ms = g_session_start_ms;
    g_next_panel_ms = 0;

    memset(&desc, 0, sizeof(desc));
    desc.title = "XP Tracker";
    /* RuneLite's own, so a person who has used the plugin there recognises
     * the row here. @see script/plugins/assets/xp-tracker/panel_icon.txt. */
    desc.icon_asset = "panel_icon.png";
    desc.preferred_width = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
    g_api->panel_request(ctx, &desc);

    /* Queued, not read: the file crosses the IO queue like every other asset,
     * and the answer arrives at EV_ASSET. A load that is already resident
     * answers 1 and no event follows, so the apply has to happen here too. */
    if( g_api->cfg_bool(ctx, "save_state") &&
        g_api->asset_load(ctx, XT_STATE_ASSET) == 1 )
        xt_state_apply(ctx);

    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
xt_asset(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvAsset const* ev = event;

    assert(ctx);
    assert(ev);

    if( ev->ok && ev->name && strcmp(ev->name, XT_STATE_ASSET) == 0 )
    {
        xt_state_apply(ctx);
        g_api->panel_clear(ctx);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
xt_stop(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    xt_state_save(ctx);
    g_page_built = false;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
xt_tick(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    struct ToriRS_PluginPlayerSnap me;
    uint64_t const now = g_api->frame_ms(ctx);
    bool const logged_in = g_api->local_player(ctx, &me) != 0;

    assert(ctx);

    if( !logged_in )
    {
        /*
         * Logged out. The reference pauses every skill here when
         * `logoutPausing` is set, and the state is KEPT rather than reset: a
         * hop is not a new session, and the saved-state reconciliation on the
         * way back in is what decides whether the xp that appeared meanwhile
         * was yours.
         *
         * The poll and the per-second half are skipped -- there is no stat
         * table to read -- but the page REFRESH below is not, or the rows
         * would go on saying "training" for as long as the panel stayed open
         * on a logged-out client.
         */
        if( g_logged_in )
        {
            if( g_api->cfg_bool(ctx, "pause_on_logout") )
                for( int i = 0; i < g_skill_count; i++ )
                    g_skill[i].paused = true;
            xt_state_save(ctx);
            /* On the EDGE, not on the throttle: the moment the state changed
             * is the moment the page has to stop disagreeing with it. */
            g_next_panel_ms = 0;
        }
        g_logged_in = false;
        g_last_second_ms = now;
    }
    else
    {
        g_logged_in = true;

        for( int i = 0; i < g_skill_count; i++ )
        {
            int xp = 0;
            if( g_api->stat_xp(ctx, i, &xp, NULL, NULL) )
                xt_observe(i, xp, now);
        }

        if( now >= g_last_second_ms + XT_SECOND_MS )
        {
            xt_tick_second(ctx, now, now - g_last_second_ms);
            g_last_second_ms = now;
        }
    }

    if( now >= g_next_panel_ms )
    {
        g_next_panel_ms = now + XT_PANEL_REFRESH_MS;
        if( xt_page_stale(ctx) )
            g_api->panel_clear(ctx);
        else
            xt_page_refresh(ctx);
    }
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginConfigItem const XT_CONFIG[] = {
    { "save_state",        TORIRS_PLUGIN_CFG_BOOL, "Save between sessions",        "1", 0, 0,  NULL, 0 },
    { "hide_maxed",        TORIRS_PLUGIN_CFG_BOOL, "Hide maxed skills",            "0", 0, 0,  NULL, 0 },
    { "pause_on_logout",   TORIRS_PLUGIN_CFG_BOOL, "Pause on logout",              "1", 0, 0,  NULL, 0 },
    { "pause_skill_after", TORIRS_PLUGIN_CFG_INT,  "Auto pause after (minutes)",   "0", 0, 60, NULL, 0 },
    { "reset_rate_after",  TORIRS_PLUGIN_CFG_INT,  "Auto reset rate after (minutes)", "0", 0, 60, NULL, 0 },
    { NULL,                TORIRS_PLUGIN_CFG_BOOL, NULL,                           NULL, 0, 0, NULL, 0 },
};

static void
xt_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, xt_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, xt_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, xt_asset, NULL);
    /*
     * EV_LOGIC_TICK and not EV_SERVER_TICK, for xp-drop-orbs' reason:
     * EV_SERVER_TICK is raised from PKT_NAME_SERVER_TICK_END, which only
     * osrs230, osrs239 and the rsprot bridge carry, so a plugin polling there
     * sits doing nothing on half the lanes. This is the client's own 20ms
     * cycle and exists everywhere.
     */
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, xt_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_BUILD, xt_panel_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_ACTION, xt_panel_action, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER = {
    .name = "xp-tracker",
    .title = "XP Tracker",
    .version = "1.0.0",
    .priority = 0,
    .config = XT_CONFIG,
    .init = xt_init,
    .shutdown = NULL,
};
