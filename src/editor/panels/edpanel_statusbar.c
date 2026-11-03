/*
 * The status bar: a one-line strip along the bottom of the main window (the
 * menu bar's counterpart; Editor_UiLayout places it, the dock column and the
 * world end above it). Fields, left to right:
 *
 *   tile 3222,3218 L0  m50_50     the hovered tile, its level and its square
 *   sel: 3 locs                   the selection's size and kind (locs,
 *                                 spawns, a rectangle, a tile, none)
 *   Raise r2                      the tool (and a brush's radius)
 *   level Auto | level 1 +above   the edit level
 *   lane working  2 unsaved       the lane and its unsaved document count
 *   live attached  RUN build 2/4  the live link and the RUN in flight
 *   60 fps                        with TORIRS_EDITOR_FPS=1 only
 *
 * Cheap by construction: every field is a LABEL whose text is rebuilt only
 * when the value behind it changes (a serial, a compare), and
 * ToriRSChrome_SetText is a no-op on the same text, so a still frame touches
 * nothing. The ergonomics work's status needs are met here; there is no
 * second bar. Module "statusbar", title "Status bar" (its View row is the
 * show/hide; the saved layout keeps it).
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/editor_doc.h"
#include "editor/live/live_client.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Frames counted over this many chrome milliseconds for the fps field. */
#define STATUSBAR_FPS_WINDOW_MS 1000

struct StatusBarPanel
{
    int panel;
    int label_tile;
    int label_sel;
    int label_tool;
    int label_level;
    int label_lane;
    int label_live;
    int label_fps;

    /* what each label was last made from */
    int shown_hover;
    int shown_x;
    int shown_z;
    int shown_level;
    int shown_sel_kind;
    int shown_sel_count;
    enum Editor_Tool shown_tool;
    int shown_radius;
    int shown_edit_level;
    int shown_above;
    char shown_lane[64];
    int shown_unsaved;
    int shown_live_status;
    int shown_live_progress;
    int shown_live_run;
    int shown_live_absent;

    /* TORIRS_EDITOR_FPS=1 */
    int fps_on;
    int64_t fps_window_start;
    int fps_frames;
    int shown_fps;
};

/* The selection's kind, in the order the bar prefers them. */
enum
{
    STATUSBAR_SEL_NONE = 0,
    STATUSBAR_SEL_LOCS,
    STATUSBAR_SEL_SPAWNS,
    STATUSBAR_SEL_RECT,
    STATUSBAR_SEL_TILE,
};

static void
statusbar_tile(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int x = 0;
    int z = 0;
    int level = 0;
    int const hover = ui->app_services.hover_tile
                      && ui->app_services.hover_tile(ui->app_services.user_data, &x, &z, &level);
    char text[96];

    if( hover == self->shown_hover && (!hover || (x == self->shown_x && z == self->shown_z && level == self->shown_level)) )
        return;
    self->shown_hover = hover;
    self->shown_x = x;
    self->shown_z = z;
    self->shown_level = level;
    if( hover )
        snprintf(text, sizeof(text), "tile %d,%d L%d  m%d_%d", x, z, level, x >> 6, z >> 6);
    else
        snprintf(text, sizeof(text), "tile -");
    ToriRSChrome_SetText(ui->chrome, self->label_tile, text);
}

static void
statusbar_selection(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int kind = STATUSBAR_SEL_NONE;
    int count = 0;
    char text[64];

    if( ui->tools.loc_count > 0 )
    {
        kind = STATUSBAR_SEL_LOCS;
        count = ui->tools.loc_count;
    }
    else if( Editor_Spawns_SelectionCount(ui) > 0 )
    {
        kind = STATUSBAR_SEL_SPAWNS;
        count = Editor_Spawns_SelectionCount(ui);
    }
    else if( ui->tools.rect.valid )
    {
        kind = STATUSBAR_SEL_RECT;
        count = (abs(ui->tools.rect.x1 - ui->tools.rect.x0) + 1) * (abs(ui->tools.rect.z1 - ui->tools.rect.z0) + 1);
    }
    else if( ui->tools.tile_valid )
    {
        kind = STATUSBAR_SEL_TILE;
        count = 1;
    }
    if( kind == self->shown_sel_kind && count == self->shown_sel_count )
        return;
    self->shown_sel_kind = kind;
    self->shown_sel_count = count;
    if( kind == STATUSBAR_SEL_LOCS )
        snprintf(text, sizeof(text), "sel: %d loc%s", count, count == 1 ? "" : "s");
    else if( kind == STATUSBAR_SEL_SPAWNS )
        snprintf(text, sizeof(text), "sel: %d spawn%s", count, count == 1 ? "" : "s");
    else if( kind == STATUSBAR_SEL_RECT )
        snprintf(text, sizeof(text), "sel: rect %d tiles", count);
    else if( kind == STATUSBAR_SEL_TILE )
        snprintf(text, sizeof(text), "sel: tile");
    else
        snprintf(text, sizeof(text), "sel: none");
    ToriRSChrome_SetText(ui->chrome, self->label_sel, text);
}

static void
statusbar_tool(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int const radius = Editor_ToolIsBrush(ui->tools.tool) ? ui->tools.brush.radius : -1;
    char text[64];

    if( ui->tools.tool == self->shown_tool && radius == self->shown_radius )
        return;
    self->shown_tool = ui->tools.tool;
    self->shown_radius = radius;
    if( radius >= 0 )
        snprintf(text, sizeof(text), "%s r%d", Editor_ToolName(ui->tools.tool), radius);
    else
        snprintf(text, sizeof(text), "%s", Editor_ToolName(ui->tools.tool));
    ToriRSChrome_SetText(ui->chrome, self->label_tool, text);
}

static void
statusbar_level(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int const above = edp_level_showing_above(ui);
    char text[48];

    if( ui->tools.edit_level == self->shown_edit_level && above == self->shown_above )
        return;
    self->shown_edit_level = ui->tools.edit_level;
    self->shown_above = above;
    if( ui->tools.edit_level < 0 )
        snprintf(text, sizeof(text), "level Auto");
    else
        snprintf(text, sizeof(text), "level %d%s", ui->tools.edit_level, above ? " +above" : "");
    ToriRSChrome_SetText(ui->chrome, self->label_level, text);
}

static void
statusbar_lane(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    char lane[64];
    int const unsaved = ui->editor ? Editor_DocUnsavedCount(&ui->editor->doc) : 0;
    char text[128];

    if( !Editor_Publish_LaneName(ui, lane, (int)sizeof(lane)) )
        snprintf(lane, sizeof(lane), "%s", ui->editor && ui->editor->host_open ? "..." : "-");
    if( strcmp(lane, self->shown_lane) == 0 && unsaved == self->shown_unsaved )
        return;
    snprintf(self->shown_lane, sizeof(self->shown_lane), "%s", lane);
    self->shown_unsaved = unsaved;
    if( unsaved > 0 )
        snprintf(text, sizeof(text), "lane %s  %d unsaved", lane, unsaved);
    else
        snprintf(text, sizeof(text), "lane %s", lane);
    ToriRSChrome_SetText(ui->chrome, self->label_lane, text);
}

static void
statusbar_live(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    const struct Editor_LiveModel* m = Editor_LivePanelModel(ui);
    char text[224];

    if( !m )
    {
        if( self->shown_live_absent )
            return;
        self->shown_live_absent = 1;
        ToriRSChrome_SetText(ui->chrome, self->label_live, "live -");
        return;
    }
    if( !self->shown_live_absent && m->status_serial == self->shown_live_status
        && m->progress_serial == self->shown_live_progress && m->run_serial == self->shown_live_run )
        return;
    self->shown_live_absent = 0;
    self->shown_live_status = m->status_serial;
    self->shown_live_progress = m->progress_serial;
    self->shown_live_run = m->run_serial;
    if( m->run_step != TORIRSMAPED_RUN_STEP_IDLE )
    {
        char line[160];
        Editor_LiveProgressLine(m, line, sizeof(line));
        snprintf(text, sizeof(text), "live %s  RUN %s", ToriRSMapEd_LiveStateName(m->state),
            line[0] ? line : ToriRSMapEd_RunStepName(m->run_step));
    }
    else if( m->run_serial > 0 )
        snprintf(text, sizeof(text), "live %s  RUN %s", ToriRSMapEd_LiveStateName(m->state),
            m->run_status == 0 ? "done" : "stopped");
    else
        snprintf(text, sizeof(text), "live %s", ToriRSMapEd_LiveStateName(m->state));
    ToriRSChrome_SetText(ui->chrome, self->label_live, text);
}

/** Frames per chrome second, from ToriRSChrome_Tick's clock: a host that
 *  never ticks the chrome shows nothing. */
static void
statusbar_fps(
    struct EditorUi* ui,
    struct StatusBarPanel* self)
{
    int64_t const now = ui->chrome->now_ms;
    char text[32];

    if( !self->fps_on || now <= 0 )
        return;
    if( self->fps_window_start <= 0 )
        self->fps_window_start = now;
    self->fps_frames++;
    if( now - self->fps_window_start < STATUSBAR_FPS_WINDOW_MS )
        return;
    {
        int const fps = (int)((int64_t)self->fps_frames * 1000 / (now - self->fps_window_start));
        self->fps_window_start = now;
        self->fps_frames = 0;
        if( fps == self->shown_fps )
            return;
        self->shown_fps = fps;
        snprintf(text, sizeof(text), "%d fps", fps);
        ToriRSChrome_SetText(ui->chrome, self->label_fps, text);
    }
}

static void
statusbar_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct StatusBarPanel* self = opaque;

    if( !ui->chrome->panels[self->panel].visible )
        return;
    statusbar_tile(ui, self);
    statusbar_selection(ui, self);
    statusbar_tool(ui, self);
    statusbar_level(ui, self);
    statusbar_lane(ui, self);
    statusbar_live(ui, self);
    statusbar_fps(ui, self);
}

static void
statusbar_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct StatusBarPanel* self = opaque;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, visible);
}

static void
statusbar_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_StatusBar_Register(struct EditorUi* ui)
{
    struct StatusBarPanel* self;
    struct EditorUiPanel desc;
    const char* fps = getenv("TORIRS_EDITOR_FPS");

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown_hover = -1;
    self->shown_sel_kind = -1;
    self->shown_tool = EDITOR_TOOL_COUNT;
    self->shown_radius = -2;
    self->shown_edit_level = -2;
    self->shown_above = -1;
    self->shown_unsaved = -1;
    self->shown_live_status = -1;
    self->shown_fps = -1;
    self->fps_on = fps && strcmp(fps, "1") == 0;

    /* A MENUBAR-style strip: its widgets run left to right in one row, the
     * same chrome as the menu bar; the layout puts it along the bottom. */
    self->panel = ToriRSChrome_PanelAdd(ui->chrome, TORIRS_CHROME_PANEL_MENUBAR, 0, 0, UITREE_LAYOUT_ROOT_W, "");
    assert(self->panel >= 0);
    self->label_tile = ToriRSChrome_Label(ui->chrome, self->panel, "tile -");
    self->label_sel = ToriRSChrome_Label(ui->chrome, self->panel, "sel: none");
    self->label_tool = ToriRSChrome_Label(ui->chrome, self->panel, "Select");
    self->label_level = ToriRSChrome_Label(ui->chrome, self->panel, "level Auto");
    self->label_lane = ToriRSChrome_Label(ui->chrome, self->panel, "lane -");
    self->label_live = ToriRSChrome_Label(ui->chrome, self->panel, "live -");
    self->label_fps = ToriRSChrome_Label(ui->chrome, self->panel, "");
    if( !self->fps_on )
        ToriRSChrome_SetHidden(ui->chrome, self->label_fps, 1);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "statusbar";
    /* Titled, so the View menu has its one show/hide row and the layout
     * file keeps it; the layout engine places it (never the fly-out). */
    desc.title = "Status bar";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = statusbar_tick;
    desc.set_visible = statusbar_set_visible;
    desc.destroy = statusbar_destroy;
    Editor_UiAddPanel(ui, &desc);
}
