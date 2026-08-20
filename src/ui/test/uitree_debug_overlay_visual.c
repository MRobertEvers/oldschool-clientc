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

#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_emit.h"

#include "engine/torirs_chrome_skin_baked.h"
#include "engine/torirs_debug_font_baked.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"

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
#define CANVAS_H 260

/** Slot -> scene font id. Arbitrary local handles; see the file comment. */
#define FONT_ID_SMALL 0
#define FONT_ID_MENU 1
#define FONT_ID_BODY 2
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
    desc.debug_font_id[TORIDBG_FONT_SMALL] = FONT_ID_SMALL;
    desc.debug_font_id[TORIDBG_FONT_MENU] = FONT_ID_MENU;
    desc.debug_font_id[TORIDBG_FONT_BODY] = FONT_ID_BODY;
    desc.debug_skin_scene_id = SKIN_SCENE_ID;
    for( int i = 0; i < TORIDBG_SKIN_SLOT_COUNT; i++ )
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
mark_rect(struct ToriDbgRect r, uint32_t color)
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
    struct ToriDbgTheme const* t = &g_ui.theme;
    struct ToriDbgRect r;
    int panel;

    printf("VISUAL: bordered background\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 24, 20, 0, "Renderer");
    ToriRSChrome_Label(&g_ui, panel, "backend  soft3d");
    ToriRSChrome_Label(&g_ui, panel, "canvas   400x260");
    ToriRSChrome_Separator(&g_ui, panel);
    ToriRSChrome_LabelColored(&g_ui, panel, "vsync    off", t->accent);
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("01_bordered_background");

    VT_ASSERT(r.w > 0 && r.h > 0, "panel resolved a box");
    VT_ASSERT(px(r.x, r.y) == t->panel_border, "top-left corner is border");
    VT_ASSERT(px(r.x + r.w - 1, r.y + r.h - 1) == t->panel_border, "bottom-right is border");
    VT_ASSERT(px(r.x + r.w / 2, r.y + r.h - 1) == t->panel_border, "bottom edge is border");
    /* One pixel in: body, not border. That is the outline being 1px wide. */
    VT_ASSERT(px(r.x + 1, r.y + r.h - 2) == t->panel_body, "border is 1px, body underneath");
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
    struct ToriDbgTheme const* t = &g_ui.theme;
    struct ToriDbgRect r;
    int panel;
    int rows[4];
    int hovered_band;
    int plain_band;

    printf("VISUAL: menu-like interface\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_MENU, 40, 30, 0, "Choose Option");
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
    struct ToriDbgTheme const* t = &g_ui.theme;
    struct ToriDbgWidget const* on;
    struct ToriDbgWidget const* off;
    int panel;
    int w_on;
    int w_off;

    printf("VISUAL: checkboxes\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 30, 30, 170, "Toggles");
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
    struct ToriDbgTheme const* t = &g_ui.theme;
    struct ToriDbgWidget const* focused;
    struct ToriDbgWidget const* plain;
    int panel;
    int w_focus;
    int w_plain;
    int caret_on;
    int caret_off;

    printf("VISUAL: text input\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 30, 30, 260, "Console");
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
    VT_ASSERT(
        count_eq(plain->x, plain->y, plain->w, plain->h, t->input_border) > 0,
        "unfocused field keeps the plain border");
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
    struct ToriDbgRect before;
    struct ToriDbgRect after;
    struct ToriDbgRect dmg;
    int panel;

    printf("VISUAL: damage rectangles\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 40, 40, 150, "Moves");
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
    VT_ASSERT(px(after.x, after.y) == g_ui.theme.panel_border, "the new position is painted");

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
    struct ToriDbgRect r;
    int panel;

    printf("VISUAL: content clipped to the panel\n");
    ToriRSChrome_Init(&g_ui);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 30, 40, 90, "Narrow");
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
    struct ToriDbgRect r_menu;

    printf("VISUAL: kitchen sink\n");
    ToriRSChrome_Init(&g_ui);

    stats = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 8, 8, 150, "Stats");
    ToriRSChrome_Label(&g_ui, stats, "fps      60");
    ToriRSChrome_Label(&g_ui, stats, "prims    128");
    ToriRSChrome_LabelColored(&g_ui, stats, "draws    41", g_ui.theme.accent);
    ToriRSChrome_Separator(&g_ui, stats);
    ToriRSChrome_Label(&g_ui, stats, "cache    osrs239");

    toggles = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 8, 120, 190, "Debug");
    ToriRSChrome_Checkbox(&g_ui, toggles, "wireframe", 0);
    ToriRSChrome_Checkbox(&g_ui, toggles, "show hitboxes", 1);
    ToriRSChrome_Separator(&g_ui, toggles);
    input = ToriRSChrome_TextInput(&g_ui, toggles, "goto", "3222 3218");

    menu = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_MENU, 215, 60, 0, "Choose Option");
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
 * The skin path end to end: TORIDBG_PRIM_SPRITE out of the chrome, through the
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
    struct ToriDbgRect r;
    int panel;
    int flat_body_px;
    int skinned_body_px;

    printf("VISUAL: baked OSRS skin\n");

    /* No skin available: the body is the theme's flat fill. */
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = 0;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 24, 20, 160, "Map Editor");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Label(&g_ui, panel, "u50 o10;1;3 f4");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("10_skin_off");
    flat_body_px = count_eq(r.x + 1, r.y + r.h - 6, r.w - 2, 4, g_ui.theme.panel_body);
    VT_ASSERT(flat_body_px > 0, "no skin: body is the flat theme fill");

    /* Same panel, skin available: the parchment covers the flat fill. */
    ToriRSChrome_Init(&g_ui);
    g_ui.skin_avail = 1u << TORIDBG_SKIN_PANEL_BODY;
    g_ui.skin_tile_w = ToriRSChromeSkin_Get(TORIDBG_SKIN_PANEL_BODY)->w;
    g_ui.skin_tile_h = ToriRSChromeSkin_Get(TORIDBG_SKIN_PANEL_BODY)->h;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 24, 20, 160, "Map Editor");
    ToriRSChrome_Label(&g_ui, panel, "m50_50  tile 12,40");
    ToriRSChrome_Label(&g_ui, panel, "u50 o10;1;3 f4");
    ToriRSChrome_Build(&g_ui);
    r = ToriRSChrome_PanelRect(&g_ui, panel);
    render("11_skin_on");
    skinned_body_px = count_eq(r.x + 1, r.y + r.h - 6, r.w - 2, 4, g_ui.theme.panel_body);

    VT_ASSERT(
        skinned_body_px < flat_body_px,
        "skin on: the parchment replaced flat-fill pixels");
    /* The border still wins: the skin tiles under the chrome, not over it. */
    VT_ASSERT(px(r.x, r.y) == g_ui.theme.panel_border, "border still drawn over the skin");
    VT_ASSERT(
        px(r.x + 1, r.y + 1) == g_ui.theme.panel_title_bg, "title bar still drawn over the skin");
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
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_SMALL, ToriDbgFont_Small());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_MENU, ToriDbgFont_Menu());
    ToriDraw_SceneFontAdd(g_scene, FONT_ID_BODY, ToriDbgFont_Body());

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
    visual_textinput();
    visual_damage();
    visual_clipping();
    visual_kitchen_sink();
    visual_skin();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All debug-overlay visual tests passed.\n");
    return 0;
}
