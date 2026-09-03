#include "plugin/plugins/plugin_draw.h"
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
 * ---- the look is the CACHE's, not invented here ----
 *
 * This client already has an XP tracker: the CS2 that builds interface 729
 * (`xptracker`) draws one box per skill, and every colour and measurement
 * below was read out of it rather than chosen. To re-derive:
 *
 *     3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 \
 *         --rev osrs239 --out /tmp/cs2xp 5362 5363 5364 5365 5366 5370 5371
 *
 * What those state, and what this draws:
 *
 *   script5364  the BOX -- a filled rect at `cc_settrans(128)` with an
 *               unfilled rect over it for the border, 48 tall on a 50 pitch
 *               (`%varcint562 = row * (48 + 2)`).
 *   script5363  the skill ICON, 25x25 at x=3, out of `enum(stat, graphic,
 *               enum_255, stat)`.
 *   script5366  the STATS, a 2x2 grid anchored to the box's RIGHT edge, in
 *               fontmetrics_494 with a shadow: keys 0xcccccc, values white,
 *               12px line height, the pairs being XP Gained / XP/Hr and
 *               Acts>Lvl / XP>Lvl.
 *   script5365  the BAR: track 0x002200 and fill 0x006600, 15 tall, under the
 *               stats at y+27, with three labels over it -- the level at the
 *               left and the goal at the right in 0xcccccc, and the percentage
 *               centred in white.
 *   script5370  the fill's width, and 0x885500 across the WHOLE bar when the
 *               goal is met rather than a green bar that happens to be full.
 *   script5371  that centre label: "12.34%" to two decimals, "Paused." while
 *               paused, "Done!" when the goal is met.
 *
 * The reference's own info box is the same shape -- an icon, four corner
 * stats, a bar with a level at each end -- which is why the two could be put
 * together at all: RuneLite's four configurable label slots are exactly the
 * 2x2 grid the CS2 lays out, so the config below offers its choices and the
 * layout stays the cache's.
 *
 * ---- drawn as ONE composed image ----
 *
 * The whole list is rasterised into a single ARGB buffer and blitted into one
 * panel drawing well, which is xp-drop-orbs' and item-stats' pattern and is
 * here for their reason: api->draw_text is the client's chunky hitsplat face
 * with no way to measure a string, so a plugin that wants the game's own
 * caption face ships a baked atlas of it and sets text itself.
 *
 * It is also what makes the budget work. The panel gives a plugin 48 controls;
 * a box built out of them would be seven each and would run out at the seventh
 * skill. One well is one control however many skills are being tracked.
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

/* ------------------------------------------------------------------------ */
/* The cache's own measurements and palette                                  */
/*                                                                           */
/* Every number here is script5363..5371's; @see the file comment.           */
/* ------------------------------------------------------------------------ */

/** One box, and the gap under it. `%varcint562 = row * (48 + 2)`. */
#define XT_BOX_H 48
#define XT_BOX_GAP 2
#define XT_BOX_PITCH (XT_BOX_H + XT_BOX_GAP)
/** The skill icon: 25x25 at x=3, y=+3. */
#define XT_ICON 25
#define XT_ICON_X 3
/** The bar: 15 tall, at `2 + 25` down the box. */
#define XT_BAR_H 15
#define XT_BAR_Y (XT_BOX_GAP + XT_ICON)
/** The stat grid's line box. */
#define XT_LINE_H 12
#define XT_PAD 4
/**
 * The grid's own inset and top, as script5366 states them: `$int7 = 2 * 2` is
 * the right-edge inset every column is anchored from, and `$y8 = row*50 + 4`
 * puts the first line four down from the box.
 */
#define XT_GRID_PAD 4
#define XT_GRID_Y 4

/* ---- the OVERVIEW box (torirs_xptracker_total_labels, script 5367) --------
 *
 * The same 48-tall box as a skill's, with no bar and no icon strip: the key
 * column starts at 31 (which is what leaves the tracker's own 25px icon its
 * margins) and both lines are CENTRED in the box rather than topped, which is
 * the one place this list's vertical rhythm differs from a skill row's.
 */
#define XT_OVER_KEY_X 31
#define XT_OVER_ICON 25
#define XT_OVER_ICON_X 3

/** The box: a half-transparent black wash under a black border. */
#define XT_BOX_FILL 0x000000u
#define XT_BOX_FILL_ALPHA 128
#define XT_BOX_BORDER 0x2E2B25u
/** The bar, exactly as script5365/5370 set it. */
#define XT_BAR_TRACK 0x002200u
#define XT_BAR_FILL 0x006600u
#define XT_BAR_DONE 0x885500u
/** Text: values white, keys and the bar's end labels 0xcccccc. */
#define XT_INK_VALUE 0xFFFFFFu
#define XT_INK_KEY 0xCCCCCCu
/** The selected box is lifted rather than recoloured, so the palette stays the
 *  cache's and the selection is still unmistakable. */
#define XT_BOX_SELECTED 0x5A5442u

/* ------------------------------------------------------------ the glyph atlas */

/* The kit's text verbs take the face as an argument; every label on this page
 * is set in the one the CS2 sets its own in, so the calls name it once here. */
#define PLUGIN_DRAW_TEXT(buf, w, h, x, top, text, tint)                                  \
    PluginDraw_TextV2((buf), (w), (h), (x), (top), &g_font, (text), (tint))
#define PLUGIN_DRAW_TEXT_RIGHT(buf, w, h, right, top, text, tint)                        \
    PluginDraw_TextRightV2((buf), (w), (h), (right), (top), &g_font, (text), (tint))

/** The face every label is set in, and the 25x25 icon strip. */
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
    struct PluginDraw_AtlasV2 font;
    struct ToriRS_ImageRef img_skills;
    struct ToriRS_ImageRef img_over;
    uint32_t* over_px;
    int over_w;
    int over_h;
    uint32_t* skill_px;
    int skill_w;
    int skill_h;
    struct XtSkill skill[XT_SKILLS_MAX];
    int skill_count;
    int detail;
    char built_rows[XT_SKILLS_MAX];
    int built_detail;
    bool page_built;
    bool page_visible;
    bool state_applied;
    uint64_t last_second_ms;
    uint64_t session_start_ms;
    uint64_t next_panel_ms;
    bool logged_in;
    int box_skill[XT_SKILLS_MAX];
    int box_count;
    uint32_t* compose;
    int compose_w;
    int compose_h;
    int well_w;
    uint64_t compose_key;
    struct ToriRS_ImageRef compose_image;
};

#define g_font (state->font)
#define g_img_skills (state->img_skills)
#define g_img_over (state->img_over)
#define g_over_px (state->over_px)
#define g_over_w (state->over_w)
#define g_over_h (state->over_h)
#define g_skill_px (state->skill_px)
#define g_skill_w (state->skill_w)
#define g_skill_h (state->skill_h)
#define g_skill (state->skill)
#define g_skill_count (state->skill_count)
#define g_detail (state->detail)
#define g_built_rows (state->built_rows)
#define g_built_detail (state->built_detail)
#define g_page_built (state->page_built)
#define g_page_visible (state->page_visible)
#define g_state_applied (state->state_applied)
#define g_last_second_ms (state->last_second_ms)
#define g_session_start_ms (state->session_start_ms)
#define g_next_panel_ms (state->next_panel_ms)
#define g_logged_in (state->logged_in)
#define g_box_skill (state->box_skill)
#define g_box_count (state->box_count)
#define g_compose (state->compose)
#define g_compose_w (state->compose_w)
#define g_compose_h (state->compose_h)
#define g_well_w (state->well_w)
#define g_compose_key (state->compose_key)

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

/* ------------------------------------------------------------------------ */
/* The boxes                                                                 */
/* ------------------------------------------------------------------------ */

/** Everything the compose needs, resident. */
static int
xt_art_ready(struct ToriRS_ApiV2* api, struct XtState* state)
{
    if( !PluginDraw_AtlasLoadV2(api, &g_font, "text") )
        return 0;
    if( !PluginDraw_ImageLoadV2(
            api, "skills.png", &g_img_skills, &g_skill_px, &g_skill_w, &g_skill_h) )
        return 0;
    /* The overview icon is wanted but not REQUIRED: the box is two lines of
     * text and a picture, and a missing picture is a box with a gap in it
     * rather than a page that refuses to draw. */
    (void)PluginDraw_ImageLoadV2(
        api, "panel_icon.png", &g_img_over, &g_over_px, &g_over_w, &g_over_h);
    return 1;
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
static char const* const XT_LABEL_KEY[XT_LABEL_COUNT] = {
    "XP Gained: ", "XP/Hr: ", "XP>Lvl: ", "Actions: ", "Acts/Hr: ", "Kills>Lvl: ", "TTL: "
};

/*
 * The RIGHT column's keys carry two leading spaces and the left column's do
 * not, and that is not a typo: it is the gutter, and the cache authors it
 * exactly this way. `xptracker_build_components_5366` measures its two key
 * columns with `parawidth("XP>Goal: ")` and `parawidth("  XP Gained: ")`, so
 * the space between the left pair's value and the right pair's key IS those
 * two spaces inside the right key's own width. Laying the columns out from
 * measured widths without them butts the two pairs together, which is what the
 * first pass did.
 */
#define XT_GUTTER "  "

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

/**
 * One skill's box, at `top` in `buf`.
 *
 * Laid out against the cache's numbers throughout; @see the file comment for
 * the script each came from.
 */
static void
xt_draw_box(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    uint32_t* buf,
    int w,
    int h,
    int top,
    int skill,
    int const slot[4],
    bool selected)
{
    struct XtSkill const* s = &g_skill[skill];
    char value[32];
    char text[48];
    struct ToriRS_SkillSnapshot snapshot;
    int xp;
    int level_xp;
    int next_xp;
    int level;
    int bar_y;
    int fill_w;
    int key_w = 0;
    int val_w;
    bool done;

    assert(buf);

    /* The box: the wash, then the border over it. */
    PluginDraw_Fill(buf, w, h, 0, top, w, XT_BOX_H, XT_BOX_FILL, XT_BOX_FILL_ALPHA);
    PluginDraw_Frame(buf, w, h, 0, top, w, XT_BOX_H, selected ? XT_BOX_SELECTED : XT_BOX_BORDER);

    /* The icon, indexed BY SKILL ID -- skills.png is cut in that order. */
    if( g_skill_px && skill * XT_ICON < g_skill_w )
        PluginDraw_Blit(
            buf, w, h, XT_ICON_X, top + XT_BOX_GAP + 1, g_skill_px, g_skill_w,
            g_skill_h, skill * XT_ICON, 0, XT_ICON, XT_ICON, 0);

    /*
     * The 2x2 stat grid, anchored to the box's RIGHT edge exactly as
     * script5366 anchors it (`cc_setposition(..., 2, 0)` is right-relative).
     * One key column width for both rows, so the two values line up.
     */
    /*
     * Four columns, every one anchored to the box's RIGHT edge, which is what
     * `cc_setposition(..., ^setpos_abs_right, ...)` means and the only way the
     * two value columns line up down a list of different-length keys.
     *
     * The offsets are the script's own, right to left:
     *   right value  at 4
     *   right key    at 4 + val_w
     *   left value   at 4 + val_w + rkey_w
     *   left key     at 4 + val_w + rkey_w + val_w
     * with each key measured at its WIDEST spelling, so switching between
     * ">Lvl" and ">Goal" does not shuffle the columns sideways.
     */
    {
        int const val_r = PluginDraw_TextWidthV2(&g_font, "88.888M");
        int const key_l = PluginDraw_TextWidthV2(&g_font, "XP>Goal: ");
        int const key_r = PluginDraw_TextWidthV2(&g_font, XT_GUTTER "Kills>Goal: ");
        int const edge = XT_GRID_PAD;

        val_w = val_r;
        key_w = key_l;
        for( int row = 0; row < 2; row++ )
        {
            int const y = top + XT_GRID_Y + row * XT_LINE_H;
            int const lhs = slot[row * 2 + 0];
            int const rhs = slot[row * 2 + 1];
            char key[48];

            /* left pair */
            snprintf(key, sizeof(key), "%s", XT_LABEL_KEY[lhs]);
            xt_label_value(api, state, skill, lhs, value, sizeof(value));
            PLUGIN_DRAW_TEXT(
                buf, w, h, w - edge - val_r - key_r - val_r - key_l, y, key, XT_INK_KEY);
            PLUGIN_DRAW_TEXT_RIGHT(
                buf, w, h, w - edge - val_r - key_r, y, value, XT_INK_VALUE);

            /* right pair, whose key carries the gutter */
            snprintf(key, sizeof(key), XT_GUTTER "%s", XT_LABEL_KEY[rhs]);
            xt_label_value(api, state, skill, rhs, value, sizeof(value));
            PLUGIN_DRAW_TEXT(buf, w, h, w - edge - val_r - key_r, y, key, XT_INK_KEY);
            PLUGIN_DRAW_TEXT_RIGHT(buf, w, h, w - edge, y, value, XT_INK_VALUE);
        }
    }
    (void)key_w;
    (void)val_w;

    /* The bar. */
    if( !xt_skill_snapshot(api, skill, &snapshot) )
        memset(&snapshot, 0, sizeof(snapshot));
    xp = snapshot.xp;
    level_xp = snapshot.level_xp;
    next_xp = snapshot.next_level_xp;
    level = snapshot.base_level;
    bar_y = top + XT_BAR_Y;
    /* next_xp is 0 at the top of the client's table -- 99, with no next level
     * to progress towards, which script5370 draws as the DONE bar rather than
     * as a green one that happens to be full. */
    done = next_xp <= level_xp;
    fill_w = done || next_xp <= level_xp
                 ? w
                 : (int)(((long long)(xp - level_xp) * w) / (next_xp - level_xp));
    if( fill_w < 0 )
        fill_w = 0;
    if( fill_w > w )
        fill_w = w;

    PluginDraw_Fill(buf, w, h, 0, bar_y, w, XT_BAR_H, XT_BAR_TRACK, 255);
    PluginDraw_Fill(
        buf, w, h, 0, bar_y, fill_w, XT_BAR_H, done ? XT_BAR_DONE : XT_BAR_FILL, 255);

    /*
     * Its three labels: the level at each end, the percentage in the middle.
     * "Lvl. " is the cache's own prefix (torirs_xptracker_stat_level_label) --
     * a bare number at the end of a bar reads as a quantity, which is what the
     * number in the middle already is.
     */
    snprintf(text, sizeof(text), "Lvl. %d", level);
    PLUGIN_DRAW_TEXT(buf, w, h, XT_PAD, bar_y + 2, text, XT_INK_KEY);
    if( !done )
    {
        snprintf(text, sizeof(text), "Lvl. %d", level + 1);
        PLUGIN_DRAW_TEXT_RIGHT(buf, w, h, w - XT_PAD, bar_y + 2, text, XT_INK_KEY);
    }

    if( s->paused )
        snprintf(text, sizeof(text), "Paused.");
    else if( done )
        snprintf(text, sizeof(text), "Done!");
    else
    {
        /*
         * The percentage, exactly as script5371 spells one: the permyriad is
         * padded with SPACES to five characters and then cut 3/2, so 6949
         * becomes " 69.49%" and 949 becomes "  9.49%". The padding is not
         * decoration -- it is what keeps the decimal point in the same column
         * down a list of boxes, which a centred "%.2f" does not.
         */
        long long const permyriad =
            ((long long)(xp - level_xp) * 10000) / (next_xp - level_xp);
        char pad[16];
        int n = snprintf(pad, sizeof(pad), "%lld", permyriad);
        char spaced[16];
        int at = 0;
        for( int i = n; i < 5; i++ )
            spaced[at++] = ' ';
        memcpy(spaced + at, pad, (size_t)n);
        at += n;
        spaced[at] = '\0';
        snprintf(text, sizeof(text), "%.3s.%.2s%%", spaced, spaced + 3);
    }
    PLUGIN_DRAW_TEXT(
        buf, w, h, (w - PluginDraw_TextWidthV2(&g_font, text)) / 2, bar_y + 2, text,
        XT_INK_VALUE);
}

/**
 * The OVERVIEW box: the session's two totals, over the whole list.
 *
 * `torirs_xptracker_total_labels` (script 5367) is the whole recipe, and it is
 * a different shape from a skill's box rather than a special case of one: two
 * lines CENTRED in the 48, no bar, no progress, and a key column that starts
 * at 31 instead of hard against a 25px icon. The icon is the tracker's own --
 * the same picture its rail stone wears -- because the box is about every
 * skill and no single skill's icon can stand for that.
 */
static void
xt_draw_overview(struct XtState* state, uint32_t* buf, int w, int h, int top)
{
    long long gained = 0;
    long long rate = 0;
    char value[32];
    int const key_w = PluginDraw_TextWidthV2(&g_font, "Total XP Gained: ");
    int const val_w = PluginDraw_TextWidthV2(&g_font, "88.888M");
    /* Centred in the 48, which is what `^settextalign_centre` with a 12 line
     * box does to two lines: 48/2 - 12 above the pair. */
    int const y0 = top + (XT_BOX_H - 2 * XT_LINE_H) / 2;

    assert(buf);

    for( int i = 0; i < g_skill_count; i++ )
    {
        gained += xt_gained(&g_skill[i]);
        if( !g_skill[i].paused )
            rate += xt_hourly(&g_skill[i], g_skill[i].gained_since_reset);
    }

    PluginDraw_Fill(buf, w, h, 0, top, w, XT_BOX_H, XT_BOX_FILL, XT_BOX_FILL_ALPHA);
    PluginDraw_Frame(buf, w, h, 0, top, w, XT_BOX_H, XT_BOX_BORDER);

    if( g_over_px )
        PluginDraw_Blit(
            buf, w, h, XT_OVER_ICON_X, top + (XT_BOX_H - XT_OVER_ICON) / 2, g_over_px,
            g_over_w, g_over_h, 0, 0,
            g_over_w < XT_OVER_ICON ? g_over_w : XT_OVER_ICON,
            g_over_h < XT_OVER_ICON ? g_over_h : XT_OVER_ICON, 0);

    PLUGIN_DRAW_TEXT(buf, w, h, XT_OVER_KEY_X, y0, "Total XP/Hr: ", XT_INK_KEY);
    PLUGIN_DRAW_TEXT(
        buf, w, h, XT_OVER_KEY_X, y0 + XT_LINE_H, "Total XP Gained: ", XT_INK_KEY);

    xt_compact(rate, value, sizeof(value));
    PLUGIN_DRAW_TEXT_RIGHT(
        buf, w, h, XT_OVER_KEY_X + key_w + val_w, y0, value, XT_INK_VALUE);
    xt_compact(gained, value, sizeof(value));
    PLUGIN_DRAW_TEXT_RIGHT(
        buf, w, h, XT_OVER_KEY_X + key_w + val_w, y0 + XT_LINE_H, value, XT_INK_VALUE);
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */
/** Which skills get a box, in stats-tab order. */
static void
xt_collect_boxes(struct ToriRS_ApiV2* api, struct XtState* state)
{
    g_box_count = 0;
    for( int i = 0; i < g_skill_count && g_box_count < XT_SKILLS_MAX; i++ )
        if( xt_row_wanted(api, state, i) )
            g_box_skill[g_box_count++] = i;
}

/** The four label slots the user chose, in reading order. */
static void
xt_slots(struct ToriRS_ApiV2* api, int out[4])
{
    /*
     * The cache's own pairing, and the ORDER is the half that was wrong:
     * script5366 puts "XP/Hr: <br>XP>Lvl: " in the LEFT column and
     * "  XP Gained: <br>  Acts>Lvl: " in the right one. Defaulting the left
     * slots to gained/actions swapped every box against the tracker it copies.
     */
    out[0] = xt_label_slot(api, "label_top_left", XT_LABEL_XP_HOUR);
    out[1] = xt_label_slot(api, "label_top_right", XT_LABEL_XP_GAINED);
    out[2] = xt_label_slot(api, "label_bottom_left", XT_LABEL_XP_LEFT);
    out[3] = xt_label_slot(api, "label_bottom_right", XT_LABEL_ACTIONS_LEFT);
}

/** The well's height for the boxes it has to hold. */
static int
xt_well_h(struct XtState const* state)
{
    /* The overview box is always there -- it is the session's answer and it
     * has one whether or not a skill has been trained -- so the strip is one
     * box taller than the list. */
    return (g_box_count + 1) * XT_BOX_PITCH;
}

/**
 * Rasterise every box and publish the strip.
 *
 * One image for the whole list rather than one per box: a compose is the
 * expensive half of this plugin and the panel blits one picture either way,
 * so composing per box would pay for the same pixels with more calls.
 */
/**
 * Everything the strip's picture depends on, in one number.
 *
 * @see the loot tracker's lt_compose_key, which exists for the same reason:
 * the draw event fires whenever the well is dirty, and composing on every one
 * of them republishes the scene sprite the overlay item is about to reference.
 *
 * The hash covers the drawn VALUES rather than the state behind them, because
 * several of them move on their own -- a rate ticks with the clock whether or
 * not any xp arrived -- and a key that missed those would leave a stale
 * picture on screen looking like a frozen tracker.
 */
static uint64_t
xt_compose_key(
    struct ToriRS_ApiV2* api,
    struct XtState* state,
    int width,
    int const slot[4])
{
    uint64_t k = 1469598103934665603ull;

#define XT_MIX(v)                                                                        \
    do                                                                                   \
    {                                                                                    \
        k ^= (uint64_t)(v);                                                              \
        k *= 1099511628211ull;                                                           \
    } while( 0 )

    XT_MIX(width);
    XT_MIX(g_box_count);
    XT_MIX(g_detail);
    for( int i = 0; i < 4; i++ )
        XT_MIX(slot[i]);
    for( int i = 0; i < g_box_count; i++ )
    {
        int const which = g_box_skill[i];
        struct XtSkill const* sk = &g_skill[which];
        struct ToriRS_SkillSnapshot snapshot;

        if( !xt_skill_snapshot(api, which, &snapshot) )
            memset(&snapshot, 0, sizeof(snapshot));
        XT_MIX(which);
        XT_MIX(snapshot.xp);
        XT_MIX(snapshot.base_level);
        XT_MIX(xt_gained(sk));
        XT_MIX(sk->actions);
        XT_MIX(sk->paused ? 1 : 0);
        XT_MIX(xt_hourly(sk, sk->gained_since_reset));
        XT_MIX(xt_hourly(sk, sk->actions_since_reset));
    }
#undef XT_MIX
    return k;
}

static void
xt_compose(struct ToriRS_ApiV2* api, struct XtState* state, int width)
{
    int slot[4];
    int const height = xt_well_h(state);
    size_t const pixels = (size_t)width * (size_t)height;
    uint64_t key;

    if( width <= 0 || height <= 0 )
        return;
    xt_slots(api, slot);
    key = xt_compose_key(api, state, width, slot);
    if( key == g_compose_key && g_compose && g_compose_w == width &&
        g_compose_h == height )
        return;
    g_compose_key = key;

    if( !g_compose || g_compose_w != width || g_compose_h != height )
    {
        free(g_compose);
        g_compose = malloc(pixels * sizeof(*g_compose));
        assert(g_compose);
        g_compose_w = width;
        g_compose_h = height;
    }
    /* Transparent, so the panel's own backing shows between the boxes exactly
     * as the interface's does between the CS2 rows. */
    memset(g_compose, 0, pixels * sizeof(*g_compose));

    xt_draw_overview(state, g_compose, width, height, 0);
    for( int i = 0; i < g_box_count; i++ )
        xt_draw_box(
            api,
            state,
            g_compose,
            width,
            height,
            (i + 1) * XT_BOX_PITCH,
            g_box_skill[i],
            slot,
            g_box_skill[i] == g_detail);

    (void)api->assets.image_compose(
        api, "boxes", width, height, g_compose, &state->compose_image);
}

/**
 * Ask for a redraw of the strip only when the picture would differ.
 *
 * The refresh runs on a timer, and an unconditional invalidate would put the
 * well through a full draw pass twice a second for a picture that is already
 * on screen -- every one of those passes a chance to catch the art or an obj
 * icon mid-flight and publish a frame that is missing one. Composing is keyed
 * on the drawn values; so is asking for the pass at all.
 */
static void
xt_strip_invalidate(struct ToriRS_ApiV2* api, struct XtState* state)
{
    int slot[4];

    xt_slots(api, slot);
    if( g_compose && xt_compose_key(api, state, g_well_w, slot) == g_compose_key )
        return;
    api->panel.redraw(api, "boxes");
}

/** Rewrite the session readouts. The boxes are pixels and redraw themselves. */
static void
xt_page_refresh(struct ToriRS_ApiV2* api, struct XtState* state)
{
    long long total_gained = 0;
    long long total_rate = 0;
    uint64_t const now = api->core.frame_ms(api);
    char text[96];

    if( !g_page_built )
        return;

    /*
     * A skill earning its first box makes the well TALLER. That is a property
     * of a widget the page already has, so the retained page states it in
     * place -- the same call the build makes. This used to be a panel_clear,
     * and re-declaring the whole page the first time each skill was trained is
     * what the strip flashed on.
     */
    (void)api->panel.set_height(api, "boxes", xt_well_h(state));

    for( int i = 0; i < g_skill_count; i++ )
    {
        total_gained += xt_gained(&g_skill[i]);
        if( !g_skill[i].paused )
            total_rate += xt_hourly(&g_skill[i], g_skill[i].gained_since_reset);
    }

    /*
     * The session's totals are the OVERVIEW BOX's, and nowhere else.
     *
     * They were rows on the page and then a rail badge, and both were the same
     * mistake in different places: the strip's first box is exactly
     * `torirs_xptracker_total_labels` and already states them, so anything
     * else that does is a second copy to keep in step -- and the rail is a
     * column of icons with no room for a number anyway.
     */
    (void)total_gained;
    (void)total_rate;
    (void)now;

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

        (void)api->panel.set_text(
            api, "sec_detail", snapshot.name[0] ? snapshot.name : "?");
        (void)api->panel.set_text(api, "d_pause", skill->paused ? "Unpause" : "Pause");

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

        /* The detail block's shape is stable across skills.  Remember which
         * skill its retained values now describe without rebuilding it. */
        if( g_built_detail >= 0 )
            g_built_detail = g_detail;
    }

    /* The strip is a picture of numbers that may just have moved. */
    xt_strip_invalidate(api, state);
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

    if( view != TORIRS_PLUGIN_PANEL_VIEW_PAGE )
    {
        g_page_built = false;
        return;
    }

    xt_collect_boxes(api, state);
    panel->custom(panel, "boxes", xt_well_h(state));

    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_skill_count &&
        xt_row_wanted(api, state, g_detail) )
    {
        struct ToriRS_SkillSnapshot snapshot;
        struct ToriRS_PanelNode heading;
        char const* name =
            xt_skill_snapshot(api, g_detail, &snapshot) ? snapshot.name : "?";

        memset(&heading, 0, sizeof(heading));
        heading.struct_size = sizeof(heading);
        heading.kind = TORIRS_PANEL_HEADING;
        heading.id = "sec_detail";
        heading.text = name;
        (void)panel->node(panel, &heading);
        panel->key_value(panel, "d_gained", "XP gained", "");
        panel->key_value(panel, "d_hr", "XP/hr", "");
        panel->key_value(panel, "d_left", "XP to level", "");
        panel->key_value(panel, "d_actions", "Actions", "");
        panel->key_value(panel, "d_actleft", "Actions to level", "");
        panel->key_value(panel, "d_ttl", "Time to level", "");
        panel->button(panel, "d_pause", g_skill[g_detail].paused ? "Unpause" : "Pause", true);
        panel->button(panel, "d_reset", "Reset", true);
        panel->button(panel, "d_reset_others", "Reset others", true);
        panel->button(panel, "d_reset_rate", "Reset/hr", true);
    }
    else
        g_built_detail = -1;

    g_page_built = true;
    xt_page_refresh(api, state);
}

/** Does the built page still show the boxes the state now wants? */
static bool
xt_page_stale(struct ToriRS_ApiV2* api, struct XtState* state)
{
    bool wants_detail;

    if( !g_page_built )
        return false;
    /*
     * The boxes are reconciled here whether or not anything moved, because the
     * strip is drawn from them. A box appearing or disappearing changes the
     * WELL's height and nothing else -- not which widgets the page has -- and
     * xt_page_refresh states that height in place. Only the detail block
     * coming or going is a different page.
     */
    xt_collect_boxes(api, state);
    wants_detail = g_detail >= 0 && g_detail < g_skill_count &&
                   xt_row_wanted(api, state, g_detail);
    return (g_built_detail >= 0) != wants_detail;
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
    if( ev->width > 0 )
        g_well_w = ev->width;
    if( g_page_visible )
    {
        xt_page_refresh(api, state);
        api->panel.redraw(api, "boxes");
    }
}

/** The strip, blitted into the well. */
static void
xt_panel_draw(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    char const* node,
    struct ToriRS_DrawBuilder* draw)
{
    struct XtState* state = plugin_state;
    struct ToriRS_DrawContext context;

    if( !node || strcmp(node, "boxes") != 0 )
        return;
    memset(&context, 0, sizeof(context));
    context.struct_size = sizeof(context);
    if( !draw->context(draw, &context) || context.bounds.width <= 0 )
        return;
    if( !xt_art_ready(api, state) )
        return;

    g_well_w = context.bounds.width;
    xt_compose(api, state, context.bounds.width);
    if( state->compose_image.value != 0 )
        draw->image(draw, state->compose_image, 0, 0, 255);
}

/**
 * A control on the page, or a click in the box strip.
 *
 * The strip is ONE control, so a click in it arrives with well-local
 * coordinates and the row is arithmetic: the boxes are a fixed pitch and the
 * order they were drawn in is `g_box_skill`.
 */
static void
xt_panel_action(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_PanelActionEvent const* ev)
{
    struct XtState* state = plugin_state;
    assert(ev);
    assert(ev->id);

    if( strcmp(ev->id, "boxes") == 0 )
    {
        int const row = ev->y / XT_BOX_PITCH;
        int const skill = row >= 0 && row < g_box_count ? g_box_skill[row] : -1;
        bool const had_detail = g_built_detail >= 0;

        /* Clicking the open box closes it, which is what makes the strip its
         * own way back out of a selection. */
        g_detail = skill >= 0 && skill != g_detail ? skill : -1;
        if( had_detail != (g_detail >= 0) )
            api->panel.invalidate(api);
        else
            xt_page_refresh(api, state);
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
    g_well_w = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
    g_skill_count = 0;
    memset(g_built_rows, 0, sizeof(g_built_rows));
    g_detail = -1;
    g_built_detail = -1;
    g_page_built = false;
    g_page_visible = false;
    g_compose_key = 0;
    g_state_applied = false;
    g_logged_in = false;
    g_session_start_ms = api->core.frame_ms(api);
    g_last_second_ms = g_session_start_ms;
    g_next_panel_ms = 0;

    memset(&desc, 0, sizeof(desc));
    /* RuneLite's own, so a person who has used the plugin there recognises
     * the row here. @see script/plugins/assets/xp-tracker/panel_icon.txt. */
    desc.icon_asset = "panel_icon.png";
    desc.preferred_width = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
    (void)api->panel.request(api, &desc);

    /* Queued, not read: the file crosses the IO queue like every other asset,
     * and the answer arrives at EV_ASSET. A load that is already resident
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
    (void)xt_art_ready(api, state);
}

static void
xt_stop(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct XtState* state = plugin_state;
    xt_state_save(api, state);
    g_page_built = false;
    free(g_compose);
    g_compose = NULL;
    PluginDraw_AtlasFreeV2(api, &g_font);
    PluginDraw_ImageFreeV2(api, &g_over_px, &g_img_over);
    PluginDraw_ImageFreeV2(api, &g_skill_px, &g_img_skills);
    if( state->compose_image.value != 0 )
        api->assets.image_release(api, state->compose_image);
    api->assets.release(api, XT_STATE_ASSET);
    memset(state, 0, sizeof(*state));
}

/**
 * Size the skill table, the first time the client can answer.
 *
 * NOT at EV_START, and that is the whole point: a plugin starts when the
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
    { "save_state",        TORIRS_PLUGIN_CFG_BOOL, "Save between sessions",        "1", 0, 0,  NULL, 0 },
    { "hide_maxed",        TORIRS_PLUGIN_CFG_BOOL, "Hide maxed skills",            "0", 0, 0,  NULL, 0 },
    { "pause_on_logout",   TORIRS_PLUGIN_CFG_BOOL, "Pause on logout",              "1", 0, 0,  NULL, 0 },
    { "pause_skill_after", TORIRS_PLUGIN_CFG_INT,  "Auto pause after (minutes)",   "0", 0, 60, NULL, 0 },
    { "reset_rate_after",  TORIRS_PLUGIN_CFG_INT,  "Auto reset rate after (minutes)", "0", 0, 60, NULL, 0 },
    /* The four corner slots of a box's 2x2 grid, and the reference's own
     * choice list for them (XpPanelLabel). The DEFAULTS are its defaults --
     * gained, rate, actions left, xp left -- which is also the pairing the
     * cache's own tracker hard-codes. @see enum XtLabel. */
    { "label_top_left",     TORIRS_PLUGIN_CFG_ENUM, "Top-left stat",     "XP/hr",     0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_top_right",    TORIRS_PLUGIN_CFG_ENUM, "Top-right stat",    "XP Gained", 0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_bottom_left",  TORIRS_PLUGIN_CFG_ENUM, "Bottom-left stat",  "XP Left",   0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { "label_bottom_right", TORIRS_PLUGIN_CFG_ENUM, "Bottom-right stat", "Actions",   0, 0, "XP Gained|XP/hr|XP Left|Actions Done|Actions/hr|Actions|TTL", 0 },
    { NULL,                TORIRS_PLUGIN_CFG_BOOL, NULL,                           NULL, 0, 0, NULL, 0 },
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
        .on_ui_draw = xt_panel_draw,
        .on_ui_layout = xt_panel_layout,
    },
};
