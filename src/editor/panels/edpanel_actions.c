/*
 * The actions more than one control reaches: the loc selection's nudge,
 * rotate, reshape, duplicate and delete (menu bar, locs panel, arrow keys, R,
 * S), and the rectangle's copy, paste, fill and clear (menu bar, rectangle
 * panel). Each is: build the commands against the mirror (edpanel_ops.h),
 * submit them as one undo step, re-point the selection, say what happened.
 */

#include "edpanel_common.h"
#include "edpanel_ops.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

/* The clipboard outlives any one panel: copy in the menu, paste from a key. */
static struct Editor_TileClip* g_clip;

static void
actions_selection_moved(
    struct EditorUi* ui,
    const struct Editor_LocOpResult* result,
    int keep)
{
    if( !keep )
        return;
    ui->tools.loc_count = result->ref_count;
    memcpy(ui->tools.locs, result->refs, sizeof(result->refs[0]) * (size_t)result->ref_count);
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
}

static int
actions_need_selection(
    struct EditorUi* ui,
    const char* what)
{
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    if( ui->tools.loc_count == 0 )
    {
        Editor_UiLog(ui, "editor: %s: select a loc first (shift-click adds more)", what);
        return 0;
    }
    return 1;
}

/** Result scratch: one op at a time, and it is ~8 KB. */
static struct Editor_LocOpResult g_result;

int
edp_locs_nudge(
    struct EditorUi* ui,
    int dx,
    int dz)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "nudge") )
        return 0;
    count = Editor_LocOpsNudge(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, dx, dz, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "nudge");
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_move_to(
    struct EditorUi* ui,
    int world_x,
    int world_z)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "move") )
        return 0;
    count = Editor_LocOpsMoveTo(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, world_x, world_z, commands, capacity,
        &g_result);
    applied = edp_apply(ui, commands, count, "move");
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_rotate(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "rotate") )
        return 0;
    count = Editor_LocOpsRotate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, 1, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "rotate");
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_reshape(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "reshape") )
        return 0;
    count = Editor_LocOpsReshape(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "reshape");
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

int
edp_locs_duplicate(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "duplicate") )
        return 0;
    /* One tile east: beside the original, so the copy is visible and the
     * arrow keys carry it from there. */
    count = Editor_LocOpsDuplicate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, 1, 0, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "duplicate");
    actions_selection_moved(ui, &g_result, applied > 0);
    if( applied > 0 )
        Editor_UiLog(ui, "editor: duplicated %d loc(s); the copies are selected", applied);
    return applied;
}

int
edp_locs_delete(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_selection(ui, "delete") )
        return 0;
    count = Editor_LocOpsDelete(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, commands, capacity, &g_result);
    applied = edp_apply(ui, commands, count, "delete");
    if( applied > 0 )
    {
        Editor_UiLog(ui, "editor: deleted %d loc(s)", applied);
        ui->tools.loc_count = 0;
        if( ui->hooks.selection_changed )
            ui->hooks.selection_changed(ui->hooks_user);
    }
    return applied;
}

int
edp_locs_place(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level)
{
    struct Editor_Cmd command;
    int applied;

    assert(ui);
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: place needs an editor session");
        return 0;
    }
    if( loc_id < 0 )
    {
        Editor_UiLog(ui, "editor: pick a loc in the catalog first");
        return 0;
    }
    if( !Editor_LocOpsPlace(
            &ui->editor->doc, loc_id, shape, rotation, world_x, world_z, level, &command, 1,
            &g_result) )
    {
        Editor_UiLog(ui, "editor: m%d_%d is not open; place refused", world_x / 64, world_z / 64);
        return 0;
    }
    applied = edp_apply(ui, &command, 1, "place");
    actions_selection_moved(ui, &g_result, applied > 0);
    return applied;
}

/* ---- the rectangle ------------------------------------------------------------ */

static struct Editor_TileClip*
actions_clip(void)
{
    if( !g_clip )
    {
        g_clip = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*g_clip));
        assert(g_clip);
    }
    return g_clip;
}

int
edp_rect_clip_size(
    int* out_w,
    int* out_h)
{
    assert(out_w);
    assert(out_h);
    if( !g_clip || !g_clip->valid )
        return 0;
    *out_w = g_clip->w;
    *out_h = g_clip->h;
    return 1;
}

static int
actions_need_rect(
    struct EditorUi* ui,
    const char* what)
{
    struct Editor_TileRect r;

    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    if( !Editor_TileRectNormalise(&ui->tools.rect, &r) )
    {
        Editor_UiLog(
            ui, "editor: %s: drag a rectangle with the rectangle tool first (64x64 at most)", what);
        return 0;
    }
    return 1;
}

int
edp_rect_copy(struct EditorUi* ui)
{
    int copied;

    assert(ui);
    if( !actions_need_rect(ui, "copy") )
        return 0;
    copied = Editor_TileClipCopy(&ui->editor->doc, &ui->tools.rect, actions_clip());
    Editor_UiLog(ui, "editor: copied %d tile(s)", copied);
    return copied;
}

int
edp_rect_paste(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    struct Editor_TileRect r;

    assert(ui);
    if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
    {
        world_x = r.x0;
        world_z = r.z0;
        level = r.level;
    }
    return edp_rect_paste_at(ui, world_x, world_z, level);
}

int
edp_rect_paste_at(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !ui->editor )
        return 0;
    if( !g_clip || !g_clip->valid )
    {
        Editor_UiLog(ui, "editor: nothing copied yet");
        return 0;
    }
    count = Editor_TileClipPaste(&ui->editor->doc, g_clip, world_x, world_z, level, commands, capacity);
    applied = edp_apply(ui, commands, count, "paste");
    Editor_UiLog(ui, "editor: pasted %dx%d, %d tile(s) changed", g_clip->w, g_clip->h, applied);
    return applied;
}

int
edp_rect_fill(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_rect(ui, "fill") )
        return 0;
    count = Editor_TileRectFill(&ui->editor->doc, &ui->tools.rect, &ui->tools, commands, capacity);
    applied = edp_apply(ui, commands, count, "fill");
    Editor_UiLog(ui, "editor: filled %d tile(s)", applied);
    return applied;
}

int
edp_rect_clear(struct EditorUi* ui)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    if( !actions_need_rect(ui, "clear") )
        return 0;
    count = Editor_TileRectClear(&ui->editor->doc, &ui->tools.rect, commands, capacity);
    applied = edp_apply(ui, commands, count, "clear");
    Editor_UiLog(ui, "editor: cleared %d tile(s)", applied);
    return applied;
}

int
edp_square_clear(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct Editor_Square* square;
    int applied = 0;
    int const was_open = ui ? ui->tools.stroke_open : 0;

    assert(ui);
    if( !ui->editor )
        return 0;
    square = Editor_DocFindSquare(&ui->editor->doc, map_x, map_z);
    if( !square || !square->loaded )
    {
        Editor_UiLog(ui, "editor: open m%d_%d before clearing it", map_x, map_z);
        return 0;
    }

    /* One undo step for the whole square: four levels of tiles and every
     * loc, submitted in chunks the scratch holds. */
    if( !was_open )
        Editor_StrokeBegin(ui->editor);
    ui->tools.stroke_open = 1;
    for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
    {
        struct Editor_TileRect rect = { 1, map_x * 64, map_z * 64, map_x * 64 + 63,
                                        map_z * 64 + 63, level };
        int const count = Editor_TileRectClear(&ui->editor->doc, &rect, commands, capacity);
        applied += edp_apply(ui, commands, count, "clear square");
    }
    while( square->loc_count > 0 )
    {
        int count = 0;
        /* Last first: each delete removes by key, and the list shrinks. */
        for( int i = square->loc_count - 1; i >= 0 && count < capacity; i-- )
        {
            Editor_CmdInitSquare(&commands[count], EDITOR_CMD_LOC, map_x, map_z);
            commands[count].has_before = 1;
            commands[count].u.loc.before = square->locs[i];
            count++;
        }
        {
            int const before = square->loc_count;
            applied += edp_apply(ui, commands, count, "clear square");
            if( square->loc_count >= before )
                break; /* refused: do not spin */
        }
    }
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
    Editor_UiLog(ui, "editor: cleared m%d_%d (%d edits, one undo step)", map_x, map_z, applied);
    return applied;
}

void
edp_actions_free(void)
{
    ToriMem_Free(ToriMem_Editor(), g_clip);
    g_clip = NULL;
}
