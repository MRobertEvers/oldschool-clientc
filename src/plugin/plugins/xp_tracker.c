#include "plugin/plugins/plugin_draw.h"
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <math.h>
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
 *   script5364  the BOX -- a black filled rect at `cc_settrans(128)` with an
 *               opaque black unfilled rect over it for the border, 48 tall on
 *               a 50 pitch (`%varcint562 = row * (48 + 2)`).
 *   script5363  the skill ICON, 25x25 at x=3, out of `enum(stat, graphic,
 *               enum_255, stat)`.
 *   script5366  the STATS, a 2x2 grid anchored to the box's RIGHT edge, in
 *               fontmetrics_494 with a shadow: keys 0xcccccc, values white,
 *               12px line height, the pairs being XP Gained / XP/Hr and
 *               Acts>Lvl / XP>Lvl.
 *   script5365  the BAR: track 0x002200 and fill 0x006600, 15 tall, with three
 *               labels over it -- the level at the left and the goal at the
 *               right in 0xcccccc, and the percentage centred in white. Its
 *               five components are the ones that are NOT in the box's layer,
 *               so none of its offsets is a box offset; @see
 *               XT_BAR_LAYER_INSET, which is the whole of that story.
 *   script5370  the fill's width, virtual levels through 126, a 200m goal
 *               beyond 126, and 0x885500 across the WHOLE bar only when that
 *               final goal is met rather than at ordinary level 99.
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
 *   progress        (xp - level_xp) / (next_xp - level_xp), where the cache's
 *                   own curve continues through virtual level 126.
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
 * LEVEL, which is the thing every revision agrees exists. The API supplies the
 * live XP; the same curve `torirs_xp_to_level` uses carries it through virtual
 * level 126, then the cache tracker uses 200m as the final goal.
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
/**
 * The skill icon: 25x25, at script5363's own `cc_setposition(calc(2 + 1),
 * calc(%varcint562 + 2 + 1))`. Those literals ARE box offsets, because
 * script5363 creates into 729:6, which shares the box's origin; @see
 * XT_BAR_LAYER_INSET for the five components that do not.
 */
#define XT_ICON 25
#define XT_ICON_X (2 + 1)
#define XT_ICON_Y (2 + 1)
/**
 * The BAR lives in a DIFFERENT LAYER from the rest of the box, and that is
 * where the port's numbers came apart.
 *
 * Interface 729 builds one skill box out of thirteen sibling layers under
 * 729:3, and they do NOT share an origin:
 *
 *     729:4      box wash        503,2  244x499  \
 *     729:5      box outline     503,2  244x499   |  the BOX's frame
 *     729:6      skill icon      503,2  244x499   |
 *     729:12..16 the stat grid   503,2  244x499  /
 *
 *     729:7      bar track       506,5  238x493  \  the BAR's frame: three
 *     729:8      bar fill        506,5  238x493   |  pixels inside the box's
 *     729:9..11  the bar labels  506,5  238x493  /   on every side
 *
 *     tools/dump_interface/dump_interface cache.osrs239 --dat2 --iface 729
 *
 * Nothing moves them afterwards: 729:0's onload and the five scripts a row
 * build calls -- 5448, 5384, 5460, 5461, 5444 -- contain no cc_setposition
 * and no cc_setsize on 729:6..11 at all.
 *
 * So script5365's `calc(%varcint562 + 25 + 2)` is measured from an origin
 * three rows BELOW script5363's and script5366's, and its `cc_setposition(0,
 * ...)` with `cc_setsize(0, 15, ^setsize_minus, ...)` spans 238 and not 244.
 * This file composes one flat buffer in the BOX's frame, so each of the bar's
 * numbers has to be carried across that inset before it is used here. The
 * port transcribed them as literals instead, as if the five scripts shared
 * one origin, and the box's arithmetic stopped closing in both directions at
 * once: the bar began on the icon's LAST ROW instead of two rows under it,
 * and it ran outline to outline, erasing the box's own 1px black border for
 * the bar's fifteen rows -- present above the bar and below it, absent
 * beside it.
 *
 * Those are one defect and they take one number. Treating them as two and
 * nudging each until it looked better -- the bar down a row to clear the
 * icon, and in a column to clear the outline -- is what this constant
 * replaces: it closes neither gap at the value the cache states, and it
 * leaves the skirt under the bar four rows where the cache leaves three.
 *
 * Carried across, the 48 rows account for themselves exactly and
 * symmetrically, and the bar's left edge (absolute 506) lands on the icon's
 * (absolute 503 + 3), which is plainly the intended look:
 *
 *     3 over the icon + 25 icon + 2 gap + 15 bar + 3 under it = 48
 */
#define XT_BAR_LAYER_INSET 3
/** script5365's own `25 + 2`, which is a BAR-layer offset as written. */
#define XT_BAR_LAYER_Y (XT_ICON + 2)
/** The same row, in the box's frame -- the frame this file draws in. */
#define XT_BAR_Y (XT_BAR_LAYER_INSET + XT_BAR_LAYER_Y)
/** The bar's height, as script5365 sets it. */
#define XT_BAR_H 15
/**
 * The two end labels, anchored inside that same inset layer: the level at
 * `calc(2 - 1)` from its left edge (729:10) and the goal at `calc(2 + 1)`
 * from its right (729:11, `^setpos_abs_right`). Carried across the inset the
 * left one lands on 4, which is what this file used to spell as a padding it
 * had chosen; the right one lands on 6, which it never did.
 */
#define XT_BAR_LABEL_L (XT_BAR_LAYER_INSET + (2 - 1))
#define XT_BAR_LABEL_R (XT_BAR_LAYER_INSET + (2 + 1))
/** The stat grid's line box. */
#define XT_LINE_H 12
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

/** The box: `cc_settrans(128)` means source alpha 255-128, under a black border. */
#define XT_BOX_FILL 0x000000u
#define XT_BOX_FILL_ALPHA (255 - 128)
#define XT_BOX_BORDER 0x000000u
/** The bar, exactly as script5365/5370 set it. */
#define XT_BAR_TRACK 0x002200u
#define XT_BAR_FILL 0x006600u
#define XT_BAR_DONE 0x885500u
/** Text: values white, keys and the bar's end labels 0xcccccc. */
#define XT_INK_VALUE 0xFFFFFFu
#define XT_INK_KEY 0xCCCCCCu
/** The virtual-level table's last level and the final XP goal after it. */
/*
 * The box closes. Either half of the layer-frame carry can be dropped on its
 * own and still compile, and each way the picture is merely a little wrong
 * rather than obviously broken -- which is how two separate nudges got
 * written for it -- so the arithmetic says so here instead of in a comment.
 */
_Static_assert(
    XT_ICON_Y + XT_ICON + 2 == XT_BAR_Y,
    "the bar starts two rows under the icon, not on its last row");
_Static_assert(
    XT_BAR_Y + XT_BAR_H + XT_BAR_LAYER_INSET == XT_BOX_H,
    "what is left under the bar is the bar layer's own inset");

#define XT_VIRTUAL_LEVEL_MAX 126
#define XT_MAX_XP 200000000

/* ------------------------------------------------------------ the glyph atlas */

/* The kit's text verbs take the face as an argument; every label on this page
 * is set in the one the CS2 sets its own in, so the calls name it once here. */
#define PLUGIN_DRAW_TEXT(buf, w, h, x, top, text, tint)                                  \
    PluginDraw_Text((buf), (w), (h), (x), (top), &g_font, (text), (tint))
#define PLUGIN_DRAW_TEXT_RIGHT(buf, w, h, right, top, text, tint)                        \
    PluginDraw_TextRight((buf), (w), (h), (right), (top), &g_font, (text), (tint))

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

/**
 * One shipped picture, held as pixels, with the LAYER's answer about it.
 *
 * `state` is the whole point. The old spelling was a bool per picture and a
 * retry on every draw pass: a missing file and a file still crossing the IO
 * queue were the same answer, so an absent skills.png was re-asked fifty
 * times a second for the life of the session and said so nowhere.
 * Porcelain_Image remembers a terminal state and reports it once.
 */
struct XtArt
{
    char const* name;
    uint32_t* px;
    int w;
    int h;
    enum PorcelainAssetState state;
};

/* Named by xt_start, which opens the layer against this plugin's own
 * definition; the definition itself is at the foot of the file. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER;

struct XtState
{
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
    struct PluginDraw_Atlas font;
    /** The atlas sheet, the 25x25 skill strip, and the overview's own icon. */
    struct XtArt art_font;
    struct XtArt art_skills;
    struct XtArt art_over;
    struct XtSkill skill[XT_SKILLS_MAX];
    int skill_count;
    int detail;
    bool page_visible;
    bool state_applied;
    bool logged_in;
    /** The server has STATED a reading. @see PORCELAIN_READY_STATS. */
    bool stats_ready;
    uint64_t session_start_ms;
    /** The box order this description wants, in stats-tab order. */
    int box_skill[XT_SKILLS_MAX];
    int box_count;
    /* The exact order painted into the retained CUSTOM well. Input uses this
     * snapshot, never a newly collected order under an older bitmap. */
    int built_box_skill[XT_SKILLS_MAX];
    int built_box_count;
    int well_w;
    /** The detail block's six readouts and its heading, held rather than
     *  built on the stack purely for legibility: Porcelain COPIES a row's
     *  strings, so a stack buffer would be legal too. */
    char detail_name[PORCELAIN_ROW_TEXT_MAX];
    char detail_text[6][PORCELAIN_ROW_TEXT_MAX];
};

#define g_font (state->font)
#define g_over_px (state->art_over.px)
#define g_over_w (state->art_over.w)
#define g_over_h (state->art_over.h)
#define g_skill_px (state->art_skills.px)
#define g_skill_w (state->art_skills.w)
#define g_skill_h (state->art_skills.h)
#define g_skill (state->skill)
#define g_skill_count (state->skill_count)
#define g_detail (state->detail)
#define g_page_visible (state->page_visible)
#define g_state_applied (state->state_applied)
#define g_session_start_ms (state->session_start_ms)
#define g_logged_in (state->logged_in)
#define g_box_skill (state->box_skill)
#define g_box_count (state->box_count)
#define g_built_box_skill (state->built_box_skill)
#define g_built_box_count (state->built_box_count)
#define g_well_w (state->well_w)

static bool
xt_cfg_bool(struct ToriRS_Api* api, char const* key)
{
    bool value = false;
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
xt_cfg_int(struct ToriRS_Api* api, char const* key)
{
    int value = 0;
    (void)api->config.get_int(api, key, &value);
    return value;
}

static char const*
xt_cfg_string(struct ToriRS_Api* api, char const* key)
{
    char const* value = "";
    (void)api->config.get_string(api, key, &value);
    return value ? value : "";
}

static bool
xt_skill_snapshot(
    struct ToriRS_Api* api,
    int index,
    struct ToriRS_SkillSnapshot* out)
{
    assert(api);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    /*
     * ABSENT is the default, because that is what the caller reads when the
     * call writes nothing. The host fills `index` on both of its false paths
     * -- -1 for a skill this lane does not have, the index itself for one it
     * has but the server has not stated -- and xt_size_table walks to a bound
     * rather than to the first refusal on exactly that distinction. Left at
     * the memset's zero, a call that answered nothing at all would read as
     * "skill 0 exists", and the walk would size the table to its ceiling.
     */
    out->index = -1;
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
/* Virtual-level progress                                                    */
/* ------------------------------------------------------------------------ */

/** The level interval the cache tracker draws for one XP value. */
struct XtProgress
{
    int xp;
    int level;
    int level_xp;
    int next_xp;
    bool max_goal;
    bool done;
};

/**
 * Reproduce `torirs_xp_to_level` plus `torirs_xptracker_goal_mode`.
 *
 * The host's snapshot thresholds deliberately stop at the real skill cap, 99.
 * The CS2 tracker does not: enum_256 carries the ordinary RuneScape XP curve
 * through virtual level 126 (188,884,740 XP), then goal mode 3 makes 200m the
 * last bar. Computing the same series avoids turning every 99 into a gold
 * "Done!" row and is revision-independent -- rs289lc and osrs239 use the same
 * XP curve even though only the latter ships this tracker interface.
 */
static void
xt_progress(int xp, struct XtProgress* out)
{
    double points = 0.0;
    int lower = 0;

    assert(out);
    if( xp < 0 )
        xp = 0;
    if( xp > XT_MAX_XP )
        xp = XT_MAX_XP;

    memset(out, 0, sizeof(*out));
    out->xp = xp;
    for( int level = 1; level < XT_VIRTUAL_LEVEL_MAX; level++ )
    {
        int threshold;
        points += floor((double)level + 300.0 * pow(2.0, (double)level / 7.0));
        threshold = (int)(points / 4.0);
        if( xp < threshold )
        {
            out->level = level;
            out->level_xp = lower;
            out->next_xp = threshold;
            return;
        }
        lower = threshold;
    }

    out->level = XT_VIRTUAL_LEVEL_MAX;
    out->level_xp = lower;
    if( xp < XT_MAX_XP )
    {
        out->next_xp = XT_MAX_XP;
        out->max_goal = true;
    }
    else
    {
        out->next_xp = 0;
        out->done = true;
    }
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
xt_reset_skill(struct ToriRS_Api* api, struct XtState* state, int index)
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
xt_row_wanted(struct ToriRS_Api* api, struct XtState* state, int index)
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
    struct ToriRS_Api* api,
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
xt_state_save(struct ToriRS_Api* api, struct XtState* state)
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
    struct ToriRS_Api* api,
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
xt_state_apply(struct ToriRS_Api* api, struct XtState* state)
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

/* ------------------------------------------------------------------------ */
/* The boxes                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Lay down script5364's black `cc_settrans(128)` fill without flattening it.
 *
 * PluginDraw_Fill is a source-over helper for buffers that already have an
 * opaque destination; on a newly transparent compose it necessarily resolves
 * the colour and produces an opaque pixel. These boxes must instead retain
 * alpha 127 in the published PNG so the chrome's brown CUSTOM-well backing is
 * what the black tint blends over in the WebView, just like the CS2 rectangle
 * blends over its interface parent.
 */
static void
xt_box_fill(uint32_t* buf, int w, int h, int top)
{
    uint32_t const argb = ((uint32_t)XT_BOX_FILL_ALPHA << 24) | XT_BOX_FILL;
    int const bottom = top + XT_BOX_H < h ? top + XT_BOX_H : h;

    assert(buf);
    if( top < 0 )
        top = 0;
    for( int y = top; y < bottom; y++ )
        for( int x = 0; x < w; x++ )
            buf[(size_t)y * (size_t)w + (size_t)x] = argb;
}

/**
 * One shipped picture's pixels, asked for through the layer.
 *
 * The layer owns the question this function used to answer by guessing: an
 * asset is READY, still crossing the IO queue (PENDING, the ordinary state
 * for the first frames and the normal one for ever on web), or terminally
 * MISSING or ERROR -- and a terminal answer is remembered and reported once
 * instead of being re-asked on every draw pass. The pixels are copied out
 * once and the handle goes back, which is what lets Porcelain release the
 * slot a few runs later.
 */
static bool
xt_art_pixels(struct XtState* state, struct XtArt* art)
{
    struct ToriRS_Api* api = state->api;
    enum PorcelainAssetState asset = PORCELAIN_ASSET_PENDING;
    struct ToriRS_ImageRef ref;
    size_t count;
    size_t written = 0;

    assert(state);
    assert(art);
    assert(art->name);
    if( art->px )
        return true;

    ref = Porcelain_Image(state->porcelain, art->name, &asset);
    art->state = asset;
    if( asset != PORCELAIN_ASSET_READY )
        return false;
    if( !Porcelain_ImageSize(state->porcelain, art->name, &art->w, &art->h) ||
        art->w <= 0 || art->h <= 0 )
        return false;

    count = (size_t)art->w * (size_t)art->h;
    art->px = malloc(count * sizeof(*art->px));
    assert(art->px);
    if( !api->assets.image_pixels(api, ref, art->px, count, &written) ||
        written != count )
    {
        /* The handle answered a size and then refused its pixels. Terminal
         * for this run rather than a retry: the alternative is the per-draw
         * ask this whole function exists to remove. */
        free(art->px);
        art->px = NULL;
        art->w = 0;
        art->h = 0;
        art->state = PORCELAIN_ASSET_ERROR;
        Porcelain_Finding(
            state->porcelain, "image_pixels", PORCELAIN_ROLE_EL(art->name),
            PORCELAIN_FINDING_ASSET_ERROR, art->name);
        return false;
    }
    return true;
}

/** The glyph table, out of the bytes Porcelain_Table fetched. */
static bool
xt_parse_atlas(
    struct ToriRS_Api* api, void* user, void const* data, size_t size)
{
    struct XtState* state = user;
    (void)api;
    assert(state);
    assert(data);
    return PluginDraw_AtlasParse(&g_font, data, size) != 0;
}

/**
 * Everything the compose needs, resident.
 *
 * The atlas and skills.png are REQUIRED -- no face and no icon strip means no
 * strip worth publishing. The overview icon is WANTED: a missing picture is a
 * box with a gap in it, not a page that refuses to draw.
 */
static int
xt_art_ready(struct ToriRS_Api* api, struct XtState* state)
{
    (void)api;
    assert(state);
    if( !g_font.ready &&
        !Porcelain_Table(state->porcelain, "text.ini", xt_parse_atlas, state) )
        return 0;
    if( !xt_art_pixels(state, &state->art_font) )
        return 0;
    g_font.px = state->art_font.px;
    g_font.w = state->art_font.w;
    g_font.h = state->art_font.h;
    if( !xt_art_pixels(state, &state->art_skills) )
        return 0;
    /* @see the struct comment: staticons2,7, the graphic
     * xptracker_build_components_5363 names for the null/overall row. It is
     * deliberately NOT panel_icon.png, which is the popout rail's related but
     * different popout_icons,2 sprite. */
    (void)xt_art_pixels(state, &state->art_over);
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
xt_label_slot(struct ToriRS_Api* api, char const* key, int fallback)
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
    struct ToriRS_Api* api,
    struct XtState* state,
    int skill,
    int which,
    char* out,
    size_t out_size)
{
    struct XtSkill const* s = &g_skill[skill];
    struct ToriRS_SkillSnapshot snapshot;
    struct XtProgress progress;
    int next_xp;
    long long remaining;
    int mean;

    assert(out);

    if( !xt_skill_snapshot(api, skill, &snapshot) )
        memset(&snapshot, 0, sizeof(snapshot));
    xt_progress(snapshot.xp, &progress);
    next_xp = progress.next_xp;
    remaining = next_xp > 0 ? next_xp - progress.xp : 0;
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
    struct ToriRS_Api* api,
    struct XtState* state,
    uint32_t* buf,
    int w,
    int h,
    int top,
    int skill,
    int const slot[4])
{
    struct XtSkill const* s = &g_skill[skill];
    char value[32];
    char text[48];
    struct ToriRS_SkillSnapshot snapshot;
    struct XtProgress progress;
    int xp;
    int level_xp;
    int next_xp;
    int level;
    int bar_y;
    int bar_w;
    int fill_w;
    int key_w = 0;
    int val_w;
    bool done;

    assert(buf);

    /* The box: the translucent black wash, then the cache's black outline.
     * Opening a detail block does not recolour the CS2 row. */
    xt_box_fill(buf, w, h, top);
    PluginDraw_Frame(buf, w, h, 0, top, w, XT_BOX_H, XT_BOX_BORDER);

    /* The icon, indexed BY SKILL ID -- skills.png is cut in that order. */
    if( g_skill_px && skill * XT_ICON < g_skill_w )
        PluginDraw_Blit(
            buf, w, h, XT_ICON_X, top + XT_ICON_Y, g_skill_px, g_skill_w,
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
        int const val_r = PluginDraw_TextWidth(&g_font, "88.888M");
        int const key_l = PluginDraw_TextWidth(&g_font, "XP>Goal: ");
        int const key_r = PluginDraw_TextWidth(&g_font, XT_GUTTER "Kills>Goal: ");
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
    xt_progress(snapshot.xp, &progress);
    xp = progress.xp;
    level_xp = progress.level_xp;
    next_xp = progress.next_xp;
    level = progress.level;
    bar_y = top + XT_BAR_Y;
    /* Only 200m is done. Level 99 continues through the cache's virtual-level
     * thresholds; level 126 continues to the final 200m "Max!" goal. */
    done = progress.done;
    /* Every span below is measured in the bar LAYER's width, which is what
     * leaves the box's outline, and three columns of its wash, beside it. */
    bar_w = w - 2 * XT_BAR_LAYER_INSET;
    if( bar_w < 0 )
        bar_w = 0;
    fill_w = done ? bar_w
                  : (int)(((long long)(xp - level_xp) * bar_w) / (next_xp - level_xp));
    if( fill_w < 0 )
        fill_w = 0;
    if( fill_w > bar_w )
        fill_w = bar_w;

    PluginDraw_Fill(
        buf, w, h, XT_BAR_LAYER_INSET, bar_y, bar_w, XT_BAR_H, XT_BAR_TRACK, 255);
    PluginDraw_Fill(
        buf, w, h, XT_BAR_LAYER_INSET, bar_y, fill_w, XT_BAR_H,
        done ? XT_BAR_DONE : XT_BAR_FILL, 255);

    /*
     * Its three labels: the level at each end, the percentage in the middle.
     * "Lvl. " is the cache's own prefix (torirs_xptracker_stat_level_label) --
     * a bare number at the end of a bar reads as a quantity, which is what the
     * number in the middle already is.
     */
    if( !done )
    {
        snprintf(text, sizeof(text), "Lvl. %d", level);
        PLUGIN_DRAW_TEXT(buf, w, h, XT_BAR_LABEL_L, bar_y + 2, text, XT_INK_KEY);
        if( progress.max_goal )
            snprintf(text, sizeof(text), "Max!");
        else
            snprintf(text, sizeof(text), "Lvl. %d", level + 1);
        PLUGIN_DRAW_TEXT_RIGHT(
            buf, w, h, w - XT_BAR_LABEL_R, bar_y + 2, text, XT_INK_KEY);
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
        buf, w, h, (w - PluginDraw_TextWidth(&g_font, text)) / 2, bar_y + 2, text,
        XT_INK_VALUE);
}

/**
 * The OVERVIEW box: the session's two totals, over the whole list.
 *
 * `torirs_xptracker_total_labels` (script 5367) is the whole recipe, and it is
 * a different shape from a skill's box rather than a special case of one: two
 * lines CENTRED in the 48, no bar, no progress, and a key column that starts
 * at 31 instead of hard against a 25px icon. The icon is the cache tracker's
 * own staticons2,7 graphic; it is not the popout rail's popout_icons,2
 * graphic, even though both depict the same three bars.
 */
static void
xt_draw_overview(struct XtState* state, uint32_t* buf, int w, int h, int top)
{
    long long gained = 0;
    long long rate = 0;
    char value[32];
    int const key_w = PluginDraw_TextWidth(&g_font, "Total XP Gained: ");
    int const val_w = PluginDraw_TextWidth(&g_font, "88.888M");
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

    xt_box_fill(buf, w, h, top);
    PluginDraw_Frame(buf, w, h, 0, top, w, XT_BOX_H, XT_BOX_BORDER);

    if( g_over_px )
        PluginDraw_Blit(
            buf, w, h, XT_OVER_ICON_X, top + (XT_BOX_H - XT_OVER_ICON) / 2 + 1, g_over_px,
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

/** The one CUSTOM well, and the heading of the detail block under it. */
#define XT_WELL "boxes"
#define XT_DETAIL_HEADING "sec_detail"

/** Which skills get a box, in stats-tab order. */
static void
xt_collect_boxes(struct ToriRS_Api* api, struct XtState* state)
{
    g_box_count = 0;
    for( int i = 0; i < g_skill_count && g_box_count < XT_SKILLS_MAX; i++ )
        if( xt_row_wanted(api, state, i) )
            g_box_skill[g_box_count++] = i;
}

/** The four label slots the user chose, in reading order. */
static void
xt_slots(struct ToriRS_Api* api, int out[4])
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
 * The well's INPUT identity: the order the boxes are in, and nothing else.
 *
 * The strip is one control, so a click in it is arithmetic on `y` against the
 * order that was painted. When that order or that count changes, the well
 * takes a new identity and a click queued against the old bitmap is refused
 * rather than delivered to whichever skill moved under the same y.
 *
 * No VALUE is in here, and that is the rule the whole family turns on: this
 * hash moving costs one row a fresh serial, and folding a readout into it
 * would mint one twice a second. What the picture SHOWS is the separate
 * question xt_compose_key answers. @see PorcelainRow::hit_key.
 */
static uint64_t
xt_hit_key(struct XtState const* state)
{
    uint64_t key = 1469598103934665603ull;

    assert(state);
    key ^= (uint64_t)g_box_count;
    key *= 1099511628211ull;
    for( int i = 0; i < g_box_count; i++ )
    {
        key ^= (uint64_t)(g_box_skill[i] + 1);
        key *= 1099511628211ull;
    }
    return key;
}

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
    struct ToriRS_Api* api,
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
    for( int i = 0; i < 4; i++ )
        XT_MIX(slot[i]);
    for( int i = 0; i < g_box_count; i++ )
    {
        int const which = g_box_skill[i];
        struct XtSkill const* sk = &g_skill[which];
        struct ToriRS_SkillSnapshot snapshot;
        char rendered[32];

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
        /* Hash the four strings the box actually paints as well as their
         * underlying counters. TTL changes with each elapsed whole second,
         * including the first minute while xt_hourly() is deliberately held
         * at its 60-second floor; hashing only the rates left a configured TTL
         * frozen until some unrelated value moved. This also keeps future
         * label formulas from having to duplicate their dependencies here. */
        for( int label = 0; label < 4; label++ )
        {
            xt_label_value(
                api, state, which, slot[label], rendered, sizeof(rendered));
            for( char const* at = rendered; *at; at++ )
                XT_MIX((unsigned char)*at);
            XT_MIX(0xffu); /* fence adjacent labels with identical text */
        }
    }
#undef XT_MIX
    return k;
}

/**
 * Rasterise every box into the buffer Porcelain handed over.
 *
 * One image for the whole list rather than one per box: a compose is the
 * expensive half of this plugin and the panel blits one picture either way,
 * so composing per box would pay for the same pixels with more calls. The
 * buffer arrives zeroed, which is what leaves the panel's own backing showing
 * between the boxes exactly as the interface's does between the CS2 rows.
 */
static bool
xt_paint_strip(struct ToriRS_Api* api, void* user, uint32_t* argb, int w, int h)
{
    struct XtState* state = user;
    int slot[4];

    assert(api);
    assert(state);
    assert(argb);

    xt_slots(api, slot);
    xt_draw_overview(state, argb, w, h, 0);
    for( int i = 0; i < g_box_count; i++ )
        xt_draw_box(
            api, state, argb, w, h, (i + 1) * XT_BOX_PITCH, g_box_skill[i], slot);
    return true;
}

/**
 * The well's draw pass: compose once per picture, blit once per pass.
 *
 * PANEL_DRAW does set a draw region, unlike on_draw_world, so the width the
 * strip is composed at is the context's own. `Porcelain_Derived` is what makes
 * "once per picture" true rather than aspirational: it paints at most once per
 * (key, hash of inputs) and hands back the same handle otherwise.
 */
static void
xt_paint_boxes(
    struct ToriRS_Api* api, void* user, char const* key, struct ToriRS_Graphics* draw)
{
    struct XtState* state = user;
    struct PorcelainDrawContext context;
    enum PorcelainDerivedState derived = PORCELAIN_DERIVED_PENDING;
    struct ToriRS_ImageRef strip;
    uint64_t inputs;
    int slot[4];
    int height;

    assert(api);
    assert(state);
    assert(key);
    assert(draw);

    if( !Porcelain_DrawContext(
            state->porcelain, draw, PORCELAIN_EL(NONE), &context) ||
        context.bounds.width <= 0 )
        return;
    /* PENDING art is the ordinary state for the first frames and the normal
     * one on web; a terminal state has already been reported once and asking
     * again is what this call no longer does. @see xt_art_pixels. */
    if( !xt_art_ready(api, state) )
        return;

    g_well_w = context.bounds.width;
    height = xt_well_h(state);
    xt_slots(api, slot);
    inputs = xt_compose_key(api, state, g_well_w, slot);
    strip = Porcelain_Derived(
        state->porcelain, XT_WELL, &inputs, sizeof(inputs), g_well_w, height,
        xt_paint_strip, state, &derived);
    if( derived == PORCELAIN_DERIVED_READY )
        draw->image(draw, strip, 0, 0, 255);
}

/* ------------------------------------------------------------------------ */
/* The detail block                                                          */
/* ------------------------------------------------------------------------ */

/** Is the selected skill still one the page has a box for? */
static bool
xt_detail_open(struct ToriRS_Api* api, struct XtState* state)
{
    assert(api);
    assert(state);
    return g_detail >= 0 && g_detail < g_skill_count &&
           xt_row_wanted(api, state, g_detail);
}

/**
 * The six readouts, restated into the state the description borrows from.
 *
 * Every one of them is derived from a clock, which is why they are computed on
 * the describe run rather than pushed: an unchanged string hashes the same and
 * reaches no setter at all.
 */
static void
xt_detail_readouts(struct ToriRS_Api* api, struct XtState* state)
{
    struct XtSkill const* skill;
    struct ToriRS_SkillSnapshot snapshot;
    struct XtProgress progress;
    char scratch[24];
    int next_xp;
    long long remaining;
    int mean;

    assert(api);
    assert(state);
    assert(g_detail >= 0);
    assert(g_detail < g_skill_count);

    skill = &g_skill[g_detail];
    if( !xt_skill_snapshot(api, g_detail, &snapshot) )
        memset(&snapshot, 0, sizeof(snapshot));
    xt_progress(snapshot.xp, &progress);
    next_xp = progress.next_xp;
    remaining = next_xp > 0 ? next_xp - progress.xp : 0;
    mean = xt_mean_action_xp(skill);

    snprintf(
        state->detail_name, sizeof(state->detail_name), "%s",
        snapshot.name[0] ? snapshot.name : "?");

    xt_commas(xt_gained(skill), state->detail_text[0], sizeof(state->detail_text[0]));
    xt_commas(
        xt_hourly(skill, skill->gained_since_reset), state->detail_text[1],
        sizeof(state->detail_text[1]));

    if( next_xp > 0 )
        xt_commas(remaining, state->detail_text[2], sizeof(state->detail_text[2]));
    else
        snprintf(state->detail_text[2], sizeof(state->detail_text[2]), "\xe2\x80\x94");

    xt_commas(skill->actions, scratch, sizeof(scratch));
    snprintf(
        state->detail_text[3], sizeof(state->detail_text[3]), "%s  (%lld/hr)", scratch,
        xt_hourly(skill, skill->actions_since_reset));

    if( mean > 0 && next_xp > 0 )
        xt_commas(
            (remaining + mean - 1) / mean, state->detail_text[4],
            sizeof(state->detail_text[4]));
    else
        snprintf(state->detail_text[4], sizeof(state->detail_text[4]), "\xe2\x80\x94");

    if( skill->skill_time_ms >= XT_SECOND_MS && skill->gained_since_reset > 0 &&
        next_xp > 0 )
        xt_duration(
            (remaining * (long long)(skill->skill_time_ms / 1000u)) /
                skill->gained_since_reset,
            state->detail_text[5],
            sizeof(state->detail_text[5]));
    else
        snprintf(state->detail_text[5], sizeof(state->detail_text[5]), "\xe2\x80\x94");
}

/* ------------------------------------------------------------------------ */
/* What a person did to the page                                             */
/* ------------------------------------------------------------------------ */

/**
 * A click in the box strip.
 *
 * The strip is ONE control, so the click arrives with well-local coordinates
 * and the row is arithmetic: the boxes are a fixed pitch and the order they
 * were DRAWN in is `built_box_skill`. Row zero is the session overview, so the
 * skill boxes begin one pitch down.
 */
static void
xt_well_pressed(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct XtState* state = user;
    int row;
    int skill;

    assert(api);
    assert(state);
    assert(action);
    if( action->kind != TORIRS_PANEL_ACTION_ACTIVATE )
        return;

    row = action->y / XT_BOX_PITCH - 1;
    skill = row >= 0 && row < g_built_box_count ? g_built_box_skill[row] : -1;
    /* Clicking the open box closes it, which is what makes the strip its own
     * way back out of a selection. */
    g_detail = skill >= 0 && skill != g_detail ? skill : -1;
    /*
     * One stamp, and the reconciler decides what it costs. A block that
     * appeared or went away is a different row SET and therefore the page's
     * one legitimate rebuild; a different skill under a block that was already
     * open is the same set, so it is the heading's text and six readouts.
     */
    Porcelain_Invalidate(state->porcelain);
}

static void
xt_pause_pressed(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    assert(action);
    if( g_detail < 0 || g_detail >= g_skill_count )
        return;
    g_skill[g_detail].paused = !g_skill[g_detail].paused;
    g_skill[g_detail].last_change_ms = api->core.frame_ms(api);
    Porcelain_Invalidate(state->porcelain);
}

static void
xt_reset_pressed(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    assert(action);
    if( g_detail < 0 || g_detail >= g_skill_count )
        return;
    xt_reset_skill(api, state, g_detail);
    g_detail = -1;
    Porcelain_Invalidate(state->porcelain);
}

static void
xt_reset_others_pressed(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    assert(action);
    if( g_detail < 0 || g_detail >= g_skill_count )
        return;
    /* The reference's "Reset others": everything BUT this one, which is how a
     * person keeps the skill they are training and clears the noise a trip
     * picked up around it. */
    for( int i = 0; i < g_skill_count; i++ )
        if( i != g_detail )
            xt_reset_skill(api, state, i);
    Porcelain_Invalidate(state->porcelain);
}

static void
xt_reset_rate_pressed(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    assert(action);
    if( g_detail < 0 || g_detail >= g_skill_count )
        return;
    /* Only the per-hour figures, keeping the session total -- @see
     * xt_reset_rate, which is XpStateSingle::resetPerHour. */
    xt_reset_rate(&g_skill[g_detail]);
    Porcelain_Invalidate(state->porcelain);
}

/* ------------------------------------------------------------------------ */
/* The description                                                           */
/* ------------------------------------------------------------------------ */

/** One KEY_VALUE readout of the detail block. */
static void
xt_readout(
    struct ToriRS_PorcelainDescribe* describe,
    char const* key,
    char const* label,
    char const* text)
{
    struct PorcelainRow row;

    assert(describe);
    memset(&row, 0, sizeof(row));
    row.key = key;
    row.kind = PORCELAIN_ROW_KEY_VALUE;
    row.label = label;
    row.text = text;
    Porcelain_Row(describe, &row);
}

/** One button of the detail block. Every one of them is servable. */
static void
xt_button(
    struct ToriRS_PorcelainDescribe* describe,
    char const* key,
    char const* caption,
    PorcelainRowActionFn on_action,
    struct XtState* state)
{
    struct PorcelainRow row;

    assert(describe);
    assert(state);
    memset(&row, 0, sizeof(row));
    row.key = key;
    row.kind = PORCELAIN_ROW_BUTTON;
    row.text = caption;
    row.on_action = on_action;
    row.user = state;
    Porcelain_Row(describe, &row);
}

/**
 * The page, described.
 *
 * The box order, the well's height, the picture's key and the six readouts are
 * all re-derived here on every run, and none of them is pushed. What the row
 * model does with the difference is the whole of the plan's cost model:
 *
 *   a new box            the well is TALLER and its input identity moved: one
 *                        set_height and one reidentify on that row, with the
 *                        rows around it and the reader's scroll untouched
 *   a readout moved      one set_text on the row that says it
 *   the picture moved    one redraw on the well
 *   the block opened     a different row SET, which is the page's one
 *                        legitimate rebuild
 *
 * It used to be that the first and the last were the same thing: any change to
 * the box ORDER called panel.invalidate, which clears the page, resets the
 * scroll to the top and retires every retained custom run. That is the flash.
 */
static void
xt_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct XtState* state = user;
    struct ToriRS_Api* api;
    struct PorcelainRow row;
    int slot[4];

    assert(describe);
    assert(state);
    api = state->api;
    assert(api);

    xt_collect_boxes(api, state);
    xt_slots(api, slot);

    memset(&row, 0, sizeof(row));
    row.key = XT_WELL;
    row.kind = PORCELAIN_ROW_CUSTOM;
    row.height = xt_well_h(state);
    row.hit_key = xt_hit_key(state);
    row.paint_key = xt_compose_key(api, state, g_well_w, slot);
    row.on_action = xt_well_pressed;
    row.paint = xt_paint_boxes;
    row.user = state;
    Porcelain_Row(describe, &row);
    /*
     * The order a click is resolved against, taken WITH the identity that
     * fences it.
     *
     * A click carries the serial of the widget it was aimed at, and the well
     * is re-identified on exactly the run that moves `hit_key` -- so a click
     * authored against the previous order fails that fence rather than
     * arriving here to be mapped by this table. Snapshotting anywhere else
     * splits the two: taken at the PAINT it lags the identity by a draw pass
     * that has not run yet, and taken at the BUILD it is not taken at all on a
     * run the row model resolved without re-declaring the page -- which, now
     * that a new box is a reidentify and not a rebuild, is every such run.
     */
    g_built_box_count = g_box_count;
    memcpy(
        g_built_box_skill, g_box_skill,
        (size_t)g_box_count * sizeof(g_built_box_skill[0]));

    if( !xt_detail_open(api, state) )
        return;

    xt_detail_readouts(api, state);

    memset(&row, 0, sizeof(row));
    row.key = XT_DETAIL_HEADING;
    row.kind = PORCELAIN_ROW_HEADING;
    row.text = state->detail_name;
    Porcelain_Row(describe, &row);

    xt_readout(describe, "d_gained", "XP gained", state->detail_text[0]);
    xt_readout(describe, "d_hr", "XP/hr", state->detail_text[1]);
    xt_readout(describe, "d_left", "XP to level", state->detail_text[2]);
    xt_readout(describe, "d_actions", "Actions", state->detail_text[3]);
    xt_readout(describe, "d_actleft", "Actions to level", state->detail_text[4]);
    xt_readout(describe, "d_ttl", "Time to level", state->detail_text[5]);

    /* The caption IS the state, and it is a setter now: the host's BUTTON
     * patch arm used to be an empty break, so pressing Pause left the button
     * saying Pause on every lane and every executor. @see host fix H1. */
    xt_button(
        describe, "d_pause", g_skill[g_detail].paused ? "Unpause" : "Pause",
        xt_pause_pressed, state);
    xt_button(describe, "d_reset", "Reset", xt_reset_pressed, state);
    xt_button(
        describe, "d_reset_others", "Reset others", xt_reset_others_pressed, state);
    xt_button(describe, "d_reset_rate", "Reset/hr", xt_reset_rate_pressed, state);
}

/* ------------------------------------------------------------------------ */
/* The state machine's own cadences                                          */
/* ------------------------------------------------------------------------ */

/**
 * Size the skill table, the first time the client can answer.
 *
 * NOT at on_start, and that is the whole point: a plugin starts when the
 * client boots, and the stat table does not exist until a session has one --
 * so a table sized there is sized to ZERO, permanently, for a plugin whose
 * every loop runs to g_skill_count. The assert that was supposed to catch it
 * is compiled out of a release build, so the symptom is not a crash: it is a
 * tracker that quietly never tracks anything for the whole session.
 *
 * The WALK is the half that was wrong. It used to stop at the first index
 * `skill` refused, which conflates three different answers: a skill the lane
 * does not have, a skill it has that the server has not stated, and the end of
 * the table. A lane with a hole therefore truncated silently and every skill
 * past the hole was invisible for the session. The snapshot tells them apart
 * now -- an absent index comes back with `index` -1 and a real one with its
 * own index and name -- so the walk is a BOUND and a refusal inside it is
 * skipped rather than obeyed. @see ToriRS_SkillSnapshot::stated.
 */
static void
xt_size_table(struct ToriRS_Api* api, struct XtState* state)
{
    struct ToriRS_SkillSnapshot snapshot;
    int highest = -1;
    bool any_stated = false;

    assert(api);
    assert(state);
    if( g_skill_count > 0 )
        return;

    for( int i = 0; i < XT_SKILLS_MAX; i++ )
    {
        if( xt_skill_snapshot(api, i, &snapshot) )
            any_stated = true;
        /* Filled on both false paths: this is "the lane HAS this skill", not
         * "the server has stated it". */
        if( snapshot.index >= 0 )
            highest = i;
    }
    /* No session yet; ask again next tick. */
    if( !any_stated )
        return;

    g_skill_count = highest + 1;
    for( int i = 0; i < g_skill_count; i++ )
    {
        memset(&g_skill[i], 0, sizeof(g_skill[i]));
        g_skill[i].start_xp = -1;
    }
    g_session_start_ms = api->core.frame_ms(api);
}

/**
 * Reconcile the saved session, once there are READINGS to reconcile onto.
 *
 * The gate is the whole of it: the names come out of the cache and answer as
 * soon as the client boots, while the xp arrives with the login burst, and in
 * between the pre-login table is a FRESH ACCOUNT's rather than an empty one.
 * Seeding from it reads the whole burst as one enormous gain -- the panel
 * snapping to the character's total the moment it appeared.
 */
static void
xt_apply_saved(struct ToriRS_Api* api, struct XtState* state)
{
    assert(api);
    assert(state);
    if( g_state_applied || !state->stats_ready || g_skill_count == 0 )
        return;
    g_state_applied = true;
    xt_state_apply(api, state);
    Porcelain_Invalidate(state->porcelain);
}

/** A skill has been STATED. @see PORCELAIN_READY_STATS, host fix H8. */
static void
xt_stats_ready(struct ToriRS_Api* api, void* user, unsigned what)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    (void)what;
    state->stats_ready = true;
    /* The names answer now even where they did not at on_start, so this is
     * also the first moment the table can be sized. */
    xt_size_table(api, state);
    xt_apply_saved(api, state);
}

/**
 * One reading per skill, per logic tick.
 *
 * The logged-out EDGE is here and not on a readiness watch because the layer
 * has no falling edge to offer: `Porcelain_WhenReady` fires when a bit comes
 * true and re-arms when the game goes away, and "the player has just logged
 * out" is the moment between those two. The state is KEPT rather than reset --
 * a hop is not a new session, and the saved-state reconciliation on the way
 * back in is what decides whether the xp that appeared meanwhile was yours.
 */
static void
xt_observe_all(struct ToriRS_Api* api, void* user, uint64_t elapsed_ms)
{
    struct XtState* state = user;
    struct ToriRS_PlayerSnapshot me;
    uint64_t now;
    bool logged_in;

    assert(api);
    assert(state);
    (void)elapsed_ms;

    now = api->core.frame_ms(api);
    logged_in = api->world.local_player(api, &me);

    xt_size_table(api, state);
    if( g_skill_count == 0 )
        return;
    xt_apply_saved(api, state);

    if( !logged_in )
    {
        if( g_logged_in )
        {
            if( xt_cfg_bool(api, "pause_on_logout") )
                for( int i = 0; i < g_skill_count; i++ )
                    g_skill[i].paused = true;
            xt_state_save(api, state);
            /* On the EDGE, not on a throttle: the moment the state changed is
             * the moment the page has to stop disagreeing with it. */
            Porcelain_Invalidate(state->porcelain);
        }
        g_logged_in = false;
        return;
    }

    g_logged_in = true;
    for( int i = 0; i < g_skill_count; i++ )
    {
        struct ToriRS_SkillSnapshot snapshot;
        if( xt_skill_snapshot(api, i, &snapshot) )
            xt_observe(state, i, snapshot.xp, now);
    }
}

/**
 * The per-second half: accumulate training time, and apply the two timers.
 *
 * Separate from the poll above because both timers are about xp NOT arriving,
 * which no xp event can announce. `elapsed_ms` is the REAL time since this
 * timer last fired rather than the second it asked for, which is what keeps a
 * rate honest across a frame budget that slipped.
 */
static void
xt_second(struct ToriRS_Api* api, void* user, uint64_t elapsed_ms)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    /* No stat table to read while logged out, and no clock that should run:
     * an idle logged-out session must not dilute the rate it measured. */
    if( !g_logged_in || g_skill_count == 0 )
        return;
    xt_tick_second(api, state, api->core.frame_ms(api), elapsed_ms);
}

/**
 * The page's numbers, twice a second, and only while it is on screen.
 *
 * Every readout on it is derived from a clock, so it would otherwise be
 * reformatted fifty times a second to say the same thing. Saying the
 * description is stale is all this does: what the run costs is decided by the
 * reconciler, and a run in which nothing moved costs no engine call at all.
 */
static void
xt_refresh(struct ToriRS_Api* api, void* user, uint64_t elapsed_ms)
{
    struct XtState* state = user;

    assert(api);
    assert(state);
    (void)elapsed_ms;
    if( !g_page_visible )
        return;
    Porcelain_Invalidate(state->porcelain);
}

/* ------------------------------------------------------------------------ */
/* The host callbacks                                                        */
/* ------------------------------------------------------------------------ */

static void
xt_start(struct ToriRS_Api* api, void* plugin_state)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    /* A page with no layer is a page that cannot exist. */
    assert(api->porcelain);

    memset(state, 0, sizeof(*state));
    state->api = api;
    g_detail = -1;
    g_well_w = TORIRS_PANEL_WIDTH_DEFAULT;
    state->art_font.name = "text.png";
    state->art_skills.name = "skills.png";
    state->art_over.name = "overview_icon.png";
    g_session_start_ms = api->core.frame_ms(api);

    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_XP_TRACKER, state);
    assert(state->porcelain);
    /* The cache popout's own XP Tracker icon (popout_icons,2 / sprite 3579).
     * @see script/plugins/assets/xp-tracker/panel_icon.txt. The PAGE face is
     * the only one this description covers: declaring nothing on the settings
     * face is what leaves the host presenting the form it generates from the
     * config schema. */
    Porcelain_Panel(
        state->porcelain, "panel_icon.png", TORIRS_PANEL_WIDTH_DEFAULT,
        PORCELAIN_FACE_PAGE);
    /*
     * The one thing this page cannot do right on both lanes, said out loud.
     *
     * skills.png is cut in RAW skill-id order, and dat1 and dat2 number the
     * stats differently -- so one of the two draws the wrong picture in every
     * box. The fix is the lane's own enum(stat, graphic) on the CS2 lane and
     * sideicons.dat on the CS1 one, which is a single verb with no engine half
     * yet. Declared rather than left quiet, so the day that verb lands this
     * declaration stops being true and says so.
     */
    Porcelain_ExpectUnsupported(
        state->porcelain, "skill_icon_lane_enum",
        "skills.png is keyed by raw skill id; the lanes number the stats "
        "differently");

    Porcelain_Describe(state->porcelain, xt_describe, state);
    /* The saved session may only be reconciled once the server has STATED a
     * reading. @see xt_apply_saved. */
    Porcelain_WhenReady(
        state->porcelain, PORCELAIN_READY_STATS, xt_stats_ready, state);
    Porcelain_Every(state->porcelain, PORCELAIN_LOGIC_TICK, xt_observe_all, state);
    Porcelain_EveryMs(state->porcelain, XT_SECOND_MS, xt_second, state);
    Porcelain_EveryMs(state->porcelain, XT_PANEL_REFRESH_MS, xt_refresh, state);

    /* Queued, not read: the file crosses the IO queue like every other asset,
     * and the answer arrives at on_asset. A load that is already resident
     * answers READY and no event follows, so the readiness watch has to try
     * the apply too. */
    if( xt_cfg_bool(api, "save_state") )
        (void)api->assets.request(api, XT_STATE_ASSET);
}

static void
xt_stop(struct ToriRS_Api* api, void* plugin_state)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    xt_state_save(api, state);
    /* Every image the layer owns -- the composed strip and the source art --
     * goes back here; the pixel copies are this plugin's own. */
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
    free(state->art_font.px);
    free(state->art_skills.px);
    free(state->art_over.px);
    api->assets.release(api, XT_STATE_ASSET);
    memset(state, 0, sizeof(*state));
}

static void
xt_asset(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_AssetEvent const* ev)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    assert(ev);
    /* Only once there are READINGS to reconcile onto; otherwise the readiness
     * watch does it the moment there are. @see xt_apply_saved. */
    if( ev->ok && ev->name && strcmp(ev->name, XT_STATE_ASSET) == 0 )
        xt_apply_saved(api, state);
    /*
     * And the art, warmed on the event rather than on the first draw.
     *
     * Not an optimisation and not a retry: it is WHEN the three source
     * pictures are claimed. Loading them only from the draw pass would mean a
     * page nobody has opened holds no handles -- which sounds better and is a
     * behaviour change, because the handles a plugin holds are the numbering
     * every handle allocated after them inherits. The ledger's cost column for
     * this row reads "boot only", and this is boot.
     */
    (void)xt_art_ready(api, state);
}

static void
xt_logic_tick(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_TickEvent const* event)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    (void)event;
    /* The library installs no callbacks of its own: the definition belongs to
     * the plugin and the host already registered it, so a cadence the layer
     * offers is a cadence the plugin forwards. */
    Porcelain_Tick(state->porcelain, PORCELAIN_LOGIC_TICK);
}

static void
xt_frame_start(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_FrameEvent const* event)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    (void)event;
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
}

static void
xt_config_changed(struct ToriRS_Api* api, void* plugin_state, char const* key)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    (void)key;
    /* One of the layer's own six inputs, forwarded: the four label slots and
     * hide_maxed are all read by the description. */
    Porcelain_Note(state->porcelain, PORCELAIN_INPUT_CONFIG);
}

static void
xt_ui_build(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    assert(panel);
    Porcelain_PanelBuild(state->porcelain, panel, view);
}

static void
xt_ui_action(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_PanelActionEvent const* event)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    assert(event);
    (void)Porcelain_PanelAction(state->porcelain, event);
}

static void
xt_ui_draw(
    struct ToriRS_Api* api,
    void* plugin_state,
    char const* node,
    struct ToriRS_Graphics* draw)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    assert(node);
    assert(draw);
    (void)Porcelain_PanelDraw(state->porcelain, node, draw);
}

/**
 * The shell moved, showed or hid this page.
 *
 * Both halves are description INPUTS: the width is in the picture's key, and
 * the page becoming presented is the moment its numbers have to be current.
 * This used to refresh and redraw without re-collecting the box order, so a
 * page shown after a new skill had been trained displayed up to half a second
 * of stale strip and then flashed when the next tick found the topology stale
 * and re-declared the whole page.
 */
static void
xt_ui_layout(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_PanelLayoutEvent const* ev)
{
    struct XtState* state = plugin_state;

    assert(api);
    assert(state);
    assert(ev);
    g_page_visible = ev->visible;
    if( ev->width > 0 )
        g_well_w = ev->width;
    Porcelain_Invalidate(state->porcelain);
}

static struct ToriRS_ConfigItem const XT_CONFIG[] = {
    { "save_state",        TORIRS_CONFIG_BOOL, "Save between sessions",        "1", 0, 0,  NULL, 0 },
    { "hide_maxed",        TORIRS_CONFIG_BOOL, "Hide maxed skills",            "0", 0, 0,  NULL, 0 },
    { "pause_on_logout",   TORIRS_CONFIG_BOOL, "Pause on logout",              "1", 0, 0,  NULL, 0 },
    { "pause_skill_after", TORIRS_CONFIG_INT,  "Auto pause after (minutes)",   "0", 0, 60, NULL, 0 },
    { "reset_rate_after",  TORIRS_CONFIG_INT,  "Auto reset rate after (minutes)", "0", 0, 60, NULL, 0 },
    /* The four corner slots of a box's 2x2 grid, and the reference's own
     * choice list for them (XpPanelLabel). The DEFAULTS are its defaults --
     * gained, rate, actions left, xp left -- which is also the pairing the
     * cache's own tracker hard-codes. @see enum XtLabel. */
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

struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "xp-tracker",
    .title = "XP Tracker",
    .version = "3.0.0",
    .state_size = sizeof(struct XtState),
    .config = &XT_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = xt_start,
        .on_stop = xt_stop,
        .on_asset = xt_asset,
        .on_config_changed = xt_config_changed,
        .on_frame_start = xt_frame_start,
        .on_logic_tick = xt_logic_tick,
        .on_ui_build = xt_ui_build,
        .on_ui_action = xt_ui_action,
        .on_ui_draw = xt_ui_draw,
        .on_ui_layout = xt_ui_layout,
    },
};
