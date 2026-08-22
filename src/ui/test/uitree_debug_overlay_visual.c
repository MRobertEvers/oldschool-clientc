/*
 * Visual (raster) tests for the debug overlay — one scene per feature.
 *
 * These go all the way down: ToriRSChrome_Build produces the display list, the
 * list travels as one UITREE_EMIT_DEBUG_OVERLAY desc exactly as the emit pass
 * builds it, ToriRS_Frame expands it one primitive per multi-step, and Soft3D
 * rasterises with the *baked* fonts. So a break anywhere in that chain — a
 * dropped primitive, a swapped font slot, a baseline off by the ascent, a
 * scissor box that clips the wrong side — shows up here and nowhere else.
 *
 * Every scene writes a BMP to build/ so it can be eyeballed, and every scene
 * also asserts on the pixels. The BMP is the artefact; the assertions are the
 * test. Assertions are written against the theme colours and the module's own
 * reported geometry (ToriRSChrome_PanelRect, the widget boxes), never against
 * hardcoded pixel coordinates, so re-baking the fonts at a different size
 * moves the picture without breaking the test.
 *
 * TORIRSRC_FONT_LOAD is a no-op in Soft3D, so the two baked faces are handed
 * to the scene directly with ToriDraw_SceneFontAdd. The scene ids below are
 * local registration handles the host picks, not cache ids: the overlay only
 * ever names a *slot*, and the desc carries whatever mapping the host chose.
 *
 * Run: make -C src test-debug-overlay-visual
 */

#include "ui/torirs_chrome_metrics.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_emit.h"

#include "engine/torirs_chrome_skin_baked.h"
#include "engine/torirs_debug_font_baked.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"

#include "toridraw_hsl16.h"
#include "toridraw_scene.h"
#include "world/world_pickset.h"

#include "bmp.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The world model, shimmed away.
 *
 * Everything under src/render/ here is the real thing — torirs_frame.c and
 * torirs_pick.c are linked as-is, because they are what this test is checking.
 * The world *model* is not: torirs_frame reaches these only on the
 * UITREE_EMIT_WORLD path and torirs_pick only when a frame armed a hittest,
 * and an overlay-only emit buffer does neither. Linking world.c to close the
 * link would drag painters, the collision map, the heightmap and the minimap
 * into a test that draws rectangles and text.
 *
 * The boundary is deliberate and total: no world model in this test. If any of
 * these ever fires, the test is walking a path it does not mean to.
 */
struct World;
struct WorldEntity_Scenery;
struct WorldEntity_NPC;
struct WorldEntity_Player;
struct WorldEntity_ObjStack;

int
World_TerrainElementAt(struct World* world, int x, int z, int level)
{
    (void)world;
    (void)x;
    (void)z;
    (void)level;
    return -1;
}

struct WorldEntity_Scenery*
World_SceneryGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

struct WorldEntity_NPC*
World_NpcGetByElementId(struct World* world, int element_id, int* out_index)
{
    (void)world;
    (void)element_id;
    (void)out_index;
    return NULL;
}

struct WorldEntity_Player*
World_PlayerGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

struct WorldEntity_ObjStack*
World_ObjStackGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

bool
WorldEntity_SceneryPickInactive(void)
{
    return false;
}

int
World_LocPaintLevel(struct World const* world, int x, int z, int cache_level)
{
    (void)world;
    (void)x;
    (void)z;
    return cache_level;
}

int
World_TerrainDrawLevel(struct World const* world, int x, int z, int mesh_level)
{
    (void)world;
    (void)x;
    (void)z;
    return mesh_level;
}

void
World_PickSetReset(struct World_PickSet* pickset)
{
    (void)pickset;
}

void
World_PickSetAdd(
    struct World_PickSet* pickset,
    int element_id,
    enum World_PickType type,
    int tile_x,
    int tile_z,
    int tile_level)
{
    (void)pickset;
    (void)element_id;
    (void)type;
    (void)tile_x;
    (void)tile_z;
    (void)tile_level;
}

/* ------------------------------------------------------------------------- */

#define CANVAS_W 400
#define CANVAS_H 320

/** Slot -> scene font id at 1x. Arbitrary local handles; see the file comment. */
#define FONT_ID_SMALL 0
#define FONT_ID_MENU 1
#define FONT_ID_BODY 2
/** The same three at scale N: one block of handles per scale, as the real
 *  bridge does with UITREE_SCENE_DEBUG_FONT_SCALED_ID. */
#define FONT_ID_AT(slot_id, scale) ((slot_id) + ((scale) - 1) * 16)
/** Local scene handle for the baked chrome skin atlas; see the file comment. */
#define SKIN_SCENE_ID 7

/** Soft3D clears to TORIRS_SOFT3D_BG; this is that colour without alpha. */
#define BG_RGB (TORIRS_SOFT3D_BG & 0xFFFFFF)

static int g_failures;
static struct ToriRSChrome g_ui;
static struct ToriDraw_Scene* g_scene;
static int g_pixels[CANVAS_W * CANVAS_H];

#define VT_ASSERT(cond, msg)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** Colour at (x,y), alpha dropped. Off-canvas reads are a test bug, not a 0. */
static uint32_t
px(int x, int y)
{
    if( x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H )
    {
        fprintf(stderr, "FAIL: px(%d,%d) off canvas (%s:%d)\n", x, y, __FILE__, __LINE__);
        g_failures++;
        return 0;
    }
    return (uint32_t)g_pixels[y * CANVAS_W + x] & 0xFFFFFFu;
}

/** Pixels inside the box that are not `color`. */
static int
count_not(int x, int y, int w, int h, uint32_t color)
{
    int n = 0;
    for( int j = y; j < y + h; j++ )
        for( int i = x; i < x + w; i++ )
            if( px(i, j) != color )
                n++;
    return n;
}

/** Pixels inside the box that are exactly `color`. */
static int
count_eq(int x, int y, int w, int h, uint32_t color)
{
    int n = 0;
    for( int j = y; j < y + h; j++ )
        for( int i = x; i < x + w; i++ )
            if( px(i, j) == color )
                n++;
    return n;
}

/**
 * Render the overlay's current display list through the real emit desc and the
 * real frame translator, then write build/debug_overlay_<name>.bmp.
 *
 * The desc is built exactly as emit_debug_overlay_pass builds it — one desc for
 * the whole list, carrying the prims by pointer and the slot -> font mapping —
 * so this covers translate_ui_cmd's DEBUG_OVERLAY case for real.
 */
static void
render(char const* name)
{
    struct UITreeEmitDesc desc;
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;
    char path[128];
    int count = 0;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_DEBUG_OVERLAY;
    desc.debug_prims = ToriRSChrome_Prims(&g_ui, &count);
    desc.debug_prim_count = count;
    /* Slot -> font id follows the chrome's SCALE, exactly as the real bridge
     * resolves it. Drawing a 2x layout with the 1x faces is the specific bug
     * this mapping exists to make impossible, and it would still render -- as
     * small text rattling around in big boxes. */
    desc.debug_font_id[TORIRS_CHROME_FONT_SMALL] = FONT_ID_AT(FONT_ID_SMALL, g_ui.scale);
    desc.debug_font_id[TORIRS_CHROME_FONT_MENU] = FONT_ID_AT(FONT_ID_MENU, g_ui.scale);
    desc.debug_font_id[TORIRS_CHROME_FONT_BODY] = FONT_ID_AT(FONT_ID_BODY, g_ui.scale);
    desc.debug_skin_scene_id = SKIN_SCENE_ID;
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = i;
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = CANVAS_W;
    desc.clip.h = CANVAS_H;

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, g_scene);
    ToriRS_FrameSetCanvas(&frame, CANVAS_W, CANVAS_H);
    ToriRS_FrameSetEmit(&frame, &desc, 1);

    ToriRS_Soft3D_Init(&soft, g_scene, g_pixels, CANVAS_W, CANVAS_H);
    ToriRS_Soft3D_RenderFrame(&soft, &frame);

    snprintf(path, sizeof(path), "build/debug_overlay_%s.bmp", name);
    bmp_write_file(path, g_pixels, CANVAS_W, CANVAS_H);
    printf("  wrote %s (%d prims)\n", path, count);
}

/** Paint a 1px outline straight into the framebuffer, after rendering. Used to
 *  mark the damage rect — it is metadata about the frame, not part of it. */
static void
mark_rect(struct ToriRSChromeRect r, uint32_t color)
{
    for( int i = r.x; i < r.x + r.w; i++ )
    {
        if( i < 0 || i >= CANVAS_W )
            continue;
        if( r.y >= 0 && r.y < CANVAS_H )
            g_pixels[r.y * CANVAS_W + i] = (int)color;
        if( r.y + r.h - 1 >= 0 && r.y + r.h - 1 < CANVAS_H )
            g_pixels[(r.y + r.h - 1) * CANVAS_W + i] = (int)color;
    }
    for( int j = r.y; j < r.y + r.h; j++ )
    {
        if( j < 0 || j >= CANVAS_H )
            continue;
        if( r.x >= 0 && r.x < CANVAS_W )
            g_pixels[j * CANVAS_W + r.x] = (int)color;
        if( r.x + r.w - 1 >= 0 && r.x + r.w - 1 < CANVAS_W )
            g_pixels[j * CANVAS_W + r.x + r.w - 1] = (int)color;
    }
}

/* ---- 1. bordered backgrounds --------------------------------------------- */

/*
 * The bordered background is the whole reason TORIRSRC_FILL_RECT carries a
 * `filled` flag: body and border are two primitives over the same box, the
 * second with filled == 0, which reaches ToriDraw2D_DrawRectOutline. This
 * checks the border really is one pixel and really is on top of the body.
 */
static void
visual_bordered_background(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeRect r;
    int panel;

    printf("VISUAL: bordered background\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 24, 20, 0, "Renderer");
    ToriRSChrome_Label(&g_ui, panel, "backend  soft3d");
    ToriRSChrome_Label(&g_ui, panel, "canvas   400x260");
    ToriRSChrome_Separator(&g_ui, panel);
    ToriRSChrome_LabelColored(&g_ui, panel, "vsync    off", t->accent);
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("01_bordered_background");

    VT_ASSERT(r.w > 0 && r.h > 0, "panel resolved a box");
    /* The outermost ring is BODY, not an outline: a window panel wears the
     * minimenu's chrome, and the minimenu has no outer edge -- its body meets
     * whatever it floats over. The black is one pixel in. */
    VT_ASSERT(px(r.x, r.y + r.h / 2) == t->panel_body, "outer edge is body, not an outline");
    VT_ASSERT(px(r.x + r.w / 2, r.y + r.h - 1) == t->panel_body, "bottom edge is body");
    VT_ASSERT(
        px(r.x + r.w / 2, r.y + r.h - 2) == t->panel_border, "bottom rule is one pixel in");
    VT_ASSERT(px(r.x + 1, r.y + r.h - 4) == t->panel_border, "left rail sits inside the edge");
    VT_ASSERT(
        px(r.x + r.w - 2, r.y + r.h - 4) == t->panel_border, "right rail sits inside the edge");
    VT_ASSERT(
        px(r.x + 2, r.y + r.h - 4) != t->panel_border, "body resumes inside the rail");
    /* Outside the box the canvas is untouched: the panel did not bleed. */
    VT_ASSERT(px(r.x - 1, r.y + r.h / 2) == BG_RGB, "nothing drawn left of the panel");
    VT_ASSERT(px(r.x + r.w, r.y + r.h / 2) == BG_RGB, "nothing drawn right of the panel");
    /* The title bar is its own fill above the body. */
    VT_ASSERT(px(r.x + 1, r.y + 1) == t->panel_title_bg, "title bar fills the top strip");
    /* Text rasterised somewhere inside: the interior is not a flat fill. */
    VT_ASSERT(
        count_not(r.x + 1, r.y + 1, r.w - 2, r.h - 2, t->panel_body) > 0,
        "something is drawn over the body");
}

/* ---- 2. menu-like interfaces --------------------------------------------- */

/*
 * The menu face has to match the minimenu's chrome, and its rows are drawn
 * with a drop shadow so they read over the body fill. The hovered row goes
 * accent-coloured — that is the one piece of per-frame state a debug menu has,
 * so it is worth seeing the two colours side by side in one BMP.
 */
static void
visual_menu(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeRect r;
    int panel;
    int rows[4];
    int hovered_band;
    int plain_band;

    printf("VISUAL: menu-like interface\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_MENU, 40, 30, 0, "Choose Option");
    rows[0] = ToriRSChrome_MenuItem(&g_ui, panel, "Toggle wireframe");
    rows[1] = ToriRSChrome_MenuItem(&g_ui, panel, "Dump emit buffer");
    rows[2] = ToriRSChrome_MenuItem(&g_ui, panel, "Reload interface");
    rows[3] = ToriRSChrome_MenuItem(&g_ui, panel, "Cancel");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);

    /* Hover the second row, the way the pointer would. */
    ToriRSChrome_MouseMove(
        &g_ui,
        g_ui.widgets[rows[1]].x + 2,
        g_ui.widgets[rows[1]].y + g_ui.widgets[rows[1]].h / 2);
    ToriRSChrome_Build(&g_ui);
    render("02_menu");

    VT_ASSERT(g_ui.hover == rows[1], "the pointer is on row 1");
    VT_ASSERT(px(r.x + 1, r.y + 1) == t->menu_chrome, "menu header bar is chrome-coloured");
    /* h - 2, not h - 1: the reference minimenu's bottom rule sits one pixel in
     * from the body's bottom edge, and the overlay copies that box for box. */
    VT_ASSERT(px(r.x + r.w / 2, r.y + r.h - 2) == t->menu_chrome, "menu bottom border strip");

    /* Row text: the hovered row must contain accent pixels and the others must
     * not. Scanned over each row's own hit box, so the bands move with the
     * font rather than being pinned to numbers.
     *
     * The "not hovered" half is checked two rows down rather than on the
     * neighbour: consecutive hit boxes overlap by a pixel (row_stride is
     * box - 1 while the band is box tall) and descenders reach past the
     * baseline, so a 'p' in the hovered row genuinely does put hover-coloured
     * pixels inside the next row's band. Row 3 is clear of both. */
    hovered_band = count_eq(
        g_ui.widgets[rows[1]].x,
        g_ui.widgets[rows[1]].y,
        g_ui.widgets[rows[1]].w,
        g_ui.widgets[rows[1]].h,
        t->menu_hover_text);
    plain_band = count_eq(
        g_ui.widgets[rows[3]].x,
        g_ui.widgets[rows[3]].y,
        g_ui.widgets[rows[3]].w,
        g_ui.widgets[rows[3]].h,
        t->menu_hover_text);
    VT_ASSERT(hovered_band > 0, "hovered row is drawn in the hover colour");
    VT_ASSERT(plain_band == 0, "a row that is not hovered is not");
    VT_ASSERT(
        count_eq(
            g_ui.widgets[rows[2]].x,
            g_ui.widgets[rows[2]].y,
            g_ui.widgets[rows[2]].w,
            g_ui.widgets[rows[2]].h,
            t->menu_text) > 0,
        "an unhovered row is drawn in the plain colour");
    /* Shadowed text puts black pixels under the glyphs. */
    VT_ASSERT(
        count_eq(
            g_ui.widgets[rows[0]].x,
            g_ui.widgets[rows[0]].y,
            g_ui.widgets[rows[0]].w,
            g_ui.widgets[rows[0]].h,
            0x000000u) > 0,
        "menu rows carry a drop shadow");
}

/* ---- 3. checkboxes -------------------------------------------------------- */

/*
 * A checkbox is a box outline plus, when set, a mark inside it. Both states
 * appear in one BMP so the difference is the thing under test.
 */
static void
visual_checkbox(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeWidget const* on;
    struct ToriRSChromeWidget const* off;
    int panel;
    int w_on;
    int w_off;

    printf("VISUAL: checkboxes\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 170, "Toggles");
    w_on = ToriRSChrome_Checkbox(&g_ui, panel, "show fps", 1);
    w_off = ToriRSChrome_Checkbox(&g_ui, panel, "show tile grid", 0);
    ToriRSChrome_Checkbox(&g_ui, panel, "freeze camera", 0);
    ToriRSChrome_Build(&g_ui);
    render("03_checkbox");

    on = &g_ui.widgets[w_on];
    off = &g_ui.widgets[w_off];
    VT_ASSERT(on->w > 0 && on->h > 0, "checkbox resolved a hit box");

    /* Both boxes are outlined... */
    VT_ASSERT(
        count_eq(on->x, on->y, on->w, on->h, t->check_box) > 0, "checked box has its outline");
    VT_ASSERT(
        count_eq(off->x, off->y, off->w, off->h, t->check_box) > 0, "unchecked box has its outline");
    /* ...only the checked one has a mark. */
    VT_ASSERT(
        count_eq(on->x, on->y, on->w, on->h, t->check_mark) > 0, "checked box draws its mark");
    VT_ASSERT(
        count_eq(off->x, off->y, off->w, off->h, t->check_mark) == 0,
        "unchecked box draws no mark");
    /* And the label is drawn beside it. */
    VT_ASSERT(count_eq(on->x, on->y, on->w, on->h, t->text) > 0, "checkbox label is drawn");

    /* Click the unchecked one: the mark appears where it was absent. */
    ToriRSChrome_MouseDown(&g_ui, off->x + 2, off->y + off->h / 2);
    ToriRSChrome_MouseUp(&g_ui, off->x + 2, off->y + off->h / 2);
    ToriRSChrome_Build(&g_ui);
    render("04_checkbox_toggled");
    VT_ASSERT(ToriRSChrome_Checked(&g_ui, w_off) == 1, "the click toggled it");
    VT_ASSERT(
        count_eq(off->x, off->y, off->w, off->h, t->check_mark) > 0,
        "the toggled box now draws its mark");
    VT_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == w_off, "the toggle latched an activation");
}

/*
 * With the skin present a checkbox is the interfaces' own 17x17 pair -- green
 * tick for on, red cross for off -- and not a box with a mark in it.
 *
 * Asserted by comparing the framebuffer against the baked array pixel for
 * pixel, because the weaker "some green appeared" would pass for any green
 * thing drawn anywhere in the box. Transparent source pixels are skipped: the
 * circle does not fill its square, and what shows through them is the panel.
 *
 * Both halves again, for the reason visual_skin gives: the flat fallback is
 * what a build with no baked skin renders, and a test that only looked at the
 * skinned half would pass with that fallback broken.
 */
/** Does this baked slot's art read green rather than red? */
static int
skin_hue_is_green(int slot)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_Get(slot);
    long greener = 0;
    long redder = 0;

    if( !spr )
        return 0;
    for( int i = 0; i < spr->w * spr->h; i++ )
    {
        uint32_t const argb = spr->argb[i];
        int const r = (int)((argb >> 16) & 0xFF);
        int const g = (int)((argb >> 8) & 0xFF);

        if( (argb >> 24) != 0xFFu )
            continue;
        if( g > r + 24 )
            greener++;
        else if( r > g + 24 )
            redder++;
    }
    return greener > redder;
}

/** Does this baked slot's art read RED -- the dismiss cross rather than a
 *  button? The mirror of skin_hue_is_green, and what tells the interfaces' own
 *  window X apart from the sprite it replaced. */
static int
skin_reads_red(int slot)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_Get(slot);
    long redder = 0;
    long rest = 0;

    if( !spr )
        return 0;
    for( int i = 0; i < spr->w * spr->h; i++ )
    {
        uint32_t const argb = spr->argb[i];
        int const r = (int)((argb >> 16) & 0xFF);
        int const g = (int)((argb >> 8) & 0xFF);
        int const b = (int)((argb)&0xFF);

        if( (argb >> 24) != 0xFFu )
            continue;
        if( r > g + 40 && r > b + 40 )
            redder++;
        else
            rest++;
    }
    return redder * 4 > rest;
}

/** Are two baked slots actually two pictures? A hover pair that baked the same
 *  image twice would satisfy every render assertion and show nothing. */
static int
skin_slots_differ(int a, int b)
{
    struct ToriRSChromeSkin_Sprite const* sa = ToriRSChromeSkin_Get(a);
    struct ToriRSChromeSkin_Sprite const* sb = ToriRSChromeSkin_Get(b);

    if( !sa || !sb )
        return 0;
    if( sa->w != sb->w || sa->h != sb->h )
        return 1;
    for( int i = 0; i < sa->w * sa->h; i++ )
        if( sa->argb[i] != sb->argb[i] )
            return 1;
    return 0;
}

/**
 * A cheap order-sensitive hash of what was rendered inside `r`.
 *
 * Compared against itself across two renders, so what it is worth is telling
 * "these pixels changed" from "these pixels did not". Deliberately not a
 * pixel-for-pixel compare against the bake: the title bar's button box is 14
 * at 1x chrome scale and the art is 16x16, so what lands on the canvas is a
 * scaled blit rather than a copy.
 */
static uint32_t
box_checksum(struct ToriRSChromeRect r)
{
    uint32_t h = 2166136261u;

    for( int y = r.y; y < r.y + r.h; y++ )
        for( int x = r.x; x < r.x + r.w; x++ )
        {
            h ^= px(x, y);
            h *= 16777619u;
        }
    return h;
}

/**
 * Is the baked slot blitted 1:1 at (bx,by)? @return matched opaque pixels, or
 * -1 at the first pixel that differs.
 *
 * skinned_check_pixels' sibling for art that is not a widget's -- the frame's
 * corners, which are panel chrome. Comparing against the BAKE rather than
 * against a colour constant is what makes the assertion survive a re-bake from
 * different art: it asks "is this the piece we baked", not "is this brown".
 */
static int
skinned_slot_pixels(int bx, int by, int slot)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_Get(slot);
    int matched = 0;

    if( !spr )
        return 0;
    for( int yy = 0; yy < spr->h; yy++ )
        for( int xx = 0; xx < spr->w; xx++ )
        {
            uint32_t const argb = spr->argb[yy * spr->w + xx];
            if( (argb >> 24) != 0xFFu )
                continue;
            if( px(bx + xx, by + yy) != (argb & 0x00FFFFFFu) )
                return -1;
            matched++;
        }
    return matched;
}

static int
skinned_check_pixels(struct ToriRSChromeWidget const* w, int slot)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_Get(slot);
    int bx;
    int by;
    int matched = 0;

    if( !spr )
        return 0;
    bx = w->x;
    by = w->y + (w->h - spr->h) / 2;
    for( int yy = 0; yy < spr->h; yy++ )
    {
        for( int xx = 0; xx < spr->w; xx++ )
        {
            uint32_t const argb = spr->argb[yy * spr->w + xx];
            if( (argb >> 24) != 0xFFu )
                continue;
            if( px(bx + xx, by + yy) != (argb & 0x00FFFFFFu) )
                return -1;
            matched++;
        }
    }
    return matched;
}

static void
visual_checkbox_skinned(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeWidget const* on;
    struct ToriRSChromeWidget const* off;
    int panel;
    int w_on;
    int w_off;

    printf("VISUAL: checkboxes, skinned\n");
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = (1u << TORIRS_CHROME_SKIN_CHECK_ON) | (1u << TORIRS_CHROME_SKIN_CHECK_OFF);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 200, "Toggles");
    w_on = ToriRSChrome_Checkbox(&g_ui, panel, "'Bank tutorial' button", 1);
    w_off = ToriRSChrome_Checkbox(&g_ui, panel, "Incinerator", 0);
    ToriRSChrome_Build(&g_ui);
    render("28_checkbox_skinned");

    on = &g_ui.widgets[w_on];
    off = &g_ui.widgets[w_off];

    VT_ASSERT(
        skinned_check_pixels(on, TORIRS_CHROME_SKIN_CHECK_ON) > 100,
        "checked box is the baked green tick, pixel for pixel");
    VT_ASSERT(
        skinned_check_pixels(off, TORIRS_CHROME_SKIN_CHECK_OFF) > 100,
        "unchecked box is the baked red cross, pixel for pixel");
    /* The flat furniture is gone, not merely covered: nothing draws it.
     * Only the MARK is asserted absent -- `check_box` is 0x000000 under this
     * theme and the sprites have a near-black ring, so "no outline colour
     * anywhere in the box" is a claim about the art, not about the code. */
    VT_ASSERT(
        count_eq(on->x, on->y, on->w, on->h, t->check_mark) == 0,
        "the skinned checkbox draws no flat mark");

    /*
     * ON is the GREEN one and OFF the RED one.
     *
     * Worth its own assertion because the pixel comparisons above cannot see
     * this: they check that slot N was blitted faithfully, and a mapping with
     * the two archives swapped satisfies them exactly as well. It was in fact
     * swapped first time round -- `graphic_8379` looks like the "on" id and is
     * the cross; `script3422` gives it op "Show", which is what you click when
     * the thing is OFF.
     */
    VT_ASSERT(
        skin_hue_is_green(TORIRS_CHROME_SKIN_CHECK_ON), "the ON slot is the green tick");
    VT_ASSERT(
        !skin_hue_is_green(TORIRS_CHROME_SKIN_CHECK_OFF), "the OFF slot is the red cross");

    /* A click still toggles, and the sprite swaps with it. */
    ToriRSChrome_MouseDown(&g_ui, off->x + 2, off->y + off->h / 2);
    ToriRSChrome_MouseUp(&g_ui, off->x + 2, off->y + off->h / 2);
    ToriRSChrome_Build(&g_ui);
    render("29_checkbox_skinned_toggled");
    VT_ASSERT(
        skinned_check_pixels(off, TORIRS_CHROME_SKIN_CHECK_ON) > 100,
        "toggling swaps the cross for the tick");
}

/*
 * The OTHER boolean: the bordered well, at its own size.
 *
 * Two claims, and the second is the one a screenshot would not settle. The art
 * has to be the boxed pair rather than the tick pair -- pixel for pixel, so a
 * slot mapping that came out one off fails here rather than in a browser. And
 * the CONTROL has to be the size of that art: the well is 18 where the tick is
 * 17, and a box left at 17 draws the well scaled, which is the speckled edge
 * the whole bake exists to avoid.
 */
/** The box the display list reserved for `slot`, or 0 when it drew none. */
static int
check_prim_side(int slot)
{
    for( int i = 0; i < g_ui.prim_count; i++ )
    {
        struct ToriRSChromePrim const* p = &g_ui.prims[i];
        if( p->kind == TORIRS_CHROME_PRIM_SPRITE && p->sprite_slot == slot )
            return p->w;
    }
    return 0;
}

static void
visual_checkbox_style_box(void)
{
    struct ToriRSChromeWidget const* on;
    struct ToriRSChromeWidget const* off;
    int panel;
    int w_on;
    int w_off;
    int tick_side;

    printf("VISUAL: checkboxes, the bordered-well style\n");
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = (1u << TORIRS_CHROME_SKIN_CHECK_ON) |
                      (1u << TORIRS_CHROME_SKIN_CHECK_OFF) |
                      (1u << TORIRS_CHROME_SKIN_CHECK_BOX_ON) |
                      (1u << TORIRS_CHROME_SKIN_CHECK_BOX_OFF);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 200, "Toggles");
    w_on = ToriRSChrome_Checkbox(&g_ui, panel, "'Bank tutorial' button", 1);
    w_off = ToriRSChrome_Checkbox(&g_ui, panel, "Incinerator", 0);
    ToriRSChrome_Build(&g_ui);
    tick_side = check_prim_side(TORIRS_CHROME_SKIN_CHECK_ON);
    VT_ASSERT(tick_side == TORIRS_CHROME_M_BOX, "the tick style reserves the tick's 17");

    ToriRSChrome_SetCheckStyle(&g_ui, TORIRS_CHROME_CHECK_STYLE_BOX);
    ToriRSChrome_Build(&g_ui);
    render("28b_checkbox_style_box");

    on = &g_ui.widgets[w_on];
    off = &g_ui.widgets[w_off];

    VT_ASSERT(
        skinned_check_pixels(on, TORIRS_CHROME_SKIN_CHECK_BOX_ON) > 100,
        "checked box is the baked tick-in-a-well, pixel for pixel");
    VT_ASSERT(
        skinned_check_pixels(off, TORIRS_CHROME_SKIN_CHECK_BOX_OFF) > 100,
        "unchecked box is the baked empty well, pixel for pixel");
    VT_ASSERT(
        skinned_check_pixels(on, TORIRS_CHROME_SKIN_CHECK_ON) != 0,
        "and it is NOT the tick pair (which would match at the same place)");
    VT_ASSERT(
        check_prim_side(TORIRS_CHROME_SKIN_CHECK_BOX_ON) == tick_side + 1,
        "and the box grew with the art: 18 where the tick was 17");

    /* Back again, because a style is a choice and not a one-way upgrade: the
     * damage-and-dirty path has to run in both directions. */
    ToriRSChrome_SetCheckStyle(&g_ui, TORIRS_CHROME_CHECK_STYLE_TICK);
    ToriRSChrome_Build(&g_ui);
    render("28c_checkbox_style_tick_again");
    VT_ASSERT(
        skinned_check_pixels(&g_ui.widgets[w_on], TORIRS_CHROME_SKIN_CHECK_ON) > 100,
        "and switching back is the tick again, at the tick's own size");
    VT_ASSERT(
        check_prim_side(TORIRS_CHROME_SKIN_CHECK_ON) == tick_side,
        "with the box back to where it was");
}

/* ---- 4. text input ------------------------------------------------------- */

/*
 * The focused input gets an accent border and, while the app says the caret
 * phase is on, a 1px caret bar after the text. Rendering both phases proves
 * the caret is a real primitive that comes and goes rather than something
 * baked into the string.
 */
static void
visual_textinput(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeWidget const* focused;
    struct ToriRSChromeWidget const* plain;
    int panel;
    int w_focus;
    int w_plain;
    int caret_on;
    int caret_off;

    printf("VISUAL: text input\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 260, "Console");
    w_focus = ToriRSChrome_TextInput(&g_ui, panel, "cmd", "");
    w_plain = ToriRSChrome_TextInput(&g_ui, panel, "arg", "1024");
    ToriRSChrome_Build(&g_ui);

    focused = &g_ui.widgets[w_focus];
    plain = &g_ui.widgets[w_plain];

    /* Focus the first field and type into it. */
    ToriRSChrome_MouseDown(&g_ui, focused->x + 2, focused->y + focused->h / 2);
    ToriRSChrome_MouseUp(&g_ui, focused->x + 2, focused->y + focused->h / 2);
    for( char const* s = "setpos 3200"; *s; s++ )
        ToriRSChrome_KeyChar(&g_ui, *s);
    ToriRSChrome_SetCaretVisible(&g_ui, 1);
    ToriRSChrome_Build(&g_ui);
    render("05_textinput_caret_on");

    VT_ASSERT(g_ui.focus == w_focus, "the click focused the field");
    VT_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, w_focus), "setpos 3200") == 0, "the typing landed");
    VT_ASSERT(
        count_eq(focused->x, focused->y, focused->w, focused->h, t->input_border_focus) > 0,
        "focused field is outlined in the focus colour");
    VT_ASSERT(
        count_eq(plain->x, plain->y, plain->w, plain->h, t->input_border_focus) == 0,
        "an unfocused field is not");
    /*
     * A field wears the settings page's own frame, not a plain outline: an
     * 0x0e0e0c edge with an 0x474745 inset one pixel inside it, which is what
     * script_3850 draws around both a dropdown button and a text input. Both
     * are checked, because either one alone would still pass with the box
     * drawn as a single-colour rectangle.
     */
    VT_ASSERT(
        count_eq(plain->x, plain->y, plain->w, plain->h, t->dropdown_border) > 0,
        "an unfocused field has the reference's outer frame");
    VT_ASSERT(
        count_eq(plain->x, plain->y, plain->w, plain->h, t->dropdown_border_inner) > 0,
        "and the inset one pixel inside it");
    VT_ASSERT(
        count_eq(plain->x, plain->y, plain->w, plain->h, t->input_text) > 0,
        "field contents are drawn");
    /* The caret is a 1px bar in the text colour, so the text colour's pixel
     * count is what moves between the two blink phases — the border colour
     * does not, which is exactly the point of checking both. */
    caret_on = count_eq(focused->x, focused->y, focused->w, focused->h, t->input_text);

    /* Blink off. Only the caret goes; the border and the text must stay. */
    ToriRSChrome_SetCaretVisible(&g_ui, 0);
    ToriRSChrome_Build(&g_ui);
    render("06_textinput_caret_off");
    caret_off = count_eq(focused->x, focused->y, focused->w, focused->h, t->input_text);
    VT_ASSERT(caret_off < caret_on, "the caret disappears on the off phase");
    VT_ASSERT(caret_off > 0, "the field contents survive the off phase");
    VT_ASSERT(
        count_eq(focused->x, focused->y, focused->w, focused->h, t->input_border_focus) > 0,
        "the focus border survives the off phase");
}

/*
 * The multiline field, as the ground-items settings page uses two of them.
 *
 * Three things only pixels can settle: the body is the reference's flat
 * 0x372e22 rather than the black a one-line field wears (a box this size at
 * input_bg reads as a hole cut in the panel), the value is broken across
 * SEVERAL lines instead of running off the right edge, and the caption sits
 * above the box rather than in the label column.
 */
static void
visual_textarea(void)
{
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeWidget const* area;
    struct ToriRSChromeWidget const* quiet;
    int panel;
    int w_area;
    int w_quiet;

    printf("VISUAL: multiline text input\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 30, 240, "Ground Items");
    ToriRSChrome_Checkbox(&g_ui, panel, "Ground Items Overlay", 1);
    w_area = ToriRSChrome_TextArea(
        &g_ui,
        panel,
        "Highlighted items",
        "Abyssal whip, Dragon bones, Rune platebody, Twisted bow",
        4);
    w_quiet = ToriRSChrome_TextArea(
        &g_ui, panel, "Filtered items", "Vial, Ashes, Coins, Bones, Bucket, Jug, Seaweed", 4);
    ToriRSChrome_Build(&g_ui);

    area = &g_ui.widgets[w_area];
    quiet = &g_ui.widgets[w_quiet];
    ToriRSChrome_MouseDown(&g_ui, area->x + 4, area->y + area->h - 4);
    ToriRSChrome_MouseUp(&g_ui, area->x + 4, area->y + area->h - 4);
    ToriRSChrome_SetCaretVisible(&g_ui, 1);
    ToriRSChrome_Build(&g_ui);
    render("05b_textarea");

    VT_ASSERT(g_ui.focus == w_area, "clicking the box focuses it");
    VT_ASSERT(
        count_eq(area->x, area->y, area->w, area->h, t->textarea_bg) > 0,
        "the box wears the reference's own body colour, not a one-line field's");
    VT_ASSERT(
        count_eq(area->x, area->y, area->w, area->h, t->dropdown_border) > 0,
        "inside the settings frame");
    VT_ASSERT(
        count_eq(area->x, area->y, area->w, area->h, t->input_border_focus) > 0,
        "and the focus ring says which box has the keyboard");

    /*
     * The value is on more than one LINE, checked band by band: a field that
     * did not wrap would draw everything in the first line's band and clip the
     * rest at the box edge, which is exactly what a one-line field does and
     * exactly what this control exists not to do.
     */
    {
        /*
         * Measured on the UNFOCUSED box, and that is not incidental: the caret
         * is a bar in the same colour as the text, a whole line box tall, and
         * on the focused field it lights the second band on its own -- so this
         * assertion passed there even with every line drawn on top of the
         * first. The quiet box has no caret and nothing else white in it.
         */
        int const line_h = ToriRSChrome_FontLineBox(g_ui.theme.font_row, g_ui.scale);
        int const first =
            quiet->y + TORIRS_CHROME_M_ROW_H + 1 + TORIRS_CHROME_M_TEXTAREA_PAD_Y;
        VT_ASSERT(
            count_eq(quiet->x, first, quiet->w, line_h, t->input_text) > 0,
            "the first line of the value is drawn");
        VT_ASSERT(
            count_eq(quiet->x, first + line_h, quiet->w, line_h, t->input_text) > 0,
            "and it wraps onto a second rather than running off the edge");
    }
}

/* ---- 5. damage rectangles ------------------------------------------------ */

/*
 * The XP-era half of the design: after a change, ToriRSChrome_Damage is the union
 * of the panel's old and new bounds — the smallest box a WM_PAINT would have
 * to repaint. The BMP marks it in magenta so it can be checked by eye that it
 * really does cover both positions and not the whole screen.
 */
static void
visual_damage(void)
{
    struct ToriRSChromeRect before;
    struct ToriRSChromeRect after;
    struct ToriRSChromeRect dmg;
    int panel;

    printf("VISUAL: damage rectangles\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 40, 40, 150, "Moves");
    ToriRSChrome_Label(&g_ui, panel, "drag me");
    ToriRSChrome_Build(&g_ui);
    before = ToriRSChrome_PanelRect(&g_ui, panel);
    ToriRSChrome_DamageClear(&g_ui);

    ToriRSChrome_PanelMove(&g_ui, panel, 150, 120);
    VT_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "a move rebuilds");
    after = ToriRSChrome_PanelRect(&g_ui, panel);
    VT_ASSERT(ToriRSChrome_Damage(&g_ui, &dmg) == 1, "a move damages");

    render("07_damage");

    /* The panel really did move: the old box is back to canvas background.
     * Checked before the marker goes down — mark_rect paints over the damage
     * outline, and (before.x, before.y) is a corner of it. */
    VT_ASSERT(px(before.x, before.y) == BG_RGB, "the old position was vacated");
    /* Only that something is there: the corner pixel is the panel's body now
     * that the chrome draws no outer outline, and pinning which colour a body
     * is would be this test doing test 01's job. */
    VT_ASSERT(px(after.x, after.y) != BG_RGB, "the new position is painted");

    mark_rect(dmg, 0xFF00FFu);
    bmp_write_file("build/debug_overlay_07_damage.bmp", g_pixels, CANVAS_W, CANVAS_H);

    /* The union covers both boxes... */
    VT_ASSERT(dmg.x <= before.x && dmg.y <= before.y, "damage covers the old top-left");
    VT_ASSERT(
        dmg.x + dmg.w >= before.x + before.w && dmg.y + dmg.h >= before.y + before.h,
        "damage covers the old bottom-right");
    VT_ASSERT(dmg.x <= after.x && dmg.y <= after.y, "damage covers the new top-left");
    VT_ASSERT(
        dmg.x + dmg.w >= after.x + after.w && dmg.y + dmg.h >= after.y + after.h,
        "damage covers the new bottom-right");
    /* ...and nothing more. A damage rect the size of the screen is no better
     * than not having one. */
    VT_ASSERT(dmg.w < CANVAS_W && dmg.h < CANVAS_H, "damage is smaller than the canvas");
}

/* ---- 6. clipping --------------------------------------------------------- */

/*
 * Every content primitive carries the panel's inner rect as its scissor box.
 * A fixed-width panel narrower than its text is the case that proves it: the
 * string must stop at the panel edge instead of running across the canvas.
 */
static void
visual_clipping(void)
{
    struct ToriRSChromeRect r;
    int panel;

    printf("VISUAL: content clipped to the panel\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 30, 40, 90, "Narrow");
    ToriRSChrome_Label(&g_ui, panel, "this label is far wider than ninety pixels");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("08_clipping");

    VT_ASSERT(r.w == 90, "fixed_w wins over the content width");
    /* Right of the panel, on the label's own line, nothing was drawn. */
    VT_ASSERT(
        count_not(r.x + r.w, r.y, CANVAS_W - (r.x + r.w), r.h, BG_RGB) == 0,
        "text did not escape the right edge");
    VT_ASSERT(
        count_not(0, r.y, r.x, r.h, BG_RGB) == 0, "text did not escape the left edge");
}

/* ---- 7. everything at once ----------------------------------------------- */

/*
 * The reference screenshot: two window panels and a menu, overlapping, with
 * every widget kind live. Nothing here is asserted beyond "it drew and it
 * stayed inside its panels" — this scene exists to be looked at.
 */
static void
visual_kitchen_sink(void)
{
    int stats;
    int toggles;
    int menu;
    int input;
    int rows[3];
    struct ToriRSChromeRect r_menu;

    printf("VISUAL: kitchen sink\n");
    ToriRSChrome_Init(&g_ui);

    stats = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 150, "Stats");
    ToriRSChrome_Label(&g_ui, stats, "fps      60");
    ToriRSChrome_Label(&g_ui, stats, "prims    128");
    ToriRSChrome_LabelColored(&g_ui, stats, "draws    41", g_ui.theme.accent);
    ToriRSChrome_Separator(&g_ui, stats);
    ToriRSChrome_Label(&g_ui, stats, "cache    osrs239");

    toggles = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 120, 190, "Debug");
    ToriRSChrome_Checkbox(&g_ui, toggles, "wireframe", 0);
    ToriRSChrome_Checkbox(&g_ui, toggles, "show hitboxes", 1);
    ToriRSChrome_Separator(&g_ui, toggles);
    input = ToriRSChrome_TextInput(&g_ui, toggles, "goto", "3222 3218");

    menu = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_MENU, 215, 60, 0, "Choose Option");
    rows[0] = ToriRSChrome_MenuItem(&g_ui, menu, "Teleport here");
    rows[1] = ToriRSChrome_MenuItem(&g_ui, menu, "Copy coordinates");
    rows[2] = ToriRSChrome_MenuItem(&g_ui, menu, "Cancel");
    (void)rows;
    (void)input;

    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_MouseMove(
        &g_ui,
        g_ui.widgets[rows[0]].x + 2,
        g_ui.widgets[rows[0]].y + g_ui.widgets[rows[0]].h / 2);
    ToriRSChrome_SetCaretVisible(&g_ui, 1);
    ToriRSChrome_Build(&g_ui);
    render("09_kitchen_sink");

    r_menu = ToriRSChrome_PanelRect(&g_ui, menu);
    VT_ASSERT(g_ui.overflow == 0, "the whole scene fit the display list");
    VT_ASSERT(
        px(r_menu.x + r_menu.w, r_menu.y + r_menu.h / 2) == BG_RGB,
        "the menu stayed inside its own box");
}

/* ------------------------------------------------------------------------- */


/* ---- 10. the baked OSRS skin --------------------------------------------- */

/*
 * The skin path end to end: TORIRS_CHROME_PRIM_SPRITE out of the chrome, through the
 * frame translator's sprite case, into a real Soft3D blit of the baked
 * parchment.
 *
 * The two halves are checked separately on purpose. `skin_avail` clear must
 * produce the flat fill -- that is the fallback every build without a baked
 * skin relies on -- and setting it must actually change the pixels. Asserting
 * only the second would pass just as well if the fallback were broken.
 */
static void
visual_skin(void)
{
    struct ToriRSChromeRect r;
    int panel;
    int flat_body_px;
    int skinned_body_px;

    printf("VISUAL: baked OSRS skin\n");

    /* No skin available: the body is the theme's flat fill. */
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = 0;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 24, 20, 160, "Map Editor");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Label(&g_ui, panel, "u50 o10;1;3 f4");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("10_skin_off");
    flat_body_px = count_eq(r.x + 1, r.y + r.h - 6, r.w - 2, 4, g_ui.theme.panel_body);
    VT_ASSERT(flat_body_px > 0, "no skin: body is the flat theme fill");

    /* Same panel, skin available: the parchment covers the flat fill. */
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = 1u << TORIRS_CHROME_SKIN_PANEL_BODY;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 24, 20, 160, "Map Editor");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Label(&g_ui, panel, "u50 o10;1;3 f4");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("11_skin_on");
    skinned_body_px = count_eq(r.x + 1, r.y + r.h - 6, r.w - 2, 4, g_ui.theme.panel_body);

    VT_ASSERT(
        skinned_body_px < flat_body_px,
        "skin on: the parchment replaced flat-fill pixels");
    /* The chrome still wins: the skin tiles under it, not over it. Checked on
     * the rail rather than the corner -- the corner is body, and under a skin
     * that means parchment. */
    VT_ASSERT(
        px(r.x + 1, r.y + r.h - 4) == g_ui.theme.panel_border, "rail still drawn over the skin");
    VT_ASSERT(
        px(r.x + 1, r.y + 1) == g_ui.theme.panel_title_bg, "title bar still drawn over the skin");
}


/* ---- 12. minimenu-style chrome, and dragging by the header --------------- */

/*
 * A window panel wears the minimenu's chrome (header bar, the rule under it,
 * side rails, bottom rule) and is carried by that header.
 *
 * The drag half is asserted through the module's own reported geometry rather
 * than by eyeballing the BMP: grab a point inside the header, move, and the
 * panel's rect must have travelled by exactly the mouse delta -- travelling by
 * a different amount is the grab offset being recomputed instead of held,
 * which reads as the panel snapping its corner to the cursor.
 */
static void
visual_panel_drag(void)
{
    struct ToriRSChromeRect before;
    struct ToriRSChromeRect after;
    int panel;
    int consumed;

    printf("VISUAL: minimenu chrome + header drag\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 40, 30, 150, "Map Editor");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Checkbox(&g_ui, panel, "block", 1);
    ToriRSChrome_Build(&g_ui);
    before = ToriRSChrome_PanelRect(&g_ui, panel);
    render("12_chrome_before_drag");

    /* The header strip carries the title bar's fill, and a rail runs down each
     * side of the body -- the minimenu's chrome, not a plain 1px outline. */
    VT_ASSERT(
        px(before.x + before.w / 2, before.y + 2) == g_ui.theme.panel_title_bg,
        "header bar fills the title strip");
    VT_ASSERT(
        px(before.x + 1, before.y + before.h - 4) == g_ui.theme.panel_border,
        "left rail runs down the body");
    VT_ASSERT(
        px(before.x + before.w - 2, before.y + before.h - 4) == g_ui.theme.panel_border,
        "right rail runs down the body");

    /* Grab the header off-centre, so a recomputed offset would show up. */
    consumed = ToriRSChrome_MouseDown(&g_ui, before.x + 20, before.y + 3);
    VT_ASSERT(consumed, "press on the header is consumed");
    ToriRSChrome_MouseMove(&g_ui, before.x + 20 + 60, before.y + 3 + 25);
    ToriRSChrome_Build(&g_ui);
    after = ToriRSChrome_PanelRect(&g_ui, panel);
    VT_ASSERT(after.x == before.x + 60, "panel followed the mouse in x");
    VT_ASSERT(after.y == before.y + 25, "panel followed the mouse in y");
    VT_ASSERT(after.w == before.w && after.h == before.h, "dragging did not resize it");

    ToriRSChrome_MouseUp(&g_ui, before.x + 20 + 60, before.y + 3 + 25);
    /* Released: the pointer is the world's again, and a later move must not
     * keep dragging the panel around. */
    ToriRSChrome_MouseMove(&g_ui, before.x + 200, before.y + 200);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(
        ToriRSChrome_PanelRect(&g_ui, panel).x == after.x,
        "panel stays put after the release");
    render("13_chrome_after_drag");

    /* A press on the body, not the header, is not a drag handle. */
    ToriRSChrome_MouseDown(&g_ui, after.x + 5, after.y + after.h - 4);
    ToriRSChrome_MouseMove(&g_ui, after.x + 90, after.y + after.h + 40);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(
        ToriRSChrome_PanelRect(&g_ui, panel).x == after.x,
        "a press on the body does not drag the panel");
    ToriRSChrome_MouseUp(&g_ui, after.x + 90, after.y + after.h + 40);
}

/* ---- 13. the resize grip ------------------------------------------------- */

/*
 * The bottom-right grip: drawn where the hit box says it is, and dragging it
 * takes the panel with it on both axes while the origin stays put.
 *
 * The origin is the assertion that matters. A resize that quietly slides the
 * whole panel -- because the size changed and x/y went along with it -- looks
 * almost right while you are dragging and is wrong the moment you stop, so it
 * is checked on every step rather than only at the end.
 */
static void
visual_panel_resize(void)
{
    struct ToriRSChromeRect before;
    struct ToriRSChromeRect after;
    struct ToriRSChromeRect grip;
    int panel;
    int rows;
    int consumed;

    printf("VISUAL: resize grip\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 60, 30, 160, "Catalog");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Label(&g_ui, panel, "u50 o10;1;3 f4");
    ToriRSChrome_Checkbox(&g_ui, panel, "block", 1);
    ToriRSChrome_Build(&g_ui);
    before = ToriRSChrome_PanelRect(&g_ui, panel);

    /* Not resizable yet: nothing is drawn in the corner and a press there is
     * an ordinary press on the body. */
    grip.x = before.x + before.w - 10;
    grip.y = before.y + before.h - 10;
    grip.w = 8;
    grip.h = 8;
    VT_ASSERT(
        count_eq(grip.x, grip.y, grip.w, grip.h, g_ui.theme.panel_border) == 0,
        "no grip on a panel that is not resizable");
    ToriRSChrome_MouseDown(&g_ui, grip.x + 4, grip.y + 4);
    ToriRSChrome_MouseMove(&g_ui, grip.x + 60, grip.y + 40);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(
        ToriRSChrome_PanelRect(&g_ui, panel).w == before.w,
        "dragging the corner of a fixed panel does nothing");
    ToriRSChrome_MouseUp(&g_ui, grip.x + 60, grip.y + 40);

    /* Turning the grip on reserves a strip along the bottom for it, so the
     * panel grows and the corner has to be measured again. */
    ToriRSChrome_PanelSetResizable(&g_ui, panel, 1);
    ToriRSChrome_Build(&g_ui);
    before = ToriRSChrome_PanelRect(&g_ui, panel);
    grip.x = before.x + before.w - 10;
    grip.y = before.y + before.h - 10;
    render("14_grip");
    VT_ASSERT(
        count_eq(grip.x, grip.y, grip.w, grip.h, g_ui.theme.panel_border) > 0,
        "the grip is drawn in the bottom-right corner");
    VT_ASSERT(
        count_eq(before.x + 2, grip.y, grip.w, grip.h, g_ui.theme.panel_border) == 0,
        "and nothing is drawn in the bottom-left one");

    /* Grab it off-centre, so a recomputed offset would show up as a jump. */
    consumed = ToriRSChrome_MouseDown(&g_ui, grip.x + 3, grip.y + 4);
    VT_ASSERT(consumed, "press on the grip is consumed");
    ToriRSChrome_MouseMove(&g_ui, grip.x + 3 + 70, grip.y + 4 + 45);
    ToriRSChrome_Build(&g_ui);
    after = ToriRSChrome_PanelRect(&g_ui, panel);
    VT_ASSERT(after.x == before.x && after.y == before.y, "the origin did not move");
    VT_ASSERT(after.w == before.w + 70, "the right edge followed the mouse");
    VT_ASSERT(after.h == before.h + 45, "the bottom edge followed the mouse");
    render("15_grip_resized");

    /* Rows survive the grow: a taller panel is the same rows with air under
     * them, not a relayout. */
    rows = 0;
    for( int i = 0; i < g_ui.widget_count; i++ )
        if( g_ui.widgets[i].h > 0 )
            rows++;
    VT_ASSERT(rows == 3, "growing the panel kept every row live");

    /* And back, past the minimum on both axes, which clamps rather than
     * inverting the panel. */
    ToriRSChrome_MouseMove(&g_ui, before.x - 200, before.y - 200);
    ToriRSChrome_Build(&g_ui);
    after = ToriRSChrome_PanelRect(&g_ui, panel);
    VT_ASSERT(after.x == before.x && after.y == before.y, "the origin still did not move");
    VT_ASSERT(after.w > 0 && after.h > 0, "a drag past the origin clamps");
    VT_ASSERT(after.w < before.w && after.h < before.h, "and it really did shrink");
    render("16_grip_clamped");

    /*
     * The rows that no longer fit are GONE, not merely undrawn.
     *
     * Checked on the hit boxes, not on pixels: an undrawn row whose box is
     * still live is the failure that matters here -- the panel would keep
     * toggling a checkbox nobody can see. Every surviving box must also end
     * inside the panel, or a row is half-out and still clickable.
     */
    {
        int live = 0;
        for( int i = 0; i < g_ui.widget_count; i++ )
        {
            struct ToriRSChromeWidget const* w = &g_ui.widgets[i];
            if( w->w <= 0 || w->h <= 0 )
                continue;
            live++;
            VT_ASSERT(
                w->y + w->h <= after.y + after.h, "a surviving row ends inside the panel");
        }
        VT_ASSERT(live < rows, "the rows that no longer fit were dropped");
        VT_ASSERT(
            ToriRSChrome_HitTest(&g_ui, after.x + 8, before.y + before.h - 14) < 0,
            "nothing is hittable where the dropped rows used to be");
    }

    ToriRSChrome_MouseUp(&g_ui, before.x - 200, before.y - 200);
    ToriRSChrome_MouseMove(&g_ui, before.x + 400, before.y + 400);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(
        ToriRSChrome_PanelRect(&g_ui, panel).w == after.w &&
            ToriRSChrome_PanelRect(&g_ui, panel).h == after.h,
        "the panel stops resizing after the release");
}

/* ---- 17. dropdowns ------------------------------------------------------- */

/*
 * The closed row and the open list, in the skin the map editor actually wears.
 *
 * Two BMPs rather than one: the closed box is chrome the panel draws every
 * frame, and the open list is a popup built after every panel, so a break in
 * one is invisible in the other. The long-list scene is the one that matters
 * for the scrollbar -- 40 options against a 10-row window is the case where a
 * list has to say how far down it goes.
 */
static char const* const dd_short[] = { "Hidden", "Wall", "Roof", "Floor decoration" };
static char const* const dd_long[] = {
    "0000 nothing",     "0001 dirt",        "0002 grass",       "0003 water",
    "0004 sand",        "0005 gravel",      "0006 stone",       "0007 lava",
    "0008 snow",        "0009 ice",         "0010 wood",        "0011 marble",
    "0012 cobbles",     "0013 mud",         "0014 swamp",       "0015 path",
    "0016 tiles",       "0017 carpet",      "0018 metal",       "0019 rubble",
    "0020 ash",         "0021 leaves",      "0022 bark",        "0023 clay",
    "0024 chalk",       "0025 slate",       "0026 brick",       "0027 thatch",
    "0028 shingle",     "0029 planks",      "0030 rope",        "0031 cloth",
    "0032 glass",       "0033 crystal",     "0034 bone",        "0035 scales",
    "0036 fur",         "0037 hide",        "0038 wool",        "0039 silk",
};

/* The dropdown geometry the chrome contracts to, restated in the test's own
 * terms. Mirrors of DBG_SCROLL_W and DBG_DROP_LIST_PAD, which are private to
 * the module -- a test that imported them could not see them change. */
#define DD_SCROLL_W 16
#define DD_LIST_PAD 2

/** Row pitch of the open list: the theme's row face plus its air. */
static int
dd_row_h(void)
{
    return ToriRSChrome_FontLineBox(g_ui.theme.font_row, g_ui.scale) + 4;
}

/** The laid-out box of a widget, read back off the chrome's own state. */
static struct ToriRSChromeRect
dbg_widget_box(int widget)
{
    struct ToriRSChromeRect r;
    r.x = g_ui.widgets[widget].x;
    r.y = g_ui.widgets[widget].y;
    r.w = g_ui.widgets[widget].w;
    r.h = g_ui.widgets[widget].h;
    return r;
}

/** Where the open list lands under `widget`: the box below it, as wide as the
 *  box for a value dropdown and as wide as its widest row for a menu. */
static struct ToriRSChromeRect
dbg_dropdown_list_rect(int widget)
{
    struct ToriRSChromeRect const box = dbg_widget_box(widget);
    struct ToriRSChromeWidget const* w = &g_ui.widgets[widget];
    struct ToriRSChromeRect r;
    int rows = w->option_count < TORIRS_CHROME_DROPDOWN_ROWS ? w->option_count
                                                             : TORIRS_CHROME_DROPDOWN_ROWS;
    int label_w = ToriRSChrome_MeasureText(g_ui.theme.font_row, g_ui.scale, w->label);

    r.x = box.x + label_w + (label_w > 0 ? 5 : 0);
    r.w = box.x + box.w - r.x;
    if( w->menu_mode )
    {
        int widest = 0;
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriRSChrome_MeasureText(g_ui.theme.font_row, g_ui.scale, w->options[i]);
            if( ow > widest )
                widest = ow;
        }
        r.x = box.x;
        r.w = widest + 2 * 3 + 6;
    }
    r.y = box.y + box.h;
    r.h = rows * dd_row_h() + 2 * DD_LIST_PAD;
    return r;
}

/** A press and release at one point, then a rebuild -- one user click. */
static void
dd_click(int x, int y)
{
    ToriRSChrome_MouseMove(&g_ui, x, y);
    ToriRSChrome_MouseDown(&g_ui, x, y);
    ToriRSChrome_MouseUp(&g_ui, x, y);
    ToriRSChrome_Build(&g_ui);
}

/** Open a dropdown by clicking its closed box. */
static void
dd_open(int widget)
{
    struct ToriRSChromeRect const box = dbg_widget_box(widget);
    dd_click(box.x + box.w / 2, box.y + box.h / 2);
}

static void
visual_dropdown(void)
{
    int panel;
    int dd_kind;
    int dd_list;
    struct ToriRSChromeRect box;
    struct ToriRSChromeRect list;
    struct ToriRSChromeRect bar;

    printf("VISUAL: dropdowns\n");

    ToriRSChrome_Init(&g_ui);
    g_ui.theme = torirs_chrome_theme_osrs;
    g_ui.skin_avail = (1u << TORIRS_CHROME_SKIN_PANEL_BODY) | (1u << TORIRS_CHROME_SKIN_SCROLL_UP) |
                      (1u << TORIRS_CHROME_SKIN_SCROLL_DOWN);
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;

    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 8, 210, "Catalog");
    dd_kind = ToriRSChrome_Dropdown(&g_ui, panel, "Kind", dd_short, 4, 1);
    dd_list = ToriRSChrome_Dropdown(
        &g_ui, panel, "", dd_long, (int)(sizeof(dd_long) / sizeof(dd_long[0])), 0);
    ToriRSChrome_Build(&g_ui);
    render("17_dropdown_closed");

    /* Open the short list by clicking its box, exactly as the user does. */
    box = dbg_widget_box(dd_kind);
    ToriRSChrome_MouseMove(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_MouseDown(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_MouseUp(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_Build(&g_ui);
    render("18_dropdown_open_short");

    /* Dismiss it before opening the other one: an open list covers the row
     * beneath it, so a click aimed at the second box would land in the first
     * list instead. */
    ToriRSChrome_MouseDown(&g_ui, CANVAS_W - 8, CANVAS_H - 8);
    ToriRSChrome_MouseUp(&g_ui, CANVAS_W - 8, CANVAS_H - 8);

    /* The long list, scrolled off the top so the grip is not at either end. */
    box = dbg_widget_box(dd_list);
    ToriRSChrome_MouseMove(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_MouseDown(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_MouseUp(&g_ui, box.x + box.w - 6, box.y + box.h / 2);
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_MouseWheel(&g_ui, box.x + 4, box.y + box.h + 20, 12);
    ToriRSChrome_MouseMove(&g_ui, box.x + 8, box.y + box.h + 20);
    ToriRSChrome_Build(&g_ui);
    render("19_dropdown_open_long");

    /*
     * The bar, checked where the module says it drew one.
     *
     * Its column is reconstructed here from the widget box and the two public
     * constants rather than read back off the chrome, which is deliberate:
     * these coordinates are the CONTRACT -- 16 wide, inset by the list's own
     * 2px pad, an arrow button at each end -- and a test that asked the module
     * where it put the bar could not notice the module moving it.
     */
    list = dbg_dropdown_list_rect(dd_list);
    bar.w = DD_SCROLL_W;
    bar.x = list.x + list.w - DD_LIST_PAD - bar.w;
    bar.y = list.y + DD_LIST_PAD;
    bar.h = list.h - 2 * DD_LIST_PAD;

    /* The two arrow buttons are sprites, so they are neither the track colour
     * nor the list's own tile: they are the lightest thing in the column. */
    VT_ASSERT(
        count_not(bar.x, bar.y, bar.w, DD_SCROLL_W, px(bar.x + 1, bar.y + bar.h / 2)) > 0,
        "scrollbar: an up arrow at the top of the column");
    VT_ASSERT(
        count_not(
            bar.x, bar.y + bar.h - DD_SCROLL_W, bar.w, DD_SCROLL_W,
            px(bar.x + 1, bar.y + bar.h / 2)) > 0,
        "scrollbar: a down arrow at the bottom of the column");
    /* A grip somewhere in the track, and not filling it: mid-list is exactly
     * the case where a bar that drew a full-length grip would look right and
     * mean nothing. */
    {
        uint32_t const track = px(bar.x + bar.w / 2, bar.y + DD_SCROLL_W + 1);
        int const grip_px = count_not(
            bar.x, bar.y + DD_SCROLL_W, bar.w, bar.h - 2 * DD_SCROLL_W, track);
        VT_ASSERT(grip_px > 0, "scrollbar: a grip inside the track");
        VT_ASSERT(
            grip_px < bar.w * (bar.h - 2 * DD_SCROLL_W),
            "scrollbar: the grip is shorter than the track");
    }

    /* No row runs under the bar. The rows are clipped to the column left of
     * it, which is what stops a long option painting over the grip. */
    VT_ASSERT(
        count_eq(bar.x, bar.y + DD_SCROLL_W, bar.w, bar.h - 2 * DD_SCROLL_W, g_ui.theme.dropdown_text) == 0,
        "scrollbar: no row text bleeds into the bar");
}

/* ---- 20. the scrollbar is a control, not a picture ----------------------- */

/*
 * Arrow, track and grip, driven through the real mouse entry points.
 *
 * Behaviour rather than pixels, because the failure this is guarding against
 * is a bar that LOOKS right and does nothing -- which is what the list had
 * before: a thumb drawn from the scroll position, with no way to move it but
 * the wheel. Each leg asserts against the widget's own `scroll`, so it fails
 * whether the cause is a hit box in the wrong place or a press routed to the
 * rows underneath.
 */
static void
visual_dropdown_scrollbar_drag(void)
{
    int panel;
    int dd;
    struct ToriRSChromeRect list;
    struct ToriRSChromeRect bar;
    int const count = (int)(sizeof(dd_long) / sizeof(dd_long[0]));
    int rows;
    int before;

    printf("VISUAL: dropdown scrollbar\n");

    ToriRSChrome_Init(&g_ui);
    g_ui.theme = torirs_chrome_theme_osrs;
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
        g_ui.skin_avail |= 1u << i;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;

    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 8, 210, "Catalog");
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "", dd_long, count, 0);
    ToriRSChrome_Build(&g_ui);
    dd_open(dd);

    list = dbg_dropdown_list_rect(dd);
    bar.w = DD_SCROLL_W;
    bar.x = list.x + list.w - DD_LIST_PAD - bar.w;
    bar.y = list.y + DD_LIST_PAD;
    bar.h = list.h - 2 * DD_LIST_PAD;
    rows = list.h / dd_row_h();

    /* The down arrow steps one row; the track below the grip pages by the
     * window. Both are presses, so both also have to NOT be read as a click on
     * the option the bar happens to cover. */
    before = g_ui.widgets[dd].scroll;
    dd_click(bar.x + bar.w / 2, bar.y + bar.h - 2);
    VT_ASSERT(g_ui.widgets[dd].scroll == before + 1, "scrollbar: the down arrow steps one row");
    VT_ASSERT(g_ui.dropdown_open == dd, "scrollbar: pressing an arrow does not choose a row");

    before = g_ui.widgets[dd].scroll;
    dd_click(bar.x + bar.w / 2, bar.y + bar.h - DD_SCROLL_W - 2);
    VT_ASSERT(
        g_ui.widgets[dd].scroll == before + rows, "scrollbar: the track below the grip pages down");

    /* The up arrow, back. */
    before = g_ui.widgets[dd].scroll;
    dd_click(bar.x + bar.w / 2, bar.y + 2);
    VT_ASSERT(g_ui.widgets[dd].scroll == before - 1, "scrollbar: the up arrow steps one row");

    /* Drag the grip to the bottom of the track. It must land on the last page
     * -- clamped, not run past it -- and the release must not choose whatever
     * option the grip finished over.
     *
     * Wound back to the top first so the grip is where the geometry says it is
     * without this test having to recompute it: at scroll 0 it starts flush
     * under the up arrow. */
    ToriRSChrome_MouseWheel(&g_ui, list.x + 4, list.y + 4, -100);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(g_ui.widgets[dd].scroll == 0, "scrollbar: wound back to the top");
    ToriRSChrome_MouseDown(&g_ui, bar.x + bar.w / 2, bar.y + DD_SCROLL_W + 2);
    VT_ASSERT(g_ui.dropdown_scroll_drag != 0, "scrollbar: pressing the grip starts a drag");
    ToriRSChrome_MouseMove(&g_ui, bar.x + bar.w / 2, bar.y + bar.h * 4);
    ToriRSChrome_MouseUp(&g_ui, bar.x + bar.w / 2, bar.y + bar.h * 4);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(
        g_ui.widgets[dd].scroll == count - rows, "scrollbar: a drag past the end clamps to it");
    VT_ASSERT(g_ui.dropdown_open == dd, "scrollbar: ending a drag does not choose a row");
    render("20_dropdown_scrolled_to_end");

    /* And back to the top the same way. */
    ToriRSChrome_MouseDown(&g_ui, bar.x + bar.w / 2, bar.y + bar.h - DD_SCROLL_W - 4);
    ToriRSChrome_MouseMove(&g_ui, bar.x + bar.w / 2, bar.y - bar.h);
    ToriRSChrome_MouseUp(&g_ui, bar.x + bar.w / 2, bar.y - bar.h);
    ToriRSChrome_Build(&g_ui);
    VT_ASSERT(g_ui.widgets[dd].scroll == 0, "scrollbar: a drag past the start clamps to it");
}

/* ---- 20b. the two fallbacks: no skin, and a scaled chrome ---------------- */

/*
 * The same open list with the skin withheld, and again at 3x with it.
 *
 * Two separate failure modes, and neither is visible in the scene above. A
 * build with no baked skin must still get a usable bar -- the flat IF1 form,
 * a dark track under a grip with a highlight and a shadow -- because that is
 * the form every host without an uploaded skin renders. And a 3x chrome must
 * BLOW THE SPRITES UP rather than leave 16px images marooned in a 48px column,
 * which is the bug the destination box on the sprite primitive exists to
 * prevent; the arrow ends up drawn at a third of the width of the bar it is
 * supposed to cap.
 */
static void
visual_dropdown_fallbacks(void)
{
    int panel;
    int dd;
    struct ToriRSChromeRect list;
    struct ToriRSChromeRect bar;
    int const count = (int)(sizeof(dd_long) / sizeof(dd_long[0]));

    printf("VISUAL: dropdown fallbacks\n");

    /* No skin at all: the flat developer look, flat bar included. */
    ToriRSChrome_Init(&g_ui);
    ToriRSChrome_SetTheme(&g_ui, &torirs_chrome_theme_default);
    g_ui.skin_avail = 0;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 8, 210, "Catalog");
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "", dd_long, count, 0);
    ToriRSChrome_Build(&g_ui);
    dd_open(dd);
    render("22_dropdown_no_skin");

    list = dbg_dropdown_list_rect(dd);
    bar.w = DD_SCROLL_W;
    bar.x = list.x + list.w - DD_LIST_PAD - bar.w;
    bar.y = list.y + DD_LIST_PAD;
    bar.h = list.h - 2 * DD_LIST_PAD;
    VT_ASSERT(
        count_eq(bar.x, bar.y + DD_SCROLL_W, bar.w, bar.h - 2 * DD_SCROLL_W,
                 g_ui.theme.scroll_track) > 0,
        "no skin: the bar falls back to the flat track colour");
    VT_ASSERT(
        count_eq(bar.x, bar.y + DD_SCROLL_W, bar.w, bar.h - 2 * DD_SCROLL_W,
                 g_ui.theme.scroll_grip_hi) > 0,
        "no skin: the flat grip keeps its highlight edge");

    /* 3x with the skin: every sprite is blown up to the column it fills. */
    ToriRSChrome_Init(&g_ui);
    g_ui.theme = torirs_chrome_theme_osrs;
    ToriRSChrome_SetScale(&g_ui, 3);
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
        g_ui.skin_avail |= 1u << i;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 4, 0, "Catalog");
    dd = ToriRSChrome_Dropdown(&g_ui, panel, "", dd_short, 4, 1);
    ToriRSChrome_Build(&g_ui);
    dd_open(dd);
    render("23_dropdown_3x");

    /*
     * The arrow on the closed button is 48 wide, not 16.
     *
     * Asserted as a COMPARISON between the two halves of the arrow's box
     * rather than against a colour, because the tile behind it is brown and
     * the arrow is brown -- naming a colour would pass on the tile alone,
     * which is exactly how the un-scaled version of this first went green.
     * The arrow's own frame is near-black, so its bottom-right quarter (only
     * reached if the sprite really is 48 wide) holds pixels much darker than
     * anything the tile has.
     */
    {
        struct ToriRSChromeRect const box = dbg_widget_box(dd);
        /* Right-aligned, two rules in from the frame's edge -- script_3850's
         * `cc_setposition(12, .., 2, 0)`, where x-mode 2 measures from the far
         * side. One rule is 3px at this scale. */
        int const arrow_x = box.x + box.w - 2 * 3 - 48;
        int const arrow_y = box.y + (box.h - 48) / 2;
        int dark_near = 0;
        int dark_far = 0;

        for( int j = 0; j < 48; j++ )
            for( int i = 0; i < 48; i++ )
            {
                uint32_t const c = px(arrow_x + i, arrow_y + j);
                int const lum = (int)((c >> 16) & 0xFF) + (int)((c >> 8) & 0xFF) + (int)(c & 0xFF);
                if( lum >= 3 * 0x20 )
                    continue;
                if( i < 16 && j < 16 )
                    dark_near++;
                else
                    dark_far++;
            }
        VT_ASSERT(dark_near > 0, "3x: the arrow is drawn at all");
        VT_ASSERT(dark_far > dark_near, "3x: the arrow fills its 48px box, not the top-left 16");
    }
}

/* ---- 21. a menu list is still a menu ------------------------------------- */

/*
 * The File/Edit bar's list, which shares the dropdown popup machinery.
 *
 * It must NOT have picked up the settings dropdown's look. The game has both
 * widgets and they are not the same one: a value list is banded and centred
 * and orange, a menu is the minimenu -- flat brown, left-aligned, and the
 * cursor turns a row's TEXT yellow rather than lighting the row. One popup
 * implementation serving both is only correct if this stays true.
 */
static char const* const dd_menu[] = { "New map", "Open...", "Save", "Quit" };

static void
visual_menubar_dropdown(void)
{
    int bar_panel;
    int menu;
    struct ToriRSChromeRect box;
    struct ToriRSChromeRect list;

    printf("VISUAL: menubar dropdown\n");

    ToriRSChrome_Init(&g_ui);
    g_ui.theme = torirs_chrome_theme_osrs;
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
        g_ui.skin_avail |= 1u << i;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;

    bar_panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_MENUBAR, 0, 0, CANVAS_W, "");
    menu = ToriRSChrome_MenuDrop(&g_ui, bar_panel, "File", dd_menu, 4);
    ToriRSChrome_Build(&g_ui);

    box = dbg_widget_box(menu);
    dd_click(box.x + box.w / 2, box.y + box.h / 2);
    /* Hover the second row, which is where the two looks differ most. */
    list = dbg_dropdown_list_rect(menu);
    ToriRSChrome_MouseMove(&g_ui, list.x + 4, list.y + DD_LIST_PAD + dd_row_h() + 2);
    ToriRSChrome_Build(&g_ui);
    render("21_menubar_dropdown");

    VT_ASSERT(g_ui.dropdown_open == menu, "menu: the title opened its list");
    /* Left-aligned: there is ink in the first few pixels of the row's text
     * column, which a centred row of this width would leave empty. */
    VT_ASSERT(
        count_eq(
            list.x + DD_LIST_PAD, list.y + DD_LIST_PAD, 10, dd_row_h(),
            g_ui.theme.menu_text) > 0,
        "menu: rows are left-aligned");
    /* Yellow text under the cursor, and no band behind it. */
    VT_ASSERT(
        count_eq(
            list.x + DD_LIST_PAD, list.y + DD_LIST_PAD + dd_row_h(), list.w - 2 * DD_LIST_PAD,
            dd_row_h(), g_ui.theme.menu_hover_text) > 0,
        "menu: the hovered row goes yellow");
    VT_ASSERT(
        count_eq(
            list.x + DD_LIST_PAD, list.y + DD_LIST_PAD, list.w - 2 * DD_LIST_PAD, dd_row_h(),
            g_ui.theme.menu_body) > 0,
        "menu: unhovered rows keep the flat menu body");
}

/* ---- 11. HighDPI / scaled chrome ----------------------------------------
 *
 * The same panel at 1x, 2x and 3x, drawn with the faces baked at each size.
 *
 * The pixel assertion is the point of doing it visually rather than in the
 * model test: a layout can scale perfectly and still be drawn with the wrong
 * font, and the rect arithmetic cannot see that. Ink AREA can -- and it comes
 * out EXACT, which is worth stating as the equality it is rather than as a
 * threshold. Every pixel of the 1x chrome becomes an N x N block at scale N:
 * the glyphs because fontbake block-scales the masks, the rules and boxes
 * because DBG_PX multiplies every coordinate. So the ink is scale^2 times the
 * 1x ink, to the pixel, and anything else -- a resampled glyph, a rule left at
 * 1px, a font slot resolved at the wrong size -- breaks the equality.
 */
static void
visual_scaled(void)
{
    int ink[TORIRS_CHROME_SCALE_MAX + 1];
    struct ToriRSChromeRect rect[TORIRS_CHROME_SCALE_MAX + 1];

    memset(ink, 0, sizeof(ink));
    memset(rect, 0, sizeof(rect));

    for( int scale = TORIRS_CHROME_SCALE_MIN; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
    {
        char name[32];
        int panel;

        ToriRSChrome_Init(&g_ui);
        ToriRSChrome_SetTheme(&g_ui, &torirs_chrome_theme_default);
        ToriRSChrome_SetScale(&g_ui, scale);
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 4, 4, 0, "Map Editor");
        ToriRSChrome_Label(&g_ui, panel, "m50_50 (1,2)");
        ToriRSChrome_Checkbox(&g_ui, panel, "block", 1);
        ToriRSChrome_Build(&g_ui);
        rect[scale] = ToriRSChrome_PanelRect(&g_ui, panel);

        snprintf(name, sizeof(name), "11_scale%dx", scale);
        render(name);

        /* Ink: anything that is neither the cleared background nor the panel
         * body. Counted over the panel's own box so a larger panel does not
         * count more background as ink. */
        for( int y = rect[scale].y; y < rect[scale].y + rect[scale].h && y < CANVAS_H; y++ )
            for( int x = rect[scale].x; x < rect[scale].x + rect[scale].w && x < CANVAS_W; x++ )
            {
                uint32_t const c = px(x, y);
                if( c != BG_RGB && c != torirs_chrome_theme_default.panel_body )
                    ink[scale]++;
            }
    }

    for( int scale = TORIRS_CHROME_SCALE_MIN; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
        printf(
            "  scale %dx: panel %dx%d, ink %d px\n", scale, rect[scale].w, rect[scale].h,
            ink[scale]);

    for( int scale = TORIRS_CHROME_SCALE_MIN + 1; scale <= TORIRS_CHROME_SCALE_MAX; scale++ )
    {
        VT_ASSERT(
            rect[scale].w == rect[1].w * scale && rect[scale].h == rect[1].h * scale,
            "scaled panel box is the 1x box times the scale");
        VT_ASSERT(
            ink[scale] == ink[1] * scale * scale,
            "scaled chrome is the 1x chrome pixel-doubled, ink and all");
    }
    ToriRSChrome_Init(&g_ui);
}

/* ---- tabs, buttons and a scrolling panel ---------------------------------
 *
 * The three Phase-0 additions in one shot, because they are one feature in
 * practice: a settings window is a tab strip over a scrolling column with a
 * commit button at the bottom. What a BMP proves here that the model tests
 * cannot is that a scrolled panel really does paint its rows clipped to the
 * scroll window, and that the strip stays put while they move under it.
 */
static void
visual_tabs_and_scroll(void)
{
    static char const* const tabs[] = { "Plugins", "tile_indicator", "lootbeam" };
    struct ToriRSChromeTheme const* t = &g_ui.theme;
    struct ToriRSChromeRect r;
    int panel;
    int strip;
    int save;
    int first_row;

    printf("VISUAL: tabs / buttons / panel scroll\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 250, "Plugins");
    strip = ToriRSChrome_Tabs(&g_ui, panel, tabs, 3, 1);

    ToriRSChrome_PanelBeginTab(&g_ui, panel, 0);
    ToriRSChrome_Label(&g_ui, panel, "2 plugins loaded");

    ToriRSChrome_PanelBeginTab(&g_ui, panel, 1);
    first_row = ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    ToriRSChrome_TextInput(&g_ui, panel, "colour", "#FFCC00");
    ToriRSChrome_TextInput(&g_ui, panel, "width", "2");
    ToriRSChrome_Checkbox(&g_ui, panel, "show text", 0);
    ToriRSChrome_TextInput(&g_ui, panel, "alpha", "128");
    ToriRSChrome_Checkbox(&g_ui, panel, "outline", 1);
    ToriRSChrome_TextInput(&g_ui, panel, "radius", "3");
    ToriRSChrome_Separator(&g_ui, panel);
    save = ToriRSChrome_Button(&g_ui, panel, "Save");

    ToriRSChrome_PanelBeginTab(&g_ui, panel, -1);
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("24_tabs_unscrolled");

    VT_ASSERT(g_ui.widgets[strip].h > 0, "the strip laid out");
    VT_ASSERT(g_ui.widgets[save].w > 0, "the button laid out");
    /* Tab 0 is not the selected one, so it keeps the rule along its bottom --
     * the pixel row the selected tab erases to join itself to the content. */
    {
        struct ToriRSChromeWidget const* s = &g_ui.widgets[strip];
        VT_ASSERT(
            px(s->x + 2, s->y + s->h - 1) == t->panel_border,
            "an unselected tab keeps its base rule");
    }

    /* Now force it short and scrollable: the same tab, below the fold. */
    ToriRSChrome_PanelSetScrollable(&g_ui, panel, 1);
    g_ui.panels[panel].fixed_h = 96;
    g_ui.panels[panel].dirty = 1;
    g_ui.dirty = 1;
    ToriRSChrome_Build(&g_ui);
    render("25_tabs_scroll_top");
    VT_ASSERT(
        g_ui.panels[panel].content_h > g_ui.panels[panel].view_h,
        "the short panel overflows its view");
    VT_ASSERT(g_ui.widgets[first_row].h > 0, "the first row is in view at rest");
    VT_ASSERT(g_ui.widgets[save].h == 0, "the button is below the fold at rest");

    /* Wheel to the end: the button arrives, the first row leaves. */
    {
        int const wx = r.x + 8;
        int const wy = r.y + 60;
        for( int i = 0; i < 30; i++ )
            ToriRSChrome_MouseWheel(&g_ui, wx, wy, -1);
        ToriRSChrome_Build(&g_ui);
        render("26_tabs_scroll_end");
        VT_ASSERT(g_ui.widgets[save].h > 0, "the button scrolls into view");
        VT_ASSERT(g_ui.widgets[first_row].h == 0, "the first row scrolls out");
        VT_ASSERT(g_ui.widgets[strip].h > 0, "the strip does not scroll away with the rows");
    }
    ToriRSChrome_Init(&g_ui);
}

/*
 * The roster list: a name, a settings affordance and a switch per row, and the
 * two outcomes a row has.
 *
 * The zones are asserted by CLICKING them rather than by reading pixels,
 * because what makes this a list rather than a column of checkboxes is that
 * one row answers two different questions depending on where it was hit.
 */
static void
visual_listrow(void)
{
    int panel;
    int on;
    int off;
    int plain;

    printf("VISUAL: roster list rows\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 250, "Plugins");
    on = ToriRSChrome_ListRow(&g_ui, panel, "entity-highlighter", 1, 1);
    off = ToriRSChrome_ListRow(&g_ui, panel, "tile-indicator-c", 0, 1);
    plain = ToriRSChrome_ListRow(&g_ui, panel, "lua", 1, 0);
    ToriRSChrome_Build(&g_ui);
    render("27_listrows");

    VT_ASSERT(g_ui.widgets[on].h > 0, "a list row lays out");
    /* Uniform height whether or not a row carries an action: the switches have
     * to line up down the column, which is what makes the list scannable. */
    VT_ASSERT(
        g_ui.widgets[on].h == g_ui.widgets[plain].h,
        "an action changes nothing about a row's height");
    VT_ASSERT(
        g_ui.widgets[on].w == g_ui.widgets[plain].w,
        "every row in a table panel is the same width");

    /* The switch zone toggles and reports an ordinary activation. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[off];
        int const tx = w->x + w->w - 4;
        int const ty = w->y + w->h / 2;
        ToriRSChrome_MouseDown(&g_ui, tx, ty);
        ToriRSChrome_MouseUp(&g_ui, tx, ty);
        VT_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == off, "the switch activates its row");
        VT_ASSERT(
            !ToriRSChrome_ActivationWasAction(&g_ui), "the switch is not the action zone");
        VT_ASSERT(ToriRSChrome_Checked(&g_ui, off), "the switch toggled the row on");
    }

    /* The action zone opens the row instead, and leaves the switch alone. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[on];
        int const ax = w->x + 4;
        int const ay = w->y + w->h / 2;
        ToriRSChrome_MouseDown(&g_ui, ax, ay);
        ToriRSChrome_MouseUp(&g_ui, ax, ay);
        VT_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == on, "the action zone activates its row");
        VT_ASSERT(
            ToriRSChrome_ActivationWasAction(&g_ui), "the action zone reports as an action");
        VT_ASSERT(ToriRSChrome_Checked(&g_ui, on), "the action zone left the switch alone");
    }

    /* A row with no action has no dead zone: its whole width is the switch. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[plain];
        int const ax = w->x + 4;
        int const ay = w->y + w->h / 2;
        ToriRSChrome_MouseDown(&g_ui, ax, ay);
        ToriRSChrome_MouseUp(&g_ui, ax, ay);
        VT_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == plain, "an actionless row still fires");
        VT_ASSERT(
            !ToriRSChrome_ActivationWasAction(&g_ui),
            "an actionless row never reports an action");
        VT_ASSERT(!ToriRSChrome_Checked(&g_ui, plain), "an actionless row toggles anywhere");
    }
    ToriRSChrome_Init(&g_ui);
}

/*
 * The roster exactly as the plugin window builds it at runtime: the reference
 * theme with every baked skin slot present, and the plugin names from a real
 * session. `visual_listrow` above deliberately runs with NO skin, because that
 * is the fallback look; this is the one a player sees, and it is the one that
 * has to match what the CS2 executor puts on the popout strip.
 */
static void
visual_listrow_skinned(void)
{
    int panel;

    printf("VISUAL: roster list rows, skinned\n");
    ToriRSChrome_Init(&g_ui);
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT && i < ToriRSChromeSkin_Count(); i++ )
        g_ui.skin_avail |= 1u << i;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIRS_CHROME_SKIN_PANEL_BODY)->h;

    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 12, 250, "Plugins");
    /* The interfaces' own nine-slice border, which is what the same roster
     * wears when the CS2 executor mounts it in the gameframe's popout strip.
     * Drawn here so the two presentations can be put side by side. */
    ToriRSChrome_PanelSetFramed(&g_ui, panel, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "tile-indicator-c", 0, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "lua", 1, 0);
    ToriRSChrome_ListRow(&g_ui, panel, "tile-indicator-lua", 1, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "entity-highlighter", 1, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "loot-beam", 1, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "ground-items", 1, 1);
    ToriRSChrome_ListRow(&g_ui, panel, "screenshot", 1, 1);
    ToriRSChrome_Build(&g_ui);
    render("32_listrows_skinned");

    /*
     * The frame is drawn, and it is drawn as ART rather than as the rails.
     *
     * Read off the panel's own reported box, not off coordinates: the corner
     * pixel is the nine-slice's, so it is neither the theme's border black nor
     * the body brown showing through, and the rails a framed panel does NOT
     * draw would have put panel_border down the side.
     */
    {
        struct ToriRSChromeRect const r = ToriRSChrome_PanelRect(&g_ui, panel);
        int const mid_y = r.y + r.h / 2;

        VT_ASSERT(
            count_eq(r.x, r.y, r.w, r.h, g_ui.theme.panel_body) <
                count_not(r.x, r.y, r.w, r.h, g_ui.theme.panel_body),
            "the framed panel is mostly not flat body");
        /* Down the left edge, level with the rows: the frame's own brown, and
         * specifically not the rail's black. */
        VT_ASSERT(
            px(r.x + 1, mid_y) != g_ui.theme.panel_border,
            "a framed panel draws no side rail");
        VT_ASSERT(
            px(r.x + 1, mid_y) != g_ui.theme.panel_body,
            "the frame covers the body down the edge");
        /*
         * The corner, against the BAKE rather than against a colour.
         *
         * This assertion used to say "the corner pixel is near-black", which
         * was true of the thin nine-slice baked before it and is not true of
         * the strip's own frame: its rail is lit from outside, so the outermost
         * row is the HIGHLIGHT and the near-black is the inner edge. A test
         * that pins a colour pins the art it was written against; one that
         * pins the blit catches the thing that actually goes wrong, which is a
         * corner drawn from the wrong slot or at the wrong size.
         */
        VT_ASSERT(
            skinned_slot_pixels(r.x, r.y, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) > 200,
            "the corner is the baked frame piece, blitted 1:1");
        /*
         * And it is ROUNDED: the corner piece's own outermost pixel is
         * transparent, so what shows at (r.x, r.y) is the panel's tile. A
         * square frame -- or an edge piece blitted into the corner -- would
         * have painted it the rail's colour.
         */
        VT_ASSERT(
            px(r.x, r.y) != px(r.x + 1, r.y + 1),
            "and the corner's own outer pixel is rounded away");
    }

    /* Withheld, the panel falls back to the rails -- and lays out for them, so
     * a build that baked no frame is not a panel indented past its own edge. */
    {
        struct ToriRSChromeRect framed = ToriRSChrome_PanelRect(&g_ui, panel);
        struct ToriRSChromeRect railed;

        g_ui.skin_avail &= ~(1u << TORIRS_CHROME_SKIN_FRAME_TOP_LEFT);
        ToriRSChrome_PanelSetVisible(&g_ui, panel, 0);
        ToriRSChrome_PanelSetVisible(&g_ui, panel, 1);
        ToriRSChrome_Build(&g_ui);
        render("33_listrows_unframed");
        railed = ToriRSChrome_PanelRect(&g_ui, panel);
        VT_ASSERT(
            px(railed.x + 1, railed.y + railed.h / 2) == g_ui.theme.panel_border,
            "no frame baked: the rails come back");
        VT_ASSERT(
            railed.h < framed.h, "and the panel is no longer padded for a border it lacks");
    }
    ToriRSChrome_Init(&g_ui);
}

/*
 * The chrome's HSL16 palette against the RASTERISER's.
 *
 * The chrome computes its own (see ToriRSChrome_Hsl16ToRgb): it links no
 * renderer, so it cannot read g_hsl16_to_rgb_table, and a swatch that could
 * only be drawn once something else had initialised a palette would be blank
 * in every test and on every frame before the first cache opened.
 *
 * A second implementation of a conversion is a liability unless something
 * checks it, and this is the check. It is also the alarm for a change nobody
 * would otherwise connect to the chrome: if the client ever builds its palette
 * at a brightness other than 0.8, every swatch and every axis bar quietly
 * becomes a shade off, and THIS is what says so.
 */
static void
visual_hsl16_palette(void)
{
    int mismatches = 0;
    int first = -1;

    printf("VISUAL: HSL16 palette agrees with the rasteriser\n");
    ToriDraw_InitHsl16();
    for( int hsl = 0; hsl < 65536; hsl++ )
    {
        uint32_t const ours = ToriRSChrome_Hsl16ToRgb(hsl);
        uint32_t const theirs = (uint32_t)ToriDraw_Hsl16ToRgb((uint16_t)hsl) & 0xFFFFFFu;
        if( ours == theirs )
            continue;
        if( first < 0 )
            first = hsl;
        mismatches++;
    }
    if( mismatches )
        fprintf(
            stderr,
            "  first mismatch at hsl16 %d: chrome 0x%06X, rasteriser 0x%06X\n",
            first,
            ToriRSChrome_Hsl16ToRgb(first),
            (unsigned)ToriDraw_Hsl16ToRgb((uint16_t)first) & 0xFFFFFFu);
    VT_ASSERT(mismatches == 0, "every one of the 65536 palette entries agrees");

    /*
     * A colour SURVIVES the round trip the picker puts it through.
     *
     * This is the property the config store depends on: Save writes the hex,
     * the next open reads it back, and a mapping that moved the colour would
     * drift a marker a shade per session with nothing anywhere saying why.
     * It is exactly why the picker uses Hsl16NearestRgb and not the reference
     * quantiser -- which fails this for 63813 of the 65536 entries, and was
     * measured doing so rather than assumed.
     */
    {
        int drifted = 0;
        int reference_drifted = 0;
        for( int hsl = 0; hsl < 65536; hsl++ )
        {
            uint32_t const rgb = ToriRSChrome_Hsl16ToRgb(hsl);
            if( ToriRSChrome_Hsl16ToRgb(ToriRSChrome_Hsl16NearestRgb(rgb)) != rgb )
                drifted++;
            if( ToriRSChrome_Hsl16ToRgb(ToriRSChrome_Hsl16FromRgb(rgb)) != rgb )
                reference_drifted++;
        }
        VT_ASSERT(drifted == 0, "every palette entry survives rgb -> hsl16 -> rgb");
        /* Pinned so the difference between the two conversions stays a stated
         * fact rather than folklore -- and so that a future "simplification"
         * back onto the reference quantiser fails here instead of in a
         * settings panel six months later. */
        VT_ASSERT(
            reference_drifted > 0,
            "the reference quantiser is NOT that mapping, which is why both exist");
    }

    /* And the spellings a config file or a wiki page actually carries. */
    {
        uint32_t rgb = 0;
        VT_ASSERT(ToriRSChrome_ParseHexRgb("#00FFFF", &rgb) && rgb == 0x00FFFFu, "#RRGGBB");
        VT_ASSERT(ToriRSChrome_ParseHexRgb("00ffff", &rgb) && rgb == 0x00FFFFu, "bare RRGGBB");
        VT_ASSERT(ToriRSChrome_ParseHexRgb("0xFF0000", &rgb) && rgb == 0xFF0000u, "0x prefix");
        rgb = 0x123456u;
        VT_ASSERT(!ToriRSChrome_ParseHexRgb("#00FF", &rgb), "a half-typed hex is not a colour");
        VT_ASSERT(rgb == 0x123456u, "and a rejected parse leaves the out alone");
        VT_ASSERT(
            !ToriRSChrome_ParseHexRgb("#00FFFFF", &rgb), "seven digits is not six digits");
    }
}

/*
 * The colour row and its axis popup, in the in-canvas presentation.
 *
 * The three bars ARE the HSL16 axes -- 64 hues, 8 saturations, 128
 * lightnesses -- so what this checks is that a sweep along one moves that axis
 * and leaves the other two where they were. A picker that quietly renormalised
 * the whole colour on every drag would look identical in the shot.
 */
static void
visual_colorpick(void)
{
    int panel;
    int pick;
    int hue;
    int sat;
    int lum;

    printf("VISUAL: HSL16 colour picker\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 260, "Tile markers");
    pick = ToriRSChrome_ColorPick(
        &g_ui, panel, "True tile colour", ToriRSChrome_Hsl16FromRgb(0x00FFFFu));
    ToriRSChrome_ColorPick(
        &g_ui, panel, "Destination colour", ToriRSChrome_Hsl16FromRgb(0xFFFF00u));
    ToriRSChrome_TextInput(&g_ui, panel, "True tile fill", "40");
    ToriRSChrome_Build(&g_ui);
    render("28_colorpick_closed");

    /* The field shows the palette entry the colour landed on, not the colour
     * that was asked for -- which is the whole reason to pick on these axes:
     * the quantisation is visible where it happens instead of at the far end
     * of the pipeline. */
    VT_ASSERT(
        ToriRSChrome_ColorPickValue(&g_ui, pick) == ToriRSChrome_Hsl16FromRgb(0x00FFFFu) ||
            ToriRSChrome_ColorPickValue(&g_ui, pick) >= 0,
        "the picker holds a palette entry");
    VT_ASSERT(
        ToriRSChrome_Text(&g_ui, pick)[0] == '#' &&
            strlen(ToriRSChrome_Text(&g_ui, pick)) == 7,
        "and the field shows it as a six-digit hex");

    /*
     * The row's two zones. Everything up to the swatch -- the label included
     * -- opens the axis popup; the hex to its right takes the focus.
     *
     * The LABEL counting as the swatch is deliberate, and the same rule the
     * roster row follows: a row whose largest target does nothing reads as
     * broken. The alternative is an eleven-pixel square being the only way to
     * open a picker.
     */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        int const sy = w->y + w->h / 2;
        ToriRSChrome_MouseDown(&g_ui, w->x + 2, sy);
        ToriRSChrome_MouseUp(&g_ui, w->x + 2, sy);
        VT_ASSERT(
            ToriRSChrome_ColorPickIsOpen(&g_ui, pick), "the label half opens the popup");
        VT_ASSERT(g_ui.focus != pick, "and does not also put a caret in the field");
        ToriRSChrome_ColorPickSetOpen(&g_ui, pick, 0);
    }
    ToriRSChrome_ColorPickSetOpen(&g_ui, pick, 1);
    ToriRSChrome_Build(&g_ui);
    render("29_colorpick_open");
    VT_ASSERT(ToriRSChrome_ColorPickIsOpen(&g_ui, pick), "the popup is up");
    VT_ASSERT(
        ToriRSChrome_Checked(&g_ui, pick),
        "and says so through `checked`, which is what crosses the executor seam");

    /* A sweep along the HUE bar. The bars sit under the field box, in order. */
    ToriRSChrome_Hsl16Split(ToriRSChrome_ColorPickValue(&g_ui, pick), &hue, &sat, &lum);
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        int const bar_y = w->y + w->h + 2 + 6;
        int const x0 = w->x + w->w / 2;
        int after_hue;
        int after_sat;
        int after_lum;

        ToriRSChrome_MouseDown(&g_ui, x0, bar_y);
        ToriRSChrome_MouseMove(&g_ui, x0 + 20, bar_y);
        ToriRSChrome_MouseUp(&g_ui, x0 + 20, bar_y);
        ToriRSChrome_Hsl16Split(
            ToriRSChrome_ColorPickValue(&g_ui, pick), &after_hue, &after_sat, &after_lum);
        VT_ASSERT(after_hue != hue, "a sweep along the hue bar moved the hue");
        VT_ASSERT(after_sat == sat, "and left the saturation alone");
        VT_ASSERT(after_lum == lum, "and the lightness");
        VT_ASSERT(
            ToriRSChrome_TakeActivated(&g_ui) == pick,
            "the sweep reports through the ordinary activation latch");
    }
    ToriRSChrome_Build(&g_ui);
    render("30_colorpick_swept");

    /* A typed hex commits on Enter, and comes back as the entry it landed on. */
    ToriRSChrome_ColorPickSetOpen(&g_ui, pick, 0);
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        int const fx = w->x + w->w - 6;
        int const fy = w->y + w->h / 2;
        ToriRSChrome_MouseDown(&g_ui, fx, fy);
        ToriRSChrome_MouseUp(&g_ui, fx, fy);
        VT_ASSERT(g_ui.focus == pick, "a click in the field half takes the focus");

        for( int i = 0; i < 8; i++ )
            ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
        for( char const* c = "#FF0000"; *c; c++ )
            ToriRSChrome_KeyChar(&g_ui, *c);
        VT_ASSERT(
            strcmp(ToriRSChrome_Text(&g_ui, pick), "#FF0000") == 0,
            "typing edits the field verbatim, without snapping under the caret");
        ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ENTER);
        VT_ASSERT(
            ToriRSChrome_ColorPickValue(&g_ui, pick) ==
                ToriRSChrome_Hsl16NearestRgb(0xFF0000u),
            "Enter commits the typed hex onto the nearest palette entry");
        VT_ASSERT(
            strcmp(ToriRSChrome_Text(&g_ui, pick), "#FF0000") != 0,
            "and rewrites the field to the entry, rather than keeping the spelling");
    }
    ToriRSChrome_Build(&g_ui);
    render("31_colorpick_typed");

    /* Escape abandons instead of committing -- the one difference between the
     * two ways out of an edit. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        int const before = ToriRSChrome_ColorPickValue(&g_ui, pick);
        char kept[TORIRS_CHROME_INPUT_MAX];

        snprintf(kept, sizeof(kept), "%s", ToriRSChrome_Text(&g_ui, pick));
        ToriRSChrome_MouseDown(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        ToriRSChrome_MouseUp(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        for( int i = 0; i < 8; i++ )
            ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
        for( char const* c = "#00FF00"; *c; c++ )
            ToriRSChrome_KeyChar(&g_ui, *c);
        ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ESCAPE);
        VT_ASSERT(
            ToriRSChrome_ColorPickValue(&g_ui, pick) == before,
            "Escape leaves the value where it was");
        VT_ASSERT(
            strcmp(ToriRSChrome_Text(&g_ui, pick), kept) == 0,
            "and puts the field back to it");
    }

    /* A blur COMMITS, which is the other half of the same rule: a hex typed
     * and then abandoned by clicking elsewhere must not leave the field
     * disagreeing with its own swatch. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        ToriRSChrome_MouseDown(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        ToriRSChrome_MouseUp(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        for( int i = 0; i < 8; i++ )
            ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
        for( char const* c = "#0000FF"; *c; c++ )
            ToriRSChrome_KeyChar(&g_ui, *c);
        /* Somewhere with no widget: the panel's own header strip. */
        ToriRSChrome_MouseDown(&g_ui, w->x, g_ui.panels[panel].y + 2);
        ToriRSChrome_MouseUp(&g_ui, w->x, g_ui.panels[panel].y + 2);
        VT_ASSERT(
            ToriRSChrome_ColorPickValue(&g_ui, pick) ==
                ToriRSChrome_Hsl16NearestRgb(0x0000FFu),
            "a blur commits what was typed");
    }

    /* Garbage in the field is refused and the field put back, rather than
     * being written into the config the next Save reads. */
    {
        struct ToriRSChromeWidget const* w = &g_ui.widgets[pick];
        int const before = ToriRSChrome_ColorPickValue(&g_ui, pick);
        ToriRSChrome_MouseDown(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        ToriRSChrome_MouseUp(&g_ui, w->x + w->w - 6, w->y + w->h / 2);
        for( int i = 0; i < 8; i++ )
            ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_BACKSPACE);
        for( char const* c = "nonsense"; *c; c++ )
            ToriRSChrome_KeyChar(&g_ui, *c);
        ToriRSChrome_KeyEdit(&g_ui, TORIRS_CHROME_KEY_ENTER);
        VT_ASSERT(
            ToriRSChrome_ColorPickValue(&g_ui, pick) == before,
            "an unparseable field does not move the value");
        VT_ASSERT(
            ToriRSChrome_Text(&g_ui, pick)[0] == '#',
            "and the field is put back to the value's own hex");
    }
    ToriRSChrome_Init(&g_ui);
}

/*
 * A panel's Close, and the fact that it is the ONLY thing in the title bar.
 *
 * Opt-in, because most panels here are developer tools with a hotkey. The
 * plugin window is the one a player uses, and in the CS2 presentation -- a
 * column of game components with no window furniture of its own -- it opened
 * and then had no way to shut.
 *
 * There was an Ok beside it that fired the page's Save row on the way out.
 * It is gone, and the assertions below say so in the one way that matters:
 * closing commits NOTHING, and the space Ok occupied belongs to the title bar
 * again.
 */
static void
visual_panel_close(void)
{
    int panel;
    int save;

    printf("VISUAL: panel Close\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 220, "Plugins");
    ToriRSChrome_Checkbox(&g_ui, panel, "live preview", 1);
    save = ToriRSChrome_Button(&g_ui, panel, "Save");
    ToriRSChrome_PanelSetClosable(&g_ui, panel, 1);
    ToriRSChrome_Build(&g_ui);
    render("32_panel_close");

    {
        struct ToriRSChromeRect const box = ToriRSChrome_PanelRect(&g_ui, panel);
        int const bar = ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, g_ui.scale);
        int const side = bar - 2;
        int const close_x = box.x + box.w - 2 - side / 2;
        /* Where the Ok used to be -- one button's width further in. */
        int const was_ok_x = close_x - side - 1;
        int const by = box.y + 2 + side / 2;

        /* Close dismisses, and fires nothing: closing a form DISCARDS. */
        ToriRSChrome_MouseDown(&g_ui, close_x, by);
        ToriRSChrome_MouseUp(&g_ui, close_x, by);
        VT_ASSERT(!g_ui.panels[panel].visible, "Close dismisses the panel");
        VT_ASSERT(
            ToriRSChrome_TakeActivated(&g_ui) < 0, "and commits nothing on the way out");

        /*
         * And Ok is GONE, not merely undrawn. The slot it held is title bar
         * again -- which is a drag handle, so a press there picks the window up
         * instead of committing the page.
         */
        ToriRSChrome_PanelSetVisible(&g_ui, panel, 1);
        ToriRSChrome_Build(&g_ui);
        ToriRSChrome_MouseDown(&g_ui, was_ok_x, by);
        VT_ASSERT(
            g_ui.drag_panel == panel, "where Ok stood is title bar, and drags the window");
        ToriRSChrome_MouseUp(&g_ui, was_ok_x, by);
        VT_ASSERT(
            ToriRSChrome_TakeActivated(&g_ui) != save, "nothing there fires the Save row");
        VT_ASSERT(g_ui.panels[panel].visible, "and nothing there closes the panel");
        (void)save;
    }

    /*
     * The skinned pair, and the whole point of it: Close is the interfaces'
     * WINDOW X, not the red cross that answers "no" to a checkbox.
     *
     * The two baked images are one button lit from opposite corners, so the
     * hover is a change of PICTURE rather than an outline laid over one. Every
     * assertion below is about that swap, because a swap is the thing that can
     * silently not happen: the buttons are panel chrome rather than widgets, so
     * nothing in the widget hover path marks them dirty, and art that changes
     * on a frame nobody rebuilds is art that never changes at all.
     */
    {
        int const bar = ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, g_ui.scale);
        int const side = bar - 2;
        struct ToriRSChromeRect box;
        struct ToriRSChromeRect close_box;
        struct ToriRSChromeRect ok_box;
        uint32_t rest_close;
        uint32_t rest_ok;

        ToriRSChrome_Init(&g_ui);
        g_ui.skin_avail = (1u << TORIRS_CHROME_SKIN_CHECK_ON) |
                          (1u << TORIRS_CHROME_SKIN_CLOSE) |
                          (1u << TORIRS_CHROME_SKIN_CLOSE_OVER);
        panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 220, "Plugins");
        ToriRSChrome_Checkbox(&g_ui, panel, "live preview", 1);
        save = ToriRSChrome_Button(&g_ui, panel, "Save");
        ToriRSChrome_PanelSetClosable(&g_ui, panel, 1);
        ToriRSChrome_Build(&g_ui);
        box = ToriRSChrome_PanelRect(&g_ui, panel);

        /* dbg_panel_button_box's own arithmetic at scale 1, where DBG_RULE is
         * 1: Close outermost, Ok one gap inside it. */
        close_box.x = box.x + box.w - 2 - side;
        close_box.y = box.y + 2;
        close_box.w = side;
        close_box.h = side;
        /* The slot Ok used to hold: bare title bar now, and it has to STAY
         * bare when the cursor moves onto Close beside it. */
        ok_box = close_box;
        ok_box.x = close_box.x - side - 1;

        render("33_panel_close_skinned");
        rest_close = box_checksum(close_box);
        rest_ok = box_checksum(ok_box);

        VT_ASSERT(
            !skin_hue_is_green(TORIRS_CHROME_SKIN_CLOSE),
            "the close slot is not the tick");
        VT_ASSERT(
            !skin_reads_red(TORIRS_CHROME_SKIN_CLOSE),
            "nor the red cross it replaced -- it is the olive window button");
        VT_ASSERT(
            skin_slots_differ(TORIRS_CHROME_SKIN_CLOSE, TORIRS_CHROME_SKIN_CLOSE_OVER),
            "and the hover slot is a different image from the resting one");

        /*
         * The cursor arrives. Build must REPORT work -- that return is the
         * repaint, and a 0 here is the bug this hover tracking exists to stop.
         */
        ToriRSChrome_MouseMove(&g_ui, close_box.x + side / 2, close_box.y + side / 2);
        VT_ASSERT(
            ToriRSChrome_Build(&g_ui) != 0,
            "the pointer landing on Close dirties the panel");
        render("34_panel_close_hover");
        VT_ASSERT(
            box_checksum(close_box) != rest_close,
            "and Close is drawn with its other picture");
        VT_ASSERT(
            box_checksum(ok_box) == rest_ok,
            "while the bar beside it, where Ok used to be, is untouched");

        /* And back off it again, which is the half that a latch with no clear
         * would leave stuck pressed. */
        ToriRSChrome_MouseMove(&g_ui, box.x + 4, box.y + box.h - 4);
        VT_ASSERT(ToriRSChrome_Build(&g_ui) != 0, "leaving it dirties the panel too");
        render("35_panel_close_unhover");
        VT_ASSERT(
            box_checksum(close_box) == rest_close, "and Close goes back to resting");
    }

    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 16, 220, "Plugins");
    ToriRSChrome_Label(&g_ui, panel, "spacer");
    ToriRSChrome_Build(&g_ui);

    /* A panel that never asked for them has neither, and its title bar is
     * still a drag handle all the way to its right edge. */
    {
        int plain = ToriRSChrome_PanelAdd(
            &g_ui, TORIRS_CHROME_PANEL_WINDOW, 20, 140, 220, "Developer");
        struct ToriRSChromeRect box;
        ToriRSChrome_Label(&g_ui, plain, "fps 60");
        ToriRSChrome_Build(&g_ui);
        box = ToriRSChrome_PanelRect(&g_ui, plain);
        ToriRSChrome_MouseDown(&g_ui, box.x + box.w - 4, box.y + 4);
        VT_ASSERT(
            g_ui.drag_panel == plain,
            "a panel with no buttons drags from the whole of its title bar");
        ToriRSChrome_MouseUp(&g_ui, box.x + box.w - 4, box.y + 4);
        VT_ASSERT(g_ui.panels[plain].visible, "and cannot be closed by clicking there");
    }
    ToriRSChrome_Init(&g_ui);
}

int
main(void)
{
    g_scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    if( !g_scene )
    {
        fprintf(stderr, "FAIL: could not create a scene\n");
        return 1;
    }

    /* Soft3D never sees TORIRSRC_FONT_LOAD (it is a no-op there), so the baked
     * faces are registered here. They are statically allocated and must never
     * reach ToriDraw_FontFree, so the scene is deliberately not freed. */
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_SMALL, ToriRSChromeFont_Small());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_MENU, ToriRSChromeFont_Menu());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_BODY, ToriRSChromeFont_Body());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_SMALL, 2), ToriRSChromeFont_Small2x());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_MENU, 2), ToriRSChromeFont_Menu2x());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_BODY, 2), ToriRSChromeFont_Body2x());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_SMALL, 3), ToriRSChromeFont_Small3x());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_MENU, 3), ToriRSChromeFont_Menu3x());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_AT(FONT_ID_BODY, 3), ToriRSChromeFont_Body3x());

    /* The baked skin, uploaded the same way the scene bridge uploads it in the
     * real client: one multi-frame entry, atlas index == skin slot. Pointing
     * straight at the const arrays is safe here only because this scene is
     * never torn down. */
    {
        int const n = ToriRSChromeSkin_Count();
        struct ToriDraw_Sprite** sprites = calloc((size_t)n, sizeof(*sprites));
        for( int i = 0; i < n; i++ )
        {
            struct ToriRSChromeSkin_Sprite const* baked = ToriRSChromeSkin_Get(i);
            struct ToriDraw_Sprite* spr = calloc(1, sizeof(*spr));
            spr->width = baked->w;
            spr->height = baked->h;
            spr->crop_width = baked->w;
            spr->crop_height = baked->h;
            spr->pixels_argb = (uint32_t*)baked->argb;
            sprites[i] = spr;
        }
        ToriDraw_SceneSpriteAdd(g_scene, SKIN_SCENE_ID, sprites, n);
    }

    visual_bordered_background();
    visual_menu();
    visual_checkbox();
    visual_checkbox_skinned();
    visual_checkbox_style_box();
    visual_textinput();
    visual_textarea();
    visual_damage();
    visual_clipping();
    visual_kitchen_sink();
    visual_skin();
    visual_panel_drag();
    visual_panel_resize();
    visual_dropdown();
    visual_dropdown_scrollbar_drag();
    visual_dropdown_fallbacks();
    visual_menubar_dropdown();
    visual_tabs_and_scroll();
    visual_listrow();
    visual_listrow_skinned();
    visual_hsl16_palette();
    visual_colorpick();
    visual_panel_close();
    visual_scaled();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All debug-overlay visual tests passed.\n");
    return 0;
}
