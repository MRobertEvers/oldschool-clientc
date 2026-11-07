#include "editor_ui.h"

#include "editor.h"
#include "editor_view.h"
#include "input/torirs_gesture.h"
#include "builder/builder_panels.h"
#include "iface/iface_panels.h"
#include "live/live_panel.h"
#include "accounts/accounts_panel.h"
#include "log/torirs_log.h"
#include "panels/edpanel_common.h"
#include "panels/edpanels.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/torirs_chrome_scale_q.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <math.h>
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
    "Select",       "Rectangle select", "Height",       "Flatten",
    "Smooth",       "Paint underlay",   "Paint overlay", "Paint flags",
    "Place",        "Move",             "Rotate",       "Delete",
    "Place player", "Paint path",       "Roughen",      "Scatter",
    "Plateau",
};

/* The hotkey table arms the tools in enum Editor_Tool's order, and its hint
 * strip and context derivation (Editor_HotkeysHint, Editor_HotkeyContextsFrom)
 * take the tool by that value. */
_Static_assert(EDITOR_TOOL_COUNT == EDITOR_HK_HINT_TOOLS, "one hint list per tool");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_LOC_PLACE == EDITOR_HK_TOOL_PLACE, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_LOC_MOVE == EDITOR_HK_TOOL_MOVE, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_RECT_SELECT == EDITOR_HK_TOOL_RECT, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_HEIGHT == EDITOR_HK_TOOL_HEIGHT, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_PLACE_PLAYER == EDITOR_HK_TOOL_PLACE_PLAYER,
    "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_PAINT_PATH == EDITOR_HK_TOOL_PAINT_PATH, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_NOISE == EDITOR_HK_TOOL_NOISE, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_SCATTER == EDITOR_HK_TOOL_SCATTER, "the tool orders agree");
_Static_assert(EDITOR_HK_TOOL_SELECT + EDITOR_TOOL_PLATEAU == EDITOR_HK_TOOL_PLATEAU, "the tool orders agree");

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
    return tool == EDITOR_TOOL_HEIGHT || tool == EDITOR_TOOL_FLATTEN
           || tool == EDITOR_TOOL_SMOOTH || tool == EDITOR_TOOL_PAINT_UNDERLAY
           || tool == EDITOR_TOOL_PAINT_OVERLAY || tool == EDITOR_TOOL_PAINT_FLAGS || tool == EDITOR_TOOL_NOISE
           || tool == EDITOR_TOOL_SCATTER || tool == EDITOR_TOOL_PLATEAU;
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
    ui->tools.place_fit = 1;
    ui->tools.pick_kind = -1;
    ui->tools.pick_id = -1;
    ui->tools.path_style = 0;
    ui->tools.path_width_halves = 4;
    ui->tools.path_roughness = 50;
    ui->tools.path_seed = 1;
    ui->tools.noise_seed = 1;
    ui->tools.paint_mix = 100;
    ui->tools.scatter_density = 20;
    ui->tools.scatter_rotate = 1;
    ui->tools.scatter_seed = 1;
    ui->tools.plateau_skirt = 3;
    ui->tools.height_mode = EDITOR_HEIGHT_MODE_BOTH;
    ui->tools.height_grid = 1;
    ui->tools.height_grid_opacity = 30;

    /* Any number other views are unlikely to pick: the address of this
     * window's registry mixed with the clock. Positive, never 0 (0 is what a
     * publisher that sends no slot is filed under). */
    {
        uint32_t mix = (uint32_t)(uintptr_t)ui ^ (uint32_t)time(NULL) * 2654435761u;
        ui->view_slot = (int)(mix & 0x3fffffff) | 1;
    }
    ui->view_published[0] = -1;
    ui->snap = 1;
    ui->compact_mode = -1;
    memset(ui->expanded, 1, sizeof(ui->expanded));
    ui->world_panel = -1;
    ui->solo_panel = -1;
    ui->layout_reopened = -1;
    Editor_HotkeysInit(&ui->hotkeys);
    ui->previous_tool = EDITOR_TOOL_SELECT;
    Editor_PrefsInit(&ui->prefs);
    ui->scale_auto_q = Editor_ChromeScaleQ(chrome);
    ui->text_face_normal = chrome->theme.font_row;

    /* A read-only session says so, and who holds the tree, before anybody
     * edits and finds out at Save (or RUN). */
    if( editor && editor->host_open && !editor->writable )
    {
        Editor_UiLog(ui, "editor: READ-ONLY: %s",
            editor->read_only_reason[0] ? editor->read_only_reason : "another editor server holds the content tree");
        Editor_UiLog(ui, "editor: close that process (a dead holder's lock is taken back at the next start), "
                         "or run this window against the daemon");
    }
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

/**
 * Every View-menu window of this registry -- World included -- wears the same
 * title-bar furniture: a Close box at the right end (closing is exactly View >
 * <panel> off: the chrome hides it, the layout notes it closed, the View menu
 * and the saved layout follow), a Pop out button beside it
 * (Editor_UiPanelPopOut), and a resize grip with every edge and corner of its
 * border draggable. Opt-in on the chrome, so the game's own panels are
 * unchanged; modal dialogs keep their own furniture.
 */
static void
ui_window_furniture(struct EditorUi* ui)
{
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        struct ToriRSChromePanel const* p;

        if( !ui->panels[i].title || panel < ui->panel_first || panel >= ui->panel_end )
            continue;
        p = &ui->chrome->panels[panel];
        if( p->style != TORIRS_CHROME_PANEL_WINDOW || p->modal )
            continue;
        ToriRSChrome_PanelSetClosable(ui->chrome, panel, 1);
        ToriRSChrome_PanelSetPoppable(ui->chrome, panel, 1);
        ToriRSChrome_PanelSetResizable(ui->chrome, panel, 1);
    }
}

static void
ui_default_groups(struct EditorUi* ui);

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
    /* The status bar across the foot, always on with the editor. */
    Editor_StatusBar_Register(ui);
    Editor_ToolRail_Register(ui);
    Editor_Terrain_Register(ui);
    Editor_Palette_Register(ui);
    Editor_RectSelect_Register(ui);
    Editor_SquarePicker_Register(ui);
    Editor_Locs_Register(ui);
    Editor_Spawns_Register(ui);
    /* App-bound: a window with an App, or services standing in for one (a
     * headless harness's fakes); the Catalog brings the Preview and the
     * Record panels with it. */
    if( ui->app || ui->app_services.user_data )
    {
        Editor_Catalog_Register(ui);
        Editor_Inspector_Register(ui);
        /* Named strips of catalog records (the PALETTES document); it picks
         * through the Catalog, so it comes with it. */
        Editor_CatalogPalette_Register(ui);
    }
    Editor_NewSquare_Register(ui);
    /* The instance helper, the picker's Instance... (design section 13). */
    Editor_Instance_Register(ui);
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
    /* The editor lane and its publish (editor/panels/edpanel_publish.c,
     * File > Publish...): hidden until opened. */
    Editor_Publish_Register(ui);
    /* Scenes (workbench 6.5): the SCENES document's list, Save scene
     * (Ctrl+Shift+K), Enter; hidden until the View menu opens it. */
    Editor_Scenes_Register(ui);
    /* The lane's accounts (editor/accounts/), hidden until the View menu
     * opens it; after the Live panel, whose log it echoes. */
    Editor_Accounts_Register(ui);
    /* The script triggers of a record or a file (View > Triggers, Ctrl+E):
     * Add trigger, Open in editor, Refresh, Check, Run. */
    Editor_Triggers_Register(ui);
    /* Workbench 6.7: the Shop builder and the Quest board, hidden until the
     * View menu opens them. */
    Editor_Shop_Register(ui);
    Editor_Quest_Register(ui);
    Editor_History_Register(ui);
    /* Workbench 6.6: every build's, Check's and CS2 compile's findings and
     * the builders' lint, one list; the bottom strip's third member. */
    Editor_Problems_Register(ui);
    /* Named areas and square collections (the AREAS document). */
    Editor_Areas_Register(ui);
    /* Encounter sets and their npcs (the ENCOUNTERS document, workbench
     * 6.7): hidden until View > Encounters or Spawns' Group... opens it. */
    Editor_Encounter_Register(ui);
    /* The hotkey table and its editor (View > Hotkeys panel, F1). */
    Editor_Hotkeys_Register(ui);
    /* The command palette (Ctrl+K), a modal over every panel above. */
    Editor_CmdPalette_Register(ui);
    ui->panel_end = ui->chrome->panel_count;
    /* The panel groups last: their frames draw over the modules' windows
     * (they never overlap the one each holds), and each is the next window
     * of the registry's (Editor_UiRegisterGroup). */
    ui_default_groups(ui);
    ui->layout_scale = Editor_ChromeScaleQ(ui->chrome);
    ui_window_furniture(ui);
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
    /* A group's frame goes with the editor; it comes back with its first
     * open member (ui_group_follow). */
    if( !ui->visible )
        for( int i = 0; i < ui->group_count; i++ )
            ToriRSChrome_PanelSetVisible(ui->chrome, ui->groups[i].chrome_panel, 0);
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

/* ---- panel groups -------------------------------------------------------------
 *
 * The model is at struct EditorUiGroup (editor_ui.h). Everything here reads
 * the registry and each group's few chrome panels, never the UI tree, so the
 * per-frame follow is a handful of comparisons per group.
 */
_Static_assert(EDITOR_UI_GROUP_TITLE_MAX == TORIRS_CHROME_LABEL_MAX, "a group or tab title is a chrome label");
_Static_assert(EDITOR_UI_GROUPS_MAX <= EDITOR_LAYOUT_GROUPS_MAX, "a layout file holds every group");
_Static_assert(EDITOR_UI_GROUPS_MAX < 255, "group_plus1 is a byte");

/** The registry index of module `name`, or -1. */
static int
ui_registry_index(
    const struct EditorUi* ui,
    const char* name)
{
    for( int i = 0; i < ui->panel_count; i++ )
        if( strcmp(ui->panels[i].name, name) == 0 )
            return i;
    return -1;
}

/** The group registry panel `reg` is in now, or NULL. */
static struct EditorUiGroup*
ui_group_of(
    struct EditorUi* ui,
    int reg)
{
    return ui->group_plus1[reg] ? &ui->groups[ui->group_plus1[reg] - 1] : NULL;
}

/** The member slot of registry panel `reg` in `g`, or -1. */
static int
ui_group_slot(
    const struct EditorUiGroup* g,
    int reg)
{
    for( int slot = 0; slot < g->member_count; slot++ )
        if( g->members[slot] == reg )
            return slot;
    return -1;
}

/** The chrome window of member `slot`. */
static int
ui_group_window(
    const struct EditorUi* ui,
    const struct EditorUiGroup* g,
    int slot)
{
    return ui->panels[g->members[slot]].chrome_panel;
}

/** The group whose frame is chrome panel `panel`, or NULL. */
static struct EditorUiGroup*
ui_frame_group(
    struct EditorUi* ui,
    int panel)
{
    for( int i = 0; i < ui->group_count; i++ )
        if( ui->groups[i].chrome_panel == panel )
            return &ui->groups[i];
    return NULL;
}

static int
ui_group_has_open(const struct EditorUiGroup* g)
{
    for( int slot = 0; slot < g->member_count; slot++ )
        if( g->open[slot] )
            return 1;
    return 0;
}

/** The frame's height (title bar and strip), from its last Build while it
 *  showed; the last one known otherwise, 0 before it was ever built. */
static int
ui_group_frame_height(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    struct ToriRSChromePanel const* f = &ui->chrome->panels[g->chrome_panel];
    if( f->visible && f->last_rect.h > 0 )
        g->frame_h = f->last_rect.h;
    return g->frame_h;
}

/** A member window inside a frame wears no title bar of its own (the frame's
 *  is the group's); out of it, its own title comes back. */
static void
ui_group_dress(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int slot,
    int on)
{
    int const panel = ui_group_window(ui, g, slot);

    if( on && !g->dressed[slot] )
    {
        snprintf(g->member_titles[slot], sizeof(g->member_titles[slot]), "%s", ui->chrome->panels[panel].title);
        ToriRSChrome_PanelSetTitle(ui->chrome, panel, "");
        g->dressed[slot] = 1;
    }
    else if( !on && g->dressed[slot] )
    {
        ToriRSChrome_PanelSetTitle(ui->chrome, panel, g->member_titles[slot]);
        g->dressed[slot] = 0;
    }
}

/** The strip's rows, from the open members: "Catalog (F8)". Rebuilt only
 *  when the open set, the front tab or a binding changed. */
static void
ui_group_strip(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    uint32_t digest = 2166136261u;
    int selected = 0;

    for( int slot = 0; slot < g->member_count; slot++ )
        digest = (digest ^ (uint32_t)(g->members[slot] * 4 + g->open[slot] * 2 + (slot == g->shown))) * 16777619u;
    digest = (digest ^ ui->hotkeys.serial) * 16777619u;
    if( g->tabs_built && digest == g->tabs_digest )
        return;
    g->tab_count = 0;
    for( int slot = 0; slot < g->member_count; slot++ )
    {
        struct EditorUiPanel const* member = &ui->panels[g->members[slot]];
        int const action = Editor_HotkeyForPanel(member->name);
        char chords[48];

        if( !g->open[slot] )
            continue;
        chords[0] = '\0';
        if( action >= 0 )
            Editor_HotkeyDisplay(&ui->hotkeys, (enum Editor_HotkeyAction)action, chords, (int)sizeof(chords));
        if( chords[0] )
            snprintf(g->tab_labels[g->tab_count], sizeof(g->tab_labels[0]), "%s (%s)", member->title, chords);
        else
            snprintf(g->tab_labels[g->tab_count], sizeof(g->tab_labels[0]), "%s", member->title);
        g->tab_rows[g->tab_count] = g->tab_labels[g->tab_count];
        g->tab_slots[g->tab_count] = slot;
        if( slot == g->shown )
            selected = g->tab_count;
        g->tab_count++;
    }
    ToriRSChrome_TabsSetTitles(ui->chrome, g->tabs, g->tab_rows, g->tab_count);
    ToriRSChrome_PanelSetActiveTab(ui->chrome, g->chrome_panel, selected);
    g->tabs_digest = digest;
    g->tabs_built = 1;
}

/** Member `slot` was maximised: put it back, and the group's box with it. */
static void
ui_group_unmaximise(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int slot)
{
    int const panel = ui_group_window(ui, g, slot);
    struct ToriRSChromePanel const* p = &ui->chrome->panels[panel];

    if( !ToriRSChrome_PanelIsMaximised(ui->chrome, panel) )
        return;
    ToriRSChrome_PanelRestore(ui->chrome, panel);
    if( g->frame_h <= 0 )
        return;
    g->x = p->x;
    g->y = p->y - g->frame_h;
    g->w = p->fixed_w;
    g->body_h = p->fixed_h;
    ToriRSChrome_PanelMove(ui->chrome, g->chrome_panel, g->x, g->y);
}

/** Bring member `slot` to the front: open in the strip, its window shown
 *  (placed into the group's box by the next ui_group_place), the one in
 *  front before it hidden but still open. */
static void
ui_group_show(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int slot)
{
    int const old = g->shown;

    g->open[slot] = 1;
    if( old >= 0 && old != slot )
    {
        ui_group_unmaximise(ui, g, old);
        ToriRSChrome_PanelSetVisible(ui->chrome, ui_group_window(ui, g, old), 0);
        g->prev_shown = old;
    }
    g->shown = slot;
    g->parked = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, ui_group_window(ui, g, slot), 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, 1);
}

/** What comes up when `closing` leaves the strip: the tab shown before it,
 *  else the next open one after it, else the nearest before it; -1 none. */
static int
ui_group_next(
    const struct EditorUiGroup* g,
    int closing)
{
    if( g->prev_shown >= 0 && g->prev_shown != closing && g->open[g->prev_shown] )
        return g->prev_shown;
    for( int slot = closing + 1; slot < g->member_count; slot++ )
        if( g->open[slot] )
            return slot;
    for( int slot = closing - 1; slot >= 0; slot-- )
        if( g->open[slot] )
            return slot;
    return -1;
}

/** The shown tab leaves the strip (its window closed from wherever, or the
 *  frame's Close); the next one comes up, or the frame goes. */
static void
ui_group_close_shown(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    int const slot = g->shown;
    int next;

    ui_group_unmaximise(ui, g, slot);
    g->open[slot] = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, ui_group_window(ui, g, slot), 0);
    next = ui_group_next(g, slot);
    g->shown = -1;
    g->prev_shown = -1;
    if( next >= 0 )
        ui_group_show(ui, g, next);
    else
        ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, 0);
}

/** The layout has no room for the group: frame and windows hidden, the
 *  strip kept as it is. */
static void
ui_group_park(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    g->parked = 1;
    ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, 0);
    for( int slot = 0; slot < g->member_count; slot++ )
        ToriRSChrome_PanelSetVisible(ui->chrome, ui_group_window(ui, g, slot), 0);
}

/** Put member window `panel` into the group's box, under the strip. */
static void
ui_group_fill(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int panel)
{
    ToriRSChrome_PanelSetFixedWidth(ui->chrome, panel, g->w);
    ToriRSChrome_PanelSetFixedHeight(ui->chrome, panel, g->body_h);
    /* A fixed height drops rows past the bottom unless the window scrolls,
     * as the dock column's panels do. */
    if( g->body_h > 0 )
        ToriRSChrome_PanelSetScrollable(ui->chrome, panel, 1);
    ToriRSChrome_PanelMove(ui->chrome, panel, g->x, g->y + g->frame_h);
}

/**
 * The frame and the shown window move as one: a new front tab takes the
 * group's box; a moved frame (its title dragged, the layout) takes the window
 * with it; a window resized by its edges, maximised or moved by its module
 * takes the frame with it -- and a resize by the user makes the group the
 * user's (the frame is what the layout asks).
 */
static void
ui_group_place(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    struct ToriRSChrome* chrome = ui->chrome;
    struct ToriRSChromePanel* frame = &chrome->panels[g->chrome_panel];
    int const fh = ui_group_frame_height(ui, g);
    int panel;
    struct ToriRSChromePanel* p;

    if( g->shown < 0 || g->parked || fh <= 0 )
        return;
    panel = ui_group_window(ui, g, g->shown);
    p = &chrome->panels[panel];
    if( g->pending_h > 0 )
    {
        g->body_h = g->pending_h - fh > 0 ? g->pending_h - fh : 0;
        g->pending_h = 0;
        g->placed = -1;
    }
    if( g->w <= 0 )
    {
        /* Never laid out (a group opened before the first layout, or by a
         * harness that lays out none): the frame's place, the window's size. */
        g->x = frame->x;
        g->y = frame->y;
        g->w = p->fixed_w > 0 ? p->fixed_w : p->last_rect.w;
        g->body_h = p->fixed_h;
        g->placed = -1;
    }
    if( g->placed != panel )
    {
        if( !ToriRSChrome_PanelIsMaximised(chrome, panel) )
            ui_group_fill(ui, g, panel);
        g->placed = panel;
    }
    else if( frame->x != g->x || frame->y != g->y )
    {
        g->x = frame->x;
        g->y = frame->y;
        ui_group_fill(ui, g, panel);
    }
    else if( p->x != g->x || p->y != g->y + fh || p->fixed_w != g->w || p->fixed_h != g->body_h )
    {
        g->x = p->x;
        g->y = p->y - fh;
        g->w = p->fixed_w > 0 ? p->fixed_w : p->last_rect.w;
        g->body_h = p->fixed_h;
        ToriRSChrome_PanelMove(chrome, g->chrome_panel, g->x, g->y);
        if( p->user_placed && !ToriRSChrome_PanelIsMaximised(chrome, panel) )
        {
            frame->user_placed = 1;
            p->user_placed = 0;
        }
    }
    if( frame->fixed_w != g->w )
        ToriRSChrome_PanelSetFixedWidth(chrome, g->chrome_panel, g->w);
}

/**
 * One group, every frame: the strip follows the members' windows (whatever
 * showed or hid them), the frame shows while a tab is open, the shown window
 * sits under it.
 */
static void
ui_group_follow(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const frame_shown = chrome->panels[g->chrome_panel].visible;
    unsigned char vis[EDITOR_UI_GROUP_MEMBERS_MAX];
    int newcomers = 0;
    int first_newcomer = -1;

    for( int slot = 0; slot < g->member_count; slot++ )
    {
        vis[slot] = (unsigned char)(chrome->panels[ui_group_window(ui, g, slot)].visible ? 1 : 0);
        if( vis[slot] && (slot != g->shown || g->parked) )
        {
            newcomers++;
            if( first_newcomer < 0 )
                first_newcomer = slot;
        }
    }
    if( newcomers > 0 )
    {
        /* Windows shown since the last frame join the strip. One shown on
         * its own (View, a hotkey, the module, a pop-in) comes to the front;
         * several at once (the editor opening, Reset layout, panels shown
         * again) keep the front tab that was, or had been, in front. */
        int front = first_newcomer;
        if( newcomers > 1 && g->shown >= 0 && vis[g->shown] )
            front = g->shown;
        else if( newcomers > 1 && g->prev_shown >= 0 && vis[g->prev_shown] )
            front = g->prev_shown;
        for( int slot = 0; slot < g->member_count; slot++ )
            if( vis[slot] )
            {
                g->open[slot] = 1;
                if( slot != front && slot != g->shown )
                    ToriRSChrome_PanelSetVisible(chrome, ui_group_window(ui, g, slot), 0);
            }
        ui_group_show(ui, g, front);
    }
    else if( g->parked )
    {
        /* Shown again by the layout's own reopen, or Reset layout. */
        if( frame_shown && g->shown >= 0 )
            ui_group_show(ui, g, g->shown);
        else if( frame_shown )
            ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, 0);
    }
    else if( g->shown >= 0 && (!vis[g->shown] || !frame_shown) )
        /* The shown window closed (View, Pop out, the module) -- or the
         * frame's own Close, which is the shown tab's. */
        ui_group_close_shown(ui, g);
    else if( g->shown < 0 && frame_shown )
    {
        /* Something showed the frame itself (Reset layout's default set). */
        int const open = ui_group_next(g, -1);
        if( open >= 0 )
            ui_group_show(ui, g, open);
        else
            ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, 0);
    }
    for( int slot = 0; slot < g->member_count; slot++ )
        ui_group_dress(ui, g, slot, 1);
    ui_group_strip(ui, g);
    ui_group_place(ui, g);
}

/** Every group, every frame. A popped-out window lays out its one panel
 *  alone: there every member wears its own title again. */
static void
ui_groups_follow(struct EditorUi* ui)
{
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        if( ui->solo_panel >= 0 )
        {
            for( int slot = 0; slot < g->member_count; slot++ )
                ui_group_dress(ui, g, slot, 0);
            continue;
        }
        ui_group_follow(ui, g);
    }
}

/** A tab click: that member to the front. 1 when `widget` was a strip. */
static int
ui_groups_activate(
    struct EditorUi* ui,
    int widget)
{
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        int tab;
        if( widget != g->tabs )
            continue;
        tab = ToriRSChrome_PanelActiveTab(ui->chrome, g->chrome_panel);
        if( tab >= 0 && tab < g->tab_count && g->tab_slots[tab] != g->shown )
        {
            ui_group_show(ui, g, g->tab_slots[tab]);
            ui_group_strip(ui, g);
            ui_group_place(ui, g);
        }
        return 1;
    }
    return 0;
}

int
Editor_UiRegisterGroup(
    struct EditorUi* ui,
    const char* name,
    const char* title)
{
    struct EditorUiGroup* g;
    int frame;

    assert(ui);
    assert(name);
    assert(title);
    assert(name[0]);
    assert(strlen(name) < EDITOR_LAYOUT_NAME_MAX);
    assert(ui->group_count < EDITOR_UI_GROUPS_MAX);
    assert(!Editor_UiFindGroup(ui, name));

    g = &ui->groups[ui->group_count];
    memset(g, 0, sizeof(*g));
    snprintf(g->name, sizeof(g->name), "%s", name);
    snprintf(g->title, sizeof(g->title), "%s", title);
    g->shown = -1;
    g->prev_shown = -1;
    g->placed = -1;
    frame = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y), edp_px(ui, EDP_COL_W),
        g->title);
    assert(frame >= 0);
    /* The frame is one of this registry's windows: the next one after the
     * modules' (while Editor_UiRegisterToolsPanels runs, panel_end is set
     * just before the groups are added). */
    assert(ui->panel_end > ui->panel_first);
    assert(frame == ui->panel_end);
    ui->panel_end = frame + 1;
    g->chrome_panel = frame;
    g->tabs = ToriRSChrome_Tabs(ui->chrome, frame, g->tab_rows, 0, 0);
    assert(g->tabs >= 0);
    /* Close and Pop out act on the shown tab (ui_group_follow,
     * ui_popout_tick); the shown window's own edges resize the group. */
    ToriRSChrome_PanelSetClosable(ui->chrome, frame, 1);
    ToriRSChrome_PanelSetPoppable(ui->chrome, frame, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, frame, 0);
    return ui->group_count++;
}

/** Insert registry panel `reg` into `g` at member slot `at`. */
static void
ui_group_insert(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int reg,
    int at)
{
    assert(g->member_count < EDITOR_UI_GROUP_MEMBERS_MAX);
    assert(at >= 0);
    assert(at <= g->member_count);
    for( int slot = g->member_count; slot > at; slot-- )
    {
        g->members[slot] = g->members[slot - 1];
        g->open[slot] = g->open[slot - 1];
        g->dressed[slot] = g->dressed[slot - 1];
        memcpy(g->member_titles[slot], g->member_titles[slot - 1], sizeof(g->member_titles[slot]));
    }
    g->members[at] = reg;
    g->open[at] = 0;
    g->dressed[at] = 0;
    g->member_titles[at][0] = '\0';
    g->member_count++;
    if( g->shown >= at )
        g->shown++;
    if( g->prev_shown >= at )
        g->prev_shown++;
    ui->group_plus1[reg] = (unsigned char)(g - ui->groups + 1);
    /* A window open as a panel of its own joins the strip (as the front tab,
     * on the next follow); a hidden one joins closed. */
    g->tabs_built = 0;
}

/** Take registry panel `reg` out of its group: its title back, its window
 *  open as a panel of its own when it was open in the strip. */
static void
ui_group_remove(
    struct EditorUi* ui,
    int reg)
{
    struct EditorUiGroup* g = ui_group_of(ui, reg);
    int const panel = ui->panels[reg].chrome_panel;
    int slot;
    int was_shown;
    int was_open;

    assert(g);
    slot = ui_group_slot(g, reg);
    assert(slot >= 0);
    was_shown = slot == g->shown;
    was_open = g->open[slot] && !g->parked;
    if( was_shown )
        ui_group_unmaximise(ui, g, slot);
    ui_group_dress(ui, g, slot, 0);
    for( int i = slot; i + 1 < g->member_count; i++ )
    {
        g->members[i] = g->members[i + 1];
        g->open[i] = g->open[i + 1];
        g->dressed[i] = g->dressed[i + 1];
        memcpy(g->member_titles[i], g->member_titles[i + 1], sizeof(g->member_titles[i]));
    }
    g->member_count--;
    if( g->prev_shown == slot )
        g->prev_shown = -1;
    else if( g->prev_shown > slot )
        g->prev_shown--;
    if( was_shown )
        g->shown = -1;
    else if( g->shown > slot )
        g->shown--;
    if( g->placed == panel )
        g->placed = -1;
    ui->group_plus1[reg] = 0;
    g->tabs_built = 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, panel, was_open);
    if( was_shown )
    {
        int const next = ui_group_next(g, -1);
        if( next >= 0 && !g->parked )
            ui_group_show(ui, g, next);
        else if( next < 0 )
            ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, 0);
    }
    /* The window is the layout's again, as any panel of its own. */
    if( panel >= ui->panel_first && panel < ui->panel_end )
    {
        int const at = panel - ui->panel_first;
        ui->docked[at] = 0;
        ui->flown[at] = 0;
        ui->closed[at] = 0;
    }
    ui->laid_out = 0;
}

void
Editor_UiJoinGroup(
    struct EditorUi* ui,
    const char* module,
    const char* group)
{
    struct EditorUiGroup* g;
    int reg;

    assert(ui);
    assert(module);
    assert(group);
    reg = ui_registry_index(ui, module);
    g = Editor_UiFindGroup(ui, group);
    assert(reg >= 0);
    assert(g);
    assert(ui->panels[reg].title);
    assert(ui->panels[reg].chrome_panel >= ui->panel_first);
    assert(ui->panels[reg].chrome_panel < ui->panel_end);
    assert(ui->chrome->panels[ui->panels[reg].chrome_panel].style == TORIRS_CHROME_PANEL_WINDOW);
    assert(ui->panels[reg].chrome_panel != ui->world_panel);
    assert(!ui->group_plus1[reg]);
    assert(g->member_count < EDITOR_UI_GROUP_MEMBERS_MAX);

    /* The first group a module joins is its default. */
    {
        int homed = 0;
        for( int i = 0; i < ui->group_count; i++ )
            for( int h = 0; h < ui->groups[i].home_count; h++ )
                if( ui->groups[i].home[h] == reg )
                    homed = 1;
        if( !homed )
        {
            assert(g->home_count < EDITOR_UI_GROUP_MEMBERS_MAX);
            g->home[g->home_count++] = reg;
        }
    }
    ui_group_insert(ui, g, reg, g->member_count);
    ui->laid_out = 0;
}

void
Editor_UiLeaveGroup(
    struct EditorUi* ui,
    const char* module)
{
    int reg;

    assert(ui);
    assert(module);
    reg = ui_registry_index(ui, module);
    assert(reg >= 0);
    assert(ui->group_plus1[reg]);
    ui_group_remove(ui, reg);
}

void
Editor_UiResetGroups(struct EditorUi* ui)
{
    assert(ui);
    /* Out of every group that is not its default... */
    for( int reg = 0; reg < ui->panel_count; reg++ )
    {
        struct EditorUiGroup* g = ui_group_of(ui, reg);
        int home = 0;
        if( !g )
            continue;
        for( int h = 0; h < g->home_count; h++ )
            if( g->home[h] == reg )
                home = 1;
        if( !home )
            ui_group_remove(ui, reg);
    }
    /* ...and into its default one, at its default place in the strip. */
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        for( int h = 0; h < g->home_count; h++ )
        {
            int const reg = g->home[h];
            int at = 0;
            if( ui->group_plus1[reg] )
                continue;
            for( int before = 0; before < h; before++ )
                if( ui_group_slot(g, g->home[before]) >= 0 )
                    at = ui_group_slot(g, g->home[before]) + 1;
            ui_group_insert(ui, g, reg, at);
        }
    }
    ui->laid_out = 0;
}

struct EditorUiGroup*
Editor_UiFindGroup(
    struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);
    for( int i = 0; i < ui->group_count; i++ )
        if( strcmp(ui->groups[i].name, name) == 0 )
            return &ui->groups[i];
    return NULL;
}

int
Editor_UiGroupOf(
    const struct EditorUi* ui,
    const char* module)
{
    int reg;

    assert(ui);
    assert(module);
    reg = ui_registry_index(ui, module);
    return reg >= 0 ? (int)ui->group_plus1[reg] - 1 : -1;
}

int
Editor_UiPanelInStrip(
    const struct EditorUi* ui,
    const char* module)
{
    int const group = Editor_UiGroupOf(ui, module);
    struct EditorUiGroup const* g;
    int slot;

    if( group < 0 )
        return 0;
    g = &ui->groups[group];
    slot = ui_group_slot(g, ui_registry_index(ui, module));
    assert(slot >= 0);
    return g->open[slot];
}

const char*
Editor_UiGroupShown(
    const struct EditorUi* ui,
    int group)
{
    struct EditorUiGroup const* g;

    assert(ui);
    assert(group >= 0);
    assert(group < ui->group_count);
    g = &ui->groups[group];
    return g->shown >= 0 ? ui->panels[g->members[g->shown]].name : NULL;
}

void
Editor_UiGroupShow(
    struct EditorUi* ui,
    const char* module)
{
    int reg;
    struct EditorUiGroup* g;

    assert(ui);
    assert(module);
    reg = ui_registry_index(ui, module);
    assert(reg >= 0);
    g = ui_group_of(ui, reg);
    assert(g);
    ui_group_show(ui, g, ui_group_slot(g, reg));
    ui_group_strip(ui, g);
    ui_group_place(ui, g);
}

/** The default groups (design #workbench): the right column's two and the
 *  bottom strip's, each holding whichever of its modules this window has. */
static void
ui_default_groups(struct EditorUi* ui)
{
    static const struct
    {
        const char* name;
        const char* title;
        const char* members[4];
    } k_groups[] = {
        { "right-top", "Catalog / Locs / Spawns / Inspector", { "catalog", "locs", "spawns", "inspector" } },
        { "right-mid", "Terrain / Palette / Catalog palette", { "terrain", "palette", "catalog_palette", NULL } },
        { "bottom", "Console / History / Problems", { "console", "history", "problems", NULL } },
    };

    for( int i = 0; i < (int)(sizeof(k_groups) / sizeof(k_groups[0])); i++ )
    {
        (void)Editor_UiRegisterGroup(ui, k_groups[i].name, k_groups[i].title);
        for( int m = 0; m < 4 && k_groups[i].members[m]; m++ )
            if( Editor_UiFindPanel(ui, k_groups[i].members[m]) )
                Editor_UiJoinGroup(ui, k_groups[i].members[m], k_groups[i].name);
    }
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
    "terrain", "palette", "locs", "spawns", "rect", "inspector", "squares", "areas", "catalog", "preview",
    "catalog_palette", "history", NULL,
};
#define UI_DOCK_COUNT ((int)(sizeof(k_dock_order) / sizeof(k_dock_order[0])) - 1)

/* The column per workspace (workbench 6.8): its own panels first, in the
 * design table's order, then the Build column's for whatever else is opened
 * there. A group takes the place of its first member in the order. Build's
 * is the default layout's own (k_dock_order), so the boot layout is the one
 * it always was. */
static const char* const k_dock_terrain[] = {
    "terrain", "palette", "squares", "inspector", "catalog_palette", "locs", "spawns", "rect", "areas",
    "catalog", "preview", "history", NULL,
};
static const char* const k_dock_script[] = {
    "record", "triggers", "locbuilder", "npcbuilder", "shelleditor", "shop", "quest", "terrain", "palette", "locs", "spawns",
    "rect", "inspector", "squares", "areas", "catalog", "preview", "catalog_palette", "history", NULL,
};
static const char* const k_dock_playtest[] = {
    "live", "accounts", "scenes", "encounter", "terrain", "palette", "locs", "spawns", "rect", "inspector", "squares", "areas",
    "catalog", "preview", "catalog_palette", "history", NULL,
};
/** Most entries one workspace's column order holds. */
#define UI_DOCK_MAX 24

static const char* const*
ui_dock_order(
    const struct EditorUi* ui,
    int* out_count)
{
    static const char* const* const k_orders[EDITOR_UI_WORKSPACES] = {
        k_dock_order, k_dock_terrain, k_dock_script, k_dock_playtest,
    };
    const char* const* order;
    int n = 0;

    assert(ui->workspace >= 0);
    assert(ui->workspace < EDITOR_UI_WORKSPACES);
    order = k_orders[ui->workspace];
    while( order[n] )
        n++;
    assert(n <= UI_DOCK_MAX);
    *out_count = n;
    return order;
}

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

/* ---- chrome scale and text size (workbench 6.0b) ----------------------------- */

/* The host's quarter steps (ui/torirs_chrome_scale_q.h) are the chrome
 * model's quarter units: one number crosses the shim unconverted. */
_Static_assert(TORIRS_CHROME_SCALEQ_ONE == TORIRS_CHROME_SCALE_Q_ONE, "one quarter unit");
_Static_assert(TORIRS_CHROME_SCALEQ_MIN == TORIRS_CHROME_SCALE_Q_MIN, "the same smallest scale");
_Static_assert(TORIRS_CHROME_SCALEQ_MAX == TORIRS_CHROME_SCALE_Q_MAX, "the same largest scale");

int
Editor_ChromeSetScaleQ(
    struct ToriRSChrome* chrome,
    int q)
{
    assert(chrome);
    assert(q >= TORIRS_CHROME_SCALEQ_MIN);
    assert(q <= TORIRS_CHROME_SCALEQ_MAX);
    if( ToriRSChrome_ScaleQ(chrome) == q )
        return 0;
    ToriRSChrome_SetScaleQ(chrome, q);
    return 1;
}

int
Editor_ChromeScaleQ(const struct ToriRSChrome* chrome)
{
    assert(chrome);
    return ToriRSChrome_ScaleQ(chrome);
}

int
Editor_ChromeScaleInteger(int q)
{
    assert(q >= TORIRS_CHROME_SCALEQ_MIN);
    assert(q <= TORIRS_CHROME_SCALEQ_MAX);
    /* ToriRSChrome_Scale is the tier the scale reads its bitmaps from, the
     * nearest at or above: answering anything else would have
     * App_SetChromeScale reset the quarter scale every frame. */
    return (q + EDITOR_UI_SCALEQ_ONE - 1) / EDITOR_UI_SCALEQ_ONE;
}

int
Editor_UiPx(
    const struct EditorUi* ui,
    int px)
{
    assert(ui);
    return ToriRSChrome_Px(ui->chrome, px);
}

int
Editor_UiChromeScaleFor(
    struct EditorUi* ui,
    int auto_q,
    int touch)
{
    int q;

    assert(ui);
    assert(auto_q >= TORIRS_CHROME_SCALEQ_MIN);
    assert(auto_q <= TORIRS_CHROME_SCALEQ_MAX);
    ui->scale_auto_q = auto_q;
    ui->scale_touch = touch ? 1 : 0;
    if( ui->prefs.chrome_scale_q == EDITOR_PREFS_SCALE_AUTO )
        return auto_q;
    q = ui->prefs.chrome_scale_q;
    /* The choice is kept (the same prefs file on a desk draws it as asked);
     * this device draws no smaller than a finger needs. */
    if( touch && q < TORIRS_CHROME_SCALEQ_TOUCH_MIN )
        q = TORIRS_CHROME_SCALEQ_TOUCH_MIN;
    return q;
}

void
Editor_UiSetChromeScalePref(
    struct EditorUi* ui,
    int q)
{
    assert(ui);
    assert(q == EDITOR_PREFS_SCALE_AUTO || ToriRSChromeScaleQ_StepIndex(q) >= 0);
    if( ui->prefs.chrome_scale_q == q )
        return;
    ui->prefs.chrome_scale_q = q;
    ui->prefs_serial++;
    if( q == EDITOR_PREFS_SCALE_AUTO )
        Editor_UiLog(ui, "editor: chrome scale Auto (the display: %d %%)", ToriRSChromeScaleQ_Percent(ui->scale_auto_q));
    else
        Editor_UiLog(ui, "editor: chrome scale %d %%", ToriRSChromeScaleQ_Percent(q));
}

/** The theme as Text size wants it: the face rows lay out in and their
 *  height. Asked every frame (two ints), so a theme the App installs later
 *  (App_SetChromeTheme, a skin change) wears this window's text size too. */
static void
ui_text_size_follow(struct EditorUi* ui)
{
    struct ToriRSChromeTheme theme = ui->chrome->theme;
    int const face = ui->prefs.text_large ? TORIRS_CHROME_FONT_MENU : TORIRS_CHROME_FONT_BODY;
    int const row_h = ui->prefs.text_large ? EDITOR_UI_LARGE_ROW_H : 0;

    /* Normal leaves a theme's own face alone (the flat developer look rows
     * in Small): only Large has an opinion, and turning it off restores
     * the face it replaced. */
    if( !ui->prefs.text_large && theme.row_h == 0 )
        return;
    if( ui->prefs.text_large && theme.font_row == face && theme.row_h == row_h )
        return;
    if( ui->prefs.text_large )
    {
        if( theme.row_h == 0 )
            ui->text_face_normal = theme.font_row;
        theme.font_row = face;
    }
    else
        theme.font_row = ui->text_face_normal;
    theme.row_h = row_h;
    ToriRSChrome_SetTheme(ui->chrome, &theme);
    /* Every row changed height: the windows the layout sized to their
     * content are a different size now. */
    ui->laid_out = 0;
}

void
Editor_UiSetTextLarge(
    struct EditorUi* ui,
    int large)
{
    assert(ui);
    large = large ? 1 : 0;
    if( ui->prefs.text_large == large )
        return;
    ui->prefs.text_large = large;
    ui->prefs_serial++;
    ui_text_size_follow(ui);
    Editor_UiLog(ui, "editor: text size %s", large ? "Large" : "Normal");
}

void
Editor_UiSetTextSmooth(
    struct EditorUi* ui,
    int smooth)
{
    assert(ui);
    smooth = smooth ? 1 : 0;
    if( ui->prefs.text_smooth == smooth )
        return;
    ui->prefs.text_smooth = smooth;
    ui->prefs_serial++;
    Editor_UiLog(ui, "editor: text rendering %s", smooth ? "smooth always" : "bitmap at whole scales");
}

int
Editor_UiTextSmooth(const struct EditorUi* ui)
{
    assert(ui);
    return ui->prefs.text_smooth;
}

void
Editor_UiApplyPrefs(
    struct EditorUi* ui,
    const struct Editor_Prefs* prefs)
{
    assert(ui);
    assert(prefs);
    assert(prefs->chrome_scale_q == EDITOR_PREFS_SCALE_AUTO || ToriRSChromeScaleQ_StepIndex(prefs->chrome_scale_q) >= 0);
    ui->prefs.chrome_scale_q = prefs->chrome_scale_q;
    ui->prefs.text_large = prefs->text_large ? 1 : 0;
    ui->prefs.text_smooth = prefs->text_smooth ? 1 : 0;
    ui_text_size_follow(ui);
}

/* ---- the Dock button ------------------------------------------------------------ */

int
Editor_UiModuleNeedsWorld(const char* module)
{
    assert(module);
    return strcmp(module, "world") == 0 || strcmp(module, "preview") == 0;
}

/** The first VLIST on chrome panel `panel` (the Catalog's rows), or -1. */
static int
ui_panel_first_vlist(
    const struct ToriRSChrome* chrome,
    int panel)
{
    for( int i = 0; i < chrome->widget_count; i++ )
        if( chrome->widgets[i].panel == panel && chrome->widgets[i].kind == TORIRS_CHROME_W_VLIST )
            return i;
    return -1;
}

int
Editor_UiDockBack(struct EditorUi* ui)
{
    int32_t values[4];
    int at = -1;
    int list;

    assert(ui);
    assert(ui->solo_panel >= 0);
    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].chrome_panel == ui->solo_panel )
            at = i;
    assert(at >= 0);
    values[0] = at;
    values[1] = (int32_t)++ui->dock_serial;
    /* Where the reader was, so the panel docks back scrolled where it was
     * left: the panel's own scroll (logical px, the same at either window's
     * scale) and its list's first row (-1: it has none). */
    values[2] = ToriRSChrome_PanelScroll(ui->chrome, ui->solo_panel);
    list = ui_panel_first_vlist(ui->chrome, ui->solo_panel);
    values[3] = list >= 0 ? ui->chrome->widgets[list].imin : -1;
    if( !Editor_UiPublish(ui, TORIRSMAPED_STATE_DOCK_PANEL, values, 4) )
    {
        Editor_UiLog(ui, "editor: Dock %s: no session to tell the main window through; close this window and "
                         "use View > %s there", ui->panels[at].title, ui->panels[at].title);
        return 0;
    }
    Editor_UiLog(ui, "editor: %s docks back into the main window; this window closes", ui->panels[at].title);
    ui->dock_exit = 1;
    return 1;
}

/** Per frame: the solo window's title-bar Dock. */
static void
ui_dock_tick(struct EditorUi* ui)
{
    int const pressed = ToriRSChrome_TakeDock(ui->chrome);

    if( pressed < 0 )
        return;
    /* Only a solo window wears it; a press on a stale box is nobody's. */
    if( ui->solo_panel >= 0 && pressed == ui->solo_panel )
        (void)Editor_UiDockBack(ui);
}

/**
 * Another window of the Client docked registry module `index`. The window
 * that shows the panel set (not solo, the editor open) shows it again exactly
 * as View > <panel> does -- the chrome window shown, its group's strip and
 * the layout following -- and a panel already open stays as it is. A solo
 * window, or a plain viewer with the panels hidden, is not the one meant.
 */
static void
ui_dock_heard(
    struct EditorUi* ui,
    const int32_t* values,
    int count)
{
    struct EditorUiPanel const* panel;
    int const index = values[0];
    int list;

    if( ui->solo_panel >= 0 || !ui->visible )
        return;
    if( index < 0 || index >= ui->panel_count )
    {
        Editor_UiLog(ui, "editor: a window docked panel %d, which this window does not have", index);
        return;
    }
    panel = &ui->panels[index];
    if( !panel->title || panel->chrome_panel < 0 )
        return;
    if( !ui->chrome->panels[panel->chrome_panel].visible )
        ToriRSChrome_PanelSetVisible(ui->chrome, panel->chrome_panel, 1);
    /* The popped window's place (a two-value dock from an older build has
     * none): the panel's scroll, clamped by its next Build, and its list's
     * first row, clamped to its rows. */
    if( count >= 4 )
    {
        ToriRSChrome_PanelSetScroll(ui->chrome, panel->chrome_panel, values[2] > 0 ? values[2] : 0);
        list = ui_panel_first_vlist(ui->chrome, panel->chrome_panel);
        if( list >= 0 && values[3] >= 0 )
            ToriRSChrome_VListSetScroll(ui->chrome, list, values[3]);
    }
    Editor_UiLog(ui, "editor: %s docked back from its own window", panel->title);
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
    /* In quarter units, so a 1.5x scale scales the windows by 3/2. At the
     * integer scales the ratio is the same fraction it always was (8/4 is
     * 2/1) and every truncation lands where it did. */
    int const scale = Editor_ChromeScaleQ(ui->chrome);
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
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        g->x = g->x * scale / was;
        g->y = g->y * scale / was;
        g->w = g->w * scale / was;
        g->body_h = g->body_h * scale / was;
        g->pending_h = g->pending_h * scale / was;
        g->frame_h = 0;
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
    /** The chrome panel placed: a module's window, or a group's frame. */
    int panel;
    struct EditorUiPanel* module;
    /** A group's place: its frame and members are placed together. */
    struct EditorUiGroup* group;
    int natural;
    int min;
    int h;
    /** The column it was admitted to: 0 is the rightmost. */
    int col;
    /** Compact dock: shown as its title bar, `coll_h` tall. */
    int collapsed;
    int coll_h;
};

/** The compact setting resolved against the surface: the setting when it
 *  says, else automatic -- below EDP_COMPACT_H rows or EDP_COMPACT_W columns
 *  of LOGICAL pixels (device px over the chrome scale). */
static int
ui_compact_effective(const struct EditorUi* ui)
{
    int const scale = ToriRSChrome_Scale(ui->chrome) > 0 ? ToriRSChrome_Scale(ui->chrome) : 1;

    if( ui->compact_mode >= 0 )
        return ui->compact_mode;
    return ui->chrome->surface_h / scale < EDP_COMPACT_H || ui->chrome->surface_w / scale < EDP_COMPACT_W;
}

int
Editor_UiCompact(const struct EditorUi* ui)
{
    assert(ui);
    return ui_compact_effective(ui);
}

void
Editor_UiSetCompact(
    struct EditorUi* ui,
    int mode)
{
    assert(ui);
    assert(mode >= -1);
    assert(mode <= 1);
    ui->compact_mode = mode;
    ui->laid_out = 0;
}

void
Editor_UiSetWindowBox(
    struct EditorUi* ui,
    int x,
    int y,
    int w,
    int h)
{
    assert(ui);
    ui->window_x = x;
    ui->window_y = y;
    ui->window_w = w;
    ui->window_h = h;
}

/** The bottom strip's height `h` (the console, or its group's frame) held so
 *  the world keeps EDP_WORLD_MIN_PERCENT of the surface beside the dock at
 *  `dock_x` (shortened to that, scrolling); 0 when that leaves it under half
 *  a dock panel. */
static int
ui_strip_capped(
    struct EditorUi* ui,
    int world,
    int h,
    int left,
    int dock_x,
    int avail,
    int grid)
{
    struct ToriRSChrome* chrome = ui->chrome;
    /* The world is the World panel's BODY: its title bar and rails come off
     * the region first (the same insets at any box). */
    int inset_w = 0;
    int inset_h = 0;
    int body_w;

    if( dock_x - left <= 0 )
        return h;
    if( world >= 0 )
    {
        struct ToriRSChromeRect const box = ToriRSChrome_PanelRect(chrome, world);
        struct ToriRSChromeRect const body = ToriRSChrome_PanelBodyRect(chrome, world);
        inset_w = box.w - body.w;
        inset_h = box.h - body.h;
    }
    body_w = dock_x - left - inset_w;
    if( body_w > 0 )
    {
        int64_t const min_body_h =
            ((int64_t)chrome->surface_w * chrome->surface_h * EDP_WORLD_MIN_PERCENT / 100 + body_w - 1) / body_w;
        int const cap = ui_round_down(avail - inset_h - (int)min_body_h, grid);
        if( h > cap )
            h = cap;
    }
    if( h < edp_px(ui, EDP_DOCK_MIN_H) / 2 )
        h = 0;
    return h;
}

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
    static const char* const k_strips[] = { "console", "world", "statusbar" };

    if( ui->booted_layout )
        return;
    for( int d = 0; d < UI_DOCK_COUNT; d++ )
    {
        int const panel = ui_chrome_panel(ui, k_dock_order[d]);
        if( panel >= 0 )
            ui->member[panel - ui->panel_first] = ui->boot_member[panel - ui->panel_first] =
                (unsigned char)(chrome->panels[panel].visible ? 1 : 0);
    }
    for( int s = 0; s < 3; s++ )
    {
        int const panel = ui_chrome_panel(ui, k_strips[s]);
        if( panel >= 0 )
            ui->member[panel - ui->panel_first] = ui->boot_member[panel - ui->panel_first] =
                (unsigned char)(chrome->panels[panel].visible ? 1 : 0);
    }
    /* A group is in the default set when any of its members is. */
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        int const at = g->chrome_panel - ui->panel_first;
        int any = chrome->panels[g->chrome_panel].visible || ui_group_has_open(g);
        for( int slot = 0; slot < g->member_count; slot++ )
            any |= chrome->panels[ui_group_window(ui, g, slot)].visible;
        ui->member[at] = ui->boot_member[at] = (unsigned char)(any ? 1 : 0);
    }
    /* The strips as they booted, for the Build workspace's default. */
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        g->boot_shown = g->shown >= 0 && g->shown < g->member_count ? g->shown : -1;
        for( int slot = 0; slot < g->member_count; slot++ )
        {
            g->boot_open[slot] =
                (unsigned char)(g->open[slot] || chrome->panels[ui_group_window(ui, g, slot)].visible ? 1 : 0);
            if( g->boot_shown < 0 && g->boot_open[slot] && chrome->panels[ui_group_window(ui, g, slot)].visible )
                g->boot_shown = slot;
        }
        if( g->boot_shown < 0 )
            for( int slot = 0; slot < g->member_count && g->boot_shown < 0; slot++ )
                if( g->boot_open[slot] )
                    g->boot_shown = slot;
    }
    ui->booted_layout = 1;
}

/** Make a group measurable at width `w` in the next Build: the frame and
 *  every member window shown, content-sized. What showed before is kept in
 *  `saved` for ui_group_measure_end. */
static void
ui_group_measure_begin(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int w,
    unsigned char* saved)
{
    struct ToriRSChrome* chrome = ui->chrome;

    saved[0] = (unsigned char)chrome->panels[g->chrome_panel].visible;
    ToriRSChrome_PanelSetFixedWidth(chrome, g->chrome_panel, w);
    ToriRSChrome_PanelSetFixedHeight(chrome, g->chrome_panel, 0);
    ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, 1);
    for( int slot = 0; slot < g->member_count; slot++ )
    {
        int const panel = ui_group_window(ui, g, slot);
        saved[1 + slot] = (unsigned char)chrome->panels[panel].visible;
        ToriRSChrome_PanelSetFixedWidth(chrome, panel, w);
        ToriRSChrome_PanelSetFixedHeight(chrome, panel, 0);
        ToriRSChrome_PanelSetVisible(chrome, panel, 1);
    }
}

/** After the Build: the group's heights (its strip plus its largest
 *  member's, and the least of that it takes), and every window back as it
 *  showed. */
static void
ui_group_measure_end(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    const unsigned char* saved,
    int* out_natural,
    int* out_min)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const floor_h = edp_px(ui, EDP_DOCK_MIN_H);
    int const frame_h = ToriRSChrome_PanelRect(chrome, g->chrome_panel).h;
    int natural = 0;
    int min = 0;

    if( frame_h > 0 )
        g->frame_h = frame_h;
    for( int slot = 0; slot < g->member_count; slot++ )
    {
        int const panel = ui_group_window(ui, g, slot);
        int const h = ToriRSChrome_PanelRect(chrome, panel).h;
        int const least = h < floor_h ? h : floor_h;
        if( h > natural )
            natural = h;
        if( least > min )
            min = least;
        ToriRSChrome_PanelSetVisible(chrome, panel, saved[1 + slot]);
    }
    ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, saved[0]);
    *out_natural = g->frame_h + natural;
    *out_min = g->frame_h + min;
}

/** Dock a group into the box (x, y) w x h: the frame at the top, every
 *  member window under its strip (the shown one showing). */
static void
ui_group_dock(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    int x,
    int y,
    int w,
    int h)
{
    struct ToriRSChrome* chrome = ui->chrome;

    ToriRSChrome_PanelSetFixedWidth(chrome, g->chrome_panel, w);
    ToriRSChrome_PanelSetFixedHeight(chrome, g->chrome_panel, 0);
    ToriRSChrome_PanelMove(chrome, g->chrome_panel, x, y);
    g->x = x;
    g->y = y;
    g->w = w;
    g->body_h = h - g->frame_h > 0 ? h - g->frame_h : 0;
    g->pending_h = 0;
    g->parked = 0;
    for( int slot = 0; slot < g->member_count; slot++ )
    {
        int const panel = ui_group_window(ui, g, slot);
        if( !ToriRSChrome_PanelIsMaximised(chrome, panel) )
            ui_group_fill(ui, g, panel);
        ToriRSChrome_PanelSetVisible(chrome, panel, slot == g->shown);
    }
    g->placed = g->shown >= 0 ? ui_group_window(ui, g, g->shown) : -1;
    ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, g->shown >= 0);
}

/** A popped-out window's layout: its one panel over the whole surface, every
 *  other registry window hidden. */
static void
ui_layout_solo(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const solo = ui->solo_panel;

    for( int i = ui->panel_first; i < ui->panel_end && i < chrome->panel_count; i++ )
        if( i != solo && !chrome->panels[i].modal )
            ToriRSChrome_PanelSetVisible(chrome, i, 0);
    ToriRSChrome_PanelSetClosable(chrome, solo, 0);
    ToriRSChrome_PanelSetPoppable(chrome, solo, 0);
    ToriRSChrome_PanelSetResizable(chrome, solo, 0);
    /* The way back in from this side (design #workbench "Every panel pops
     * out"): the title bar's Dock, at its right end where the main window's
     * Close and Pop out sit -- the OS window's own close ends it. */
    ToriRSChrome_PanelSetDockable(chrome, solo, 1);
    ToriRSChrome_PanelSetFixedWidth(chrome, solo, chrome->surface_w);
    ToriRSChrome_PanelSetFixedHeight(chrome, solo, chrome->surface_h);
    ToriRSChrome_PanelSetScrollable(chrome, solo, 1);
    ToriRSChrome_PanelMove(chrome, solo, 0, 0);
    ToriRSChrome_PanelSetVisible(chrome, solo, 1);
    ui->world.x = 0;
    ui->world.y = 0;
    ui->world.w = chrome->surface_w;
    ui->world.h = chrome->surface_h;
    ui->dock_x = chrome->surface_w;
    ui->dock_w = 0;
    ui->dock_top = 0;
    ui->dock_bottom = chrome->surface_h;
    ui->layout_surface_w = chrome->surface_w;
    ui->layout_surface_h = chrome->surface_h;
    ui->laid_out = 1;
}

void
Editor_UiLayout(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome;
    struct UiDockSlot slots[UI_DOCK_MAX];
    int slot_count = 0;
    int dock_count = 0;
    const char* const* dock_order = ui_dock_order(ui, &dock_count);
    int const grid = edp_px(ui, EDP_GRID);
    int surface_w;
    int surface_h;
    int top = 0;
    int left = 0;
    /** Where the status bar starts: everything else stops above it. */
    int bottom;
    int world_bottom;
    int dock_w;
    int dock_x;
    int menubar;
    int statusbar;
    int rail;
    int console;
    int world;
    int avail;
    int used;
    int world_wanted;
    int columns;
    int column_h;
    int compact_col;
    /* The bottom strip: the console's window, or its group's frame. */
    struct EditorUiGroup* strip_group = NULL;
    int strip;
    int group_added[EDITOR_UI_GROUPS_MAX];
    unsigned char group_saved[EDITOR_UI_GROUPS_MAX][EDITOR_UI_GROUP_MEMBERS_MAX + 1];
    int strip_natural = 0;
    int strip_min = 0;
    int strip_measured = 0;

    assert(ui);
    chrome = ui->chrome;
    surface_w = chrome->surface_w;
    surface_h = chrome->surface_h;
    assert(surface_w > 0);
    assert(surface_h > 0);

    ui->layout_runs++;
    if( ui->solo_panel >= 0 )
    {
        ui_layout_solo(ui);
        return;
    }
    /* The strips as the windows have them now (a window shown or closed
     * since the last frame is in or out of its strip). */
    ui_groups_follow(ui);
    menubar = ui_chrome_panel(ui, "menubar");
    statusbar = ui_chrome_panel(ui, "statusbar");
    rail = ui_chrome_panel(ui, "toolrail");
    console = ui_chrome_panel(ui, "console");
    world = ui_chrome_panel(ui, "world");
    ui_capture_boot(ui);
    ui->compact = ui_compact_effective(ui);

    /* Every registry window measures at its full height: what the compact
     * column collapses, it collapses below, after measuring. */
    for( int i = ui->panel_first; i < ui->panel_end && i < chrome->panel_count; i++ )
        if( chrome->panels[i].collapsed )
            ToriRSChrome_PanelSetCollapsed(chrome, i, 0);
    memset(group_added, 0, sizeof(group_added));
    strip = console;
    if( console >= 0 )
    {
        strip_group = ui_group_of(ui, ui_registry_index(ui, "console"));
        if( strip_group )
            strip = strip_group->chrome_panel;
    }

    /* Who is in the column: the default set (and anything open now), minus
     * what the user closed or dragged out. The first layout fixes the
     * default set as what the modules showed with the editor. */
    for( int d = 0; d < dock_count; d++ )
    {
        struct EditorUiPanel* module = Editor_UiFindPanel(ui, dock_order[d]);
        int const panel = ui_chrome_panel(ui, dock_order[d]);
        int at;

        if( panel < 0 )
            continue;
        /* A member's place is its group's: the first member in this order
         * puts the group here, and the group is one window of the column. */
        if( ui_group_of(ui, (int)(module - ui->panels)) )
        {
            struct EditorUiGroup* g = ui_group_of(ui, (int)(module - ui->panels));
            int const frame = g->chrome_panel;
            if( g == strip_group || group_added[g - ui->groups] )
                continue;
            group_added[g - ui->groups] = 1;
            at = frame - ui->panel_first;
            ui->docked[at] = 0;
            if( chrome->panels[frame].user_placed || !ui_group_has_open(g) )
                continue;
            if( !chrome->panels[frame].visible && !g->parked && (!ui->member[at] || ui->closed[at]) )
                continue;
            if( g->parked && ui->closed[at] )
                continue;
            ui->member[at] = 1;
            slots[slot_count].panel = frame;
            slots[slot_count].module = NULL;
            slots[slot_count].group = g;
            slots[slot_count].col = 0;
            slot_count++;
            continue;
        }
        at = panel - ui->panel_first;
        ui->docked[at] = 0;
        if( chrome->panels[panel].user_placed )
            continue;
        if( !chrome->panels[panel].visible && (!ui->member[at] || ui->closed[at]) )
            continue;
        ui->member[at] = 1;
        slots[slot_count].panel = panel;
        slots[slot_count].module = module;
        slots[slot_count].group = NULL;
        slots[slot_count].col = 0;
        slot_count++;
    }

    /* The menu bar spans the top; everything else starts below it. */
    if( menubar >= 0 && chrome->panels[menubar].visible )
    {
        ToriRSChrome_PanelSetFixedWidth(chrome, menubar, surface_w);
        ToriRSChrome_PanelMove(chrome, menubar, 0, 0);
        top = ToriRSChrome_PanelRect(chrome, menubar).h;
    }
    /* The status bar spans the foot; everything else ends above it. Its
     * height is its face's line box (20 px at 1x), known from the last
     * Build like the menu bar's. */
    bottom = surface_h;
    if( statusbar >= 0 && chrome->panels[statusbar].visible )
    {
        int const h = ToriRSChrome_PanelRect(chrome, statusbar).h;
        ToriRSChrome_PanelSetFixedWidth(chrome, statusbar, surface_w);
        ToriRSChrome_PanelMove(chrome, statusbar, 0, surface_h - h);
        bottom = surface_h - h;
    }
    avail = bottom - top;

    /* The rail against the left edge, wrapped into as many columns as its
     * buttons need to stay on the surface. */
    if( rail >= 0 && chrome->panels[rail].visible && !chrome->panels[rail].user_placed )
    {
        struct ToriRSChromePanel const* p = &chrome->panels[rail];
        int const one_w = edp_px(ui, EDP_RAIL_W);
        int const pitch = edp_px(ui, EDP_RAIL_PITCH);
        int const columns_now = p->fixed_w > one_w ? 1 + (p->fixed_w - one_w) / pitch : 1;
        /* The rail's height at c columns: its rows (the buttons wrapped)
         * at the button pitch, plus the strip's own padding, which the
         * measured rect gives once the rows it holds now are taken off.
         * (Dividing the one-column height by c undercounts the padding,
         * and at 2x the last row then overlapped the console.) */
        int const buttons = Editor_ToolRail_ButtonCount();
        int const rows_now = (buttons + columns_now - 1) / columns_now;
        int pad = ToriRSChrome_PanelRect(chrome, rail).h - rows_now * pitch;
        int columns = 1;

        if( pad < 0 )
            pad = 0;
        /* As many columns as it takes: never past the bottom. */
        while( columns < 8 && ((buttons + columns - 1) / columns) * pitch + pad > avail )
            columns++;
        ToriRSChrome_PanelSetFixedWidth(chrome, rail, one_w + (columns - 1) * pitch);
        ToriRSChrome_PanelMove(chrome, rail, 0, top);
        left = one_w + (columns - 1) * pitch;
    }

    /* The World panel is wanted unless the user closed it or popped it out
     * (a World the user placed by hand floats: the column leaves it be). */
    {
        int const at = world >= 0 ? world - ui->panel_first : -1;
        world_wanted = world < 0 || chrome->panels[world].user_placed || chrome->panels[world].visible
                       || (ui->member[at] && !ui->closed[at]);
    }

    /* The column's width: a share of the surface, never so wide the world
     * would be narrower than it. With no World the space is the panels':
     * as many columns of that width as fit between the rail and the right
     * edge, widened to fill it exactly. */
    dock_w = ui_round_down(surface_w * EDP_DOCK_W_PERCENT / 100, grid);
    if( dock_w < edp_px(ui, EDP_DOCK_W_MIN) )
        dock_w = edp_px(ui, EDP_DOCK_W_MIN);
    if( dock_w > edp_px(ui, EDP_DOCK_W_MAX) )
        dock_w = edp_px(ui, EDP_DOCK_W_MAX);
    if( surface_w - left - dock_w < dock_w )
        dock_w = ui_round_down((surface_w - left) / 2, grid);
    columns = 1;
    if( !world_wanted && dock_w > 0 )
    {
        columns = (surface_w - left) / dock_w;
        if( columns < 1 )
            columns = 1;
        if( columns > dock_count )
            columns = dock_count;
        dock_w = ui_round_down((surface_w - left) / columns, grid);
    }
    dock_x = surface_w - dock_w * columns;

    /* Measure every candidate at the column's width and the console at the
     * world's (with no World, under every column), content-sized: one Build
     * answers all of them. */
    for( int i = 0; i < slot_count; i++ )
    {
        if( slots[i].group )
        {
            ui_group_measure_begin(ui, slots[i].group, dock_w, group_saved[slots[i].group - ui->groups]);
            continue;
        }
        ToriRSChrome_PanelSetFixedWidth(chrome, slots[i].panel, dock_w);
        ToriRSChrome_PanelSetFixedHeight(chrome, slots[i].panel, 0);
        ToriRSChrome_PanelSetVisible(chrome, slots[i].panel, 1);
    }
    if( strip_group && !chrome->panels[strip].user_placed && ui_group_has_open(strip_group) )
    {
        ui_group_measure_begin(ui, strip_group, world_wanted ? dock_x - left : surface_w - left,
            group_saved[strip_group - ui->groups]);
        strip_measured = 1;
    }
    else if( console >= 0 && !strip_group && !chrome->panels[console].user_placed )
    {
        ToriRSChrome_PanelSetFixedWidth(chrome, console, world_wanted ? dock_x - left : surface_w - left);
        ToriRSChrome_PanelSetFixedHeight(chrome, console, 0);
    }
    ToriRSChrome_Build(chrome);
    /* The groups' heights, and their windows back as they showed. */
    for( int i = 0; i < slot_count; i++ )
        if( slots[i].group )
            ui_group_measure_end(
                ui, slots[i].group, group_saved[slots[i].group - ui->groups], &slots[i].natural, &slots[i].min);
    if( strip_measured )
        ui_group_measure_end(ui, strip_group, group_saved[strip_group - ui->groups], &strip_natural, &strip_min);

    /* The console is a strip under the world when it takes a quarter of the
     * height or less (the plain console: and leaves the world
     * EDP_WORLD_MIN_PERCENT of the surface, shortened to that, scrolling,
     * down to half a dock panel); otherwise the world keeps the height and
     * the console waits for the View menu. */
    world_bottom = bottom;
    if( strip_group && !chrome->panels[strip].user_placed )
    {
        /* The console's group: its strip plus its tallest member, held to a
         * quarter of the height (the windows scroll) so long as each member
         * keeps its few rows. */
        int const at = strip - ui->panel_first;
        int const wanted = strip_measured
                           && (chrome->panels[strip].visible || strip_group->parked
                               || (ui->member[at] && !ui->closed[at]))
                           && !(strip_group->parked && ui->closed[at]);
        int h = strip_natural;
        int fits;

        if( h * 4 > avail )
            h = avail / 4;
        /* The world's share holds the strip down too, but never below the
         * group's minimum: a group is shown at that, not parked for it. */
        if( world_wanted && h > strip_min )
        {
            int const capped = ui_strip_capped(ui, world, h, left, dock_x, avail, grid);
            if( capped < h )
                h = capped > strip_min ? capped : strip_min;
        }
        fits = strip_measured && h > 0 && h >= strip_min;
        ui->docked[at] = 0;
        if( wanted && fits )
        {
            ui_group_dock(ui, strip_group, left, bottom - h, world_wanted ? dock_x - left : surface_w - left, h);
            world_bottom = bottom - h;
            ui->docked[at] = 1;
            ui->member[at] = 1;
            ui->closed[at] = 0;
            ui->flown[at] = 0;
        }
        else if( wanted )
            ui_group_park(ui, strip_group);
    }
    else if( console >= 0 && !strip_group && !chrome->panels[console].user_placed )
    {
        int const at = console - ui->panel_first;
        int h = ToriRSChrome_PanelRect(chrome, console).h;
        int const wanted = chrome->panels[console].visible || (ui->member[at] && !ui->closed[at]);
        int fits;

        if( world_wanted && h > 0 )
            h = ui_strip_capped(ui, world, h, left, dock_x, avail, grid);
        fits = h > 0 && h * 4 <= avail;

        ui->docked[at] = 0;
        if( wanted && fits )
        {
            ToriRSChrome_PanelSetFixedHeight(chrome, console, h);
            ToriRSChrome_PanelSetScrollable(chrome, console, 1);
            ToriRSChrome_PanelMove(chrome, console, left, bottom - h);
            ToriRSChrome_PanelSetVisible(chrome, console, 1);
            world_bottom = bottom - h;
            ui->docked[at] = 1;
            ui->member[at] = 1;
        }
        else if( wanted )
            ToriRSChrome_PanelSetVisible(chrome, console, 0);
    }
    /* With no World the console strip runs under the columns too. */
    column_h = world_wanted ? avail : world_bottom - top;
    /* Compact: the one column beside the world holds every member, collapsed
     * to its title bar when it cannot have its minimum. */
    compact_col = ui->compact && world_wanted && columns == 1;

    /* A group's frame was measured with its members above; it is never
     * collapsed (its strip is the tabs), so its title bar is its minimum. */
    for( int i = 0; i < slot_count; i++ )
    {
        if( !slots[i].group )
            ui_dock_measure(ui, &slots[i]);
        slots[i].collapsed = 0;
        slots[i].coll_h = slots[i].group ? slots[i].min : ToriRSChrome_PanelCollapsedHeight(chrome, slots[i].panel);
    }
    if( compact_col )
    {
        /* Compact: every member gets its title bar first (the last ones are
         * hidden only when even the titles do not fit); then, most recently
         * asked for first, each panel the user wants expanded takes its
         * minimum while the column still holds it -- so expanding one
         * collapses the least recently expanded when there is no room. */
        int order[UI_DOCK_MAX];
        used = 0;
        for( int i = 0; i < slot_count; i++ )
        {
            slots[i].collapsed = !slots[i].group;
            slots[i].h = slots[i].coll_h;
            slots[i].col = 0;
            used += slots[i].coll_h;
        }
        for( int i = slot_count - 1; i >= 0 && used > column_h; i-- )
        {
            if( slots[i].group )
                ui_group_park(ui, slots[i].group);
            else
                ToriRSChrome_PanelSetVisible(chrome, slots[i].panel, 0);
            used -= slots[i].coll_h;
            slots[i].h = -1;
        }
        for( int i = 0; i < slot_count; i++ )
        {
            int k = i;
            unsigned const serial = ui->expand_serial[slots[i].panel - ui->panel_first];
            /* Insertion by serial, descending; equal serials keep dock order. */
            while( k > 0 && ui->expand_serial[slots[order[k - 1]].panel - ui->panel_first] < serial )
            {
                order[k] = order[k - 1];
                k--;
            }
            order[k] = i;
        }
        for( int k = 0; k < slot_count; k++ )
        {
            int const i = order[k];
            int const at = slots[i].panel - ui->panel_first;
            if( slots[i].h < 0 || !slots[i].collapsed || !ui->expanded[at] )
                continue;
            if( used + slots[i].min - slots[i].coll_h > column_h )
                continue;
            used += slots[i].min - slots[i].coll_h;
            slots[i].collapsed = 0;
            slots[i].h = slots[i].min;
        }
    }
    else
    {
        /* Admit candidates top-down while every admitted one still gets its
         * minimum, a column at a time; the rest of the default set is hidden. */
        int col = 0;
        used = 0;
        for( int i = 0; i < slot_count; i++ )
        {
            if( used + slots[i].min > column_h && used > 0 && col + 1 < columns )
            {
                col++;
                used = 0;
            }
            if( used + slots[i].min > column_h )
            {
                if( slots[i].group )
                    ui_group_park(ui, slots[i].group);
                else
                    ToriRSChrome_PanelSetVisible(chrome, slots[i].panel, 0);
                slots[i].h = -1;
                continue;
            }
            slots[i].h = slots[i].min;
            slots[i].col = col;
            used += slots[i].min;
        }
    }
    /* Then, per column, share what is left in proportion to what each still
     * asks for (so the picker's well grows with the terrain panel rather than
     * after it), each up to its asking height; a stretching panel (the
     * picker's well) takes any remainder. A collapsed panel is its title bar
     * and takes nothing. */
    for( int col = 0; col < columns; col++ )
    {
        int left_over = column_h;
        int64_t wanted = 0;
        int stretcher = -1;
        for( int i = 0; i < slot_count; i++ )
            if( slots[i].h >= 0 && slots[i].col == col )
                left_over -= slots[i].h;
        for( int i = 0; i < slot_count; i++ )
            if( slots[i].h >= 0 && slots[i].col == col && !slots[i].collapsed && slots[i].natural > slots[i].h )
                wanted += slots[i].natural - slots[i].h;
        if( wanted > 0 && left_over > 0 )
        {
            int64_t const share = wanted < left_over ? wanted : left_over;
            int given = 0;
            for( int i = 0; i < slot_count; i++ )
            {
                int extra;
                if( slots[i].h < 0 || slots[i].col != col || slots[i].collapsed || slots[i].natural <= slots[i].h )
                    continue;
                extra = (int)((int64_t)(slots[i].natural - slots[i].h) * share / wanted);
                slots[i].h += extra;
                given += extra;
            }
            left_over -= given;
        }
        /* The first stretching panel of the column: the picker's well over
         * the Preview's when both are docked in one column. */
        for( int i = 0; i < slot_count && stretcher < 0; i++ )
            if( slots[i].h >= 0 && slots[i].col == col && !slots[i].collapsed && slots[i].module
                && slots[i].module->stretch )
                stretcher = i;
        if( stretcher >= 0 && left_over > 0 )
            slots[stretcher].h += left_over;
    }
    for( int col = 0; col < columns; col++ )
    {
        int y = top;
        int const x = surface_w - dock_w * (col + 1);
        for( int i = 0; i < slot_count; i++ )
        {
            int const at = slots[i].panel - ui->panel_first;
            if( slots[i].h < 0 || slots[i].col != col )
                continue;
            if( slots[i].group )
                ui_group_dock(ui, slots[i].group, x, y, dock_w, slots[i].h);
            else
            {
                ToriRSChrome_PanelSetCollapsed(chrome, slots[i].panel, slots[i].collapsed);
                ToriRSChrome_PanelSetFixedHeight(chrome, slots[i].panel, slots[i].h);
                /* A fixed height drops rows past the bottom unless the panel
                 * scrolls; content that grows later (the inspector's rows)
                 * must not vanish. */
                ToriRSChrome_PanelSetScrollable(chrome, slots[i].panel, 1);
                ToriRSChrome_PanelMove(chrome, slots[i].panel, x, y);
            }
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
/**
 * The i-th window ui_layout_follow keeps -- the registry's panels, then the
 * groups' frames -- as its chrome panel, or -1 for a member of a group (its
 * frame stands for it). `*out_titled` says it is a View-menu window.
 */
static int
ui_follow_window(
    const struct EditorUi* ui,
    int i,
    int* out_titled)
{
    if( i < ui->panel_count )
    {
        *out_titled = ui->panels[i].title != NULL;
        return ui->group_plus1[i] ? -1 : ui->panels[i].chrome_panel;
    }
    *out_titled = 1;
    return ui->groups[i - ui->panel_count].chrome_panel;
}

/** A window's height as the flyouts stack it: a group is its strip and the
 *  shown window under it. 0 when it was never built. */
static int
ui_follow_height(
    struct EditorUi* ui,
    int panel)
{
    struct EditorUiGroup* g = ui_frame_group(ui, panel);
    int const h = ui->chrome->panels[panel].last_rect.h;

    if( g && g->shown >= 0 )
        return h + ui->chrome->panels[ui_group_window(ui, g, g->shown)].last_rect.h;
    return h;
}

static void
ui_layout_follow(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int y = ui->dock_top;
    int const windows = ui->panel_count + ui->group_count;

    /* A popped-out window has no column to fly beside. */
    if( ui->solo_panel >= 0 )
        return;

    /* Where the open flyouts end. */
    for( int i = 0; i < windows; i++ )
    {
        int titled;
        int const panel = ui_follow_window(ui, i, &titled);
        struct ToriRSChromeRect r;
        if( panel < ui->panel_first || panel >= ui->panel_end || !ui->flown[panel - ui->panel_first]
            || !chrome->panels[panel].visible )
            continue;
        r = ToriRSChrome_PanelRect(chrome, panel);
        if( r.y + ui_follow_height(ui, panel) > y )
            y = r.y + ui_follow_height(ui, panel);
    }
    for( int i = 0; i < windows; i++ )
    {
        int titled;
        int const panel = ui_follow_window(ui, i, &titled);
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
            /* A docked panel just closed (its Close box, the View menu, a
             * pop-out): lay out again, so the rest of the column -- or, for
             * World, the columns -- take the space it left. */
            if( ui->docked[at] && ui->member[at] && !ui->closed[at] && !p->user_placed )
                ui->laid_out = 0;
            if( ui->docked[at] && ui->member[at] )
                ui->closed[at] = 1;
            ui->flown[at] = 0;
            continue;
        }
        /* And one coming back takes its place in the layout again (or,
         * when the column has no room for it, flies out beside it:
         * ui_default_layout). */
        if( ui->closed[at] && !p->user_placed )
        {
            ui->laid_out = 0;
            ui->layout_reopened = panel;
        }
        ui->closed[at] = 0;
        if( ui->docked[at] || ui->flown[at] || p->user_placed || p->modal
            || p->style != TORIRS_CHROME_PANEL_WINDOW || !titled )
            continue;
        w = p->last_rect.w > 0 ? p->last_rect.w : p->fixed_w;
        x = ui->dock_x - w;
        if( x < ui->world.x )
            x = ui->world.x;
        if( y >= ui->dock_bottom - edp_px(ui, EDP_DOCK_MIN_H) )
            y = ui->dock_top;
        ToriRSChrome_PanelMove(chrome, panel, x, y);
        ui->flown[at] = 1;
        y += ui_follow_height(ui, panel) > 0 ? ui_follow_height(ui, panel) : edp_px(ui, EDP_DOCK_MIN_H);
    }
}

static void
ui_workspace_default(
    struct EditorUi* ui,
    int workspace,
    int log);
static void
ui_default_layout(struct EditorUi* ui);

void
Editor_UiResetLayout(struct EditorUi* ui)
{
    assert(ui);
    /* Another workspace's reset is to ITS default; Build's is the modules'
     * own set, below. */
    if( ui->workspace != EDITOR_UI_WS_BUILD )
    {
        ui->workspace_has[ui->workspace] = 0;
        ui_workspace_default(ui, ui->workspace, 0);
        if( ui->chrome->surface_w > 0 && ui->chrome->surface_h > 0 )
            Editor_UiLayout(ui);
        return;
    }
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        int const at = i - ui->panel_first;
        /* Reset returns every panel to the default layout, unmaximised. */
        if( ToriRSChrome_PanelIsMaximised(ui->chrome, i) )
            ToriRSChrome_PanelRestore(ui->chrome, i);
        ui->maximise_pending[at] = 0;
        ui->chrome->panels[i].user_placed = 0;
        ui->closed[at] = 0;
        ui->member[at] = ui->boot_member[at];
        ui->expanded[at] = 1;
        ui->expand_serial[at] = 0;
        if( ui->boot_member[at] )
            ToriRSChrome_PanelSetVisible(ui->chrome, i, 1);
    }
    /* Compact goes back to automatic: the setting is part of the layout. */
    ui->compact_mode = -1;
    /* A group whose frame alone came back takes its first open tab (or its
     * default members, shown above, join it); every box is placed afresh. */
    for( int i = 0; i < ui->group_count; i++ )
    {
        ui->groups[i].placed = -1;
        ui->groups[i].pending_h = 0;
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
    int const scale = Editor_ChromeScaleQ(ui->chrome);

    assert(ui);
    assert(out);
    EditorLayout_Clear(out);
    out->version = EDITOR_LAYOUT_VERSION;
    out->surface_w = ui->chrome->surface_w;
    out->surface_h = ui->chrome->surface_h;
    out->scale = Editor_ChromeScaleInteger(scale);
    out->has_snap = 1;
    out->snap = ui->snap;
    out->has_compact = 1;
    out->compact = ui->compact_mode;
    if( ui->window_w > 0 && ui->window_h > 0 )
    {
        out->has_window = 1;
        out->window_x = ui->window_x;
        out->window_y = ui->window_y;
        out->window_w = ui->window_w;
        out->window_h = ui->window_h;
    }
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* cp;
        struct EditorLayoutPanel* p;
        if( !ui_layout_saves(ui, i) )
            continue;
        if( ui->group_plus1[i] )
        {
            /* A member says only whether it is in its strip; its box is the
             * group's. */
            p = EditorLayout_Add(out, ui->panels[i].name);
            p->has_visible = 1;
            p->visible = Editor_UiPanelInStrip(ui, ui->panels[i].name);
            continue;
        }
        cp = &ui->chrome->panels[ui->panels[i].chrome_panel];
        p = EditorLayout_Add(out, ui->panels[i].name);
        p->has_visible = 1;
        p->visible = cp->visible ? 1 : 0;
        p->has_collapsed = 1;
        p->collapsed = cp->collapsed ? 1 : 0;
        p->has_user_placed = 1;
        p->user_placed = cp->user_placed ? 1 : 0;
        p->has_x = p->has_y = p->has_w = p->has_h = 1;
        p->x = cp->x * EDITOR_UI_SCALEQ_ONE / scale;
        p->y = cp->y * EDITOR_UI_SCALEQ_ONE / scale;
        p->w = cp->fixed_w * EDITOR_UI_SCALEQ_ONE / scale;
        p->h = cp->fixed_h * EDITOR_UI_SCALEQ_ONE / scale;
        /* Maximised: the box written is the one it restores to. */
        {
            int rx = 0;
            int ry = 0;
            int rw = 0;
            int rh = 0;
            int placed = 0;
            p->has_maximised = 1;
            p->maximised = ToriRSChrome_PanelRestoreBox(
                ui->chrome, ui->panels[i].chrome_panel, &rx, &ry, &rw, &rh, &placed);
            if( p->maximised )
            {
                p->user_placed = placed ? 1 : 0;
                p->x = rx * EDITOR_UI_SCALEQ_ONE / scale;
                p->y = ry * EDITOR_UI_SCALEQ_ONE / scale;
                p->w = rw * EDITOR_UI_SCALEQ_ONE / scale;
                p->h = rh * EDITOR_UI_SCALEQ_ONE / scale;
            }
        }
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        struct ToriRSChromePanel const* frame = &ui->chrome->panels[g->chrome_panel];
        struct EditorLayoutGroup* gp;
        if( g->member_count == 0 )
            continue;
        gp = EditorLayout_AddGroup(out, g->name);
        gp->has_visible = gp->has_user_placed = gp->has_x = gp->has_y = gp->has_w = gp->has_h = 1;
        gp->visible = frame->visible ? 1 : 0;
        gp->user_placed = frame->user_placed ? 1 : 0;
        gp->x = frame->x * EDITOR_UI_SCALEQ_ONE / scale;
        gp->y = frame->y * EDITOR_UI_SCALEQ_ONE / scale;
        gp->w = (g->w > 0 ? g->w : frame->fixed_w) * EDITOR_UI_SCALEQ_ONE / scale;
        gp->h = (g->body_h > 0 ? g->frame_h + g->body_h : 0) * EDITOR_UI_SCALEQ_ONE / scale;
        gp->has_maximised = 1;
        gp->maximised = 0;
        if( g->shown >= 0 )
        {
            int rx = 0;
            int ry = 0;
            int rw = 0;
            int rh = 0;
            int placed = 0;
            gp->has_shown = 1;
            snprintf(gp->shown, sizeof(gp->shown), "%s", ui->panels[g->members[g->shown]].name);
            /* Maximised: the box written is the one it restores to. */
            gp->maximised = ToriRSChrome_PanelRestoreBox(
                ui->chrome, ui_group_window(ui, g, g->shown), &rx, &ry, &rw, &rh, &placed);
            if( gp->maximised )
            {
                gp->x = rx * EDITOR_UI_SCALEQ_ONE / scale;
                gp->y = (ry - g->frame_h) * EDITOR_UI_SCALEQ_ONE / scale;
                gp->w = rw * EDITOR_UI_SCALEQ_ONE / scale;
                gp->h = (rh > 0 ? rh + g->frame_h : 0) * EDITOR_UI_SCALEQ_ONE / scale;
            }
        }
    }
    /* The workspaces: the current one is the live layout just captured, the
     * others as they were left (or as the file had them). */
    out->has_workspace = 1;
    snprintf(out->workspace, sizeof(out->workspace), "%s", Editor_UiWorkspaceName(ui->workspace));
    for( int w = 0; w < EDITOR_UI_WORKSPACES; w++ )
    {
        struct EditorLayoutWorkspace* ws;
        if( w != ui->workspace && !ui->workspace_has[w] )
            continue;
        ws = EditorLayout_AddWorkspace(out, Editor_UiWorkspaceName(w));
        if( w == ui->workspace )
            EditorLayout_WorkspaceFromTop(ws, out);
        else
        {
            *ws = ui->workspace_saved[w];
            snprintf(ws->name, sizeof(ws->name), "%s", Editor_UiWorkspaceName(w));
        }
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

static void
ui_group_apply(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    const struct EditorLayoutFile* file,
    int scale);

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
    scale = Editor_ChromeScaleQ(chrome);

    /* The default set first, from what the modules showed: Reset layout must
     * return to the default, not to this file. */
    ui_capture_boot(ui);
    /* A file that names its workspace (the layout file; a switch's own
     * file names none): the current one and every one's saved layout. */
    if( file->has_workspace )
    {
        int const current = Editor_UiWorkspaceFind(file->workspace);
        ui->workspace = current >= 0 ? current : EDITOR_UI_WS_BUILD;
        for( int w = 0; w < EDITOR_UI_WORKSPACES; w++ )
        {
            struct EditorLayoutWorkspace const* ws = EditorLayout_FindWorkspace(file, Editor_UiWorkspaceName(w));
            ui->workspace_has[w] = ws != NULL;
            if( ws )
                ui->workspace_saved[w] = *ws;
        }
    }
    if( file->has_snap )
        Editor_UiSetSnap(ui, file->snap);
    if( file->has_compact )
        ui->compact_mode = file->compact;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorLayoutPanel const* sp;
        int panel;
        int at;

        if( !ui_layout_saves(ui, i) || ui->group_plus1[i] )
            continue;
        sp = EditorLayout_Find(file, ui->panels[i].name);
        if( !sp )
            continue;
        panel = ui->panels[i].chrome_panel;
        at = panel - ui->panel_first;
        /* Collapsed in the compact dock as the user left it: the wish, which
         * the layout honours while the column has room for the rest. */
        if( sp->has_collapsed )
            ui->expanded[at] = (unsigned char)(sp->collapsed ? 0 : 1);

        if( sp->has_user_placed && sp->user_placed && sp->has_x && sp->has_y )
        {
            struct ToriRSChromePanel* cp = &chrome->panels[panel];
            int x = sp->x * scale / EDITOR_UI_SCALEQ_ONE;
            int y = sp->y * scale / EDITOR_UI_SCALEQ_ONE;
            int w = sp->has_w ? sp->w * scale / EDITOR_UI_SCALEQ_ONE : cp->fixed_w;
            int h = sp->has_h ? sp->h * scale / EDITOR_UI_SCALEQ_ONE : cp->fixed_h;

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
        /* Maximised when saved: once the layout below has given it the box
         * it restores to (ui_maximise_tick). */
        if( sp->has_maximised )
        {
            ui->maximise_pending[at] = (unsigned char)(sp->maximised ? 1 : 0);
            if( !sp->maximised && ToriRSChrome_PanelIsMaximised(chrome, panel) )
                ToriRSChrome_PanelRestore(chrome, panel);
        }
        if( sp->has_visible )
        {
            ToriRSChrome_PanelSetVisible(chrome, panel, sp->visible);
            /* A default panel the user closed stays shut through the layout
             * (the `closed` mark); one opened is admitted where it belongs. */
            ui->closed[at] = (unsigned char)(!sp->visible && ui->member[at] ? 1 : 0);
        }
    }
    for( int i = 0; i < ui->group_count; i++ )
        ui_group_apply(ui, &ui->groups[i], file, scale);
    /* Made for this very surface and scale: once the default layout has
     * placed the column (and set who is docked), the panels it placed take
     * the file's boxes too -- the column as the user left it, heights and
     * all, not as a fresh layout would share it out. */
    ui->layout_restore = file->surface_w == chrome->surface_w && file->surface_h == chrome->surface_h
                         && file->scale == Editor_ChromeScaleInteger(scale);
    if( ui->layout_restore && file != &ui->layout_saved )
        ui->layout_saved = *file;
    ui->laid_out = 0;
}

/** One group from a layout file: its members' `visible` say who is in the
 *  strip, its [group] section the front tab, whether it shows, and -- placed
 *  by the user -- its box. A file with no word on the group leaves it be. */
static void
ui_group_apply(
    struct EditorUi* ui,
    struct EditorUiGroup* g,
    const struct EditorLayoutFile* file,
    int scale)
{
    struct ToriRSChrome* chrome = ui->chrome;
    struct EditorLayoutGroup const* gs = EditorLayout_FindGroup(file, g->name);
    int const at = g->chrome_panel - ui->panel_first;
    int stated = gs != NULL;
    int shown = -1;
    int visible;

    for( int slot = 0; slot < g->member_count; slot++ )
    {
        struct EditorLayoutPanel const* sp = EditorLayout_Find(file, ui->panels[g->members[slot]].name);
        if( sp && sp->has_visible )
        {
            g->open[slot] = (unsigned char)(sp->visible ? 1 : 0);
            stated = 1;
        }
    }
    if( !stated )
        return;
    if( gs && gs->has_shown )
    {
        int const slot = ui_group_slot(g, ui_registry_index(ui, gs->shown));
        if( slot >= 0 && g->open[slot] )
            shown = slot;
    }
    if( shown < 0 && g->shown >= 0 && g->open[g->shown] )
        shown = g->shown;
    if( shown < 0 )
        shown = ui_group_next(g, -1);
    visible = shown >= 0 && (!gs || !gs->has_visible || gs->visible);
    for( int slot = 0; slot < g->member_count; slot++ )
    {
        int const panel = ui_group_window(ui, g, slot);
        if( slot != shown )
            ui_group_unmaximise(ui, g, slot);
        ToriRSChrome_PanelSetVisible(chrome, panel, visible && slot == shown);
    }
    if( shown != g->shown )
        g->prev_shown = -1;
    g->shown = shown;
    g->parked = shown >= 0 && !visible;
    g->tabs_built = 0;
    ToriRSChrome_PanelSetVisible(chrome, g->chrome_panel, visible);
    /* A group the user closed (its tabs kept: it had no room, say) stays
     * shut through the layout, as a closed panel does. */
    ui->closed[at] = (unsigned char)(!visible && ui->member[at] ? 1 : 0);
    if( gs && gs->has_user_placed && gs->user_placed && gs->has_x && gs->has_y )
    {
        int x = gs->x * scale / EDITOR_UI_SCALEQ_ONE;
        int y = gs->y * scale / EDITOR_UI_SCALEQ_ONE;
        int w = gs->has_w ? gs->w * scale / EDITOR_UI_SCALEQ_ONE : g->w;
        int h = gs->has_h ? gs->h * scale / EDITOR_UI_SCALEQ_ONE : 0;

        ui_layout_clamp(ui, &x, &y, &w, &h);
        ToriRSChrome_PanelSetFixedWidth(chrome, g->chrome_panel, w);
        ToriRSChrome_PanelMove(chrome, g->chrome_panel, x, y);
        chrome->panels[g->chrome_panel].user_placed = 1;
        g->x = x;
        g->y = y;
        g->w = w;
        g->pending_h = h;
        g->placed = -1;
        ui->docked[at] = 0;
        ui->flown[at] = 0;
    }
    if( gs && gs->has_maximised )
    {
        ui->maximise_pending[at] = (unsigned char)(gs->maximised ? 1 : 0);
        if( !gs->maximised && shown >= 0 )
            ui_group_unmaximise(ui, g, shown);
    }
}

/** The second half of a same-surface restore: the boxes of the panels the
 *  default layout just placed, from the file. */
static void
ui_layout_restore_boxes(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const scale = Editor_ChromeScaleQ(chrome);

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorLayoutPanel const* sp;
        int panel;
        int in_column;
        if( !ui_layout_saves(ui, i) )
            continue;
        /* A member's box is its group's (below). */
        if( ui->group_plus1[i] )
            continue;
        panel = ui->panels[i].chrome_panel;
        sp = EditorLayout_Find(&ui->layout_saved, ui->panels[i].name);
        if( !sp || chrome->panels[panel].user_placed || !chrome->panels[panel].visible || !sp->has_x || !sp->has_y )
            continue;
        if( sp->has_visible && !sp->visible )
            continue;
        in_column = panel >= ui->panel_first && panel < ui->panel_end && ui->docked[panel - ui->panel_first]
                    && chrome->panels[panel].x >= ui->dock_x;
        if( sp->has_w )
            ToriRSChrome_PanelSetFixedWidth(chrome, panel, sp->w * scale / EDITOR_UI_SCALEQ_ONE);
        if( sp->has_h )
            ToriRSChrome_PanelSetFixedHeight(chrome, panel, sp->h * scale / EDITOR_UI_SCALEQ_ONE);
        ToriRSChrome_PanelMove(chrome, panel, sp->x * scale / EDITOR_UI_SCALEQ_ONE, sp->y * scale / EDITOR_UI_SCALEQ_ONE);
        /* Docked in the column and put back off it: the flyout it was saved
         * as (the Preview beside the Catalog), so nothing holds it to the
         * dock. The World and the bottom strip are docked off the column
         * and are left as they are. */
        if( in_column && chrome->panels[panel].x < ui->dock_x )
        {
            ui->docked[panel - ui->panel_first] = 0;
            ui->flown[panel - ui->panel_first] = 1;
        }
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        struct EditorLayoutGroup const* gs = EditorLayout_FindGroup(&ui->layout_saved, g->name);
        int const frame = g->chrome_panel;
        if( !gs || chrome->panels[frame].user_placed || !chrome->panels[frame].visible || !gs->has_x || !gs->has_y )
            continue;
        if( gs->has_visible && !gs->visible )
            continue;
        g->x = gs->x * scale / EDITOR_UI_SCALEQ_ONE;
        g->y = gs->y * scale / EDITOR_UI_SCALEQ_ONE;
        if( gs->has_w )
            g->w = gs->w * scale / EDITOR_UI_SCALEQ_ONE;
        if( gs->has_h && gs->h > 0 )
            g->pending_h = gs->h * scale / EDITOR_UI_SCALEQ_ONE;
        g->placed = -1;
        ToriRSChrome_PanelSetFixedWidth(chrome, frame, g->w);
        ToriRSChrome_PanelMove(chrome, frame, g->x, g->y);
        ui_group_place(ui, g);
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
    h = ui_fnv(h, ToriRSChrome_ScaleQ(ui->chrome));
    h = ui_fnv(h, ui->snap);
    h = ui_fnv(h, ui->compact_mode);
    h = ui_fnv(h, ui->window_x);
    h = ui_fnv(h, ui->window_y);
    h = ui_fnv(h, ui->window_w);
    h = ui_fnv(h, ui->window_h);
    h = ui_fnv(h, ui->workspace);
    h = ui_fnv(h, (int)ui->workspace_serial);
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* cp;
        if( !ui_layout_saves(ui, i) )
            continue;
        cp = &ui->chrome->panels[ui->panels[i].chrome_panel];
        h = ui_fnv(h, i);
        if( ui->group_plus1[i] )
        {
            h = ui_fnv(h, Editor_UiPanelInStrip(ui, ui->panels[i].name));
            continue;
        }
        h = ui_fnv(h, cp->visible);
        h = ui_fnv(h, cp->collapsed);
        h = ui_fnv(h, cp->user_placed);
        h = ui_fnv(h, cp->x);
        h = ui_fnv(h, cp->y);
        h = ui_fnv(h, cp->fixed_w);
        h = ui_fnv(h, cp->fixed_h);
        h = ui_fnv(h, cp->maximised);
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        struct ToriRSChromePanel const* frame = &ui->chrome->panels[g->chrome_panel];
        if( g->member_count == 0 )
            continue;
        h = ui_fnv(h, 1000 + i);
        h = ui_fnv(h, frame->visible);
        h = ui_fnv(h, frame->user_placed);
        h = ui_fnv(h, frame->x);
        h = ui_fnv(h, frame->y);
        h = ui_fnv(h, g->w);
        h = ui_fnv(h, g->body_h);
        h = ui_fnv(h, g->shown >= 0 ? g->members[g->shown] : -1);
        h = ui_fnv(h, g->shown >= 0 ? ui->chrome->panels[ui_group_window(ui, g, g->shown)].maximised : 0);
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

/* ---- workspaces (editor_ui.h has the table) --------------------------------- */

_Static_assert(EDITOR_UI_WORKSPACES == 4, "EditorUi's workspace arrays hold the four");
_Static_assert(EDITOR_HK_WORKSPACES == EDITOR_UI_WORKSPACES, "one workspace action per workspace, in order");

static const char* const k_workspace_names[EDITOR_UI_WORKSPACES] = { "build", "terrain", "script", "playtest" };
static const char* const k_workspace_titles[EDITOR_UI_WORKSPACES] = { "Build", "Terrain", "Script", "Playtest" };

/* The design table's panels per workspace, as registry module names; the
 * first listed member of a group is its front tab. Script also takes the
 * Shop builder and the Quest board (workbench 6.7: both write scripts);
 * Playtest the Scenes panel and the encounter builder (6.5, 6.7); Script the
 * Problems panel (6.6, part 1). "vars", "dialogue_preview" (workbench 6.6)
 * and "live_log" are not built in this tree (the Live log is the Live
 * panel's own list today) and are skipped with a console line. */
static const char* const k_ws_terrain[] = { "terrain", "palette", "squares", "inspector", "console", "history",
    NULL };
static const char* const k_ws_script[] = { "record", "triggers", "vars", "locbuilder", "npcbuilder",
    "shelleditor", "shop", "quest", "problems", "console", "dialogue_preview", NULL };
static const char* const k_ws_playtest[] = { "live", "accounts", "scenes", "encounter", "live_log", "console",
    NULL };

const char*
Editor_UiWorkspaceName(int workspace)
{
    assert(workspace >= 0);
    assert(workspace < EDITOR_UI_WORKSPACES);
    return k_workspace_names[workspace];
}

const char*
Editor_UiWorkspaceTitle(int workspace)
{
    assert(workspace >= 0);
    assert(workspace < EDITOR_UI_WORKSPACES);
    return k_workspace_titles[workspace];
}

int
Editor_UiWorkspaceFind(const char* name)
{
    assert(name);
    for( int w = 0; w < EDITOR_UI_WORKSPACES; w++ )
        if( strcmp(k_workspace_names[w], name) == 0 )
            return w;
    return -1;
}

const char* const*
Editor_UiWorkspacePanels(int workspace)
{
    static const char* const* const k_lists[EDITOR_UI_WORKSPACES] = { NULL, k_ws_terrain, k_ws_script,
        k_ws_playtest };
    assert(workspace >= 0);
    assert(workspace < EDITOR_UI_WORKSPACES);
    return k_lists[workspace];
}

/** Is module `name` on the NULL-terminated `list`? */
static int
ui_name_listed(
    const char* const* list,
    const char* name)
{
    for( int i = 0; list[i]; i++ )
        if( strcmp(list[i], name) == 0 )
            return 1;
    return 0;
}

/**
 * Workspace `workspace`'s default: Build is the modules' own set (Reset
 * layout's); another shows the table's panels and hides every other panel
 * of the View menu but the World and the tool rail, every box placed afresh
 * by the next layout. With `log`, a console line names what was skipped.
 */
static void
ui_workspace_default(
    struct EditorUi* ui,
    int workspace,
    int log)
{
    const char* const* list = Editor_UiWorkspacePanels(workspace);
    struct EditorLayoutFile* file;
    char skipped[EDITOR_UI_CONSOLE_LINE_MAX];
    int skipped_count = 0;
    int used = 0;

    skipped[0] = '\0';
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
    {
        int const at = i - ui->panel_first;
        if( ToriRSChrome_PanelIsMaximised(ui->chrome, i) )
            ToriRSChrome_PanelRestore(ui->chrome, i);
        ui->maximise_pending[at] = 0;
        ui->chrome->panels[i].user_placed = 0;
        ui->closed[at] = 0;
        ui->docked[at] = 0;
        ui->flown[at] = 0;
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        ui->groups[i].placed = -1;
        ui->groups[i].pending_h = 0;
    }
    if( !list )
    {
        /* Build: the boot set, as Reset layout gives it in Build. */
        ui_capture_boot(ui);
        for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
        {
            int const at = i - ui->panel_first;
            ui->member[at] = ui->boot_member[at];
        }
        for( int i = 0; i < ui->panel_count; i++ )
        {
            int const panel = ui->panels[i].chrome_panel;
            int at;
            if( !ui_layout_saves(ui, i) || strcmp(ui->panels[i].name, "world") == 0
                || strcmp(ui->panels[i].name, "toolrail") == 0 )
                continue;
            at = panel - ui->panel_first;
            ToriRSChrome_PanelSetVisible(ui->chrome, panel, ui->boot_member[at] ? 1 : 0);
        }
        /* Each strip as it booted. */
        for( int i = 0; i < ui->group_count; i++ )
        {
            struct EditorUiGroup* g = &ui->groups[i];
            int const at = g->chrome_panel - ui->panel_first;
            int const shown = ui->boot_member[at] ? g->boot_shown : -1;
            for( int slot = 0; slot < g->member_count; slot++ )
                g->open[slot] = (unsigned char)(shown >= 0 && g->boot_open[slot]);
            for( int slot = 0; slot < g->member_count; slot++ )
                ToriRSChrome_PanelSetVisible(ui->chrome, ui_group_window(ui, g, slot), slot == shown);
            g->shown = shown;
            g->prev_shown = -1;
            g->parked = 0;
            g->tabs_built = 0;
            ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, shown >= 0);
        }
        ui->layout_restore = 0;
        ui->laid_out = 0;
        return;
    }
    for( int i = 0; list[i]; i++ )
        if( !Editor_UiFindPanel(ui, list[i]) )
        {
            used += snprintf(skipped + used, sizeof(skipped) - (size_t)used, "%s%s", skipped_count ? ", " : "",
                list[i]);
            if( used >= (int)sizeof(skipped) )
                used = (int)sizeof(skipped) - 1;
            skipped_count++;
        }
    if( log && skipped_count > 0 )
        Editor_UiLog(ui, "editor: workspace %s: no %s panel in this window, skipped", Editor_UiWorkspaceTitle(workspace),
            skipped);
    /* The default as a file: every saved panel shown or hidden, each group's
     * front tab its first listed member. Applied like any file (visibility,
     * the strips), with no boxes: the layout places everything. */
    file = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*file));
    assert(file);
    file->version = EDITOR_LAYOUT_VERSION;
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorLayoutPanel* p;
        if( !ui_layout_saves(ui, i) || strcmp(ui->panels[i].name, "world") == 0
            || strcmp(ui->panels[i].name, "toolrail") == 0 )
            continue;
        p = EditorLayout_Add(file, ui->panels[i].name);
        p->has_visible = 1;
        p->visible = ui_name_listed(list, ui->panels[i].name);
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        struct EditorLayoutGroup* gs;
        if( g->member_count == 0 )
            continue;
        gs = EditorLayout_AddGroup(file, g->name);
        gs->has_visible = 1;
        gs->visible = 0;
        for( int l = 0; list[l] && !gs->has_shown; l++ )
            if( ui_group_slot(g, ui_registry_index(ui, list[l])) >= 0 )
            {
                gs->has_shown = 1;
                gs->visible = 1;
                snprintf(gs->shown, sizeof(gs->shown), "%s", list[l]);
            }
    }
    for( int i = ui->panel_first; i < ui->panel_end && i < ui->chrome->panel_count; i++ )
        ui->member[i - ui->panel_first] = 0;
    Editor_UiLayoutApply(ui, file);
    /* The default set of this workspace is what it shows. */
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        if( ui_layout_saves(ui, i) && !ui->group_plus1[i] )
            ui->member[panel - ui->panel_first] = (unsigned char)(ui->chrome->panels[panel].visible ? 1 : 0);
    }
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        ui->member[g->chrome_panel - ui->panel_first] =
            (unsigned char)(ui->chrome->panels[g->chrome_panel].visible ? 1 : 0);
    }
    ToriMem_Free(ToriMem_Editor(), file);
}

int
Editor_UiSetWorkspace(
    struct EditorUi* ui,
    int workspace)
{
    const char* const* list;
    int skipped = 0;

    assert(ui);
    assert(workspace >= 0);
    assert(workspace < EDITOR_UI_WORKSPACES);
    list = Editor_UiWorkspacePanels(workspace);
    for( int i = 0; list && list[i]; i++ )
        if( !Editor_UiFindPanel(ui, list[i]) )
            skipped++;
    if( workspace == ui->workspace )
    {
        Editor_UiStatus(ui, "Workspace %s is already the one shown", Editor_UiWorkspaceTitle(workspace));
        return skipped;
    }
    /* The one being left first, as it is now. */
    {
        struct EditorLayoutFile* now = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*now));
        assert(now);
        Editor_UiLayoutCapture(ui, now);
        EditorLayout_WorkspaceFromTop(&ui->workspace_saved[ui->workspace], now);
        snprintf(ui->workspace_saved[ui->workspace].name, sizeof(ui->workspace_saved[ui->workspace].name), "%s",
            Editor_UiWorkspaceName(ui->workspace));
        ui->workspace_has[ui->workspace] = 1;
        ToriMem_Free(ToriMem_Editor(), now);
    }
    ui->workspace = workspace;
    ui->workspace_serial++;
    /* Its default first, so a panel its saved layout does not name (one
     * registered since) is where the default puts it. */
    ui_workspace_default(ui, workspace, 1);
    if( ui->workspace_has[workspace] )
    {
        struct EditorLayoutFile* file = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*file));
        assert(file);
        EditorLayout_TopFromWorkspace(file, &ui->workspace_saved[workspace]);
        file->has_snap = 1;
        file->snap = ui->snap;
        Editor_UiLayoutApply(ui, file);
        ToriMem_Free(ToriMem_Editor(), file);
    }
    Editor_UiLog(ui, "editor: workspace %s%s", Editor_UiWorkspaceTitle(workspace),
        ui->workspace_has[workspace] ? " (as you left it)" : "");
    ui->laid_out = 0;
    if( ui->chrome->surface_w > 0 && ui->chrome->surface_h > 0 )
        ui_default_layout(ui);
    return skipped;
}

static void
ui_default_layout(struct EditorUi* ui)
{
    int const menubar = ui_chrome_panel(ui, "menubar");
    int const statusbar = ui_chrome_panel(ui, "statusbar");

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
    /* Not built yet (no heights to stack): next frame. A popped-out window
     * stacks nothing (its menu bar is hidden). */
    if( ui->solo_panel < 0 && menubar >= 0 && ToriRSChrome_PanelRect(ui->chrome, menubar).h <= 0 )
        return;
    /* Nor the status bar's band at the foot. */
    if( ui->solo_panel < 0 && statusbar >= 0 && ui->chrome->panels[statusbar].visible
        && ToriRSChrome_PanelRect(ui->chrome, statusbar).h <= 0 )
        return;
    Editor_UiLayout(ui);
    /* A panel the user just brought back is shown, whatever the column had
     * room for: if the layout could not admit it, it opens beside the column
     * (ui_layout_follow), never back into hiding. */
    if( ui->layout_reopened >= 0 )
    {
        int const reopened = ui->layout_reopened;
        ui->layout_reopened = -1;
        if( reopened >= ui->panel_first && reopened < ui->panel_end && !ui->chrome->panels[reopened].visible )
        {
            ToriRSChrome_PanelSetVisible(ui->chrome, reopened, 1);
            ui->docked[reopened - ui->panel_first] = 0;
            ui->flown[reopened - ui->panel_first] = 0;
        }
    }
    if( ui->layout_restore )
    {
        ui->layout_restore = 0;
        ui_layout_restore_boxes(ui);
    }
}

/* ---- the shared tools (STATE_TOOLS) ------------------------------------------
 *
 * Every window of a Client edits with the same brush, palette, flags,
 * placement shape, Fit and view level as the tool itself (STATE_TOOL): a slider
 * moved in a popped-out Terrain window paints in the main window's World. One
 * key carries them all; a tick whose tools differ from what this window last
 * published or applied publishes them (so every way a panel or a hotkey
 * changes a tool is covered without each one publishing), and the fact lands
 * back in every window's tools, where each panel's own follow redraws it. */
_Static_assert(EDITOR_UI_TOOLS_VALUES == TORIRSMAPED_TOOLS_VALUES, "the editor packs every shared tool");
_Static_assert(-EDITOR_HEIGHT_TARGET_MIN < (1 << 13), "the Height target packs in 13 bits");

static void
ui_tools_pack(
    const struct EditorUi* ui,
    int32_t* out)
{
    struct EditorUiTools const* t = &ui->tools;
    out[0] = t->brush.radius;
    out[1] = (int32_t)t->brush.falloff;
    out[2] = t->brush.step;
    out[3] = t->brush.strength;
    out[4] = t->underlay_id;
    out[5] = t->overlay_id;
    out[6] = t->overlay_none;
    out[7] = t->overlay_shape;
    out[8] = t->overlay_rotation;
    out[9] = t->flags_value;
    out[10] = t->place_shape;
    out[11] = t->place_rotation;
    out[12] = t->vis_level;
    out[13] = t->vis_solo;
    out[14] = t->place_fit;
    /* The Height brush in one value (the store's 16 are spent): mode 2
     * bits, grid 1, opacity 7, target valid 1, target 13 (-target, which
     * EDITOR_HEIGHT_TARGET_MIN keeps under 8192). */
    out[15] = (t->height_mode & 3) | (t->height_grid ? 1 << 2 : 0) | ((t->height_grid_opacity & 0x7f) << 3)
              | (t->height_valid ? 1 << 10 : 0) | (((-t->height_target) & 0x1fff) << 11);
}

static void
ui_tools_unpack(
    struct EditorUi* ui,
    const int32_t* values)
{
    struct EditorUiTools* t = &ui->tools;
    t->brush.radius = values[0] < 0 ? 0 : values[0] > EDITOR_BRUSH_RADIUS_MAX ? EDITOR_BRUSH_RADIUS_MAX : values[0];
    if( values[1] >= 0 && values[1] < EDITOR_BRUSH_FALLOFF_COUNT )
        t->brush.falloff = (enum Editor_BrushFalloff)values[1];
    t->brush.step = values[2];
    t->brush.strength = values[3];
    t->underlay_id = values[4];
    t->overlay_id = values[5];
    t->overlay_none = values[6];
    t->overlay_shape = values[7];
    t->overlay_rotation = values[8];
    t->flags_value = values[9];
    t->place_shape = values[10];
    t->place_rotation = values[11];
    if( values[12] >= -1 && values[12] < EDITOR_SQUARE_LEVELS )
        t->vis_level = values[12];
    t->vis_solo = values[13];
    t->place_fit = values[14] ? 1 : 0;
    if( (values[15] & 3) < EDITOR_HEIGHT_MODE_COUNT )
        t->height_mode = values[15] & 3;
    t->height_grid = (values[15] >> 2) & 1;
    t->height_grid_opacity = ((values[15] >> 3) & 0x7f) > 100 ? 100 : (values[15] >> 3) & 0x7f;
    t->height_valid = (values[15] >> 10) & 1;
    t->height_target = -((values[15] >> 11) & 0x1fff);
}

/** A popped-out World says in its title what a click in it does -- the
 *  Client's tool and pinned level -- since its window has no tool rail or
 *  Level row to show them. */
static void
ui_solo_title(struct EditorUi* ui)
{
    char title[TORIRS_CHROME_LABEL_MAX];

    if( ui->solo_panel < 0 || ui->solo_panel != ui->world_panel )
        return;
    if( ui->tools.edit_level >= 0 )
        snprintf(title, sizeof(title), "World - %s - level %d", Editor_ToolName(ui->tools.tool), ui->tools.edit_level);
    else
        snprintf(title, sizeof(title), "World - %s", Editor_ToolName(ui->tools.tool));
    if( strcmp(ui->chrome->panels[ui->solo_panel].title, title) != 0 )
        ToriRSChrome_PanelSetTitle(ui->chrome, ui->solo_panel, title);
}

/** Per frame, after the panels ticked: publish the tools when they moved. */
static void
ui_tools_publish(struct EditorUi* ui)
{
    int32_t now[EDITOR_UI_TOOLS_VALUES];

    ui_tools_pack(ui, now);
    if( !ui->tools_known )
    {
        /* The baseline: what this window booted with is not a change. */
        memcpy(ui->tools_shared, now, sizeof(now));
        ui->tools_known = 1;
        return;
    }
    if( memcmp(now, ui->tools_shared, sizeof(now)) == 0 )
        return;
    memcpy(ui->tools_shared, now, sizeof(now));
    if( !Editor_UiPublish(ui, TORIRSMAPED_STATE_TOOLS, now, EDITOR_UI_TOOLS_VALUES) )
        return;
    if( ui->tools_echo_count == (int)(sizeof(ui->tools_echo) / sizeof(ui->tools_echo[0])) )
    {
        /* A full queue of publishes with no echo: the oldest is not coming back. */
        memmove(ui->tools_echo[0], ui->tools_echo[1], sizeof(ui->tools_echo) - sizeof(ui->tools_echo[0]));
        ui->tools_echo_count--;
    }
    memcpy(ui->tools_echo[ui->tools_echo_count++], now, sizeof(now));
}

/**
 * A STATE_TOOLS fact. The server relays every publish to the whole Client in
 * ONE order, the publisher included, so while this window still waits for
 * the echo of its own publish, anything it hears was ordered BEFORE that
 * publish: superseded by it, and every other window will end on this
 * window's value too. So: this window's own echo is consumed, another
 * window's fact heard before it is dropped, and only a fact heard with no
 * echo pending becomes this window's tools.
 */
static void
ui_tools_heard(
    struct EditorUi* ui,
    const int32_t* values,
    int count)
{
    if( count < EDITOR_UI_TOOLS_VALUES )
        return;
    if( ui->tools_echo_count > 0 )
    {
        if( memcmp(values, ui->tools_echo[0], sizeof(ui->tools_echo[0])) == 0 )
        {
            ui->tools_echo_count--;
            memmove(
                ui->tools_echo[0], ui->tools_echo[1], sizeof(ui->tools_echo[0]) * (size_t)ui->tools_echo_count);
        }
        return;
    }
    ui_tools_unpack(ui, values);
    ui_tools_pack(ui, ui->tools_shared);
    ui->tools_known = 1;
}

/* ---- the frame ------------------------------------------------------------- */

/** The registry index of chrome panel `panel`, or -1. */
static int
ui_registry_of(
    const struct EditorUi* ui,
    int panel)
{
    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].chrome_panel == panel )
            return i;
    return -1;
}

/** Per frame: a title-bar Pop out press, and a popped-out panel shown here
 *  again (pop in, however it came back: the View menu, its hotkey, Reset
 *  layout). Cheap: the registry, not the UI tree. */
static void
ui_popout_tick(struct EditorUi* ui)
{
    int const pressed = ToriRSChrome_TakePopOut(ui->chrome);

    if( pressed >= 0 )
    {
        int const at = ui_registry_of(ui, pressed);
        struct EditorUiGroup const* g = ui_frame_group(ui, pressed);
        if( at >= 0 )
            (void)Editor_UiPanelPopOut(ui, ui->panels[at].name);
        /* A group's Pop out is its shown tab's. */
        else if( g && g->shown >= 0 )
            (void)Editor_UiPanelPopOut(ui, ui->panels[g->members[g->shown]].name);
    }
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        if( !ui->popped[i] || panel < 0 || !ui->chrome->panels[panel].visible )
            continue;
        ui->popped[i] = 0;
        Editor_UiLog(ui, "editor: %s popped back in (its own window keeps running until you close it)",
            ui->panels[i].title);
    }
}

int
Editor_UiPanelPopOut(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel;
    struct EditorViewCamera camera;
    struct EditorViewCamera const* pose = NULL;
    char why[192];
    int at;
    int const level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : 0;

    assert(ui);
    assert(name);

    panel = Editor_UiFindPanel(ui, name);
    if( !panel || !panel->title || panel->chrome_panel < 0 )
    {
        Editor_UiLog(ui, "editor: there is no panel '%s' to pop out", name);
        return 0;
    }
    at = (int)(panel - ui->panels);
    if( ui->solo_panel >= 0 )
    {
        Editor_UiLog(ui, "editor: this window is already a popped-out %s", panel->title);
        return 0;
    }
    if( !ui->app_services.spawn_panel )
    {
        Editor_UiLog(ui, "editor: Pop out %s: this window cannot open other windows", panel->title);
        return 0;
    }
    /* Where the World panel looks, on the pinned level's ground: the 3D view
     * opens exactly there, and a panel window loads the same square. */
    memset(&camera, 0, sizeof(camera));
    if( ui->app_services.world_camera
        && ui->app_services.world_camera(ui->app_services.user_data, level, &camera) )
        pose = &camera;
    why[0] = '\0';
    if( !ui->app_services.spawn_panel(ui->app_services.user_data, name, pose, why, (int)sizeof(why)) )
    {
        Editor_UiLog(ui, "editor: could not pop out %s: %s", panel->title, why[0] ? why : "no reason given");
        return 0;
    }
    ToriRSChrome_PanelSetVisible(ui->chrome, panel->chrome_panel, 0);
    ui->popped[at] = 1;
    if( pose )
        Editor_UiLog(ui, "editor: %s popped out into its own window (at %d,%d level %d) -- View > %s brings it back",
            panel->title, pose->x / 128, pose->z / 128, pose->level, panel->title);
    else
        Editor_UiLog(ui, "editor: %s popped out into its own window -- View > %s brings it back", panel->title,
            panel->title);
    return 1;
}

int
Editor_UiWorldPopOut(struct EditorUi* ui)
{
    assert(ui);
    return Editor_UiPanelPopOut(ui, "world");
}

int
Editor_UiPanelPoppedOut(
    const struct EditorUi* ui,
    const char* name)
{
    assert(ui);
    assert(name);
    for( int i = 0; i < ui->panel_count; i++ )
        if( strcmp(ui->panels[i].name, name) == 0 )
            return ui->popped[i];
    return 0;
}

const char*
Editor_UiPanelAt(
    struct EditorUi* ui,
    int x,
    int y)
{
    const char* found = NULL;
    int top = -1;

    assert(ui);
    /* The chrome draws later panels over earlier ones: the highest index
     * holding the point is the one the pointer is on. */
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        struct ToriRSChromeRect r;
        if( !ui->panels[i].title || panel < 0 || panel <= top || !ui->chrome->panels[panel].visible )
            continue;
        r = ui->chrome->panels[panel].last_rect;
        if( x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h )
        {
            top = panel;
            found = ui->panels[i].name;
        }
    }
    /* A group's frame stands for its shown tab. */
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup const* g = &ui->groups[i];
        struct ToriRSChromeRect r;
        if( g->shown < 0 || g->chrome_panel <= top || !ui->chrome->panels[g->chrome_panel].visible )
            continue;
        r = ui->chrome->panels[g->chrome_panel].last_rect;
        if( x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h )
        {
            top = g->chrome_panel;
            found = ui->panels[g->members[g->shown]].name;
        }
    }
    return found;
}

void
Editor_UiClearSolo(struct EditorUi* ui)
{
    assert(ui);
    assert(ui->solo_panel >= 0);
    if( ui->solo_panel == ui->world_panel )
        ToriRSChrome_PanelSetTitle(ui->chrome, ui->world_panel, "World");
    ToriRSChrome_PanelSetDockable(ui->chrome, ui->solo_panel, 0);
    ui->solo_panel = -1;
    ui_window_furniture(ui);
    /* Every titled panel shown, the default layout hiding what the column
     * has no room for -- the main window's own boot. */
    Editor_UiSetVisible(ui, 1);
}

int
Editor_UiSetSolo(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel;

    assert(ui);
    assert(name);
    panel = Editor_UiFindPanel(ui, name);
    if( !panel || !panel->title || panel->chrome_panel < 0 )
        return 0;
    ui->solo_panel = panel->chrome_panel;
    ui->laid_out = 0;
    return 1;
}

/* ---- maximise: a double-click on a panel's title ------------------------ */

/** The area a maximised panel fills: the whole surface between the menu bar
 *  and the status bar -- every pixel the dock lays panels out in. */
static struct ToriRSChromeRect
ui_maximise_area(struct EditorUi* ui)
{
    struct ToriRSChromeRect area;
    int const menubar = ui_chrome_panel(ui, "menubar");
    int const statusbar = ui_chrome_panel(ui, "statusbar");
    int top = 0;
    int bottom = ui->chrome->surface_h;

    if( menubar >= 0 && ui->chrome->panels[menubar].visible )
    {
        struct ToriRSChromeRect const bar = ToriRSChrome_PanelRect(ui->chrome, menubar);
        top = bar.y + bar.h;
    }
    /* A maximised panel stops above the status bar, as the dock does. */
    if( statusbar >= 0 && ui->chrome->panels[statusbar].visible )
        bottom -= ToriRSChrome_PanelRect(ui->chrome, statusbar).h;
    area.x = 0;
    area.y = top;
    area.w = ui->chrome->surface_w;
    area.h = bottom - top;
    return area;
}

/** The area a group's shown window is maximised to: the dock's, under the
 *  group's strip (the frame follows it to the top, ui_group_place). */
static struct ToriRSChromeRect
ui_maximise_group_area(
    struct EditorUi* ui,
    struct EditorUiGroup* g)
{
    struct ToriRSChromeRect area = ui_maximise_area(ui);
    int const fh = ui_group_frame_height(ui, g);

    area.y += fh;
    area.h -= fh;
    return area;
}

/** The registry panel whose chrome panel is `panel`, when it is one the
 *  layout keeps (a titled View-menu window), else -1. */
static int
ui_layout_panel_of(
    const struct EditorUi* ui,
    int panel)
{
    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].chrome_panel == panel && ui_layout_saves(ui, i) )
            return i;
    return -1;
}

/**
 * Every frame (the registry, not the UI tree): a title double-click maximises
 * that panel over the dock or restores it; a maximised panel follows the
 * surface; one the saved layout says was maximised is maximised once the
 * layout has placed its box.
 */
static void
ui_maximise_tick(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    struct ToriRSChromeRect const area = ui_maximise_area(ui);
    int const clicked = ToriRSChrome_TakeTitleDoubleClick(chrome);

    /* A popped-out window is a window of its own; it has no dock to fill. */
    if( ui->solo_panel >= 0 || area.w <= 0 || area.h <= 0 )
        return;
    if( clicked >= 0 && ui_layout_panel_of(ui, clicked) >= 0 )
    {
        if( ToriRSChrome_PanelIsMaximised(chrome, clicked) )
        {
            ToriRSChrome_PanelRestore(chrome, clicked);
            /* A docked panel goes back into the column the layout keeps. */
            ui->laid_out = 0;
            Editor_UiLog(ui, "editor: %s restored", chrome->panels[clicked].title);
        }
        else
        {
            /* A collapsed panel maximises as a whole panel, and comes back
             * expanded: the first click of the double collapsed it. */
            if( ToriRSChrome_PanelIsCollapsed(chrome, clicked) )
            {
                ToriRSChrome_PanelSetCollapsed(chrome, clicked, 0);
                ui->expanded[clicked - ui->panel_first] = 1;
                ui->expand_serial[clicked - ui->panel_first] = ++ui->expand_clock;
            }
            ToriRSChrome_PanelMaximise(chrome, clicked, area);
            Editor_UiLog(ui, "editor: %s maximised (double-click its title to restore)",
                chrome->panels[clicked].title);
        }
    }
    else if( clicked >= 0 && ui_frame_group(ui, clicked) && ui_frame_group(ui, clicked)->shown >= 0
             && !ui_frame_group(ui, clicked)->parked )
    {
        /* A group's title: the group over the dock, its strip on top. */
        struct EditorUiGroup* g = ui_frame_group(ui, clicked);
        int const panel = ui_group_window(ui, g, g->shown);
        if( ToriRSChrome_PanelIsMaximised(chrome, panel) )
        {
            ui_group_unmaximise(ui, g, g->shown);
            ui->laid_out = 0;
            Editor_UiLog(ui, "editor: %s restored", g->title);
        }
        else
        {
            ToriRSChrome_PanelMaximise(chrome, panel, ui_maximise_group_area(ui, g));
            ui_group_place(ui, g);
            Editor_UiLog(ui, "editor: %s maximised (double-click its title to restore)", g->title);
        }
    }
    else if( clicked >= 0 )
        chrome->title_double = clicked;

    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        int const at = g->chrome_panel - ui->panel_first;
        int panel;
        if( g->shown < 0 || g->parked )
            continue;
        panel = ui_group_window(ui, g, g->shown);
        if( ui->maximise_pending[at] && ui->laid_out && !ui->layout_hold && !ui->layout_pending
            && ui_group_frame_height(ui, g) > 0 )
        {
            ui->maximise_pending[at] = 0;
            ToriRSChrome_PanelMaximise(chrome, panel, ui_maximise_group_area(ui, g));
            ui_group_place(ui, g);
        }
        else if( ToriRSChrome_PanelIsMaximised(chrome, panel) )
            ToriRSChrome_PanelMaximise(chrome, panel, ui_maximise_group_area(ui, g));
    }
    for( int i = 0; i < ui->panel_count; i++ )
    {
        int const panel = ui->panels[i].chrome_panel;
        int at;
        if( !ui_layout_saves(ui, i) || ui->group_plus1[i] )
            continue;
        at = panel - ui->panel_first;
        if( ui->maximise_pending[at] && ui->laid_out && !ui->layout_hold && !ui->layout_pending )
        {
            ui->maximise_pending[at] = 0;
            ToriRSChrome_PanelMaximise(chrome, panel, area);
        }
        else if( ToriRSChrome_PanelIsMaximised(chrome, panel) )
            ToriRSChrome_PanelMaximise(chrome, panel, area);
    }
}

/**
 * Every frame: a click on a docked panel's title in the compact dock toggles
 * it between expanded and collapsed (the layout then collapses the least
 * recently expanded panels the column cannot hold); a click on a collapsed
 * panel anywhere else (one dragged out of the column) expands it in place.
 */
static void
ui_compact_tick(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    int const clicked = ToriRSChrome_TakeTitleClick(chrome);
    int at;

    if( clicked < 0 || ui->solo_panel >= 0 || ui_layout_panel_of(ui, clicked) < 0 )
        return;
    at = clicked - ui->panel_first;
    if( ui->docked[at] && ui->compact && !chrome->panels[clicked].user_placed )
    {
        ui->expanded[at] = (unsigned char)(ToriRSChrome_PanelIsCollapsed(chrome, clicked) ? 1 : 0);
        if( ui->expanded[at] )
            ui->expand_serial[at] = ++ui->expand_clock;
        ui->laid_out = 0;
    }
    else if( ToriRSChrome_PanelIsCollapsed(chrome, clicked) )
    {
        ToriRSChrome_PanelSetCollapsed(chrome, clicked, 0);
        ToriRSChrome_PanelSetFixedHeight(chrome, clicked, 0);
        ui->expanded[at] = 1;
    }
}

/**
 * The chrome's truncation ring into the console, one line per record (the
 * workbench contract's R2: a cut string is never silent). A record whose
 * widget is still the one that was cut names its window. Once a frame, after
 * the modules ran, so a label a module set this frame is said this frame.
 */
static void
ui_truncations_drain(struct EditorUi* ui)
{
    struct ToriRSChrome* chrome = ui->chrome;
    struct ToriRSChromeTruncation rec;
    int dropped;

    while( ToriRSChrome_TakeTruncation(chrome, &rec) )
    {
        const char* where = "";
        /* The module's title: a grouped window's own title bar is gone (its
         * group's frame wears the tabs), the registry still knows it. */
        if( rec.widget >= 0 && rec.widget < chrome->widget_count && chrome->widgets[rec.widget].serial == rec.serial
            && chrome->widgets[rec.widget].kind != TORIRS_CHROME_W_FREE )
        {
            int const panel = chrome->widgets[rec.widget].panel;
            for( int i = 0; i < ui->panel_count && !where[0]; i++ )
                if( ui->panels[i].chrome_panel == panel && ui->panels[i].title )
                    where = ui->panels[i].title;
            if( !where[0] && panel >= 0 && panel < chrome->panel_count )
                where = chrome->panels[panel].title;
        }
        Editor_UiLog(ui, "chrome: %s%s%s cut to %d of %d bytes (widget %d): \"%s...\"", where, where[0] ? " " : "",
            ToriRSChrome_TruncationFieldName(rec.field), rec.kept, rec.length, rec.widget, rec.head);
    }
    dropped = ToriRSChrome_TakeTruncationsDropped(chrome);
    if( dropped > 0 )
        Editor_UiLog(ui, "chrome: %d more cut string(s) were not kept (the ring holds %d)", dropped,
            TORIRS_CHROME_TRUNCATION_RING);
}

/* ---- gesture frames (workbench 6.1, design section 14) -------------------- */

int
Editor_UiGesture(
    struct EditorUi* ui,
    const struct LibToriRS_Gesture* frame)
{
    static int const k_kind[LIBTORIRS_GESTURE_KIND_COUNT] = {
        [LIBTORIRS_GESTURE_PAN] = TORIRS_CHROME_GESTURE_PAN,
        [LIBTORIRS_GESTURE_PINCH] = TORIRS_CHROME_GESTURE_PINCH,
        [LIBTORIRS_GESTURE_TWIST] = TORIRS_CHROME_GESTURE_TWIST,
    };
    static int const k_phase[LIBTORIRS_GESTURE_PHASE_COUNT] = {
        [LIBTORIRS_GESTURE_BEGIN] = TORIRS_CHROME_GESTURE_BEGIN,
        [LIBTORIRS_GESTURE_UPDATE] = TORIRS_CHROME_GESTURE_UPDATE,
        [LIBTORIRS_GESTURE_END] = TORIRS_CHROME_GESTURE_END,
    };
    struct ToriRSChromeGesture gesture;

    assert(ui);
    assert(frame);
    assert(frame->kind < LIBTORIRS_GESTURE_KIND_COUNT);
    assert(frame->phase < LIBTORIRS_GESTURE_PHASE_COUNT);
    assert(frame->scale > 0.0f);
    /* Any panel of the instance, not only the editor's: the loc editor's
     * windows share it, and a frame over one must not reach the World. */
    if( !ToriRSChrome_HasVisiblePanel(ui->chrome) )
        return 0;
    /* The decoded frame (input/torirs_input.h) in the chrome's own copy of
     * it: ui/ includes nothing of input/. The chrome keeps its own part-pixel
     * remainders (pan_rem), so whole pixels are what it is handed. */
    gesture.kind = k_kind[frame->kind];
    gesture.x = frame->x;
    gesture.y = frame->y;
    gesture.dx = (int)lroundf(frame->dx);
    gesture.dy = (int)lroundf(frame->dy);
    gesture.fingers = frame->fingers;
    gesture.scale_q16 = (int)lroundf(frame->scale * 65536.0f);
    if( gesture.scale_q16 < 1 )
        gesture.scale_q16 = 1;
    gesture.radians_q16 = (int)lroundf(frame->radians * 65536.0f);
    gesture.phase = k_phase[frame->phase];
    return ToriRSChrome_Gesture(ui->chrome, &gesture);
}

void
Editor_UiSetTouchInput(
    struct EditorUi* ui,
    int touch)
{
    assert(ui);
    ui->input_touch = touch != 0;
}


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
    ui_text_size_follow(ui);
    ui_follow_scale(ui);
    ui_follow_surface(ui);
    /* The strips first: what showed or hid a member window since the last
     * frame says who is in which strip before anything is laid out. */
    ui_groups_follow(ui);
    ui_default_layout(ui);
    if( ui->laid_out )
        ui_layout_follow(ui);

    ui_popout_tick(ui);
    ui_dock_tick(ui);
    ui_maximise_tick(ui);
    ui_compact_tick(ui);

    /* ONE activation per frame, and it is either one module's or nobody's
     * here. Unclaimed, it goes back where it was: another consumer of this
     * chrome instance later in the frame (the loc editor) may own it. */
    activated = ToriRSChrome_TakeActivated(ui->chrome);
    if( activated >= 0 )
    {
        int claimed = ui_groups_activate(ui, activated);
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
    /* And again after the modules ran: a window a module (or a menu row)
     * showed or hid this frame is in or out of its strip before the Build,
     * so two tabs never draw at once. */
    ui_groups_follow(ui);
    ui_tools_publish(ui);
    ui_solo_title(ui);
    ui_truncations_drain(ui);
}

void
Editor_UiOpenSquare(
    struct EditorUi* ui,
    int map_x,
    int map_z,
    const char* storage,
    int storage_index,
    int add)
{
    assert(ui);
    assert(storage);
    Editor_RecentNote(&ui->recent, map_x, map_z, storage);
    if( Editor_UiWorldShown(ui) )
    {
        if( ui->hooks.open_square )
            ui->hooks.open_square(ui->hooks_user, map_x, map_z, storage, add);
        else
            Editor_UiLog(ui, "editor: this window cannot open squares");
        return;
    }
    if( ui->editor )
    {
        int32_t values[5];
        values[0] = map_x;
        values[1] = map_z;
        values[2] = add ? 1 : 0;
        values[3] = storage_index;
        values[4] = (int32_t)++ui->open_square_serial;
        if( Editor_UiPublish(ui, TORIRSMAPED_STATE_OPEN_SQUARE, values, 5) )
        {
            Editor_UiLog(ui, "editor: %s m%d_%d in the window that shows the world", add ? "adding" : "opening",
                map_x, map_z);
            return;
        }
    }
    Editor_UiLog(ui, "editor: no window shows a world to open m%d_%d in", map_x, map_z);
}

void
Editor_UiOpenSquareSet(
    struct EditorUi* ui,
    const struct EditorUiSquareRef* squares,
    int count)
{
    assert(ui);
    assert(squares);
    assert(count > 0);
    assert(count <= EDITOR_UI_OPEN_SET_MAX);
    for( int i = 0; i < count; i++ )
        Editor_RecentNote(&ui->recent, squares[i].map_x, squares[i].map_z, squares[i].storage);
    if( Editor_UiWorldShown(ui) && ui->hooks.open_square_set )
    {
        ui->hooks.open_square_set(ui->hooks_user, squares, count);
        return;
    }
    Editor_UiLog(ui, "editor: Open squares needs the window that shows the world");
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
    else if( key == TORIRSMAPED_STATE_TOOLS )
        ui_tools_heard(ui, values, count);
    else if( key == TORIRSMAPED_STATE_OPEN_SQUARE && count >= 5 && Editor_UiWorldShown(ui) )
    {
        /* Another window's Open: this one shows a world, so it is the one
         * meant. The storage comes back from the index through this
         * window's own atlas; an unknown index is the content tree. */
        char storage[EDITOR_DOC_STORAGE_MAX];
        storage[0] = '\0';
        (void)Editor_Squares_StorageName(ui, values[3], storage);
        if( ui->hooks.open_square )
            ui->hooks.open_square(ui->hooks_user, values[0], values[1], storage, values[2] != 0);
    }
    else if( key == TORIRSMAPED_STATE_DOCK_PANEL && count >= 2 )
        ui_dock_heard(ui, values, count);

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
Editor_UiSetHeightTarget(
    struct EditorUi* ui,
    int height)
{
    assert(ui);
    ui->tools.height_target = height < EDITOR_HEIGHT_TARGET_MIN ? EDITOR_HEIGHT_TARGET_MIN
                              : height > 0                      ? 0
                                                                : height;
    ui->tools.height_valid = 1;
    Editor_UiStatus(ui, "Height: grid at %d (%d world units up)", -ui->tools.height_target / EDITOR_HEIGHT_UNIT,
        -ui->tools.height_target);
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
    ui->previous_tool = ui->tools.tool;
    ui->tools.tool = tool;
    ui->tools.flatten_valid = 0;
    value = (int32_t)tool;
    (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_TOOL, &value, 1);
}

void
Editor_UiPanelOpenBeside(
    struct EditorUi* ui,
    const char* name,
    const char* anchor)
{
    struct EditorUiPanel* panel;
    struct EditorUiPanel* beside;
    struct ToriRSChrome* chrome;
    struct ToriRSChromeRect r;
    int at;
    int w;
    int x;

    assert(ui);
    assert(name);
    assert(anchor);
    chrome = ui->chrome;
    panel = Editor_UiFindPanel(ui, name);
    assert(panel);
    assert(panel->chrome_panel >= 0);

    ToriRSChrome_PanelSetVisible(chrome, panel->chrome_panel, 1);
    /* A member opens in its strip, where its group is. */
    if( ui->group_plus1[panel - ui->panels] )
        return;
    beside = Editor_UiFindPanel(ui, anchor);
    /* Placed only among the windows this registry lays out (a harness that
     * registers modules one by one lays out none). */
    if( ui->solo_panel >= 0 || panel->chrome_panel < ui->panel_first || panel->chrome_panel >= ui->panel_end
        || !beside || beside->chrome_panel < ui->panel_first
        || beside->chrome_panel >= ui->panel_end || !chrome->panels[beside->chrome_panel].visible
        || chrome->panels[panel->chrome_panel].user_placed )
        return;
    /* Where the anchor IS (a panel the layout flew out this very tick has
     * moved since its last Build), at the width it was laid out at. */
    r = chrome->panels[beside->chrome_panel].last_rect;
    r.x = chrome->panels[beside->chrome_panel].x;
    r.y = chrome->panels[beside->chrome_panel].y;
    if( chrome->panels[beside->chrome_panel].fixed_w > 0 )
        r.w = chrome->panels[beside->chrome_panel].fixed_w;
    w = chrome->panels[panel->chrome_panel].fixed_w > 0 ? chrome->panels[panel->chrome_panel].fixed_w
                                                          : chrome->panels[panel->chrome_panel].last_rect.w;
    x = r.x - w;
    if( x < ui->world.x && r.x + r.w + w <= chrome->surface_w )
        x = r.x + r.w;
    if( x < 0 )
        x = 0;
    /* A flyout: content-sized, at its own box, and the column leaves it be
     * (ui_layout_follow places only what is neither docked nor flown). */
    ToriRSChrome_PanelSetFixedHeight(chrome, panel->chrome_panel, 0);
    ToriRSChrome_PanelMove(chrome, panel->chrome_panel, x, r.y);
    at = panel->chrome_panel - ui->panel_first;
    ui->docked[at] = 0;
    ui->flown[at] = 1;
}

int
Editor_UiTogglePanel(
    struct EditorUi* ui,
    const char* name)
{
    struct EditorUiPanel* panel;
    int open;

    assert(ui);
    assert(name);
    panel = Editor_UiFindPanel(ui, name);
    if( !panel || panel->chrome_panel < 0 )
        return -1;
    open = !ui->chrome->panels[panel->chrome_panel].visible;
    ToriRSChrome_PanelSetVisible(ui->chrome, panel->chrome_panel, open);
    return open;
}

int
Editor_UiToggleAllPanels(struct EditorUi* ui)
{
    assert(ui);
    if( ui->panels_hidden )
    {
        for( int i = 0; i < ui->panel_count; i++ )
            if( ui->panels_hidden_open[i] && ui->panels[i].chrome_panel >= 0 )
                ToriRSChrome_PanelSetVisible(ui->chrome, ui->panels[i].chrome_panel, 1);
        memset(ui->panels_hidden_open, 0, sizeof(ui->panels_hidden_open));
        ui->panels_hidden = 0;
        return 0;
    }
    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct EditorUiPanel const* panel = &ui->panels[i];
        int const chrome_panel = panel->chrome_panel;
        ui->panels_hidden_open[i] = 0;
        /* The View-menu panels only: the menu bar (no title) stays, so the
         * View menu can still bring any of them back, and so does World. */
        if( chrome_panel < 0 || !panel->title || chrome_panel == ui->world_panel )
            continue;
        /* A member behind another tab is open too: it comes back with it. */
        if( !ui->chrome->panels[chrome_panel].visible && !Editor_UiPanelInStrip(ui, panel->name) )
            continue;
        ui->panels_hidden_open[i] = 1;
        ToriRSChrome_PanelSetVisible(ui->chrome, chrome_panel, 0);
    }
    /* Every strip empties with its windows (the press that brings them back
     * refills it, the front tab first again). */
    for( int i = 0; i < ui->group_count; i++ )
    {
        struct EditorUiGroup* g = &ui->groups[i];
        if( g->shown >= 0 )
            g->prev_shown = g->shown;
        g->shown = -1;
        g->parked = 0;
        memset(g->open, 0, sizeof(g->open));
        ToriRSChrome_PanelSetVisible(ui->chrome, g->chrome_panel, 0);
    }
    ui->panels_hidden = 1;
    return 1;
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

/* ---- the status sentence --------------------------------------------------- */

void
Editor_UiStatus(
    struct EditorUi* ui,
    const char* format,
    ...)
{
    va_list args;
    size_t length;

    assert(ui);
    assert(format);

    va_start(args, format);
    vsnprintf(ui->status, sizeof(ui->status), format, args);
    va_end(args);
    /* One sentence, one line: a trailing newline is TORIRS_LOG's habit. */
    length = strlen(ui->status);
    while( length > 0 && (ui->status[length - 1] == '\n' || ui->status[length - 1] == '\r') )
        ui->status[--length] = '\0';
    /* The console keeps the sentence (R8); the bar shows it for 6 s. */
    Editor_UiLog(ui, "%s", ui->status);
    ui->status_ms = ui->chrome->now_ms;
    ui->status_serial++;
}

const char*
Editor_UiStatusText(const struct EditorUi* ui)
{
    assert(ui);
    if( !ui->status[0] || ui->chrome->now_ms - ui->status_ms >= EDITOR_UI_STATUS_MS )
        return "";
    return ui->status;
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
        Editor_UiOpenSquare(ui, guard->map_x, guard->map_z, guard->storage, guard->storage_index, 0);
        break;
    case EDITOR_UI_GUARD_ENTER_SCENE:
        (void)Editor_Scenes_EnterNow(ui, guard->scene);
        break;
    case EDITOR_UI_GUARD_OPEN_SET:
        Editor_UiOpenSquareSet(ui, guard->set, guard->set_count);
        break;
    case EDITOR_UI_GUARD_CLOSE:
        if( ui->hooks.close_editor )
            ui->hooks.close_editor(ui->hooks_user);
        break;
    case EDITOR_UI_GUARD_CLOSE_SQUARE:
        if( ui->hooks.close_square && Editor_UiWorldShown(ui) )
            ui->hooks.close_square(ui->hooks_user, guard->map_x, guard->map_z, guard->storage);
        else
            Editor_UiStatus(ui, "Close refused: this window shows no world to close m%d_%d in", guard->map_x,
                guard->map_z);
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
