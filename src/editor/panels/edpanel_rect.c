/*
 * The rectangle select's panel (design #terrain "Select rectangle"): what is
 * latched, and copy / paste / fill / clear on it. The rectangle itself is
 * dragged in the world with EDITOR_TOOL_RECT_SELECT (app_map_editor.c writes
 * tools.rect); the panel opens itself when that tool is armed. "Mark area from
 * rect" names it as an area (edpanel_areas.c).
 *
 * "with spawns" (on by default) is what lets an area be carried to a new map
 * with its npcs: Copy takes the spawns standing in the rectangle beside its
 * tiles and locs, Paste stands them again (edp_rect_copy, edpanel_actions.c).
 * The clipboard row counts what is held.
 *
 * "Save as stamp" (2026-11-11) names what stands in the rectangle as a stamp
 * (edp_stamp_save_rect): with "All levels" a whole building -- its walls, its
 * upper floors, its roof, its furniture, its floors and "remove roof" flags
 * -- where Copy and the Locs panel's Save as stamp take one level and what
 * is selected. The Stamps panel places it again.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "edpanel_ops_stamps.h"
#include "edpanels.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

struct RectPanel
{
    int panel;
    int row_rect;
    int row_clip;
    int check_spawns;
    int button_copy;
    int button_paste;
    int button_fill;
    int button_clear;
    int button_plateau;
    int button_mark_area;
    int input_stamp;
    int check_all_levels;
    int check_overwrite;
    int button_stamp;
    struct Editor_TileRect shown;
    int shown_clip_w;
    int shown_clip_h;
    int shown_clip_locs;
    int shown_clip_spawns;
    enum Editor_Tool shown_tool;
    /** The panel's visibility and ui->layout_runs as the last tick saw
     *  them: a panel that turned visible in a tick a layout ran in was the
     *  saved layout's doing (rect_tick). */
    int was_visible;
    unsigned seen_layout_runs;
    /** ui->hotkeys.serial the buttons' keycaps were labelled from. */
    uint32_t chord_serial;
};

/** Every button wears its Edit action's chord as a keycap (design section
 *  15 surface 3), from the hotkey table; again whenever its serial moves. */
static void
rect_chords(
    struct EditorUi* ui,
    struct RectPanel* self)
{
    edp_button_chord(ui, self->button_copy, EDITOR_HK_EDIT_COPY);
    edp_button_chord(ui, self->button_paste, EDITOR_HK_EDIT_PASTE);
    edp_button_chord(ui, self->button_fill, EDITOR_HK_EDIT_FILL);
    edp_button_chord(ui, self->button_clear, EDITOR_HK_EDIT_CLEAR);
    edp_button_chord(ui, self->button_mark_area, EDITOR_HK_EDIT_MARK_AREA);
    edp_button_chord(ui, self->button_stamp, EDITOR_HK_STAMP_SAVE_RECT);
    self->chord_serial = ui->hotkeys.serial;
}

static void
rect_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct RectPanel* self = opaque;
    char line[TORIRS_CHROME_LABEL_MAX];
    int clip_w = 0;
    int clip_h = 0;

    if( self->chord_serial != ui->hotkeys.serial )
        rect_chords(ui, self);
    /* A saved layout reopens the flyout where the last session left it open,
     * with Select armed: put it away, as the tool's going would have
     * (2026-11-11: it booted open over the Building panel and the Stamps
     * panel, all three stacked on one spot).
     * Shown by View between layouts, it is the user's and stays. */
    {
        int const visible = ui->chrome->panels[self->panel].visible;
        if( visible && !self->was_visible && self->seen_layout_runs != ui->layout_runs
            && ui->tools.tool != EDITOR_TOOL_RECT_SELECT && !ui->chrome->panels[self->panel].user_placed )
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        self->was_visible = ui->chrome->panels[self->panel].visible;
        self->seen_layout_runs = ui->layout_runs;
    }
    /* Arming the tool brings the panel up. Another tool's arming puts it
     * away again where it is still the flyout it opened as (2026-11-11: the
     * Rectangle, Building and Stamps flyouts open on one spot, and stood
     * three deep); a panel the user moved or docked is theirs and stays. */
    if( self->shown_tool != ui->tools.tool )
    {
        if( ui->tools.tool == EDITOR_TOOL_RECT_SELECT )
        {
            /* In front of a panel opened over the same spot since. */
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
            ToriRSChrome_PanelRaise(ui->chrome, self->panel);
        }
        else if( self->shown_tool == EDITOR_TOOL_RECT_SELECT && !ui->chrome->panels[self->panel].user_placed )
            ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
        self->shown_tool = ui->tools.tool;
    }

    if( memcmp(&self->shown, &ui->tools.rect, sizeof(self->shown)) != 0 )
    {
        struct Editor_TileRect r;
        if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
            snprintf(line, sizeof(line), "%d,%d .. %d,%d  level %d  (%dx%d)", r.x0, r.z0, r.x1, r.z1,
                r.level, r.x1 - r.x0 + 1, r.z1 - r.z0 + 1);
        else if( ui->tools.rect.valid )
            snprintf(line, sizeof(line), "too large: 64x64 tiles at most");
        else
            snprintf(line, sizeof(line), "drag a rectangle in the world");
        ToriRSChrome_SetText(ui->chrome, self->row_rect, line);
        self->shown = ui->tools.rect;
    }
    /* The box is the clipboard's switch (Ctrl+C and Ctrl+V read it too). */
    edp_rect_set_with_spawns(ToriRSChrome_Checked(ui->chrome, self->check_spawns));
    (void)edp_rect_clip_size(&clip_w, &clip_h);
    {
        int const locs = clip_w > 0 ? edp_stamp_clip_locs() : 0;
        int const spawns = clip_w > 0 ? edp_stamp_clip_spawns() : 0;
        if( clip_w != self->shown_clip_w || clip_h != self->shown_clip_h || locs != self->shown_clip_locs
            || spawns != self->shown_clip_spawns )
        {
            if( clip_w > 0 )
                snprintf(line, sizeof(line), "clipboard %dx%d, %d loc%s, %d spawn%s", clip_w, clip_h, locs,
                    locs == 1 ? "" : "s", spawns, spawns == 1 ? "" : "s");
            else
                snprintf(line, sizeof(line), "clipboard empty");
            ToriRSChrome_SetText(ui->chrome, self->row_clip, line);
            self->shown_clip_w = clip_w;
            self->shown_clip_h = clip_h;
            self->shown_clip_locs = locs;
            self->shown_clip_spawns = spawns;
        }
    }
}

static int
rect_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct RectPanel* self = opaque;

    if( widget == self->button_copy )
        (void)edp_rect_copy(ui);
    else if( widget == self->button_paste )
    {
        /* The button pastes at the rectangle's corner, so it needs one: with
         * none latched its fields name no tile (the hotkey pastes at the
         * pointer instead). */
        struct Editor_TileRect r;
        if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
            (void)edp_rect_paste(ui, r.x0, r.z0, r.level);
        else
            Editor_UiStatus(ui, "Paste refused: drag a rectangle first; the paste lands at its SW corner");
    }
    else if( widget == self->button_fill )
        (void)edp_rect_fill(ui);
    else if( widget == self->button_clear )
        (void)edp_rect_clear(ui);
    else if( widget == self->button_plateau )
        (void)edp_rect_plateau(ui);
    else if( widget == self->button_mark_area )
    {
        /* Named in the Areas panel, where it is then selected for Rename. */
        if( Editor_Areas_NewFromRect(ui, NULL) )
            Editor_Areas_Show(ui);
    }
    else if( widget == self->button_stamp || widget == self->input_stamp )
        (void)Editor_RectSelect_SaveStamp(ui);
    else if( widget == self->check_all_levels || widget == self->check_overwrite )
        return 1;
    else
        return 0;
    return 1;
}

int
Editor_RectSelect_SaveStamp(struct EditorUi* ui)
{
    struct RectPanel* self;
    const char* name;

    assert(ui);
    self = Editor_UiPanelSelf(ui, "rect");
    if( !self )
        return 0;
    name = ToriRSChrome_Text(ui->chrome, self->input_stamp);
    if( !name || !name[0] )
    {
        /* The field is in a panel that opens with the Rectangle tool. */
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
        Editor_UiStatus(ui, "Save as stamp refused: type a name in the Rectangle panel's Stamp name first");
        return 0;
    }
    return edp_stamp_save_rect(ui, &ui->tools.rect, name, ToriRSChrome_Checked(ui->chrome, self->check_all_levels),
        edp_rect_with_spawns(), ToriRSChrome_Checked(ui->chrome, self->check_overwrite));
}

static void
rect_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct RectPanel* self = opaque;
    /* Opens with its tool, not with the editor. */
    if( visible && ui->tools.tool != EDITOR_TOOL_RECT_SELECT )
        ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
}

static void
rect_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_RectSelect_Register(struct EditorUi* ui)
{
    struct RectPanel* self;
    struct EditorUiPanel desc;

    assert(ui);

    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->shown.valid = -1;
    self->shown_clip_w = -1;
    self->shown_tool = EDITOR_TOOL_COUNT;
    self->panel = ToriRSChrome_PanelAdd(
        ui->chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + EDP_COL_W + 8),
        edp_px(ui, EDP_TOP_Y), 0, "Rectangle");
    assert(self->panel >= 0);
    self->row_rect = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->row_clip = ToriRSChrome_Label(ui->chrome, self->panel, "");
    self->check_spawns = ToriRSChrome_Checkbox(ui->chrome, self->panel, "with spawns", edp_rect_with_spawns());
    ToriRSChrome_SetTooltip(ui->chrome, self->check_spawns,
        "Copy and Paste take the rectangle's npcs and ground items too");
    self->button_copy = ToriRSChrome_Button(ui->chrome, self->panel, "Copy");
    self->button_paste = ToriRSChrome_Button(ui->chrome, self->panel, "Paste");
    self->button_fill = ToriRSChrome_Button(ui->chrome, self->panel, "Fill with palette");
    self->button_clear = ToriRSChrome_Button(ui->chrome, self->panel, "Clear");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_copy, "Copy the rectangle's tiles, locs and spawns for Paste");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_fill, "Paint the rectangle with the Palette's picks");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_paste,
        "Paste at the SW corner; authored heights follow the new ground");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_clear, "Every tile back to no tokens");
    self->button_plateau = ToriRSChrome_Button(ui->chrome, self->panel, "Plateau");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_plateau,
        "Flatten to the latched tile's height, a skirt eased around it");
    self->button_mark_area = ToriRSChrome_Button(ui->chrome, self->panel, "Mark area from rect");
    /* Every button here wears a keycap: in the label column's width the cap
     * left "Fill ..." and "Ma..." of the captions. They take the row. */
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_copy, 1);
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_paste, 1);
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_fill, 1);
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_clear, 1);
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_plateau, 1);
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_mark_area, 1);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_mark_area, "Name this rectangle as an area (Areas panel)");
    ToriRSChrome_Separator(ui->chrome, self->panel);
    self->input_stamp = ToriRSChrome_TextInput(ui->chrome, self->panel, "Stamp name", "");
    ToriRSChrome_SetTooltip(ui->chrome, self->input_stamp, "Letters, digits, _ and -: cottage, sheep-pen");
    self->check_all_levels = ToriRSChrome_Checkbox(ui->chrome, self->panel, "All levels", 1);
    ToriRSChrome_SetTooltip(ui->chrome, self->check_all_levels, "The stamp takes every level: upper floors and the roof");
    self->check_overwrite = ToriRSChrome_Checkbox(ui->chrome, self->panel, "Overwrite stamp", 0);
    ToriRSChrome_SetTooltip(ui->chrome, self->check_overwrite, "Replace a stamp that already has that name");
    self->button_stamp = ToriRSChrome_Button(ui->chrome, self->panel, "Save as stamp");
    ToriRSChrome_SetTooltip(ui->chrome, self->button_stamp, "Everything in the rectangle as a stamp to place again");
    ToriRSChrome_ButtonSetFill(ui->chrome, self->button_stamp, 1);
    rect_chords(ui, self);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "rect";
    desc.title = "Rectangle";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = rect_tick;
    desc.activate = rect_activate;
    desc.set_visible = rect_set_visible;
    desc.destroy = rect_destroy;
    Editor_UiAddPanel(ui, &desc);
}
