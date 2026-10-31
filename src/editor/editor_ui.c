#include "editor_ui.h"

#include "editor.h"
#include "builder/builder_panels.h"
#include "iface/iface_panels.h"
#include "live/live_panel.h"
#include "log/torirs_log.h"
#include "panels/edpanel_common.h"
#include "panels/edpanels.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* A view frame nobody has restated for this long is a window that closed. */
#define EDITOR_UI_FRAME_EXPIRE_MS 15000
/* Restate this window's frame this often, so a picker that joined late (or
 * expired it) gets it back. */
#define EDITOR_UI_FRAME_HEARTBEAT_MS 5000

static const char* const k_tool_names[EDITOR_TOOL_COUNT] = {
    "Select",       "Rectangle select", "Raise",        "Lower",
    "Flatten",      "Smooth",           "Paint underlay", "Paint overlay",
    "Paint flags",  "Place",            "Move",         "Rotate",
    "Delete",
};

const char*
Editor_ToolName(enum Editor_Tool tool)
{
    assert(tool >= 0);
    assert(tool < EDITOR_TOOL_COUNT);
    return k_tool_names[tool];
}

int
Editor_ToolIsBrush(enum Editor_Tool tool)
{
    return tool == EDITOR_TOOL_RAISE || tool == EDITOR_TOOL_LOWER || tool == EDITOR_TOOL_FLATTEN
           || tool == EDITOR_TOOL_SMOOTH || tool == EDITOR_TOOL_PAINT_UNDERLAY
           || tool == EDITOR_TOOL_PAINT_OVERLAY || tool == EDITOR_TOOL_PAINT_FLAGS;
}

void
Editor_UiInit(
    struct EditorUi* ui,
    struct ToriRSChrome* chrome,
    struct Editor* editor)
{
    assert(ui);
    assert(chrome);

    memset(ui, 0, sizeof(*ui));
    ui->chrome = chrome;
    ui->editor = editor;

    ui->tools.tool = EDITOR_TOOL_SELECT;
    ui->tools.brush.radius = 2;
    ui->tools.brush.falloff = EDITOR_BRUSH_FALLOFF_SMOOTH;
    ui->tools.brush.step = 2;
    ui->tools.brush.strength = 50;
    ui->tools.underlay_id = 0;
    ui->tools.overlay_none = 1;
    ui->tools.edit_level = -1;
    ui->tools.vis_level = -1;
    ui->tools.place_shape = 10;
    ui->tools.pick_kind = -1;
    ui->tools.pick_id = -1;

    /* Any number other views are unlikely to pick: the address of this
     * window's registry mixed with the clock. Positive, never 0 (0 is what a
     * publisher that sends no slot is filed under). */
    {
        uint32_t mix = (uint32_t)(uintptr_t)ui ^ (uint32_t)time(NULL) * 2654435761u;
        ui->view_slot = (int)(mix & 0x3fffffff) | 1;
    }
    ui->view_published[0] = -1;
    ui->snap = 1;
    ui->world_panel = -1;
}

void
Editor_UiFree(struct EditorUi* ui)
{
    assert(ui);

    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct EditorUiPanel* panel = &ui->panels[i];
        if( panel->destroy )
            panel->destroy(ui, panel->self);
        panel->self = NULL;
    }
    ui->panel_count = 0;
    edp_scratch_free();
    edp_actions_free();
}

int
Editor_UiAddPanel(
    struct EditorUi* ui,
    const struct EditorUiPanel* panel)
{
    assert(ui);
    assert(panel);
    assert(panel->name);
    assert(ui->panel_count < EDITOR_UI_PANELS_MAX);
    assert(!Editor_UiFindPanel(ui, panel->name));

    ui->panels[ui->panel_count] = *panel;
    return ui->panel_count++;
}

struct EditorUiPanel*
Editor_UiFindPanel(
    struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);

    for( int i = 0; i < ui->panel_count; i++ )
        if( strcmp(ui->panels[i].name, name) == 0 )
            return &ui->panels[i];
    return NULL;
}

void*
Editor_UiPanelSelf(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(ui, name);
    return panel ? panel->self : NULL;
}

/* ---- the World panel --------------------------------------------------------
 *
 * The world view as a panel like the others: a titled, resizable, HOLLOW
 * chrome window (ToriRSChromePanel::hollow) -- the chrome draws its title bar,
 * rails and grip and takes the pointer there, while the body is neither
 * painted nor hit, so the 3D view the App draws under it shows through and
 * every press, drag, wheel and hover in it reaches the world (camera, picks,
 * tools) exactly as before. The default layout docks it over the region the
 * other panels leave; View > World hides it (and with it the world). It has
 * no widgets and no per-frame work: the module is its geometry.
 */
static void
ui_world_destroy(
    struct EditorUi* ui,
    void* self)
{
    (void)ui;
    (void)self;
}

static void
ui_world_register(struct EditorUi* ui)
{
    struct EditorUiPanel desc;
    int panel;

    panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_RAIL_W), edp_px(ui, 24), edp_px(ui, 640), "World");
    assert(panel >= 0);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, panel, edp_px(ui, 480));
    ToriRSChrome_PanelSetResizable(ui->chrome, panel, 1);
    ToriRSChrome_PanelSetHollow(ui->chrome, panel, 1);

    memset(&desc, 0, sizeof(desc));
    desc.name = "world";
    desc.title = "World";
    desc.chrome_panel = panel;
    desc.destroy = ui_world_destroy;
    Editor_UiAddPanel(ui, &desc);
    ui->world_panel = panel;
}

void
Editor_UiRegisterToolsPanels(struct EditorUi* ui)
{
    assert(ui);
    ui->panel_first = ui->chrome->panel_count;

    /* The World panel first: the lowest chrome panel draws under every other
     * one, so a panel dragged over the world sits on top of it. */
    ui_world_register(ui);

    /* Order is the View menu's order and the dispatch order. The console goes
     * first so every later Register can already log into it; the modal pair
     * goes last so a modal's panel sits above the panels it guards. */
    Editor_Console_Register(ui);
    Editor_MenuBar_Register(ui);
    Editor_ToolRail_Register(ui);
    Editor_Terrain_Register(ui);
    Editor_Palette_Register(ui);
    Editor_RectSelect_Register(ui);
    Editor_SquarePicker_Register(ui);
    Editor_Locs_Register(ui);
    Editor_Spawns_Register(ui);
    if( ui->app )
    {
        Editor_Catalog_Register(ui);
        Editor_Inspector_Register(ui);
    }
    Editor_NewSquare_Register(ui);
    Editor_SaveGuard_Register(ui);
    /* The other phase-3 owners' modules: the interaction builders and the
     * shell editor (editor/builder/), the interface editor and the CS2 tools
     * (editor/iface/). They start hidden; the View menu opens them. The
     * views' thin toolbar is not a registry panel (editor_view.h). */
    Editor_LocBuilder_Register(ui);
    Editor_NpcBuilder_Register(ui);
    Editor_ShellEditor_Register(ui);
    Editor_Iface_Register(ui);
    Editor_Cs2_Register(ui);
    /* Phase 4: the Live panel (editor/live/), hidden until the View menu
     * opens it; it talks to the editor server's live link. */
    Editor_Live_Register(ui);
    Editor_History_Register(ui);
    /* Named areas and square collections (the AREAS document). */
    Editor_Areas_Register(ui);
    ui->panel_end = ui->chrome->panel_count;
    ui->layout_scale = ToriRSChrome_Scale(ui->chrome);
}

void
Editor_UiSetVisible(
    struct EditorUi* ui,
    int visible)
{
    assert(ui);

    ui->visible = visible ? 1 : 0;
    /* Showing every titled panel undoes what the layout hid: lay out again. */
    if( ui->visible )
        ui->laid_out = 0;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorUiPanel* panel = &ui->panels[i];
        if( panel->chrome_panel >= 0 && panel->title )
            ToriRSChrome_PanelSetVisible(ui->chrome, panel->chrome_panel, ui->visible);
        if( panel->set_visible )
            panel->set_visible(ui, panel->self, ui->visible);
    }
}

/* ---- view frames ----------------------------------------------------------- */

static void
ui_frames_expire(struct EditorUi* ui)
{
    int64_t const now = ui->chrome->now_ms;

    if( now <= 0 )
        return;
    for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
        if( ui->frames[i].used && ui->frames[i].slot != ui->view_slot
            && now - ui->frames[i].seen_ms > EDITOR_UI_FRAME_EXPIRE_MS )
            ui->frames[i].used = 0;
}

static void
ui_frame_note(
    struct EditorUi* ui,
    int slot,
    int map_x,
    int map_z,
    int w,
    int h)
{
    int free_at = -1;
    int oldest = 0;

    for( int i = 0; i < EDITOR_UI_VIEW_FRAMES_MAX; i++ )
    {
        if( ui->frames[i].used && ui->frames[i].slot == slot )
        {
            free_at = i;
            break;
        }
        if( !ui->frames[i].used && free_at < 0 )
            free_at = i;
        if( ui->frames[i].seen_ms < ui->frames[oldest].seen_ms )
            oldest = i;
    }
    if( free_at < 0 )
        free_at = oldest;
    ui->frames[free_at].used = 1;
    ui->frames[free_at].slot = slot;
    ui->frames[free_at].map_x = map_x;
    ui->frames[free_at].map_z = map_z;
    ui->frames[free_at].w = w > 0 ? w : 1;
    ui->frames[free_at].h = h > 0 ? h : 1;
    ui->frames[free_at].seen_ms = ui->chrome->now_ms;
}

/** This window's own frame: kept locally (it is ours whether or not the
 *  server echoes it) and published when it moved, or on the heartbeat. */
static void
ui_publish_view(struct EditorUi* ui)
{
    int map_x;
    int map_z;
    int w;
    int h;
    int64_t const now = ui->chrome->now_ms;

    if( !ui->hooks.view_window || !ui->hooks.view_window(ui->hooks_user, &map_x, &map_z, &w, &h) )
        return;
    ui_frame_note(ui, ui->view_slot, map_x, map_z, w, h);

    if( ui->view_published[0] == map_x && ui->view_published[1] == map_z
        && ui->view_published[2] == w && ui->view_published[3] == h
        && (now <= 0 || now - ui->view_published_ms < EDITOR_UI_FRAME_HEARTBEAT_MS) )
        return;

    {
        int32_t values[5] = { map_x, map_z, w, h, ui->view_slot };
        if( Editor_UiPublish(ui, TORIRSMAPED_STATE_VIEW_SQUARE, values, 5) || !ui->editor )
        {
            ui->view_published[0] = map_x;
            ui->view_published[1] = map_z;
            ui->view_published[2] = w;
            ui->view_published[3] = h;
            ui->view_published_ms = now;
        }
    }
}

void
Editor_UiPublishView(struct EditorUi* ui)
{
    assert(ui);
    ui_frames_expire(ui);
    ui_publish_view(ui);
}

/* ---- the default layout --------------------------------------------------
 *
 * The World panel (module "world", the 3D view) takes whatever the other
 * panels leave, so the layout is a statement about where they do NOT go: one column against the right edge, the tool
 * rail against the left, the menu bar across the top, the console along the
 * bottom of the world when it is short enough. Nothing is placed over the
 * world, and the column never grows past what keeps the world the largest
 * rectangle on the surface. It is computed from the surface, not from fixed
 * origins, on the first frame and again on every surface or scale change;
 * what the user dragged keeps its place (ToriRSChromePanel::user_placed)
 * until Reset layout.
 */
_Static_assert(EDITOR_UI_LAYOUT_PANELS_MAX >= TORIRS_CHROME_MAX_PANELS,
    "the layout tracks every chrome window");

/** The column, top-down. Locs, Spawns and Rectangle open on demand; when one
 *  is open at a layout it takes its place here, else it opens beside the
 *  column. */
static const char* const k_dock_order[] = {
    "terrain", "palette", "locs", "spawns", "rect", "inspector", "squares", "areas", "catalog", "history",
};
#define UI_DOCK_COUNT ((int)(sizeof(k_dock_order) / sizeof(k_dock_order[0])))

/** The chrome panel of registry module `name`, or -1. */
static int
ui_chrome_panel(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel = Editor_UiFindPanel(ui, name);
    return panel && panel->chrome_panel >= ui->panel_first && panel->chrome_panel < ui->panel_end
               ? panel->chrome_panel
               : -1;
}

static int
ui_round_down(
    int v,
    int grid)
{
    return grid > 0 ? v / grid * grid : v;
}

/**
 * The modules size and place their windows in chrome pixels of the scale at
 * registration; the chrome stores those numbers as given. When the scale
 * changes, scale every window the modules made by the same ratio, so a panel
 * built for 1x does not clip the 2x widgets drawn inside it, then lay them out
 * again.
 */
static void
ui_follow_scale(struct EditorUi* ui)
{
    int const scale = ToriRSChrome_Scale(ui->chrome);
    int const was = ui->layout_scale;

    if( was <= 0 || scale == was )
        return;
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        struct ToriRSChromePanel const* p = &ui->chrome->panels[i];
        if( p->fixed_w > 0 )
            ToriRSChrome_PanelSetFixedWidth(ui->chrome, i, p->fixed_w * scale / was);
        if( p->fixed_h > 0 )
            ToriRSChrome_PanelSetFixedHeight(ui->chrome, i, p->fixed_h * scale / was);
        ToriRSChrome_PanelMove(ui->chrome, i, p->x * scale / was, p->y * scale / was);
    }
    ui->layout_scale = scale;
    ui->laid_out = 0;
}

/** The surface changed size since the layout: lay out again. */
static void
ui_follow_surface(struct EditorUi* ui)
{
    if( ui->laid_out
        && (ui->chrome->surface_w != ui->layout_surface_w || ui->chrome->surface_h != ui->layout_surface_h) )
        ui->laid_out = 0;
}

/** A dock candidate's heights: what it asks for and the least it takes. */
struct UiDockSlot
{
    int panel;
    struct EditorUiPanel* module;
    int natural;
    int min;
    int h;
};

/** Measure a column candidate laid out at the column's width with no fixed
 *  height (the caller ran a Build since setting both). */
static void
ui_dock_measure(
    struct EditorUi* ui,
    struct UiDockSlot* slot)
{
    int const measured = ToriRSChrome_PanelRect(ui->chrome, slot->panel).h;
    int const floor_h = edp_px(ui, EDP_DOCK_MIN_H);

    slot->natural = measured;
    slot->min = measured < floor_h ? measured : floor_h;
    if( slot->module && slot->module->stretch )
    {
        int stretch_min = 0;
        int stretch_natural = 0;
        int const now = slot->module->stretch(ui, slot->module->self, &stretch_min, &stretch_natural);
        int const base = measured - now;
        slot->natural = base + stretch_natural;
        slot->min = base + stretch_min;
    }
}

/** The modules' own default set, as they showed it with the editor: what the
 *  first layout docks and what Reset layout returns to. Taken once, before
 *  the first layout or before a saved layout changes any visibility. */
static void
ui_capture_boot(struct EditorUi* ui)
{
    struct ToriRSChrome const* chrome = ui->chrome;
    static const char* const k_strips[] = { "console", "world" };

    if( ui->booted_layout )
        return;
    for( int d = 0; d < UI_DOCK_COUNT; d++ )
    {
        int const panel = ui_chrome_panel(ui, k_dock_order[d]);
        if( panel >= 0 )
            ui->member[panel - ui->panel_first] = ui->boot_member[panel - ui->panel_first] =
                (unsigned char)(chrome->panels[panel].visible ? 1 : 0);
    }
    for( int s = 0; s < 2; s++ )
    {
        int const panel = ui_chrome_panel(ui, k_strips[s]);
        if( panel >= 0 )
            ui->member[panel - ui->panel_first] = ui->boot_member[panel - ui->panel_first] =
                (unsigned char)(chrome->panels[panel].visible ? 1 : 0);
    }
    ui->booted_layout = 1;
}

void
Editor_UiLayout(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome;
    struct UiDockSlot slots[UI_DOCK_COUNT];
    int slot_count = 0;
    int const grid = edp_px(ui, EDP_GRID);
    int surface_w;
    int surface_h;
    int top = 0;
    int left = 0;
    int world_bottom;
    int dock_w;
    int dock_x;
    int menubar;
    int rail;
    int console;
    int world;
    int avail;
    int used;

    assert(ui);
    chrome = ui->chrome;
    surface_w = chrome->surface_w;
    surface_h = chrome->surface_h;
    assert(surface_w > 0);
    assert(surface_h > 0);

    menubar = ui_chrome_panel(ui, "menubar");
    rail = ui_chrome_panel(ui, "toolrail");
    console = ui_chrome_panel(ui, "console");
    world = ui_chrome_panel(ui, "world");
    ui_capture_boot(ui);

    /* Who is in the column: the default set (and anything open now), minus
     * what the user closed or dragged out. The first layout fixes the
     * default set as what the modules showed with the editor. */
    for( int d = 0; d < UI_DOCK_COUNT; d++ )
    {
        struct EditorUiPanel* module = Editor_UiFindPanel(ui, k_dock_order[d]);
        int const panel = ui_chrome_panel(ui, k_dock_order[d]);
        int at;

        if( panel < 0 )
            continue;
        at = panel - ui->panel_first;
        ui->docked[at] = 0;
        if( chrome->panels[panel].user_placed )
            continue;
        if( !chrome->panels[panel].visible && (!ui->member[at] || ui->closed[at]) )
            continue;
        ui->member[at] = 1;
        slots[slot_count].panel = panel;
        slots[slot_count].module = module;
        slot_count++;
    }

    /* The menu bar spans the top; everything else starts below it. */
    if( menubar >= 0 && chrome->panels[menubar].visible )
    {
        ToriRSChrome_PanelSetFixedWidth(chrome, menubar, surface_w);
        ToriRSChrome_PanelMove(chrome, menubar, 0, 0);
        top = ToriRSChrome_PanelRect(chrome, menubar).h;
    }
    avail = surface_h - top;

    /* The rail against the left edge, wrapped into as many columns as its
     * buttons need to stay on the surface. */
    if( rail >= 0 && chrome->panels[rail].visible && !chrome->panels[rail].user_placed )
    {
        struct ToriRSChromePanel const* p = &chrome->panels[rail];
        int const one_w = edp_px(ui, EDP_RAIL_W);
        int const pitch = edp_px(ui, EDP_RAIL_PITCH);
        int const columns_now = p->fixed_w > one_w ? 1 + (p->fixed_w - one_w) / pitch : 1;
        int const one_h = ToriRSChrome_PanelRect(chrome, rail).h * columns_now;
        int columns = 1;

        while( columns < 4 && one_h / columns > avail )
            columns++;
        ToriRSChrome_PanelSetFixedWidth(chrome, rail, one_w + (columns - 1) * pitch);
        ToriRSChrome_PanelMove(chrome, rail, 0, top);
        left = one_w + (columns - 1) * pitch;
    }

    /* The column's width: a share of the surface, never so wide the world
     * would be narrower than it. */
    dock_w = ui_round_down(surface_w * EDP_DOCK_W_PERCENT / 100, grid);
    if( dock_w < edp_px(ui, EDP_DOCK_W_MIN) )
        dock_w = edp_px(ui, EDP_DOCK_W_MIN);
    if( dock_w > edp_px(ui, EDP_DOCK_W_MAX) )
        dock_w = edp_px(ui, EDP_DOCK_W_MAX);
    if( surface_w - left - dock_w < dock_w )
        dock_w = ui_round_down((surface_w - left) / 2, grid);
    dock_x = surface_w - dock_w;

    /* Measure every candidate at the column's width and the console at the
     * world's, content-sized: one Build answers all of them. */
    for( int i = 0; i < slot_count; i++ )
    {
        ToriRSChrome_PanelSetFixedWidth(chrome, slots[i].panel, dock_w);
        ToriRSChrome_PanelSetFixedHeight(chrome, slots[i].panel, 0);
        ToriRSChrome_PanelSetVisible(chrome, slots[i].panel, 1);
    }
    if( console >= 0 && !chrome->panels[console].user_placed )
    {
        ToriRSChrome_PanelSetFixedWidth(chrome, console, dock_x - left);
        ToriRSChrome_PanelSetFixedHeight(chrome, console, 0);
    }
    ToriRSChrome_Build(chrome);

    /* The console is a strip under the world when it takes a quarter of the
     * height or less; otherwise the world keeps the height and the console
     * waits for the View menu. */
    world_bottom = surface_h;
    if( console >= 0 && !chrome->panels[console].user_placed )
    {
        int const at = console - ui->panel_first;
        int const h = ToriRSChrome_PanelRect(chrome, console).h;
        int const wanted = chrome->panels[console].visible || (ui->member[at] && !ui->closed[at]);
        int const fits = h > 0 && h * 4 <= avail;

        ui->docked[at] = 0;
        if( wanted && fits )
        {
            ToriRSChrome_PanelSetFixedHeight(chrome, console, h);
            ToriRSChrome_PanelMove(chrome, console, left, surface_h - h);
            ToriRSChrome_PanelSetVisible(chrome, console, 1);
            world_bottom = surface_h - h;
            ui->docked[at] = 1;
            ui->member[at] = 1;
        }
        else if( wanted )
            ToriRSChrome_PanelSetVisible(chrome, console, 0);
    }

    /* Admit candidates top-down while every admitted one still gets its
     * minimum; the rest of the default set is hidden. */
    used = 0;
    for( int i = 0; i < slot_count; i++ )
    {
        ui_dock_measure(ui, &slots[i]);
        if( used + slots[i].min > avail )
        {
            ToriRSChrome_PanelSetVisible(chrome, slots[i].panel, 0);
            slots[i].h = -1;
            continue;
        }
        slots[i].h = slots[i].min;
        used += slots[i].min;
    }
    /* Then share what is left in proportion to what each still asks for
     * (so the picker's well grows with the terrain panel rather than after
     * it), each up to its asking height; a stretching panel (the picker's
     * well) takes any remainder. */
    {
        int left_over = avail - used;
        int64_t wanted = 0;
        int stretcher = -1;
        for( int i = 0; i < slot_count; i++ )
            if( slots[i].h >= 0 && slots[i].natural > slots[i].h )
                wanted += slots[i].natural - slots[i].h;
        if( wanted > 0 && left_over > 0 )
        {
            int64_t const share = wanted < left_over ? wanted : left_over;
            int given = 0;
            for( int i = 0; i < slot_count; i++ )
            {
                int extra;
                if( slots[i].h < 0 || slots[i].natural <= slots[i].h )
                    continue;
                extra = (int)((int64_t)(slots[i].natural - slots[i].h) * share / wanted);
                slots[i].h += extra;
                given += extra;
            }
            left_over -= given;
        }
        for( int i = 0; i < slot_count; i++ )
            if( slots[i].h >= 0 && slots[i].module && slots[i].module->stretch )
                stretcher = i;
        if( stretcher >= 0 && left_over > 0 )
            slots[stretcher].h += left_over;
    }
    {
        int y = top;
        for( int i = 0; i < slot_count; i++ )
        {
            int const at = slots[i].panel - ui->panel_first;
            if( slots[i].h < 0 )
                continue;
            ToriRSChrome_PanelSetFixedHeight(chrome, slots[i].panel, slots[i].h);
            /* A fixed height drops rows past the bottom unless the panel
             * scrolls; content that grows later (the inspector's rows) must
             * not vanish. */
            ToriRSChrome_PanelSetScrollable(chrome, slots[i].panel, 1);
            ToriRSChrome_PanelMove(chrome, slots[i].panel, dock_x, y);
            ui->docked[at] = 1;
            ui->closed[at] = 0;
            ui->flown[at] = 0;
            y += slots[i].h;
        }
    }

    ui->world.x = left;
    ui->world.y = top;
    ui->world.w = dock_x - left;
    ui->world.h = world_bottom - top;
    /* The World panel fills the region the others left -- the largest on the
     * surface -- unless the user placed it, or closed it. */
    if( world >= 0 && !chrome->panels[world].user_placed )
    {
        int const at = world - ui->panel_first;
        int const wanted = chrome->panels[world].visible || (ui->member[at] && !ui->closed[at]);

        ui->docked[at] = 0;
        if( wanted && ui->world.w > 0 && ui->world.h > 0 )
        {
            ToriRSChrome_PanelSetFixedWidth(chrome, world, ui->world.w);
            ToriRSChrome_PanelSetFixedHeight(chrome, world, ui->world.h);
            ToriRSChrome_PanelMove(chrome, world, ui->world.x, ui->world.y);
            ToriRSChrome_PanelSetVisible(chrome, world, 1);
            ui->docked[at] = 1;
            ui->member[at] = 1;
            ui->closed[at] = 0;
            ui->flown[at] = 0;
        }
    }
    ui->dock_x = dock_x;
    ui->dock_w = dock_w;
    ui->dock_top = top;
    ui->dock_bottom = world_bottom;
    ui->layout_surface_w = surface_w;
    ui->layout_surface_h = surface_h;
    ui->laid_out = 1;
    /* Whatever else is open now (a builder, the Live panel) opens beside
     * the column on the next tick. */
    for( int i = ui->panel_first; i < ui->panel_end && i < chrome->panel_count; i++ )
        if( !ui->docked[i - ui->panel_first] )
            ui->flown[i - ui->panel_first] = 0;
}

/**
 * Every frame, cheap (the registry, not the UI tree): note docked panels the
 * user closed, and put a module window that has just opened and is not part
 * of the layout beside the column, top-down below any already there, so it
 * covers the world and not another panel.
 */
static void
ui_layout_follow(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int y = ui->dock_top;

    /* Where the open flyouts end. */
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        struct ToriRSChromeRect r;
        if( panel < ui->panel_first || panel >= ui->panel_end || !ui->flown[panel - ui->panel_first]
            || !chrome->panels[panel].visible )
            continue;
        r = ToriRSChrome_PanelRect(chrome, panel);
        if( r.y + r.h > y )
            y = r.y + r.h;
    }
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        struct ToriRSChromePanel const* p;
        int at;
        int w;
        int x;

        if( panel < ui->panel_first || panel >= ui->panel_end )
            continue;
        at = panel - ui->panel_first;
        p = &chrome->panels[panel];
        if( !p->visible )
        {
            if( ui->docked[at] && ui->member[at] )
                ui->closed[at] = 1;
            ui->flown[at] = 0;
            continue;
        }
        ui->closed[at] = 0;
        if( ui->docked[at] || ui->flown[at] || p->user_placed || p->modal
            || p->style != TORIRS_CHROME_PANEL_WINDOW || !ui->panels[i].title )
            continue;
        w = p->last_rect.w > 0 ? p->last_rect.w : p->fixed_w;
        x = ui->dock_x - w;
        if( x < ui->world.x )
            x = ui->world.x;
        if( y >= ui->dock_bottom - edp_px(ui, EDP_DOCK_MIN_H) )
            y = ui->dock_top;
        ToriRSChrome_PanelMove(chrome, panel, x, y);
        ui->flown[at] = 1;
        y += p->last_rect.h > 0 ? p->last_rect.h : edp_px(ui, EDP_DOCK_MIN_H);
    }
}

void
Editor_UiResetLayout(struct EditorUi* ui)
{
    assert(ui);
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        int const at = i - ui->panel_first;
        ui->chrome->panels[i].user_placed = 0;
        ui->closed[at] = 0;
        ui->member[at] = ui->boot_member[at];
        if( ui->boot_member[at] )
            ToriRSChrome_PanelSetVisible(ui->chrome, i, 1);
    }
    ui->laid_out = 0;
    if( ui->chrome->surface_w > 0 && ui->chrome->surface_h > 0 )
        Editor_UiLayout(ui);
}

void
Editor_UiSetSnap(
    struct EditorUi* ui,
    int on)
{
    assert(ui);
    ui->snap = on ? 1 : 0;
    ToriRSChrome_SetSnap(
        ui->chrome, ui->snap ? edp_px(ui, EDP_GRID) : 0, ui->snap ? edp_px(ui, EDP_GRID) : 0);
}

struct EditorUiRect
Editor_UiWorldRect(const struct EditorUi* ui)
{
    struct EditorUiRect r = { 0, 0, 0, 0 };
    struct ToriRSChromeRect body;

    assert(ui);
    if( ui->world_panel < 0 )
        return ui->world;
    /* Before the first layout the panel sits at its registration box, which
     * is nobody's answer. */
    if( ui->layout_surface_w <= 0 || !Editor_UiWorldShown(ui) )
        return r;
    body = ToriRSChrome_PanelBodyRect(ui->chrome, ui->world_panel);
    r.x = body.x;
    r.y = body.y;
    r.w = body.w;
    r.h = body.h;
    return r;
}

int
Editor_UiWorldShown(const struct EditorUi* ui)
{
    assert(ui);
    return ui->world_panel >= 0 && ui->visible && ui->chrome->panels[ui->world_panel].visible;
}

/* ---- the saved layout ---------------------------------------------------- */

_Static_assert(EDITOR_LAYOUT_PANELS_MAX >= EDITOR_UI_PANELS_MAX, "a layout file holds every registry panel");

/** Registry panel `i` takes part in the saved layout: a View-menu window of
 *  this registry. The menu bar, the modal pair and other titleless parts are
 *  the modules' own business. */
static int
ui_layout_saves(
    const struct EditorUi* ui,
    int i)
{
    struct EditorUiPanel const* panel = &ui->panels[i];
    return panel->title && panel->chrome_panel >= ui->panel_first && panel->chrome_panel < ui->panel_end
           && panel->chrome_panel < ui->chrome->panel_count && !ui->chrome->panels[panel->chrome_panel].modal;
}

void
Editor_UiLayoutCapture(
    const struct EditorUi* ui,
    struct EditorLayoutFile* out)
{
    int const scale = ToriRSChrome_Scale(ui->chrome) > 0 ? ToriRSChrome_Scale(ui->chrome) : 1;

    assert(ui);
    assert(out);
    EditorLayout_Clear(out);
    out->version = EDITOR_LAYOUT_VERSION;
    out->surface_w = ui->chrome->surface_w;
    out->surface_h = ui->chrome->surface_h;
    out->scale = scale;
    out->has_snap = 1;
    out->snap = ui->snap;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* cp;
        struct EditorLayoutPanel* p;
        if( !ui_layout_saves(ui, i) )
            continue;
        cp = &ui->chrome->panels[ui->panels[i].chrome_panel];
        p = EditorLayout_Add(out, ui->panels[i].name);
        p->has_visible = 1;
        p->visible = cp->visible ? 1 : 0;
        p->has_user_placed = 1;
        p->user_placed = cp->user_placed ? 1 : 0;
        p->has_x = p->has_y = p->has_w = p->has_h = 1;
        p->x = cp->x / scale;
        p->y = cp->y / scale;
        p->w = cp->fixed_w / scale;
        p->h = cp->fixed_h / scale;
    }
}

/** Pull a saved box back onto the surface: at least a title bar's worth of
 *  it stays reachable, and it is never larger than the surface. */
static void
ui_layout_clamp(
    const struct EditorUi* ui,
    int* x,
    int* y,
    int* w,
    int* h)
{
    int const sw = ui->chrome->surface_w;
    int const sh = ui->chrome->surface_h;
    int const keep = edp_px(ui, 64);

    if( sw <= 0 || sh <= 0 )
        return;
    if( *w > sw )
        *w = sw;
    if( *h > sh )
        *h = sh;
    if( *x > sw - keep )
        *x = sw - keep;
    if( *y > sh - keep )
        *y = sh - keep;
    if( *w > 0 && *x + *w > sw )
        *x = sw - *w;
    if( *h > 0 && *y + *h > sh )
        *y = sh - *h;
    if( *x < 0 )
        *x = 0;
    if( *y < 0 )
        *y = 0;
}

void
Editor_UiLayoutApply(
    struct EditorUi* ui,
    const struct EditorLayoutFile* file)
{
    struct ToriRSChrome* chrome;
    int scale;

    assert(ui);
    assert(file);
    chrome = ui->chrome;
    scale = ToriRSChrome_Scale(chrome) > 0 ? ToriRSChrome_Scale(chrome) : 1;

    /* The default set first, from what the modules showed: Reset layout must
     * return to the default, not to this file. */
    ui_capture_boot(ui);
    if( file->has_snap )
        Editor_UiSetSnap(ui, file->snap);
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorLayoutPanel const* sp;
        int panel;
        int at;

        if( !ui_layout_saves(ui, i) )
            continue;
        sp = EditorLayout_Find(file, ui->panels[i].name);
        if( !sp )
            continue;
        panel = ui->panels[i].chrome_panel;
        at = panel - ui->panel_first;

        if( sp->has_user_placed && sp->user_placed && sp->has_x && sp->has_y )
        {
            struct ToriRSChromePanel* cp = &chrome->panels[panel];
            int x = sp->x * scale;
            int y = sp->y * scale;
            int w = sp->has_w ? sp->w * scale : cp->fixed_w;
            int h = sp->has_h ? sp->h * scale : cp->fixed_h;

            ui_layout_clamp(ui, &x, &y, &w, &h);
            ToriRSChrome_PanelSetFixedWidth(chrome, panel, w);
            ToriRSChrome_PanelSetFixedHeight(chrome, panel, h);
            /* A hand-set height drops rows past the bottom unless it scrolls,
             * as the dock column's panels do. */
            if( h > 0 )
                ToriRSChrome_PanelSetScrollable(chrome, panel, 1);
            ToriRSChrome_PanelMove(chrome, panel, x, y);
            cp->user_placed = 1;
            ui->docked[at] = 0;
            ui->flown[at] = 0;
        }
        if( sp->has_visible )
        {
            ToriRSChrome_PanelSetVisible(chrome, panel, sp->visible);
            /* A default panel the user closed stays shut through the layout
             * (the `closed` mark); one opened is admitted where it belongs. */
            ui->closed[at] = (unsigned char)(!sp->visible && ui->member[at] ? 1 : 0);
        }
    }
    /* Made for this very surface and scale: once the default layout has
     * placed the column (and set who is docked), the panels it placed take
     * the file's boxes too -- the column as the user left it, heights and
     * all, not as a fresh layout would share it out. */
    ui->layout_restore = file->surface_w == chrome->surface_w && file->surface_h == chrome->surface_h
                         && file->scale == scale;
    if( ui->layout_restore && file != &ui->layout_saved )
        ui->layout_saved = *file;
    ui->laid_out = 0;
}

/** The second half of a same-surface restore: the boxes of the panels the
 *  default layout just placed, from the file. */
static void
ui_layout_restore_boxes(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const scale = ToriRSChrome_Scale(chrome) > 0 ? ToriRSChrome_Scale(chrome) : 1;

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorLayoutPanel const* sp;
        int panel;
        if( !ui_layout_saves(ui, i) )
            continue;
        panel = ui->panels[i].chrome_panel;
        sp = EditorLayout_Find(&ui->layout_saved, ui->panels[i].name);
        if( !sp || chrome->panels[panel].user_placed || !chrome->panels[panel].visible || !sp->has_x || !sp->has_y )
            continue;
        if( sp->has_visible && !sp->visible )
            continue;
        if( sp->has_w )
            ToriRSChrome_PanelSetFixedWidth(chrome, panel, sp->w * scale);
        if( sp->has_h )
            ToriRSChrome_PanelSetFixedHeight(chrome, panel, sp->h * scale);
        ToriRSChrome_PanelMove(chrome, panel, sp->x * scale, sp->y * scale);
    }
}

void
Editor_UiLayoutHold(struct EditorUi* ui)
{
    assert(ui);
    ui->layout_hold = 1;
}

void
Editor_UiLayoutLoaded(
    struct EditorUi* ui,
    const struct EditorLayoutFile* file)
{
    assert(ui);
    ui->layout_hold = 0;
    if( file )
    {
        ui->layout_saved = *file;
        ui->layout_pending = 1;
    }
}

static uint32_t
ui_fnv(
    uint32_t h,
    int v)
{
    for( int b = 0; b < 4; b++ )
    {
        h ^= (uint32_t)((unsigned)v >> (8 * b)) & 0xffu;
        h *= 16777619u;
    }
    return h;
}

uint32_t
Editor_UiLayoutFingerprint(const struct EditorUi* ui)
{
    uint32_t h = 2166136261u;

    assert(ui);
    h = ui_fnv(h, ui->chrome->surface_w);
    h = ui_fnv(h, ui->chrome->surface_h);
    h = ui_fnv(h, ToriRSChrome_Scale(ui->chrome));
    h = ui_fnv(h, ui->snap);
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* cp;
        if( !ui_layout_saves(ui, i) )
            continue;
        cp = &ui->chrome->panels[ui->panels[i].chrome_panel];
        h = ui_fnv(h, i);
        h = ui_fnv(h, cp->visible);
        h = ui_fnv(h, cp->user_placed);
        h = ui_fnv(h, cp->x);
        h = ui_fnv(h, cp->y);
        h = ui_fnv(h, cp->fixed_w);
        h = ui_fnv(h, cp->fixed_h);
    }
    return h;
}

int
Editor_UiLayoutBusy(const struct EditorUi* ui)
{
    struct ToriRSChrome const* chrome;

    assert(ui);
    chrome = ui->chrome;
    return !ui->visible || !ui->laid_out || ui->layout_hold || ui->layout_pending || chrome->drag_panel >= 0
           || chrome->resize_panel >= 0 || chrome->split_panel >= 0;
}

static void
ui_default_layout(struct EditorUi* ui)
{
    int const menubar = ui_chrome_panel(ui, "menubar");

    /* The saved layout is still being read: wait for it rather than lay out
     * the default and move everything a frame later. */
    if( ui->layout_hold )
        return;
    if( ui->layout_pending )
    {
        ui->layout_pending = 0;
        Editor_UiLayoutApply(ui, &ui->layout_saved);
    }
    if( ui->laid_out || ui->chrome->surface_w <= 0 || ui->chrome->surface_h <= 0 )
        return;
    /* Not built yet (no heights to stack): next frame. */
    if( menubar >= 0 && ToriRSChrome_PanelRect(ui->chrome, menubar).h <= 0 )
        return;
    Editor_UiLayout(ui);
    if( ui->layout_restore )
    {
        ui->layout_restore = 0;
        ui_layout_restore_boxes(ui);
    }
}

/* ---- the frame ------------------------------------------------------------- */

void
Editor_UiTick(struct EditorUi* ui)
{
    struct ToriRSChromeEvent event;
    int activated;

    assert(ui);

    if( !ui->visible )
        return;
    /* The grid follows the chrome's scale (8 px at 1x, 16 at 2x). */
    Editor_UiSetSnap(ui, ui->snap);
    ui_follow_scale(ui);
    ui_follow_surface(ui);
    ui_default_layout(ui);
    if( ui->laid_out )
        ui_layout_follow(ui);

    /* ONE activation per frame, and it is either one module's or nobody's
     * here. Unclaimed, it goes back where it was: another consumer of this
     * chrome instance later in the frame (the loc editor) may own it. */
    activated = ToriRSChrome_TakeActivated(ui->chrome);
    if( activated >= 0 )
    {
        int claimed = 0;
        for( int i = 0; i < ui->panel_count && !claimed; i++ )
            if( ui->panels[i].activate )
                claimed = ui->panels[i].activate(ui, ui->panels[i].self, activated);
        if( !claimed )
            ui->chrome->activated = activated;
    }

    while( ToriRSChrome_TakeEvent(ui->chrome, &event) )
    {
        for( int i = 0; i < ui->panel_count; i++ )
            if( ui->panels[i].event && ui->panels[i].event(ui, ui->panels[i].self, &event) )
                break;
    }
    if( ToriRSChrome_TakeEventOverflow(ui->chrome) )
        Editor_UiLog(ui, "editor: chrome event queue overflowed; some gestures were dropped");

    ui_frames_expire(ui);
    ui_publish_view(ui);

    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].tick )
            ui->panels[i].tick(ui, ui->panels[i].self);
}

void
Editor_UiApplyState(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(ui);
    assert(values || count == 0);

    ui->applying_state = 1;
    if( key == TORIRSMAPED_STATE_VIEW_SQUARE && count >= 2 )
    {
        int const slot = count >= 5 ? values[4] : 0;
        ui_frame_note(
            ui, slot, values[0], values[1], count >= 4 ? values[2] : 1, count >= 4 ? values[3] : 1);
    }
    else if( key == TORIRSMAPED_STATE_TOOL && count >= 1 && values[0] >= 0
             && values[0] < EDITOR_TOOL_COUNT )
        ui->tools.tool = (enum Editor_Tool)values[0];
    else if( key == TORIRSMAPED_STATE_EDIT_LEVEL && count >= 1 && values[0] >= -1 && values[0] <= 3 )
        ui->tools.edit_level = values[0];

    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].on_state )
            ui->panels[i].on_state(ui, ui->panels[i].self, key, values, count);
    ui->applying_state = 0;
}

int
Editor_UiPublish(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(ui);
    assert(values || count == 0);

    if( ui->applying_state || !ui->editor )
        return 0;
    return Editor_StateSet(ui->editor, key, values, count);
}

void
Editor_UiSetTool(
    struct EditorUi* ui,
    enum Editor_Tool tool)
{
    int32_t value;

    assert(ui);
    assert(tool >= 0);
    assert(tool < EDITOR_TOOL_COUNT);

    if( ui->tools.tool == tool )
        return;
    ui->tools.tool = tool;
    ui->tools.flatten_valid = 0;
    value = (int32_t)tool;
    (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_TOOL, &value, 1);
}

/* ---- console --------------------------------------------------------------- */

void
Editor_UiLogV(
    struct EditorUi* ui,
    const char* format,
    va_list args)
{
    char* line;

    assert(ui);
    assert(format);

    line = ui->console[ui->console_head];
    vsnprintf(line, EDITOR_UI_CONSOLE_LINE_MAX, format, args);
    /* One line per entry: a trailing newline is the caller's habit from
     * TORIRS_LOG, not part of the message. */
    {
        size_t length = strlen(line);
        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
    }
    TORIRS_LOG("%s\n", line);
    ui->console_head = (ui->console_head + 1) % EDITOR_UI_CONSOLE_LINES;
    if( ui->console_count < EDITOR_UI_CONSOLE_LINES )
        ui->console_count++;
    ui->console_serial++;
}

void
Editor_UiLog(
    struct EditorUi* ui,
    const char* format,
    ...)
{
    va_list args;
    va_start(args, format);
    Editor_UiLogV(ui, format, args);
    va_end(args);
}

const char*
Editor_UiConsoleLine(
    const struct EditorUi* ui,
    int index)
{
    assert(ui);

    if( index < 0 || index >= ui->console_count )
        return NULL;
    return ui->console[(ui->console_head - 1 - index + EDITOR_UI_CONSOLE_LINES)
                       % EDITOR_UI_CONSOLE_LINES];
}

/* ---- the save guard -------------------------------------------------------- */

void
Editor_UiRunGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard)
{
    assert(ui);
    assert(guard);

    switch( guard->action )
    {
    case EDITOR_UI_GUARD_OPEN_SQUARE:
        if( ui->hooks.open_square )
            ui->hooks.open_square(ui->hooks_user, guard->map_x, guard->map_z, guard->storage, 0);
        else
            Editor_UiLog(ui, "editor: this window cannot open squares");
        break;
    case EDITOR_UI_GUARD_CLOSE:
        if( ui->hooks.close_editor )
            ui->hooks.close_editor(ui->hooks_user);
        break;
    case EDITOR_UI_GUARD_NONE:
        break;
    }
}

int
Editor_UiGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard)
{
    assert(ui);
    assert(guard);
    assert(guard->action != EDITOR_UI_GUARD_NONE);

    if( !ui->editor || !Editor_DocHasUnsaved(&ui->editor->doc) )
    {
        Editor_UiRunGuarded(ui, guard);
        return 1;
    }
    /* The save guard module shows its modal from its tick when this is set
     * (edpanel_saveguard.c); without that module the action is refused
     * rather than run over unsaved work. */
    ui->guard = *guard;
    if( !Editor_UiFindPanel(ui, "save_guard") )
    {
        Editor_UiLog(ui, "editor: save or undo your changes first");
        memset(&ui->guard, 0, sizeof(ui->guard));
    }
    return 0;
}

/* ---- the loc multi-selection ------------------------------------------------ */

static int
loc_ref_same(
    const struct Editor_LocRef* a,
    const struct Editor_LocRef* b)
{
    return a->map_x == b->map_x && a->map_z == b->map_z && a->level == b->level && a->x == b->x
           && a->z == b->z && a->shape == b->shape;
}

int
Editor_UiFindLoc(
    const struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    assert(ui);
    assert(ref);

    for( int i = 0; i < ui->tools.loc_count; i++ )
        if( loc_ref_same(&ui->tools.locs[i], ref) )
            return i;
    return -1;
}

void
Editor_UiSelectLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    assert(ui);

    ui->tools.loc_count = 0;
    if( ref )
    {
        ui->tools.locs[0] = *ref;
        ui->tools.loc_count = 1;
    }
}

int
Editor_UiToggleLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref)
{
    int at;

    assert(ui);
    assert(ref);

    at = Editor_UiFindLoc(ui, ref);
    if( at >= 0 )
    {
        memmove(
            &ui->tools.locs[at], &ui->tools.locs[at + 1],
            (size_t)(ui->tools.loc_count - at - 1) * sizeof(ui->tools.locs[0]));
        ui->tools.loc_count--;
        return 0;
    }
    if( ui->tools.loc_count >= EDITOR_LOC_SELECTION_MAX )
    {
        Editor_UiLog(ui, "editor: the selection holds %d locs at most", EDITOR_LOC_SELECTION_MAX);
        return 0;
    }
    ui->tools.locs[ui->tools.loc_count++] = *ref;
    return 1;
}
