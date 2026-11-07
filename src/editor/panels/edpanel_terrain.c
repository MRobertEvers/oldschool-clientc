/*
 * Terrain: the brush's SLIDERs (radius, step, strength), its falloff and
 * the edit level as RADIO groups, the flags brush's four bits, and the view's
 * level cap (design #terrain). Every control writes the shared tool state
 * the moment it changes; the world click reads it on the next press.
 *
 * The Height brush's section (shown while it is armed): which way it may
 * move the ground, the grid's height (the target, which Ctrl/Cmd+wheel and
 * Ctrl/Cmd+click also set; "Follow the ground" unsets it so the grid stands on
 * the hovered tile again), and whether the grid shows and how solid its
 * fill is.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <stdio.h>
#include <string.h>

enum
{
    TERRAIN_GROUP_FALLOFF = 1,
    TERRAIN_GROUP_LEVEL = 2,
    TERRAIN_GROUP_VIS = 3,
    TERRAIN_GROUP_PATH = 4,
    TERRAIN_GROUP_HEIGHT = 5,
};

/** The grid slider's top: EDITOR_HEIGHT_TARGET_MIN in height units. */
#define TERRAIN_HEIGHT_UNITS_MAX (-EDITOR_HEIGHT_TARGET_MIN / EDITOR_HEIGHT_UNIT)

static const char* const k_falloff_names[EDITOR_BRUSH_FALLOFF_COUNT] = { "linear", "smooth", "flat" };
static const char* const k_level_names[5] = { "auto", "0", "1", "2", "3" };
static const char* const k_vis_names[5] = { "all", "0", "1", "2", "3" };
static const char* const k_path_names[EDITOR_PATH_STYLE_COUNT] = { "dirt", "pavement" };
static const char* const k_height_mode_names[EDITOR_HEIGHT_MODE_COUNT] = { "up and down", "raise only",
                                                                         "lower only" };
static const char* const k_flag_names[4] = { "block", "link below (bridge)", "remove roof",
                                             "vis below" };

struct TerrainPanel
{
    int panel;
    int row_tool;
    int slider_radius;
    int slider_step;
    int slider_strength;
    int radio_falloff[EDITOR_BRUSH_FALLOFF_COUNT];
    int radio_level[5];
    int radio_vis[5];
    int check_solo;
    int check_flags[4];
    /** The Path tool's section (shown while it is armed): the style, the
     *  width in half tiles, dirt's roughness, New edges (another seed). */
    int label_path;
    int radio_path[EDITOR_PATH_STYLE_COUNT];
    int slider_path_width;
    int slider_path_rough;
    int button_path_seed;
    int shown_path_style;
    int shown_path_width;
    int shown_path_rough;
    /** The Scatter tool's section (shown while it is armed, under the
     *  radius and falloff): the density, random rotation, New seed. */
    int label_scatter;
    int slider_scatter_density;
    int check_scatter_rotate;
    int button_scatter_seed;
    int shown_scatter_density;
    int shown_scatter_rotate;
    /** The Plateau tool's skirt (shown while it is armed). */
    int slider_plateau_skirt;
    int shown_plateau_skirt;
    /** Paint underlay's mix (shown while it is armed): the share of the
     *  brush's tiles painted, the dither the client blends. */
    int slider_mix;
    int shown_mix;
    /** The Height brush's section (shown while it is armed). */
    int label_height;
    int radio_height_mode[EDITOR_HEIGHT_MODE_COUNT];
    int slider_height;
    int button_height_follow;
    int check_height_grid;
    int slider_height_fill;
    int shown_height_mode;
    int shown_height_valid;
    int shown_height_target;
    int shown_height_grid;
    int shown_height_fill;
    /** What the readout and the radios were last set from. */
    enum Editor_Tool shown_tool;
    int shown_level;
    int shown_vis;
    /** The brush and flags the sliders and boxes were last set from: a
     *  hotkey ([ ] Shift+[ Ctrl+[ ...) or the eyedropper moves the shared
     *  state and the controls follow. */
    struct Editor_Brush shown_brush;
    int shown_flags;
    /** ui->hotkeys.serial the sliders' labels and tips were labelled from. */
    uint32_t hint_serial;
};

/**
 * A slider wears its two steppers' chords after its label, compacted the way
 * the hint strip says them ("Radius  [ ]", "Strength  Shift+[ ]"; design
 * section 15), and its tooltip ends with them as a keycap. The label alone
 * when both are unbound.
 */
static void
terrain_slider_chords(
    struct EditorUi* ui,
    int widget,
    const char* label,
    const char* text,
    enum Editor_HotkeyAction down,
    enum Editor_HotkeyAction up)
{
    int const actions[2] = { (int)down, (int)up };
    char chords[EDITOR_HK_HINT_CHORD_MAX];
    char caption[TORIRS_CHROME_LABEL_MAX];

    Editor_HotkeysJoin(&ui->hotkeys, actions, 2, chords, (int)sizeof(chords));
    if( chords[0] )
        snprintf(caption, sizeof(caption), "%s  %s", label, chords);
    else
        snprintf(caption, sizeof(caption), "%s", label);
    ToriRSChrome_SetLabel(ui->chrome, widget, caption);
    ToriRSChrome_SetTooltip(ui->chrome, widget, text);
    ToriRSChrome_SetTooltipChord(ui->chrome, widget, chords);
}

static void
terrain_hints(
    struct EditorUi* ui,
    struct TerrainPanel* self)
{
    terrain_slider_chords(ui, self->slider_radius, "Radius", "Brush radius in tiles", EDITOR_HK_BRUSH_RADIUS_DOWN,
        EDITOR_HK_BRUSH_RADIUS_UP);
    terrain_slider_chords(ui, self->slider_step, "Step", "Height: units per wheel notch; Roughen: amplitude",
        EDITOR_HK_BRUSH_STEP_DOWN, EDITOR_HK_BRUSH_STEP_UP);
    terrain_slider_chords(ui, self->slider_strength, "Strength",
        "Height and Smooth's pull per stroke; also density, skirt, mix", EDITOR_HK_BRUSH_STRENGTH_DOWN,
        EDITOR_HK_BRUSH_STRENGTH_UP);
    self->hint_serial = ui->hotkeys.serial;
}

/* ---- the footprint --------------------------------------------------------- */

int
Editor_Terrain_BrushFootprint(
    const struct Editor_Brush* brush,
    struct Editor_BrushTile* out,
    int max)
{
    int count = 0;

    assert(brush);
    assert(out);
    assert(brush->radius >= 0);
    assert(brush->radius <= EDITOR_BRUSH_RADIUS_MAX);

    /* Centre first, so a caller short of room still shows where it lands. */
    if( max > 0 )
    {
        out[0].dx = 0;
        out[0].dz = 0;
        out[0].weight = Editor_BrushWeight(brush, 0, 0);
        count = 1;
    }
    /* The square Editor_BrushCommands walks, kept where Editor_BrushWeight
     * (the stroke's own weight) is above zero: the preview cannot disagree
     * with the stroke about which tiles, or how strongly. */
    for( int dz = -brush->radius; dz <= brush->radius; dz++ )
        for( int dx = -brush->radius; dx <= brush->radius; dx++ )
        {
            double const weight = Editor_BrushWeight(brush, dx, dz);
            if( weight <= 0.0 || (dx == 0 && dz == 0) || count >= max )
                continue;
            out[count].dx = dx;
            out[count].dz = dz;
            out[count].weight = weight;
            count++;
        }
    return count;
}

int
Editor_Terrain_ToolUsesFalloff(int tool)
{
    return tool == EDITOR_TOOL_HEIGHT || tool == EDITOR_TOOL_SMOOTH;
}

static void
terrain_readout(
    struct EditorUi* ui,
    struct TerrainPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    const struct Editor_Brush* brush = &ui->tools.brush;

    int const path = ui->tools.tool == EDITOR_TOOL_PAINT_PATH;
    int const scatter = ui->tools.tool == EDITOR_TOOL_SCATTER;
    int const plateau = ui->tools.tool == EDITOR_TOOL_PLATEAU;
    int const underlay = ui->tools.tool == EDITOR_TOOL_PAINT_UNDERLAY;
    int const height = ui->tools.tool == EDITOR_TOOL_HEIGHT;

    if( height && ui->tools.height_valid )
        snprintf(line, sizeof(line), "%s  r%d %s  grid at %d", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff], -ui->tools.height_target / EDITOR_HEIGHT_UNIT);
    else if( height )
        snprintf(line, sizeof(line), "%s  r%d %s  grid on the ground", Editor_ToolName(ui->tools.tool),
            brush->radius, k_falloff_names[brush->falloff]);
    else if( scatter )
        snprintf(line, sizeof(line), "%s  r%d %s %d%%", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff], ui->tools.scatter_density);
    else if( underlay && ui->tools.paint_mix < 100 )
        snprintf(line, sizeof(line), "%s  r%d %s mix %d%%", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff], ui->tools.paint_mix);
    else if( plateau )
        snprintf(line, sizeof(line), "%s  r%d skirt %d", Editor_ToolName(ui->tools.tool), brush->radius,
            ui->tools.plateau_skirt);
    else if( Editor_ToolIsBrush(ui->tools.tool) )
        snprintf(line, sizeof(line), "%s  r%d %s", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff]);
    else if( path )
        snprintf(line, sizeof(line), "%s  %s %d.%d wide", Editor_ToolName(ui->tools.tool),
            k_path_names[ui->tools.path_style == EDITOR_PATH_PAVED], ui->tools.path_width_halves / 2,
            ui->tools.path_width_halves % 2 ? 5 : 0);
    else
        snprintf(line, sizeof(line), "%s (not a brush)", Editor_ToolName(ui->tools.tool));
    ToriRSChrome_SetText(ui->chrome, self->row_tool, line);
    /* The path's section only while the Path tool is up, and then in the
     * brush's place (a path has no radius, falloff, step or strength), so it
     * is at the top of the panel rather than under the fold. */
    ToriRSChrome_SetHidden(ui->chrome, self->slider_radius, path);
    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_falloff[i], path);
    /* Scatter and Plateau keep the radius (and Scatter the falloff: the
     * density fades with it) and show their own rows instead of the step
     * and strength, which neither reads. */
    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_falloff[i], path || plateau);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_step, path || scatter || plateau);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_strength, path || scatter || plateau);
    ToriRSChrome_SetHidden(ui->chrome, self->label_scatter, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_scatter_density, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->check_scatter_rotate, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->button_scatter_seed, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_plateau_skirt, !plateau);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_mix, !underlay);
    ToriRSChrome_SetHidden(ui->chrome, self->label_height, !height);
    for( int i = 0; i < EDITOR_HEIGHT_MODE_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_height_mode[i], !height);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_height, !height);
    ToriRSChrome_SetHidden(ui->chrome, self->button_height_follow, !height);
    ToriRSChrome_SetHidden(ui->chrome, self->check_height_grid, !height);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_height_fill, !height || !ui->tools.height_grid);
    ToriRSChrome_SetHidden(ui->chrome, self->label_path, !path);
    for( int i = 0; i < EDITOR_PATH_STYLE_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_path[i], !path);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_path_width, !path);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_path_rough, !path || ui->tools.path_style == EDITOR_PATH_PAVED);
    ToriRSChrome_SetHidden(ui->chrome, self->button_path_seed, !path || ui->tools.path_style == EDITOR_PATH_PAVED);
    self->shown_tool = ui->tools.tool;
}

static void
terrain_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct TerrainPanel* self = opaque;

    if( self->shown_tool != ui->tools.tool )
        terrain_readout(ui, self);
    /* The level follows STATE_EDIT_LEVEL from other windows and the menu
     * bar's Level row; the view cap follows the level (edp_level_set). */
    if( self->shown_level != ui->tools.edit_level )
    {
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_level[ui->tools.edit_level + 1]);
        self->shown_level = ui->tools.edit_level;
    }
    if( self->shown_vis != ui->tools.vis_level )
    {
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_vis[ui->tools.vis_level + 1]);
        self->shown_vis = ui->tools.vis_level;
    }
    if( self->hint_serial != ui->hotkeys.serial )
        terrain_hints(ui, self);
    if( memcmp(&self->shown_brush, &ui->tools.brush, sizeof(self->shown_brush)) != 0 )
    {
        ToriRSChrome_SliderSet(ui->chrome, self->slider_radius, ui->tools.brush.radius);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_step, ui->tools.brush.step);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_strength, ui->tools.brush.strength);
        for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
            if( i == (int)ui->tools.brush.falloff )
                ToriRSChrome_RadioSelect(ui->chrome, self->radio_falloff[i]);
        self->shown_brush = ui->tools.brush;
        terrain_readout(ui, self);
    }
    if( self->shown_path_style != ui->tools.path_style || self->shown_path_width != ui->tools.path_width_halves
        || self->shown_path_rough != ui->tools.path_roughness )
    {
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_path[ui->tools.path_style == EDITOR_PATH_PAVED]);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_path_width, ui->tools.path_width_halves);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_path_rough, ui->tools.path_roughness);
        self->shown_path_style = ui->tools.path_style;
        self->shown_path_width = ui->tools.path_width_halves;
        self->shown_path_rough = ui->tools.path_roughness;
        terrain_readout(ui, self);
    }
    if( self->shown_scatter_density != ui->tools.scatter_density
        || self->shown_scatter_rotate != ui->tools.scatter_rotate
        || self->shown_plateau_skirt != ui->tools.plateau_skirt || self->shown_mix != ui->tools.paint_mix )
    {
        ToriRSChrome_SliderSet(ui->chrome, self->slider_scatter_density, ui->tools.scatter_density);
        ToriRSChrome_SetChecked(ui->chrome, self->check_scatter_rotate, ui->tools.scatter_rotate ? 1 : 0);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_plateau_skirt, ui->tools.plateau_skirt);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_mix, ui->tools.paint_mix);
        self->shown_scatter_density = ui->tools.scatter_density;
        self->shown_scatter_rotate = ui->tools.scatter_rotate;
        self->shown_plateau_skirt = ui->tools.plateau_skirt;
        self->shown_mix = ui->tools.paint_mix;
        terrain_readout(ui, self);
    }
    /* The wheel, Ctrl/Cmd+click and other windows move the Height state. */
    if( self->shown_height_mode != ui->tools.height_mode || self->shown_height_valid != ui->tools.height_valid
        || self->shown_height_target != ui->tools.height_target || self->shown_height_grid != ui->tools.height_grid
        || self->shown_height_fill != ui->tools.height_grid_opacity )
    {
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_height_mode[ui->tools.height_mode]);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_height, -ui->tools.height_target / EDITOR_HEIGHT_UNIT);
        ToriRSChrome_SetChecked(ui->chrome, self->check_height_grid, ui->tools.height_grid ? 1 : 0);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_height_fill, ui->tools.height_grid_opacity);
        self->shown_height_mode = ui->tools.height_mode;
        self->shown_height_valid = ui->tools.height_valid;
        self->shown_height_target = ui->tools.height_target;
        self->shown_height_grid = ui->tools.height_grid;
        self->shown_height_fill = ui->tools.height_grid_opacity;
        terrain_readout(ui, self);
    }
    if( self->shown_flags != ui->tools.flags_value )
    {
        for( int i = 0; i < 4; i++ )
            ToriRSChrome_SetChecked(ui->chrome, self->check_flags[i], (ui->tools.flags_value >> i) & 1);
        self->shown_flags = ui->tools.flags_value;
    }
}

static int
terrain_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct TerrainPanel* self = opaque;
    struct EditorUiTools* tools = &ui->tools;

    if( widget == self->slider_radius )
        self->shown_brush.radius = tools->brush.radius = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_step )
        self->shown_brush.step = tools->brush.step = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_strength )
        self->shown_brush.strength = tools->brush.strength = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_path_width )
        self->shown_path_width = tools->path_width_halves = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_path_rough )
        self->shown_path_rough = tools->path_roughness = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->button_path_seed )
    {
        tools->path_seed = tools->path_seed * 1664525u + 1013904223u;
        Editor_UiStatus(ui, "Path: new edges for the next dirt path");
        return 1;
    }
    else if( widget == self->slider_scatter_density )
        self->shown_scatter_density = tools->scatter_density = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->check_scatter_rotate )
    {
        self->shown_scatter_rotate = tools->scatter_rotate = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
        return 1;
    }
    else if( widget == self->button_scatter_seed )
    {
        tools->scatter_seed = tools->scatter_seed * 1664525u + 1013904223u;
        Editor_UiStatus(ui, "Scatter: another spread for the next stroke");
        return 1;
    }
    else if( widget == self->slider_plateau_skirt )
        self->shown_plateau_skirt = tools->plateau_skirt = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_mix )
    {
        self->shown_mix = tools->paint_mix = ToriRSChrome_SliderValue(ui->chrome, widget);
        terrain_readout(ui, self);
        return 1;
    }
    else if( widget == self->slider_height )
    {
        Editor_UiSetHeightTarget(ui, -ToriRSChrome_SliderValue(ui->chrome, widget) * EDITOR_HEIGHT_UNIT);
        return 1;
    }
    else if( widget == self->button_height_follow )
    {
        tools->height_valid = 0;
        Editor_UiStatus(ui, "Height: the grid stands on the ground under the pointer until set");
        return 1;
    }
    else if( widget == self->check_height_grid )
    {
        tools->height_grid = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
        return 1;
    }
    else if( widget == self->slider_height_fill )
    {
        tools->height_grid_opacity = ToriRSChrome_SliderValue(ui->chrome, widget);
        return 1;
    }
    else if( widget == self->check_solo )
    {
        tools->vis_solo = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
        return 1;
    }
    else
    {
        for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
            if( widget == self->radio_falloff[i] )
            {
                self->shown_brush.falloff = tools->brush.falloff = (enum Editor_BrushFalloff)i;
                terrain_readout(ui, self);
                return 1;
            }
        for( int i = 0; i < EDITOR_HEIGHT_MODE_COUNT; i++ )
            if( widget == self->radio_height_mode[i] )
            {
                tools->height_mode = i;
                return 1;
            }
        for( int i = 0; i < EDITOR_PATH_STYLE_COUNT; i++ )
            if( widget == self->radio_path[i] )
            {
                self->shown_path_style = tools->path_style = i;
                terrain_readout(ui, self);
                return 1;
            }
        for( int i = 0; i < 5; i++ )
        {
            if( widget == self->radio_level[i] )
            {
                edp_level_set(ui, i - 1);
                self->shown_level = tools->edit_level;
                return 1;
            }
            if( widget == self->radio_vis[i] )
            {
                tools->vis_level = i - 1;
                self->shown_vis = tools->vis_level;
                return 1;
            }
        }
        for( int i = 0; i < 4; i++ )
            if( widget == self->check_flags[i] )
            {
                if( ToriRSChrome_Checked(ui->chrome, widget) )
                    tools->flags_value |= 1 << i;
                else
                    tools->flags_value &= ~(1 << i);
                self->shown_flags = tools->flags_value;
                return 1;
            }
        return 0;
    }
    terrain_readout(ui, self);
    return 1;
}

static void
terrain_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Terrain_Register(struct EditorUi* ui)
{
    struct TerrainPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown_tool = EDITOR_TOOL_COUNT;
    self->shown_level = -2;
    self->shown_vis = -2;
    self->shown_brush = ui->tools.brush;
    self->shown_flags = ui->tools.flags_value;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y),
        edp_px(ui, EDP_COL_W), "Terrain");
    assert(self->panel >= 0);
    self->row_tool = ToriRSChrome_Label(chrome, self->panel, "");
    self->slider_radius = ToriRSChrome_Slider(
        chrome, self->panel, "Radius", 0, EDITOR_BRUSH_RADIUS_MAX, ui->tools.brush.radius, 1);

    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        self->radio_falloff[i] = ToriRSChrome_Radio(
            chrome, self->panel, k_falloff_names[i], TERRAIN_GROUP_FALLOFF,
            i == (int)ui->tools.brush.falloff);
    self->slider_step =
        ToriRSChrome_Slider(chrome, self->panel, "Step", 1, 16, ui->tools.brush.step, 1);

    self->slider_strength =
        ToriRSChrome_Slider(chrome, self->panel, "Strength", 0, 100, ui->tools.brush.strength, 5);
    terrain_hints(ui, self);

    /* The Height brush (3): hidden until armed (terrain_readout). */
    self->label_height = ToriRSChrome_Label(chrome, self->panel, "Height brush");
    for( int i = 0; i < EDITOR_HEIGHT_MODE_COUNT; i++ )
        self->radio_height_mode[i] = ToriRSChrome_Radio(
            chrome, self->panel, k_height_mode_names[i], TERRAIN_GROUP_HEIGHT, i == ui->tools.height_mode);
    ToriRSChrome_SetTooltip(chrome, self->radio_height_mode[1], "Fill hollows up to the grid, leave rises above it");
    ToriRSChrome_SetTooltip(chrome, self->radio_height_mode[2], "Cut rises down to the grid, leave hollows below it");
    self->slider_height =
        ToriRSChrome_Slider(chrome, self->panel, "Grid height", 0, TERRAIN_HEIGHT_UNITS_MAX, 0, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_height,
        "Units of 8; Ctrl/Cmd+wheel moves it, +Shift the grid only");
    self->button_height_follow = ToriRSChrome_Button(chrome, self->panel, "Grid follows the ground");
    ToriRSChrome_SetTooltip(chrome, self->button_height_follow, "Unset the grid: it stands on the hovered tile again");
    self->check_height_grid = ToriRSChrome_Checkbox(chrome, self->panel, "Show grid", ui->tools.height_grid ? 1 : 0);
    self->slider_height_fill =
        ToriRSChrome_Slider(chrome, self->panel, "Grid fill (%)", 0, 100, ui->tools.height_grid_opacity, 5);
    ToriRSChrome_SetTooltip(chrome, self->slider_height_fill, "How solid the grid's fill is; its lines always show");
    self->shown_height_mode = -1;
    ToriRSChrome_SetHidden(chrome, self->label_height, 1);
    for( int i = 0; i < EDITOR_HEIGHT_MODE_COUNT; i++ )
        ToriRSChrome_SetHidden(chrome, self->radio_height_mode[i], 1);
    ToriRSChrome_SetHidden(chrome, self->slider_height, 1);
    ToriRSChrome_SetHidden(chrome, self->button_height_follow, 1);
    ToriRSChrome_SetHidden(chrome, self->check_height_grid, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_height_fill, 1);

    /* The Path tool (H): hidden until it is armed (terrain_readout). */
    self->label_path = ToriRSChrome_Label(chrome, self->panel, "Path");
    for( int i = 0; i < EDITOR_PATH_STYLE_COUNT; i++ )
        self->radio_path[i] = ToriRSChrome_Radio(chrome, self->panel, k_path_names[i], TERRAIN_GROUP_PATH,
            i == (ui->tools.path_style == EDITOR_PATH_PAVED));
    ToriRSChrome_SetTooltip(chrome, self->radio_path[0], "Edges that wander, the width breathing (the content's tracks)");
    ToriRSChrome_SetTooltip(chrome, self->radio_path[1], "Straight runs and 45-degree bends, a steady width (roads)");
    self->slider_path_width = ToriRSChrome_Slider(chrome, self->panel, "Width (half tiles)", 1,
        (int)(EDITOR_PATH_WIDTH_MAX * 2), ui->tools.path_width_halves, 1);
    self->slider_path_rough =
        ToriRSChrome_Slider(chrome, self->panel, "Roughness", 0, 100, ui->tools.path_roughness, 5);
    self->button_path_seed = ToriRSChrome_Button(chrome, self->panel, "New edges");
    ToriRSChrome_SetTooltip(chrome, self->button_path_seed, "Another wander for the next dirt path");
    self->shown_path_style = ui->tools.path_style;
    self->shown_path_width = ui->tools.path_width_halves;
    self->shown_path_rough = ui->tools.path_roughness;
    ToriRSChrome_SetHidden(chrome, self->label_path, 1);
    for( int i = 0; i < EDITOR_PATH_STYLE_COUNT; i++ )
        ToriRSChrome_SetHidden(chrome, self->radio_path[i], 1);
    ToriRSChrome_SetHidden(chrome, self->slider_path_width, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_path_rough, 1);
    ToriRSChrome_SetHidden(chrome, self->button_path_seed, 1);
    /* The Scatter tool (B) and the Plateau tool (V): hidden until armed. */
    self->label_scatter = ToriRSChrome_Label(chrome, self->panel, "Scatter");
    self->slider_scatter_density =
        ToriRSChrome_Slider(chrome, self->panel, "Density (%)", 1, 100, ui->tools.scatter_density, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_scatter_density,
        "Share of free tiles that take a loc, fading with the falloff");
    self->check_scatter_rotate =
        ToriRSChrome_Checkbox(chrome, self->panel, "Random rotation", ui->tools.scatter_rotate ? 1 : 0);
    self->button_scatter_seed = ToriRSChrome_Button(chrome, self->panel, "New seed");
    ToriRSChrome_SetTooltip(chrome, self->button_scatter_seed, "Another spread for the next stroke");
    self->slider_plateau_skirt = ToriRSChrome_Slider(
        chrome, self->panel, "Skirt (tiles)", 0, EDITOR_PLATEAU_SKIRT_MAX, ui->tools.plateau_skirt, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_plateau_skirt,
        "Tiles around the flat top that ease to the ground (Rect too)");
    /* Paint underlay (7): its mix, hidden until armed. */
    self->slider_mix = ToriRSChrome_Slider(chrome, self->panel, "Mix (%)", 10, 100, ui->tools.paint_mix, 10);
    ToriRSChrome_SetTooltip(chrome, self->slider_mix, "Share of tiles painted under 100: a dither the ground blends");
    self->shown_scatter_density = ui->tools.scatter_density;
    self->shown_scatter_rotate = ui->tools.scatter_rotate;
    self->shown_plateau_skirt = ui->tools.plateau_skirt;
    self->shown_mix = ui->tools.paint_mix;
    ToriRSChrome_SetHidden(chrome, self->slider_mix, 1);
    ToriRSChrome_SetHidden(chrome, self->label_scatter, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_scatter_density, 1);
    ToriRSChrome_SetHidden(chrome, self->check_scatter_rotate, 1);
    ToriRSChrome_SetHidden(chrome, self->button_scatter_seed, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_plateau_skirt, 1);

    ToriRSChrome_Separator(chrome, self->panel);
    ToriRSChrome_Label(chrome, self->panel, "Flags brush");
    for( int i = 0; i < 4; i++ )
        self->check_flags[i] = ToriRSChrome_Checkbox(chrome, self->panel, k_flag_names[i], 0);

    ToriRSChrome_Separator(chrome, self->panel);
    ToriRSChrome_Label(chrome, self->panel, "Edit level");
    for( int i = 0; i < 5; i++ )
        self->radio_level[i] =
            ToriRSChrome_Radio(chrome, self->panel, k_level_names[i], TERRAIN_GROUP_LEVEL, i == 0);
    ToriRSChrome_Label(chrome, self->panel, "View levels");
    for( int i = 0; i < 5; i++ )
        self->radio_vis[i] =
            ToriRSChrome_Radio(chrome, self->panel, k_vis_names[i], TERRAIN_GROUP_VIS, i == 0);
    self->check_solo = ToriRSChrome_Checkbox(chrome, self->panel, "only that level", 0);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "terrain";
    desc.title = "Terrain";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = terrain_tick;
    desc.activate = terrain_activate;
    desc.destroy = terrain_destroy;
    Editor_UiAddPanel(ui, &desc);
}
