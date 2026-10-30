#include "editor_cmd.h"

#include "editor_doc.h"
#include "editor_if.h"
#include "editor_spawn.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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

static struct Editor_Square*
command_square(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command)
{
    int map_x;
    int map_z;

    if( command->doc_kind != EDITOR_DOC_SQUARE
        || !Editor_SquareKeyParse(command->doc_key, &map_x, &map_z) )
        return NULL;
    return Editor_DocFindSquare(doc, map_x, map_z);
}

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

    square = command_square(doc, command);
    if( !square )
        return 0;
    if( command->u.tile.level < 0 || command->u.tile.level >= EDITOR_SQUARE_LEVELS
        || command->u.tile.x < 0 || command->u.tile.x >= EDITOR_SQUARE_X
        || command->u.tile.z < 0 || command->u.tile.z >= EDITOR_SQUARE_Z )
        return 0;

    want = (direction == EDITOR_CMD_FORWARD) ? &command->u.tile.after : &command->u.tile.before;
    square->tiles[Editor_TileIndex(command->u.tile.x, command->u.tile.z, command->u.tile.level)] =
        *want;
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

    square = command_square(doc, command);
    if( !square )
        return 0;

    /* Forward removes `before` and inserts `after`; the inverse swaps them.
     * Spelling it as one path rather than two keeps add/delete/replace from
     * needing three pairs of nearly-identical functions. */
    if( direction == EDITOR_CMD_FORWARD )
    {
        remove_loc = &command->u.loc.before;
        had_remove = command->has_before;
        insert_loc = &command->u.loc.after;
        had_insert = command->has_after;
    }
    else
    {
        remove_loc = &command->u.loc.after;
        had_remove = command->has_after;
        insert_loc = &command->u.loc.before;
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

/**
 * SPAWN and SPAWN_SUPPRESS: one row, added (has_after) or deleted
 * (has_before); the inverse swaps the two. A delete of a row that is not in
 * the file changes nothing and reports so — on the authority that refuses
 * the command, and every mirror applies only what the authority accepted.
 */
static int
apply_spawn(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_DocHandle handle;
    struct Editor_SpawnRow row;
    int adds;

    assert(doc);
    assert(command);

    if( command->has_before == command->has_after )
        return 0;
    if( !Editor_DocFind(doc, EDITOR_DOC_SPAWN, command->doc_key, &handle) || !handle.spawn->loaded )
        return 0;
    if( command->u.spawn.name[0] == '\0' || command->u.spawn.level < 0
        || command->u.spawn.level > 3 )
        return 0;

    memset(&row, 0, sizeof(row));
    row.is_obj = command->u.spawn.is_obj ? 1 : 0;
    row.suppress = command->kind == EDITOR_CMD_SPAWN_SUPPRESS;
    snprintf(row.name, sizeof(row.name), "%s", command->u.spawn.name);
    row.abs_x = command->u.spawn.abs_x;
    row.abs_z = command->u.spawn.abs_z;
    row.level = command->u.spawn.level;
    row.count = command->u.spawn.count < 1 ? 1 : command->u.spawn.count;

    adds = command->has_after;
    if( direction == EDITOR_CMD_INVERSE )
        adds = !adds;

    if( adds )
        Editor_SpawnInsert(&handle.spawn->file, &row);
    else
    {
        int index = Editor_SpawnFind(&handle.spawn->file, &row);
        if( index < 0 )
            return 0;
        Editor_SpawnRemoveAt(&handle.spawn->file, index);
    }
    handle.spawn->dirty = 1;
    return 1;
}

static int
apply_text_splice(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_DocHandle handle;
    const char* expect;
    const char* want;
    uint32_t expect_length;
    uint32_t want_length;
    uint32_t offset = command->u.text_splice.offset;

    assert(doc);
    assert(command);

    if( command->u.text_splice.before_len > EDITOR_CMD_SPLICE_MAX
        || command->u.text_splice.after_len
               > EDITOR_CMD_SPLICE_MAX - command->u.text_splice.before_len )
        return 0;
    if( !Editor_DocFind(doc, command->doc_kind, command->doc_key, &handle) || !handle.text
        || !handle.text->loaded )
        return 0;

    if( direction == EDITOR_CMD_FORWARD )
    {
        expect = command->u.text_splice.text;
        expect_length = command->u.text_splice.before_len;
        want = command->u.text_splice.text + command->u.text_splice.before_len;
        want_length = command->u.text_splice.after_len;
    }
    else
    {
        expect = command->u.text_splice.text + command->u.text_splice.before_len;
        expect_length = command->u.text_splice.after_len;
        want = command->u.text_splice.text;
        want_length = command->u.text_splice.before_len;
    }

    /* The splice names the text it replaces, so a stale offset is caught
     * here instead of corrupting the document. */
    if( offset > handle.text->length || expect_length > handle.text->length - offset )
        return 0;
    if( expect_length > 0 && memcmp(handle.text->text + offset, expect, expect_length) != 0 )
        return 0;

    Editor_TextDocSplice(handle.text, offset, expect_length, want, want_length);
    handle.text->dirty = 1;
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

    if( !Editor_CmdKindFits(command->doc_kind, command->kind) )
        return 0;

    switch( command->kind )
    {
    case EDITOR_CMD_TILE:
        return apply_tile(doc, command, direction);
    case EDITOR_CMD_LOC:
        return apply_loc(doc, command, direction);
    case EDITOR_CMD_SPAWN:
    case EDITOR_CMD_SPAWN_SUPPRESS:
        return apply_spawn(doc, command, direction);
    case EDITOR_CMD_CFG_FIELD:
    case EDITOR_CMD_CFG_BLOCK:
        return Editor_CfgApply(doc, command, direction);
    case EDITOR_CMD_IF_FIELD:
    case EDITOR_CMD_IF_COMPONENT:
        /* The interface codec (editor_if.h): canonical field order,
         * renumbering structure edits, exact undo. */
        return Editor_IfDocApply(doc, command, direction);
    case EDITOR_CMD_TEXT_SPLICE:
        return apply_text_splice(doc, command, direction);
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

    for( int i = 0; i < *count && i < max; i++ )
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
    int map_x;
    int map_z;
    int x;
    int z;

    assert(command);
    assert(out_coords || max == 0);

    if( command->doc_kind != EDITOR_DOC_SQUARE
        || !Editor_SquareKeyParse(command->doc_key, &map_x, &map_z) )
        return 0;

    span_add(out_coords, max, &count, map_x, map_z);

    /* Placements are local: a loc draws where it stands and does not change a
     * neighbouring square's terrain. */
    if( command->kind != EDITOR_CMD_TILE )
        return count;

    height_changed = command->u.tile.before.has_height != command->u.tile.after.has_height
                     || command->u.tile.before.height != command->u.tile.after.height;
    underlay_changed = command->u.tile.before.underlay_id != command->u.tile.after.underlay_id;

    if( !height_changed && !underlay_changed )
        return count;

    /* A height edit reaches exactly one tile past the square, because the
     * corner it moves is shared with the adjacent edge row. An underlay edit
     * reaches as far as the colour blend window. */
    reach = underlay_changed ? EDITOR_UNDERLAY_BLEND_REACH : 1;
    x = command->u.tile.x;
    z = command->u.tile.z;

    if( x < reach )
        span_add(out_coords, max, &count, map_x - 1, map_z);
    if( x >= EDITOR_SQUARE_X - reach )
        span_add(out_coords, max, &count, map_x + 1, map_z);
    if( z < reach )
        span_add(out_coords, max, &count, map_x, map_z - 1);
    if( z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, map_x, map_z + 1);

    /* Corners touch a third square diagonally — the one that shares only the
     * corner vertex. Missing it leaves a single visibly wrong tile. */
    if( x < reach && z < reach )
        span_add(out_coords, max, &count, map_x - 1, map_z - 1);
    if( x < reach && z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, map_x - 1, map_z + 1);
    if( x >= EDITOR_SQUARE_X - reach && z < reach )
        span_add(out_coords, max, &count, map_x + 1, map_z - 1);
    if( x >= EDITOR_SQUARE_X - reach && z >= EDITOR_SQUARE_Z - reach )
        span_add(out_coords, max, &count, map_x + 1, map_z + 1);

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
Editor_UndoFree(struct Editor_UndoStack* stack)
{
    if( !stack )
        return;

    ToriMem_Free(ToriMem_Editor(), stack->commands);
    ToriMem_Free(ToriMem_Editor(), stack->stroke_start);
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

    if( stack->applied >= stack->capacity )
    {
        int next = stack->capacity ? stack->capacity * 2 : 256;
        if( next > EDITOR_UNDO_MAX )
            next = EDITOR_UNDO_MAX;
        stack->commands = ToriMem_Realloc(
            ToriMem_Editor(), stack->commands, (size_t)next * sizeof(*stack->commands));
        assert(stack->commands);
        stack->stroke_start = ToriMem_Realloc(
            ToriMem_Editor(), stack->stroke_start, (size_t)next * sizeof(*stack->stroke_start));
        assert(stack->stroke_start);
        stack->capacity = next;
    }

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
