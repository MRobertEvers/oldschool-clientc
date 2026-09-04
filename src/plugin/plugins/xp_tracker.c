#include "plugin/torirs_plugin_v2.h"

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
 * ---- native retained panel ----
 *
 * The page is semantic UI, not a picture. Session totals are key/value DOM
 * rows and every tracked skill is one retained action row whose summary is
 * patched in place. Selecting a skill replaces the overview with native
 * detail rows and buttons. A topology change (a skill appears or disappears)
 * rebuilds the page; ordinary XP/rate changes touch only that row's text.
 * This keeps the widget budget bounded while preserving browser selection,
 * accessibility, hit testing, and density-independent layout.
 *
 * It is a PAGE and not an overlay, which is the one structural difference from
 * the reference. RuneLite's plugin is two halves -- a side panel and a set of
 * opt-in canvas info boxes -- and this client already has the second half:
 * `xp-drop-orbs` (XP Globes) draws the on-canvas progress orb and the floating
 * drop. Writing a second thing that paints xp over the viewport would give the
 * player two readouts of one number, drawn by two plugins, that disagree the
 * first time either is configured. So this one owns the panel and nothing on
 * the canvas, and the two features remain separate.
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

/** Native skill rows the overview will declare. Kept below the panel's
 *  48-control budget even if future lanes add more skills. */
#define XT_ROWS_MAX 30

/** How often the page's numbers are rewritten, in ms. Every readout on it is
 *  derived from a clock, so it would otherwise be reformatted 50 times a
 *  second to say the same thing. */
#define XT_PANEL_REFRESH_MS 500

/** The per-second cadence the reference accumulates skill time on. */
#define XT_SECOND_MS 1000

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

struct XtState
{
    struct XtSkill skill[XT_SKILLS_MAX];
    int skill_count;
    int detail;
    char built_rows[XT_SKILLS_MAX];
    int built_detail;
    bool rows_truncated;
    bool built_rows_truncated;
    bool page_built;
    bool page_visible;
    bool state_applied;
    uint64_t last_second_ms;
    uint64_t session_start_ms;
    uint64_t next_panel_ms;
    bool logged_in;
    int row_skill[XT_SKILLS_MAX];
    int row_count;
};

#define g_skill (state->skill)
#define g_skill_count (state->skill_count)
#define g_detail (state->detail)
#define g_built_rows (state->built_rows)
#define g_built_detail (state->built_detail)
#define g_rows_truncated (state->rows_truncated)
#define g_built_rows_truncated (state->built_rows_truncated)
#define g_page_built (state->page_built)
#define g_page_visible (state->page_visible)
#define g_state_applied (state->state_applied)
#define g_last_second_ms (state->last_second_ms)
#define g_session_start_ms (state->session_start_ms)
#define g_next_panel_ms (state->next_panel_ms)
#define g_logged_in (state->logged_in)
#define g_row_skill (state->row_skill)
#define g_row_count (state->row_count)

static bool
xt_cfg_bool(struct ToriRS_ApiV2* api, char const* key)
{
    bool value = false;
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
xt_cfg_int(struct ToriRS_ApiV2* api, char const* key)
{
    int value = 0;
    (void)api->config.get_int(api, key, &value);
    return value;
}

static char const*
xt_cfg_string(struct ToriRS_ApiV2* api, char const* key)
{
    char const* value = "";
    (void)api->config.get_string(api, key, &value);
    return value ? value : "";
}

static bool
xt_skill_snapshot(
    struct ToriRS_ApiV2* api,
    int index,
    struct ToriRS_SkillSnapshot* out)
{
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    return api->game && api->game->skill(api, index, out);
}

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
 * A number the way the tracker's own `~torirs_text_format_compact_int`
 * (script 5377) writes one, branch for branch.
 *
 * Not "close enough to k/M": the script's cuts are on the DIGIT COUNT of the
 * decimal spelling, which is why 393,120 becomes "393.12k" with two decimals
 * and 3,145,400 becomes "3,145.4k" with one and a comma. A formatter that
 * picked a unit by magnitude and a fixed precision agrees with it at almost no
 * value, and the difference is legible in a column of them.
 *
 *   6 digits   spacer(d[0..3)) "." d[3..5) "k"
 *   7 digits   spacer(d[0..4)) "." d[4..5) "k"
 *   8 digits   spacer(d[0..2)) "." d[2..5) "M"
 *   more       "99.999M", flat
 *   fewer      spacer(n), which is where the thousands comma comes from
 *
 * Negative is "0" and not "-N": the script says so, and every caller here is a
 * total that cannot legitimately be one.
 */
static void
xt_compact(long long value, char* out, size_t out_size)
{
    char digits[24];
    char head[24];
    int len;

    assert(out);
    assert(out_size > 0);

    if( value < 0 )
    {
        snprintf(out, out_size, "0");
        return;
    }
    len = snprintf(digits, sizeof(digits), "%lld", value);
    if( len == 6 || len == 7 )
    {
        int const whole = len == 6 ? 3 : 4;
        int const frac = len == 6 ? 2 : 1;
        char tail[8];
        digits[whole + frac] = '\0';
        memcpy(tail, digits + whole, (size_t)frac);
        tail[frac] = '\0';
        digits[whole] = '\0';
        xt_commas(atoll(digits), head, sizeof(head));
        snprintf(out, out_size, "%s.%sk", head, tail);
        return;
    }
    if( len == 8 )
    {
        char tail[8];
        memcpy(tail, digits + 2, 3);
        tail[3] = '\0';
        digits[2] = '\0';
        xt_commas(atoll(digits), head, sizeof(head));
        snprintf(out, out_size, "%s.%sM", head, tail);
        return;
    }
    if( len > 8 )
    {
        snprintf(out, out_size, "99.999M");
        return;
    }
    xt_commas(value, out, out_size);
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
xt_reset_skill(struct ToriRS_ApiV2* api, struct XtState* state, int index)
{
    struct XtSkill* skill;
    struct ToriRS_SkillSnapshot snapshot;

    assert(index >= 0);
    assert(index < g_skill_count);

    skill = &g_skill[index];
    memset(skill, 0, sizeof(*skill));
    skill->start_xp = -1;
    if( xt_skill_snapshot(api, index, &snapshot) )
    {
        skill->start_xp = snapshot.xp;
        skill->last_xp = snapshot.xp;
    }
}


/** Is this skill worth a row? Trained at all, and not hidden by hide_maxed. */
static bool
xt_row_wanted(struct ToriRS_ApiV2* api, struct XtState* state, int index)
{
    struct ToriRS_SkillSnapshot snapshot;

    assert(index >= 0);
    assert(index < g_skill_count);

    if( xt_gained(&g_skill[index]) <= 0 )
        return false;
    if( !xt_cfg_bool(api, "hide_maxed") )
        return true;
    return !xt_skill_snapshot(api, index, &snapshot) || snapshot.base_level < 99;
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
xt_observe(struct XtState* state, int index, int xp, uint64_t now)
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
xt_tick_second(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    uint64_t now,
    uint64_t delta_ms)
{
    int const pause_after_min = xt_cfg_int(api, "pause_skill_after");
    int const reset_after_min = xt_cfg_int(api, "reset_rate_after");

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
xt_state_save(struct ToriRS_ApiV2* api, struct XtState* state)
{
    char buf[XT_STATE_MAX];
    int at = 0;

    if( !xt_cfg_bool(api, "save_state") )
        return;

    for( int i = 0; i < g_skill_count && at < (int)sizeof(buf); i++ )
    {
        struct XtSkill const* skill = &g_skill[i];
        struct ToriRS_SkillSnapshot snapshot;
        char const* name = xt_skill_snapshot(api, i, &snapshot) ? snapshot.name : NULL;
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
    (void)api->assets.save(api, XT_STATE_ASSET, buf, (size_t)at);
}

/** Index of the skill this client calls `name`, or -1. */
static int
xt_skill_by_name(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    char const* name)
{
    assert(name);

    for( int i = 0; i < g_skill_count; i++ )
    {
        struct ToriRS_SkillSnapshot snapshot;
        char const* have =
            xt_skill_snapshot(api, i, &snapshot) ? snapshot.name : NULL;
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
xt_state_apply(struct ToriRS_ApiV2* api, struct XtState* state)
{
    void const* data;
    size_t size = 0;
    char const* at;
    char const* end;

    if( !xt_cfg_bool(api, "save_state") )
        return;

    if( !api->assets.bytes(api, XT_STATE_ASSET, &data, &size) )
        return;
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
        index = xt_skill_by_name(api, state, name);
        if( index < 0 )
            continue;
        {
            struct ToriRS_SkillSnapshot snapshot;
            if( !xt_skill_snapshot(api, index, &snapshot) )
                continue;
            live_xp = snapshot.xp;
        }

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
        g_skill[index].last_change_ms = api->core.frame_ms(api);
    }
}

/**
 * True once the server has stated ANY skill.
 *
 * Sizing the table and having readings to put in it are two different moments:
 * the names come out of the cache and answer as soon as the client boots,
 * while the xp arrives with the login burst. In between, stat_xp answers "no
 * reading" for every skill -- which is what makes this the moment the saved
 * session can be reconciled, and the moment before which seeding one would be
 * seeding it from a fresh account's defaults.
 */
static bool
xt_stats_live(struct ToriRS_ApiV2* api, struct XtState* state)
{
    for( int i = 0; i < g_skill_count; i++ )
    {
        struct ToriRS_SkillSnapshot snapshot;
        if( xt_skill_snapshot(api, i, &snapshot) )
            return true;
    }
    return false;
}

/** Which of RuneLite's XpPanelLabel values a stat slot is showing. */
enum XtLabel
{
    XT_LABEL_XP_GAINED = 0,
    XT_LABEL_XP_HOUR,
    XT_LABEL_XP_LEFT,
    XT_LABEL_ACTIONS_DONE,
    XT_LABEL_ACTIONS_HOUR,
    XT_LABEL_ACTIONS_LEFT,
    XT_LABEL_TIME_TO_LEVEL,
    XT_LABEL_COUNT
};

/** The choice list, and the KEY each choice prints. The reference's own
 *  spellings, so a person who has used it recognises the row. */
static char const* const XT_LABEL_CHOICES =
    "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL";
/** Compact captions for the native action-row summary. */
static char const* const XT_LABEL_SHORT[XT_LABEL_COUNT] = {
    "XP", "XP/hr", "XP left", "Actions", "Actions/hr", "Actions left", "TTL"
};

/** Read a label slot out of the config, by its choice text. */
static int
xt_label_slot(struct ToriRS_ApiV2* api, char const* key, int fallback)
{
    char const* value = xt_cfg_string(api, key);
    char const* at = XT_LABEL_CHOICES;

    if( !value || !value[0] )
        return fallback;
    for( int index = 0; index < XT_LABEL_COUNT; index++ )
    {
        char const* end = strchr(at, '|');
        size_t const len = end ? (size_t)(end - at) : strlen(at);

        if( strlen(value) == len && strncmp(at, value, len) == 0 )
            return index;
        if( !end )
            break;
        at = end + 1;
    }
    return fallback;
}

/** One stat slot's VALUE for one skill. */
static void
xt_label_value(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    int skill,
    int which,
    char* out,
    size_t out_size)
{
    struct XtSkill const* s = &g_skill[skill];
    struct ToriRS_SkillSnapshot snapshot;
    int xp;
    int next_xp;
    long long remaining;
    int mean;

    assert(out);

    if( !xt_skill_snapshot(api, skill, &snapshot) )
        memset(&snapshot, 0, sizeof(snapshot));
    xp = snapshot.xp;
    next_xp = snapshot.next_level_xp;
    remaining = next_xp > 0 ? next_xp - xp : 0;
    mean = xt_mean_action_xp(s);

    switch( which )
    {
    case XT_LABEL_XP_HOUR:
        xt_compact(xt_hourly(s, s->gained_since_reset), out, out_size);
        break;
    case XT_LABEL_XP_LEFT:
        if( next_xp > 0 )
            xt_compact(remaining, out, out_size);
        else
            snprintf(out, out_size, "-");
        break;
    case XT_LABEL_ACTIONS_DONE:
        xt_compact(s->actions, out, out_size);
        break;
    case XT_LABEL_ACTIONS_HOUR:
        xt_compact(xt_hourly(s, s->actions_since_reset), out, out_size);
        break;
    case XT_LABEL_ACTIONS_LEFT:
        /* Unknown until ten actions have been seen -- @see xt_mean_action_xp,
         * where refusing to answer is the point. */
        if( mean > 0 && next_xp > 0 )
            xt_compact((remaining + mean - 1) / mean, out, out_size);
        else
            snprintf(out, out_size, "-");
        break;
    case XT_LABEL_TIME_TO_LEVEL:
        if( s->skill_time_ms >= XT_SECOND_MS && s->gained_since_reset > 0 && next_xp > 0 )
            xt_duration(
                (remaining * (long long)(s->skill_time_ms / 1000u)) /
                    s->gained_since_reset,
                out,
                out_size);
        else
            snprintf(out, out_size, "-");
        break;
    case XT_LABEL_XP_GAINED:
    default:
        xt_compact(xt_gained(s), out, out_size);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */
/** Which skills get a native action row, in stats-tab order. */
static void
xt_collect_rows(struct ToriRS_ApiV2* api, struct XtState* state)
{
    g_row_count = 0;
    g_rows_truncated = false;
    for( int i = 0; i < g_skill_count; i++ )
        if( xt_row_wanted(api, state, i) )
        {
            if( g_row_count < XT_ROWS_MAX )
                g_row_skill[g_row_count++] = i;
            else
                g_rows_truncated = true;
        }
}

/** The four label slots the user chose, in reading order. */
static void
xt_slots(struct ToriRS_ApiV2* api, int out[4])
{
    /*
     * The cache's own pairing, and the ORDER is the half that was wrong:
     * script5366 puts "XP/Hr: <br>XP>Lvl: " in the LEFT column and
     * "  XP Gained: <br>  Acts>Lvl: " in the right one. Defaulting the left
     * slots to gained/actions swapped every row against the tracker it copies.
     */
    out[0] = xt_label_slot(api, "label_top_left", XT_LABEL_XP_HOUR);
    out[1] = xt_label_slot(api, "label_top_right", XT_LABEL_XP_GAINED);
    out[2] = xt_label_slot(api, "label_bottom_left", XT_LABEL_XP_LEFT);
    out[3] = xt_label_slot(api, "label_bottom_right", XT_LABEL_ACTIONS_LEFT);
}

/** Format the four configured statistics for one retained native row. */
static void
xt_row_summary(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    int skill,
    char* out,
    size_t out_size)
{
    int slot[4];
    char value[4][24];

    xt_slots(api, slot);
    for( int i = 0; i < 4; i++ )
        xt_label_value(api, state, skill, slot[i], value[i], sizeof(value[i]));
    snprintf(
        out,
        out_size,
        "%s %s \xc2\xb7 %s %s \xc2\xb7 %s %s \xc2\xb7 %s %s",
        XT_LABEL_SHORT[slot[0]], value[0],
        XT_LABEL_SHORT[slot[1]], value[1],
        XT_LABEL_SHORT[slot[2]], value[2],
        XT_LABEL_SHORT[slot[3]], value[3]);
}

/** Patch live text on the already-retained native DOM rows. */
static void
xt_page_refresh(struct ToriRS_ApiV2* api, struct XtState* state)
{
    long long total_gained = 0;
    long long total_rate = 0;
    char text[192];

    if( !g_page_built )
        return;

    for( int i = 0; i < g_skill_count; i++ )
    {
        total_gained += xt_gained(&g_skill[i]);
        if( !g_skill[i].paused )
            total_rate += xt_hourly(&g_skill[i], g_skill[i].gained_since_reset);
    }

    if( g_detail >= 0 && g_detail < g_skill_count )
    {
        struct XtSkill const* skill = &g_skill[g_detail];
        char scratch[24];
        struct ToriRS_SkillSnapshot snapshot;
        int xp;
        int next_xp;
        long long remaining;
        int mean;

        if( !xt_skill_snapshot(api, g_detail, &snapshot) )
            memset(&snapshot, 0, sizeof(snapshot));
        xp = snapshot.xp;
        next_xp = snapshot.next_level_xp;
        remaining = next_xp > 0 ? next_xp - xp : 0;
        mean = xt_mean_action_xp(skill);

        {
            int percent = 100;

            if( next_xp > snapshot.level_xp )
                percent = (int)(((long long)(xp - snapshot.level_xp) * 100) /
                                (next_xp - snapshot.level_xp));
            if( percent < 0 ) percent = 0;
            if( percent > 100 ) percent = 100;
            if( next_xp > 0 )
                snprintf(
                    text, sizeof(text), "Level %d to %d",
                    snapshot.base_level, snapshot.base_level + 1);
            else
                snprintf(text, sizeof(text), "Level %d", snapshot.base_level);
            (void)api->panel.set_text(api, "d_progress", text);
            (void)api->panel.set_value(api, "d_progress", percent);
        }

        (void)api->panel.set_text(
            api, "sec_detail", snapshot.name[0] ? snapshot.name : "?");
        (void)api->panel.set_text(
            api, "d_status", skill->paused ? "Paused" : "Tracking");
        xt_commas(xt_gained(skill), text, sizeof(text));
        (void)api->panel.set_text(api, "d_gained", text);
        xt_commas(xt_hourly(skill, skill->gained_since_reset), text, sizeof(text));
        (void)api->panel.set_text(api, "d_hr", text);

        if( next_xp > 0 )
            xt_commas(remaining, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "\xe2\x80\x94");
        (void)api->panel.set_text(api, "d_left", text);

        xt_commas(skill->actions, scratch, sizeof(scratch));
        snprintf(
            text, sizeof(text), "%s  (%lld/hr)", scratch,
            xt_hourly(skill, skill->actions_since_reset));
        (void)api->panel.set_text(api, "d_actions", text);

        if( mean > 0 && next_xp > 0 )
            xt_commas((remaining + mean - 1) / mean, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "\xe2\x80\x94");
        (void)api->panel.set_text(api, "d_actleft", text);

        if( skill->skill_time_ms >= XT_SECOND_MS && skill->gained_since_reset > 0 &&
            next_xp > 0 )
            xt_duration(
                (remaining * (long long)(skill->skill_time_ms / 1000u)) /
                    skill->gained_since_reset,
                text,
                sizeof(text));
        else
            snprintf(text, sizeof(text), "\xe2\x80\x94");
        (void)api->panel.set_text(api, "d_ttl", text);

        return;
    }

    xt_commas(total_rate, text, sizeof(text));
    (void)api->panel.set_text(api, "total_rate", text);
    xt_commas(total_gained, text, sizeof(text));
    (void)api->panel.set_text(api, "total_gained", text);

    for( int i = 0; i < g_row_count; i++ )
    {
        char id[32];
        int const skill = g_row_skill[i];

        if( skill < 0 || skill >= g_skill_count || !g_built_rows[skill] )
            continue;
        snprintf(id, sizeof(id), "skill_%d", skill);
        xt_row_summary(api, state, skill, text, sizeof(text));
        (void)api->panel.set_text(api, id, text);
    }
}

/**
 * Declare the page.
 *
 * The dispatch is the whole declaration -- the host empties the model before
 * calling -- so this states the page it wants rather than the difference from
 * the page it had.
 */
static void
xt_panel_build(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct XtState* state = plugin_state;

    if( view != TORIRS_PANEL_VIEW_PAGE )
    {
        g_page_built = false;
        return;
    }

    xt_collect_rows(api, state);
    memset(g_built_rows, 0, sizeof(g_built_rows));
    g_built_rows_truncated = g_rows_truncated;

    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_skill_count &&
        xt_row_wanted(api, state, g_detail) )
    {
        struct ToriRS_SkillSnapshot snapshot;
        struct ToriRS_PanelNode heading;
        char const* name =
            xt_skill_snapshot(api, g_detail, &snapshot) ? snapshot.name : "?";

        panel->button(panel, "d_back", "Back to skills", true);
        memset(&heading, 0, sizeof(heading));
        heading.struct_size = sizeof(heading);
        heading.kind = TORIRS_PANEL_HEADING;
        heading.id = "sec_detail";
        heading.text = name;
        (void)panel->node(panel, &heading);
        panel->key_value(panel, "d_status", "Status", "");
        {
            struct ToriRS_PanelNode progress;

            memset(&progress, 0, sizeof(progress));
            progress.struct_size = sizeof(progress);
            progress.kind = TORIRS_PANEL_PROGRESS;
            progress.id = "d_progress";
            progress.label = "Level progress";
            (void)panel->node(panel, &progress);
        }
        panel->key_value(panel, "d_gained", "XP gained", "");
        panel->key_value(panel, "d_hr", "XP/hr", "");
        panel->key_value(panel, "d_left", "XP to level", "");
        panel->key_value(panel, "d_actions", "Actions", "");
        panel->key_value(panel, "d_actleft", "Actions to level", "");
        panel->key_value(panel, "d_ttl", "Time to level", "");
        panel->button(panel, "d_pause", "Pause / resume", true);
        panel->button(panel, "d_reset", "Reset", true);
        panel->button(panel, "d_reset_others", "Reset others", true);
        panel->button(panel, "d_reset_rate", "Reset/hr", true);
    }
    else
    {
        char text[192];

        g_built_detail = -1;
        panel->heading(panel, "Session");
        panel->key_value(panel, "total_rate", "Total XP/hr", "0");
        panel->key_value(panel, "total_gained", "Total XP gained", "0");
        if( g_row_count == 0 )
            panel->paragraph(panel, "No XP gained this session.");
        for( int i = 0; i < g_row_count; i++ )
        {
            struct ToriRS_SkillSnapshot snapshot;
            char id[32];
            int const skill = g_row_skill[i];
            char const* name =
                xt_skill_snapshot(api, skill, &snapshot) ? snapshot.name : "Skill";

            snprintf(id, sizeof(id), "skill_%d", skill);
            xt_row_summary(api, state, skill, text, sizeof(text));
            panel->action_row(panel, id, name, text);
            if( skill >= 0 && skill < XT_SKILLS_MAX )
                g_built_rows[skill] = 1;
        }
        if( g_rows_truncated )
            panel->paragraph(
                panel,
                "More skills are tracked than fit here; hide maxed skills in Settings.");
    }

    g_page_built = true;
    xt_page_refresh(api, state);
}

/** Does the retained page need a structural rebuild? Value-only changes are
 * patched by xt_page_refresh and never redeclare unrelated DOM nodes. */
static bool
xt_page_stale(struct ToriRS_ApiV2* api, struct XtState* state)
{
    if( !g_page_built )
        return false;
    xt_collect_rows(api, state);
    if( g_detail >= 0 &&
        (g_detail >= g_skill_count || !xt_row_wanted(api, state, g_detail)) )
        g_detail = -1;
    if( g_built_detail != g_detail )
        return true;
    if( g_detail >= 0 )
        return false;
    if( g_built_rows_truncated != g_rows_truncated )
        return true;
    {
        char wanted[XT_SKILLS_MAX] = { 0 };
        for( int i = 0; i < g_row_count; i++ )
            if( g_row_skill[i] >= 0 && g_row_skill[i] < XT_SKILLS_MAX )
                wanted[g_row_skill[i]] = 1;
        for( int i = 0; i < g_skill_count; i++ )
            if( (g_built_rows[i] != 0) != (wanted[i] != 0) )
                return true;
    }
    return false;
}

/** The shell moved, showed or hid this page. */
static void
xt_panel_layout(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_PanelLayoutEvent const* ev)
{
    struct XtState* state = plugin_state;
    assert(ev);

    g_page_visible = ev->visible;
    if( g_page_visible )
        xt_page_refresh(api, state);
}

/** A native action row or detail control on the page. */
static void
xt_panel_action(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_PanelActionEvent const* ev)
{
    struct XtState* state = plugin_state;
    assert(ev);
    assert(ev->id);

    if( strcmp(ev->id, "d_back") == 0 )
    {
        g_detail = -1;
        api->panel.invalidate(api);
        return;
    }
    if( strncmp(ev->id, "skill_", 6) == 0 )
    {
        char* end = NULL;
        long const skill = strtol(ev->id + 6, &end, 10);

        if( end && !*end && skill >= 0 && skill < g_skill_count &&
            xt_row_wanted(api, state, (int)skill) )
        {
            g_detail = (int)skill;
            api->panel.invalidate(api);
        }
        return;
    }

    if( g_detail < 0 || g_detail >= g_skill_count )
        return;

    if( strcmp(ev->id, "d_pause") == 0 )
    {
        g_skill[g_detail].paused = !g_skill[g_detail].paused;
        g_skill[g_detail].last_change_ms = api->core.frame_ms(api);
        xt_page_refresh(api, state);
        return;
    }
    if( strcmp(ev->id, "d_reset") == 0 )
    {
        xt_reset_skill(api, state, g_detail);
        g_detail = -1;
        api->panel.invalidate(api);
        return;
    }
    if( strcmp(ev->id, "d_reset_others") == 0 )
    {
        /* The reference's "Reset others": everything BUT this one, which is
         * how a person keeps the skill they are training and clears the noise
         * a trip picked up around it. */
        for( int i = 0; i < g_skill_count; i++ )
            if( i != g_detail )
                xt_reset_skill(api, state, i);
        api->panel.invalidate(api);
        return;
    }
    if( strcmp(ev->id, "d_reset_rate") == 0 )
    {
        /* Only the per-hour figures, keeping the session total -- @see
         * xt_reset_rate, which is XpStateSingle::resetPerHour. */
        xt_reset_rate(&g_skill[g_detail]);
        xt_page_refresh(api, state);
        return;
    }
}

static void
xt_start(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct XtState* state = plugin_state;
    struct ToriRS_PanelDescriptor desc;

    memset(state, 0, sizeof(*state));
    g_detail = -1;
    g_built_detail = -1;
    g_skill_count = 0;
    memset(g_built_rows, 0, sizeof(g_built_rows));
    g_detail = -1;
    g_built_detail = -1;
    g_page_built = false;
    g_page_visible = false;
    g_state_applied = false;
    g_logged_in = false;
    g_session_start_ms = api->core.frame_ms(api);
    g_last_second_ms = g_session_start_ms;
    g_next_panel_ms = 0;

    memset(&desc, 0, sizeof(desc));
    /* RuneLite's own, so a person who has used the plugin there recognises
     * the row here. @see script/plugins/assets/xp-tracker/panel_icon.txt. */
    desc.icon_asset = "panel_icon.png";
    desc.preferred_width = TORIRS_PANEL_WIDTH_DEFAULT;
    (void)api->panel.request(api, &desc);

    /* Queued, not read: the file crosses the IO queue like every other asset,
     * and the answer arrives at on_asset. A load that is already resident
     * answers 1 and no event follows, so the apply has to happen here too. */
    /* Requested here and applied by the sizer: the file crosses the IO queue
     * and the stat table does not exist yet either. */
    if( xt_cfg_bool(api, "save_state") )
        (void)api->assets.request(api, XT_STATE_ASSET);
}

static void
xt_asset(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_AssetEvent const* ev)
{
    struct XtState* state = plugin_state;
    assert(ev);

    /* Only once there are READINGS to reconcile onto; otherwise the tick does
     * it the moment there are. @see xt_stats_live. */
    if( ev->ok && ev->name && strcmp(ev->name, XT_STATE_ASSET) == 0 &&
        !g_state_applied && g_skill_count > 0 && xt_stats_live(api, state) )
    {
        g_state_applied = true;
        xt_state_apply(api, state);
        api->panel.invalidate(api);
    }
}

static void
xt_stop(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct XtState* state = plugin_state;
    xt_state_save(api, state);
    g_page_built = false;
    api->assets.release(api, XT_STATE_ASSET);
    memset(state, 0, sizeof(*state));
}

/**
 * Size the skill table, the first time the client can answer.
 *
 * NOT at on_start, and that is the whole point: a plugin starts when the
 * client boots, and the stat table does not exist until a session has one --
 * so `skill_name` answers NULL for every index and a table sized there is
 * sized to ZERO, permanently, for a plugin whose every loop runs to
 * g_skill_count. The assert that was supposed to catch it is compiled out of a
 * release build, so the symptom is not a crash: it is a tracker that quietly
 * never tracks anything for the whole session.
 *
 * Lazy and idempotent, which is how xp-drop-orbs sizes the same table.
 */
static void
xt_size_table(struct ToriRS_ApiV2* api, struct XtState* state)
{
    int count = 0;
    struct ToriRS_SkillSnapshot snapshot;

    if( g_skill_count > 0 )
        return;
    while( count < XT_SKILLS_MAX && xt_skill_snapshot(api, count, &snapshot) )
        count++;
    if( count == 0 )
        return; /* no session yet; ask again next tick */

    g_skill_count = count;
    for( int i = 0; i < g_skill_count; i++ )
    {
        memset(&g_skill[i], 0, sizeof(g_skill[i]));
        g_skill[i].start_xp = -1;
    }
    g_session_start_ms = api->core.frame_ms(api);
}

static void
xt_tick(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_TickEvent const* event)
{
    struct XtState* state = plugin_state;
    (void)event;

    struct ToriRS_PlayerSnapshot me;
    uint64_t const now = api->core.frame_ms(api);
    bool const logged_in = api->world.local_player(api, &me);

    xt_size_table(api, state);
    if( g_skill_count == 0 )
        return;

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
            if( xt_cfg_bool(api, "pause_on_logout") )
                for( int i = 0; i < g_skill_count; i++ )
                    g_skill[i].paused = true;
            xt_state_save(api, state);
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

        /* BEFORE the poll: the saved session's start_xp is the one this
         * session runs on, and a seed taken first would be the one it kept. */
        if( !g_state_applied && xt_stats_live(api, state) )
        {
            g_state_applied = true;
            xt_state_apply(api, state);
            api->panel.invalidate(api);
        }

        for( int i = 0; i < g_skill_count; i++ )
        {
            struct ToriRS_SkillSnapshot snapshot;
            if( xt_skill_snapshot(api, i, &snapshot) )
                xt_observe(state, i, snapshot.xp, now);
        }

        if( now >= g_last_second_ms + XT_SECOND_MS )
        {
            xt_tick_second(api, state, now, now - g_last_second_ms);
            g_last_second_ms = now;
        }
    }

    if( g_page_visible && now >= g_next_panel_ms )
    {
        g_next_panel_ms = now + XT_PANEL_REFRESH_MS;
        if( xt_page_stale(api, state) )
            api->panel.invalidate(api);
        else
            xt_page_refresh(api, state);
    }
}

static struct ToriRS_ConfigItem const XT_CONFIG[] = {
    { "save_state",        TORIRS_CONFIG_BOOL, "Save between sessions",        "1", 0, 0,  NULL, 0 },
    { "hide_maxed",        TORIRS_CONFIG_BOOL, "Hide maxed skills",            "0", 0, 0,  NULL, 0 },
    { "pause_on_logout",   TORIRS_CONFIG_BOOL, "Pause on logout",              "1", 0, 0,  NULL, 0 },
    { "pause_skill_after", TORIRS_CONFIG_INT,  "Auto pause after (minutes)",   "0", 0, 60, NULL, 0 },
    { "reset_rate_after",  TORIRS_CONFIG_INT,  "Auto reset rate after (minutes)", "0", 0, 60, NULL, 0 },
    /* The four values shown in each action-row summary, in reading order, and
     * RuneLite's own XpPanelLabel choices. */
    { "label_top_left",     TORIRS_CONFIG_ENUM, "Top-left stat",     "XP/hr",     0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_top_right",    TORIRS_CONFIG_ENUM, "Top-right stat",    "XP Gained", 0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_bottom_left",  TORIRS_CONFIG_ENUM, "Bottom-left stat",  "XP Left",   0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_bottom_right", TORIRS_CONFIG_ENUM, "Bottom-right stat", "Actions",   0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { NULL,                TORIRS_CONFIG_BOOL, NULL,                           NULL, 0, 0, NULL, 0 },
};

static struct ToriRS_ConfigSchema const XT_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = XT_CONFIG,
};

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_TRACKER = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
    .id = "xp-tracker",
    .title = "XP Tracker",
    .version = "2.0.0",
    .state_size = sizeof(struct XtState),
    .config = &XT_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = xt_start,
        .on_stop = xt_stop,
        .on_asset = xt_asset,
        .on_logic_tick = xt_tick,
        .on_ui_build = xt_panel_build,
        .on_ui_action = xt_panel_action,
        .on_ui_layout = xt_panel_layout,
    },
};
