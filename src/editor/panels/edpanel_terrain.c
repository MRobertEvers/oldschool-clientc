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
 * fill is. Its first row says the one thing a first drag needs: the grid
 * starts at the ground, so a drag moves nothing until Ctrl+wheel has moved
 * the grid (T9, 2026-11-09; the World says the same when a drag changed
 * nothing).
 *
 * "Tile grid" under the view's levels is the World's ground grid and square
 * borders (edp_tile_grid_set): automatic on a square with no loc yet, on, or
 * off.
 *
 * WHAT THE STROKE LAYS (2026-11-11). The brush is set here and the colour in
 * the Palette, the other tab of the same group: with Terrain in front
 * nothing on screen said what a stroke would paint. The second row does --
 * "Paints underlay 48 grass", "Lays overlay 14 dirt track" -- for every tool
 * that reads a palette pick, in the Palette's own words
 * (Editor_Palette_Describe).
 *
 * The Path tool arms with an overlay to lay: when none is picked, the common
 * floor of its style (Editor_Palette_PathOverlay), said in the status bar,
 * and changing the style changes a default with it.
 *
 * Paint underlay's section says the one thing its strokes hide: the client
 * averages underlays over five tiles, so a thin stroke fades away.
 *
 * The flags brush's four boxes show while Paint flags is armed, like every
 * other tool's rows; they were four rows of every tool's page.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "editor/editor_region.h"

#include <stdio.h>
#include <string.h>

enum
{
    TERRAIN_GROUP_FALLOFF = 1,
    TERRAIN_GROUP_LEVEL = 2,
    TERRAIN_GROUP_VIS = 3,
    TERRAIN_GROUP_PATH = 4,
    TERRAIN_GROUP_HEIGHT = 5,
    TERRAIN_GROUP_REGION_TARGET = 6,
    TERRAIN_GROUP_REGION_SET = 7,
    TERRAIN_GROUP_PATH_HEIGHT = 8,
};

/** The armed path style as an index into k_path_names. */
static int
terrain_path_style(const struct EditorUi* ui)
{
    return ui->tools.path_style >= 0 && ui->tools.path_style < EDITOR_PATH_STYLE_COUNT ? ui->tools.path_style : 0;
}

/** The grid slider's top: EDITOR_HEIGHT_TARGET_MIN in height units. */
#define TERRAIN_HEIGHT_UNITS_MAX (-EDITOR_HEIGHT_TARGET_MIN / EDITOR_HEIGHT_UNIT)

static const char* const k_falloff_names[EDITOR_BRUSH_FALLOFF_COUNT] = { "linear", "smooth", "flat" };
static const char* const k_level_names[5] = { "auto", "0", "1", "2", "3" };
static const char* const k_vis_names[5] = { "all", "0", "1", "2", "3" };
static const char* const k_path_names[EDITOR_PATH_STYLE_COUNT] = { "dirt", "pavement", "river" };
/** The styles as a sentence says them: "a dirt path lays ...". */
static const char* const k_path_nouns[EDITOR_PATH_STYLE_COUNT] = { "A dirt path", "A pavement", "A river" };
static const char* const k_path_height_names[EDITOR_PATH_HEIGHT_MODE_COUNT] = { "leave the ground", "set to",
                                                                               "lower by", "raise by" };
static const char* const k_height_mode_names[EDITOR_HEIGHT_MODE_COUNT] = { "up and down", "raise only",
                                                                         "lower only" };
static const char* const k_region_target_names[2] = { "overlay", "underlay" };
static const char* const k_region_set_names[EDITOR_REGION_SET_COUNT] = { "natural", "paved", "all twelve" };
static const char* const k_flag_names[4] = { "block", "link below (bridge)", "remove roof",
                                             "vis below" };
static const char* const k_tile_grid_names[EDITOR_TILE_GRID_MODE_COUNT] = { "auto: squares with no locs", "on",
                                                                           "off" };

struct TerrainPanel
{
    int panel;
    int row_tool;
    /** What the armed tool lays, by the Palette's names; and the picks and
     *  target it was last written from. */
    int row_paints;
    int shown_paints[5];
    int slider_radius;
    int slider_step;
    int slider_strength;
    int radio_falloff[EDITOR_BRUSH_FALLOFF_COUNT];
    int radio_level[5];
    int radio_vis[5];
    int check_solo;
    int label_flags;
    int check_flags[4];
    /** Paint underlay's note on the client's five-tile blend. */
    int hint_underlay;
    /** The Path tool's section (shown while it is armed): the style, the
     *  width in half tiles, dirt's roughness, New edges (another seed). */
    int label_path;
    int radio_path[EDITOR_PATH_STYLE_COUNT];
    /** "Use <the style's floor>", while the Palette's overlay is another. */
    int button_path_floor;
    int slider_path_width;
    int slider_path_rough;
    int button_path_seed;
    /** What the path does to the ground under it, and by how much. */
    int radio_path_height[EDITOR_PATH_HEIGHT_MODE_COUNT];
    int slider_path_height;
    int shown_path_height_mode;
    int shown_path_height_units;
    int shown_path_style;
    int shown_path_width;
    int shown_path_rough;
    /** The Scatter tool's section (shown while it is armed, under the
     *  radius and falloff): the density, random rotation, New seed. */
    int label_scatter;
    int slider_scatter_density;
    int check_scatter_rotate;
    int check_scatter_bare;
    int button_scatter_seed;
    int shown_scatter_density;
    int shown_scatter_rotate;
    int shown_scatter_bare;
    /** The Plateau tool's skirt (shown while it is armed). */
    int slider_plateau_skirt;
    int shown_plateau_skirt;
    /** Paint underlay's mix (shown while it is armed): the share of the
     *  brush's tiles painted, the dither the client blends. */
    int slider_mix;
    int shown_mix;
    /** Roughen's relief (shown while it is armed): the hills' size in tiles
     *  and how much finer bumps ride on them. */
    int slider_noise_scale;
    int slider_noise_detail;
    int shown_noise_scale;
    int shown_noise_detail;
    /** The World's tile grid and square borders (EditorUi::tile_grid). */
    int dropdown_tile_grid;
    int shown_tile_grid;
    /** The Height brush's section (shown while it is armed). */
    int label_height;
    /** What a first drag needs to know: the grid starts at the ground. */
    int hint_height;
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
    /** The Region tools' section (shown while the Region brush, the Lasso
     *  or Tidy is armed): the target, the brush's radius in half tiles,
     *  hardness, half-tile snap, the shape set, the lasso's corner
     *  smoothing, Tidy's smoothing, the underlay rim's feather, New dither;
     *  and the values they were last set from. */
    int label_region;
    int radio_region_target[2];
    int slider_region_radius;
    int slider_region_hardness;
    int check_region_snap;
    int radio_region_set[EDITOR_REGION_SET_COUNT];
    int slider_region_lasso_smooth;
    int slider_region_tidy_smooth;
    int slider_region_feather;
    int button_region_seed;
    int shown_region[8];
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
    terrain_slider_chords(ui, self->slider_step, "Step", "Height: units per wheel notch; Roughen: a hill's height",
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

/** The row under the readout: what the armed tool lays, in the Palette's
 *  words; hidden for a tool that reads no palette pick. */
static void
terrain_paints(
    struct EditorUi* ui,
    struct TerrainPanel* self)
{
    char line[TORIRS_CHROME_LABEL_MAX];
    char under[EDITOR_PALETTE_DESCRIBE_MAX];
    char over[EDITOR_PALETTE_DESCRIBE_MAX];
    enum Editor_Tool const tool = ui->tools.tool;
    int const region_under = (tool == EDITOR_TOOL_REGION || tool == EDITOR_TOOL_LASSO) && ui->tools.region_underlay;
    int hidden = 0;

    Editor_Palette_Describe(ui, 0, ui->tools.underlay_id, under, sizeof(under));
    Editor_Palette_Describe(ui, 1, ui->tools.overlay_none ? 0 : ui->tools.overlay_id, over, sizeof(over));
    if( tool == EDITOR_TOOL_PAINT_UNDERLAY || region_under )
    {
        if( ui->tools.underlay_id == 0 )
            snprintf(line, sizeof(line), "Paints no underlay: pick one in the Palette");
        else
            snprintf(line, sizeof(line), "Paints underlay %s", under);
    }
    else if( tool == EDITOR_TOOL_PAINT_OVERLAY || tool == EDITOR_TOOL_REGION || tool == EDITOR_TOOL_LASSO )
    {
        if( ui->tools.overlay_none )
            snprintf(line, sizeof(line), "Takes the overlay off (none is picked in the Palette)");
        else
            snprintf(line, sizeof(line), "Paints overlay %s", over);
    }
    else if( tool == EDITOR_TOOL_PAINT_PATH )
    {
        if( ui->tools.overlay_none )
            snprintf(line, sizeof(line), "Lays its style's common floor (none is picked)");
        else
            snprintf(line, sizeof(line), "Lays overlay %s", over);
    }
    else if( tool == EDITOR_TOOL_RECT_SELECT )
    {
        /* Fill lays the underlay when one is picked, and the overlay or its
         * removal; two names are cut to fit one row. */
        if( ui->tools.underlay_id == 0 && ui->tools.overlay_none )
            snprintf(line, sizeof(line), "Fill takes the overlay off (nothing is picked)");
        else if( ui->tools.underlay_id == 0 )
            snprintf(line, sizeof(line), "Fill lays overlay %s", over);
        else if( ui->tools.overlay_none )
            snprintf(line, sizeof(line), "Fill lays underlay %.24s, no overlay", under);
        else
            snprintf(line, sizeof(line), "Fill lays %.24s + %.24s", under, over);
    }
    else
        hidden = 1;
    if( !hidden )
        ToriRSChrome_SetText(ui->chrome, self->row_paints, line);
    ToriRSChrome_SetHidden(ui->chrome, self->row_paints, hidden);
    /* The Palette still holds the pond's water when the road comes next:
     * one click lays the style's own floor instead of a trip to the
     * Palette tab and back (2026-11-11). */
    {
        char floor[EDITOR_PALETTE_DESCRIBE_MAX];
        char caption[TORIRS_CHROME_LABEL_MAX];
        int const floor_id = tool == EDITOR_TOOL_PAINT_PATH && !ui->tools.overlay_none
                                 ? Editor_Palette_PathFloor(ui, terrain_path_style(ui), floor, sizeof(floor))
                                 : -1;
        int const offer = floor_id > 0 && floor_id != ui->tools.overlay_id;

        if( offer )
        {
            /* A button's caption is its text, not its label. */
            snprintf(caption, sizeof(caption), "Use %s", floor);
            ToriRSChrome_SetText(ui->chrome, self->button_path_floor, caption);
        }
        ToriRSChrome_SetHidden(ui->chrome, self->button_path_floor, !offer);
    }
    self->shown_paints[0] = ui->tools.underlay_id;
    self->shown_paints[1] = ui->tools.overlay_id;
    self->shown_paints[2] = ui->tools.overlay_none;
    self->shown_paints[3] = ui->tools.region_underlay;
    self->shown_paints[4] = (int)tool;
}

/** The Path tool with no overlay picked takes its style's common floor, and
 *  the bar says which (a pick of the user's is left alone). */
static void
terrain_path_default(struct EditorUi* ui)
{
    char name[EDITOR_PALETTE_DESCRIBE_MAX];
    int const style = terrain_path_style(ui);
    int const id = Editor_Palette_PathOverlay(ui, style, name, sizeof(name));

    if( id > 0 )
        Editor_UiStatus(ui, "%s lays overlay %d %s (none was picked; the Palette picks another)",
            k_path_nouns[style], id, name);
    else if( id == 0 && ui->tools.tool == EDITOR_TOOL_PAINT_PATH )
    {
        /* The Palette's pick, whatever it was picked for: said, so a road
         * is not laid in the water the pond was just painted with. */
        Editor_Palette_Describe(ui, 1, ui->tools.overlay_id, name, sizeof(name));
        Editor_UiStatus(ui, "%s lays overlay %s, the Palette's pick", k_path_nouns[style], name);
    }
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
    int const roughen = ui->tools.tool == EDITOR_TOOL_NOISE;
    int const height = ui->tools.tool == EDITOR_TOOL_HEIGHT;
    int const region_brush = ui->tools.tool == EDITOR_TOOL_REGION;
    int const lasso = ui->tools.tool == EDITOR_TOOL_LASSO;
    int const tidy = ui->tools.tool == EDITOR_TOOL_TIDY;
    int const region = region_brush || lasso || tidy;
    int const region_under = (region_brush || lasso) && ui->tools.region_underlay;

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
    else if( roughen && ui->tools.noise_scale < 2 )
        snprintf(line, sizeof(line), "%s  r%d %s, a draw per tile", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff]);
    else if( roughen )
        snprintf(line, sizeof(line), "%s  r%d %s, hills %d tiles, detail %d%%", Editor_ToolName(ui->tools.tool),
            brush->radius, k_falloff_names[brush->falloff], ui->tools.noise_scale, ui->tools.noise_detail);
    else if( Editor_ToolIsBrush(ui->tools.tool) )
        snprintf(line, sizeof(line), "%s  r%d %s", Editor_ToolName(ui->tools.tool), brush->radius,
            k_falloff_names[brush->falloff]);
    else if( path && ui->tools.path_height_mode > EDITOR_PATH_HEIGHT_OFF
             && ui->tools.path_height_mode < EDITOR_PATH_HEIGHT_MODE_COUNT )
        snprintf(line, sizeof(line), "%s  %s %d.%d wide, %s %d", Editor_ToolName(ui->tools.tool),
            k_path_names[terrain_path_style(ui)], ui->tools.path_width_halves / 2,
            ui->tools.path_width_halves % 2 ? 5 : 0, k_path_height_names[ui->tools.path_height_mode],
            ui->tools.path_height_units);
    else if( path )
        snprintf(line, sizeof(line), "%s  %s %d.%d wide", Editor_ToolName(ui->tools.tool),
            k_path_names[terrain_path_style(ui)], ui->tools.path_width_halves / 2,
            ui->tools.path_width_halves % 2 ? 5 : 0);
    else if( region_brush )
        snprintf(line, sizeof(line), "%s  r%d.%d %s, hardness %d%%", Editor_ToolName(ui->tools.tool),
            ui->tools.region_radius_halves / 2, ui->tools.region_radius_halves % 2 ? 5 : 0,
            k_region_target_names[ui->tools.region_underlay ? 1 : 0], ui->tools.region_hardness);
    else if( lasso )
        snprintf(line, sizeof(line), "%s  %s, hardness %d%%", Editor_ToolName(ui->tools.tool),
            k_region_target_names[ui->tools.region_underlay ? 1 : 0], ui->tools.region_hardness);
    else if( tidy )
        snprintf(line, sizeof(line), "%s  smooth %d.%d, hardness %d%%", Editor_ToolName(ui->tools.tool),
            ui->tools.region_tidy_halves / 2, ui->tools.region_tidy_halves % 2 ? 5 : 0, ui->tools.region_hardness);
    else
        snprintf(line, sizeof(line), "%s (not a brush)", Editor_ToolName(ui->tools.tool));
    ToriRSChrome_SetText(ui->chrome, self->row_tool, line);
    terrain_paints(ui, self);
    /* The flags brush's rows and Paint underlay's note, with their tools. */
    ToriRSChrome_SetHidden(ui->chrome, self->label_flags, ui->tools.tool != EDITOR_TOOL_PAINT_FLAGS);
    for( int i = 0; i < 4; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->check_flags[i], ui->tools.tool != EDITOR_TOOL_PAINT_FLAGS);
    ToriRSChrome_SetHidden(ui->chrome, self->hint_underlay, !underlay && !region_under);
    /* The path's section only while the Path tool is up, and then in the
     * brush's place (a path has no radius, falloff, step or strength), so it
     * is at the top of the panel rather than under the fold. */
    ToriRSChrome_SetHidden(ui->chrome, self->slider_radius, path || region);
    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_falloff[i], path);
    /* Scatter and Plateau keep the radius (and Scatter the falloff: the
     * density fades with it) and show their own rows instead of the step
     * and strength, which neither reads. */
    for( int i = 0; i < EDITOR_BRUSH_FALLOFF_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_falloff[i], path || plateau || region);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_step, path || scatter || plateau || region);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_strength, path || scatter || plateau || region);
    /* The Region section, in the brush's place: each row only for the tools
     * that read it. */
    ToriRSChrome_SetHidden(ui->chrome, self->label_region, !region);
    for( int i = 0; i < 2; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_region_target[i], !(region_brush || lasso));
    ToriRSChrome_SetHidden(ui->chrome, self->slider_region_radius, !region_brush);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_region_hardness, !region || region_under);
    ToriRSChrome_SetHidden(ui->chrome, self->check_region_snap, !(region_brush || lasso));
    for( int i = 0; i < EDITOR_REGION_SET_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_region_set[i], !region || region_under);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_region_lasso_smooth, !lasso);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_region_tidy_smooth, !tidy);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_region_feather, !region_under);
    ToriRSChrome_SetHidden(ui->chrome, self->button_region_seed, !region_under);
    ToriRSChrome_SetHidden(ui->chrome, self->label_scatter, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_scatter_density, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->check_scatter_rotate, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->check_scatter_bare, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->button_scatter_seed, !scatter);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_plateau_skirt, !plateau);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_mix, !underlay);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_noise_scale, !roughen);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_noise_detail, !roughen || ui->tools.noise_scale < 2);
    ToriRSChrome_SetHidden(ui->chrome, self->label_height, !height);
    ToriRSChrome_SetHidden(ui->chrome, self->hint_height, !height);
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
    for( int i = 0; i < EDITOR_PATH_HEIGHT_MODE_COUNT; i++ )
        ToriRSChrome_SetHidden(ui->chrome, self->radio_path_height[i], !path);
    ToriRSChrome_SetHidden(ui->chrome, self->slider_path_height, !path || ui->tools.path_height_mode == EDITOR_PATH_HEIGHT_OFF);
    self->shown_tool = ui->tools.tool;
}

static void
terrain_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct TerrainPanel* self = opaque;

    if( self->shown_tool != ui->tools.tool )
    {
        terrain_readout(ui, self);
        if( ui->tools.tool == EDITOR_TOOL_PAINT_PATH )
            terrain_path_default(ui);
    }
    /* A pick in the Palette, the eyedropper, another window. */
    if( self->shown_paints[0] != ui->tools.underlay_id || self->shown_paints[1] != ui->tools.overlay_id
        || self->shown_paints[2] != ui->tools.overlay_none || self->shown_paints[3] != ui->tools.region_underlay )
        terrain_paints(ui, self);
    /* A View row or a hotkey may have cycled it. */
    if( self->shown_tile_grid != ui->tile_grid )
    {
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_tile_grid, ui->tile_grid);
        self->shown_tile_grid = ui->tile_grid;
    }
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
        ToriRSChrome_RadioSelect(ui->chrome, self->radio_path[terrain_path_style(ui)]);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_path_width, ui->tools.path_width_halves);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_path_rough, ui->tools.path_roughness);
        self->shown_path_style = ui->tools.path_style;
        self->shown_path_width = ui->tools.path_width_halves;
        self->shown_path_rough = ui->tools.path_roughness;
        terrain_readout(ui, self);
    }
    /* Ctrl/Cmd+click under Path picks the height and the mode. */
    if( self->shown_path_height_mode != ui->tools.path_height_mode
        || self->shown_path_height_units != ui->tools.path_height_units )
    {
        if( ui->tools.path_height_mode >= 0 && ui->tools.path_height_mode < EDITOR_PATH_HEIGHT_MODE_COUNT )
            ToriRSChrome_RadioSelect(ui->chrome, self->radio_path_height[ui->tools.path_height_mode]);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_path_height, ui->tools.path_height_units);
        self->shown_path_height_mode = ui->tools.path_height_mode;
        self->shown_path_height_units = ui->tools.path_height_units;
        terrain_readout(ui, self);
    }
    if( self->shown_scatter_density != ui->tools.scatter_density
        || self->shown_scatter_rotate != ui->tools.scatter_rotate || self->shown_scatter_bare != ui->tools.scatter_bare
        || self->shown_plateau_skirt != ui->tools.plateau_skirt || self->shown_mix != ui->tools.paint_mix )
    {
        ToriRSChrome_SliderSet(ui->chrome, self->slider_scatter_density, ui->tools.scatter_density);
        ToriRSChrome_SetChecked(ui->chrome, self->check_scatter_rotate, ui->tools.scatter_rotate ? 1 : 0);
        ToriRSChrome_SetChecked(ui->chrome, self->check_scatter_bare, ui->tools.scatter_bare ? 1 : 0);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_plateau_skirt, ui->tools.plateau_skirt);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_mix, ui->tools.paint_mix);
        self->shown_scatter_density = ui->tools.scatter_density;
        self->shown_scatter_rotate = ui->tools.scatter_rotate;
        self->shown_scatter_bare = ui->tools.scatter_bare;
        self->shown_plateau_skirt = ui->tools.plateau_skirt;
        self->shown_mix = ui->tools.paint_mix;
        terrain_readout(ui, self);
    }
    if( self->shown_noise_scale != ui->tools.noise_scale || self->shown_noise_detail != ui->tools.noise_detail )
    {
        ToriRSChrome_SliderSet(ui->chrome, self->slider_noise_scale, ui->tools.noise_scale);
        ToriRSChrome_SliderSet(ui->chrome, self->slider_noise_detail, ui->tools.noise_detail);
        self->shown_noise_scale = ui->tools.noise_scale;
        self->shown_noise_detail = ui->tools.noise_detail;
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
    {
        int const now[8] = { ui->tools.region_radius_halves, ui->tools.region_hardness, ui->tools.region_snap,
            ui->tools.region_set, ui->tools.region_underlay, ui->tools.region_lasso_smooth,
            ui->tools.region_tidy_halves, ui->tools.region_feather };
        if( memcmp(now, self->shown_region, sizeof(now)) != 0 )
        {
            ToriRSChrome_SliderSet(ui->chrome, self->slider_region_radius, now[0]);
            ToriRSChrome_SliderSet(ui->chrome, self->slider_region_hardness, now[1]);
            ToriRSChrome_SetChecked(ui->chrome, self->check_region_snap, now[2] ? 1 : 0);
            if( now[3] >= 0 && now[3] < EDITOR_REGION_SET_COUNT )
                ToriRSChrome_RadioSelect(ui->chrome, self->radio_region_set[now[3]]);
            ToriRSChrome_RadioSelect(ui->chrome, self->radio_region_target[now[4] ? 1 : 0]);
            ToriRSChrome_SliderSet(ui->chrome, self->slider_region_lasso_smooth, now[5]);
            ToriRSChrome_SliderSet(ui->chrome, self->slider_region_tidy_smooth, now[6]);
            ToriRSChrome_SliderSet(ui->chrome, self->slider_region_feather, now[7]);
            memcpy(self->shown_region, now, sizeof(now));
            terrain_readout(ui, self);
        }
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
    else if( widget == self->slider_path_height )
        self->shown_path_height_units = tools->path_height_units = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->button_path_seed )
    {
        tools->path_seed = tools->path_seed * 1664525u + 1013904223u;
        Editor_UiStatus(ui, "Path: new edges for the next dirt path");
        return 1;
    }
    else if( widget == self->button_path_floor )
    {
        char name[EDITOR_PALETTE_DESCRIBE_MAX];
        int const style = terrain_path_style(ui);
        int const id = Editor_Palette_PathUseFloor(ui, style, name, sizeof(name));

        if( id > 0 )
            Editor_UiStatus(ui, "%s lays overlay %d %s now (picked in the Palette too)", k_path_nouns[style], id, name);
        else
            Editor_UiStatus(ui, "Path: this cache's usual floor for the style is still loading; try again");
        terrain_paints(ui, self);
        return 1;
    }
    else if( widget == self->slider_scatter_density )
        self->shown_scatter_density = tools->scatter_density = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->check_scatter_rotate )
    {
        self->shown_scatter_rotate = tools->scatter_rotate = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
        return 1;
    }
    else if( widget == self->check_scatter_bare )
    {
        self->shown_scatter_bare = tools->scatter_bare = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
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
    else if( widget == self->slider_noise_scale || widget == self->slider_noise_detail )
    {
        self->shown_noise_scale = tools->noise_scale = ToriRSChrome_SliderValue(ui->chrome, self->slider_noise_scale);
        self->shown_noise_detail = tools->noise_detail =
            ToriRSChrome_SliderValue(ui->chrome, self->slider_noise_detail);
        /* The readout, and Detail's row: a draw per tile has no hills for
         * detail to ride on. */
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
    else if( widget == self->dropdown_tile_grid )
    {
        int const mode = ToriRSChrome_DropdownSelected(ui->chrome, widget);
        if( mode >= 0 && mode < EDITOR_TILE_GRID_MODE_COUNT )
        {
            edp_tile_grid_set(ui, (enum Editor_TileGridMode)mode);
            self->shown_tile_grid = ui->tile_grid;
        }
        return 1;
    }
    else if( widget == self->slider_height_fill )
    {
        tools->height_grid_opacity = ToriRSChrome_SliderValue(ui->chrome, widget);
        return 1;
    }
    else if( widget == self->slider_region_radius )
        tools->region_radius_halves = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_region_hardness )
        tools->region_hardness = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->check_region_snap )
        tools->region_snap = ToriRSChrome_Checked(ui->chrome, widget) ? 1 : 0;
    else if( widget == self->slider_region_lasso_smooth )
        tools->region_lasso_smooth = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_region_tidy_smooth )
        tools->region_tidy_halves = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->slider_region_feather )
        tools->region_feather = ToriRSChrome_SliderValue(ui->chrome, widget);
    else if( widget == self->button_region_seed )
    {
        tools->region_seed = tools->region_seed * 1664525u + 1013904223u;
        Editor_UiStatus(ui, "Region: another dither for the next underlay stroke");
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
                /* Another mode starts the grid over: the next drag sets it
                 * one Step off the ground it lands on (up, or down in
                 * "lower only"). A grid kept from the last job is on the
                 * wrong side for the new one -- left at the hilltop, "lower
                 * only" over the pond below it moved nothing and said
                 * nothing (2026-11-11). */
                if( tools->height_mode != i )
                {
                    tools->height_mode = i;
                    tools->height_valid = 0;
                    Editor_UiStatus(ui, "Height, %s: the next drag %s the ground one Step (%d); Ctrl+wheel sets a grid",
                        k_height_mode_names[i], i == EDITOR_HEIGHT_MODE_LOWER ? "sinks" : "lifts",
                        tools->brush.step * EDITOR_HEIGHT_UNIT);
                }
                return 1;
            }
        for( int i = 0; i < 2; i++ )
            if( widget == self->radio_region_target[i] )
            {
                tools->region_underlay = i;
                return 1;
            }
        for( int i = 0; i < EDITOR_REGION_SET_COUNT; i++ )
            if( widget == self->radio_region_set[i] )
            {
                tools->region_set = i;
                return 1;
            }
        for( int i = 0; i < EDITOR_PATH_HEIGHT_MODE_COUNT; i++ )
            if( widget == self->radio_path_height[i] )
            {
                self->shown_path_height_mode = tools->path_height_mode = i;
                terrain_readout(ui, self);
                return 1;
            }
        for( int i = 0; i < EDITOR_PATH_STYLE_COUNT; i++ )
            if( widget == self->radio_path[i] )
            {
                self->shown_path_style = tools->path_style = i;
                /* A default overlay follows the style. */
                terrain_path_default(ui);
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
    self->row_paints = ToriRSChrome_Label(chrome, self->panel, "");
    ToriRSChrome_SetHidden(chrome, self->row_paints, 1);
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
    self->hint_height = ToriRSChrome_Paragraph(chrome, self->panel,
        "A drag pulls the ground to the grid. With no grid set, or after picking a mode, the first drag lifts "
        "the ground one Step (lower only: sinks it). Ctrl+wheel moves the grid.");
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
    /* The caption is wider than a button's column ("Grid follows t..."). */
    ToriRSChrome_ButtonSetFill(chrome, self->button_height_follow, 1);
    ToriRSChrome_SetTooltip(chrome, self->button_height_follow, "Unset the grid: it stands on the hovered tile again");
    self->check_height_grid = ToriRSChrome_Checkbox(chrome, self->panel, "Show grid", ui->tools.height_grid ? 1 : 0);
    self->slider_height_fill =
        ToriRSChrome_Slider(chrome, self->panel, "Grid fill (%)", 0, 100, ui->tools.height_grid_opacity, 5);
    ToriRSChrome_SetTooltip(chrome, self->slider_height_fill, "How solid the grid's fill is; its lines always show");
    self->shown_height_mode = -1;
    ToriRSChrome_SetHidden(chrome, self->label_height, 1);
    ToriRSChrome_SetHidden(chrome, self->hint_height, 1);
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
            i == terrain_path_style(ui));
    ToriRSChrome_SetTooltip(chrome, self->radio_path[0], "Edges that wander, the width breathing (the content's tracks)");
    ToriRSChrome_SetTooltip(chrome, self->radio_path[1], "Straight runs and 45-degree bends, a steady width (roads)");
    ToriRSChrome_SetTooltip(chrome, self->radio_path[2], "A slow meander, banks that swell (pick water; dig its bed)");
    /* "Use dirt track": shown while the Palette's overlay is another floor
     * (terrain_paints names the style's). */
    self->button_path_floor = ToriRSChrome_Button(chrome, self->panel, "Use the style's floor");
    ToriRSChrome_ButtonSetFill(chrome, self->button_path_floor, 1);
    ToriRSChrome_SetTooltip(chrome, self->button_path_floor, "Lay this style's usual floor, not the Palette's overlay");
    ToriRSChrome_SetHidden(chrome, self->button_path_floor, 1);
    self->slider_path_width = ToriRSChrome_Slider(chrome, self->panel, "Width (half tiles)", 1,
        (int)(EDITOR_PATH_WIDTH_MAX * 2), ui->tools.path_width_halves, 1);
    self->slider_path_rough =
        ToriRSChrome_Slider(chrome, self->panel, "Roughness", 0, 100, ui->tools.path_roughness, 5);
    self->button_path_seed = ToriRSChrome_Button(chrome, self->panel, "New edges");
    ToriRSChrome_SetTooltip(chrome, self->button_path_seed, "Another wander for the next dirt path or river");
    for( int i = 0; i < EDITOR_PATH_HEIGHT_MODE_COUNT; i++ )
        self->radio_path_height[i] = ToriRSChrome_Radio(chrome, self->panel, k_path_height_names[i],
            TERRAIN_GROUP_PATH_HEIGHT, i == ui->tools.path_height_mode);
    ToriRSChrome_SetTooltip(chrome, self->radio_path_height[1], "One height under the path; Ctrl/Cmd+click picks it");
    ToriRSChrome_SetTooltip(chrome, self->radio_path_height[2], "Dig the ground under the path: a river's bed");
    ToriRSChrome_SetTooltip(chrome, self->radio_path_height[3], "Lift the ground under the path: a causeway");
    self->slider_path_height = ToriRSChrome_Slider(
        chrome, self->panel, "Height (units of 8)", 0, TERRAIN_HEIGHT_UNITS_MAX, ui->tools.path_height_units, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_path_height, "Set to: above zero; lower or raise: by how much");
    self->shown_path_height_mode = ui->tools.path_height_mode;
    self->shown_path_height_units = ui->tools.path_height_units;
    for( int i = 0; i < EDITOR_PATH_HEIGHT_MODE_COUNT; i++ )
        ToriRSChrome_SetHidden(chrome, self->radio_path_height[i], 1);
    ToriRSChrome_SetHidden(chrome, self->slider_path_height, 1);
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
    self->check_scatter_bare =
        ToriRSChrome_Checkbox(chrome, self->panel, "Bare ground only", ui->tools.scatter_bare ? 1 : 0);
    ToriRSChrome_SetTooltip(chrome, self->check_scatter_bare,
        "Skip roads, floors, water and tiles that hold a loc: for grass, twigs and flowers");
    self->button_scatter_seed = ToriRSChrome_Button(chrome, self->panel, "New seed");
    ToriRSChrome_SetTooltip(chrome, self->button_scatter_seed, "Another spread for the next stroke");
    self->slider_plateau_skirt = ToriRSChrome_Slider(
        chrome, self->panel, "Skirt (tiles)", 0, EDITOR_PLATEAU_SKIRT_MAX, ui->tools.plateau_skirt, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_plateau_skirt,
        "Tiles around the flat top that ease to the ground (Rect too)");
    /* Paint underlay (7): its mix, hidden until armed. */
    self->slider_mix = ToriRSChrome_Slider(chrome, self->panel, "Mix (%)", 10, 100, ui->tools.paint_mix, 10);
    ToriRSChrome_SetTooltip(chrome, self->slider_mix, "Share of tiles painted; under 100 shows as a speckle of tiles");
    self->hint_underlay = ToriRSChrome_Paragraph(chrome, self->panel,
        "The ground blends underlays over 5 tiles: a stroke under 3 wide fades away. "
        "For a hard edge paint an overlay.");
    ToriRSChrome_SetHidden(chrome, self->hint_underlay, 1);
    self->shown_scatter_density = ui->tools.scatter_density;
    self->shown_scatter_rotate = ui->tools.scatter_rotate;
    self->shown_plateau_skirt = ui->tools.plateau_skirt;
    self->shown_mix = ui->tools.paint_mix;
    ToriRSChrome_SetHidden(chrome, self->slider_mix, 1);
    /* Roughen (U): its relief, hidden until armed. */
    self->slider_noise_scale = ToriRSChrome_Slider(
        chrome, self->panel, "Hills (tiles)", 1, EDITOR_ROUGHEN_SCALE_MAX, ui->tools.noise_scale, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_noise_scale, "How wide the relief's hills are; 1 is a draw per tile");
    self->slider_noise_detail =
        ToriRSChrome_Slider(chrome, self->panel, "Detail (%)", 0, 100, ui->tools.noise_detail, 10);
    ToriRSChrome_SetTooltip(chrome, self->slider_noise_detail, "Finer bumps on the hills: 0 rolling ground, 100 crags");
    self->shown_noise_scale = ui->tools.noise_scale;
    self->shown_noise_detail = ui->tools.noise_detail;
    ToriRSChrome_SetHidden(chrome, self->slider_noise_scale, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_noise_detail, 1);
    ToriRSChrome_SetHidden(chrome, self->label_scatter, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_scatter_density, 1);
    ToriRSChrome_SetHidden(chrome, self->check_scatter_rotate, 1);
    ToriRSChrome_SetHidden(chrome, self->button_scatter_seed, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_plateau_skirt, 1);

    /* The Region tools (O, J, Y): hidden until one is armed. */
    self->label_region = ToriRSChrome_Label(chrome, self->panel, "Region");
    for( int i = 0; i < 2; i++ )
        self->radio_region_target[i] = ToriRSChrome_Radio(chrome, self->panel, k_region_target_names[i],
            TERRAIN_GROUP_REGION_TARGET, i == (ui->tools.region_underlay ? 1 : 0));
    ToriRSChrome_SetTooltip(chrome, self->radio_region_target[0], "The palette's overlay, its edge cut to tile shapes");
    ToriRSChrome_SetTooltip(chrome, self->radio_region_target[1],
        "The palette's underlay, its rim dithered for the ground's blend");
    self->slider_region_radius = ToriRSChrome_Slider(chrome, self->panel, "Radius (half tiles)", 1,
        EDITOR_REGION_RADIUS_HALVES_MAX, ui->tools.region_radius_halves, 1);
    self->slider_region_hardness = ToriRSChrome_Slider(chrome, self->panel, "Hardness (%)", EDITOR_REGION_HARDNESS_MIN,
        EDITOR_REGION_HARDNESS_MAX, ui->tools.region_hardness, 10);
    ToriRSChrome_SetTooltip(chrome, self->slider_region_hardness,
        "Higher: shapes stay inside the line; lower: they spill over it");
    self->check_region_snap = ToriRSChrome_Checkbox(chrome, self->panel, "Half-tile snap", ui->tools.region_snap);
    ToriRSChrome_SetTooltip(chrome, self->check_region_snap,
        "Points on half tiles: edges land on tile edges and middles");
    for( int i = 0; i < EDITOR_REGION_SET_COUNT; i++ )
        self->radio_region_set[i] = ToriRSChrome_Radio(
            chrome, self->panel, k_region_set_names[i], TERRAIN_GROUP_REGION_SET, i == ui->tools.region_set);
    ToriRSChrome_SetTooltip(chrome, self->radio_region_set[0], "Every shape the content's tracks use");
    ToriRSChrome_SetTooltip(chrome, self->radio_region_set[1], "Straight edges and 45-degree corners only");
    ToriRSChrome_SetTooltip(chrome, self->radio_region_set[2], "All twelve shapes, the trapezium too");
    self->slider_region_lasso_smooth = ToriRSChrome_Slider(
        chrome, self->panel, "Corner smoothing (passes)", 0, 3, ui->tools.region_lasso_smooth, 1);
    self->slider_region_tidy_smooth =
        ToriRSChrome_Slider(chrome, self->panel, "Smooth (half tiles)", 0, 4, ui->tools.region_tidy_halves, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_region_tidy_smooth,
        "How far Tidy rounds steps: 1 a single step, 3 a whole bay");
    self->slider_region_feather =
        ToriRSChrome_Slider(chrome, self->panel, "Feather (tiles)", 0, 3, ui->tools.region_feather, 1);
    ToriRSChrome_SetTooltip(chrome, self->slider_region_feather, "How wide the underlay's dithered rim is");
    self->button_region_seed = ToriRSChrome_Button(chrome, self->panel, "New dither");
    ToriRSChrome_SetTooltip(chrome, self->button_region_seed, "Another dither for the next underlay stroke");
    self->shown_region[0] = -1;
    ToriRSChrome_SetHidden(chrome, self->label_region, 1);
    for( int i = 0; i < 2; i++ )
        ToriRSChrome_SetHidden(chrome, self->radio_region_target[i], 1);
    ToriRSChrome_SetHidden(chrome, self->slider_region_radius, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_region_hardness, 1);
    ToriRSChrome_SetHidden(chrome, self->check_region_snap, 1);
    for( int i = 0; i < EDITOR_REGION_SET_COUNT; i++ )
        ToriRSChrome_SetHidden(chrome, self->radio_region_set[i], 1);
    ToriRSChrome_SetHidden(chrome, self->slider_region_lasso_smooth, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_region_tidy_smooth, 1);
    ToriRSChrome_SetHidden(chrome, self->slider_region_feather, 1);
    ToriRSChrome_SetHidden(chrome, self->button_region_seed, 1);

    /* Paint flags (9): its four bits, hidden until armed. */
    self->label_flags = ToriRSChrome_Label(chrome, self->panel, "Flags brush");
    ToriRSChrome_SetHidden(chrome, self->label_flags, 1);
    for( int i = 0; i < 4; i++ )
    {
        self->check_flags[i] = ToriRSChrome_Checkbox(chrome, self->panel, k_flag_names[i], 0);
        ToriRSChrome_SetHidden(chrome, self->check_flags[i], 1);
    }

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
    self->dropdown_tile_grid = ToriRSChrome_Dropdown(chrome, self->panel, "Tile grid", k_tile_grid_names,
        EDITOR_TILE_GRID_MODE_COUNT, ui->tile_grid);
    ToriRSChrome_SetTooltip(chrome, self->dropdown_tile_grid,
        "Tiles near the pointer, each square's border, an arrow north");
    self->shown_tile_grid = ui->tile_grid;
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
