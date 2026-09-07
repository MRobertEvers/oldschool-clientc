#include "plugin/torirs_plugin_api.h"

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
 * ## How an orb is presented now
 *
 * Each orb is an OWNED IMAGE CONTROL: one plugin-owned graphic child carrying
 * a 57x34 picture the plugin composes itself from its shipped art (plate,
 * fill disc clipped to the meter, icon, atlas digits), republished under a
 * stable name whenever a value changes. On a lane whose cache draws its own
 * orbs (interface 160) the control is a child of the native orb root, so it
 * paints over the native art and follows the native layout, hiding, and
 * remount exactly; the native button underneath keeps its identity and its
 * operations. On a lane without native orbs the controls are children of the
 * minimap's parent, placed by interface 160's own offsets and clamped out of
 * the map disc.
 *
 * ## Clicking one
 *
 * The control is armed with one operation (widgets.set_on_op) whose label is
 * the reference verb. Pressing it invokes a CHECKED native widget action: the
 * lane's `[role:action_frame_orb_*]` node is looked up live, its current
 * actions are queried and the first is invoked through the ordinary native
 * dispatcher, which re-checks visibility, masks and identity. A raw
 * `<interface>:<component>[:<op>]` override, and the older `[iface:<name>]`
 * compatibility lookup, remain as documented escape hatches.
 *
 * Pressing the real button rather than writing the var is what makes this
 * work at all: the run toggle and the special attack are the SERVER's, and a
 * client that flipped the varp locally would show a state the server never
 * agreed to and lose it on the next sync.
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

/** One orb's presentation: where it hangs, its control and its picture. */
struct OrbControl
{
    /* The native orb root this control covers, or zero when the lane has no
     * native orb and the control is placed beside the minimap instead. */
    struct ToriRS_WidgetRef native_root;
    struct ToriRS_WidgetRef control;
    /* The parent the control was created under: a change of anchor removes
     * the old control rather than leaving it behind under the old parent. */
    struct ToriRS_WidgetRef parent;
    struct ToriRS_ImageRef composed;
    /* What the last composition drew, so a quiet frame composes nothing. */
    uint32_t composed_key;
    bool armed;
    bool action_available;
    bool native;
};

/** All mutable data belongs to one registered instance. */
struct OrbsState
{
    struct ToriRS_ImageRef image[ORB_IMG_COUNT];
    /* Decoded source art, read once each through assets.image_pixels. */
    uint32_t* pixels[ORB_IMG_COUNT];
    int pixel_w[ORB_IMG_COUNT];
    int pixel_h[ORB_IMG_COUNT];
    struct OrbGlyph digit[10];
    int digits_ready;
    int digit_steps;
    int digit_row_h;
    int digit_line_height;
    int digit_max_ascent;
    int digit_max_descent;
    struct ToriRS_WidgetRef minimap;
    struct OrbControl orb[ORB_COUNT];
    /* The minimap's parent-local box as last laid out against. */
    struct ToriRS_WidgetBounds map_local;
    bool map_valid;
    uint32_t composed_buffer[ORB_W * ORB_H];
};

static bool
orbs_cfg_bool(struct ToriRS_Api* api, char const* key)
{
    bool value = false;
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
orbs_cfg_int(struct ToriRS_Api* api, char const* key)
{
    int value = 0;
    (void)api->config.get_int(api, key, &value);
    return value;
}

static char const*
orbs_cfg_string(struct ToriRS_Api* api, char const* key)
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
 * Compatibility lookup for profiles that predate semantic action roles.
 *
 * The profile's old `[iface:<name>]` entry resolves to a component id. New
 * profiles bind a `[role:<name>]`, which orbs_native_action resolves to a
 * checked action ref invoked through the widget API (widgets.invoke); this
 * path remains so an older/private profile keeps working during migration.
 *
 * @return 1 when a button was named, and then `out_*` describe it.
 */
static int
orbs_compat_button(
    struct ToriRS_Api* api,
    char const* name,
    int* out_component,
    int* out_op)
{
    int declared;

    if( !api->cache.named_id(api, "iface", name, &declared) )
        return 0;
    *out_component = declared;
    *out_op = 0;
    return 1;
}

/** Request every authored image once; pending tokens become live in place. */
static void
orbs_load_images(struct ToriRS_Api* api, struct OrbsState* state)
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
    struct ToriRS_Api* api,
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
orbs_load_digits(struct ToriRS_Api* api, struct OrbsState* state)
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

/* Each orb's verb, the role of the native button it presses, and the config
 * keys of its raw overrides. */
static struct
{
    char const* key;
    char const* show_key;
    char const* button_key;
    char const* button_name;
    char const* action;
    char const* native_role;
    char const* action_role;
} const ORB_PART[ORB_COUNT] = {
    { "orb_hitpoints", "show_hp", "hp_button", "orb_hp_button", "Cure", "orb_hitpoints", NULL },
    { "orb_prayer", "show_prayer", "prayer_button", "orb_prayer_button", "Quick-prayers", "orb_prayer", "action_frame_orb_prayer_activate" },
    { "orb_run", "show_run", "run_button", "orb_run_on", "Toggle Run", "orb_run", "action_frame_orb_run_enable" },
    { "orb_special", "show_spec", "spec_button", "orb_spec_button", "Use Special Attack", "orb_spec", "action_frame_orb_special_activate" },
};

/* ------------------------------------------------------------ compositing */

/** The decoded pixels of one shipped image, read once; NULL until it lands. */
static uint32_t const*
orbs_pixels(struct ToriRS_Api* api, struct OrbsState* state, int which, int* out_w, int* out_h)
{
    if( !state->pixels[which] )
    {
        int w = 0, h = 0;
        size_t count = 0;
        if( state->image[which].value == 0 ||
            !api->assets.image_size(api, state->image[which], &w, &h) || w <= 0 || h <= 0 ||
            w > 1024 || h > 1024 )
            return NULL;
        uint32_t* argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
        assert(argb);
        if( !api->assets.image_pixels(api, state->image[which], argb, (size_t)w * (size_t)h, &count) ||
            count != (size_t)w * (size_t)h )
        {
            free(argb);
            return NULL;
        }
        state->pixels[which] = argb;
        state->pixel_w[which] = w;
        state->pixel_h[which] = h;
    }
    *out_w = state->pixel_w[which];
    *out_h = state->pixel_h[which];
    return state->pixels[which];
}

/** Source-over one ARGB pixel scaled by `alpha` (255 = as authored). */
static uint32_t
orbs_blend(uint32_t dst, uint32_t src, int alpha)
{
    unsigned const sa = ((src >> 24) & 0xff) * (unsigned)alpha / 255u;
    if( sa == 0 )
        return dst;
    unsigned const da = (dst >> 24) & 0xff;
    unsigned const out_a = sa + da * (255u - sa) / 255u;
    uint32_t out = out_a << 24;
    for( int shift = 0; shift < 24; shift += 8 )
    {
        unsigned const sc = (src >> shift) & 0xff;
        unsigned const dc = (dst >> shift) & 0xff;
        unsigned const c = out_a ? (sc * sa + dc * da * (255u - sa) / 255u) / out_a : 0;
        out |= (c > 255u ? 255u : c) << shift;
    }
    return out;
}

/**
 * Blit the `clip` rectangle of `which` (clip given in ORB space) with the
 * image's origin at (ox, oy) in orb space. Pixels outside the orb are dropped.
 */
static void
orbs_blit(
    struct ToriRS_Api* api,
    struct OrbsState* state,
    int which,
    int ox,
    int oy,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    int alpha)
{
    int w, h;
    uint32_t const* src = orbs_pixels(api, state, which, &w, &h);
    if( !src )
        return;
    for( int y = clip_y; y < clip_y + clip_h; y++ )
    {
        int const sy = y - oy;
        if( y < 0 || y >= ORB_H || sy < 0 || sy >= h )
            continue;
        for( int x = clip_x; x < clip_x + clip_w; x++ )
        {
            int const sx = x - ox;
            if( x < 0 || x >= ORB_W || sx < 0 || sx >= w )
                continue;
            state->composed_buffer[y * ORB_W + x] =
                orbs_blend(state->composed_buffer[y * ORB_W + x], src[sy * w + sx], alpha);
        }
    }
}

/**
 * `value`, centred on `cx`, with `top` as the line box's top, from the atlas.
 * The colour is the METER's: the atlas carries clientscript 449's ramp as rows.
 */
static void
orbs_compose_number(
    struct ToriRS_Api* api,
    struct OrbsState* state,
    int cx,
    int top,
    int value,
    int filled,
    int total)
{
    char text[16];
    int len, width = 0, pen, row = 0;

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
        orbs_blit(api, state, ORB_IMG_DIGITS, dx - g->x, dy - src_y, dx, dy, g->w, g->h, 255);
        pen += g->advance;
    }
}

/**
 * One orb's picture: plate, meter disc, icon and number. Returns the key of
 * what was drawn so an unchanged orb costs nothing next frame.
 *
 * The three states the reference draws an orb in (clientscript 2792):
 * INACTIVE a grey disc at trans 50 and no operation; idle the orb's own colour
 * at trans 25. The lit (hovered) plate is not reproduced.
 */
/** What a composition would draw, hashed: equal keys mean an identical picture. */
static uint32_t
orbs_key(struct OrbsState const* state, int fill_image, int icon_image, int value, int filled, int total, int inactive)
{
    return 0x9e3779b9u * (uint32_t)(fill_image + 1) ^ (uint32_t)(icon_image + 1) * 0x85ebca6bu ^
           (uint32_t)value * 0xc2b2ae35u ^ (uint32_t)filled * 0x27d4eb2fu ^ (uint32_t)total * 0x165667b1u ^
           (uint32_t)(inactive ? 0x1000000u : 0u) ^ (uint32_t)(state->digits_ready ? 0x2000000u : 0u) ^
           (uint32_t)(state->pixels[ORB_IMG_FRAME] ? 0x4000000u : 0u);
}

static uint32_t
orbs_compose(
    struct ToriRS_Api* api,
    struct OrbsState* state,
    int orb,
    int fill_image,
    int icon_image,
    int value,
    int filled,
    int total,
    int inactive)
{
    int const trans = inactive ? 50 : 25;
    int hidden;
    uint32_t key;
    /* The inactive substitution is part of the inputs the key describes, so
     * the hash is taken after it; otherwise a walking run orb would rehash
     * differently every frame and recompose for nothing. */
    if( inactive )
    {
        fill_image = ORB_IMG_FILL_GREY;
        filled = total;
    }
    key = orbs_key(state, fill_image, icon_image, value, filled, total, inactive);
    /* Unchanged inputs draw nothing: a quiet frame costs one hash per orb. */
    if( key == state->orb[orb].composed_key )
        return key;
    api->core.log(api, "MINIMAP_ORBS_VALUE orb=%s value=%d filled=%d total=%d inactive=%d",
        ORB_PART[orb].key, value, filled, total, inactive);
    memset(state->composed_buffer, 0, sizeof(state->composed_buffer));
    orbs_blit(api, state, ORB_IMG_FRAME, 0, 0, 0, 0, ORB_W, ORB_H, 255);
    orbs_blit(api, state, fill_image, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y, ORB_DISC, ORB_DISC, 255 - trans);
    /* The dark disc over the unfilled rows, rounded so 98 of 99 still shows
     * a sliver of dark: the orb must never read as full when it is not. */
    hidden = total > 0 ? ORB_DISC - (filled * ORB_DISC + total - 1) / total : ORB_DISC;
    if( hidden < 0 ) hidden = 0;
    if( hidden > ORB_DISC ) hidden = ORB_DISC;
    if( hidden > 0 )
        orbs_blit(api, state, ORB_IMG_FILL_EMPTY, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y, ORB_DISC, hidden, 255);
    orbs_blit(api, state, icon_image, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y, ORB_DISC, ORB_DISC, 255);
    if( state->digits_ready && state->image[ORB_IMG_DIGITS].value != 0 )
        orbs_compose_number(api, state, ORB_TEXT_CX, orbs_text_origin(state, ORB_TEXT_Y, ORB_TEXT_H), value, filled, total);
    /* The frame art may have landed during this composition; hash again so a
     * picture composed without its plate is redrawn once it arrives. */
    return orbs_key(state, fill_image, icon_image, value, filled, total, inactive);
}

/* ------------------------------------------------------------ actions */

static int
orbs_running(struct ToriRS_Api* api)
{
    int const run_varp = orbs_varp(api, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
    return run_varp >= 0 && api->cache.varp(api, run_varp) != 0;
}

static char const*
orbs_action_role(struct ToriRS_Api* api, int orb)
{
    if( orb == ORB_RUN )
        return orbs_running(api) ? "action_frame_orb_run_disable" : "action_frame_orb_run_enable";
    return ORB_PART[orb].action_role;
}

/** The current native action behind this orb's button role, if any. */
static bool
orbs_native_action(
    struct ToriRS_Api* api,
    int orb,
    struct ToriRS_WidgetActionRef* out)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetRef button;
    struct ToriRS_WidgetAction action;
    size_t count = 0;
    char const* role = orbs_action_role(api, orb);
    if( !role || ui->find(ui->context, role, &button) != TORIRS_CONTRACT_OK )
        return false;
    if( ui->actions(ui->context, button, &action, 1, &count) != TORIRS_CONTRACT_OK &&
        count == 0 )
        return false;
    if( count == 0 )
        return false;
    if( out )
        *out = action.ref;
    return true;
}

static bool
orbs_has_action(struct ToriRS_Api* api, int orb)
{
    int component, operation;
    char const* key = ORB_PART[orb].button_key;
    char const* name = ORB_PART[orb].button_name;
    if( orb == ORB_RUN && orbs_running(api) )
    {
        key = "run_button_off";
        name = "orb_run_off";
    }
    if( orbs_parse_button(orbs_cfg_string(api, key), &component, &operation) )
        return true;
    if( orbs_native_action(api, orb, NULL) )
        return true;
    return orbs_compat_button(api, name, &component, &operation) != 0;
}

/** Press the orb: raw override, checked native action, then compatibility. */
static void
orbs_press(struct ToriRS_Api* api, int orb)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetActionRef action;
    int component, operation;
    char const* key = ORB_PART[orb].button_key;
    char const* name = ORB_PART[orb].button_name;
    if( orb == ORB_RUN && orbs_running(api) )
    {
        key = "run_button_off";
        name = "orb_run_off";
    }
    if( orbs_parse_button(orbs_cfg_string(api, key), &component, &operation) )
        goto invoke;
    if( orbs_native_action(api, orb, &action) )
    {
        enum ToriRS_ContractResult result = ui->invoke(ui->context, action);
        api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=native result=%d", ORB_PART[orb].key, result);
        if( result == TORIRS_CONTRACT_OK )
            return;
    }
    if( !orbs_compat_button(api, name, &component, &operation) )
    {
        api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=none", ORB_PART[orb].key);
        return;
    }
invoke:
    if( !api->cache.invoke(api, component, operation) )
    {
        char message[160];
        snprintf(message, sizeof(message),
            "Minimap orbs: this world has no interface component %d for '%s'.", component, key);
        api->core.notify(api, message);
        api->core.log(api, "%s", message);
        return;
    }
    api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=component component=%d op=%d", ORB_PART[orb].key, component, operation);
}

static void
orbs_operation(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct OrbsState* state = user;
    for( int i = 0; i < ORB_COUNT; i++ )
        if( ToriRS_WidgetRefEqual(state->orb[i].control, event->widget) )
        {
            orbs_press(api, i);
            return;
        }
}

/* ------------------------------------------------------------ layout */

/** The first column of the map disc's ink over rows [top, bottom), in the
 * map's parent-local space; the box's right edge when the span misses it. */
static int
orbs_map_ink_left(struct ToriRS_WidgetBounds const* map, int top, int bottom)
{
    long const cx = 2 * (long)map->x + map->width;
    long const cy = 2 * (long)map->y + map->height;
    long const r = map->width < map->height ? map->width : map->height;
    int left = map->x + map->width;
    for( int y = top; y < bottom; y++ )
    {
        long const dy = 2 * (long)y + 1 - cy;
        if( dy * dy >= r * r )
            continue;
        for( int x = map->x; x < left; x++ )
        {
            long const dx = 2 * (long)x + 1 - cx;
            if( dx * dx + dy * dy < r * r )
            {
                left = x;
                break;
            }
        }
    }
    return left;
}

/** Interface 160's slot for `orb` beside `map`, then clamped out of the disc. */
static void
orbs_beside_map(struct ToriRS_Api* api, struct ToriRS_WidgetBounds const* map, int orb, int* out_x, int* out_y)
{
    int x = map->x + orbs_cfg_int(api, "offset_x") - ORB_W + ORB_SLOT[orb].dx;
    int const y = map->y + map->height / 4 + orbs_cfg_int(api, "offset_y") - ORB_SLOT[0].dy + ORB_SLOT[orb].dy;
    int const limit = orbs_map_ink_left(map, y, y + ORB_H);
    if( x + ORB_W > limit )
        x = limit - ORB_W;
    *out_x = x;
    *out_y = y;
}

static void
orbs_remove_control(struct ToriRS_Api* api, struct OrbControl* orb)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    if( orb->control.opaque[2] )
        (void)ui->remove(ui->context, orb->control);
    orb->control = (struct ToriRS_WidgetRef){ 0 };
    orb->parent = (struct ToriRS_WidgetRef){ 0 };
    orb->composed_key = 0;
    orb->armed = false;
}

/** The control's current canvas box, for the headless harness. */
static void
orbs_log_control(struct ToriRS_Api* api, struct OrbsState* state, int i)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetBounds box = { 0 };
    struct OrbControl const* orb = &state->orb[i];
    if( !orb->control.opaque[2] )
        return;
    (void)ui->bounds(ui->context, orb->control, &box);
    api->core.log(api, "MINIMAP_ORBS_CONTROL orb=%s native=%d armed=%d box=%d,%d,%d,%d",
        ORB_PART[i].key, orb->native, orb->armed, box.x, box.y, box.width, box.height);
}

/**
 * Place (or remove) every orb control for the current bindings and settings.
 * Idempotent: create_image returns the existing child, and the setters are
 * plain writes of current values.
 */
static void
orbs_layout(struct ToriRS_Api* api, struct OrbsState* state)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetRef beside_parent = { 0 };
    bool const replace_native = orbs_cfg_bool(api, "replace_native");
    state->map_valid = false;
    if( state->minimap.opaque[2] &&
        ui->position(ui->context, state->minimap, &state->map_local) == TORIRS_CONTRACT_OK &&
        state->map_local.width > 0 && state->map_local.height > 0 &&
        ui->parent(ui->context, state->minimap, &beside_parent) == TORIRS_CONTRACT_OK )
        state->map_valid = true;

    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct OrbControl* orb = &state->orb[i];
        struct ToriRS_WidgetRef parent = { 0 };
        int x = 0, y = 0;
        bool native = replace_native && orb->native_root.opaque[2];
        if( !orbs_cfg_bool(api, ORB_PART[i].show_key) )
        {
            orbs_remove_control(api, orb);
            continue;
        }
        if( native )
            parent = orb->native_root;
        else if( state->map_valid && !orb->native_root.opaque[2] )
        {
            parent = beside_parent;
            orbs_beside_map(api, &state->map_local, i, &x, &y);
        }
        else
        {
            /* A native orb the user chose to keep, or no minimap yet. */
            orbs_remove_control(api, orb);
            continue;
        }
        if( orb->control.opaque[2] && !ToriRS_WidgetRefEqual(orb->parent, parent) )
            orbs_remove_control(api, orb);
        if( ui->create_image(ui->context, parent, ORB_PART[i].key, &orb->control) != TORIRS_CONTRACT_OK )
        {
            orb->control = (struct ToriRS_WidgetRef){ 0 };
            orb->parent = (struct ToriRS_WidgetRef){ 0 };
            continue;
        }
        orb->parent = parent;
        (void)ui->set_position(ui->context, orb->control, x, y);
        if( orb->composed.value )
            (void)ui->set_image(ui->context, orb->control, orb->composed, ORB_W, ORB_H);
        orb->action_available = orbs_has_action(api, i);
        if( orb->action_available )
        {
            if( ui->set_on_op(ui->context, orb->control, ORB_PART[i].action, orbs_operation, state) == TORIRS_CONTRACT_OK )
                orb->armed = true;
        }
        else if( orb->armed )
        {
            (void)ui->set_on_op(ui->context, orb->control, NULL, NULL, NULL);
            orb->armed = false;
        }
        (void)ui->revalidate(ui->context, orb->control);
        orb->composed_key = 0; /* a moved or re-created control redraws */
        orb->native = native;
        orbs_log_control(api, state, i);
    }
}

/** Compose and publish one orb's picture when its inputs changed. */
static void
orbs_refresh(struct ToriRS_Api* api, struct OrbsState* state, int i)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct OrbControl* orb = &state->orb[i];
    uint32_t key = 0;
    char name[32];
    if( !orb->control.opaque[2] || state->image[ORB_IMG_FRAME].value == 0 )
        return;
    if( i == ORB_HP || i == ORB_PRAYER )
    {
        struct ToriRS_SkillSnapshot skill;
        int const index = i == ORB_HP ? ORB_STAT_HITPOINTS : ORB_STAT_PRAYER;
        memset(&skill, 0, sizeof(skill));
        skill.struct_size = sizeof(skill);
        if( !api->game || !api->game->skill(api, index, &skill) || skill.base_level <= 0 )
            return;
        key = orbs_compose(api, state, i, i == ORB_HP ? ORB_IMG_FILL_RED : ORB_IMG_FILL_PRAYER,
            i == ORB_HP ? ORB_IMG_ICON_HP : ORB_IMG_ICON_PRAYER, skill.current_level, skill.current_level, skill.base_level, 0);
    }
    else if( i == ORB_RUN )
    {
        int const energy = api->game ? api->game->run_energy(api) : 0;
        int const running = orbs_running(api);
        key = orbs_compose(api, state, i, ORB_IMG_FILL_GOLD, running ? ORB_IMG_ICON_RUN : ORB_IMG_ICON_WALK, energy, energy, 100, !running);
    }
    else
    {
        int const spec_varp = orbs_varp(api, "spec_varp", "special_attack_energy", ORB_VARP_SPEC_FALLBACK);
        int const spec_max = orbs_cfg_int(api, "spec_max");
        int energy, armed;
        if( spec_varp < 0 || spec_max <= 0 )
            return;
        energy = api->cache.varp(api, spec_varp);
        armed = api->cache.varp(api, spec_varp + 1) > 0;
        if( energy < 0 ) energy = 0;
        if( energy > spec_max ) energy = spec_max;
        key = orbs_compose(api, state, i, armed ? ORB_IMG_FILL_CYAN_LIT : ORB_IMG_FILL_CYAN, ORB_IMG_ICON_SPEC,
            energy * 100 / spec_max, energy, spec_max, !orb->action_available);
    }
    if( key == orb->composed_key )
        return;
    snprintf(name, sizeof(name), "%s.composed", ORB_PART[i].key);
    if( api->assets.image_compose(api, name, ORB_W, ORB_H, state->composed_buffer, &orb->composed) != TORIRS_ASSET_READY )
        return;
    if( ui->set_image(ui->context, orb->control, orb->composed, ORB_W, ORB_H) == TORIRS_CONTRACT_OK )
    {
        orb->composed_key = key;
        orbs_log_control(api, state, i);
    }
}

/* ------------------------------------------------------------ lifecycle */

static void
orbs_binding(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct OrbsState* state = user;
    struct ToriRS_WidgetRef const bound = event->type == TORIRS_WIDGET_BOUND ? event->widget : (struct ToriRS_WidgetRef){ 0 };
    if( strcmp(event->role, "minimap") == 0 )
    {
        state->minimap = bound;
        if( event->type == TORIRS_WIDGET_UNBOUND )
            for( int i = 0; i < ORB_COUNT; i++ )
                if( !state->orb[i].native_root.opaque[2] )
                    state->orb[i].control = state->orb[i].parent = (struct ToriRS_WidgetRef){ 0 };
    }
    else
        for( int i = 0; i < ORB_COUNT; i++ )
            if( strcmp(event->role, ORB_PART[i].native_role) == 0 )
            {
                state->orb[i].native_root = bound;
                /* The native root's remount took the owned child with it. */
                if( event->type == TORIRS_WIDGET_UNBOUND )
                    state->orb[i].control = state->orb[i].parent = (struct ToriRS_WidgetRef){ 0 };
            }
    orbs_layout(api, state);
    for( int i = 0; i < ORB_COUNT; i++ )
        orbs_refresh(api, state, i);
}

/* Test seam: the unit test drives native bindings directly. */
void
minimap_orbs_test_binding(struct ToriRS_Api* api, void* state, struct ToriRS_WidgetEvent const* event)
{
    orbs_binding(api, state, event);
}

static void
orbs_start(struct ToriRS_Api* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;
    struct ToriRS_WidgetApi* ui = &api->widgets;
    memset(state, 0, sizeof(*state));
    state->digit_steps = 1;
    orbs_load_images(api, state);
    (void)orbs_load_digits(api, state);
    (void)ui->watch(ui->context, "minimap", orbs_binding, state);
    for( int i = 0; i < ORB_COUNT; i++ )
        (void)ui->watch(ui->context, ORB_PART[i].native_role, orbs_binding, state);
}

static void
orbs_stop(struct ToriRS_Api* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;
    /* Owner teardown removes the controls; the pictures and the source art
     * are the plugin's to release. */
    for( int i = 0; i < ORB_COUNT; i++ )
        if( state->orb[i].composed.value != 0 )
            api->assets.image_release(api, state->orb[i].composed);
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
    {
        if( state->image[i].value != 0 )
            api->assets.image_release(api, state->image[i]);
        free(state->pixels[i]);
    }
    api->assets.release(api, "digits.ini");
    memset(state, 0, sizeof(*state));
}

static void
orbs_changed(struct ToriRS_Api* api, void* plugin_state, char const* key)
{
    struct OrbsState* state = plugin_state;
    (void)key;
    orbs_layout(api, state);
    for( int i = 0; i < ORB_COUNT; i++ )
        orbs_refresh(api, state, i);
}

static void
orbs_asset(struct ToriRS_Api* api, void* plugin_state, struct ToriRS_AssetEvent const* event)
{
    struct OrbsState* state = plugin_state;
    (void)event;
    orbs_load_images(api, state);
    (void)orbs_load_digits(api, state);
    for( int i = 0; i < ORB_COUNT; i++ )
    {
        state->orb[i].composed_key = 0;
        orbs_refresh(api, state, i);
    }
}

/*
 * Each frame: re-read the live numbers and republish only the pictures whose
 * inputs changed; re-place the column only when the minimap's box moved or an
 * action became (un)available. A settled frame composes nothing and writes no
 * geometry.
 */
static void
orbs_frame(struct ToriRS_Api* api, void* plugin_state, struct ToriRS_FrameEvent const* event)
{
    struct OrbsState* state = plugin_state;
    struct ToriRS_WidgetApi* ui = &api->widgets;
    bool relayout = false;
    (void)event;
    if( state->minimap.opaque[2] )
    {
        struct ToriRS_WidgetBounds map;
        bool const valid = ui->position(ui->context, state->minimap, &map) == TORIRS_CONTRACT_OK && map.width > 0;
        if( valid != state->map_valid || (valid && (map.x != state->map_local.x || map.y != state->map_local.y ||
            map.width != state->map_local.width || map.height != state->map_local.height)) )
            relayout = true;
    }
    for( int i = 0; i < ORB_COUNT && !relayout; i++ )
        if( state->orb[i].control.opaque[2] && orbs_has_action(api, i) != state->orb[i].action_available )
            relayout = true;
    if( relayout )
        orbs_layout(api, state);
    for( int i = 0; i < ORB_COUNT; i++ )
        orbs_refresh(api, state, i);
}

/* Per-world overrides remain ordinary V2 config schema entries. */
static struct ToriRS_ConfigItem const ORBS_CONFIG[] = {
    { "show_hp",        TORIRS_CONFIG_BOOL,   "Hitpoints orb",                  "1",    0,    0,      NULL, 0 },
    { "show_prayer",    TORIRS_CONFIG_BOOL,   "Prayer orb",                     "1",    0,    0,      NULL, 0 },
    { "show_run",       TORIRS_CONFIG_BOOL,   "Run energy orb",                 "1",    0,    0,      NULL, 0 },
    { "show_spec",      TORIRS_CONFIG_BOOL,   "Special attack orb",             "1",    0,    0,      NULL, 0 },
    /* Cover a cache's own orbs with the plugin's art (the historical default);
     * off keeps the native orbs and adds none. */
    { "replace_native", TORIRS_CONFIG_BOOL,   "Replace native orbs",            "1",    0,    0,      NULL, 0 },
    { "offset_x",       TORIRS_CONFIG_INT,    "Offset from minimap left",       "6",    -512, 512,    NULL, 0 },
    { "offset_y",       TORIRS_CONFIG_INT,    "Offset from the anchor",         "-3",   -512, 512,    NULL, 0 },
    { "run_varp",       TORIRS_CONFIG_INT,    "Run mode varp (-1 auto)",        "-1",   -1,   65535,  NULL, 0 },
    { "spec_varp",
     TORIRS_CONFIG_INT,                       "Special attack varp (-1 auto)",
     "-1",                                                                                  -1,
     65535,                                                                                               NULL,
     0                                                                                                            },
    { "spec_max",       TORIRS_CONFIG_INT,    "Special attack bar maximum",     "1000", 1,    100000, NULL, 0 },
    /*
     * Optional raw button overrides, `<interface>:<component>[:<op>]`.
     *
     * Empty by default because normal lanes use semantic base actions. These
     * remain an explicit private-lane escape hatch; a wrong id is not an orb
     * that does nothing, it is an IF_BUTTON about a component never touched.
     */
    { "hp_button",      TORIRS_CONFIG_STRING, "Hitpoints orb button",           "",     0,    0,      NULL, 0 },
    { "prayer_button",  TORIRS_CONFIG_STRING, "Prayer orb button",              "",     0,    0,      NULL, 0 },
    { "run_button",     TORIRS_CONFIG_STRING, "Run orb button (turns run on)",  "",     0,    0,      NULL, 0 },
    { "run_button_off",
     TORIRS_CONFIG_STRING,                    "Run orb button (turns run off)",
     "",                                                                                    0,
     0,                                                                                                   NULL,
     0                                                                                                            },
    { "spec_button",    TORIRS_CONFIG_STRING, "Special attack orb button",      "",     0,    0,      NULL, 0 },
    { NULL,             TORIRS_CONFIG_BOOL,   NULL,                             NULL,   0,    0,      NULL, 0 },
};

_Static_assert(
    ORB_SPEC_MAX == 1000,
    "the spec_max default above states this number too");

static struct ToriRS_ConfigSchema const ORBS_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = ORBS_CONFIG,
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "minimap-orbs",
    .title = "Minimap Orbs",
    .version = "2.0.0",
    .state_size = sizeof(struct OrbsState),
    .config = &ORBS_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = orbs_start,
        .on_stop = orbs_stop,
        .on_frame_start = orbs_frame,
        .on_config_changed = orbs_changed,
        .on_asset = orbs_asset,
    },
};
