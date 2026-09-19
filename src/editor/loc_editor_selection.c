#include "editor/loc_editor_selection.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
LocEditorSelection_Reset(struct LocEditorSelection* selection)
{
    assert(selection);
    memset(selection, 0, sizeof(*selection));
    selection->loc_id = -1;
    selection->shape = -1;
    selection->scene_x = -1;
    selection->scene_z = -1;
    selection->hover_x = -1;
    selection->hover_z = -1;
}

enum LocEditorSubject
LocEditorSelection_Subject(struct LocEditorSelection const* selection)
{
    assert(selection);
    if( selection->terrain )
        return LOC_EDITOR_SUBJECT_TILE;
    return selection->loc_id >= 0 ? LOC_EDITOR_SUBJECT_LOC : LOC_EDITOR_SUBJECT_NONE;
}

void
LocEditorSelection_SelectLoc(
    struct LocEditorSelection* selection,
    struct LocEditorLocPlacement const* placement)
{
    assert(selection);
    assert(placement);

    /* The two subjects are exclusive: one panel, one set of rows. */
    selection->terrain = false;
    selection->loc_id = placement->loc_id;
    selection->shape = placement->shape;
    selection->angle = placement->angle;
    selection->size_x = placement->size_x;
    selection->size_z = placement->size_z;
    selection->interactive = placement->interactive;
    snprintf(selection->name, sizeof(selection->name), "%s", placement->name ? placement->name : "");
    selection->scene_x = placement->scene_x;
    selection->scene_z = placement->scene_z;
    selection->level = placement->level;
}

void
LocEditorSelection_SelectTile(
    struct LocEditorSelection* selection,
    int scene_x,
    int scene_z,
    int cache_level)
{
    assert(selection);

    selection->terrain = true;
    /* No loc id, so every rule below that asks "is there a loc to move" says
     * no without needing to know about tiles at all. */
    selection->loc_id = -1;
    selection->scene_x = scene_x;
    selection->scene_z = scene_z;
    selection->terrain_level = cache_level;
    selection->level = cache_level;
}

void
LocEditorSelection_Clear(struct LocEditorSelection* selection)
{
    assert(selection);
    selection->loc_id = -1;
    selection->terrain = false;
}

bool
LocEditorSelection_Nudge(
    struct LocEditorSelection* selection,
    int dx,
    int dz,
    struct LocEditorMove* out_move)
{
    assert(selection);
    assert(out_move);

    if( selection->loc_id < 0 )
        return false;

    out_move->loc_id = selection->loc_id;
    out_move->shape = selection->shape;
    out_move->level = selection->level;
    out_move->from_x = selection->scene_x;
    out_move->from_z = selection->scene_z;
    out_move->from_angle = selection->angle;
    out_move->to_x = selection->scene_x + dx;
    out_move->to_z = selection->scene_z + dz;
    /* A nudge does not turn anything. Reported anyway so the caller places
     * with one field rather than choosing between two. */
    out_move->to_angle = selection->angle;

    /* Advanced only now that the move is described, so the next nudge starts
     * from where this one finished. */
    selection->scene_x = out_move->to_x;
    selection->scene_z = out_move->to_z;
    return true;
}

bool
LocEditorSelection_Rotate(struct LocEditorSelection* selection, struct LocEditorMove* out_move)
{
    assert(selection);
    assert(out_move);

    if( selection->loc_id < 0 )
        return false;

    out_move->loc_id = selection->loc_id;
    out_move->shape = selection->shape;
    out_move->level = selection->level;
    out_move->from_x = selection->scene_x;
    out_move->from_z = selection->scene_z;
    out_move->from_angle = selection->angle;
    /* Same tile: placing over it replaces whatever is there, so no remove leg
     * is needed and reporting a different `to` would delete the loc. */
    out_move->to_x = selection->scene_x;
    out_move->to_z = selection->scene_z;
    out_move->to_angle = (selection->angle + 1) % LOC_EDITOR_ANGLE_COUNT;

    selection->angle = out_move->to_angle;
    return true;
}

void
LocEditorSelection_NoteHover(
    struct LocEditorSelection* selection,
    bool cursor_over_panel,
    int tile_x,
    int tile_z)
{
    assert(selection);

    /* Over the panel the world hover names whatever the panel is sitting on
     * top of, which is not what the user was pointing at -- and by the time a
     * click on Reselect lands, the cursor is necessarily over the panel. */
    if( cursor_over_panel )
        return;
    /* Off the world entirely -- the sky, or a gap -- is not a new target
     * either. Taking it would drop the last real one for a tile that is not
     * there. */
    if( tile_x < 0 || tile_z < 0 )
        return;
    selection->hover_x = tile_x;
    selection->hover_z = tile_z;
}

bool
LocEditorSelection_HoverTile(
    struct LocEditorSelection const* selection,
    int* out_tile_x,
    int* out_tile_z)
{
    assert(selection);
    assert(out_tile_x);
    assert(out_tile_z);

    if( selection->hover_x < 0 || selection->hover_z < 0 )
        return false;
    *out_tile_x = selection->hover_x;
    *out_tile_z = selection->hover_z;
    return true;
}
