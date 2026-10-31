/*
 * The tool rail: a vertical TOOLBAR of the baked tool icons, one per
 * enum Editor_Tool. Exclusive: the armed tool's button is PRESSED, and it
 * follows TORIRSMAPED_STATE_TOOL, so a tool armed in another window of the
 * Client presses here too. A command strip (Save, the New square button)
 * hangs under the tools.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

static const int k_tool_icons[EDITOR_TOOL_COUNT] = {
    [EDITOR_TOOL_SELECT] = TORIRS_CHROME_SKIN_TOOL_SELECT,
    /* No rectangle glyph in the cache; the splitter's grip reads as a
     * marquee handle beside the select crosshair. */
    [EDITOR_TOOL_RECT_SELECT] = TORIRS_CHROME_SKIN_SPLITTER_GRIP,
    [EDITOR_TOOL_RAISE] = TORIRS_CHROME_SKIN_TOOL_RAISE,
    [EDITOR_TOOL_LOWER] = TORIRS_CHROME_SKIN_TOOL_LOWER,
    [EDITOR_TOOL_FLATTEN] = TORIRS_CHROME_SKIN_TOOL_FLATTEN,
    [EDITOR_TOOL_SMOOTH] = TORIRS_CHROME_SKIN_TOOL_SMOOTH,
    [EDITOR_TOOL_PAINT_UNDERLAY] = TORIRS_CHROME_SKIN_TOOL_PAINT_UNDERLAY,
    [EDITOR_TOOL_PAINT_OVERLAY] = TORIRS_CHROME_SKIN_TOOL_PAINT_OVERLAY,
    [EDITOR_TOOL_PAINT_FLAGS] = TORIRS_CHROME_SKIN_TOOL_FLAGS,
    [EDITOR_TOOL_LOC_PLACE] = TORIRS_CHROME_SKIN_TOOL_PLACE,
    [EDITOR_TOOL_LOC_MOVE] = TORIRS_CHROME_SKIN_TOOL_MOVE,
    [EDITOR_TOOL_LOC_ROTATE] = TORIRS_CHROME_SKIN_TOOL_ROTATE,
    [EDITOR_TOOL_LOC_DELETE] = TORIRS_CHROME_SKIN_TOOL_DELETE,
};

/* Short enough that "  [<chord>]" fits a tooltip's 63 bytes. */
static const char* const k_tool_tips[EDITOR_TOOL_COUNT] = {
    [EDITOR_TOOL_SELECT] = "Select: click a tile, shift-click locs",
    [EDITOR_TOOL_RECT_SELECT] = "Rectangle: copy, paste, fill, clear",
    [EDITOR_TOOL_RAISE] = "Raise terrain (brush)",
    [EDITOR_TOOL_LOWER] = "Lower terrain (brush)",
    [EDITOR_TOOL_FLATTEN] = "Flatten to the first tile's height",
    [EDITOR_TOOL_SMOOTH] = "Smooth toward the neighbours",
    [EDITOR_TOOL_PAINT_UNDERLAY] = "Paint the palette's underlay",
    [EDITOR_TOOL_PAINT_OVERLAY] = "Paint the palette's overlay and shape",
    [EDITOR_TOOL_PAINT_FLAGS] = "Paint the tile flags",
    [EDITOR_TOOL_LOC_PLACE] = "Place the catalog's pick",
    [EDITOR_TOOL_LOC_MOVE] = "Move the selected locs here",
    [EDITOR_TOOL_LOC_ROTATE] = "Rotate the clicked loc",
    [EDITOR_TOOL_LOC_DELETE] = "Delete the clicked loc",
};

struct ToolRailPanel
{
    int panel;
    int buttons[EDITOR_TOOL_COUNT];
    int button_save;
    int button_new;
    int button_squares;
    enum Editor_Tool shown;
    /** ui->hotkeys.serial the tips were labelled from. */
    uint32_t hint_serial;
};

/** Each tool's tip ends with its chord, from the hotkey table. */
static void
toolrail_hints(
    struct EditorUi* ui,
    struct ToolRailPanel* self)
{
    char tip[TORIRS_CHROME_LABEL_MAX];

    for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
    {
        edp_hotkey_tip(ui, (enum Editor_HotkeyAction)(EDITOR_HK_TOOL_SELECT + t), k_tool_tips[t], tip, (int)sizeof(tip));
        ToriRSChrome_SetTooltip(ui->chrome, self->buttons[t], tip);
    }
    edp_hotkey_tip(ui, EDITOR_HK_FILE_SAVE_ALL, "Save every changed document", tip, (int)sizeof(tip));
    ToriRSChrome_SetTooltip(ui->chrome, self->button_save, tip);
    edp_hotkey_tip(ui, EDITOR_HK_FILE_NEW, "New square: coordinates, storage", tip, (int)sizeof(tip));
    ToriRSChrome_SetTooltip(ui->chrome, self->button_new, tip);
    edp_hotkey_tip(ui, EDITOR_HK_VIEW_SQUARES, "Show / hide the square picker", tip, (int)sizeof(tip));
    ToriRSChrome_SetTooltip(ui->chrome, self->button_squares, tip);
    self->hint_serial = ui->hotkeys.serial;
}

static void
toolrail_press(
    struct EditorUi* ui,
    struct ToolRailPanel* self)
{
    for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
        ToriRSChrome_SetChecked(ui->chrome, self->buttons[t], t == (int)ui->tools.tool);
    self->shown = ui->tools.tool;
}

static void
toolrail_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct ToolRailPanel* self = opaque;
    if( self->shown != ui->tools.tool )
        toolrail_press(ui, self);
    if( self->hint_serial != ui->hotkeys.serial )
        toolrail_hints(ui, self);
}

static int
toolrail_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct ToolRailPanel* self = opaque;

    for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
    {
        if( widget != self->buttons[t] )
            continue;
        Editor_UiSetTool(ui, (enum Editor_Tool)t);
        toolrail_press(ui, self);
        return 1;
    }
    if( widget == self->button_save )
    {
        int const saved = ui->editor ? Editor_SaveAll(ui->editor) : -1;
        if( saved < 0 )
            Editor_UiLog(ui, "editor: read-only session, nothing saved");
        else
            Editor_UiLog(ui, "editor: saved %d document(s) as text", saved);
        return 1;
    }
    if( widget == self->button_new )
    {
        char key[EDITOR_DOC_KEY_MAX];
        int map_x = -1;
        int map_z = -1;
        (void)Editor_SquarePicker_EmptyCell(ui, &map_x, &map_z);
        Editor_NewSquare_Open(
            ui, EDITOR_NEW_SQUARE_NEW,
            Editor_SquarePicker_SelectionKey(ui, key, (int)sizeof(key)) ? key : NULL, map_x, map_z);
        return 1;
    }
    if( widget == self->button_squares )
    {
        struct EditorUiPanel* picker = Editor_UiFindPanel(ui, "squares");
        if( picker && picker->chrome_panel >= 0 )
            ToriRSChrome_PanelSetVisible(
                ui->chrome, picker->chrome_panel,
                !ui->chrome->panels[picker->chrome_panel].visible);
        return 1;
    }
    return 0;
}

static void
toolrail_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct ToolRailPanel* self = opaque;
    if( visible )
        toolrail_press(ui, self);
}

static void
toolrail_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_ToolRail_Register(struct EditorUi* ui)
{
    struct ToolRailPanel* self;
    struct EditorUiPanel desc;
    int icon_w = 0;
    int icon_h = 0;

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown = EDITOR_TOOL_COUNT;
    ToriRSChrome_SkinSlotSize(TORIRS_CHROME_SKIN_TOOL_SELECT, &icon_w, &icon_h);
    (void)icon_h;

    /* Fixed width of one button: the strip wraps into a column. One column
     * is the 32 px icon box plus the strip's padding (2 px gap + 1 px rule)
     * on each side; at exactly 32 the box is clipped to half an icon. */
    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_TOOLBAR, edp_px(ui, EDP_RAIL_X), edp_px(ui, EDP_TOP_Y),
        edp_px(ui, 38), "Tools");
    assert(self->panel >= 0);
    for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
    {
        self->buttons[t] =
            ToriRSChrome_IconButton(ui->chrome, self->panel, k_tool_icons[t], Editor_ToolName(t));
    }
    self->button_save = ToriRSChrome_IconButton(ui->chrome, self->panel, TORIRS_CHROME_SKIN_SAVE, "Save");
    /* The design's New button lives in the picker; it is here too because a
     * new square is the first thing an empty session wants. */
    self->button_new = ToriRSChrome_IconButton(ui->chrome, self->panel, TORIRS_CHROME_SKIN_BUILD, "New square");
    self->button_squares =
        ToriRSChrome_IconButton(ui->chrome, self->panel, TORIRS_CHROME_SKIN_SEARCH, "Squares");
    toolrail_hints(ui, self);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "toolrail";
    desc.title = "Tools";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = toolrail_tick;
    desc.activate = toolrail_activate;
    desc.set_visible = toolrail_set_visible;
    desc.destroy = toolrail_destroy;
    Editor_UiAddPanel(ui, &desc);
}
