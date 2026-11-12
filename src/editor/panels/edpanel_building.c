/*
 * The Building tool's panel (module "building", 2026-11-11): what the next
 * gesture with EDITOR_TOOL_BUILDING makes, and the actions that make it.
 *
 * Before it a house was a wall run dragged with the Place tool, a door
 * swapped in by selecting one piece and pressing E, the floor and the
 * "remove roof" flag painted with two more tools, and a roof placed a piece
 * at a time with its shape and rotation chosen by hand: a few hundred
 * interactions for a cottage. Here a drag is the house (editor_building.h
 * plans it from rules read off the content's own houses) and a click is a
 * door or a window:
 *
 *   Shell      drag the footprint: walls and corners on its ring, the floor
 *              and the "remove roof" flag in the room, the roof a level up;
 *              more storeys repeat the walls and floor and join them with a
 *              ladder
 *   Wall line  drag a line: a straight inside wall
 *   Door       click a wall piece: the style's door; click it again: the
 *              wall back
 *   Window     click a wall piece: the style's window, both faces; again
 *              takes it off
 *
 * The styles are short named lists of real locs (editor_building.h); "from
 * pick" puts the Catalog's pick in a role instead, so any wall, door, window
 * or roof kit found in the Catalog or the Gallery can be built with. The
 * panel opens when the tool is armed and goes when another is, as the
 * Rectangle panel does with its tool.
 *
 * Every gesture is one undo step and leaves ordinary locs and tiles.
 */

#include "edpanel_common.h"
#include "edpanel_ops_building.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

/** The Floor dropdown: None, the styles, the Palette's overlay. */
#define BUILDING_FLOOR_OPTIONS_MAX 8
#define BUILDING_ROOF_OPTIONS_MAX 8
#define BUILDING_WALL_OPTIONS_MAX 12

struct BuildingPanel
{
    int panel;
    int row_hint;
    int radio_mode[EDITOR_BUILDING_MODE_COUNT];
    int drop_walls;
    int drop_floor;
    int drop_roof;
    int drop_storeys;
    int check_ladder;
    int check_roof_flag;
    int row_pieces;
    int row_roof;
    int button_wall_pick;
    int button_door_pick;
    int button_window_pick;
    int button_roof_pick;
    int button_build;

    enum Editor_BuildingMode mode;
    /** The Walls style the roles below were last set from. */
    int style;
    /** The locs in each role: the style's, or the Catalog's pick put there
     *  with a "from pick" button. -1: the style has none. */
    int wall_id;
    int door_id;
    int door2_id;
    int window_id;
    int roof_id;
    int edge_id;
    /** 1: the door (or window) role is the Catalog's pick, put there with
     *  its "from pick" button, and every Door (Window) click uses it. 0: a
     *  click uses the door (window) of the style the clicked wall is of
     *  (Editor_BuildingWallStyleOf), the role only where no style names it. */
    int door_picked;
    int window_picked;
    /** The Roof dropdown's choice the roof roles were last set from. */
    int roof_choice;

    enum Editor_Tool shown_tool;
    /** The panel's visibility and ui->layout_runs as the last tick saw
     *  them: a panel that turned visible in a tick a layout ran in was the
     *  saved layout's doing (building_tick). */
    int was_visible;
    unsigned seen_layout_runs;
    enum Editor_BuildingMode shown_mode;
    char shown_pieces[TORIRS_CHROME_LABEL_MAX];
    char shown_roof[TORIRS_CHROME_LABEL_MAX];
    /** ui->hotkeys.serial the controls' chords were labelled from. */
    uint32_t chord_serial;

    const char* wall_options[BUILDING_WALL_OPTIONS_MAX];
    const char* floor_options[BUILDING_FLOOR_OPTIONS_MAX];
    const char* roof_options[BUILDING_ROOF_OPTIONS_MAX];
    int floor_option_count;
    int roof_option_count;
};

static const char* const k_storey_options[EDITOR_BUILDING_STOREYS_MAX] = { "1", "2", "3" };

/* One line each, under the chrome's 63-byte label. */
static const char* const k_mode_names[EDITOR_BUILDING_MODE_COUNT] = { "Shell", "Wall line", "Door", "Window" };
static const char* const k_mode_hints[EDITOR_BUILDING_MODE_COUNT] = {
    "Drag a rectangle: the whole house",
    "Drag a line: an inside wall",
    "Click a wall: a door (again: wall)",
    "Click a wall: a window (again: off)",
};
/* What the status bar says when the mode is chosen. */
static const char* const k_mode_says[EDITOR_BUILDING_MODE_COUNT] = {
    "drag a rectangle for walls, floor and roof in one step; its outer ring is the walls",
    "drag along the tiles an inside wall should follow",
    "click a wall piece for a door; click the door for the wall back",
    "click a wall piece for a window; click again to take it off",
};
static const enum Editor_HotkeyAction k_mode_actions[EDITOR_BUILDING_MODE_COUNT] = {
    EDITOR_HK_BUILDING_SHELL,
    EDITOR_HK_BUILDING_WALL,
    EDITOR_HK_BUILDING_DOOR,
    EDITOR_HK_BUILDING_WINDOW,
};

/** The plan a build makes: one building at a time. */
static struct Editor_BuildingPlan g_building_plan;

static struct BuildingPanel*
building_self(const struct EditorUi* ui)
{
    /* The registry is read, never written, by a lookup. */
    return Editor_UiPanelSelf((struct EditorUi*)ui, "building");
}

/** The roof roles from the Roof dropdown's choice (0 None). */
static void
building_take_roof(
    struct BuildingPanel* self,
    int choice)
{
    self->roof_choice = choice;
    if( choice <= 0 || choice > Editor_BuildingRoofStyleCount() )
    {
        self->roof_id = -1;
        self->edge_id = -1;
        return;
    }
    self->roof_id = Editor_BuildingRoofStyleAt(choice - 1)->roof_id;
    self->edge_id = Editor_BuildingRoofStyleAt(choice - 1)->edge_id;
}

/** The roles from Walls style `style`, and the defaults that go with it: a
 *  house gets a plank floor, the roof it is most often built with and the
 *  flag; a fence gets none of them. */
static void
building_take_style(
    struct EditorUi* ui,
    struct BuildingPanel* self,
    int style)
{
    const struct Editor_BuildingWallStyle* walls = Editor_BuildingWallStyleAt(style);

    self->style = style;
    self->wall_id = walls->wall_id;
    self->door_id = walls->door_id;
    self->door2_id = walls->door2_id;
    self->window_id = walls->window_id;
    self->door_picked = 0;
    self->window_picked = 0;
    /* A fence is one storey; a house keeps what was chosen. */
    if( walls->fence )
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->drop_storeys, 0);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->drop_floor, walls->fence ? 0 : 1);
    ToriRSChrome_DropdownSetSelected(ui->chrome, self->drop_roof, walls->fence ? 0 : walls->roof_style + 1);
    ToriRSChrome_SetChecked(ui->chrome, self->check_roof_flag, !walls->fence);
    building_take_roof(self, walls->fence ? 0 : walls->roof_style + 1);
}

/** The floor overlay the Floor dropdown names, as the .jm2 holds it; -1 for
 *  None (and for the Palette's overlay while the Palette has none). */
static int
building_floor_overlay(
    const struct EditorUi* ui,
    const struct BuildingPanel* self)
{
    int const choice = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_floor);

    if( choice <= 0 )
        return -1;
    if( choice <= Editor_BuildingFloorStyleCount() )
        return Editor_BuildingFloorStyleAt(choice - 1)->overlay;
    return ui->tools.overlay_none ? -1 : ui->tools.overlay_id;
}

static void
building_chords(
    struct EditorUi* ui,
    struct BuildingPanel* self)
{
    for( int m = 0; m < EDITOR_BUILDING_MODE_COUNT; m++ )
        edp_tooltip_chord(ui, self->radio_mode[m], k_mode_actions[m]);
    edp_button_chord(ui, self->button_build, EDITOR_HK_BUILDING_BUILD);
    self->chord_serial = ui->hotkeys.serial;
}

static void
building_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct BuildingPanel* self = opaque;
    char line[TORIRS_CHROME_LABEL_MAX];
    int choice;

    if( self->chord_serial != ui->hotkeys.serial )
        building_chords(ui, self);
    /* A saved layout reopens the flyout where the last session left it open,
     * with Select armed: put it away, as the tool's going would have
     * (2026-11-11: it booted open under the Stamps panel, which hid its
     * Door and Window radios).
     * Shown by View between layouts, it is the user's and stays. */
    {
        int const visible = ui->chrome->panels[self->panel].visible;
        if( visible && !self->was_visible && self->seen_layout_runs != ui->layout_runs
            && ui->tools.tool != EDITOR_TOOL_BUILDING && !ui->chrome->panels[self->panel].user_placed )
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        self->was_visible = ui->chrome->panels[self->panel].visible;
        self->seen_layout_runs = ui->layout_runs;
    }
    /* Arming the tool brings the panel up; another tool's arming puts it
     * away where it is still the flyout it opened as (edpanel_rect.c: the
     * tool flyouts share one spot). Moved or docked, it is the user's. */
    if( self->shown_tool != ui->tools.tool )
    {
        if( ui->tools.tool == EDITOR_TOOL_BUILDING )
        {
            /* In front of a panel opened over the same spot since. */
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
            ToriRSChrome_PanelRaise(ui->chrome, self->panel);
            Editor_UiStatus(ui, "Building, %s: %s", k_mode_names[self->mode], k_mode_says[self->mode]);
        }
        else if( self->shown_tool == EDITOR_TOOL_BUILDING && !ui->chrome->panels[self->panel].user_placed )
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        self->shown_tool = ui->tools.tool;
    }
    if( !ui->chrome->panels[self->panel].visible )
        return;

    for( int m = 0; m < EDITOR_BUILDING_MODE_COUNT; m++ )
        if( m != (int)self->mode && ToriRSChrome_Checked(ui->chrome, self->radio_mode[m]) )
            edp_building_set_mode(ui, (enum Editor_BuildingMode)m);
    if( self->shown_mode != self->mode )
    {
        ToriRSChrome_SetText(ui->chrome, self->row_hint, k_mode_hints[self->mode]);
        self->shown_mode = self->mode;
    }
    choice = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_walls);
    if( choice >= 0 && choice != self->style )
        building_take_style(ui, self, choice);
    choice = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_roof);
    if( choice >= 0 && choice != self->roof_choice )
        building_take_roof(self, choice);

    /* The records the next gesture reads, asked for while the user is still
     * choosing (edp_loc_shapes requests one that is not resident). */
    {
        int const ids[5] = { self->wall_id, self->door_id, self->window_id, self->roof_id, self->edge_id };
        uint32_t mask;
        for( int i = 0; i < 5; i++ )
            if( ids[i] >= 0 )
                (void)edp_loc_shapes(ui, ids[i], &mask);
    }

    if( self->door_id >= 0 && self->window_id >= 0 )
        snprintf(line, sizeof(line), "wall %d, door %d, window %d", self->wall_id, self->door_id, self->window_id);
    else if( self->door_id >= 0 && self->door2_id >= 0 )
        snprintf(line, sizeof(line), "fence %d, gate %d and %d", self->wall_id, self->door_id, self->door2_id);
    else if( self->door_id >= 0 )
        snprintf(line, sizeof(line), "wall %d, door %d, no window", self->wall_id, self->door_id);
    else
        snprintf(line, sizeof(line), "wall %d, no door, no window", self->wall_id);
    if( strcmp(line, self->shown_pieces) != 0 )
    {
        snprintf(self->shown_pieces, sizeof(self->shown_pieces), "%s", line);
        ToriRSChrome_SetText(ui->chrome, self->row_pieces, line);
    }
    if( self->roof_id >= 0 && self->edge_id >= 0 )
        snprintf(line, sizeof(line), "roof %d, eaves %d", self->roof_id, self->edge_id);
    else
        snprintf(line, sizeof(line), "no roof");
    if( strcmp(line, self->shown_roof) != 0 )
    {
        snprintf(self->shown_roof, sizeof(self->shown_roof), "%s", line);
        ToriRSChrome_SetText(ui->chrome, self->row_roof, line);
    }
}

/** The Catalog's loc pick into a role; says so. 0 when there is no loc
 *  pick. */
static int
building_pick(
    struct EditorUi* ui,
    const char* role,
    int* out_id)
{
    if( ui->tools.pick_kind != 0 || ui->tools.pick_id < 0 )
    {
        Editor_UiStatus(ui, "%s from pick refused: pick a loc in the Catalog or the Gallery first", role);
        return 0;
    }
    *out_id = ui->tools.pick_id;
    Editor_UiStatus(ui, "Building: the %s is now %s", role, ui->tools.pick_name);
    return 1;
}

static int
building_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct BuildingPanel* self = opaque;
    uint32_t mask = 0;

    for( int m = 0; m < EDITOR_BUILDING_MODE_COUNT; m++ )
        if( widget == self->radio_mode[m] )
        {
            edp_building_set_mode(ui, (enum Editor_BuildingMode)m);
            return 1;
        }
    if( widget == self->button_wall_pick )
        (void)building_pick(ui, "wall", &self->wall_id);
    else if( widget == self->button_door_pick )
    {
        /* A picked door is one piece: the style's second leaf goes. */
        if( building_pick(ui, "door", &self->door_id) )
        {
            self->door2_id = -1;
            self->door_picked = 1;
        }
    }
    else if( widget == self->button_window_pick )
    {
        if( building_pick(ui, "window", &self->window_id) )
            self->window_picked = 1;
    }
    else if( widget == self->button_roof_pick )
    {
        /* A kit that is one loc (slopes and edge) takes both roles; a roof
         * loc with no edge of its own keeps the edge that was chosen. */
        if( building_pick(ui, "roof", &self->roof_id) )
        {
            if( edp_loc_shapes(ui, self->roof_id, &mask) && ((mask >> 18) & 1u) )
                self->edge_id = self->roof_id;
            else if( self->edge_id < 0 )
                self->edge_id = Editor_BuildingRoofStyleAt(0)->edge_id;
        }
    }
    else if( widget == self->button_build )
        (void)edp_building_build(ui, &ui->tools.rect);
    else if( widget == self->drop_walls || widget == self->drop_floor || widget == self->drop_roof
             || widget == self->drop_storeys || widget == self->check_ladder || widget == self->check_roof_flag )
        return 1;
    else
        return 0;
    return 1;
}

static void
building_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct BuildingPanel* self = opaque;
    /* Opens with its tool, not with the editor. */
    if( visible && ui->tools.tool != EDITOR_TOOL_BUILDING )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
building_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

enum Editor_BuildingMode
edp_building_mode(const struct EditorUi* ui)
{
    const struct BuildingPanel* self;

    assert(ui);
    self = building_self(ui);
    return self ? self->mode : EDITOR_BUILDING_MODE_SHELL;
}

void
edp_building_set_mode(
    struct EditorUi* ui,
    enum Editor_BuildingMode mode)
{
    struct BuildingPanel* self;

    assert(ui);
    assert(mode >= 0);
    assert(mode < EDITOR_BUILDING_MODE_COUNT);
    self = building_self(ui);
    if( !self )
        return;
    self->mode = mode;
    ToriRSChrome_RadioSelect(ui->chrome, self->radio_mode[mode]);
    /* A mode is chosen to be used: the tool comes with it. */
    if( ui->tools.tool != EDITOR_TOOL_BUILDING )
    {
        self->shown_tool = EDITOR_TOOL_BUILDING;
        Editor_UiSetTool(ui, EDITOR_TOOL_BUILDING);
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
        ToriRSChrome_PanelRaise(ui->chrome, self->panel);
    }
    Editor_UiStatus(ui, "Building, %s: %s", k_mode_names[mode], k_mode_says[mode]);
}

int
edp_building_drags(const struct EditorUi* ui)
{
    enum Editor_BuildingMode const mode = edp_building_mode(ui);
    return mode == EDITOR_BUILDING_MODE_SHELL || mode == EDITOR_BUILDING_MODE_WALL;
}

/** A role's shapes, or 0 with a sentence while its record is loading. */
static int
building_shapes(
    struct EditorUi* ui,
    int loc_id,
    const char* role,
    uint32_t* out_mask)
{
    if( edp_loc_shapes(ui, loc_id, out_mask) )
        return 1;
    Editor_UiStatus(ui, "Building: the %s (loc %d) is still loading from the cache; try again in a moment", role,
        loc_id);
    return 0;
}

/** The loc selection emptied: a building is not a set to rotate or swap by
 *  the next key. */
static void
building_deselect(struct EditorUi* ui)
{
    if( ui->tools.loc_count == 0 )
        return;
    ui->tools.loc_count = 0;
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
}

static int
building_wall_line(
    struct EditorUi* ui,
    struct BuildingPanel* self,
    const struct Editor_TileRect* rect)
{
    static struct Editor_BuildingPiece pieces[EDITOR_BUILDING_SIDE_MAX];
    struct Editor_BuildingApplyResult result;
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    uint32_t mask = 0;
    int const dx = rect->x1 > rect->x0 ? rect->x1 - rect->x0 : rect->x0 - rect->x1;
    int const dz = rect->z1 > rect->z0 ? rect->z1 - rect->z0 : rect->z0 - rect->z1;
    /* The longer way the drag went is the wall's line; it starts on the row
     * (or column) the press was on. */
    int const along_x = dx >= dz;
    int const length = (along_x ? dx : dz) + 1;
    int const x = along_x ? (rect->x0 < rect->x1 ? rect->x0 : rect->x1) : rect->x0;
    int const z = along_x ? rect->z0 : (rect->z0 < rect->z1 ? rect->z0 : rect->z1);
    int count;
    int applied;

    if( length < 2 )
    {
        Editor_UiStatus(ui, "Wall line refused: drag along the tiles the wall should follow (two or more)");
        return 0;
    }
    if( length > EDITOR_BUILDING_SIDE_MAX )
    {
        Editor_UiStatus(ui, "Wall line refused: %d tiles is longer than the %d a wall line takes", length,
            EDITOR_BUILDING_SIDE_MAX);
        return 0;
    }
    if( !building_shapes(ui, self->wall_id, "wall", &mask) )
        return 0;
    if( !(mask & 1u) )
    {
        Editor_UiStatus(ui, "Wall line refused: loc %d has no straight wall (shape 0); choose a wall", self->wall_id);
        return 0;
    }
    (void)Editor_BuildingWallLine(self->wall_id, x, z, rect->level, length, along_x, pieces);
    count = Editor_BuildingCommands(&ui->editor->doc, pieces, length, commands, capacity, &result);
    if( count == 0 )
    {
        if( result.closed > 0 )
            Editor_UiStatus(ui, "Wall line refused: %d,%d is not in an open square (open it first)", result.closed_x,
                result.closed_z);
        else
            Editor_UiStatus(ui, "Wall line: that wall is already there exactly; nothing changed");
        return 0;
    }
    applied = edp_apply(ui, commands, count, "wall line");
    if( applied > 0 )
    {
        building_deselect(ui);
        Editor_UiStatus(ui, "Wall line: %d pieces along the %s edge of %s %d from %d,%d L%d; Ctrl+Z undoes it",
            result.placed, along_x ? "south" : "west", along_x ? "row" : "column", along_x ? z : x, x, z, rect->level);
    }
    return applied;
}

int
edp_building_build(
    struct EditorUi* ui,
    const struct Editor_TileRect* rect)
{
    struct BuildingPanel* self;
    struct Editor_BuildingSpec spec;
    struct Editor_BuildingApplyResult result;
    struct Editor_TileRect r;
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int cleared;
    int count;
    int applied;

    assert(ui);
    assert(rect);
    self = building_self(ui);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Building refused: there is no editor session");
        return 0;
    }
    if( !rect->valid || !Editor_TileRectNormalise(rect, &r) )
    {
        Editor_UiStatus(ui, "Building refused: drag a rectangle in the World first (its outer ring is the walls)");
        return 0;
    }
    if( r.level < 0 || r.level >= EDITOR_SQUARE_LEVELS )
    {
        Editor_UiStatus(ui, "Building refused: level %d is not a level (0 to 3)", r.level);
        return 0;
    }
    if( self->mode == EDITOR_BUILDING_MODE_WALL )
        return building_wall_line(ui, self, rect);

    memset(&spec, 0, sizeof(spec));
    spec.x0 = r.x0;
    spec.z0 = r.z0;
    spec.x1 = r.x1;
    spec.z1 = r.z1;
    spec.level = r.level;
    spec.storeys = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_storeys) + 1;
    if( spec.storeys < 1 )
        spec.storeys = 1;
    spec.wall_id = self->wall_id;
    spec.outward = self->style >= 0 && Editor_BuildingWallStyleAt(self->style)->fence;
    spec.floor_overlay = building_floor_overlay(ui, self);
    spec.roof_flag = ToriRSChrome_Checked(ui->chrome, self->check_roof_flag);
    spec.roof_id = self->roof_id;
    spec.edge_id = self->edge_id;
    spec.ladder_up_id = -1;
    spec.ladder_down_id = -1;
    if( spec.storeys > 1 && ToriRSChrome_Checked(ui->chrome, self->check_ladder) )
    {
        uint32_t ladder_mask;
        spec.ladder_up_id = EDITOR_BUILDING_LADDER_UP;
        spec.ladder_down_id = EDITOR_BUILDING_LADDER_DOWN;
        if( !building_shapes(ui, spec.ladder_up_id, "ladder", &ladder_mask)
            || !building_shapes(ui, spec.ladder_down_id, "ladder top", &ladder_mask) )
            return 0;
    }
    if( !building_shapes(ui, spec.wall_id, "wall", &spec.wall_mask) )
        return 0;
    if( spec.roof_id >= 0 && spec.edge_id >= 0
        && (!building_shapes(ui, spec.roof_id, "roof", &spec.roof_mask)
            || !building_shapes(ui, spec.edge_id, "roof edge", &spec.edge_mask)) )
        return 0;

    if( Editor_BuildingPlanMake(&spec, &g_building_plan) == 0 )
    {
        int const w = r.x1 - r.x0 + 1;
        int const h = r.z1 - r.z0 + 1;
        if( g_building_plan.refusal == EDITOR_BUILDING_TOO_SMALL && spec.outward )
            Editor_UiStatus(ui, "Building refused: %dx%d is too small to fence; drag at least 2x2", w, h);
        else if( g_building_plan.refusal == EDITOR_BUILDING_TOO_SMALL )
            Editor_UiStatus(ui, "Building refused: %dx%d has no room inside its walls; drag at least %dx%d", w, h,
                EDITOR_BUILDING_SIDE_MIN, EDITOR_BUILDING_SIDE_MIN);
        else if( g_building_plan.refusal == EDITOR_BUILDING_TOO_LARGE )
            Editor_UiStatus(ui, "Building refused: %dx%d is larger than %dx%d, the most one building takes", w, h,
                EDITOR_BUILDING_SIDE_MAX, EDITOR_BUILDING_SIDE_MAX);
        else if( g_building_plan.refusal == EDITOR_BUILDING_NO_LEVEL )
            Editor_UiStatus(ui, "Building refused: %d storey%s from level %d put the %s above level 3; build lower or "
                                "with fewer storeys",
                spec.storeys, spec.storeys == 1 ? "" : "s", spec.level, spec.roof_id >= 0 ? "roof" : "walls");
        else if( g_building_plan.refusal == EDITOR_BUILDING_NOT_A_WALL )
            Editor_UiStatus(ui, "Building refused: loc %d has no straight wall (shape 0); choose a wall", spec.wall_id);
        else if( g_building_plan.refusal == EDITOR_BUILDING_NOT_A_ROOF )
            Editor_UiStatus(ui, "Building refused: loc %d / %d is not a roof with an edge (shapes 12 or 17, and 18)",
                spec.roof_id, spec.edge_id);
        else
            Editor_UiStatus(ui, "Building refused: nothing to build; choose walls, a floor or a roof");
        return 0;
    }
    /* A roof standing where this building's storeys now go (the same
     * footprint built again a storey taller) comes off in the same step. */
    cleared = Editor_BuildingClearRoofCommands(&ui->editor->doc, g_building_plan.x0, g_building_plan.z0,
        g_building_plan.x1, g_building_plan.z1, spec.level, spec.level + spec.storeys - 1, commands, capacity);
    /* And the grass, soil and twigs a Scatter left on the ground it is built
     * over: indoors now (a fence's pen keeps its grass). */
    if( !spec.outward )
        cleared += Editor_BuildingClearDecorCommands(&ui->editor->doc, g_building_plan.x0, g_building_plan.z0,
            g_building_plan.x1, g_building_plan.z1, spec.level, commands + cleared, capacity - cleared);
    count = cleared + Editor_BuildingCommands(&ui->editor->doc, g_building_plan.pieces, g_building_plan.count,
                          commands + cleared, capacity - cleared, &result);
    if( count >= capacity )
    {
        Editor_UiStatus(ui, "Building refused: that is more than the %d edits one step takes; build it smaller or "
                            "a storey at a time",
            capacity);
        return 0;
    }
    if( count == 0 )
    {
        if( result.closed > 0 )
            Editor_UiStatus(ui, "Building refused: %d,%d is not in an open square (open it first)", result.closed_x,
                result.closed_z);
        else
            Editor_UiStatus(ui, "Building: that building is already there exactly; nothing changed");
        return 0;
    }
    applied = edp_apply(ui, commands, count, "building");
    if( applied > 0 )
    {
        char roof[48] = "no roof";
        building_deselect(ui);
        if( spec.roof_id >= 0 && spec.edge_id >= 0 )
            snprintf(roof, sizeof(roof), "a roof on L%d", spec.level + spec.storeys);
        Editor_UiStatus(ui, "Built %dx%d at %d,%d L%d: %d wall pieces, %d floor tiles, %d storey%s, %s%s; Ctrl+Z undoes it",
            g_building_plan.x1 - g_building_plan.x0 + 1, g_building_plan.z1 - g_building_plan.z0 + 1,
            g_building_plan.x0, g_building_plan.z0, spec.level,
            g_building_plan.parts[EDITOR_BUILDING_PART_WALL] + g_building_plan.parts[EDITOR_BUILDING_PART_CORNER],
            result.tiles, spec.storeys, spec.storeys == 1 ? "" : "s", roof,
            result.closed > 0 ? " (pieces outside the open squares left out)" : "");
    }
    return applied;
}

int
edp_building_click(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    const struct Editor_LocRef* pointed)
{
    struct BuildingPanel* self;
    struct Editor_BuildingSwapResult result;
    struct Editor_LocRef wall;
    enum Editor_BuildingWallFind found;
    struct Editor_Cmd commands[2];
    const char* what;
    const char* style_name;
    int door;
    int door_id;
    int door2_id;
    int window_id;
    int wall_id;
    int style;
    int count;
    int applied;

    assert(ui);
    self = building_self(ui);
    if( !self || !ui->editor )
        return 0;
    if( self->mode != EDITOR_BUILDING_MODE_DOOR && self->mode != EDITOR_BUILDING_MODE_WINDOW )
    {
        Editor_UiStatus(ui, "Building, %s: %s", k_mode_names[self->mode], k_mode_says[self->mode]);
        return 0;
    }
    door = self->mode == EDITOR_BUILDING_MODE_DOOR;
    what = door ? "Door" : "Window";
    if( level < 0 || level >= EDITOR_SQUARE_LEVELS )
        return 0;
    /* The piece under the pointer when it is a straight wall (a door is
     * one); a window clicked on its own face names the tile it hangs on. */
    if( pointed && pointed->shape == 0 && Editor_LocRefResolve(&ui->editor->doc, pointed) >= 0 )
    {
        wall = *pointed;
        found = EDITOR_BUILDING_WALL_FOUND;
    }
    else
    {
        if( pointed && pointed->shape >= 4 && pointed->shape <= 8 )
            Editor_LocRefWorld(pointed, &world_x, &world_z);
        found = Editor_BuildingFindWall(&ui->editor->doc, world_x, world_z, level, &wall);
    }
    if( found == EDITOR_BUILDING_WALL_NONE )
    {
        Editor_UiStatus(ui, "%s refused: no straight wall piece at or beside %d,%d L%d; click one (not a corner)",
            what, world_x, world_z, level);
        return 0;
    }
    if( found == EDITOR_BUILDING_WALL_AMBIGUOUS )
    {
        Editor_UiStatus(ui, "%s refused: two walls meet at %d,%d; click the wall piece itself", what, world_x,
            world_z);
        return 0;
    }
    /* The door or window of the wall that was clicked, not of whatever the
     * Walls dropdown was last set to for another building. */
    door_id = self->door_id;
    door2_id = self->door2_id;
    window_id = self->window_id;
    wall_id = self->wall_id;
    style_name = self->style >= 0 ? Editor_BuildingWallStyleAt(self->style)->name : "this wall";
    style = Editor_BuildingWallStyleOf(wall.loc_id);
    if( style >= 0 && (door ? !self->door_picked : !self->window_picked) )
    {
        const struct Editor_BuildingWallStyle* walls = Editor_BuildingWallStyleAt(style);
        door_id = walls->door_id;
        door2_id = walls->door2_id;
        window_id = walls->window_id;
        wall_id = walls->wall_id;
        style_name = walls->name;
    }
    if( (door ? door_id : window_id) < 0 )
    {
        Editor_UiStatus(ui, "%s refused: %s has none; pick one in the Catalog, then press %s from pick", what,
            style_name, what);
        return 0;
    }
    if( !edp_loc_shape_check(ui, door ? door_id : window_id, door ? 0 : 5, what) )
        return 0;
    count = door ? Editor_BuildingDoorCommands(&ui->editor->doc, &wall, door_id, door2_id, wall_id, commands, 2,
                       &result)
                 : Editor_BuildingWindowCommands(&ui->editor->doc, &wall, window_id, commands, 2, &result);
    if( count == 0 )
    {
        int wx;
        int wz;
        Editor_LocRefWorld(&wall, &wx, &wz);
        if( result.no_leaf )
            Editor_UiStatus(ui, "Door refused: a gate takes two fence pieces side by side, and %d,%d has no plain "
                                "one beside it",
                wx, wz);
        else
            Editor_UiStatus(ui, "%s refused: the wall at %d,%d L%d is no longer there", what, wx, wz, wall.level);
        return 0;
    }
    applied = edp_apply(ui, commands, count, door ? "door" : "window");
    if( applied > 0 )
    {
        int wx;
        int wz;
        /* The piece the swap started from: a gate that opened the other way
         * names its first leaf, not the tile clicked. */
        Editor_LocRefWorld(&result.ref, &wx, &wz);
        building_deselect(ui);
        if( door && result.added )
            Editor_UiStatus(ui, "Door: loc %d in the wall at %d,%d L%d%s; click it again for the wall back",
                result.loc_id, wx, wz, wall.level, applied > 1 ? " (a gate: two leaves)" : "");
        else if( door )
            Editor_UiStatus(ui, "Door: the wall at %d,%d L%d is wall %d again", wx, wz, wall.level, result.loc_id);
        else if( result.added )
            Editor_UiStatus(ui, "Window: loc %d on the wall at %d,%d L%d, both faces; click again to take it off",
                result.loc_id, wx, wz, wall.level);
        else
            Editor_UiStatus(ui, "Window: taken off the wall at %d,%d L%d", wx, wz, wall.level);
    }
    return applied;
}

int
Editor_Building_RunAction(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action)
{
    assert(ui);
    for( int m = 0; m < EDITOR_BUILDING_MODE_COUNT; m++ )
        if( action == k_mode_actions[m] )
        {
            edp_building_set_mode(ui, (enum Editor_BuildingMode)m);
            return 1;
        }
    if( action == EDITOR_HK_BUILDING_BUILD )
    {
        (void)edp_building_build(ui, &ui->tools.rect);
        return 1;
    }
    return 0;
}

void
Editor_Building_Register(struct EditorUi* ui)
{
    struct BuildingPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    int n;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown_tool = EDITOR_TOOL_COUNT;
    self->shown_mode = EDITOR_BUILDING_MODE_COUNT;
    self->style = -1;
    self->roof_choice = -1;

    assert(Editor_BuildingWallStyleCount() <= BUILDING_WALL_OPTIONS_MAX);
    for( int i = 0; i < Editor_BuildingWallStyleCount(); i++ )
        self->wall_options[i] = Editor_BuildingWallStyleAt(i)->name;
    n = 0;
    self->floor_options[n++] = "None";
    for( int i = 0; i < Editor_BuildingFloorStyleCount(); i++ )
        self->floor_options[n++] = Editor_BuildingFloorStyleAt(i)->name;
    self->floor_options[n++] = "The Palette's overlay";
    assert(n <= BUILDING_FLOOR_OPTIONS_MAX);
    self->floor_option_count = n;
    n = 0;
    self->roof_options[n++] = "None";
    for( int i = 0; i < Editor_BuildingRoofStyleCount(); i++ )
        self->roof_options[n++] = Editor_BuildingRoofStyleAt(i)->name;
    assert(n <= BUILDING_ROOF_OPTIONS_MAX);
    self->roof_option_count = n;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, EDP_TOP_Y), edp_px(ui, 260), "Building");
    assert(self->panel >= 0);
    self->row_hint = ToriRSChrome_Label(chrome, self->panel, k_mode_hints[EDITOR_BUILDING_MODE_SHELL]);
    for( int m = 0; m < EDITOR_BUILDING_MODE_COUNT; m++ )
    {
        self->radio_mode[m] = ToriRSChrome_Radio(chrome, self->panel, k_mode_names[m], 1, m == 0);
        ToriRSChrome_SetTooltip(chrome, self->radio_mode[m], k_mode_hints[m]);
    }
    ToriRSChrome_Separator(chrome, self->panel);
    self->drop_walls = ToriRSChrome_Dropdown(chrome, self->panel, "Walls", self->wall_options,
        Editor_BuildingWallStyleCount(), 0);
    ToriRSChrome_SetTooltip(chrome, self->drop_walls, "The wall, with the door and window that go with it");
    self->drop_floor =
        ToriRSChrome_Dropdown(chrome, self->panel, "Floor", self->floor_options, self->floor_option_count, 1);
    ToriRSChrome_SetTooltip(chrome, self->drop_floor, "The overlay laid on the room's tiles");
    self->drop_roof = ToriRSChrome_Dropdown(chrome, self->panel, "Roof", self->roof_options, self->roof_option_count, 1);
    ToriRSChrome_SetTooltip(chrome, self->drop_roof, "Eaves, slopes and a flat top, one level above the walls");
    self->drop_storeys =
        ToriRSChrome_Dropdown(chrome, self->panel, "Storeys", k_storey_options, EDITOR_BUILDING_STOREYS_MAX, 0);
    ToriRSChrome_SetTooltip(chrome, self->drop_storeys, "Walls and floor on this many levels, the roof above");
    self->check_ladder = ToriRSChrome_Checkbox(chrome, self->panel, "Ladder between storeys", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_ladder, "A ladder in the room's north-east corner, and its top above");
    self->check_roof_flag = ToriRSChrome_Checkbox(chrome, self->panel, "Hide the roof indoors", 1);
    ToriRSChrome_SetTooltip(chrome, self->check_roof_flag, "Sets the remove-roof flag on the room's tiles");
    ToriRSChrome_Separator(chrome, self->panel);
    self->row_pieces = ToriRSChrome_Label(chrome, self->panel, "");
    self->row_roof = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_wall_pick = ToriRSChrome_Button(chrome, self->panel, "Wall from pick");
    ToriRSChrome_SetTooltip(chrome, self->button_wall_pick, "Build with the Catalog's pick as the wall");
    self->button_door_pick = ToriRSChrome_Button(chrome, self->panel, "Door from pick");
    ToriRSChrome_SetTooltip(chrome, self->button_door_pick, "The Catalog's pick is the door a Door click puts in");
    self->button_window_pick = ToriRSChrome_Button(chrome, self->panel, "Window from pick");
    ToriRSChrome_SetTooltip(chrome, self->button_window_pick, "The Catalog's pick is the window a Window click hangs");
    self->button_roof_pick = ToriRSChrome_Button(chrome, self->panel, "Roof from pick");
    ToriRSChrome_SetTooltip(chrome, self->button_roof_pick, "Build with the Catalog's pick as the roof");
    ToriRSChrome_Separator(chrome, self->panel);
    self->button_build = ToriRSChrome_Button(chrome, self->panel, "Build on rectangle");
    ToriRSChrome_SetTooltip(chrome, self->button_build, "Build on the rectangle last dragged (any tool's)");
    ToriRSChrome_ButtonSetFill(chrome, self->button_wall_pick, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_door_pick, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_window_pick, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_roof_pick, 1);
    ToriRSChrome_ButtonSetFill(chrome, self->button_build, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);
    building_take_style(ui, self, 0);
    building_chords(ui, self);

    memset(&desc, 0, sizeof(desc));
    desc.name = "building";
    desc.title = "Building";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = building_tick;
    desc.activate = building_activate;
    desc.set_visible = building_set_visible;
    desc.destroy = building_destroy;
    Editor_UiAddPanel(ui, &desc);
}
