#include "editor_cmd.h"

#include "editor_doc.h"

#include <assert.h>
#include <string.h>

/**
 * How far an underlay change reaches past its own tile.
 *
 * Terrain colour is blended over a sliding window of neighbouring underlays,
 * so painting near a square's edge changes colours on the other side of it.
 * Five is the window the client's terrain build uses either side of a tile.
 */
#define EDITOR_UNDERLAY_BLEND_REACH 5

/* ---- applying ------------------------------------------------------------ */

static int
apply_tile(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_Square* square;
    const struct Editor_Tile* want;

    assert(doc);
    assert(command);

    square = Editor_DocFindSquare(doc, command->map_x, command->map_z);
    if( !square )
        return 0;

    want = (direction == EDITOR_CMD_FORWARD) ? &command->tile_after : &command->tile_before;
    square->tiles[Editor_TileIndex(command->x, command->z, command->level)] = *want;
    square->dirty_map = 1;
    return 1;
}

static int
apply_loc(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_Square* square;
    const struct Editor_Loc* remove_loc;
    const struct Editor_Loc* insert_loc;
    int had_remove;
    int had_insert;

    assert(doc);
    assert(command);

    square = Editor_DocFindSquare(doc, command->map_x, command->map_z);
    if( !square )
        return 0;

    /* Forward removes `before` and inserts `after`; the inverse swaps them.
     * Spelling it as one path rather than two keeps add/delete/replace from
     * needing three pairs of nearly-identical functions. */
    if( direction == EDITOR_CMD_FORWARD )
    {
        remove_loc = &command->loc_before;
        had_remove = command->has_before;
        insert_loc = &command->loc_after;
        had_insert = command->has_after;
    }
    else
    {
        remove_loc = &command->loc_after;
        had_remove = command->has_after;
        insert_loc = &command->loc_before;
        had_insert = command->has_before;
    }

    if( had_remove )
    {
        int index = Editor_SquareLocFind(
            square, remove_loc->level, remove_loc->x, remove_loc->z, remove_loc->shape);
        if( index >= 0 )
            Editor_SquareLocRemoveAt(square, index);
    }
    if( had_insert )
        Editor_SquareLocAdd(square, insert_loc);

    square->dirty_loc = 1;
    return 1;
}

int
Editor_CmdApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    assert(doc);
    assert(command);

    switch( command->kind )
    {
    case EDITOR_CMD_TILE:
        return apply_tile(doc, command, direction);
    case EDITOR_CMD_LOC:
        return apply_loc(doc, command, direction);
    }
    return 0;
}

static void
span_add(
    int* out_coords,
    int max,
    int* count,
    int map_x,
    int map_z)
{
    assert(count);

    for( int i = 0; i < *count; i++ )
    {
        if( out_coords[i * 2] == map_x && out_coords[i * 2 + 1] == map_z )
            return;
    }
    if( *count < max )
    {
        out_coords[*count * 2] = map_x;
        out_coords[*count * 2 + 1] = map_z;
    }
    (*count)++;
}

int
Editor_CmdRebuildSpan(
    const struct Editor_Cmd* command,
    int* out_coords,
    int max)
{
    int count = 0;
    int reach;
    int height_changed;
    int underlay_changed;

    assert(command);
    assert(out_coords || max == 0);

    span_add(out_coords, max, &count, command->map_x, command->map_z);

    /* Placements are local: a loc draws where it stands and does not change a
     * neighbouring square's terrain. */
    if( command->kind != EDITOR_CMD_TILE )
        return count;

    height_changed = command->tile_before.has_height != command->tile_after.has_height ||
                     command->tile_before.height != command->tile_after.height;
    underlay_changed = command->tile_before.underlay_id != command->tile_after.underlay_id;

    if( !height_changed && !underlay_changed )
        return count;

    /* A height edit reaches exactly one tile past the square, because the
     * corner it moves is shared with the adjacent edge row. An underlay edit
     * reaches as far as the colour blend window. */
    reach = underlay_changed ? EDITOR_UNDERLAY_BLEND_REACH : 1;

    if( command->x < reach )
        span_add(out_coords, max, &count, command->map_x - 1, command->map_z);
    if( command->x >= EDITOR_SQUARE_X - reach )
        span_add(out_coords, max, &count, command->map_x + 1, command->map_z);
    if( command->z < reach )
        span_add(out_coords, max, &count, command->map_x, command->map_z - 1);
    if( command->z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, command->map_x, command->map_z + 1);

    /* Corners touch a third square diagonally — the one that shares only the
     * corner vertex. Missing it leaves a single visibly wrong tile. */
    if( command->x < reach && command->z < reach )
        span_add(out_coords, max, &count, command->map_x - 1, command->map_z - 1);
    if( command->x < reach && command->z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, command->map_x - 1, command->map_z + 1);
    if( command->x >= EDITOR_SQUARE_X - reach && command->z < reach )
        span_add(out_coords, max, &count, command->map_x + 1, command->map_z - 1);
    if( command->x >= EDITOR_SQUARE_X - reach && command->z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, command->map_x + 1, command->map_z + 1);

    return count;
}

/* ---- history ------------------------------------------------------------- */

void
Editor_UndoInit(struct Editor_UndoStack* stack)
{
    assert(stack);
    memset(stack, 0, sizeof(*stack));
}

void
Editor_UndoStrokeBegin(struct Editor_UndoStack* stack)
{
    assert(stack);

    stack->in_stroke = 1;
    stack->stroke_opened = 0;
}

void
Editor_UndoStrokeEnd(struct Editor_UndoStack* stack)
{
    assert(stack);

    stack->in_stroke = 0;
    stack->stroke_opened = 0;
}

int
Editor_UndoPush(
    struct Editor_UndoStack* stack,
    const struct Editor_Cmd* command)
{
    int starts_group;

    assert(stack);
    assert(command);

    if( stack->applied >= EDITOR_UNDO_MAX )
        return 0;

    /* A new edit after an undo abandons the redo tail — the user has taken a
     * different branch, and keeping the old one would let redo reapply an edit
     * that no longer follows from the current state. */
    stack->count = stack->applied;

    /* Inside a stroke only the first command opens a group, so the whole drag
     * undoes at once. Outside one, every command is its own group. */
    starts_group = !stack->in_stroke || !stack->stroke_opened;
    if( stack->in_stroke )
        stack->stroke_opened = 1;

    stack->commands[stack->count] = *command;
    stack->stroke_start[stack->count] = (unsigned char)starts_group;
    stack->count++;
    stack->applied = stack->count;
    return 1;
}

int
Editor_UndoUndo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reverted)(void*, const struct Editor_Cmd*),
    void* user_data)
{
    int reverted = 0;

    assert(stack);
    assert(doc);

    if( stack->applied == 0 )
        return 0;

    /* Walk back to and including the command that opened the group. */
    do
    {
        const struct Editor_Cmd* command;

        stack->applied--;
        command = &stack->commands[stack->applied];
        Editor_CmdApply(doc, command, EDITOR_CMD_INVERSE);
        if( on_reverted )
            on_reverted(user_data, command);
        reverted++;
    } while( stack->applied > 0 && !stack->stroke_start[stack->applied] );

    return reverted;
}

int
Editor_UndoRedo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reapplied)(void*, const struct Editor_Cmd*),
    void* user_data)
{
    int reapplied = 0;

    assert(stack);
    assert(doc);

    if( stack->applied >= stack->count )
        return 0;

    /* The group starting here, up to the next group boundary. */
    do
    {
        const struct Editor_Cmd* command = &stack->commands[stack->applied];
        Editor_CmdApply(doc, command, EDITOR_CMD_FORWARD);
        if( on_reapplied )
            on_reapplied(user_data, command);
        stack->applied++;
        reapplied++;
    } while( stack->applied < stack->count && !stack->stroke_start[stack->applied] );

    return reapplied;
}
