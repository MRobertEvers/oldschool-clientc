#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimap orbs: hitpoints, run energy and special attack, beside the minimap.
 *
 * ## Why a plugin and not a gameframe
 *
 * The orbs are a 2013 addition to the OSRS interface, so on a rev-239 cache
 * they are interface 160 and the client draws them for itself. On everything
 * older -- the LostCity 254/289 worlds, the 2004 gameframe, anything built
 * from a dat1 cache -- there is no such interface and no way to author one:
 * the numbers exist (UPDATE_STAT, UPDATE_RUNENERGY, the special-attack varp
 * are all on the wire) and the picture does not.
 *
 * The four are hitpoints, prayer, run energy and special attack, in the
 * reference's own order and at its own positions.
 *
 * A plugin is the right shape for that gap precisely because it is not tied to
 * a revision. It brings its own art, reads the numbers through the api rather
 * than through a cache table, and anchors to wherever THIS gameframe put the
 * minimap. Nothing here knows which era it is running on.
 *
 * ## The art is the plugin's, not the cache's
 *
 * The ten PNGs in `script/plugins/assets/minimap-orbs/` are shipped beside this
 * source. They were cut from the rev-239 sprites table once, at authoring
 * time, and they are the plugin's own files now -- so this draws the same orbs
 * on a 2004 cache, on a cache that failed to open, and on a client started
 * with no cache at all.
 *
 * That is deliberate and it is the whole reason api->image_load exists. The
 * alternative -- naming graphic 1071 by cache id -- would work on exactly the
 * one revision the ids came from and silently draw whatever is numbered 1071
 * everywhere else.
 *
 * ## How one orb is built
 *
 * Exactly as interface 160 builds it, which is why the art lines up without a
 * single hand-tuned offset:
 *
 *   1. `frame`      57x34, the stone plate, at the orb's origin.
 *   2. a fill disc  26x26 at +27,+4 -- the orb's colour at FULL.
 *   3. `fill_empty` 26x26 at the same place, clipped to the UNFILLED rows at
 *                   the top. A meter is a dark disc covering a bright one, not
 *                   a bright one being scaled: scaling squashes the sphere.
 *   4. an icon      26x26 at the same place; the heart, the boot, the swords.
 *   5. the value    centred in the plate's left panel, in the interfaces' own
 *                   yellow.
 *
 * ## Reading the numbers
 *
 * Hitpoints and run energy have api calls of their own, because both arrive in
 * packets on every revision this client speaks. The special attack does not:
 * it is a VARP, and a varp id is a property of the cache. So it is resolved
 * three ways, in order -- the plugin's own config key, the boot profile's
 * `[varp:special_attack_energy]`, and the id every revision from 2004 to
 * today has used -- and the orb switches itself off if none of them answer.
 * See orbs_varp.
 *
 * ## Clicking one
 *
 * An orb is a button in the reference and it is a button here. Each one claims
 * its plate with api->hit_region, so the mouseover line names the verb, the
 * right-click menu offers it and a left click runs it -- one declaration, all
 * three -- and the click is answered with api->if_click, which presses the
 * interface button the gameframe already has for that job.
 *
 * Pressing the real button rather than writing the var is what makes this
 * work at all: the run toggle and the special attack are the SERVER's, and a
 * client that flipped the varp locally would show a state the server never
 * agreed to and lose it on the next sync. Which component that button is
 * cannot be known from here -- it is a different one in every gameframe and no
 * profile names it -- so it comes from config, and an orb told nothing offers
 * no verb rather than pressing something at random.
 */

/* The plate, and where interface 160 puts each piece inside it. */
#define ORB_W 57
#define ORB_H 34
#define ORB_DISC_X 27
#define ORB_DISC_Y 4
#define ORB_DISC 26
/* The value panel, exactly as every orb authors it: a 23x13 box at 4,16 with
 * halign 1 and valign 1. */
#define ORB_TEXT_X 4
#define ORB_TEXT_Y 16
#define ORB_TEXT_W 23
#define ORB_TEXT_H 13
#define ORB_TEXT_CX (ORB_TEXT_X + ORB_TEXT_W / 2)
/* Only the fallback path's, which draws through api->draw_text and so takes a
 * baseline rather than a box. */
#define ORB_TEXT_BASELINE (ORB_TEXT_Y + ORB_TEXT_H / 2 + 4)
/** Yellow, as every orb's own `colour=16776960` states it. */
#define ORB_TEXT_RGB 0xFFFF00u

/**
 * Where each orb sits, relative to the column's origin.
 *
 * The reference's own four positions, from interface 160: the orbs do not
 * stack in a straight line, they step out to the right as they go down,
 * because they are following the curve of the minimap they hang off. A fixed
 * pitch puts the lower ones under the map instead of beside it.
 *
 * Orbs fill these slots in order, so a client showing three of them uses the
 * first three and the curve is continuous -- rather than leaving the gap where
 * the prayer orb would have been.
 */
static const struct
{
    int dx;
    int dy;
} ORB_SLOT[] = {
    { 0,  37  },
    { 0,  71  },
    { 10, 103 },
    { 32, 128 },
};
#define ORB_SLOT_COUNT ((int)(sizeof(ORB_SLOT) / sizeof(ORB_SLOT[0])))

/** Hitpoints and prayer, in the skill order that has not moved since 2001. */
#define ORB_STAT_HITPOINTS 3
#define ORB_STAT_PRAYER 5

/*
 * The ids the three-step resolve below falls back on.
 *
 * Both have been these numbers since 2004 and are these numbers on every cache
 * in this tree, which is what makes them a reasonable LAST resort -- and why
 * they are a last resort rather than a constant: a lane that moved one says so
 * in its profile, and a lane that moved one and did not gets an orb that is
 * wrong rather than a client that is broken.
 */
#define ORB_VARP_SPEC_FALLBACK 300
#define ORB_VARP_RUN_FALLBACK 173
/** `^sa_max_energy`: the special attack bar is 0..1000, not 0..100. */
#define ORB_SPEC_MAX 1000

static struct ToriRS_PluginApi const* g_api;

/** The plugin's art, by the order it is loaded in. */
enum OrbImage
{
    ORB_IMG_FRAME = 0,
    ORB_IMG_FRAME_OVER,
    ORB_IMG_FILL_EMPTY,
    ORB_IMG_FILL_RED,
    ORB_IMG_FILL_GREY,
    ORB_IMG_FILL_GOLD,
    ORB_IMG_FILL_CYAN,
    ORB_IMG_FILL_CYAN_LIT,
    ORB_IMG_FILL_PRAYER,
    ORB_IMG_ICON_HP,
    ORB_IMG_ICON_PRAYER,
    ORB_IMG_ICON_WALK,
    ORB_IMG_ICON_RUN,
    ORB_IMG_ICON_SPEC,
    /* The digits, as one row of glyphs. @see orbs_draw_number. */
    ORB_IMG_DIGITS,
    ORB_IMG_COUNT
};

static char const* const ORB_IMAGE_FILE[ORB_IMG_COUNT] = {
    "frame.png",       "frame_over.png", "fill_empty.png",    "fill_red.png",    "fill_grey.png",
    "fill_gold.png",   "fill_cyan.png",  "fill_cyan_lit.png", "fill_prayer.png", "icon_hp.png",
    "icon_prayer.png", "icon_walk.png",  "icon_run.png",      "icon_spec.png",   "digits.png",
};

/** Handles from api->image_load, or -1 while a load has not been asked for. */
static int g_image[ORB_IMG_COUNT];

/** What a click on each orb means. Handed to api->hit_region and read back in
 *  EV_CANVAS_CLICK; the host does not look at these. */
enum OrbTag
{
    ORB_TAG_HP = 1,
    ORB_TAG_PRAYER,
    ORB_TAG_RUN,
    ORB_TAG_SPEC
};

/**
 * The interface button an orb presses, as `<interface>:<component>[:<op>]`.
 *
 * A string and not three int keys because the three are one ANSWER -- "the run
 * toggle is this button" -- and splitting it across three rows of the settings
 * panel invites two of them being right. The op defaults to 0, the classic
 * unnumbered button, which is what a 2004 gameframe's toggles are.
 *
 * @return 1 when `spec` named a button, 0 when it is empty or malformed (and
 * then nothing is written to the outputs).
 */
static int
orbs_parse_button(
    char const* spec,
    int* out_component,
    int* out_op)
{
    int a = -1;
    int b = -1;
    int op = 0;
    int fields;

    assert(out_component);
    assert(out_op);

    if( !spec || !spec[0] )
        return 0;
    fields = sscanf(spec, "%d:%d:%d", &a, &b, &op);
    if( fields < 1 || a < 0 )
        return 0;
    if( op < 0 || op > 10 )
        return 0;

    /*
     * ONE number is an id already, two are an interface and a component in it.
     *
     * Both spellings exist because both eras do. A dat1 interface numbers
     * every component flatly -- LostCity's run toggle is 153, full stop -- and
     * a dat2 one addresses `(interface << 16) | index`, which is far easier to
     * read as the pair the wire and the interface tree both speak in.
     */
    if( fields == 1 )
    {
        *out_component = a;
        *out_op = 0;
        return 1;
    }
    if( b < 0 || a > 0xFFFF || b > 0xFFFF )
        return 0;
    *out_component = (a << 16) | b;
    *out_op = op;
    return 1;
}

/**
 * The button an orb presses: the plugin's config first, the boot profile's
 * `[iface:<name>]` second.
 *
 * Same three-step shape as orbs_varp, and for the same reason -- which
 * component a button is cannot be known from here. The profile is where a lane
 * states it (the LostCity run toggle is `controls:com_5`, id 153; the rev-239
 * one is interface 160's `runbutton`), and the config key is the escape hatch
 * for a server that has moved one and has no profile entry to say so.
 *
 * @return 1 when a button was named, and then `out_*` describe it.
 */
static int
orbs_button(
    struct ToriRS_PluginCtx* ctx,
    char const* key,
    char const* name,
    int* out_component,
    int* out_op)
{
    int declared;

    if( orbs_parse_button(g_api->cfg_str(ctx, key), out_component, out_op) )
        return 1;

    declared = g_api->cache_id(ctx, "iface", name);
    if( declared < 0 )
        return 0;
    *out_component = declared;
    *out_op = 0;
    return 1;
}

/**
 * The verbs an orb offers, into `out`, which must hold
 * TORIRS_PLUGIN_REGION_OPS_MAX entries.
 *
 * An orb offers a verb only when the lane has told the plugin which button it
 * presses -- there is nothing to put in the menu otherwise, and a row that
 * does nothing is worse than no row. The REGION is still claimed either way,
 * which is what keeps a click on an orb from falling through to the minimap
 * behind it and walking the player.
 *
 * @return how many were written.
 */
static int
orbs_ops(
    struct ToriRS_PluginCtx* ctx,
    char const* key,
    char const* name,
    char const* verb,
    char const** out)
{
    int component;
    int op;

    assert(out);
    if( !orbs_button(ctx, key, name, &component, &op) )
        return 0;
    out[0] = verb;
    return 1;
}

static void
orbs_load_images(struct ToriRS_PluginCtx* ctx)
{
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
    {
        if( g_image[i] >= 0 )
            continue;
        g_image[i] = g_api->image_load(ctx, ORB_IMAGE_FILE[i]);
    }
}

/**
 * Which varp holds `name` on this cache: the plugin's override, the profile's
 * declaration, then the historical id.
 *
 * Three steps and not one, because the three answer different questions and
 * only the middle one is knowable from here. The config key is the escape
 * hatch for a private server that moved a var and has no profile entry to say
 * so; `[varp:<name>]` is where a lane states it properly; and the fallback is
 * what keeps a lane that has declared nothing -- every dat1 profile in this
 * tree, until now -- drawing an orb instead of hiding it.
 *
 * @param key this plugin's config key holding an override, or -1 for none.
 * @return the id, or -1 when even the fallback is switched off (`0`).
 */
static int
orbs_varp(
    struct ToriRS_PluginCtx* ctx,
    char const* key,
    char const* name,
    int fallback)
{
    int const override = g_api->cfg_int(ctx, key);
    int declared;

    if( override > 0 )
        return override;
    /* 0 is a legal varp id, so "switched off" needs a value of its own: a
     * config key set to 0 means "this lane has no such var, draw nothing". */
    if( override == 0 )
        return -1;

    declared = g_api->cache_id(ctx, "varp", name);
    if( declared >= 0 )
        return declared;
    return fallback;
}

/*
 * The orb face, shipped as the plugin's own font.
 *
 * `digits.png` is one row of glyphs and `digits.ini` says where each of them
 * is in it -- both cut from the rev-239 orb face (cache font 494) at authoring
 * time, in the orbs' own yellow with the reference's drop shadow already on
 * them. See tools/fontbake_atlas.py.
 *
 * Why not api->draw_text: that verb draws in the CLIENT's hitsplat face,
 * because that is the one face the overlay layer can be sure of. It is a
 * chunky combat face and it is not what an orb's number is set in -- the
 * difference is immediately visible beside a cache that draws its own orbs.
 * Laying the digits out here costs one blit per digit and gets the right
 * picture on a lane whose cache has no such font at all, which is the whole
 * reason this plugin brings its own art.
 */
struct OrbGlyph
{
    /** Where in the atlas, and how big. */
    int x;
    int y;
    int w;
    int h;
    /** Where inside the line box, and how far the pen moves after it. */
    int off_x;
    int off_y;
    int advance;
};

static struct OrbGlyph g_digit[10];
static int g_digits_ready;
/** Colour steps in the atlas, and how far apart their rows are. @see
 *  tools/fontbake_atlas.py `ramp:N`. 1 step is a single-colour atlas. */
static int g_digit_steps = 1;
static int g_digit_row_h;
/** The face's vertical metrics, as ToriDraw2D_DrawStringBox reads them. @see
 *  orbs_text_origin. */
static int g_digit_line_height;
static int g_digit_max_ascent;
static int g_digit_max_descent;

/**
 * Read `digits.ini` into the table above.
 *
 * Parsed by hand rather than through a config key, because it is not a
 * SETTING: it is the other half of the atlas, generated beside it, and a user
 * editing it would only ever break the pairing. Returns 1 once the table is
 * usable; a file that has not landed yet, or that carries no digit rows, keeps
 * the caller on its fallback.
 */
static int
orbs_load_digits(struct ToriRS_PluginCtx* ctx)
{
    char const* at;
    int size = 0;

    if( g_digits_ready )
        return 1;

    if( !g_api->asset_load(ctx, "digits.ini") )
        return 0;
    at = (char const*)g_api->asset_data(ctx, "digits.ini", &size);
    if( !at || size <= 0 )
        return 0;

    for( char const* end = at + size; at < end; )
    {
        /* The asset is a byte range, not a C string -- PlatformX_IO hands back
         * exactly the bytes it read, with no terminator -- so every line is
         * copied out before it is parsed. atoi/sscanf run to a NUL, and on the
         * last line of the file that NUL is past the end of the allocation. */
        char line[128];
        char const* start = at;
        char const* stop = start;
        size_t len;

        while( stop < end && *stop != '\n' )
            stop++;
        at = stop < end ? stop + 1 : end;
        if( stop > start && stop[-1] == '\r' )
            stop--;

        len = (size_t)(stop - start);
        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';

        /*
         * The face's vertical metrics, which orbs_text_origin needs to place a
         * centred line the way ToriDraw2D_DrawStringBox places one. Read
         * before the glyph rows because they sit above them in the file, and
         * because a zero here is not a harmless default -- it moves the number
         * several pixels down the panel.
         */
        if( len > 12 && strncmp(line, "line_height=", 12) == 0 )
        {
            g_digit_line_height = atoi(line + 12);
            continue;
        }
        if( len > 11 && strncmp(line, "max_ascent=", 11) == 0 )
        {
            g_digit_max_ascent = atoi(line + 11);
            continue;
        }
        if( len > 12 && strncmp(line, "max_descent=", 12) == 0 )
        {
            g_digit_max_descent = atoi(line + 12);
            continue;
        }
        /* `<digit>=x y w h ox oy advance`. Anything else -- the header
         * comments, the `ascent=` line -- is skipped by the same test. */
        if( len > 6 && strncmp(line, "steps=", 6) == 0 )
        {
            g_digit_steps = atoi(line + 6);
            continue;
        }
        if( len > 11 && strncmp(line, "row_height=", 11) == 0 )
        {
            g_digit_row_h = atoi(line + 11);
            continue;
        }
        if( len < 3 || line[0] < '0' || line[0] > '9' || line[1] != '=' )
            continue;
        {
            struct OrbGlyph* g = &g_digit[line[0] - '0'];
            if( sscanf(
                    line + 2,
                    "%d %d %d %d %d %d %d",
                    &g->x,
                    &g->y,
                    &g->w,
                    &g->h,
                    &g->off_x,
                    &g->off_y,
                    &g->advance) == 7 )
                g_digits_ready = 1;
        }
    }
    return g_digits_ready;
}

/**
 * Where a vertically CENTRED line's glyph offsets are measured from, inside a
 * box `h` tall whose top is `y`.
 *
 * ToriDraw2D_DrawStringBox's own arithmetic, and it has to be run rather than
 * approximated:
 *
 *     base_y0 = max_ascent + (h - max_ascent - max_descent) / 2
 *     origin  = y + base_y0 - line_height
 *
 * For the orb face those cancel exactly -- ascent 10, descent 2, box 13, line
 * height 10 -- so the origin is the box's own y and a digit lands one pixel
 * into it. Centring the glyph CELL in the box by eye instead lands it a pixel
 * lower, which is what the numbers were doing, and would land it somewhere
 * else again on any other face.
 */
static int
orbs_text_origin(
    int y,
    int h)
{
    int const space = h - g_digit_max_ascent - g_digit_max_descent;

    /*
     * Metrics that never arrived would put the number several pixels down the
     * panel rather than nowhere, which is the kind of wrong that looks like a
     * taste decision. The box's own top is the answer for a face whose ascent
     * fills its line box -- which is every face an orb is set in -- so it is
     * the honest fallback as well as the common case.
     */
    if( g_digit_line_height <= 0 || g_digit_max_ascent <= 0 )
        return y;
    return y + g_digit_max_ascent + space / 2 - g_digit_line_height;
}

/**
 * `value`, centred on `cx`, with `top` as the line box's top.
 *
 * A glyph is drawn by sliding the WHOLE atlas so the glyph lands where it
 * belongs and clipping to the glyph's own box -- which is what the clip
 * argument on api->draw_image is for, and why it is an argument rather than a
 * second entry point.
 */
static void
orbs_draw_number(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int cx,
    int top,
    int value,
    int filled,
    int total)
{
    char text[16];
    int len;
    int width = 0;
    int pen;
    int row = 0;

    /*
     * The colour is the METER's, not the number's: clientscript 449 ramps it
     * from red at empty through yellow at half to green at full, so a glance
     * at the colour says as much as reading the digits. The atlas carries that
     * ramp as rows (see tools/fontbake_atlas.py); this picks the row.
     */
    if( g_digit_steps > 1 && total > 0 )
    {
        int const clamped = filled < 0 ? 0 : (filled > total ? total : filled);
        row = clamped * (g_digit_steps - 1) / total;
    }

    snprintf(text, sizeof(text), "%d", value);
    len = (int)strlen(text);

    for( int i = 0; i < len; i++ )
        width += g_digit[text[i] - '0'].advance;
    pen = cx - width / 2;

    for( int i = 0; i < len; i++ )
    {
        struct OrbGlyph const* g = &g_digit[text[i] - '0'];
        int const dx = pen + g->off_x;
        int const dy = top + g->off_y;

        int const src_y = g->y + row * g_digit_row_h;

        g_api->draw_image(
            ctx, surface, g_image[ORB_IMG_DIGITS], dx - g->x, dy - src_y, dx, dy, g->w, g->h, 0);
        pen += g->advance;
    }
}

/**
 * One orb, drawn.
 *
 * `value` is what the panel reads and `pct` how full the disc is, and they are
 * separate on purpose: the run orb shows a percent and fills by it, while the
 * hitpoints orb shows a LEVEL and fills by its share of the base level. A
 * single number could only serve one of them.
 */
static void
orbs_draw_one(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int x,
    int y,
    int fill_image,
    int icon_image,
    int value,
    int filled,
    int total,
    uint32_t tag,
    char const* const* ops,
    int op_count,
    int inactive)
{
    char text[16];
    int hidden;
    int trans;

    if( g_image[ORB_IMG_FRAME] < 0 )
        return;

    /*
     * The three states the reference draws an orb in, and what each one
     * changes (clientscript 2792):
     *
     *   INACTIVE  a GREY disc at `if_settrans(50)` and the ops cleared -- what
     *             the spec orb is when nothing you are holding has a special
     *             attack.
     *   idle      the orb's own colour at trans 25, which is what
     *             `specenergy_indicator` is authored with.
     *   HOVERED   the same, with the lit plate (graphic 1072 beside 1071).
     *
     * The plate, the lit plate and the hit region are the HOST's now: they
     * were declared as the part's art and ops in orbs_chrome, and the host
     * picks the hovered plate itself from the pointer and the box. What is
     * drawn here is what changes every tick -- the disc, its cap, the icon
     * and the number -- over the plate the host already put down.
     */
    (void)ops;
    (void)op_count;
    (void)tag;
    trans = inactive ? 50 : 25;
    if( inactive )
    {
        fill_image = ORB_IMG_FILL_GREY;
        /* Full, so no dark cap is drawn over it: an inactive orb shows no
         * level at all, rather than a level of zero. */
        filled = total;
    }

    g_api->draw_image(
        ctx, surface, g_image[fill_image], x + ORB_DISC_X, y + ORB_DISC_Y, 0, 0, 0, 0, trans);

    /*
     * The dark disc over the unfilled part, clipped to the rows above the
     * fill line -- interface 160's `orb_*_empty` container, whose height its
     * clientscript sets and whose 26x26 child is cut by it.
     *
     * Rounded so that a meter which is not quite full never reads as full: a
     * player on 98 of 99 hitpoints must see a sliver of dark, or the orb is
     * lying about the one thing it exists to say.
     */
    hidden = total > 0 ? ORB_DISC - (filled * ORB_DISC + total - 1) / total : ORB_DISC;
    if( hidden < 0 )
        hidden = 0;
    if( hidden > ORB_DISC )
        hidden = ORB_DISC;
    if( hidden > 0 )
        g_api->draw_image(
            ctx,
            surface,
            g_image[ORB_IMG_FILL_EMPTY],
            x + ORB_DISC_X,
            y + ORB_DISC_Y,
            x + ORB_DISC_X,
            y + ORB_DISC_Y,
            ORB_DISC,
            hidden,
            0);

    g_api->draw_image(
        ctx, surface, g_image[icon_image], x + ORB_DISC_X, y + ORB_DISC_Y, 0, 0, 0, 0, 0);

    if( g_digits_ready && g_image[ORB_IMG_DIGITS] >= 0 )
    {
        orbs_draw_number(
            ctx,
            surface,
            x + ORB_TEXT_CX,
            y + orbs_text_origin(ORB_TEXT_Y, ORB_TEXT_H),
            value,
            filled,
            total);
        return;
    }

    /* The atlas has not landed yet, or would not decode. The client's own
     * overlay face is the wrong one for an orb, but a number in the wrong face
     * beats an orb with no number in it. */
    snprintf(text, sizeof(text), "%d", value);
    g_api->draw_text(ctx, surface, x + ORB_TEXT_CX, y + ORB_TEXT_BASELINE, text, ORB_TEXT_RGB);
}

/*
 * ## The orbs are PARTS
 *
 * Each orb is a chrome part -- `orb_hitpoints`, `orb_prayer`, `orb_run`,
 * `orb_spec` -- claimed at start and, where this revision has no such thing,
 * ADDED and hung off the minimap. That is what lets this plugin coexist with
 * whatever else provides orbs: on an OldSchool cache the four names bind to
 * interface 160's own orb roots, so claiming them hides the cache's orbs and
 * this plugin draws exactly one set; on a 2004 cache nothing binds, so the
 * parts are introduced here; and on a frame where a gameframe plugin already
 * drew them, the claim comes back 0 and this plugin draws none of that orb --
 * and lays its remaining ones out AROUND it, because a part somebody else
 * holds still answers where it is.
 *
 * The split of labour: the HOST paints the plate and owns the hit region from
 * this plugin's declaration (chrome_paint / chrome_ops in EV_CHROME), and this
 * plugin draws what changes every tick -- the fill, the icon, the number -- on
 * top of it in EV_DRAW_CANVAS. Hover is the host's: it has the pointer and the
 * box and picks the lit plate itself.
 */

enum OrbIndex
{
    ORB_HP = 0,
    ORB_PRAYER,
    ORB_RUN,
    ORB_SPEC,
    ORB_COUNT
};

static struct
{
    char const* part;
    char const* label;
    uint32_t tag;
} const ORB_PART[ORB_COUNT] = {
    { "orb_hitpoints", "the hitpoints orb", ORB_TAG_HP },
    { "orb_prayer", "the prayer orb", ORB_TAG_PRAYER },
    { "orb_run", "the run orb", ORB_TAG_RUN },
    { "orb_spec", "the special attack orb", ORB_TAG_SPEC },
};

/** Per-orb claim state. */
static struct
{
    /** Scopes this plugin holds, 0 for an orb it does not draw. */
    int held;
    /** The claim has been tried against a frame that had somewhere to put
     *  it: an add that failed because the anchor was not there yet is
     *  retried, one that failed for good is not. */
    int settled;
    /** The verbs last declared, so a change (run on -> off) re-declares. */
    char ops_sig[128];
    /** The role this orb was ADDED under, empty for one that was claimed
     *  rather than introduced. A claimed part is the lane's node and its
     *  parent is not ours to move; an added one is ours. @see orbs_reanchor. */
    char anchor[TORIRS_PLUGIN_ROLE_NAME_MAX];
} g_orb[ORB_COUNT];

/** Every claim has been tried once against a frame that could answer. */
static int g_orbs_reported;

/** The names the user reads, joined for the one line the chatbox gets. */
static void
orbs_append(char* buf, size_t cap, char const* what)
{
    size_t const len = strlen(buf);
    if( len == 0 )
        snprintf(buf, cap, "%s", what);
    else
        snprintf(buf + len, cap - len, ", %s", what);
}

/*
 * Which node the column hangs off, and what that costs in arithmetic.
 *
 * The plate and the disc are drawn by two different halves of this file -- the
 * HOST paints the plate from the chrome part, this plugin draws the disc in
 * EV_DRAW_CANVAS -- and each is placed by naming a role. chrome_add emits the
 * part after its anchor's own subtree; role_anchor emits the primitives after
 * the anchored role's own subtree. It is the same rule, so the two halves land
 * together only while they name the SAME role.
 *
 * They did not. The part was added to `minimap` while the draw anchored to
 * `minimap_edge`, and on any frame that has both -- the 2004 assembly, where
 * the housing is a plate with the map's hole cut out of it and therefore has
 * to paint AFTER the map -- that put the plate between the map and its
 * housing and the disc on top of the housing. Which is exactly what the
 * player sees: orbs whose backs are missing, hidden behind the map's edge.
 *
 * So the role is resolved ONCE, here, and every site uses this one answer.
 *
 * The column's arithmetic stays in the MAP's terms -- `offset_x` is documented
 * to the user as "offset from minimap left", and the y is a share of the map's
 * height -- so what the anchor changes is only the origin those numbers are
 * measured from. dx/dy carry that difference. They are zero when the anchor IS
 * the map, so the fallback needs no path of its own.
 */
struct OrbAnchor
{
    /** The role the plate hangs off AND the draw anchors to. Never NULL. */
    char const* role;
    /** Added to a map-relative box to make it anchor-relative. */
    int dx;
    int dy;
    /** The map square's height; the column's y is a share of it. */
    int map_h;
    /** The housing resolved, so this is the role the column WANTS. 0 means
     *  the map square is standing in for it. @see orbs_reanchor. */
    int preferred;
};

/** The anchor for this pass, or 0 when the frame has no minimap right now. */
static int
orbs_anchor(struct ToriRS_PluginCtx* ctx, struct OrbAnchor* out)
{
    int mx = 0;
    int my = 0;
    int mw = 0;
    int mh = 0;
    int ex = 0;
    int ey = 0;

    assert(ctx);
    assert(out);

    /* No map means nothing to hang off, on either half. A state, not a fault:
     * the login screen and a cutscene that took the map away both land here. */
    if( !g_api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_MINIMAP, &mx, &my, &mw, &mh) )
        return 0;

    out->map_h = mh;

    /*
     * `minimap_edge` is the LAST piece of the map assembly -- the housing --
     * and a readout drawn beside the map has to sit on top of it. A frame that
     * has no such thing, because a gameframe plugin declared its housing with
     * layout_slot_overlay and so paints it inside the map's own boundary,
     * answers 0 here, and there the map itself is already the right answer.
     *
     * Asked whether it is ON SCREEN and not only where it is. The lane's own
     * plate keeps its box after a gameframe layout suppresses it, so a box
     * alone would hang the column off a node that paints nothing and the
     * column would paint nothing with it. role_visible says whether the
     * object is provided -- by the lane, or by a plugin that replaced it and
     * paints it at the tombstone -- which is the question this is asking.
     */
    if( g_api->role_visible(ctx, "minimap_edge") &&
        g_api->role_rect(ctx, "minimap_edge", &ex, &ey, NULL, NULL) )
    {
        out->role = "minimap_edge";
        out->dx = mx - ex;
        out->dy = my - ey;
        out->preferred = 1;
    }
    else
    {
        out->role = "minimap";
        out->dx = 0;
        out->dy = 0;
        out->preferred = 0;
    }
    return 1;
}

/**
 * Where one orb's plate goes, relative to the anchor.
 *
 * One copy of the sum, because the claim states this box once and EV_CHROME
 * restates it on every pass: two spellings of it is a column that walks
 * whenever only one of them is edited.
 */
static void
orbs_box(
    struct ToriRS_PluginCtx* ctx,
    struct OrbAnchor const* anchor,
    int orb,
    int* out_x,
    int* out_y)
{
    assert(ctx);
    assert(anchor);
    assert(out_x);
    assert(out_y);

    *out_x = g_api->cfg_int(ctx, "offset_x") - ORB_W + ORB_SLOT[orb].dx + anchor->dx;
    *out_y = anchor->map_h / 4 + g_api->cfg_int(ctx, "offset_y") - ORB_SLOT[0].dy +
             ORB_SLOT[orb].dy + anchor->dy;
}

/**
 * Try every claim. Called at start and again after each layout pass until
 * every orb is settled -- an ADD needs an anchor with a box, and the first
 * layout may not have given the minimap one yet.
 */
static void
orbs_claim_all(struct ToriRS_PluginCtx* ctx)
{
    struct ToriRS_PluginChromePart initial;
    char missing[192] = "";
    int provided = 0;
    int pending = 0;

    assert(ctx);

    memset(&initial, 0, sizeof(initial));
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
        initial.art[i] = -1;
    initial.w = ORB_W;
    initial.h = ORB_H;

    for( int i = 0; i < ORB_COUNT; i++ )
    {
        int got;

        if( g_orb[i].settled )
        {
            if( g_orb[i].held )
                provided++;
            continue;
        }

        got = g_api->chrome_claim(ctx, ORB_PART[i].part, TORIRS_PLUGIN_CHROME_SCOPE_ALL, 1);
        if( got > 0 )
        {
            g_orb[i].held = got;
            g_orb[i].settled = 1;
            provided++;
            continue;
        }
        if( got == 0 )
        {
            /* Somebody else draws it. The player's screen is right; the log
             * is where this belongs and the chatbox is not. */
            char const* who = g_api->chrome_owner(
                ctx, ORB_PART[i].part, TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE);
            g_api->log(ctx, "'%s' is provided by '%s'; not drawing one",
                ORB_PART[i].part, who ? who : "another plugin");
            g_orb[i].settled = 1;
            continue;
        }

        /* Nothing on this revision has it: introduce one, hung off the map
         * assembly. On the SAME role the draw anchors to -- name a different
         * one and the plate and the disc come out on opposite sides of the
         * housing. @see orbs_anchor. */
        {
            struct OrbAnchor anchor;

            if( !orbs_anchor(ctx, &anchor) )
            {
                /* No map yet: not a refusal, just not now. */
                pending++;
                continue;
            }
            orbs_box(ctx, &anchor, i, &initial.x, &initial.y);
            got = g_api->chrome_add(
                ctx, ORB_PART[i].part, anchor.role, TORIRS_PLUGIN_ANCHOR_AFTER, &initial);
            if( got > 0 )
                snprintf(g_orb[i].anchor, sizeof(g_orb[i].anchor), "%s", anchor.role);
        }
        if( got > 0 )
        {
            g_orb[i].held = got;
            g_orb[i].settled = 1;
            provided++;
        }
        else if( got == 0 )
        {
            char const* who = g_api->chrome_owner(
                ctx, ORB_PART[i].part, TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE);
            g_api->log(ctx, "'%s' is provided by '%s'; not drawing one",
                ORB_PART[i].part, who ? who : "another plugin");
            g_orb[i].settled = 1;
        }
        else
        {
            orbs_append(missing, sizeof(missing), ORB_PART[i].label);
            g_orb[i].settled = 1;
        }
    }

    if( pending || g_orbs_reported )
        return;
    g_orbs_reported = 1;

    if( !provided )
    {
        g_api->disable_self(ctx, "this gameframe has no minimap to hang orbs off");
        return;
    }
    if( missing[0] )
    {
        /* A part NOBODY ends up providing is the one thing the player is
         * told about: they switched a feature on and cannot see it. */
        char msg[256];
        snprintf(msg, sizeof(msg), "Minimap orbs: no room on this gameframe for %s.", missing);
        g_api->notify(ctx, msg);
        g_api->log(ctx, "%s", msg);
    }
}

/*
 * Move an added orb when the anchor it should hang off has changed.
 *
 * The plate's parent is chosen ONCE, at chrome_add, and the host is explicit
 * that it stays: "an added part keeps the anchor it was introduced with;
 * re-anchoring is a different part, not the same one said again". So the only
 * way to correct one is to release it -- which, for an added part, removes it
 * -- and add it again.
 *
 * That is needed because the two roles do not become available together. The
 * map square carries its size in the layout (`w=146 h=151`) and so has a box
 * from the first pass; the housing is a SPRITE, and it has no box until its
 * pixels arrive -- which on a lane that streams its cache over on-demand can
 * be a good while after the map is placed. An orb added in that window went
 * under `minimap`, settled there for good, and spent the session painted into
 * the gap between the map and its housing while the discs drew on top of it.
 * The picture that report describes: orbs with no backs.
 *
 * So it is re-asked instead of assumed. Only parts this plugin ADDED are
 * touched: a claimed one is the lane's own node, and where the lane puts it is
 * not this plugin's business.
 */
static void
orbs_reanchor(struct ToriRS_PluginCtx* ctx, struct OrbAnchor const* anchor)
{
    assert(ctx);
    assert(anchor);

    /*
     * UPWARDS only: onto the housing, never back down off it.
     *
     * The move is worth making once, when the housing's pixels finally give it
     * a box. Making it in both directions would let a frame that briefly loses
     * that box -- a rebuild, a re-decode -- drag every plate down onto the map
     * and back again, and each leg is a release, an add and a layout
     * notification. An orb that has really lost its housing loses it along
     * with the anchor it hangs off, and is rebuilt with the frame.
     */
    if( !anchor->preferred )
        return;

    for( int i = 0; i < ORB_COUNT; i++ )
    {
        if( !g_orb[i].held || g_orb[i].anchor[0] == '\0' )
            continue;
        if( strcmp(g_orb[i].anchor, anchor->role) == 0 )
            continue;

        g_api->log(ctx, "'%s' moves from '%s' to '%s'",
            ORB_PART[i].part, g_orb[i].anchor, anchor->role);
        /* Releasing every scope of an ADDED part removes it, which is what
         * makes the add below a fresh introduction under the new role. */
        (void)g_api->chrome_claim(ctx, ORB_PART[i].part, TORIRS_PLUGIN_CHROME_SCOPE_ALL, 0);
        g_orb[i].held = 0;
        g_orb[i].settled = 0;
        g_orb[i].anchor[0] = '\0';
        g_orb[i].ops_sig[0] = '\0';
    }
}

/** Any orb whose claim has not yet been tried against a frame that could
 *  answer it. @see orbs_draw, which is where the retry actually happens. */
static int
orbs_unsettled(void)
{
    for( int i = 0; i < ORB_COUNT; i++ )
        if( !g_orb[i].settled )
            return 1;
    return 0;
}

static enum ToriRS_PluginVerdict
orbs_layout_changed(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;
    orbs_claim_all(ctx);
    return TORIRS_PLUGIN_PASS;
}

/**
 * The verbs one orb offers right now, and a signature of them so a change
 * (the run orb flips between "Turn run on" and "Turn run off") re-declares.
 */
static int
orbs_ops_for(
    struct ToriRS_PluginCtx* ctx, int orb, char const** ops, char* sig, size_t sig_cap)
{
    int n = 0;

    switch( orb )
    {
    case ORB_HP:
        n = orbs_ops(ctx, "hp_button", "orb_hp_button", "Cure", ops);
        break;
    case ORB_PRAYER:
        n = orbs_ops(ctx, "prayer_button", "orb_prayer_button", "Quick-prayers", ops);
        break;
    case ORB_RUN:
        n = orbs_ops(ctx, "run_button", "orb_run_on", "Toggle Run", ops);
        break;
    case ORB_SPEC:
        n = orbs_ops(ctx, "spec_button", "orb_spec_button", "Use Special Attack", ops);
        break;
    default:
        break;
    }
    sig[0] = '\0';
    for( int i = 0; i < n; i++ )
    {
        size_t const len = strlen(sig);
        snprintf(sig + len, sig_cap - len, "%s|", ops[i]);
    }
    return n;
}

/**
 * EV_CHROME: state the plate and the click for every orb this plugin holds.
 * The box is only read for a part this plugin holds the POSITION of (an added
 * one); on a cache orb the box is the cache's.
 */
static enum ToriRS_PluginVerdict
orbs_chrome(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    orbs_load_images(ctx);

    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct ToriRS_PluginChromePart part;
        char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };
        int n;

        if( !g_orb[i].held )
            continue;

        memset(&part, 0, sizeof(part));
        for( int s = 0; s < TORIRS_PLUGIN_CHROME_STATE_COUNT; s++ )
            part.art[s] = -1;
        part.art[TORIRS_PLUGIN_CHROME_IDLE] = g_image[ORB_IMG_FRAME];
        part.art[TORIRS_PLUGIN_CHROME_HOVER] = g_image[ORB_IMG_FRAME_OVER];
        part.w = ORB_W;
        part.h = ORB_H;
        if( g_orb[i].held & TORIRS_PLUGIN_CHROME_SCOPE_POSITION )
        {
            struct OrbAnchor anchor;

            /* A pass with no map restates no box: the part keeps the one it
             * has rather than being moved to the origin for a frame. */
            if( orbs_anchor(ctx, &anchor) )
                orbs_box(ctx, &anchor, i, &part.x, &part.y);
        }
        g_api->chrome_paint(ctx, ORB_PART[i].part, &part);

        /* Claimed even with no verbs: the region's other job is to stop the
         * click falling through to the map tile under the orb. */
        n = orbs_ops_for(ctx, i, ops, g_orb[i].ops_sig, sizeof(g_orb[i].ops_sig));
        g_api->chrome_ops(ctx, ORB_PART[i].part, ops, n, ORB_PART[i].tag);
    }
    return TORIRS_PLUGIN_PASS;
}

/** Where an orb this plugin holds is, or 0 for one it does not hold or one
 *  the frame has nowhere for right now. */
static int
orbs_part_box(struct ToriRS_PluginCtx* ctx, int orb, int* out_x, int* out_y)
{
    struct ToriRS_PluginChromePart part;

    if( !g_orb[orb].held )
        return 0;
    if( !g_api->chrome_part(ctx, ORB_PART[orb].part, &part) )
        return 0;
    *out_x = part.x;
    *out_y = part.y;
    return 1;
}

static enum ToriRS_PluginVerdict
orbs_draw(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDrawCanvas* ev = (struct ToriRS_PluginEvDrawCanvas*)event;
    struct OrbAnchor anchor;
    int x;
    int y;

    assert(ctx);
    assert(ev);

    /*
     * Nothing to anchor to means nothing to draw. A gameframe with no minimap
     * -- the login screen, a cutscene that took it away -- is a state, not a
     * fault, and the orbs simply are not there for it.
     *
     * Asked for by ROLE rather than through minimap_rect, which is the same
     * rectangle reached through the single-purpose verb that predates the
     * vocabulary. One name for the map means a plugin that reads it and a
     * layout that PLACES it cannot come to disagree about where it is.
     */
    if( !orbs_anchor(ctx, &anchor) )
        return TORIRS_PLUGIN_PASS;

    /*
     * The claim, retried HERE and not only from EV_LAYOUT_CHANGED.
     *
     * An ADD needs the map to have a box, and at EV_START it usually has none,
     * so the first attempt fails and the orb waits to be told the layout
     * moved. That telling is not guaranteed to come after the map is placed.
     * On a lane where no plugin owns the gameframe, app.c announces the layout
     * once -- on the first tree it sees -- and then not again until something
     * marks it dirty; a boot whose map was not placed on that one pass was
     * left with four unclaimed orbs, no plate, no hit region and no verbs,
     * until the player opened a panel and dirtied the layout by hand. Which is
     * exactly the report: the orbs do not work until you open the settings
     * menu.
     *
     * EV_CHROME cannot be the retry either. The host dispatches it only to a
     * plugin that already HOLDS a claim, so a plugin whose every claim failed
     * is never asked again -- the one state that needs the retry is the one
     * state that cannot receive it. The draw is the only event that arrives
     * every frame regardless of what this plugin holds, which is the same
     * reason the image loads below are retried from here.
     *
     * Reaching this line means the map HAS a box, which is the condition the
     * add was waiting on; and orbs_claim_all is a no-op once every orb has
     * settled, so the steady state costs a four-iteration loop.
     */
    orbs_reanchor(ctx, &anchor);
    if( orbs_unsettled() )
        orbs_claim_all(ctx);

    /*
     * And drawn AS PART OF the minimap, not merely beside it.
     *
     * Reading the map's rectangle answers where to put the orbs; it says
     * nothing about what they ARE, and without this declaration they are a
     * global canvas overlay -- the topmost surface the client has, painted and
     * hit-tested above every interface in the tree. That is wrong in the two
     * places it shows: a modal or a sidebar panel drawn over the minimap gets
     * four orbs floating on top of it, and a click meant for that panel lands
     * on an orb instead. Neither is visible on a plain gameframe, which is why
     * a column that reads its position from the map can look correct for a
     * long time while belonging to nothing.
     *
     * Anchored to a role, so the picture and the position cannot disagree: the
     * orbs emit immediately after that role's own subtree, under its PARENT
     * clip -- which is what lets the column hang off the map's left edge and
     * past its bottom while still being cut by the panel that houses the map
     * -- and they inherit its fate. A gameframe that hides, moves or rebuilds
     * the thing they hang off hides, moves and rebuilds these.
     *
     * WHICH role that is was settled by orbs_anchor, and this is the same
     * answer the plate was hung off -- which is the whole point of resolving
     * it in one place. The map surface and the chrome that wraps it are two
     * different nodes, and on every frame that has both the wrapper is painted
     * AFTER the surface, because it is a plate with a hole in it. Naming one
     * role here and the other at chrome_add is what put the discs on top of
     * the housing and their plates underneath it.
     *
     * Zero means the role did not resolve for this pass. Every declaration
     * after it would be dropped by the host anyway; returning here says so
     * once instead of drawing nine images into a discard.
     */
    /* AFTER the housing: the whole point of hanging off `minimap_edge` is
     * to sit on top of it. */
    if( !g_api->role_anchor(ctx, anchor.role, TORIRS_PLUGIN_ANCHOR_AFTER) )
        return TORIRS_PLUGIN_PASS;

    /* Asked for every frame, not only at start: an image that failed its read
     * on the first attempt is retried, and a plugin re-enabled after a reload
     * has no handles at all. Both are answered by the same line, and a handle
     * it already has costs a table scan. */
    orbs_load_images(ctx);
    orbs_load_digits(ctx);

    /*
     * Where each orb IS is answered by its part, not computed here: an added
     * part's box was stated relative to the map in orbs_chrome and the host
     * has already made it absolute; a cache orb's box is the cache's own. An
     * orb this plugin does not hold is simply not drawn, and the ones it does
     * hold keep the places they were given -- which is what lets a column of
     * two coexist with somebody else's two without stacking on them.
     */
    x = 0;
    y = 0;

    if( g_api->cfg_bool(ctx, "show_hp") && orbs_part_box(ctx, ORB_HP, &x, &y) )
    {
        int current = 0;
        int base = 0;
        g_api->stat(ctx, ORB_STAT_HITPOINTS, &current, &base);
        /* A client that has not been told its stats yet reads 0/0, and a
         * hitpoints orb showing zero out of zero looks like a death. */
        if( base > 0 )
        {
            char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };
            int const n = orbs_ops(ctx, "hp_button", "orb_hp_button", "Cure", ops);
            orbs_draw_one(
                ctx,
                ev->surface,
                x,
                y,
                ORB_IMG_FILL_RED,
                ORB_IMG_ICON_HP,
                current,
                current,
                base,
                ORB_TAG_HP,
                ops,
                n,
                0);
        }
    }

    if( g_api->cfg_bool(ctx, "show_prayer") && orbs_part_box(ctx, ORB_PRAYER, &x, &y) )
    {
        int current = 0;
        int base = 0;
        g_api->stat(ctx, ORB_STAT_PRAYER, &current, &base);
        if( base > 0 )
        {
            char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };
            /* The reference's own verb: `prayerbutton` carries `op1=*` over
             * the name "Quick-prayers", which reads as one row. */
            int const n = orbs_ops(ctx, "prayer_button", "orb_prayer_button", "Quick-prayers", ops);
            orbs_draw_one(
                ctx,
                ev->surface,
                x,
                y,
                ORB_IMG_FILL_PRAYER,
                ORB_IMG_ICON_PRAYER,
                current,
                current,
                base,
                ORB_TAG_PRAYER,
                ops,
                n,
                0);
        }
    }

    if( g_api->cfg_bool(ctx, "show_run") && orbs_part_box(ctx, ORB_RUN, &x, &y) )
    {
        char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };
        char sig[128];
        int const n = orbs_ops_for(ctx, ORB_RUN, ops, sig, sizeof(sig));
        int const energy = g_api->run_energy(ctx);
        /* The verb follows the state, and the host's region carries the verb
         * -- so a change here restates the claim, which re-asks for the
         * declaration on the next chrome tick. */
        if( strcmp(sig, g_orb[ORB_RUN].ops_sig) != 0 )
            g_api->chrome_claim(ctx, ORB_PART[ORB_RUN].part, TORIRS_PLUGIN_CHROME_SCOPE_HITBOX, 1);
        int const run_varp = orbs_varp(ctx, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
        /* Run ON is the gold disc and the running boot; walking is the grey
         * disc and the standing one -- the same pair the reference swaps. A
         * lane with no run var reads as walking rather than as an error. */
        int const running = run_varp >= 0 && g_api->varp(ctx, run_varp) != 0;
        {
            orbs_draw_one(
                ctx,
                ev->surface,
                x,
                y,
                ORB_IMG_FILL_GOLD,
                running ? ORB_IMG_ICON_RUN : ORB_IMG_ICON_WALK,
                energy,
                energy,
                100,
                ORB_TAG_RUN,
                ops,
                n,
                /* Walking is the run orb's OFF state, and it wears the same
                 * grey the spec orb wears when nothing can spend it: the disc
                 * says whether running is on, and the number beside it still
                 * says how much energy there is. The gold disc and the running
                 * boot are the on state. */
                !running);
        }
    }

    if( g_api->cfg_bool(ctx, "show_spec") && orbs_part_box(ctx, ORB_SPEC, &x, &y) )
    {
        int const spec_varp =
            orbs_varp(ctx, "spec_varp", "special_attack_energy", ORB_VARP_SPEC_FALLBACK);
        int const spec_max = g_api->cfg_int(ctx, "spec_max");
        if( spec_varp >= 0 && spec_max > 0 )
        {
            char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };
            int const n =
                orbs_ops(ctx, "spec_button", "orb_spec_button", "Use Special Attack", ops);
            /*
             * INACTIVE when nothing can spend it.
             *
             * The reference asks whether the weapon in the worn slot has a
             * special attack at all (clientscript 2792 reads its cost out of
             * enum 906) and greys the orb out when it does not. This client's
             * plugin layer cannot see equipment or read an enum, so it asks
             * the nearest question it CAN: whether this world has named a
             * button for the orb to press. On a world with no special attack
             * anywhere -- every LostCity one -- that is the same answer for
             * the same reason, and it is permanently right rather than
             * momentarily wrong.
             *
             * The per-weapon half is still missing, and looks like this: an
             * orb that stays lit while holding a rune scimitar. Closing it
             * needs the worn obj and an enum lookup in the api.
             */
            int const inactive = n == 0;
            int energy = g_api->varp(ctx, spec_varp);
            int const armed = g_api->varp(ctx, spec_varp + 1) > 0;

            if( energy < 0 )
                energy = 0;
            if( energy > spec_max )
                energy = spec_max;
            orbs_draw_one(
                ctx,
                ev->surface,
                x,
                y,
                /* The lit disc while the special is ARMED, the plain one
                 * otherwise -- graphic 1608 beside 1607, as 2792 picks them. */
                armed ? ORB_IMG_FILL_CYAN_LIT : ORB_IMG_FILL_CYAN,
                ORB_IMG_ICON_SPEC,
                /* The panel reads a PERCENT, as the reference's does; the bar
                 * itself is in thousandths and only the fill uses them. */
                energy * 100 / spec_max,
                energy,
                spec_max,
                ORB_TAG_SPEC,
                ops,
                n,
                inactive);
        }
    }

    return TORIRS_PLUGIN_PASS;
}

/*
 * An orb was used: press the button its config names.
 *
 * One handler for all three, because what differs between them is a config key
 * and nothing else -- there is no orb-specific behaviour here, only a
 * different button on the same interface.
 */
static enum ToriRS_PluginVerdict
orbs_click(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvCanvasClick* ev = (struct ToriRS_PluginEvCanvasClick*)event;
    char const* key = NULL;
    char const* name = NULL;
    int component = -1;
    int op = 0;

    assert(ctx);
    assert(ev);

    switch( ev->tag )
    {
    case ORB_TAG_HP:
        key = "hp_button";
        name = "orb_hp_button";
        break;
    case ORB_TAG_PRAYER:
        key = "prayer_button";
        name = "orb_prayer_button";
        break;
    case ORB_TAG_RUN:
        /*
         * The run toggle is TWO buttons wherever the gameframe states run as a
         * choice rather than as a switch: LostCity's controls tab carries a
         * walk button and a run button, each `buttontype=select` on the same
         * varp, and pressing the one that is already selected does nothing.
         * So the orb presses the OTHER one -- and a lane that names only the
         * first has a single toggle, which is the rev-239 shape.
         */
        {
            int const run_varp = orbs_varp(ctx, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
            int const running = run_varp >= 0 && g_api->varp(ctx, run_varp) != 0;
            char const* off_key = "run_button_off";
            char const* off_name = "orb_run_off";

            if( running && orbs_button(ctx, off_key, off_name, &component, &op) )
            {
                key = off_key;
                break;
            }
            key = "run_button";
            name = "orb_run_on";
        }
        break;
    case ORB_TAG_SPEC:
        key = "spec_button";
        name = "orb_spec_button";
        break;
    default:
        return TORIRS_PLUGIN_PASS;
    }

    /* No button named is the ordinary state on a lane nobody has told this
     * plugin about, and the region offered no verb for it -- so the click that
     * got here is a click on an orb that says it does nothing, and it does
     * nothing. */
    if( component < 0 && !orbs_button(ctx, key, name, &component, &op) )
        return TORIRS_PLUGIN_PASS;

    if( !g_api->if_click(ctx, component, op) )
    {
        char msg[128];

        /*
         * Said to the PLAYER, not only to the log.
         *
         * "The orb does nothing" is the least debuggable report a control can
         * produce, and the cause is always the same one thing: the component
         * this world was told to press is not in the interface tree -- the
         * gameframe puts that button somewhere else, or does not have it at
         * all. Naming the id turns a dead button into a line someone can act
         * on, and it is a client-owned control, so the client is entitled to
         * explain itself.
         */
        snprintf(
            msg,
            sizeof(msg),
            "Minimap orbs: this world has no interface component %d for '%s'.",
            component,
            key);
        g_api->notify(ctx, msg);
        g_api->log(ctx, "%s", msg);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orbs_start(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)event;
    (void)userdata;

    /* The handles are file-static and this plugin can be stopped and started
     * again, which drops every image the host held for it. Forgetting them
     * here is what makes the next start ask for them afresh instead of drawing
     * with handles the host has since handed to someone else. */
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        g_image[i] = -1;
    g_digits_ready = 0;
    orbs_load_images(ctx);
    orbs_load_digits(ctx);

    /* Every claim, NOW -- before a pixel is drawn -- so the arbitration
     * happens at the moment the user flipped the switch. Adds that need a map
     * the frame has not laid out yet are retried from EV_LAYOUT_CHANGED. */
    memset(g_orb, 0, sizeof(g_orb));
    g_orbs_reported = 0;
    orbs_claim_all(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orbs_stop(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;

    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        g_image[i] = -1;
    g_digits_ready = 0;

    /*
     * And the claims, which the HOST has just released for us.
     *
     * `held` is this plugin's memory of scopes the host no longer records, and
     * `settled` is its promise not to ask for them again. Kept across a stop,
     * the two combine into a plugin that comes back from a re-enable believing
     * it owns four parts it does not: orbs_claim_all skips every one as
     * settled, so nothing is ever re-added, and orbs_chrome paints and states
     * verbs for parts the host will not accept them for. The orbs simply never
     * reappear -- switched off and on again in the settings panel, they are
     * gone for the rest of the session.
     *
     * A stop is the end of everything this plugin knew, so it ends here rather
     * than being repaired later.
     */
    memset(g_orb, 0, sizeof(g_orb));
    g_orbs_reported = 0;
    return TORIRS_PLUGIN_PASS;
}

static void
orbs_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, orbs_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, orbs_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, orbs_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CANVAS_CLICK, orbs_click, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CHROME, orbs_chrome, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT_CHANGED, orbs_layout_changed, NULL);
}

/*
 * `-1` on the two varp keys means "work it out", which is the answer for every
 * lane in this tree; a number is the override a private server needs. They are
 * shown in the panel rather than hidden because the whole point of them is
 * that someone whose orb reads wrong can fix it without a rebuild.
 */
static struct ToriRS_PluginConfigItem const ORBS_CONFIG[] = {
    { "show_hp",        TORIRS_PLUGIN_CFG_BOOL,   "Hitpoints orb",                  "1",    0,    0,      NULL, 0 },
    { "show_prayer",    TORIRS_PLUGIN_CFG_BOOL,   "Prayer orb",                     "1",    0,    0,      NULL, 0 },
    { "show_run",       TORIRS_PLUGIN_CFG_BOOL,   "Run energy orb",                 "1",    0,    0,      NULL, 0 },
    { "show_spec",      TORIRS_PLUGIN_CFG_BOOL,   "Special attack orb",             "1",    0,    0,      NULL, 0 },
    { "offset_x",       TORIRS_PLUGIN_CFG_INT,    "Offset from minimap left",       "6",    -512, 512,    NULL, 0 },
    { "offset_y",       TORIRS_PLUGIN_CFG_INT,    "Offset from the anchor",         "-3",   -512, 512,    NULL, 0 },
    { "run_varp",       TORIRS_PLUGIN_CFG_INT,    "Run mode varp (-1 auto)",        "-1",   -1,   65535,  NULL, 0 },
    { "spec_varp",
     TORIRS_PLUGIN_CFG_INT,                       "Special attack varp (-1 auto)",
     "-1",                                                                                  -1,
     65535,                                                                                               NULL,
     0                                                                                                            },
    { "spec_max",       TORIRS_PLUGIN_CFG_INT,    "Special attack bar maximum",     "1000", 1,    100000, NULL, 0 },
    /*
     * The buttons each orb presses, `<interface>:<component>[:<op>]`, empty
     * for none.
     *
     * Empty by DEFAULT, on every lane, and that is the safe answer rather than
     * a missing one: a wrong id here is not an orb that does nothing, it is an
     * IF_BUTTON sent to a server about a component the player never touched.
     * Filling these in is a per-world job, which is what a config key is.
     */
    { "hp_button",      TORIRS_PLUGIN_CFG_STRING, "Hitpoints orb button",           "",     0,    0,      NULL, 0 },
    { "prayer_button",  TORIRS_PLUGIN_CFG_STRING, "Prayer orb button",              "",     0,    0,      NULL, 0 },
    { "run_button",     TORIRS_PLUGIN_CFG_STRING, "Run orb button (turns run on)",  "",     0,    0,      NULL, 0 },
    { "run_button_off",
     TORIRS_PLUGIN_CFG_STRING,                    "Run orb button (turns run off)",
     "",                                                                                    0,
     0,                                                                                                   NULL,
     0                                                                                                            },
    { "spec_button",    TORIRS_PLUGIN_CFG_STRING, "Special attack orb button",      "",     0,    0,      NULL, 0 },
    { NULL,             TORIRS_PLUGIN_CFG_BOOL,   NULL,                             NULL,   0,    0,      NULL, 0 },
};

_Static_assert(
    ORB_SPEC_MAX == 1000,
    "the spec_max default above states this number too");

struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS = {
    .name = "minimap-orbs",
    .title = "Minimap Orbs",
    .version = "1.0.0",
    .priority = 0,
    .config = ORBS_CONFIG,
    /*
     * ON by default now. It used to be off because on a rev-239 cache the
     * gameframe draws these orbs ITSELF, from interface 160, and a plugin
     * drawing a second set over them was two of everything. The four orb
     * names now bind to interface 160's own roots on that lane, so the claim
     * this plugin takes at start HIDES the cache's orb and draws this one in
     * its place -- exactly one set, on every lane, with nothing for the user
     * to know.
     */
    .disabled_by_default = false,
    .init = orbs_init,
    .shutdown = NULL,
};
