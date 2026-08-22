#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
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
 * A plugin is the right shape for that gap precisely because it is not tied to
 * a revision. It brings its own art, reads the numbers through the api rather
 * than through a cache table, and anchors to wherever THIS gameframe put the
 * minimap. Nothing here knows which era it is running on.
 *
 * ## The art is the plugin's, not the cache's
 *
 * `script/plugins/assets/minimap-orbs/*.png` are ten files shipped beside this
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
 */

/* The plate, and where interface 160 puts each piece inside it. */
#define ORB_W 57
#define ORB_H 34
#define ORB_DISC_X 27
#define ORB_DISC_Y 4
#define ORB_DISC 26
/* The value panel: x=4 y=16 w=23 h=13, centred both ways. The baseline is the
 * box's middle plus half a cap height, which is where the interfaces' own
 * valign=1 lands an 11px face. */
#define ORB_TEXT_CX (4 + 23 / 2)
#define ORB_TEXT_BASELINE (16 + 13 / 2 + 4)
/** Yellow, as every orb's own `colour=16776960` states it. */
#define ORB_TEXT_RGB 0xFFFF00u

/** Hitpoints, in the skill order that has not moved since 2001. */
#define ORB_STAT_HITPOINTS 3

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
    ORB_IMG_FILL_EMPTY,
    ORB_IMG_FILL_RED,
    ORB_IMG_FILL_GREY,
    ORB_IMG_FILL_GOLD,
    ORB_IMG_FILL_CYAN,
    ORB_IMG_ICON_HP,
    ORB_IMG_ICON_WALK,
    ORB_IMG_ICON_RUN,
    ORB_IMG_ICON_SPEC,
    ORB_IMG_COUNT
};

static char const* const ORB_IMAGE_FILE[ORB_IMG_COUNT] = {
    "frame.png",     "fill_empty.png", "fill_red.png",  "fill_grey.png",
    "fill_gold.png", "fill_cyan.png",  "icon_hp.png",   "icon_walk.png",
    "icon_run.png",  "icon_spec.png",
};

/** Handles from api->image_load, or -1 while a load has not been asked for. */
static int g_image[ORB_IMG_COUNT];

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
    int total)
{
    char text[16];
    int hidden;

    if( g_image[ORB_IMG_FRAME] < 0 )
        return;

    g_api->draw_image(ctx, surface, g_image[ORB_IMG_FRAME], x, y, 0, 0, 0, 0, 0);
    g_api->draw_image(
        ctx, surface, g_image[fill_image], x + ORB_DISC_X, y + ORB_DISC_Y, 0, 0, 0, 0, 0);

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

    snprintf(text, sizeof(text), "%d", value);
    g_api->draw_text(
        ctx, surface, x + ORB_TEXT_CX, y + ORB_TEXT_BASELINE, text, ORB_TEXT_RGB);
}

static enum ToriRS_PluginVerdict
orbs_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDrawCanvas* ev = (struct ToriRS_PluginEvDrawCanvas*)event;
    int map_x;
    int map_y;
    int map_w;
    int map_h;
    int x;
    int y;
    int pitch;

    assert(ctx);
    assert(ev);

    /*
     * Nothing to anchor to means nothing to draw. A gameframe with no minimap
     * -- the login screen, a cutscene that took it away -- is a state, not a
     * fault, and the orbs simply are not there for it.
     */
    if( !g_api->minimap_rect(ctx, &map_x, &map_y, &map_w, &map_h) )
        return TORIRS_PLUGIN_PASS;
    (void)map_h;

    /* Asked for every frame, not only at start: an image that failed its read
     * on the first attempt is retried, and a plugin re-enabled after a reload
     * has no handles at all. Both are answered by the same line, and a handle
     * it already has costs a table scan. */
    orbs_load_images(ctx);

    /*
     * The column hangs off the minimap's LEFT edge, which is where interface
     * 160 puts it and where a player looks for it. The two offsets are config
     * so that a gameframe whose minimap sits against the screen edge, or whose
     * frame art the plate would cover, can move them without a rebuild.
     */
    x = map_x + g_api->cfg_int(ctx, "offset_x") - ORB_W;
    y = map_y + g_api->cfg_int(ctx, "offset_y");
    pitch = g_api->cfg_int(ctx, "pitch");

    if( g_api->cfg_bool(ctx, "show_hp") )
    {
        int current = 0;
        int base = 0;
        g_api->stat(ctx, ORB_STAT_HITPOINTS, &current, &base);
        /* A client that has not been told its stats yet reads 0/0, and a
         * hitpoints orb showing zero out of zero looks like a death. */
        if( base > 0 )
        {
            orbs_draw_one(
                ctx, ev->surface, x, y, ORB_IMG_FILL_RED, ORB_IMG_ICON_HP,
                current, current, base);
            y += pitch;
        }
    }

    if( g_api->cfg_bool(ctx, "show_run") )
    {
        int const energy = g_api->run_energy(ctx);
        int const run_varp = orbs_varp(ctx, "run_varp", "run_mode", ORB_VARP_RUN_FALLBACK);
        /* Run ON is the gold disc and the running boot; walking is the grey
         * disc and the standing one -- the same pair the reference swaps. A
         * lane with no run var reads as walking rather than as an error. */
        int const running = run_varp >= 0 && g_api->varp(ctx, run_varp) != 0;
        orbs_draw_one(
            ctx,
            ev->surface,
            x,
            y,
            running ? ORB_IMG_FILL_GOLD : ORB_IMG_FILL_GREY,
            running ? ORB_IMG_ICON_RUN : ORB_IMG_ICON_WALK,
            energy,
            energy,
            100);
        y += pitch;
    }

    if( g_api->cfg_bool(ctx, "show_spec") )
    {
        int const spec_varp =
            orbs_varp(ctx, "spec_varp", "special_attack_energy", ORB_VARP_SPEC_FALLBACK);
        int const spec_max = g_api->cfg_int(ctx, "spec_max");
        if( spec_varp >= 0 && spec_max > 0 )
        {
            int energy = g_api->varp(ctx, spec_varp);
            if( energy < 0 )
                energy = 0;
            if( energy > spec_max )
                energy = spec_max;
            orbs_draw_one(
                ctx,
                ev->surface,
                x,
                y,
                ORB_IMG_FILL_CYAN,
                ORB_IMG_ICON_SPEC,
                /* The panel reads a PERCENT, as the reference's does; the bar
                 * itself is in thousandths and only the fill uses them. */
                energy * 100 / spec_max,
                energy,
                spec_max);
            y += pitch;
        }
    }

    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orbs_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    /* The handles are file-static and this plugin can be stopped and started
     * again, which drops every image the host held for it. Forgetting them
     * here is what makes the next start ask for them afresh instead of drawing
     * with handles the host has since handed to someone else. */
    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        g_image[i] = -1;
    orbs_load_images(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orbs_stop(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;

    for( int i = 0; i < ORB_IMG_COUNT; i++ )
        g_image[i] = -1;
    return TORIRS_PLUGIN_PASS;
}

static void
orbs_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, orbs_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, orbs_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, orbs_draw, NULL);
}

/*
 * `-1` on the two varp keys means "work it out", which is the answer for every
 * lane in this tree; a number is the override a private server needs. They are
 * shown in the panel rather than hidden because the whole point of them is
 * that someone whose orb reads wrong can fix it without a rebuild.
 */
static struct ToriRS_PluginConfigItem const ORBS_CONFIG[] = {
    { "show_hp",   TORIRS_PLUGIN_CFG_BOOL, "Hitpoints orb",           "1",  0, 0,      NULL, 0 },
    { "show_run",  TORIRS_PLUGIN_CFG_BOOL, "Run energy orb",          "1",  0, 0,      NULL, 0 },
    { "show_spec", TORIRS_PLUGIN_CFG_BOOL, "Special attack orb",      "1",  0, 0,      NULL, 0 },
    { "offset_x",  TORIRS_PLUGIN_CFG_INT,  "Offset from minimap left", "6", -512, 512, NULL, 0 },
    { "offset_y",  TORIRS_PLUGIN_CFG_INT,  "Offset from minimap top",  "0", -512, 512, NULL, 0 },
    { "pitch",     TORIRS_PLUGIN_CFG_INT,  "Spacing between orbs",    "33", 8,  128,   NULL, 0 },
    { "run_varp",  TORIRS_PLUGIN_CFG_INT,  "Run mode varp (-1 auto)", "-1", -1, 65535, NULL, 0 },
    { "spec_varp", TORIRS_PLUGIN_CFG_INT,  "Special attack varp (-1 auto)", "-1", -1, 65535, NULL, 0 },
    { "spec_max",  TORIRS_PLUGIN_CFG_INT,  "Special attack bar maximum", "1000", 1, 100000, NULL, 0 },
    { NULL,        TORIRS_PLUGIN_CFG_BOOL, NULL,                      NULL, 0, 0,      NULL, 0 },
};

_Static_assert(ORB_SPEC_MAX == 1000, "the spec_max default above states this number too");

struct ToriRS_PluginDef const TORIRS_PLUGIN_MINIMAP_ORBS = {
    .name = "minimap-orbs",
    .title = "Minimap Orbs",
    .version = "1.0.0",
    .priority = 0,
    .config = ORBS_CONFIG,
    /*
     * OFF until asked for, and this one has a second reason beyond the usual.
     *
     * The usual reason is that a client which marks up the screen on first
     * launch without being asked reads as broken. The second is that on a
     * rev-239 cache the gameframe draws these orbs ITSELF, from interface 160,
     * and a plugin drawing a second set over them would be two of everything.
     * The lane that wants them is the one whose cache has none.
     */
    .disabled_by_default = true,
    .init = orbs_init,
    .shutdown = NULL,
};
