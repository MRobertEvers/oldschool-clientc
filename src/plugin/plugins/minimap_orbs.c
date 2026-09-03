#include "plugin/torirs_plugin_v2.h"

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
 * That is deliberate and it is the whole reason api->assets.image exists. The
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
 * An orb is a button in the reference and it is a button here. Each canonical
 * named node retains its hit rectangle and verb; on_ui_node_action resolves
 * the lane's configured component and cache.invoke presses it.
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
/* Only the fallback path's, which draws through DrawBuilder.text and so takes a
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

enum OrbIndex
{
    ORB_HP = 0,
    ORB_PRAYER,
    ORB_RUN,
    ORB_SPEC,
    ORB_COUNT
};

static char const* const ORB_IMAGE_FILE[ORB_IMG_COUNT] = {
    "frame.png",       "frame_over.png", "fill_empty.png",    "fill_red.png",    "fill_grey.png",
    "fill_gold.png",   "fill_cyan.png",  "fill_cyan_lit.png", "fill_prayer.png", "icon_hp.png",
    "icon_prayer.png", "icon_walk.png",  "icon_run.png",      "icon_spec.png",   "digits.png",
};

struct OrbGlyph
{
    int x;
    int y;
    int w;
    int h;
    int off_x;
    int off_y;
    int advance;
};

/** All mutable data belongs to one registered V2 instance. */
struct OrbsState
{
    struct ToriRS_ImageRef image[ORB_IMG_COUNT];
    struct OrbGlyph digit[10];
    int digits_ready;
    int digit_steps;
    int digit_row_h;
    int digit_line_height;
    int digit_max_ascent;
    int digit_max_descent;
    struct ToriRS_UiNodeRef node[ORB_COUNT];
};

static bool
orbs_cfg_bool(struct ToriRS_ApiV2* api, char const* key)
{
    bool value = false;
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
orbs_cfg_int(struct ToriRS_ApiV2* api, char const* key)
{
    int value = 0;
    (void)api->config.get_int(api, key, &value);
    return value;
}

static char const*
orbs_cfg_string(struct ToriRS_ApiV2* api, char const* key)
{
    char const* value = "";
    (void)api->config.get_string(api, key, &value);
    return value ? value : "";
}

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
    struct ToriRS_ApiV2* api,
    char const* key,
    char const* name,
    int* out_component,
    int* out_op)
{
    int declared;

    if( orbs_parse_button(orbs_cfg_string(api, key), out_component, out_op) )
        return 1;

    if( !api->cache.named_id(api, "iface", name, &declared) )
        return 0;
    *out_component = declared;
    *out_op = 0;
    return 1;
}

/** Request every authored image once; pending tokens become live in place. */
static void
orbs_load_images(struct ToriRS_ApiV2* api, struct OrbsState* state)
{
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
    {
        if( state->image[i].value != 0 )
            continue;
        (void)api->assets.image(api, ORB_IMAGE_FILE[i], &state->image[i]);
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
    struct ToriRS_ApiV2* api,
    char const* key,
    char const* name,
    int fallback)
{
    int const override = orbs_cfg_int(api, key);
    int declared;

    if( override > 0 )
        return override;
    /* 0 is a legal varp id, so "switched off" needs a value of its own: a
     * config key set to 0 means "this lane has no such var, draw nothing". */
    if( override == 0 )
        return -1;

    if( api->cache.named_id(api, "varp", name, &declared) )
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
orbs_load_digits(struct ToriRS_ApiV2* api, struct OrbsState* state)
{
    char const* at;
    size_t size = 0;

    if( state->digits_ready )
        return 1;

    if( api->assets.request(api, "digits.ini") != TORIRS_ASSET_READY )
        return 0;
    if( !api->assets.bytes(api, "digits.ini", (void const**)&at, &size) )
        return 0;
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
            state->digit_line_height = atoi(line + 12);
            continue;
        }
        if( len > 11 && strncmp(line, "max_ascent=", 11) == 0 )
        {
            state->digit_max_ascent = atoi(line + 11);
            continue;
        }
        if( len > 12 && strncmp(line, "max_descent=", 12) == 0 )
        {
            state->digit_max_descent = atoi(line + 12);
            continue;
        }
        /* `<digit>=x y w h ox oy advance`. Anything else -- the header
         * comments, the `ascent=` line -- is skipped by the same test. */
        if( len > 6 && strncmp(line, "steps=", 6) == 0 )
        {
            state->digit_steps = atoi(line + 6);
            continue;
        }
        if( len > 11 && strncmp(line, "row_height=", 11) == 0 )
        {
            state->digit_row_h = atoi(line + 11);
            continue;
        }
        if( len < 3 || line[0] < '0' || line[0] > '9' || line[1] != '=' )
            continue;
        {
            struct OrbGlyph* g = &state->digit[line[0] - '0'];
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
                state->digits_ready = 1;
        }
    }
    return state->digits_ready;
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
    struct OrbsState const* state,
    int y,
    int h)
{
    int const space = h - state->digit_max_ascent - state->digit_max_descent;

    /*
     * Metrics that never arrived would put the number several pixels down the
     * panel rather than nowhere, which is the kind of wrong that looks like a
     * taste decision. The box's own top is the answer for a face whose ascent
     * fills its line box -- which is every face an orb is set in -- so it is
     * the honest fallback as well as the common case.
     */
    if( state->digit_line_height <= 0 || state->digit_max_ascent <= 0 )
        return y;
    return y + state->digit_max_ascent + space / 2 - state->digit_line_height;
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
    struct OrbsState const* state,
    struct ToriRS_DrawBuilder* draw,
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
    if( state->digit_steps > 1 && total > 0 )
    {
        int const clamped = filled < 0 ? 0 : (filled > total ? total : filled);
        row = clamped * (state->digit_steps - 1) / total;
    }

    snprintf(text, sizeof(text), "%d", value);
    len = (int)strlen(text);

    for( int i = 0; i < len; i++ )
        width += state->digit[text[i] - '0'].advance;
    pen = cx - width / 2;

    for( int i = 0; i < len; i++ )
    {
        struct OrbGlyph const* g = &state->digit[text[i] - '0'];
        int const dx = pen + g->off_x;
        int const dy = top + g->off_y;

        int const src_y = g->y + row * state->digit_row_h;

        draw->image_clip(
            draw,
            state->image[ORB_IMG_DIGITS],
            dx - g->x,
            dy - src_y,
            (struct ToriRS_Rect){ dx, dy, g->w, g->h },
            255);
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
    struct OrbsState const* state,
    struct ToriRS_DrawBuilder* draw,
    int x,
    int y,
    int fill_image,
    int icon_image,
    int value,
    int filled,
    int total,
    int inactive)
{
    char text[16];
    int hidden;
    int trans;

    if( state->image[ORB_IMG_FRAME].value == 0 )
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
     * The plate, lit plate and hit region are retained named-node facets; the
     * host picks the hovered plate from the pointer and box. This callback
     * draws only what changes every tick over that retained plate.
     */
    trans = inactive ? 50 : 25;
    if( inactive )
    {
        fill_image = ORB_IMG_FILL_GREY;
        /* Full, so no dark cap is drawn over it: an inactive orb shows no
         * level at all, rather than a level of zero. */
        filled = total;
    }

    draw->image(
        draw,
        state->image[fill_image],
        x + ORB_DISC_X,
        y + ORB_DISC_Y,
        255 - trans);

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
        draw->image_clip(
            draw,
            state->image[ORB_IMG_FILL_EMPTY],
            x + ORB_DISC_X,
            y + ORB_DISC_Y,
            (struct ToriRS_Rect){ x + ORB_DISC_X, y + ORB_DISC_Y, ORB_DISC, hidden },
            255);

    draw->image(
        draw, state->image[icon_image], x + ORB_DISC_X, y + ORB_DISC_Y, 255);

    if( state->digits_ready && state->image[ORB_IMG_DIGITS].value != 0 )
    {
        orbs_draw_number(
            state,
            draw,
            x + ORB_TEXT_CX,
            y + orbs_text_origin(state, ORB_TEXT_Y, ORB_TEXT_H),
            value,
            filled,
            total);
        return;
    }

    /* The atlas has not landed yet, or would not decode. The client's own
     * overlay face is the wrong one for an orb, but a number in the wrong face
     * beats an orb with no number in it. */
    snprintf(text, sizeof(text), "%d", value);
    draw->text(draw, x + ORB_TEXT_CX, y + ORB_TEXT_BASELINE, text, ORB_TEXT_RGB);
}

/* Each orb is a canonical named node. Static retained facets own its plate,
 * hover art, bounds and action; on_ui_node_draw adds only the live meter, icon
 * and number. That keeps arbitration, paint order and input on one semantic
 * tree without any legacy chrome claim or role lookup. */

static struct
{
    char const* node;
    char const* show_key;
    char const* button_key;
    char const* button_name;
    char const* action;
} const ORB_PART[ORB_COUNT] = {
    { "frame.orb.hitpoints", "show_hp", "hp_button", "orb_hp_button", "Cure" },
    { "frame.orb.prayer", "show_prayer", "prayer_button", "orb_prayer_button", "Quick-prayers" },
    { "frame.orb.run", "show_run", "run_button", "orb_run_on", "Toggle Run" },
    { "frame.orb.special", "show_spec", "spec_button", "orb_spec_button", "Use Special Attack" },
};

#define ORB_CONTRIBUTION(name_)                                                    \
    { .struct_size = sizeof(struct ToriRS_UiContribution),                        \
      .node = (name_),                                                             \
      .mode = TORIRS_UI_REPLACE_OR_PROVIDE,                                        \
      .facets = TORIRS_UI_FACET_ALL,                                               \
      .value = { .struct_size = sizeof(struct ToriRS_UiNode),                      \
                 .bounds = { 0, 0, 1, 1 },                                        \
                 .parent = "frame.minimap",                                        \
                 .anchor = TORIRS_ANCHOR_TOP_LEFT,                                 \
                 .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,                      \
                 .flags = TORIRS_UI_NODE_ENABLED,                                  \
                 .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM } }

static struct ToriRS_UiContribution const ORB_CONTRIBUTIONS[] = {
    ORB_CONTRIBUTION("frame.orb.hitpoints"),
    ORB_CONTRIBUTION("frame.orb.prayer"),
    ORB_CONTRIBUTION("frame.orb.run"),
    ORB_CONTRIBUTION("frame.orb.special"),
    { .node = NULL },
};

static int
orbs_node_index(struct OrbsState const* state, struct ToriRS_UiNodeRef node)
{
    for( int i = 0; i < ORB_COUNT; i++ )
        if( state->node[i].value == node.value )
            return i;
    return -1;
}

static int
orbs_bounds(
    struct ToriRS_ApiV2* api,
    int orb,
    struct ToriRS_Rect* out)
{
    struct ToriRS_UiNodeInfo map;
    struct ToriRS_UiNodeRef const ref = api->ui.ref(api, "frame.minimap");

    memset(&map, 0, sizeof(map));
    map.struct_size = sizeof(map);
    if( ref.value == 0 || !api->ui.info(api, ref, &map) || !map.visible ||
        map.bounds.width <= 0 || map.bounds.height <= 0 )
        return 0;
    out->x = map.bounds.x + orbs_cfg_int(api, "offset_x") - ORB_W + ORB_SLOT[orb].dx;
    out->y = map.bounds.y + map.bounds.height / 4 + orbs_cfg_int(api, "offset_y") -
             ORB_SLOT[0].dy + ORB_SLOT[orb].dy;
    out->width = ORB_W;
    out->height = ORB_H;
    return 1;
}

static int
orbs_has_action(struct ToriRS_ApiV2* api, int orb)
{
    int component;
    int operation;

    if( orb == ORB_RUN )
    {
        int const run_varp =
            orbs_varp(api, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
        if( run_varp >= 0 && api->cache.varp(api, run_varp) != 0 &&
            orbs_button(api, "run_button_off", "orb_run_off", &component, &operation) )
            return 1;
    }
    return orbs_button(
        api,
        ORB_PART[orb].button_key,
        ORB_PART[orb].button_name,
        &component,
        &operation);
}

static void
orbs_update(struct ToriRS_ApiV2* api, struct OrbsState* state)
{
    orbs_load_images(api, state);
    (void)orbs_load_digits(api, state);
    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct ToriRS_UiNode value;
        int have_bounds;

        memset(&value, 0, sizeof(value));
        have_bounds = orbs_bounds(api, i, &value.bounds);
        value.struct_size = sizeof(value);
        value.parent = "frame.minimap";
        value.anchor = TORIRS_ANCHOR_TOP_LEFT;
        value.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
        value.clip = TORIRS_UI_CLIP_NONE;
        value.flags = TORIRS_UI_NODE_ENABLED;
        if( have_bounds && orbs_cfg_bool(api, ORB_PART[i].show_key) )
            value.flags |= TORIRS_UI_NODE_VISIBLE;
        value.hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM;
        value.hit_rect = value.bounds;
        value.state_image_mask =
            (1u << TORIRS_UI_VISUAL_IDLE) | (1u << TORIRS_UI_VISUAL_HOVER);
        value.state_images[TORIRS_UI_VISUAL_IDLE] = state->image[ORB_IMG_FRAME];
        value.state_images[TORIRS_UI_VISUAL_HOVER] = state->image[ORB_IMG_FRAME_OVER];
        if( orbs_has_action(api, i) )
        {
            value.action_count = 1;
            value.actions[0] = ORB_PART[i].action;
        }
        if( state->node[i].value != 0 )
            (void)api->ui.update(api, state->node[i], TORIRS_UI_FACET_ALL, &value);
    }
}

static void
orbs_draw_node(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_DrawBuilder* draw)
{
    struct OrbsState* state = plugin_state;
    struct ToriRS_UiNodeInfo info;
    int const orb = orbs_node_index(state, node);
    int x;
    int y;

    if( orb < 0 )
        return;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if( !api->ui.info(api, node, &info) || !info.visible )
        return;
    x = info.bounds.x;
    y = info.bounds.y;

    if( orb == ORB_HP || orb == ORB_PRAYER )
    {
        struct ToriRS_SkillSnapshot skill;
        int const index = orb == ORB_HP ? ORB_STAT_HITPOINTS : ORB_STAT_PRAYER;

        memset(&skill, 0, sizeof(skill));
        skill.struct_size = sizeof(skill);
        if( api->game && api->game->skill(api, index, &skill) && skill.base_level > 0 )
            orbs_draw_one(
                state,
                draw,
                x,
                y,
                orb == ORB_HP ? ORB_IMG_FILL_RED : ORB_IMG_FILL_PRAYER,
                orb == ORB_HP ? ORB_IMG_ICON_HP : ORB_IMG_ICON_PRAYER,
                skill.current_level,
                skill.current_level,
                skill.base_level,
                0);
        return;
    }

    if( orb == ORB_RUN )
    {
        int const energy = api->game ? api->game->run_energy(api) : 0;
        int const run_varp =
            orbs_varp(api, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
        int const running = run_varp >= 0 && api->cache.varp(api, run_varp) != 0;

        orbs_draw_one(
            state,
            draw,
            x,
            y,
            ORB_IMG_FILL_GOLD,
            running ? ORB_IMG_ICON_RUN : ORB_IMG_ICON_WALK,
            energy,
            energy,
            100,
            !running);
        return;
    }

    if( orb == ORB_SPEC )
    {
        int const spec_varp =
            orbs_varp(api, "spec_varp", "special_attack_energy", ORB_VARP_SPEC_FALLBACK);
        int const spec_max = orbs_cfg_int(api, "spec_max");
        if( spec_varp >= 0 && spec_max > 0 )
        {
            int energy = api->cache.varp(api, spec_varp);
            int const armed = api->cache.varp(api, spec_varp + 1) > 0;
            int const inactive = !orbs_has_action(api, ORB_SPEC);

            if( energy < 0 )
                energy = 0;
            if( energy > spec_max )
                energy = spec_max;
            orbs_draw_one(
                state,
                draw,
                x,
                y,
                armed ? ORB_IMG_FILL_CYAN_LIT : ORB_IMG_FILL_CYAN,
                ORB_IMG_ICON_SPEC,
                energy * 100 / spec_max,
                energy,
                spec_max,
                inactive);
        }
    }
}

static enum ToriRS_CallbackResult
orbs_action(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct OrbsState* state = plugin_state;
    int const orb = orbs_node_index(state, node);
    char const* key;
    char const* name;
    int component;
    int operation;

    if( orb < 0 || strcmp(action, ORB_PART[orb].action) != 0 )
        return TORIRS_CALLBACK_CONTINUE;
    key = ORB_PART[orb].button_key;
    name = ORB_PART[orb].button_name;
    if( orb == ORB_RUN )
    {
        int const run_varp =
            orbs_varp(api, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
        if( run_varp >= 0 && api->cache.varp(api, run_varp) != 0 &&
            orbs_button(
                api, "run_button_off", "orb_run_off", &component, &operation) )
            goto invoke;
    }
    if( !orbs_button(api, key, name, &component, &operation) )
        return TORIRS_CALLBACK_CONSUME;

invoke:
    if( !api->cache.invoke(api, component, operation) )
    {
        char message[160];
        snprintf(
            message,
            sizeof(message),
            "Minimap orbs: this world has no interface component %d for '%s'.",
            component,
            key);
        api->core.notify(api, message);
        api->core.log(api, "%s", message);
    }
    return TORIRS_CALLBACK_CONSUME;
}

static void
orbs_start(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;

    memset(state, 0, sizeof(*state));
    state->digit_steps = 1;
    for( int i = 0; i < ORB_COUNT; i++ )
        state->node[i] = api->ui.ref(api, ORB_PART[i].node);
    orbs_update(api, state);
}

static void
orbs_stop(struct ToriRS_ApiV2* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;

    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        if( state->image[i].value != 0 )
            api->assets.image_release(api, state->image[i]);
    api->assets.release(api, "digits.ini");
    memset(state, 0, sizeof(*state));
}

static void
orbs_changed(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    char const* key)
{
    (void)key;
    orbs_update(api, plugin_state);
}

static void
orbs_asset(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_PluginEvAsset const* event)
{
    (void)event;
    orbs_update(api, plugin_state);
}

static void
orbs_placement(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    uint32_t revision)
{
    (void)revision;
    orbs_update(api, plugin_state);
}

/* Per-world overrides remain ordinary V2 config schema entries. */
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

static struct ToriRS_ConfigSchema const ORBS_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = ORBS_CONFIG,
};

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_MINIMAP_ORBS = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
    .id = "minimap-orbs",
    .title = "Minimap Orbs",
    .version = "1.0.0",
    .state_size = sizeof(struct OrbsState),
    .config = &ORBS_SCHEMA,
    .ui_contributions = ORB_CONTRIBUTIONS,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = orbs_start,
        .on_stop = orbs_stop,
        .on_config_changed = orbs_changed,
        .on_asset = orbs_asset,
        .on_placement_changed = orbs_placement,
        .on_ui_node_draw = orbs_draw_node,
        .on_ui_node_action = orbs_action,
    },
};
