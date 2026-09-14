#include "plugin/porcelain/torirs_porcelain.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimap orbs: hitpoints, prayer, run energy and special attack, beside the
 * minimap -- described to Porcelain rather than placed by hand.
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
 * The fifteen PNGs in `script/plugins/assets/minimap-orbs/` are shipped beside
 * this source. They were cut from the rev-239 sprites table once, at authoring
 * time, and they are the plugin's own files now -- so this draws the same orbs
 * on a 2004 cache, on a cache that failed to open, and on a client started
 * with no cache at all.
 *
 * That is deliberate and it is the whole reason the asset verbs exist. The
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
 * That whole picture is a PorcelainPaintFn now. Porcelain paints it at most
 * once per (key, hash of inputs) and owns the publication, which is the same
 * rule orbs_key used to enforce by hand -- with the difference that the source
 * art's arrival is one of the hashed inputs, so a plate that landed after the
 * first composition is redrawn by the rule rather than by on_asset zeroing
 * every key.
 *
 * ## What the LANE says, and what this file no longer decides
 *
 * Four facts used to be arithmetic here and are lane data now:
 *
 *   - `PORCELAIN_FACET_ACTIVE` on ORB(run) is the run toggle, and on ORB(spec)
 *     the special-attack armed bit. The `spec_varp + 1` guess is gone: a lane
 *     that moved "armed" off the next slot says so in `[varp:...]`.
 *   - `PORCELAIN_FACET_HIDDEN_BY_CUTSCENE` is the cutscene. A lane with no
 *     such varbit answers OFF, and that is an answer rather than a failure --
 *     which is why it is a facet and not a `Porcelain_Setting`, whose absence
 *     is a finding.
 *   - `Porcelain_Count(ORB)` is how many orbs the lane has. Asking each ORB
 *     element on a lane that has none would file four ABSENT findings for a
 *     fact the count already answers without a watch.
 *   - The action role behind each orb is a watched ELEMENT, so an arming
 *     change re-describes instead of being polled with four find + four
 *     actions calls every frame.
 *
 * ## How an orb is presented
 *
 * Each orb is one Porcelain CONTROL carrying a derived 57x34 picture:
 *
 *   - On a lane whose cache draws its own orbs, `PORCELAIN_REPLACE` on
 *     `ORB(n)`. Porcelain creates the control as a SIBLING under the native
 *     root's parent and copies the root's parent-local box onto it -- a
 *     REPLACE anchor from a child of the target is ANCHOR_INVALID, which is
 *     what the old child-at-(0,0) shape was. The anchor carries ordering and
 *     the native visibility; the box is Porcelain's to copy.
 *   - On a lane without native orbs, `PORCELAIN_AT_ELEMENT` on the minimap
 *     with interface 160's own offsets, clamped out of the map disc, and
 *     `visible_with = MINIMAP` so four plates never hang beside nothing.
 *
 * ## Clicking one
 *
 * The control is armed with one operation whose label is the reference verb.
 * Pressing it invokes a CHECKED native widget action: the lane's
 * `[role:action_frame_orb_*]` node is looked up live, its current actions are
 * queried and the first is invoked through the ordinary native dispatcher,
 * which re-checks visibility, masks and identity. A raw
 * `<interface>:<component>[:<op>]` override, and the older `[iface:<name>]`
 * compatibility lookup, remain as documented escape hatches.
 *
 * Pressing the real button rather than writing the var is what makes this
 * work at all: the run toggle and the special attack are the SERVER's, and a
 * client that flipped the varp locally would show a state the server never
 * agreed to and lose it on the next sync.
 *
 * A native action that answers NATIVE_BLOCKED now STOPS. It used to fall
 * through to the compatibility id and press an unrelated component by number.
 */

/** Defined at the foot of this file; Porcelain_Open names it at on_start. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS;

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
    /* The digits, as one row of glyphs. @see orbs_compose_number. */
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

/**
 * Everything one orb's picture depends on, as bytes.
 *
 * This IS the compose key: Porcelain hashes the struct and paints at most once
 * per value of it. `art` is the count of source images decoded so far, which
 * is what makes a picture composed before its plate landed redraw once it
 * does -- the old hash carried only "is the frame there", so a fill disc that
 * arrived late never triggered a redraw and on_asset had to zero every key by
 * hand.
 *
 * Packed explicitly (no padding to hash) because the whole struct is hashed.
 */
struct OrbPicture
{
    int32_t fill_image;
    int32_t icon_image;
    int32_t value;
    int32_t filled;
    int32_t total;
    int32_t inactive;
    int32_t digits_ready;
    int32_t art;
};

/** What the painter is handed: the instance, and which orb it is drawing. */
struct OrbPaintCall
{
    struct OrbsState* state;
    int orb;
};

/** All mutable data belongs to one registered instance. */
struct OrbsState
{
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;

    /* Decoded source art, read once each through assets.image_pixels. The
     * handle itself is Porcelain's: it is requested on first use and released
     * after several describe runs that did not ask for it, which frees the
     * eleven of fifteen slots this plugin never reads through a handle again
     * once the pixels are copied. */
    uint32_t* pixels[ORB_IMG_COUNT];
    int pixel_w[ORB_IMG_COUNT];
    int pixel_h[ORB_IMG_COUNT];
    /** How many of the fifteen have been decoded. A hashed painter input. */
    int art;

    struct OrbGlyph digit[10];
    int digits_ready;
    int digit_steps;
    int digit_row_h;
    int digit_line_height;
    int digit_max_ascent;
    int digit_max_descent;

    /* Resolved once, at the first describe: a varp id is a property of the
     * cache, not of the frame. */
    bool ids_resolved;
    int run_varp;
    int spec_varp;
    int spec_armed_varp;

    /* The inputs the last describe drew from, so a frame that changed none of
     * them costs one comparison and no describe. */
    struct OrbPicture picture[ORB_COUNT];
    /** What the last describe decided about each orb, so the per-frame
     *  number poll compares the same picture the description drew. */
    bool available[ORB_COUNT];
    bool bound[ORB_COUNT];
    int native_count;
    /**
     * Describe runs in which the minimap was bound.
     *
     * Porcelain keeps a re-described key under the parent it was CREATED
     * under: a control first placed beside the minimap and later re-described
     * as REPLACE(ORB) is moved to the target's parent-local box but stays a
     * child of the minimap's parent, so it lands at that box under the WRONG
     * origin. Until the layer re-creates on a placement change, the beside
     * placement waits for the orb question to settle -- the same two fences
     * Porcelain itself waits before calling an element ABSENT.
     */
    int settled;
    /**
     * The incarnation each orb's control was CREATED against.
     *
     * Porcelain creates a control under its target's parent once and keeps it
     * there: a key re-described after the target REMOUNTED under a different
     * parent is moved to the new parent-local box but stays a child of the old
     * parent, so it lands at that box under the wrong origin. Until the layer
     * re-creates on a parent change, the description does it: an orb whose
     * target has a new incarnation is not re-described for one fence, which
     * removes the control, and is described again on the next, which creates
     * it under the parent it now belongs to.
     */
    uint64_t incarnation[ORB_COUNT];
    bool described[ORB_COUNT];
    /**
     * A re-create is owed, and it takes TWO fences.
     *
     * "A key not re-described is removed" is a rule of the RECONCILE, and the
     * reconcile runs once per fence, on the LAST describe pass. A describe
     * that calls Porcelain_Invalidate forces a second pass inside that same
     * fence, and it is the second pass which is reconciled -- so a run that
     * described nothing in order to have its controls removed is discarded,
     * the keys are still in the final scratch, and nothing is removed at all.
     *
     * So the wipe describes nothing and does NOT invalidate: this fence
     * reconciles the empty description and the stale controls go. The flag is
     * consumed AFTER the fence, which invalidates for the next one, and that
     * is where the column is created again under the parent it now belongs
     * to. One frame without it, and correct on the other side of the switch.
     */
    bool recreate;
    /**
     * The frame root the controls were created under.
     *
     * Same rule as the incarnation above, one level up: a root switch (the
     * `layout` verb, a provider swap) builds a new frame root, and a control
     * created under the old one is not moved to the new one -- it is simply
     * gone, with the description still believing it is there. Noticing the
     * change here and dropping the whole column for one fence is what puts it
     * back.
     *
     * The minimap's incarnation is watched for the same reason and is the
     * sharper signal of the two: a `layout` verb rebuilds the whole HUD, and
     * the frame root can be the same NODE on the other side of that while
     * everything under it -- the owned controls included -- has been torn
     * down and rebuilt. There is no verb that asks whether an owned control
     * still exists, so the rebuild has to be inferred from the elements that
     * do report one.
     */
    struct ToriRS_WidgetRef root;
    uint64_t map_incarnation;
    /* The canvas box each control was last reported at, for the harness. */
    struct ToriRS_WidgetBounds reported[ORB_COUNT];
    bool reported_live[ORB_COUNT];

    struct OrbPaintCall call[ORB_COUNT];
    char derived_key[ORB_COUNT][32];
    /** Set while orbs_paint is running, so the painter's log line is the
     *  "recomposed" one the pixel checker reads and not a per-frame echo. */
    struct OrbPicture painting[ORB_COUNT];
};

static bool
orbs_cfg_bool(struct ToriRS_Api* api, char const* key)
{
    bool value = false;
    assert(api);
    assert(key);
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
orbs_cfg_int(struct ToriRS_Api* api, char const* key)
{
    int value = 0;
    assert(api);
    assert(key);
    (void)api->config.get_int(api, key, &value);
    return value;
}

static char const*
orbs_cfg_string(struct ToriRS_Api* api, char const* key)
{
    char const* value = "";
    assert(api);
    assert(key);
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
 * checked action ref invoked through the widget API; this path remains so an
 * older or private profile keeps working during migration.
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

    assert(api);
    assert(name);
    assert(out_component);
    assert(out_op);

    if( !api->cache.named_id || !api->cache.named_id(api, "iface", name, &declared) )
        return 0;
    *out_component = declared;
    *out_op = 0;
    return 1;
}

/**
 * Which varp holds `name` on this cache: the plugin's override, the profile's
 * declaration, then the historical id.
 *
 * Three steps and not one, because the three answer different questions and
 * only the middle one is knowable from here. The config key is the escape
 * hatch for a private server that moved a var and has no profile entry to say
 * so; `[varp:<name>]` is where a lane states it properly; and the fallback is
 * what keeps a lane that has declared nothing drawing an orb instead of
 * hiding it.
 *
 * Resolved ONCE, into OrbsState: it used to be recomputed several times a
 * frame, each time a string compare through the profile's ref table.
 *
 * @param key this plugin's config key holding an override.
 * @return the id, or -1 when even the fallback is switched off (`0`), or when
 * `fallback` is negative and the lane declared nothing.
 */
static int
orbs_varp(
    struct ToriRS_Api* api,
    char const* key,
    char const* name,
    int fallback)
{
    int const override = key ? orbs_cfg_int(api, key) : -1;
    int declared;

    assert(api);
    assert(name);

    if( override > 0 )
        return override;
    /* 0 is a legal varp id, so "switched off" needs a value of its own: a
     * config key set to 0 means "this lane has no such var, draw nothing". */
    if( override == 0 )
        return -1;

    if( api->cache.named_id && api->cache.named_id(api, "varp", name, &declared) )
        return declared;
    return fallback;
}

static void
orbs_resolve_ids(struct OrbsState* state)
{
    struct ToriRS_Api* api = state->api;

    assert(state);
    if( state->ids_resolved )
        return;
    state->ids_resolved = true;
    state->run_varp = orbs_varp(api, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
    state->spec_varp = orbs_varp(api, "spec_varp", "special_attack_energy", ORB_VARP_SPEC_FALLBACK);
    /*
     * No fallback and no config key: "armed" was `spec_varp + 1` for years,
     * which is arithmetic over a cache id and silently wrong on a lane that
     * put it anywhere else. A lane that has an armed bit declares it; a lane
     * that does not has no armed bit, and the orb simply never lights.
     */
    state->spec_armed_varp = orbs_varp(api, NULL, "special_attack_armed", -1);
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
 *
 * Still the raw asset verbs: the plan's `Porcelain_Table` is specified and not
 * built, so there is nowhere yet to say "PENDING on the web lane is a state,
 * not a failure".
 */
static int
orbs_load_digits(struct ToriRS_Api* api, struct OrbsState* state)
{
    char const* at;
    size_t size = 0;

    assert(api);
    assert(state);

    if( state->digits_ready )
        return 1;

    if( !api->assets.request || api->assets.request(api, "digits.ini") != TORIRS_ASSET_READY )
        return 0;
    if( !api->assets.bytes || !api->assets.bytes(api, "digits.ini", (void const**)&at, &size) )
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
    int space;

    assert(state);
    space = h - state->digit_max_ascent - state->digit_max_descent;
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

/*
 * Each orb's key, verb, the role of the native button it presses, and the
 * config keys of its raw overrides.
 *
 * ORB_HP's action role is named even though no profile in the tree declares
 * it: an absence that is declared is a fact, and `Porcelain_ExpectAbsent`
 * makes it one in both directions -- a profile that ever does bind a cure verb
 * turns the run red rather than quietly arming an orb nobody tested.
 */
static struct
{
    char const* key;
    char const* show_key;
    char const* button_key;
    char const* button_name;
    char const* action;
    char const* action_role;
} const ORB_PART[ORB_COUNT] = {
    { "orb_hitpoints", "show_hp", "hp_button", "orb_hp_button", "Cure",
      "action_frame_orb_hitpoints_activate" },
    { "orb_prayer", "show_prayer", "prayer_button", "orb_prayer_button", "Quick-prayers",
      "action_frame_orb_prayer_activate" },
    { "orb_run", "show_run", "run_button", "orb_run_on", "Toggle Run",
      "action_frame_orb_run_enable" },
    { "orb_special", "show_spec", "spec_button", "orb_spec_button", "Use Special Attack",
      "action_frame_orb_special_activate" },
};

/* ------------------------------------------------------------ compositing */

/**
 * The decoded pixels of one shipped image, read once.
 *
 * The HANDLE is Porcelain's -- requested on first use, released after several
 * describe runs that did not ask for it. This asks only until the copy exists,
 * so eleven of the fifteen slots are handed back after boot instead of being
 * held for the life of the instance.
 */
static uint32_t const*
orbs_pixels(struct OrbsState* state, int which, int* out_w, int* out_h)
{
    struct ToriRS_Api* api = state->api;

    assert(state);
    assert(which >= 0 && which < ORB_IMG_COUNT);
    assert(out_w);
    assert(out_h);

    if( !state->pixels[which] )
    {
        enum PorcelainAssetState asset = PORCELAIN_ASSET_PENDING;
        struct ToriRS_ImageRef const ref =
            Porcelain_Image(state->porcelain, ORB_IMAGE_FILE[which], &asset);
        int w = 0, h = 0;
        size_t count = 0;
        uint32_t* argb;

        if( asset != PORCELAIN_ASSET_READY || ref.value == 0 )
            return NULL;
        if( !api->assets.image_size(api, ref, &w, &h) || w <= 0 || h <= 0 || w > 1024 || h > 1024 )
            return NULL;
        argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
        assert(argb);
        if( !api->assets.image_pixels(api, ref, argb, (size_t)w * (size_t)h, &count) ||
            count != (size_t)w * (size_t)h )
        {
            free(argb);
            return NULL;
        }
        state->pixels[which] = argb;
        state->pixel_w[which] = w;
        state->pixel_h[which] = h;
        state->art++;
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
    struct OrbsState* state,
    uint32_t* target,
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
    uint32_t const* src;

    assert(state);
    assert(target);
    src = orbs_pixels(state, which, &w, &h);
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
            target[y * ORB_W + x] = orbs_blend(target[y * ORB_W + x], src[sy * w + sx], alpha);
        }
    }
}

/**
 * `value`, centred on `cx`, with `top` as the line box's top, from the atlas.
 * The colour is the METER's: the atlas carries clientscript 449's ramp as rows.
 */
static void
orbs_compose_number(
    struct OrbsState* state,
    uint32_t* target,
    int cx,
    int top,
    int value,
    int filled,
    int total)
{
    char text[16];
    int len, width = 0, pen, row = 0;

    assert(state);
    assert(target);

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
        orbs_blit(state, target, ORB_IMG_DIGITS, dx - g->x, dy - src_y, dx, dy, g->w, g->h, 255);
        pen += g->advance;
    }
}

/**
 * One orb's picture: plate, meter disc, icon and number.
 *
 * Porcelain calls this at most once per (key, hash of struct OrbPicture), so
 * the "a quiet frame draws nothing" rule that orbs_key used to enforce by hand
 * is the layer's now. The three states the reference draws an orb in
 * (clientscript 2792): INACTIVE a grey disc at trans 50 and no operation; idle
 * the orb's own colour at trans 25. The lit (hovered) plate is not reproduced.
 *
 * @return false only when the plate art is not decoded, which is a terminal
 * DERIVED_FAILED and one finding -- so an orb that never got its art says so
 * instead of publishing a transparent square.
 */
static bool
orbs_paint(struct ToriRS_Api* api, void* user, uint32_t* argb, int width, int height)
{
    struct OrbPaintCall const* call = user;
    struct OrbsState* state;
    struct OrbPicture const* picture;
    int trans, hidden, w, h;

    assert(api);
    assert(call);
    assert(argb);
    assert(width == ORB_W);
    assert(height == ORB_H);

    state = call->state;
    picture = &state->painting[call->orb];
    trans = picture->inactive ? 50 : 25;

    if( !orbs_pixels(state, ORB_IMG_FRAME, &w, &h) )
        return false;

    api->core.log(api, "MINIMAP_ORBS_VALUE orb=%s value=%d filled=%d total=%d inactive=%d",
        ORB_PART[call->orb].key, picture->value, picture->filled, picture->total,
        picture->inactive);

    orbs_blit(state, argb, ORB_IMG_FRAME, 0, 0, 0, 0, ORB_W, ORB_H, 255);
    orbs_blit(state, argb, picture->fill_image, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y,
        ORB_DISC, ORB_DISC, 255 - trans);
    /* The dark disc over the unfilled rows, rounded so 98 of 99 still shows
     * a sliver of dark: the orb must never read as full when it is not. */
    hidden = picture->total > 0
                 ? ORB_DISC - (picture->filled * ORB_DISC + picture->total - 1) / picture->total
                 : ORB_DISC;
    if( hidden < 0 ) hidden = 0;
    if( hidden > ORB_DISC ) hidden = ORB_DISC;
    if( hidden > 0 )
        orbs_blit(state, argb, ORB_IMG_FILL_EMPTY, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y,
            ORB_DISC, hidden, 255);
    orbs_blit(state, argb, picture->icon_image, ORB_DISC_X, ORB_DISC_Y, ORB_DISC_X, ORB_DISC_Y,
        ORB_DISC, ORB_DISC, 255);
    if( state->digits_ready )
        orbs_compose_number(state, argb, ORB_TEXT_CX,
            orbs_text_origin(state, ORB_TEXT_Y, ORB_TEXT_H), picture->value, picture->filled,
            picture->total);
    return true;
}

/* ------------------------------------------------------------ the numbers */

/**
 * Is the player running?
 *
 * The lane's ACTIVE facet on its own run orb, and the resolved varp only where
 * there is no such orb to carry it. Both read the id the profile declares; the
 * facet is that answer arriving without a string lookup and without this file
 * doing the arithmetic.
 */
static bool
orbs_running(struct OrbsState* state, bool orb_bound, uint32_t facets)
{
    assert(state);
    if( orb_bound )
        return (facets & PORCELAIN_FACET_ACTIVE) != 0;
    return state->run_varp >= 0 && state->api->cache.varp &&
           state->api->cache.varp(state->api, state->run_varp) != 0;
}

/**
 * What the orb's picture should be, from the live numbers and the lane facets.
 *
 * @return false when there is no reading to draw: an unstated skill, a lane
 * with no special-attack var, a switched-off orb.
 */
static bool
orbs_picture(
    struct OrbsState* state,
    int orb,
    bool orb_bound,
    uint32_t facets,
    bool action_available,
    struct OrbPicture* out)
{
    struct ToriRS_Api* api = state->api;

    assert(state);
    assert(out);
    assert(orb >= 0 && orb < ORB_COUNT);

    memset(out, 0, sizeof(*out));
    out->digits_ready = state->digits_ready;
    out->art = state->art;

    if( orb == ORB_HP || orb == ORB_PRAYER )
    {
        struct ToriRS_SkillSnapshot skill;
        int const index = orb == ORB_HP ? ORB_STAT_HITPOINTS : ORB_STAT_PRAYER;
        memset(&skill, 0, sizeof(skill));
        skill.struct_size = sizeof(skill);
        if( !api->game || !api->game->skill(api, index, &skill) )
            return false;
        /*
         * `stated` is the reading, and it is the whole test. The old
         * `base_level <= 0` guard was a guess at the same question -- the
         * pre-login table is a fresh account's, not an empty one, so a level
         * that has never been sent looks exactly like a level 1 -- and it
         * also refused a legitimately level-0 skill.
         */
        if( !skill.stated )
            return false;
        out->fill_image = orb == ORB_HP ? ORB_IMG_FILL_RED : ORB_IMG_FILL_PRAYER;
        out->icon_image = orb == ORB_HP ? ORB_IMG_ICON_HP : ORB_IMG_ICON_PRAYER;
        out->value = skill.current_level;
        out->filled = skill.current_level;
        out->total = skill.base_level;
        return true;
    }
    if( orb == ORB_RUN )
    {
        int const energy = api->game ? api->game->run_energy(api) : 0;
        bool const running = orbs_running(state, orb_bound, facets);
        out->fill_image = ORB_IMG_FILL_GOLD;
        out->icon_image = running ? ORB_IMG_ICON_RUN : ORB_IMG_ICON_WALK;
        out->value = energy;
        out->filled = energy;
        out->total = 100;
        out->inactive = !running;
        if( out->inactive )
        {
            out->fill_image = ORB_IMG_FILL_GREY;
            out->filled = out->total;
        }
        return true;
    }

    {
        int const spec_max = orbs_cfg_int(api, "spec_max");
        int energy;
        bool armed;
        if( state->spec_varp < 0 || spec_max <= 0 || !api->cache.varp )
            return false;
        energy = api->cache.varp(api, state->spec_varp);
        if( energy < 0 ) energy = 0;
        if( energy > spec_max ) energy = spec_max;
        armed = orb_bound ? (facets & PORCELAIN_FACET_ACTIVE) != 0
                          : (state->spec_armed_varp >= 0 &&
                             api->cache.varp(api, state->spec_armed_varp) > 0);
        out->fill_image = armed ? ORB_IMG_FILL_CYAN_LIT : ORB_IMG_FILL_CYAN;
        out->icon_image = ORB_IMG_ICON_SPEC;
        out->value = energy * 100 / spec_max;
        out->filled = energy;
        out->total = spec_max;
        out->inactive = !action_available;
        if( out->inactive )
        {
            out->fill_image = ORB_IMG_FILL_GREY;
            out->filled = out->total;
        }
        return true;
    }
}

/* ------------------------------------------------------------ actions */

/**
 * The role, config key and compat name of the button this orb presses now.
 *
 * One function for the three because they are one ANSWER -- "which button is
 * the run orb at this moment" -- and the run orb has two: `..._enable` while
 * walking and `..._disable` while running, which is what lets one control
 * carry both. Three functions asking the same question separately is how two
 * of them end up answering it differently.
 */
static void
orbs_button(
    struct OrbsState* state,
    int orb,
    bool orb_bound,
    uint32_t facets,
    char const** out_role,
    char const** out_key,
    char const** out_name)
{
    assert(state);
    assert(out_role);
    assert(out_key);
    assert(out_name);
    *out_role = ORB_PART[orb].action_role;
    *out_key = ORB_PART[orb].button_key;
    *out_name = ORB_PART[orb].button_name;
    if( orb != ORB_RUN )
        return;
    if( !orbs_running(state, orb_bound, facets) )
        return;
    *out_role = "action_frame_orb_run_disable";
    *out_key = "run_button_off";
    *out_name = "orb_run_off";
}

/** The current native action behind this orb's button role, if any. */
static bool
orbs_native_action(
    struct ToriRS_Api* api,
    char const* role,
    struct ToriRS_WidgetActionRef* out)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetRef button;
    struct ToriRS_WidgetAction action;
    size_t count = 0;

    assert(api);
    if( !role || ui->find(ui->context, role, &button) != TORIRS_CONTRACT_OK )
        return false;
    if( ui->actions(ui->context, button, &action, 1, &count) != TORIRS_CONTRACT_OK && count == 0 )
        return false;
    if( count == 0 )
        return false;
    if( out )
        *out = action.ref;
    return true;
}

/** Press the orb: raw override, checked native action, then compatibility. */
static void
orbs_press(struct ToriRS_Api* api, void* user, char const* key)
{
    struct OrbsState* state = user;
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetActionRef action;
    struct PorcelainElementState orb;
    char const* config_key;
    char const* name;
    char const* role;
    int component, operation;
    int i;
    bool bound;

    assert(api);
    assert(state);
    assert(key);

    for( i = 0; i < ORB_COUNT; i++ )
        if( strcmp(ORB_PART[i].key, key) == 0 )
            break;
    if( i == ORB_COUNT )
        return;

    bound = Porcelain_Element(state->porcelain, PORCELAIN_ORB_EL(i), &orb);
    orbs_button(state, i, bound, orb.facets, &role, &config_key, &name);

    if( orbs_parse_button(orbs_cfg_string(api, config_key), &component, &operation) )
        goto invoke;
    if( orbs_native_action(api, role, &action) )
    {
        enum ToriRS_ContractResult const result = ui->invoke(ui->context, action);
        api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=native result=%d", ORB_PART[i].key, result);
        if( result == TORIRS_CONTRACT_OK )
            return;
        /*
         * The action exists and the engine refused it -- natively hidden,
         * masked, or gone between the query and the press. Guessing a
         * component id from an older profile entry at that point presses
         * something unrelated by number, which is exactly what this used to
         * do. Stop here and say so.
         */
        api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=refused role=%s result=%d",
            ORB_PART[i].key, role ? role : "-", result);
        return;
    }
    if( !orbs_compat_button(api, name, &component, &operation) )
    {
        api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=none", ORB_PART[i].key);
        return;
    }
invoke:
    if( !api->cache.invoke || !api->cache.invoke(api, component, operation) )
    {
        char message[160];
        snprintf(message, sizeof(message),
            "Minimap orbs: this world has no interface component %d for '%s'.", component,
            config_key);
        api->core.notify(api, message);
        api->core.log(api, "%s", message);
        return;
    }
    api->core.log(api, "MINIMAP_ORBS_OP orb=%s via=component component=%d op=%d",
        ORB_PART[i].key, component, operation);
}

/* ------------------------------------------------------------ layout */

/** The first column of the map disc's ink over rows [top, bottom), in the
 * map's parent-local space; the box's right edge when the span misses it. */
static int
orbs_map_ink_left(struct ToriRS_WidgetBounds const* map, int top, int bottom)
{
    long cx, cy, r;
    int left;

    assert(map);
    cx = 2 * (long)map->x + map->width;
    cy = 2 * (long)map->y + map->height;
    r = map->width < map->height ? map->width : map->height;
    left = map->x + map->width;
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

/**
 * Interface 160's slot for `orb` beside `map`, clamped out of the disc, as an
 * OFFSET from the map's own origin -- which is what PORCELAIN_AT_ELEMENT
 * takes, and the same arithmetic the parent-local placement did.
 *
 * The clamp stays the plugin's: the layer has no verb for "the opaque extent
 * of this element over these rows", and a plain BESIDE-with-a-gap puts the
 * lower orbs under the map.
 */
static void
orbs_beside_map(
    struct ToriRS_Api* api,
    struct ToriRS_WidgetBounds const* map,
    int orb,
    int* out_dx,
    int* out_dy)
{
    int x;
    int y;
    int limit;

    assert(api);
    assert(map);
    assert(out_dx);
    assert(out_dy);
    assert(orb >= 0);
    assert(orb < ORB_SLOT_COUNT);
    x = map->x + orbs_cfg_int(api, "offset_x") - ORB_W + ORB_SLOT[orb].dx;
    y = map->y + map->height / 4 + orbs_cfg_int(api, "offset_y") - ORB_SLOT[0].dy +
        ORB_SLOT[orb].dy;
    limit = orbs_map_ink_left(map, y, y + ORB_H);
    if( x + ORB_W > limit )
        x = limit - ORB_W;
    *out_dx = x - map->x;
    *out_dy = y - map->y;
}

/**
 * Where this orb's plate belongs in CANVAS space.
 *
 * One function, because the description and the harness read-out must not be
 * able to disagree: the line the pixel checker reads is the box the control
 * was actually described at.
 */
static struct ToriRS_WidgetBounds
orbs_canvas_box(
    struct OrbsState* state,
    int orb,
    bool orb_bound,
    struct PorcelainElementState const* native,
    struct PorcelainElementState const* map)
{
    struct ToriRS_WidgetBounds box;

    assert(state);
    assert(native);
    assert(map);
    box.width = ORB_W;
    box.height = ORB_H;
    if( orb_bound )
    {
        /* Centred on the native root, which is 57x34 on every lane that has
         * one, so the offset is zero there and honest anywhere else. */
        box.x = native->box.x + (native->box.width - ORB_W) / 2;
        box.y = native->box.y + (native->box.height - ORB_H) / 2;
        return box;
    }
    {
        int dx = 0, dy = 0;
        orbs_beside_map(state->api, &map->local, orb, &dx, &dy);
        box.x = map->box.x + dx;
        box.y = map->box.y + dy;
    }
    return box;
}

/* ------------------------------------------------------------ describe */

/**
 * Does this orb have a verb to offer?
 *
 * The action role is a watched ELEMENT, so its arrival and departure move the
 * describe's ELEMENT stamp. That is what replaced the per-frame poll: four
 * `find` plus four `actions` calls every frame, purely to notice that a
 * special weapon had been equipped.
 *
 * The role is only ASKED FOR where the lane can have it: the run pair is
 * declared by every profile in the tree, and the prayer and special roles only
 * by a profile that also draws the orbs. Watching one on a lane that cannot
 * have it would file an ABSENT finding for something the orb count already
 * answered.
 */
static bool
orbs_action_available(
    struct OrbsState* state,
    int orb,
    bool orb_bound,
    uint32_t facets,
    int native_count)
{
    struct ToriRS_Api* api = state->api;
    struct PorcelainElementState action;
    char const* role;
    char const* key;
    char const* name;
    int component, operation;

    assert(state);
    orbs_button(state, orb, orb_bound, facets, &role, &key, &name);
    if( orbs_parse_button(orbs_cfg_string(api, key), &component, &operation) )
        return true;
    /*
     * Which of the two remaining rungs is ASKED first is lane data, and it is
     * about findings rather than about the answer -- both say "there is
     * something to press", and orbs_press tries them in its own order at press
     * time either way.
     *
     * A lane that draws its own orbs declares the semantic roles beside them,
     * so the role is the watched element there and its arrival and departure
     * drive the describe -- which is what replaced a per-frame poll of four
     * `find` plus four `actions` calls. A lane that draws none declares the
     * older `[iface:]` entry instead, so that is asked there, and a role
     * watched on a lane that cannot have it would file an ABSENT finding for a
     * fact the orb count has already answered.
     */
    if( native_count == 0 && orbs_compat_button(api, name, &component, &operation) )
        return true;
    if( role && state->settled >= PORCELAIN_ABSENT_FENCES &&
        (native_count > 0 || orb == ORB_HP) &&
        Porcelain_Element(state->porcelain, PORCELAIN_ROLE_EL(role), &action) )
        return action.input_present;
    if( native_count > 0 )
        return orbs_compat_button(api, name, &component, &operation) != 0;
    return false;
}

static void
orbs_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct OrbsState* state = user;
    struct Porcelain* porcelain = describe->porcelain;
    struct ToriRS_Api* api = state->api;
    struct PorcelainElementState map;
    bool have_map;
    bool replace_native;
    int native_count;
    uint32_t screen_facets = 0;

    assert(describe);
    assert(state);

    orbs_resolve_ids(state);
    (void)orbs_load_digits(api, state);

    have_map = Porcelain_Element(porcelain, PORCELAIN_EL(MINIMAP), &map);
    if( have_map )
        screen_facets = map.facets;

    /*
     * How many orbs the LANE has, by data and without a watch. A lane with
     * none never asks for an ORB element, so it never files four absences for
     * a question this already answered.
     */
    native_count = Porcelain_Count(porcelain, PORCELAIN_EL_ORB);
    state->native_count = native_count;
    if( have_map && state->settled < PORCELAIN_ABSENT_FENCES )
        state->settled++;

    /*
     * Has the tree this column hangs on been rebuilt under us?
     *
     * Porcelain creates a control under its target's parent ONCE and never
     * moves it: a control whose parent was torn down is gone, with the
     * description still believing it is there, and no verb asks whether an
     * owned control still exists. So the rebuild is inferred from the three
     * elements that DO report one -- the frame root the canvas placement
     * parents to, the minimap, and each orb's own incarnation -- and the
     * column is dropped for one fence so that it is created again, under the
     * parent it now belongs to, on the next.
     *
     * @see OrbsState::recreate for why that takes two fences and not one.
     */
    {
        struct PorcelainElementState root;
        bool rebuilt = false;

        if( Porcelain_Element(porcelain, PORCELAIN_EL(FRAME_ROOT), &root) )
        {
            rebuilt = ToriRS_WidgetRefValid(state->root) &&
                      !ToriRS_WidgetRefEqual(state->root, root.ref);
            state->root = root.ref;
        }
        if( have_map )
        {
            rebuilt = rebuilt ||
                      (state->map_incarnation != 0 && state->map_incarnation != map.incarnation);
            state->map_incarnation = map.incarnation;
        }
        for( int i = 0; native_count > 0 && i < ORB_COUNT; i++ )
        {
            struct PorcelainElementState orb;
            if( !Porcelain_Element(porcelain, PORCELAIN_ORB_EL(i), &orb) )
                continue;
            rebuilt = rebuilt || (state->described[i] && state->incarnation[i] != 0 &&
                                  state->incarnation[i] != orb.incarnation);
            state->incarnation[i] = orb.incarnation;
        }
        if( rebuilt )
        {
            for( int i = 0; i < ORB_COUNT; i++ )
            {
                state->described[i] = false;
                state->reported_live[i] = false;
            }
            state->recreate = true;
            return;
        }
    }
    replace_native = orbs_cfg_bool(api, "replace_native");
    /*
     * Nothing is described before the plate art is decoded. Porcelain's
     * derived state is terminal on a painter that answered false, so a
     * painter run before its art landed would file DERIVED_FAILED once and
     * never draw the orb again.
     */
    {
        int plate_w = 0, plate_h = 0;
        /*
         * Decode everything that has landed before composing anything. The
         * count of decoded images is one of the painter's hashed inputs -- it
         * is what redraws a picture composed before its fill disc arrived --
         * so decoding them one at a time, as each blit first asks, would
         * redraw every orb once per image instead of once.
         */
        for( int image = 0; image < ORB_IMG_COUNT; image++ )
        {
            int width = 0, height = 0;
            (void)orbs_pixels(state, image, &width, &height);
        }
        if( !orbs_pixels(state, ORB_IMG_FRAME, &plate_w, &plate_h) )
            return;
    }

    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct PorcelainElementState orb;
        struct PorcelainItem item;
        struct OrbPicture picture;
        enum PorcelainDerivedState derived = PORCELAIN_DERIVED_PENDING;
        struct ToriRS_ImageRef composed;
        bool orb_bound = false;
        bool available;

        memset(&orb, 0, sizeof(orb));
        state->bound[i] = false;
        state->available[i] = false;
        if( !orbs_cfg_bool(api, ORB_PART[i].show_key) )
        {
            state->described[i] = false;
            continue;
        }
        if( native_count > 0 )
            orb_bound = Porcelain_Element(porcelain, PORCELAIN_ORB_EL(i), &orb);
        state->bound[i] = orb_bound;
        /*
         * A cutscene hides the cache's own orbs. The facet is reported on
         * every node because it is a fact about the SCREEN, so any bound
         * element answers it -- and a lane with no cutscene varbit answers
         * "no cutscene", which is the lane's answer and not a failure.
         */
        if( screen_facets & PORCELAIN_FACET_HIDDEN_BY_CUTSCENE )
            continue;
        /* A native orb the user chose to keep gets no cover at all; it does
         * not fall back to beside-the-map. */
        if( orb_bound && !replace_native )
            continue;
        if( !orb_bound && !have_map )
            continue;
        /* @see OrbsState::settled. */
        if( !orb_bound && state->settled < PORCELAIN_ABSENT_FENCES )
            continue;

        available = orbs_action_available(state, i, orb_bound, orb.facets, native_count);
        state->available[i] = available;
        if( !orbs_picture(state, i, orb_bound, orb.facets, available, &picture) )
            continue;

        state->painting[i] = picture;
        state->call[i].state = state;
        state->call[i].orb = i;
        composed = Porcelain_Derived(porcelain, state->derived_key[i], &picture, sizeof(picture),
            ORB_W, ORB_H, orbs_paint, &state->call[i], &derived);
        (void)composed;
        if( derived != PORCELAIN_DERIVED_READY )
            continue;
        state->picture[i] = picture;

        memset(&item, 0, sizeof(item));
        item.key = ORB_PART[i].key;
        item.image = state->derived_key[i];
        item.w = ORB_W;
        item.h = ORB_H;
        item.op_label = ORB_PART[i].action;
        item.on_op = orbs_press;
        item.user = state;
        item.hit = true;
        item.enabled = available;
        /*
         * AT_CANVAS, and not REPLACE or AT_ELEMENT, for both placements.
         *
         * The two parent-local placements derive the box from
         * PorcelainElementState::local under the target's PARENT, and on every
         * lane with a frame provider that lands the control at the target's
         * NATIVE origin instead of the one it draws at -- measured at (-5,0)
         * on classic-fixed 548, (+33,-4) on 161, (0,-10) on modern 164 and
         * (+20,-14) on the 601 stone drawer, and exact only on the untranslated
         * native lane. See the port report: it is a fault in the layer's
         * geometry, not a choice made here.
         *
         * The canvas box, which this plugin computes from the element's own
         * DRAWN box, is right on all six. It is also the truer description:
         * the orb column is the topmost piece of minimap chrome in the
         * reference, it must not be under the frame's own housing art, and a
         * cover that consumed the native orb's menu rows -- which REPLACE
         * does and the historical child-at-(0,0) did not -- would be a
         * behaviour change this port has no mandate for.
         *
         * `visible_with` carries what the anchor would otherwise have: the
         * cover is presented exactly when the thing it stands for is.
         */
        {
            struct ToriRS_WidgetBounds const box = orbs_canvas_box(state, i, orb_bound, &orb, &map);
            item.place.kind = PORCELAIN_AT_CANVAS;
            item.place.dx = box.x;
            item.place.dy = box.y;
            item.visible_with = orb_bound ? PORCELAIN_ORB_EL(i) : PORCELAIN_EL(MINIMAP);
        }
        describe->control(describe, &item);
        state->described[i] = true;
    }
}

/* ------------------------------------------------------------ the harness */

/**
 * `MINIMAP_ORBS_CONTROL` for every control whose canvas box changed.
 *
 * The box is computed from the same element state the description placed it
 * against, because Porcelain has no read-out of an applied item's resolved
 * box -- see the port report. For REPLACE that is the native root's canvas
 * box, which is exactly what the pixel checker compares it with; for the
 * beside placement it is the minimap's canvas box plus the offset.
 */
static void
orbs_log_controls(struct OrbsState* state)
{
    struct ToriRS_Api* api = state->api;
    struct PorcelainElementState map;
    bool have_map;

    assert(state);
    have_map = Porcelain_Element(state->porcelain, PORCELAIN_EL(MINIMAP), &map);
    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct PorcelainElementState orb;
        struct ToriRS_WidgetBounds box;

        memset(&orb, 0, sizeof(orb));
        if( !state->described[i] || (!state->bound[i] && !have_map) )
        {
            state->reported_live[i] = false;
            continue;
        }
        if( state->bound[i] && !Porcelain_Element(state->porcelain, PORCELAIN_ORB_EL(i), &orb) )
        {
            state->reported_live[i] = false;
            continue;
        }
        box = orbs_canvas_box(state, i, state->bound[i], &orb, &map);
        if( state->reported_live[i] && box.x == state->reported[i].x &&
            box.y == state->reported[i].y )
            continue;
        state->reported[i] = box;
        state->reported_live[i] = true;
        api->core.log(api, "MINIMAP_ORBS_CONTROL orb=%s native=%d armed=%d box=%d,%d,%d,%d",
            ORB_PART[i].key, state->bound[i] ? 1 : 0, state->available[i] ? 1 : 0, box.x, box.y,
            box.width, box.height);
    }
}

/* ------------------------------------------------------------ lifecycle */

/**
 * Have the live numbers moved?
 *
 * Describe re-runs when one of Porcelain's six inputs moved, and a skill
 * packet is not one of them: it is this plugin's own input, so this plugin
 * says so. Reading four numbers and comparing them is what a settled frame
 * costs now -- no widget call, no string lookup, no composition.
 */
static bool
orbs_numbers_moved(struct OrbsState* state)
{
    bool moved = false;

    assert(state);
    for( int i = 0; i < ORB_COUNT; i++ )
    {
        struct PorcelainElementState orb;
        struct OrbPicture picture;

        memset(&orb, 0, sizeof(orb));
        /* Only an orb the last describe actually placed is polled, and its
         * element is only asked for where that describe already asked -- a
         * poll that watched an element the lane does not have would file the
         * absence the orb count exists to avoid. */
        if( state->native_count > 0 && state->bound[i] )
            (void)Porcelain_Element(state->porcelain, PORCELAIN_ORB_EL(i), &orb);
        if( !orbs_picture(state, i, state->bound[i], orb.facets, state->available[i], &picture) )
            continue;
        /* `art` and `digits_ready` are the describe's business; only the
         * numbers are polled here. */
        if( picture.value != state->picture[i].value ||
            picture.filled != state->picture[i].filled ||
            picture.total != state->picture[i].total ||
            picture.inactive != state->picture[i].inactive ||
            picture.fill_image != state->picture[i].fill_image ||
            picture.icon_image != state->picture[i].icon_image )
            moved = true;
    }
    return moved;
}

static void
orbs_frame(struct ToriRS_Api* api, void* plugin_state, struct ToriRS_FrameEvent const* event)
{
    struct OrbsState* state = plugin_state;

    assert(api);
    assert(state);
    (void)event;
    if( !state->porcelain )
        return;
    if( orbs_numbers_moved(state) )
        Porcelain_Invalidate(state->porcelain);
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
    /*
     * AFTER the fence, never from inside the describe. @see
     * OrbsState::recreate: invalidating from in there re-runs the describe in
     * the same fence, and the empty description never reaches the reconcile.
     */
    if( state->recreate )
    {
        state->recreate = false;
        Porcelain_Invalidate(state->porcelain);
    }
    orbs_log_controls(state);
}

static void
orbs_start(struct ToriRS_Api* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;

    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    state->api = api;
    state->digit_steps = 1;
    state->run_varp = -1;
    state->spec_varp = -1;
    state->spec_armed_varp = -1;
    for( int i = 0; i < ORB_COUNT; i++ )
        snprintf(state->derived_key[i], sizeof(state->derived_key[i]), "%s.composed",
            ORB_PART[i].key);

    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_MINIMAP_ORBS, state);
    assert(state->porcelain);
    /*
     * Claim all fifteen image slots here, in the order they have always been
     * claimed. Porcelain requests an image on first use and hands the slot
     * back after several describe runs that did not ask for it, which is the
     * lifetime this plugin wants -- but "first use" is the first COMPOSITION,
     * several frames after the other plugins have loaded their art, and every
     * one of their handles would land in a different slot. Asking here keeps
     * the numbering and still lets the eleven write-once images go back.
     */
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
    {
        enum PorcelainAssetState asset = PORCELAIN_ASSET_PENDING;
        (void)Porcelain_Image(state->porcelain, ORB_IMAGE_FILE[i], &asset);
    }
    /*
     * No profile in this tree binds a cure verb for the hitpoints orb, so its
     * "Cure" label is armed only through a raw override or an [iface:] compat
     * entry -- that is, never, by default. Declared rather than discovered:
     * the run stays clean while it holds, and goes red the day a profile does
     * bind one and nobody wired the orb up to it.
     */
    Porcelain_ExpectAbsent(state->porcelain,
        PORCELAIN_ROLE_EL("action_frame_orb_hitpoints_activate"),
        "no profile in this tree binds a cure verb");
    Porcelain_Describe(state->porcelain, orbs_describe, state);
}

static void
orbs_stop(struct ToriRS_Api* api, void* plugin_state)
{
    struct OrbsState* state = plugin_state;

    assert(api);
    assert(state);
    /* Porcelain removes every control, releases every image it requested and
     * every picture it derived, and resets every edit. The decoded pixel
     * copies and the raw digits.ini are this file's own. */
    Porcelain_Close(state->porcelain);
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        free(state->pixels[i]);
    if( api->assets.release )
        api->assets.release(api, "digits.ini");
    memset(state, 0, sizeof(*state));
}

static void
orbs_changed(struct ToriRS_Api* api, void* plugin_state, char const* key)
{
    struct OrbsState* state = plugin_state;

    assert(api);
    assert(state);
    (void)key;
    if( state->porcelain )
        Porcelain_Note(state->porcelain, PORCELAIN_INPUT_CONFIG);
}

static void
orbs_asset(struct ToriRS_Api* api, void* plugin_state, struct ToriRS_AssetEvent const* event)
{
    struct OrbsState* state = plugin_state;

    assert(api);
    assert(state);
    (void)event;
    if( state->porcelain )
        Porcelain_Note(state->porcelain, PORCELAIN_INPUT_ASSET);
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
_Static_assert(
    ORB_SLOT_COUNT == ORB_COUNT,
    "one column slot per orb, in the reference's order");

static struct ToriRS_ConfigSchema const ORBS_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = ORBS_CONFIG,
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "minimap-orbs",
    .title = "Minimap Orbs",
    .version = "3.0.0",
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
