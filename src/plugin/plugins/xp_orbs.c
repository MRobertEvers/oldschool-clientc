#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * XP drop orbs: a globe per skill you just gained experience in, showing how
 * far through the level that gain took you.
 *
 * A port of RuneLite's "XP Globes" (runelite-client .../plugins/xpglobes),
 * whose own config calls them orbs -- `Orb size`, `Orb duration`,
 * `alignOrbsVertically`. One appears when a skill's xp goes up, carries that
 * skill's icon inside a ring filled to its progress towards the next level,
 * and fades out after a few seconds. Hovering one darkens it, prints the
 * percentage across the middle and opens a tooltip with the numbers behind it.
 *
 * ## Why this is a plugin and not part of the client
 *
 * For the same reason the minimap orbs are: the picture does not exist in any
 * cache. The NUMBERS do -- UPDATE_STAT carries an xp per skill on every
 * revision this client speaks -- and no gameframe from 2004 to today draws
 * them like this, because this is a RuneLite idea rather than a Jagex one.
 * A plugin is what a client-owned readout is.
 *
 * ## The art is the plugin's
 *
 * Two files in `script/plugins/assets/xp-drop-orbs/`, both cut once at
 * authoring time:
 *
 *   skills.png  the 25 skill icons in one strip, 25x25 each, indexed BY SKILL
 *               ID -- so `skills.png` cell 3 is hitpoints because hitpoints is
 *               skill 3, and nothing has to map one to the other. Cut from the
 *               rev-239 sprites table (197..222 and 228 for sailing) and from
 *               the summoning lane's cache for 229, the wolf head, which the
 *               base cache leaves empty.
 *   text.png    a glyph atlas of the cache's own p11 face, in two colour rows
 *               -- white for values, orange for the labels beside them --
 *               with the reference's drop shadow already on them.
 *   text.ini    where each glyph is in that atlas.
 *
 * Both are files rather than cache ids for the reason NXT_CLIENT_PLUGINS
 * states: an id is a property of ONE revision, and this has to draw the same
 * orb on a 2004 cache, on a cache that failed to open, and on a client started
 * with no cache at all.
 *
 * ## Why it composes pixels instead of drawing shapes
 *
 * A globe is a disc, an annulus and a sector of an annulus. The draw verbs are
 * a rect, a line, a hull and a blit, because those are the primitives the
 * CLIENT needs -- and the two honest ways to get a circle out of them are to
 * teach four rasterisers a new primitive, or to approximate one out of a few
 * hundred one-pixel rects and spend the frame's entire draw budget on a single
 * orb.
 *
 * So it takes the third way: api->image_pixels reads the art this plugin
 * ships, the plugin rasterises the globe -- disc, rings, arc, icon, caption,
 * anti-aliased -- into an ARGB buffer, and api->image_compose publishes it as
 * an image. One blit per globe, one for the tooltip, and the shapes are as
 * good as the arithmetic rather than as good as the primitive.
 *
 * That also means the recompose is CACHED: the buffer is rebuilt only when
 * something in it changed (see orb_key), which for an idle globe is never.
 *
 * ## The numbers
 *
 * api->stat_xp answers the three a progress ring asks for -- the xp, and the
 * thresholds either side of the level it is inside -- out of the client's own
 * table, so this plugin carries no copy of the xp curve. What it does carry is
 * a per-skill TRACKER (struct XpTrack), because "xp per hour", "actions left"
 * and "time to goal" are not properties of a stat, they are properties of a
 * session: they need the xp this skill had when the session's first gain
 * landed and how many gains there have been since. That is exactly what
 * RuneLite's XpTrackerPlugin is, and it is a dozen lines when the only
 * consumer is this.
 *
 * A gain is noticed by polling on EV_SERVER_TICK rather than by an event of
 * its own, and deliberately: the client has no "a stat changed" event, the
 * poll is 25 integer compares once per server tick, and a poll cannot miss a
 * gain the way a subscription to the wrong packet could.
 */

/* -------------------------------------------------------------- the shape */

/** Globes on screen at once, as the reference's MAXIMUM_SHOWN_GLOBES. */
#define ORB_MAX_SHOWN 5
/** Gap between two globes, as the reference's MINIMUM_STEP. */
#define ORB_STEP 10
/** The full ring behind the progress arc, as PROGRESS_BACKGROUND_SIZE. */
#define ORB_RING_WIDTH 5
/** The icon's share of the disc, as GLOBE_ICON_RATIO. */
#define ORB_ICON_RATIO_NUM 65
#define ORB_ICON_RATIO_DEN 100
/** The wash over a hovered globe, as DARK_OVERLAY_COLOR. */
#define ORB_HOVER_ARGB 0xB4000000u
/** The tooltip's width, as TOOLTIP_RECT_SIZE_X. */
#define ORB_TIP_W 150
/** Its inset and its plate, as ComponentConstants STANDARD_BORDER and
 *  STANDARD_BACKGROUND_COLOR. */
#define ORB_TIP_BORDER 4
#define ORB_TIP_ARGB 0x9C463D32u

/** The largest orb the config will accept, and so the compose buffer's side:
 *  the orb plus the ring that straddles its edge. */
#define ORB_SIZE_MAX 96
#define ORB_BUF_MAX (ORB_SIZE_MAX + 2 * ORB_RING_WIDTH + 8)

/** One scratch buffer serves every compose, because image_compose copies the
 *  pixels before it returns. Wide enough for the tooltip, tall enough for the
 *  largest globe or the longest tooltip. */
#define ORB_SCRATCH_W 256
#define ORB_SCRATCH_H 192

/** Samples per pixel per axis when a shape's edge is resolved. 4x4 is finer
 *  than the eye separates on a 40px disc and costs nothing at this size. */
#define ORB_AA 4

_Static_assert(ORB_BUF_MAX <= ORB_SCRATCH_W, "a globe has to fit the scratch");
_Static_assert(ORB_BUF_MAX <= ORB_SCRATCH_H, "a globe has to fit the scratch");

static struct ToriRS_PluginApi const* g_api;

/* ------------------------------------------------------------- skill colour */

/**
 * The arc's colour per skill, as RuneLite's own SkillColor enum states it,
 * indexed by skill id.
 *
 * Restated rather than derived, because it is not derivable: it is a palette
 * somebody chose, and the whole value of matching it is that a player who
 * knows RuneLite reads the same colour for the same skill without looking at
 * the icon.
 *
 * Summoning is the one entry that is NOT the reference's, because the
 * reference has no summoning: SkillColor stops at sailing. This is a violet
 * chosen to sit clear of every neighbour in the table -- agility's navy,
 * defence's blue and thieving's plum are the near ones -- and it is stated
 * here rather than sampled from the wolf head so that it stays put if the icon
 * is ever re-cut.
 */
static uint32_t const ORB_SKILL_RGB[] = {
    0x9B2007u, /* attack       155,  32,   7 */
    0x6277BEu, /* defence       98, 119, 190 */
    0x04955Au, /* strength       4, 149,  90 */
    0x837E7Eu, /* hitpoints    131, 126, 126 */
    0x6D9017u, /* ranged       109, 144,  23 */
    0x9F9323u, /* prayer       159, 147,  35 */
    0x3250C1u, /* magic         50,  80, 193 */
    0x702386u, /* cooking      112,  35, 134 */
    0x348C25u, /* woodcutting   52, 140,  37 */
    0x038D7Du, /* fletching      3, 141, 125 */
    0x6A84A4u, /* fishing      106, 132, 164 */
    0xBD7819u, /* firemaking   189, 120,  25 */
    0x976E4Du, /* crafting     151, 110,  77 */
    0x6C6B52u, /* smithing     108, 107,  82 */
    0x5D8FA7u, /* mining        93, 143, 167 */
    0x078509u, /* herblore       7, 133,   9 */
    0x3A3C89u, /* agility       58,  60, 137 */
    0x6C3457u, /* thieving     108,  52,  87 */
    0x646464u, /* slayer       100, 100, 100 */
    0x65983Fu, /* farming      101, 152,  63 */
    0xAA8D1Au, /* runecraft    170, 141,  26 */
    0x5C5941u, /* hunter        92,  89,  65 */
    0x82745Fu, /* construction 130, 116,  95 */
    0x0BA59Du, /* sailing       11, 165, 157 */
    0xA860D6u, /* summoning    168,  96, 214 -- this plugin's own */
};
#define ORB_SKILL_RGB_COUNT ((int)(sizeof(ORB_SKILL_RGB) / sizeof(ORB_SKILL_RGB[0])))

/* ------------------------------------------------------------ the glyph atlas */

/**
 * One glyph in text.png, as text.ini states it.
 *
 * Same file format tools/fontbake_atlas.py writes for the minimap orbs' digit
 * strip, read here for the whole printable set rather than for ten digits, and
 * with the two colour ROWS used as colours rather than as a meter ramp. See
 * that tool for why an atlas is a plugin's font at all: api->draw_text draws
 * in the client's hitsplat face, which is a chunky combat face and not what a
 * caption is set in.
 */
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

/** Indexed by `ch - 32`, so the printable ASCII range and nothing else. */
#define ORB_GLYPH_FIRST 32
#define ORB_GLYPH_COUNT 96
static struct OrbGlyph g_glyph[ORB_GLYPH_COUNT];
static int g_glyph_ready;
/** Colour rows in the atlas and how far apart they are. Row 0 is white and
 *  row 1 orange, in the order the bake was asked for them. */
static int g_glyph_rows = 1;
static int g_glyph_row_h;
/** The line box, which is what a caption's height is measured in. */
static int g_glyph_line_h = 12;

#define ORB_TEXT_WHITE 0
#define ORB_TEXT_LABEL 1

/* ---------------------------------------------------------------- the state */

/** A skill's session tracker: what RuneLite's XpTrackerPlugin holds, for the
 *  three tooltip lines that are about a session rather than about a stat. */
struct XpTrack
{
    /** The xp this skill had when its first gain of the session landed, or -1
     *  for a skill that has not gained any. */
    int start_xp;
    uint64_t start_ms;
    /** Gains seen since. RuneLite calls one an ACTION, and it is the only
     *  meaning of the word available here: the client cannot see that three
     *  logs came off one tree. */
    int actions;
};

/** One globe on screen. */
struct XpGlobe
{
    int skill;
    int xp;
    int level;
    /** When it appeared, or was last touched by a hover. */
    uint64_t at_ms;
    /** What the composed image was built from, so an unchanged globe is not
     *  rasterised again. @see orb_key. */
    uint64_t key;
    /** Its image handle, or -1. One per SLOT rather than per skill: a slot's
     *  picture is replaced in place, and there are at most five of them. */
    int image;
};

static struct XpGlobe g_globe[ORB_MAX_SHOWN];
static int g_globe_count;

/** Per skill: the xp this plugin last saw, or -1 for one it has never seen.
 *  -1 is what makes the login burst -- every skill arriving at once -- seed
 *  the table instead of putting a globe on screen for all 25. */
static int* g_seen_xp;
static struct XpTrack* g_track;
static int g_skill_count;

/** The art, and its pixels once they have been read back. */
static int g_img_skills = -1;
static int g_img_text = -1;
static uint32_t* g_skills_px;
static int g_skills_w;
static int g_skills_h;
static uint32_t* g_text_px;
static int g_text_w;
static int g_text_h;

static uint32_t g_scratch[ORB_SCRATCH_W * ORB_SCRATCH_H];

/** The one verb a globe offers, and the reference's own: it flips the column
 *  between across and down. */
#define ORB_TAG_FLIP 1u

/* ---------------------------------------------------------------- pixel work */

static int
orb_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * `src` over `dst`, both non-premultiplied ARGB.
 *
 * Straight Porter-Duff over rather than a copy, because everything in a globe
 * is drawn on top of something: the ring straddles the disc's edge, the icon
 * sits on the disc, the hover wash sits on both.
 */
static uint32_t
orb_over(uint32_t dst, uint32_t src)
{
    uint32_t const sa = src >> 24;
    uint32_t da;
    uint32_t out_a;

    if( sa == 0 )
        return dst;
    if( sa == 255 )
        return src;

    da = dst >> 24;
    out_a = sa + da * (255 - sa) / 255;
    if( out_a == 0 )
        return 0;

    {
        uint32_t out = out_a << 24;
        for( int shift = 16; shift >= 0; shift -= 8 )
        {
            uint32_t const s = (src >> shift) & 0xFF;
            uint32_t const d = (dst >> shift) & 0xFF;
            uint32_t const c = (s * sa + d * da * (255 - sa) / 255) / out_a;
            out |= (c > 255 ? 255 : c) << shift;
        }
        return out;
    }
}

/** `rgb` at `coverage`/255 of `alpha`, as an ARGB to blend with. */
static uint32_t
orb_shade(uint32_t argb, int coverage)
{
    uint32_t const a = (argb >> 24) * (uint32_t)orb_clampi(coverage, 0, 255) / 255;
    return (a << 24) | (argb & 0x00FFFFFFu);
}

/* ------------------------------------------------------------ the primitives */

/**
 * How much of the pixel at (px, py) is inside the annulus sector.
 *
 * Resolved by sampling rather than by an edge equation, because the shape is
 * an intersection of three conditions -- outside the inner radius, inside the
 * outer one, and within the swept angle -- and a coverage that is right at
 * every one of their meetings is far more code than ORB_AA samples.
 *
 * `r_in`/`r_out` are in 1/256ths of a pixel so a stroke of an odd width still
 * straddles its path. `sweep` is in 1/1024ths of a turn measured CLOCKWISE
 * FROM STRAIGHT UP, which is where the reference starts its arc (`Arc2D` at 90
 * degrees, extending negative), and `sweep >= 1024` is the whole ring.
 */
static int
orb_ring_coverage(
    int px,
    int py,
    int cx256,
    int cy256,
    int r_in,
    int r_out,
    int sweep)
{
    int hits = 0;

    for( int sy = 0; sy < ORB_AA; sy++ )
    {
        for( int sx = 0; sx < ORB_AA; sx++ )
        {
            /* The sample's centre, in the same 1/256ths the radii are in. */
            int const x = px * 256 + (2 * sx + 1) * 256 / (2 * ORB_AA) - cx256;
            int const y = py * 256 + (2 * sy + 1) * 256 / (2 * ORB_AA) - cy256;
            double const d = sqrt((double)x * (double)x + (double)y * (double)y);

            if( d < r_in || d > r_out )
                continue;
            if( sweep < 1024 )
            {
                /* atan2(x, -y) is the angle clockwise from up, in (-pi, pi];
                 * folded to [0, 2pi) it is the arc's own parameter. */
                double a = atan2((double)x, (double)-y);
                if( a < 0.0 )
                    a += 2.0 * 3.14159265358979323846;
                if( a * 1024.0 / (2.0 * 3.14159265358979323846) > (double)sweep )
                    continue;
            }
            hits++;
        }
    }
    return hits * 255 / (ORB_AA * ORB_AA);
}

/** A filled ring/disc/arc into `buf`, `w`x`h`, in `argb`. @see
 *  orb_ring_coverage for the units. */
static void
orb_draw_ring(
    uint32_t* buf,
    int w,
    int h,
    int cx256,
    int cy256,
    int r_in,
    int r_out,
    int sweep,
    uint32_t argb)
{
    int const y0 = orb_clampi((cy256 - r_out) / 256 - 1, 0, h);
    int const y1 = orb_clampi((cy256 + r_out) / 256 + 2, 0, h);
    int const x0 = orb_clampi((cx256 - r_out) / 256 - 1, 0, w);
    int const x1 = orb_clampi((cx256 + r_out) / 256 + 2, 0, w);

    assert(buf);
    if( r_out <= 0 || sweep <= 0 )
        return;

    for( int y = y0; y < y1; y++ )
    {
        for( int x = x0; x < x1; x++ )
        {
            int const cover = orb_ring_coverage(x, y, cx256, cy256, r_in, r_out, sweep);
            if( cover <= 0 )
                continue;
            buf[y * w + x] = orb_over(buf[y * w + x], orb_shade(argb, cover));
        }
    }
}

/**
 * `src_w`x`src_h` pixels scaled into a `dst_w`x`dst_h` box at (dx, dy).
 *
 * Box-filtered, and over PREMULTIPLIED alpha: a cut-out scaled with its colour
 * unweighted drags the colour of its transparent pixels into the edge, which
 * on a black-outlined icon is a black halo. Every icon here is a cut-out.
 */
static void
orb_blit_scaled(
    uint32_t* buf,
    int w,
    int h,
    int dx,
    int dy,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_stride,
    int src_x,
    int src_y,
    int src_w,
    int src_h)
{
    assert(buf);
    assert(src);
    if( dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0 )
        return;

    for( int y = 0; y < dst_h; y++ )
    {
        int const ty = dy + y;
        if( ty < 0 || ty >= h )
            continue;
        for( int x = 0; x < dst_w; x++ )
        {
            int const tx = dx + x;
            /* The source box this destination pixel covers, at least one
             * source pixel wide so an upscale still samples something. */
            int const u0 = x * src_w / dst_w;
            int const v0 = y * src_h / dst_h;
            int u1 = (x + 1) * src_w / dst_w;
            int v1 = (y + 1) * src_h / dst_h;
            uint32_t a = 0;
            uint32_t r = 0;
            uint32_t g = 0;
            uint32_t b = 0;
            int n = 0;

            if( tx < 0 || tx >= w )
                continue;
            if( u1 <= u0 )
                u1 = u0 + 1;
            if( v1 <= v0 )
                v1 = v0 + 1;

            for( int v = v0; v < v1 && v < src_h; v++ )
            {
                for( int u = u0; u < u1 && u < src_w; u++ )
                {
                    uint32_t const p = src[(src_y + v) * src_stride + src_x + u];
                    uint32_t const pa = p >> 24;
                    a += pa;
                    r += ((p >> 16) & 0xFF) * pa;
                    g += ((p >> 8) & 0xFF) * pa;
                    b += (p & 0xFF) * pa;
                    n++;
                }
            }
            if( n == 0 || a == 0 )
                continue;
            {
                uint32_t const px = ((a / (uint32_t)n) << 24) | ((r / a) << 16) |
                                    ((g / a) << 8) | (b / a);
                buf[ty * w + tx] = orb_over(buf[ty * w + tx], px);
            }
        }
    }
}

/* -------------------------------------------------------------------- text */

/**
 * Read text.ini into g_glyph.
 *
 * Parsed here rather than through a config key because it is not a SETTING: it
 * is the other half of the atlas, generated beside it, and a user editing it
 * could only ever break the pairing.
 *
 * A glyph line is one whose SECOND byte is '=', which is what lets the space
 * glyph -- a line beginning with a space -- be read by the same rule as every
 * other, and keeps it apart from the header keys (`ascent=`, `steps=`) and
 * from the comment lines, whose second byte is never that.
 */
static int
orb_load_glyphs(struct ToriRS_PluginCtx* ctx)
{
    char const* at;
    int size = 0;

    if( g_glyph_ready )
        return 1;
    if( !g_api->asset_load(ctx, "text.ini") )
        return 0;
    at = (char const*)g_api->asset_data(ctx, "text.ini", &size);
    if( !at || size <= 0 )
        return 0;

    for( char const* end = at + size; at < end; )
    {
        char const* line = at;
        char const* stop = line;
        while( stop < end && *stop != '\n' )
            stop++;
        at = stop < end ? stop + 1 : end;

        if( stop - line > 6 && strncmp(line, "steps=", 6) == 0 )
        {
            g_glyph_rows = atoi(line + 6);
            continue;
        }
        if( stop - line > 11 && strncmp(line, "row_height=", 11) == 0 )
        {
            g_glyph_row_h = atoi(line + 11);
            continue;
        }
        if( stop - line > 12 && strncmp(line, "line_height=", 12) == 0 )
        {
            g_glyph_line_h = atoi(line + 12);
            continue;
        }
        if( stop - line < 3 || line[1] != '=' )
            continue;
        {
            int const index = (unsigned char)line[0] - ORB_GLYPH_FIRST;
            struct OrbGlyph* g;

            if( index < 0 || index >= ORB_GLYPH_COUNT )
                continue;
            g = &g_glyph[index];
            if( sscanf(
                    line + 2, "%d %d %d %d %d %d %d", &g->x, &g->y, &g->w, &g->h,
                    &g->off_x, &g->off_y, &g->advance) == 7 )
                g_glyph_ready = 1;
        }
    }
    return g_glyph_ready;
}

/** How wide `text` is in the atlas face. */
static int
orb_text_width(char const* text)
{
    int width = 0;

    assert(text);
    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - ORB_GLYPH_FIRST;
        if( index >= 0 && index < ORB_GLYPH_COUNT )
            width += g_glyph[index].advance;
    }
    return width;
}

/** `text` into `buf`, with `x` as the pen and `top` as the line box's top. */
static void
orb_text(
    uint32_t* buf,
    int w,
    int h,
    int x,
    int top,
    char const* text,
    int row)
{
    int pen = x;

    assert(buf);
    assert(text);
    if( !g_glyph_ready || !g_text_px )
        return;
    if( row < 0 || row >= g_glyph_rows )
        row = 0;

    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - ORB_GLYPH_FIRST;
        struct OrbGlyph const* g;

        if( index < 0 || index >= ORB_GLYPH_COUNT )
            continue;
        g = &g_glyph[index];
        if( g->w > 0 && g->h > 0 )
            orb_blit_scaled(
                buf, w, h, pen + g->off_x, top + g->off_y, g->w, g->h, g_text_px,
                g_text_w, g->x, g->y + row * g_glyph_row_h, g->w, g->h);
        pen += g->advance;
    }
}

/** `value` with thousands separators, as the reference's DecimalFormat. */
static void
orb_commas(char* out, int out_size, int value)
{
    char plain[16];
    int len;
    int at = 0;

    assert(out);
    snprintf(plain, sizeof(plain), "%d", value < 0 ? -value : value);
    len = (int)strlen(plain);
    if( value < 0 && at + 1 < out_size )
        out[at++] = '-';
    for( int i = 0; i < len; i++ )
    {
        if( i > 0 && (len - i) % 3 == 0 && at + 1 < out_size )
            out[at++] = ',';
        if( at + 1 < out_size )
            out[at++] = plain[i];
    }
    out[at < out_size ? at : out_size - 1] = '\0';
}

/** `seconds` as H:MM:SS, or MM:SS under an hour. */
static void
orb_duration(char* out, int out_size, int seconds)
{
    assert(out);
    if( seconds < 0 )
        seconds = 0;
    if( seconds >= 100 * 3600 )
    {
        snprintf(out, out_size, "99:59:59");
        return;
    }
    if( seconds >= 3600 )
        snprintf(
            out, out_size, "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60,
            seconds % 60);
    else
        snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

/* ------------------------------------------------------------------ levels */

/**
 * The level `xp` is worth past the client's own table, i.e. a VIRTUAL level.
 *
 * The client's table stops at 99, which is where its stats stop, so this is
 * the one number the api cannot answer and the plugin has to work out. It is
 * the same series the client builds its table from -- `points += level + 300 *
 * 2^(level/7)`, quartered -- carried past 99 to the 200m ceiling, which is the
 * definition of a virtual level rather than a second opinion about a real one.
 */
static int
orb_virtual_level(int xp)
{
    double points = 0.0;
    int level;

    for( level = 1; level < 126; level++ )
    {
        points += floor((double)level + 300.0 * pow(2.0, (double)level / 7.0));
        if( (int)(points / 4.0) > xp )
            break;
    }
    return level;
}

/* ------------------------------------------------------------------ config */

static uint32_t
orb_cfg_argb(struct ToriRS_PluginCtx* ctx, char const* key, int alpha)
{
    uint32_t const rgb = g_api->cfg_color(ctx, key) & 0x00FFFFFFu;
    return ((uint32_t)orb_clampi(alpha, 0, 255) << 24) | rgb;
}

static int
orb_size(struct ToriRS_PluginCtx* ctx)
{
    return orb_clampi(g_api->cfg_int(ctx, "orb_size"), 16, ORB_SIZE_MAX);
}

static int
orb_arc_width(struct ToriRS_PluginCtx* ctx)
{
    return orb_clampi(g_api->cfg_int(ctx, "arc_width"), 1, 12);
}

/** The ring straddles the disc's edge, so the buffer is wider than the orb by
 *  half of the widest of the two rings, top and bottom. The reference's own
 *  progressArcOffset. */
static int
orb_arc_offset(struct ToriRS_PluginCtx* ctx)
{
    int const widest =
        ORB_RING_WIDTH > orb_arc_width(ctx) ? ORB_RING_WIDTH : orb_arc_width(ctx);
    return (widest + 1) / 2;
}

/* -------------------------------------------------------------- the globes */

/** Drop globe `slot`, keeping the rest in order. */
static void
orb_remove(struct ToriRS_PluginCtx* ctx, int slot)
{
    assert(slot >= 0);
    assert(slot < g_globe_count);

    /* The IMAGE stays with the slot rather than travelling with the globe:
     * handles are per-slot ("globe0".."globe4") and a slot that inherits
     * another globe's picture simply recomposes on its next frame, because its
     * key no longer matches. */
    (void)ctx;
    for( int i = slot; i + 1 < g_globe_count; i++ )
    {
        int const image = g_globe[i].image;
        g_globe[i] = g_globe[i + 1];
        g_globe[i].image = image;
        g_globe[i].key = 0;
    }
    g_globe_count--;
    g_globe[g_globe_count].skill = -1;
    g_globe[g_globe_count].key = 0;
}

/**
 * Put `skill` on screen, or refresh the globe it already has.
 *
 * Ordered BY SKILL, as the reference orders them, so the row does not reshuffle
 * every time a different skill ticks -- and when there are too many, the OLDEST
 * goes, not the leftmost.
 */
static void
orb_add(struct ToriRS_PluginCtx* ctx, int skill, int xp, int level, uint64_t now)
{
    int at;

    for( int i = 0; i < g_globe_count; i++ )
    {
        if( g_globe[i].skill != skill )
            continue;
        g_globe[i].xp = xp;
        g_globe[i].level = level;
        g_globe[i].at_ms = now;
        return;
    }

    if( g_globe_count >= ORB_MAX_SHOWN )
    {
        int oldest = 0;
        for( int i = 1; i < g_globe_count; i++ )
            if( g_globe[i].at_ms < g_globe[oldest].at_ms )
                oldest = i;
        orb_remove(ctx, oldest);
    }

    for( at = 0; at < g_globe_count && g_globe[at].skill < skill; at++ )
        ;
    for( int i = g_globe_count; i > at; i-- )
    {
        int const image = g_globe[i].image;
        g_globe[i] = g_globe[i - 1];
        g_globe[i].image = image;
        g_globe[i].key = 0;
    }
    {
        int const image = g_globe[at].image;
        memset(&g_globe[at], 0, sizeof(g_globe[at]));
        g_globe[at].skill = skill;
        g_globe[at].xp = xp;
        g_globe[at].level = level;
        g_globe[at].at_ms = now;
        g_globe[at].image = image;
        g_globe[at].key = 0;
    }
    g_globe_count++;
}

/** Forget every globe and every session number. What a logout is. */
static void
orb_reset(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    for( int i = 0; i < ORB_MAX_SHOWN; i++ )
    {
        g_globe[i].skill = -1;
        g_globe[i].key = 0;
    }
    g_globe_count = 0;
    for( int i = 0; i < g_skill_count; i++ )
    {
        g_seen_xp[i] = -1;
        g_track[i].start_xp = -1;
        g_track[i].start_ms = 0;
        g_track[i].actions = 0;
    }
}

/** How many skills this client has, discovered once from api->skill_name. */
static void
orb_size_tables(struct ToriRS_PluginCtx* ctx)
{
    int count = 0;

    if( g_seen_xp )
        return;
    while( g_api->skill_name(ctx, count) )
        count++;
    assert(count > 0);

    g_skill_count = count;
    g_seen_xp = malloc((size_t)count * sizeof(*g_seen_xp));
    assert(g_seen_xp);
    g_track = malloc((size_t)count * sizeof(*g_track));
    assert(g_track);
    orb_reset(ctx);
}

/* --------------------------------------------------------------- the poll */

static enum ToriRS_PluginVerdict
orb_tick(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    struct ToriRS_PluginPlayerSnap me;
    uint64_t const now = g_api->frame_ms(ctx);
    int const hide_maxed = g_api->cfg_bool(ctx, "hide_maxed");
    int const virtual_level = g_api->cfg_bool(ctx, "show_virtual_level");

    assert(ctx);
    orb_size_tables(ctx);

    /*
     * Logged out is a reset, and it is the honest stand-in for the
     * reference's LOGGING_IN and HOPPING. The client has no login event in the
     * plugin bus, but it does have "is there a local player", and every way of
     * arriving at a different account's stat table passes through not having
     * one. Without this, hopping shows a globe for every skill whose xp
     * differs between the two characters.
     */
    if( !g_api->local_player(ctx, &me) )
    {
        if( g_globe_count > 0 || (g_skill_count > 0 && g_seen_xp[0] >= 0) )
            orb_reset(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    for( int skill = 0; skill < g_skill_count; skill++ )
    {
        int xp = 0;
        int level = 0;

        if( !g_api->stat_xp(ctx, skill, &xp, NULL, NULL) )
            continue;
        g_api->stat(ctx, skill, NULL, &level);

        /* The first sight of a skill SEEDS. Every stat arrives at once on
         * login, and 25 globes is not what a player did. */
        if( g_seen_xp[skill] < 0 )
        {
            g_seen_xp[skill] = xp;
            continue;
        }
        if( xp <= g_seen_xp[skill] )
        {
            /* A drop means a different character's table -- the reset above
             * missed it, or the server corrected one. Re-seed rather than
             * report a negative gain. */
            g_seen_xp[skill] = xp;
            continue;
        }

        if( g_track[skill].start_xp < 0 )
        {
            g_track[skill].start_xp = g_seen_xp[skill];
            g_track[skill].start_ms = now;
        }
        g_track[skill].actions++;
        g_seen_xp[skill] = xp;

        if( level >= 99 )
        {
            if( hide_maxed )
                continue;
            if( virtual_level )
                level = orb_virtual_level(xp);
        }
        orb_add(ctx, skill, xp, level, now);
    }
    return TORIRS_PLUGIN_PASS;
}

/* ------------------------------------------------------------- composition */

/**
 * Everything a composed globe's picture depends on, in one number.
 *
 * The compose is the expensive half of this plugin -- a few thousand
 * anti-aliased samples -- and for a globe sitting still nothing in it changes
 * from one frame to the next. Hashing the inputs is what turns "rasterise five
 * discs sixty times a second" into "rasterise one when its xp moves".
 *
 * Everything the rasteriser READS has to be in here. A colour left out is a
 * config change that does nothing until the next xp drop.
 */
static uint64_t
orb_key(
    struct ToriRS_PluginCtx* ctx,
    struct XpGlobe const* globe,
    int progress,
    int hovered)
{
    uint64_t key = 1469598103934665603u;
    uint32_t const parts[] = {
        (uint32_t)globe->skill,
        (uint32_t)progress,
        (uint32_t)hovered,
        (uint32_t)orb_size(ctx),
        (uint32_t)orb_arc_width(ctx),
        (uint32_t)g_api->cfg_bool(ctx, "custom_arc_color"),
        g_api->cfg_color(ctx, "arc_color"),
        g_api->cfg_color(ctx, "outline_color"),
        g_api->cfg_color(ctx, "background_color"),
        (uint32_t)g_api->cfg_int(ctx, "background_alpha"),
    };

    for( size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++ )
    {
        key ^= parts[i];
        key *= 1099511628211u;
    }
    return key;
}

/**
 * One globe, rasterised into g_scratch and published.
 *
 * The reference's own build order, which is why the pieces line up without a
 * hand-tuned offset anywhere: the background disc, the icon on it, the hover
 * wash and its caption over that, then the full ring and the progress arc over
 * everything -- the arc last because it straddles the disc's edge and has to
 * sit on top of it rather than under.
 *
 * @return the image handle, or -1 when it could not be published.
 */
static int
orb_compose(
    struct ToriRS_PluginCtx* ctx,
    struct XpGlobe const* globe,
    int slot,
    int progress,
    int hovered)
{
    char name[TORIRS_PLUGIN_ASSET_NAME_MAX];
    int const size = orb_size(ctx);
    int const arc_w = orb_arc_width(ctx);
    int const offset = orb_arc_offset(ctx);
    int const side = size + 2 * offset;
    /* The disc's centre and its path radius, in 1/256ths: the circle the
     * reference draws is inscribed in a `size` box at (offset, offset). */
    int const cx = (offset * 2 + size) * 128;
    int const cy = cx;
    int const radius = size * 128;
    uint32_t const bg = orb_cfg_argb(
        ctx, "background_color", g_api->cfg_int(ctx, "background_alpha"));
    uint32_t const outline = orb_cfg_argb(ctx, "outline_color", 255);
    uint32_t arc;

    assert(globe);
    assert(side <= ORB_SCRATCH_W);
    assert(side <= ORB_SCRATCH_H);

    if( g_api->cfg_bool(ctx, "custom_arc_color") )
        arc = orb_cfg_argb(ctx, "arc_color", 255);
    else if( globe->skill >= 0 && globe->skill < ORB_SKILL_RGB_COUNT )
        arc = 0xFF000000u | ORB_SKILL_RGB[globe->skill];
    else
        arc = 0xFFFFFFFFu;

    memset(g_scratch, 0, (size_t)side * (size_t)side * sizeof(uint32_t));

    /* 1. the disc. */
    orb_draw_ring(g_scratch, side, side, cx, cy, 0, radius, 1024, bg);

    /* 2. the skill icon, at the reference's ratio of the disc inside the arc. */
    if( g_skills_px && globe->skill >= 0 )
    {
        int const cell = g_skills_h;
        int const icon = (size - arc_w) * ORB_ICON_RATIO_NUM / ORB_ICON_RATIO_DEN;
        if( icon > 0 && (globe->skill + 1) * cell <= g_skills_w )
            orb_blit_scaled(
                g_scratch, side, side, offset + (size - icon) / 2,
                offset + (size - icon) / 2, icon, icon, g_skills_px, g_skills_w,
                globe->skill * cell, 0, cell, cell);
    }

    /* 3. hovered: the wash, and the percentage the reference prints on it. */
    if( hovered )
    {
        orb_draw_ring(g_scratch, side, side, cx, cy, 0, radius, 1024, ORB_HOVER_ARGB);
        if( progress < 1000 )
        {
            char label[8];
            snprintf(label, sizeof(label), "%d%%", progress / 10);
            orb_text(
                g_scratch,
                side,
                side,
                (side - orb_text_width(label)) / 2,
                (side - g_glyph_line_h) / 2,
                label,
                ORB_TEXT_WHITE);
        }
    }

    /* 4. the full ring, then the progress arc over it. */
    orb_draw_ring(
        g_scratch, side, side, cx, cy, radius - ORB_RING_WIDTH * 128,
        radius + ORB_RING_WIDTH * 128, 1024, outline);
    if( progress > 0 )
        orb_draw_ring(
            g_scratch, side, side, cx, cy, radius - arc_w * 128, radius + arc_w * 128,
            progress * 1024 / 1000, arc);

    snprintf(name, sizeof(name), "globe%d.png", slot);
    return g_api->image_compose(ctx, name, side, side, g_scratch);
}

/* ------------------------------------------------------------- the tooltip */

/** One tooltip row: a label on the left in orange, a value on the right in
 *  white -- the reference's LineComponent, which is the whole panel. */
struct OrbTipRow
{
    char left[32];
    char right[32];
    int left_row;
};

/**
 * The tooltip for `globe`, composed and blitted at (x, y).
 *
 * Composed rather than drawn as rects and glyph blits for the reason the globe
 * is: eight lines of two strings is well over a hundred draw items, and the
 * whole panel is one blit this way.
 */
static void
orb_draw_tooltip(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    struct XpGlobe const* globe,
    int goal_xp,
    int canvas_w,
    int canvas_h,
    int mouse_x,
    int mouse_y)
{
    struct OrbTipRow row[8];
    int rows = 0;
    int height;
    int x;
    int y;
    int image;

    assert(globe);

    {
        char const* const name = g_api->skill_name(ctx, globe->skill);
        snprintf(row[rows].left, sizeof(row[rows].left), "%s", name ? name : "?");
        snprintf(row[rows].right, sizeof(row[rows].right), "%d", globe->level);
        row[rows].left_row = ORB_TEXT_WHITE;
        rows++;
    }
    snprintf(row[rows].left, sizeof(row[rows].left), "Current XP:");
    orb_commas(row[rows].right, sizeof(row[rows].right), globe->xp);
    row[rows].left_row = ORB_TEXT_LABEL;
    rows++;

    if( goal_xp > globe->xp )
    {
        struct XpTrack const* track = &g_track[globe->skill];
        int const xp_left = goal_xp - globe->xp;
        int const gained = track->start_xp >= 0 ? globe->xp - track->start_xp : 0;
        uint64_t const elapsed =
            track->start_xp >= 0 ? g_api->frame_ms(ctx) - track->start_ms : 0;
        /* Per hour, from the session's first gain. Under a second of elapsed
         * time the rate is noise, so it is not offered rather than offered as
         * a number in the millions. */
        int const per_hour = elapsed >= 1000 && gained > 0
                                 ? (int)((uint64_t)gained * 3600000u / elapsed)
                                 : 0;

        if( g_api->cfg_bool(ctx, "show_actions_left") && track->actions > 0 &&
            gained > 0 )
        {
            int const per_action = gained / track->actions;
            if( per_action > 0 )
            {
                snprintf(row[rows].left, sizeof(row[rows].left), "Actions left:");
                orb_commas(
                    row[rows].right,
                    sizeof(row[rows].right),
                    (xp_left + per_action - 1) / per_action);
                row[rows].left_row = ORB_TEXT_LABEL;
                rows++;
            }
        }
        if( g_api->cfg_bool(ctx, "show_xp_left") )
        {
            snprintf(row[rows].left, sizeof(row[rows].left), "XP left:");
            orb_commas(row[rows].right, sizeof(row[rows].right), xp_left);
            row[rows].left_row = ORB_TEXT_LABEL;
            rows++;
        }
        if( g_api->cfg_bool(ctx, "show_xp_hour") && per_hour > 0 )
        {
            snprintf(row[rows].left, sizeof(row[rows].left), "XP per hour:");
            orb_commas(row[rows].right, sizeof(row[rows].right), per_hour);
            row[rows].left_row = ORB_TEXT_LABEL;
            rows++;
        }
        if( g_api->cfg_bool(ctx, "show_time_to_goal") && per_hour > 0 )
        {
            snprintf(row[rows].left, sizeof(row[rows].left), "Time left:");
            orb_duration(
                row[rows].right,
                sizeof(row[rows].right),
                (int)((int64_t)xp_left * 3600 / per_hour));
            row[rows].left_row = ORB_TEXT_LABEL;
            rows++;
        }
    }

    height = ORB_TIP_BORDER * 2 + rows * g_glyph_line_h;
    assert(ORB_TIP_W <= ORB_SCRATCH_W);
    if( height > ORB_SCRATCH_H )
        return;

    /* The whole panel is the plate, so it is written rather than cleared and
     * then covered. */
    for( int i = 0; i < ORB_TIP_W * height; i++ )
        g_scratch[i] = ORB_TIP_ARGB;
    for( int i = 0; i < rows; i++ )
    {
        int const top = ORB_TIP_BORDER + i * g_glyph_line_h;
        orb_text(
            g_scratch, ORB_TIP_W, height, ORB_TIP_BORDER, top, row[i].left,
            row[i].left_row);
        orb_text(
            g_scratch,
            ORB_TIP_W,
            height,
            ORB_TIP_W - ORB_TIP_BORDER - orb_text_width(row[i].right),
            top,
            row[i].right,
            ORB_TEXT_WHITE);
    }

    image = g_api->image_compose(ctx, "tooltip.png", ORB_TIP_W, height, g_scratch);
    if( image < 0 )
        return;

    /* Below and right of the pointer, as every tooltip in the client is, and
     * folded back inside the canvas rather than clipped off the edge of it. */
    x = mouse_x + 10;
    y = mouse_y + 20;
    if( x + ORB_TIP_W > canvas_w )
        x = canvas_w - ORB_TIP_W;
    if( y + height > canvas_h )
        y = mouse_y - height - 5;
    g_api->draw_image(
        ctx, surface, image, orb_clampi(x, 0, canvas_w), orb_clampi(y, 0, canvas_h), 0,
        0, 0, 0, 0);
}

/* ---------------------------------------------------------------- the draw */

/** Ask for the art, and read back the pixels once they land. */
static void
orb_load_art(struct ToriRS_PluginCtx* ctx)
{
    if( g_img_skills < 0 )
        g_img_skills = g_api->image_load(ctx, "skills.png");
    if( g_img_text < 0 )
        g_img_text = g_api->image_load(ctx, "text.png");
    orb_load_glyphs(ctx);

    if( !g_skills_px && g_img_skills >= 0 &&
        g_api->image_size(ctx, g_img_skills, &g_skills_w, &g_skills_h) )
    {
        int const pixels = g_skills_w * g_skills_h;
        g_skills_px = malloc((size_t)pixels * sizeof(uint32_t));
        assert(g_skills_px);
        if( g_api->image_pixels(ctx, g_img_skills, g_skills_px, pixels) != pixels )
        {
            free(g_skills_px);
            g_skills_px = NULL;
        }
    }
    if( !g_text_px && g_img_text >= 0 &&
        g_api->image_size(ctx, g_img_text, &g_text_w, &g_text_h) )
    {
        int const pixels = g_text_w * g_text_h;
        g_text_px = malloc((size_t)pixels * sizeof(uint32_t));
        assert(g_text_px);
        if( g_api->image_pixels(ctx, g_img_text, g_text_px, pixels) != pixels )
        {
            free(g_text_px);
            g_text_px = NULL;
        }
    }
}

static enum ToriRS_PluginVerdict
orb_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDrawCanvas* ev = (struct ToriRS_PluginEvDrawCanvas*)event;
    uint64_t const now = g_api->frame_ms(ctx);
    int const vertical = g_api->cfg_bool(ctx, "vertical");
    int const size = orb_size(ctx);
    int const offset = orb_arc_offset(ctx);
    int const side = size + 2 * offset;
    int const duration_ms = orb_clampi(g_api->cfg_int(ctx, "orb_duration"), 1, 600) * 1000;
    int mouse_x = -1;
    int mouse_y = -1;
    int origin_x;
    int origin_y;
    int hovered_slot = -1;
    int hovered_goal = 0;

    assert(ctx);
    assert(ev);

    orb_size_tables(ctx);
    orb_load_art(ctx);
    if( !g_skills_px )
        return TORIRS_PLUGIN_PASS;

    /* Expiry first, so a globe that timed out this frame is not laid out and
     * then skipped. A HOVERED one does not expire -- the reference resets its
     * timer while the pointer is on it, because a tooltip that vanishes while
     * being read is worse than one that overstays. */
    for( int i = g_globe_count - 1; i >= 0; i-- )
        if( now - g_globe[i].at_ms > (uint64_t)duration_ms )
            orb_remove(ctx, i);
    if( g_globe_count == 0 )
        return TORIRS_PLUGIN_PASS;

    if( !g_api->mouse_pos(ctx, &mouse_x, &mouse_y) )
        mouse_x = -1;

    /*
     * Top centre of the canvas, as the reference's OverlayPosition.TOP_CENTER,
     * shifted by the config.
     *
     * Offsets and not a drag, because this client has no draggable overlay
     * layer to hang one off -- and the column is centred on the WHOLE run of
     * globes so it stays centred as they come and go, rather than growing off
     * to one side.
     */
    {
        /* The pitch is the reference's -- one ORB plus MINIMUM_STEP, so the gap
         * between two discs is 10 whatever the rings around them are doing. The
         * buffer is wider than the orb by the ring's overhang, and it is the
         * DISC that is laid out; the buffer is blitted back by that overhang.
         */
        int const run = g_globe_count * size + (g_globe_count - 1) * ORB_STEP;
        origin_x = vertical ? (ev->width - size) / 2 : (ev->width - run) / 2;
        origin_y = offset;
        origin_x += g_api->cfg_int(ctx, "offset_x");
        origin_y += g_api->cfg_int(ctx, "offset_y");
    }

    for( int i = 0; i < g_globe_count; i++ )
    {
        struct XpGlobe* globe = &g_globe[i];
        /* Where the DISC goes; the buffer's own origin is `offset` above and
         * left of it, which is where the ring's overhang lives. */
        int const x = origin_x + (vertical ? 0 : i * (size + ORB_STEP));
        int const y = origin_y + (vertical ? i * (size + ORB_STEP) : 0);
        int level_xp = 0;
        int next_xp = 0;
        int progress;
        int hovered;
        uint64_t key;
        char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX] = { 0 };

        g_api->stat_xp(ctx, globe->skill, NULL, &level_xp, &next_xp);
        /* Thousandths, not a percent: an arc a fifth of a degree out is
         * visible, and a percent quantises a 40px ring into steps a player can
         * see stepping. `next_xp == 0` is the top of the client's table -- a
         * maxed skill, whose ring is simply full. */
        if( next_xp > level_xp )
            progress = orb_clampi(
                (int)((int64_t)(globe->xp - level_xp) * 1000 / (next_xp - level_xp)), 0,
                1000);
        else
            progress = 1000;

        /* The pointer is tested against the DISC, not the buffer: the corners
         * of the box are empty, and a tooltip that opens from a gap between
         * two globes reads as the wrong one having answered. */
        hovered = 0;
        if( mouse_x >= 0 )
        {
            int const dx = mouse_x - (x + size / 2);
            int const dy = mouse_y - (y + size / 2);
            hovered = dx * dx + dy * dy <= (size / 2) * (size / 2);
        }
        if( hovered )
        {
            globe->at_ms = now;
            hovered_slot = i;
            hovered_goal = next_xp > level_xp ? next_xp : 0;
        }

        key = orb_key(ctx, globe, progress, hovered);
        if( globe->image < 0 || key != globe->key )
        {
            globe->image = orb_compose(ctx, globe, i, progress, hovered);
            globe->key = key;
        }
        if( globe->image < 0 )
            continue;

        /*
         * The globe claims its own box before it is drawn, and offers the
         * reference's one verb on this overlay -- Flip, which turns the column
         * from a row into a column and back.
         *
         * Claimed for the other reason a region is, too: these sit over the
         * world viewport, and without this a click on one walks the player to
         * whatever tile is behind it.
         */
        ops[0] = "Flip";
        g_api->hit_region(
            ctx, ev->surface, x - offset, y - offset, side, side, ops, 1, ORB_TAG_FLIP);
        g_api->draw_image(
            ctx, ev->surface, globe->image, x - offset, y - offset, 0, 0, 0, 0, 0);
    }

    if( hovered_slot >= 0 && g_api->cfg_bool(ctx, "enable_tooltips") && g_glyph_ready )
        orb_draw_tooltip(
            ctx, ev->surface, &g_globe[hovered_slot], hovered_goal, ev->width,
            ev->height, mouse_x, mouse_y);

    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orb_click(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvCanvasClick* ev = (struct ToriRS_PluginEvCanvasClick*)event;

    assert(ctx);
    assert(ev);

    if( ev->tag != ORB_TAG_FLIP )
        return TORIRS_PLUGIN_PASS;
    g_api->cfg_set(ctx, "vertical", g_api->cfg_bool(ctx, "vertical") ? "0" : "1");
    return TORIRS_PLUGIN_PASS;
}

/* --------------------------------------------------------------- lifecycle */

static enum ToriRS_PluginVerdict
orb_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    /* Every handle the host held for this plugin was dropped when it stopped,
     * so forgetting them here is what makes the next start ask afresh instead
     * of drawing with numbers the host has since handed to someone else. */
    g_img_skills = -1;
    g_img_text = -1;
    free(g_skills_px);
    g_skills_px = NULL;
    free(g_text_px);
    g_text_px = NULL;
    g_glyph_ready = 0;
    for( int i = 0; i < ORB_MAX_SHOWN; i++ )
    {
        g_globe[i].image = -1;
        g_globe[i].key = 0;
    }
    orb_size_tables(ctx);
    orb_reset(ctx);
    orb_load_art(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
orb_stop(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;

    g_img_skills = -1;
    g_img_text = -1;
    free(g_skills_px);
    g_skills_px = NULL;
    free(g_text_px);
    g_text_px = NULL;
    g_glyph_ready = 0;
    g_globe_count = 0;
    for( int i = 0; i < ORB_MAX_SHOWN; i++ )
    {
        g_globe[i].image = -1;
        g_globe[i].key = 0;
    }
    return TORIRS_PLUGIN_PASS;
}

static void
orb_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, orb_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, orb_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SERVER_TICK, orb_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, orb_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CANVAS_CLICK, orb_click, NULL);
}

static void
orb_shutdown(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    free(g_skills_px);
    g_skills_px = NULL;
    free(g_text_px);
    g_text_px = NULL;
    free(g_seen_xp);
    g_seen_xp = NULL;
    free(g_track);
    g_track = NULL;
    g_skill_count = 0;
}

/*
 * The reference's own config, key for key, with two additions it does not need
 * and this does.
 *
 * `background_alpha` is one: RuneLite's colour pickers carry an alpha channel
 * and this client's carry "#RRGGBB", so the transparency of the disc -- 127 in
 * the reference, and the whole reason a globe does not black out the scene
 * behind it -- has to be a key of its own.
 *
 * The offsets are the other. The reference's overlays are dragged; this
 * client's plugin surface has no drag, so where the column sits is stated
 * rather than pointed at. Zero is the reference's own TOP_CENTER.
 */
static struct ToriRS_PluginConfigItem const ORB_CONFIG[] = {
    { "enable_tooltips",   TORIRS_PLUGIN_CFG_BOOL,  "Enable tooltips",              "1", 0, 0, NULL, 0 },
    { "show_xp_left",      TORIRS_PLUGIN_CFG_BOOL,  "Show XP left",                 "1", 0, 0, NULL, 0 },
    { "show_actions_left", TORIRS_PLUGIN_CFG_BOOL,  "Show actions left",            "1", 0, 0, NULL, 0 },
    { "show_xp_hour",      TORIRS_PLUGIN_CFG_BOOL,  "Show XP/hr",                   "1", 0, 0, NULL, 0 },
    { "show_time_to_goal", TORIRS_PLUGIN_CFG_BOOL,  "Show time til goal",           "1", 0, 0, NULL, 0 },
    { "hide_maxed",        TORIRS_PLUGIN_CFG_BOOL,  "Hide maxed skills",            "0", 0, 0, NULL, 0 },
    { "show_virtual_level", TORIRS_PLUGIN_CFG_BOOL, "Show virtual level",           "0", 0, 0, NULL, 0 },
    { "custom_arc_color",  TORIRS_PLUGIN_CFG_BOOL,  "Enable custom arc colour",     "0", 0, 0, NULL, 0 },
    { "arc_color",         TORIRS_PLUGIN_CFG_COLOR, "Progress arc colour",          "#FFC800", 0, 0, NULL, 0 },
    { "outline_color",     TORIRS_PLUGIN_CFG_COLOR, "Progress orb outline colour",  "#000000", 0, 0, NULL, 0 },
    { "background_color",  TORIRS_PLUGIN_CFG_COLOR, "Progress orb background colour", "#808080", 0, 0, NULL, 0 },
    { "background_alpha",  TORIRS_PLUGIN_CFG_INT,   "Orb background opacity",       "127", 0, 255, NULL, 0 },
    { "arc_width",         TORIRS_PLUGIN_CFG_INT,   "Progress arc width",           "2", 1, 12, NULL, 0 },
    { "orb_size",          TORIRS_PLUGIN_CFG_INT,   "Size of orbs",                 "40", 16, ORB_SIZE_MAX, NULL, 0 },
    { "orb_duration",      TORIRS_PLUGIN_CFG_INT,   "Duration of orbs (seconds)",   "10", 1, 600, NULL, 0 },
    { "vertical",          TORIRS_PLUGIN_CFG_BOOL,  "Vertical orbs",                "0", 0, 0, NULL, 0 },
    { "offset_x",          TORIRS_PLUGIN_CFG_INT,   "Offset from top centre, across", "0", -2048, 2048, NULL, 0 },
    { "offset_y",          TORIRS_PLUGIN_CFG_INT,   "Offset from top centre, down", "0", -2048, 2048, NULL, 0 },
    { NULL,                TORIRS_PLUGIN_CFG_BOOL,  NULL,                           NULL, 0, 0, NULL, 0 },
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_ORBS = {
    .name = "xp-drop-orbs",
    .title = "XP Drop Orbs",
    .version = "1.0.0",
    .priority = 0,
    .config = ORB_CONFIG,
    /* OFF until asked for, as the reference is (`enabledByDefault = false`):
     * a client that starts marking up the screen without being asked reads as
     * broken, and this one draws over the top of the world viewport. */
    .disabled_by_default = true,
    .init = orb_init,
    .shutdown = orb_shutdown,
};
