#include "edpanel_common.h"

#include "edpanel_ops.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct Editor_Cmd* g_scratch;

struct Editor_Cmd*
edp_scratch(int* out_capacity)
{
    assert(out_capacity);
    /* One allocation for the session: the largest single action is a
     * rectangle of EDITOR_RECT_TILES_MAX tiles, and a brush is smaller. */
    if( !g_scratch )
    {
        g_scratch = ToriMem_Calloc(ToriMem_Editor(), EDITOR_RECT_TILES_MAX, sizeof(*g_scratch));
        assert(g_scratch);
    }
    *out_capacity = EDITOR_RECT_TILES_MAX;
    return g_scratch;
}

void
edp_scratch_free(void)
{
    ToriMem_Free(ToriMem_Editor(), g_scratch);
    g_scratch = NULL;
}

int
edp_apply(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count,
    const char* what)
{
    int applied = 0;

    assert(ui);
    assert(commands || count == 0);
    assert(what);

    if( count <= 0 )
        return 0;
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    /* A read-only window (another editor holds the lane) looks, and does not
     * edit: an edit here could never be saved, and "Placed npc ... Ctrl+Z
     * undoes it" read as done while the window's Save wrote nothing
     * (2026-11-12). */
    if( !ui->editor->writable || (ui->lane.known && !ui->lane.writable) )
    {
        Editor_UiStatus(ui, "%s refused: this window is read-only (another editor holds the lane %s); close that "
                            "one to edit here",
            what, ui->lane.known ? ui->lane.name : "tree");
        return 0;
    }
    if( !ui->tools.stroke_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, what);
    }
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    if( !ui->tools.stroke_open )
        Editor_StrokeEnd(ui->editor);
    if( applied < count )
        Editor_UiLog(ui, "editor: %s: the authority refused %d of %d edits", what, count - applied, count);
    /* A spawn PLACED (the Place click in the world, the Spawns panel's
     * Place, the builders: all submit as "place spawn") is shown in the
     * world (S1, 2026-11-09). By the action's name, so an edit form's move,
     * which is also a delete and an add, is not taken for a placement. */
    if( applied > 0 && strcmp(what, "place spawn") == 0 )
        edp_spawns_note_added(ui, commands, count);
    return applied;
}

int
edp_ensure_doc(
    struct EditorUi* ui,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor_DocHandle handle;

    assert(ui);
    assert(key);

    if( !ui->editor )
        return 0;
    if( Editor_DocFind(&ui->editor->doc, kind, key, &handle) && Editor_DocHandleLoaded(&handle) )
        return 1;
    return Editor_OpenDoc(ui->editor, kind, key);
}

void
edp_spawns_note_added(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count)
{
    assert(ui);
    assert(commands || count == 0);
    /* Counted here and read by the Spawns panel's tick, so this file names
     * no panel (it links into harnesses that hold none). */
    for( int i = 0; i < count; i++ )
        if( commands[i].kind == EDITOR_CMD_SPAWN && commands[i].has_after && !commands[i].has_before )
        {
            if( commands[i].u.spawn.is_obj )
                ui->spawns_added_obj++;
            else
                ui->spawns_added_npc++;
        }
}

/* ---- the tile grid ---------------------------------------------------------- */

void
edp_tile_grid_set(
    struct EditorUi* ui,
    enum Editor_TileGridMode mode)
{
    assert(ui);
    assert(mode >= EDITOR_TILE_GRID_AUTO);
    assert(mode < EDITOR_TILE_GRID_MODE_COUNT);
    if( ui->tile_grid == (int)mode )
        return;
    ui->tile_grid = (int)mode;
    if( mode == EDITOR_TILE_GRID_ON )
        Editor_UiStatus(ui, "Tile grid on: tiles round the pointer, each open square's border, the arrow points north");
    else if( mode == EDITOR_TILE_GRID_OFF )
        Editor_UiStatus(ui, "Tile grid off");
    else
        Editor_UiStatus(ui, "Tile grid automatic: shown on a square until it holds its first loc");
    if( ui->app_services.request_redraw )
        ui->app_services.request_redraw(ui->app_services.user_data);
}

enum Editor_TileGridMode
edp_tile_grid_cycle(struct EditorUi* ui)
{
    enum Editor_TileGridMode next;

    assert(ui);
    next = (enum Editor_TileGridMode)((ui->tile_grid + 1) % EDITOR_TILE_GRID_MODE_COUNT);
    edp_tile_grid_set(ui, next);
    return next;
}

int
edp_tile_grid_shows(
    const struct EditorUi* ui,
    int loc_count)
{
    assert(ui);
    if( ui->tile_grid == EDITOR_TILE_GRID_ON )
        return 1;
    if( ui->tile_grid == EDITOR_TILE_GRID_OFF )
        return 0;
    return loc_count == 0;
}

/* ---- the active level ------------------------------------------------------ */

int
edp_level_follow_vis(
    int old_edit,
    int old_vis,
    int new_edit)
{
    assert(old_edit >= EDP_LEVEL_AUTO);
    assert(old_edit < EDITOR_SQUARE_LEVELS);
    assert(new_edit >= EDP_LEVEL_AUTO);
    assert(new_edit < EDITOR_SQUARE_LEVELS);
    if( new_edit == EDP_LEVEL_AUTO )
        return -1;
    if( old_edit != EDP_LEVEL_AUTO && old_vis < 0 )
        return -1;
    return new_edit;
}

void
edp_level_set(
    struct EditorUi* ui,
    int level)
{
    int32_t value = level;

    assert(ui);
    assert(level >= EDP_LEVEL_AUTO);
    assert(level < EDITOR_SQUARE_LEVELS);
    ui->tools.vis_level = edp_level_follow_vis(ui->tools.edit_level, ui->tools.vis_level, level);
    ui->tools.edit_level = level;
    (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_EDIT_LEVEL, &value, 1);
}

int
edp_level_step(
    struct EditorUi* ui,
    int delta,
    int auto_level)
{
    int level;

    assert(ui);
    level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : auto_level;
    level += delta;
    if( level < 0 )
        level = 0;
    if( level > EDITOR_SQUARE_LEVELS - 1 )
        level = EDITOR_SQUARE_LEVELS - 1;
    edp_level_set(ui, level);
    return level;
}

int
edp_level_show_above(
    struct EditorUi* ui,
    int on)
{
    assert(ui);
    if( ui->tools.edit_level < 0 )
        return 0;
    ui->tools.vis_level = on ? -1 : ui->tools.edit_level;
    return 1;
}

int
edp_level_showing_above(const struct EditorUi* ui)
{
    assert(ui);
    return ui->tools.edit_level >= 0 && ui->tools.vis_level < 0;
}

int
edp_level_click(
    const struct EditorUi* ui,
    int hover_level)
{
    int level;

    assert(ui);
    level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : hover_level;
    if( level < 0 )
        level = 0;
    if( level > EDITOR_SQUARE_LEVELS - 1 )
        level = EDITOR_SQUARE_LEVELS - 1;
    return level;
}

/* ---- the brush steppers and the hints --------------------------------------- */

int
edp_brush_step(
    struct EditorUi* ui,
    enum Edp_BrushField field,
    int delta)
{
    struct Editor_Brush* brush;
    int* value;
    int lo;
    int hi;
    int notch;

    assert(ui);
    brush = &ui->tools.brush;
    /* The Terrain panel's slider ranges (edpanel_terrain.c). */
    if( field == EDP_BRUSH_RADIUS )
    {
        value = &brush->radius;
        lo = 0;
        hi = EDITOR_BRUSH_RADIUS_MAX;
        notch = 1;
    }
    else if( field == EDP_BRUSH_STEP )
    {
        value = &brush->step;
        lo = 1;
        hi = 16;
        notch = 1;
    }
    else
    {
        assert(field == EDP_BRUSH_STRENGTH);
        value = &brush->strength;
        lo = 0;
        hi = 100;
        notch = 5;
    }
    *value += delta * notch;
    if( *value < lo )
        *value = lo;
    if( *value > hi )
        *value = hi;
    return *value;
}

void
edp_hotkey_row(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    const char* text,
    char* out,
    int out_size)
{
    char chords[48];

    assert(ui);
    assert(text);
    assert(out);
    Editor_HotkeyDisplay(&ui->hotkeys, action, chords, (int)sizeof(chords));
    if( chords[0] )
        snprintf(out, (size_t)out_size, "%s\t%s", text, chords);
    else
        snprintf(out, (size_t)out_size, "%s", text);
}

void
edp_hotkey_tip(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    const char* text,
    char* out,
    int out_size)
{
    char chords[48];

    assert(ui);
    assert(text);
    assert(out);
    Editor_HotkeyDisplay(&ui->hotkeys, action, chords, (int)sizeof(chords));
    if( chords[0] )
        snprintf(out, (size_t)out_size, "%s  [%s]", text, chords);
    else
        snprintf(out, (size_t)out_size, "%s", text);
}

/** `action`'s first chord (the keycap) and every chord (the tooltip's); the
 *  tooltip's is "" when it says no more than the keycap, so the chrome falls
 *  back to the worn chord. */
static void
edp_chord_pair(
    const struct EditorUi* ui,
    enum Editor_HotkeyAction action,
    char* key,
    int key_size,
    char* all,
    int all_size)
{
    int const one = (int)action;

    Editor_HotkeysJoin(&ui->hotkeys, &one, 1, key, key_size);
    Editor_HotkeyDisplay(&ui->hotkeys, action, all, all_size);
    if( strcmp(all, key) == 0 )
        all[0] = '\0';
}

void
edp_button_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action)
{
    char key[EDITOR_HK_HINT_CHORD_MAX];
    char all[EDITOR_HK_HINT_CHORD_MAX];

    assert(ui);
    edp_chord_pair(ui, action, key, (int)sizeof(key), all, (int)sizeof(all));
    ToriRSChrome_ButtonSetChord(ui->chrome, widget, key);
    ToriRSChrome_SetTooltipChord(ui->chrome, widget, all);
    /* A button that wears a keycap takes its row. In the label column's
     * width the cap left three letters of the caption: "Ch... [Ctrl+Shift+B]"
     * for Check, "Re..." for Redo, "Ma..." for Mark area, "Auto-la..." -- on
     * every one of the editor's keycapped buttons that had not been widened
     * by hand (2026-11-09). An unbound action keeps the button's own width. */
    if( key[0] )
        ToriRSChrome_ButtonSetFill(ui->chrome, widget, 1);
}

void
edp_iconbutton_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action)
{
    char key[EDITOR_HK_HINT_CHORD_MAX];
    char all[EDITOR_HK_HINT_CHORD_MAX];

    assert(ui);
    edp_chord_pair(ui, action, key, (int)sizeof(key), all, (int)sizeof(all));
    ToriRSChrome_IconButtonSetChord(ui->chrome, widget, key);
    ToriRSChrome_SetTooltipChord(ui->chrome, widget, all);
}

void
edp_tooltip_chord(
    struct EditorUi* ui,
    int widget,
    enum Editor_HotkeyAction action)
{
    char all[EDITOR_HK_HINT_CHORD_MAX];

    assert(ui);
    Editor_HotkeyDisplay(&ui->hotkeys, action, all, (int)sizeof(all));
    ToriRSChrome_SetTooltipChord(ui->chrome, widget, all);
}
