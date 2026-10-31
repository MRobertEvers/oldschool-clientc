/*
 * Terrain: the brush's SLIDERs (radius, step, strength), its falloff and
 * the edit level as RADIO groups, the flags brush's four bits, and the view's
 * level cap (design #terrain). Every control writes the shared tool state
 * the moment it changes; the world click reads it on the next press.
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
};

static const char* const k_falloff_names[EDITOR_BRUSH_FALLOFF_COUNT] = { "linear", "smooth", "flat" };
static const char* const k_level_names[5] = { "auto", "0", "1", "2", "3" };
static const char* const k_vis_names[5] = { "all", "0", "1", "2", "3" };
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
    /** What the readout and the radios were last set from. */
    enum Editor_Tool shown_tool;
    int shown_level;
    int shown_vis;
};

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
    return tool == EDITOR_TOOL_RAISE || tool == EDITOR_TOOL_LOWER || tool == EDITOR_TOOL_SMOOTH;
}

static void
terrain_readout(
    struct EditorUi* ui,
    struct TerrainPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    const struct Editor_Brush* brush = &ui->tools.brush;

    if( Editor_ToolIsBrush(ui->tools.tool) )
        snprintf(line, sizeof(line), "%s  r%d %s", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff]);
    else
        snprintf(line, sizeof(line), "%s (not a brush)", Editor_ToolName(ui->tools.tool));
    ToriRSChrome_SetText(ui->chrome, self->row_tool, line);
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
        tools->brush.radius = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_step )
        tools->brush.step = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_strength )
        tools->brush.strength = ToriRSChrome_SliderValue(ui->chrome, widget);
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
                tools->brush.falloff = (enum Editor_BrushFalloff)i;
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

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X), edp_px(ui, EDP_TOP_Y),
        edp_px(ui, EDP_COL_W), "Terrain");
    assert(self->panel >= 0);
    self->row_tool = ToriRSChrome_Label(chrome, self->panel, "");
    self->slider_radius = ToriRSChrome_Slider(
        chrome, self->panel, "Radius", 0, EDITOR_BRUSH_RADIUS_MAX, ui->tools.brush.radius, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_radius, "Brush radius in tiles (0 = one tile)");
    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        self->radio_falloff[i] = ToriRSChrome_Radio(
            chrome, self->panel, k_falloff_names[i], TERRAIN_GROUP_FALLOFF,
            i == (int)ui->tools.brush.falloff);
    self->slider_step =
        ToriRSChrome_Slider(chrome, self->panel, "Step", 1, 16, ui->tools.brush.step, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_step, "Raise / lower: height steps per click (8 units)");
    self->slider_strength =
        ToriRSChrome_Slider(chrome, self->panel, "Strength", 0, 100, ui->tools.brush.strength, 5);
    ToriRSChrome_SetTooltip(chrome, self->slider_strength, "Smooth: how far toward the neighbours");

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
