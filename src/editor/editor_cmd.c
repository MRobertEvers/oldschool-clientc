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
        Editor_SpawnRemoveRow(&handle.spawn->file, index);
    }
    handle.spawn->dirty = 1;
    return 1;
}

/** SPAWN_EDIT: one row rewritten where it stands (editor_spawn.h). */
static int
apply_spawn_edit(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_DocHandle handle;

    assert(doc);
    assert(command);

    if( !command->has_before || !command->has_after )
        return 0;
    if( !Editor_DocFind(doc, EDITOR_DOC_SPAWN, command->doc_key, &handle) || !handle.spawn->loaded )
        return 0;
    if( !Editor_SpawnApplyEdit(&handle.spawn->file, command, direction == EDITOR_CMD_INVERSE) )
        return 0;
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
    case EDITOR_CMD_SPAWN_EDIT:
        return apply_spawn_edit(doc, command, direction);
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

/* ---- the history as a mirror sees it ---------------------------------------- */

uint32_t
Editor_CmdHash(const struct Editor_Cmd* command)
{
    const unsigned char* bytes = (const unsigned char*)command;
    uint32_t hash = 2166136261u;

    assert(command);
    for( size_t i = 0; i < sizeof(*command); i++ )
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

void
Editor_HistoryInit(struct Editor_History* history)
{
    assert(history);
    memset(history, 0, sizeof(*history));
}

void
Editor_HistoryFree(struct Editor_History* history)
{
    if( !history )
        return;
    ToriMem_Free(ToriMem_Editor(), history->groups);
    memset(history, 0, sizeof(*history));
}

void
Editor_HistoryBreak(
    struct Editor_History* history,
    const char* label)
{
    assert(history);
    history->open = 0;
    history->next_verb[0] = '\0';
    snprintf(history->next_label, sizeof(history->next_label), "%s", label ? label : "");
}

void
Editor_HistoryVerb(
    struct Editor_History* history,
    const char* verb)
{
    assert(history);
    assert(verb);
    /* Before its first command the group does not exist yet: the name waits
     * with the break. After, it names the open group. */
    if( !history->open || history->count == 0 || history->applied < history->count )
        snprintf(history->next_verb, sizeof(history->next_verb), "%s", verb);
    else
        snprintf(history->groups[history->count - 1].verb,
            sizeof(history->groups[history->count - 1].verb), "%s", verb);
    history->serial++;
}

/** Tile units of an `h` byte: 1 is height 0, r >= 2 is r. */
static int
history_units(const struct Editor_Tile* tile)
{
    return tile->height == 1 ? 0 : tile->height;
}

static void
history_tally(
    struct Editor_HistoryGroup* group,
    const struct Editor_Cmd* command)
{
    if( group->cmd_count == 0 )
    {
        group->doc_kind = command->doc_kind;
        snprintf(group->doc_key, sizeof(group->doc_key), "%s", command->doc_key);
        group->doc_keys = 1;
        group->first_hash = Editor_CmdHash(command);
    }
    else if( group->doc_keys == 1
             && (group->doc_kind != command->doc_kind || strcmp(group->doc_key, command->doc_key) != 0) )
        group->doc_keys = 2;
    group->last_hash = Editor_CmdHash(command);
    group->kinds[command->kind]++;

    switch( command->kind )
    {
    case EDITOR_CMD_TILE:
    {
        const struct Editor_Tile* before = &command->u.tile.before;
        const struct Editor_Tile* after = &command->u.tile.after;
        int map_x = 0;
        int map_z = 0;
        int world_x;
        int world_z;

        (void)Editor_SquareKeyParse(command->doc_key, &map_x, &map_z);
        world_x = map_x * EDITOR_SQUARE_X + command->u.tile.x;
        world_z = map_z * EDITOR_SQUARE_Z + command->u.tile.z;
        if( group->kinds[EDITOR_CMD_TILE] == 1 )
        {
            group->tile_level = command->u.tile.level;
            group->tile_x0 = group->tile_x1 = world_x;
            group->tile_z0 = group->tile_z1 = world_z;
        }
        if( world_x < group->tile_x0 )
            group->tile_x0 = world_x;
        if( world_x > group->tile_x1 )
            group->tile_x1 = world_x;
        if( world_z < group->tile_z0 )
            group->tile_z0 = world_z;
        if( world_z > group->tile_z1 )
            group->tile_z1 = world_z;
        if( !after->has_height && !after->has_overlay && !after->settings && !after->underlay_id )
            group->cleared++;
        if( before->has_height && after->has_height )
        {
            if( history_units(after) > history_units(before) )
                group->raised++;
            else if( history_units(after) < history_units(before) )
                group->lowered++;
        }
        /* Authored from absent, or back to absent: the command alone does not
         * say which way the ground went, so neither tally moves. */
        if( before->underlay_id != after->underlay_id )
            group->underlay++;
        if( before->has_overlay != after->has_overlay || before->overlay_id != after->overlay_id
            || before->shape != after->shape || before->rotation != after->rotation )
            group->overlay++;
        if( before->settings != after->settings )
            group->flags++;
        break;
    }
    case EDITOR_CMD_LOC:
        if( command->has_after && !command->has_before )
        {
            group->loc_adds++;
            if( group->loc_adds == 1 )
                group->loc_id = command->u.loc.after.loc_id;
        }
        else if( command->has_before && !command->has_after )
        {
            group->loc_removes++;
            if( group->loc_removes == 1 && group->loc_adds == 0 )
                group->loc_id = command->u.loc.before.loc_id;
        }
        else if( command->has_before && command->has_after )
        {
            const struct Editor_Loc* b = &command->u.loc.before;
            const struct Editor_Loc* a = &command->u.loc.after;
            if( b->x != a->x || b->z != a->z || b->level != a->level )
                group->loc_moves++;
            else
                group->loc_turns++;
            if( group->loc_moves + group->loc_turns == 1 )
                group->loc_id = a->loc_id;
        }
        break;
    case EDITOR_CMD_SPAWN:
    case EDITOR_CMD_SPAWN_SUPPRESS:
    {
        int const is_suppress = command->kind == EDITOR_CMD_SPAWN_SUPPRESS;
        if( group->name[0] == '\0' )
        {
            snprintf(group->name, sizeof(group->name), "%s", command->u.spawn.name);
            group->spawn_is_obj = command->u.spawn.is_obj;
            group->tile_x0 = command->u.spawn.abs_x;
            group->tile_z0 = command->u.spawn.abs_z;
        }
        if( command->has_after )
            *(is_suppress ? &group->suppress_adds : &group->spawn_adds) += 1;
        else
            *(is_suppress ? &group->suppress_removes : &group->spawn_removes) += 1;
        break;
    }
    case EDITOR_CMD_SPAWN_EDIT:
    {
        const struct Editor_CmdSpawnRow* b = &command->u.spawn_edit.before;
        const struct Editor_CmdSpawnRow* a = &command->u.spawn_edit.after;
        if( group->name[0] == '\0' )
        {
            snprintf(group->name, sizeof(group->name), "%s", a->name);
            group->spawn_is_obj = a->is_obj;
            group->tile_x0 = a->abs_x;
            group->tile_z0 = a->abs_z;
        }
        group->spawn_edits++;
        if( b->abs_x != a->abs_x || b->abs_z != a->abs_z || b->level != a->level )
            group->spawn_moves++;
        break;
    }
    case EDITOR_CMD_CFG_FIELD:
        if( group->field[0] == '\0' )
            snprintf(group->field, sizeof(group->field), "%s", command->u.cfg_field.key);
        break;
    case EDITOR_CMD_CFG_BLOCK:
        if( group->field[0] == '\0' )
            snprintf(group->field, sizeof(group->field), "%s",
                command->has_after && !command->has_before ? command->u.cfg_block.new_name[0]
                                                                 ? command->u.cfg_block.new_name
                                                                 : command->u.cfg_block.name
                                                           : command->u.cfg_block.name);
        if( command->has_before && command->has_after )
            group->block_renames++;
        else if( command->has_after )
            group->block_adds++;
        else
            group->block_removes++;
        break;
    case EDITOR_CMD_IF_FIELD:
        if( group->field[0] == '\0' )
        {
            snprintf(group->field, sizeof(group->field), "%s", command->u.if_field.key);
            group->component = command->u.if_field.component;
        }
        break;
    case EDITOR_CMD_IF_COMPONENT:
        if( group->kinds[EDITOR_CMD_IF_COMPONENT] == 1 && group->field[0] == '\0' )
            group->component = command->u.if_component.component;
        if( command->has_before && command->has_after )
            group->block_renames++;
        else if( command->has_after )
            group->block_adds++;
        else
            group->block_removes++;
        break;
    case EDITOR_CMD_TEXT_SPLICE:
        break;
    }
}

/** Room for one more group at the top: the redo tail is gone already, so a
 *  full list drops its oldest. */
static struct Editor_HistoryGroup*
history_new_group(struct Editor_History* history)
{
    struct Editor_HistoryGroup* group;

    if( !history->groups )
    {
        history->groups = ToriMem_Calloc(
            ToriMem_Editor(), EDITOR_HISTORY_GROUPS_MAX, sizeof(*history->groups));
        assert(history->groups);
    }
    /* A new edit abandons the redo tail, as the authority's push does. */
    history->count = history->applied;
    if( history->count == EDITOR_HISTORY_GROUPS_MAX )
    {
        memmove(&history->groups[0], &history->groups[1],
            (size_t)(EDITOR_HISTORY_GROUPS_MAX - 1) * sizeof(*history->groups));
        history->count--;
        history->applied--;
        history->dropped++;
    }
    group = &history->groups[history->count++];
    memset(group, 0, sizeof(*group));
    snprintf(group->verb, sizeof(group->verb), "%s", history->next_verb);
    snprintf(group->label, sizeof(group->label), "%s", history->next_label);
    history->next_verb[0] = '\0';
    history->next_label[0] = '\0';
    history->applied = history->count;
    history->open = 1;
    return group;
}

/** Cut group `index` in two, the first `first_count` commands and the rest
 *  (the tail moves up one). Returns the first part's index, which moves
 *  down when a full list dropped its oldest; -1 when it could not cut. */
static int
history_split(
    struct Editor_History* history,
    int index,
    int first_count)
{
    struct Editor_HistoryGroup* groups = history->groups;
    int const total = groups[index].cmd_count;

    assert(first_count > 0);
    assert(first_count < total);
    if( history->count == EDITOR_HISTORY_GROUPS_MAX )
    {
        /* Full: lose the oldest rather than the cut. */
        if( index == 0 )
            return -1;
        memmove(&groups[0], &groups[1], (size_t)(EDITOR_HISTORY_GROUPS_MAX - 1) * sizeof(*groups));
        history->count--;
        history->applied--;
        history->dropped++;
        index--;
    }
    memmove(&groups[index + 2], &groups[index + 1],
        (size_t)(history->count - index - 1) * sizeof(*groups));
    groups[index + 1] = groups[index];
    groups[index].cmd_count = first_count;
    groups[index + 1].cmd_count = total - first_count;
    groups[index].approximate = 1;
    groups[index + 1].approximate = 1;
    history->count++;
    return index;
}

/** Fold groups [first, first + n) into one. */
static void
history_merge(
    struct Editor_History* history,
    int first,
    int n)
{
    struct Editor_HistoryGroup* groups = history->groups;
    struct Editor_HistoryGroup* into = &groups[first];

    if( n <= 1 )
        return;
    for( int k = 1; k < n; k++ )
    {
        const struct Editor_HistoryGroup* g = &groups[first + k];
        into->cmd_count += g->cmd_count;
        into->last_hash = g->last_hash;
        if( g->doc_kind != into->doc_kind || strcmp(g->doc_key, into->doc_key) != 0 || g->doc_keys > 1 )
            into->doc_keys = 2;
        for( int i = 0; i < EDITOR_CMD_KIND_COUNT; i++ )
            into->kinds[i] += g->kinds[i];
        into->raised += g->raised;
        into->lowered += g->lowered;
        into->underlay += g->underlay;
        into->overlay += g->overlay;
        into->flags += g->flags;
        into->cleared += g->cleared;
        into->loc_adds += g->loc_adds;
        into->loc_removes += g->loc_removes;
        into->loc_moves += g->loc_moves;
        into->loc_turns += g->loc_turns;
        into->spawn_adds += g->spawn_adds;
        into->spawn_removes += g->spawn_removes;
        into->spawn_edits += g->spawn_edits;
        into->spawn_moves += g->spawn_moves;
        into->suppress_adds += g->suppress_adds;
        into->suppress_removes += g->suppress_removes;
        into->block_adds += g->block_adds;
        into->block_removes += g->block_removes;
        into->block_renames += g->block_renames;
        if( g->kinds[EDITOR_CMD_TILE] )
        {
            if( g->tile_x0 < into->tile_x0 )
                into->tile_x0 = g->tile_x0;
            if( g->tile_z0 < into->tile_z0 )
                into->tile_z0 = g->tile_z0;
            if( g->tile_x1 > into->tile_x1 )
                into->tile_x1 = g->tile_x1;
            if( g->tile_z1 > into->tile_z1 )
                into->tile_z1 = g->tile_z1;
        }
    }
    into->approximate = 1;
    memmove(&groups[first + 1], &groups[first + n],
        (size_t)(history->count - first - n) * sizeof(*groups));
    history->count -= n - 1;
    if( history->applied > first )
        history->applied = history->applied >= first + n ? history->applied - (n - 1) : first + 1;
}

/**
 * A group stopped part way through an undo or a redo: the authority's group
 * was smaller than this one. Cut it where the facts stopped -- undo: the
 * applied head stays, the undone tail goes above the position; redo: the
 * redone head is applied, the rest stays above. Returns 1 when cut.
 */
static int
history_cut_replay(struct Editor_History* history)
{
    int index;

    if( history->replay < 0 )
    {
        index = history->applied - 1;
        index = history_split(history, index, history->groups[index].cmd_count - history->replay_done);
    }
    else
        index = history_split(history, history->applied, history->replay_done);
    if( index < 0 )
        return 0;
    history->applied = index + 1;
    return 1;
}

void
Editor_HistoryReplayBegin(
    struct Editor_History* history,
    int direction)
{
    assert(history);
    assert(direction == -1 || direction == 1);
    Editor_HistoryBatchEnd(history);
    history->local_replay = direction;
    history->local_groups = 0;
}

void
Editor_HistoryReplayEnd(
    struct Editor_History* history,
    int count)
{
    int direction;
    int finished;

    assert(history);
    direction = history->local_replay;
    finished = history->local_groups;
    if( direction == 0 )
        return;
    if( history->replay != 0 && history->replay_left > 0 )
    {
        /* The authority's group was smaller than this one: cut where its
         * facts stopped. */
        if( history_cut_replay(history) )
            finished++;
    }
    /* The authority's group was larger: the groups it consumed were one. */
    if( count > 0 && finished > 1 )
    {
        if( direction < 0 )
            history_merge(history, history->applied, finished);
        else
            history_merge(history, history->applied - finished, finished);
    }
    history->replay = 0;
    history->replay_left = 0;
    history->replay_done = 0;
    history->local_replay = 0;
    history->local_groups = 0;
    history->open = 0;
    history->serial++;
}

void
Editor_HistoryBatchEnd(struct Editor_History* history)
{
    assert(history);
    if( history->local_replay != 0 || history->replay == 0 || history->replay_left == 0 )
        return;
    (void)history_cut_replay(history);
    history->replay = 0;
    history->replay_left = 0;
    history->replay_done = 0;
    history->serial++;
}

void
Editor_HistoryOnFact(
    struct Editor_History* history,
    int direction,
    const struct Editor_Cmd* command)
{
    assert(history);
    assert(command);

    history->serial++;
    if( direction == EDITOR_CMD_INVERSE )
    {
        if( history->replay != -1 || history->replay_left == 0 )
        {
            /* A redo half done and now an undo: the redo stopped there. */
            if( history->replay == 1 && history->replay_left > 0 )
                Editor_HistoryBatchEnd(history);
            /* Undoing something before this window's first sight: nothing
             * here to move. */
            if( history->applied == 0 )
                return;
            history->replay = -1;
            history->replay_left = history->groups[history->applied - 1].cmd_count;
            history->replay_done = 0;
        }
        history->replay_left--;
        history->replay_done++;
        if( history->replay_left == 0 )
        {
            history->applied--;
            history->replay = 0;
            history->replay_done = 0;
            history->local_groups++;
        }
        history->open = 0;
        return;
    }

    /* Forward: this window's Redo, another window's Redo, or a new edit. */
    if( history->replay == 1 && history->replay_left > 0 )
    {
        history->replay_left--;
        history->replay_done++;
        if( history->replay_left == 0 )
        {
            history->applied++;
            history->replay = 0;
            history->replay_done = 0;
            history->local_groups++;
        }
        return;
    }
    if( history->replay == -1 && history->replay_left > 0 )
        Editor_HistoryBatchEnd(history);
    if( history->applied < history->count
        && (history->local_replay > 0
            || (history->local_replay == 0 && !history->open
                && Editor_CmdHash(command) == history->groups[history->applied].first_hash)) )
    {
        history->replay = 1;
        history->replay_left = history->groups[history->applied].cmd_count - 1;
        history->replay_done = 1;
        if( history->replay_left == 0 )
        {
            history->applied++;
            history->replay = 0;
            history->replay_done = 0;
            history->local_groups++;
        }
        return;
    }
    if( !history->open || history->count == 0 || history->applied < history->count )
        (void)history_new_group(history);
    history_tally(&history->groups[history->count - 1], command);
    history->groups[history->count - 1].cmd_count++;
}

int
Editor_HistoryCommandsThrough(
    const struct Editor_History* history,
    int groups)
{
    int total = 0;

    assert(history);
    assert(groups >= 0);
    assert(groups <= history->count);
    for( int i = 0; i < groups; i++ )
        total += history->groups[i].cmd_count;
    return total;
}

int
Editor_HistoryAppliedCommands(const struct Editor_History* history)
{
    assert(history);
    return Editor_HistoryCommandsThrough(history, history->applied);
}

const char*
Editor_HistoryGroupName(
    const struct Editor_HistoryGroup* group,
    char* out,
    int out_size)
{
    char verb[48];
    char what[128];
    char doc[EDITOR_DOC_KEY_MAX + 8];
    int kinds_used = 0;
    int main_kind = EDITOR_CMD_TILE;

    assert(group);
    assert(out);
    assert(out_size > 0);

    if( group->label[0] )
    {
        snprintf(out, (size_t)out_size, "%s", group->label);
        return out;
    }
    for( int i = 0; i < EDITOR_CMD_KIND_COUNT; i++ )
        if( group->kinds[i] )
        {
            if( kinds_used == 0 )
                main_kind = i;
            kinds_used++;
        }
    snprintf(doc, sizeof(doc), "%s%s", group->doc_key, group->doc_keys > 1 ? " +more" : "");
    verb[0] = '\0';
    what[0] = '\0';

    switch( (enum Editor_CmdKind)main_kind )
    {
    case EDITOR_CMD_TILE:
    {
        int const n = group->kinds[EDITOR_CMD_TILE];
        int const w = group->tile_x1 - group->tile_x0 + 1;
        int const h = group->tile_z1 - group->tile_z0 + 1;
        int const heights = group->raised + group->lowered;
        int const only_height = !group->underlay && !group->overlay && !group->flags;
        const char* derived = "Edit";

        if( group->cleared == n )
            derived = "Clear";
        else if( only_height && group->raised && !group->lowered )
            derived = "Raise";
        else if( only_height && group->lowered && !group->raised )
            derived = "Lower";
        else if( only_height )
            derived = "Height";
        else if( !heights && group->underlay && !group->overlay && !group->flags )
            derived = "Underlay";
        else if( !heights && group->overlay && !group->underlay && !group->flags )
            derived = "Overlay";
        else if( !heights && group->flags && !group->underlay && !group->overlay )
            derived = "Flags";
        else if( n == w * h && n > 1 )
            derived = "Paste";
        snprintf(verb, sizeof(verb), "%s", derived);
        if( n == w * h && w > 1 && h > 1 )
            snprintf(what, sizeof(what), "%dx%d %s L%d", w, h, doc, group->tile_level);
        else
            snprintf(what, sizeof(what), "%d tile%s %s L%d", n, n == 1 ? "" : "s", doc, group->tile_level);
        break;
    }
    case EDITOR_CMD_LOC:
    {
        int const changes = group->loc_moves + group->loc_turns;
        int const n = group->kinds[EDITOR_CMD_LOC];
        if( group->loc_adds && !group->loc_removes && !changes )
            snprintf(verb, sizeof(verb), "Place");
        else if( group->loc_removes && !group->loc_adds && !changes )
            snprintf(verb, sizeof(verb), "Delete");
        else if( group->loc_moves || (group->loc_adds && group->loc_adds == group->loc_removes) )
            snprintf(verb, sizeof(verb), "Move");
        else if( group->loc_turns )
            snprintf(verb, sizeof(verb), "Turn");
        else
            snprintf(verb, sizeof(verb), "Edit");
        if( n == 1 || (n == 2 && group->loc_adds == 1 && group->loc_removes == 1) )
            snprintf(what, sizeof(what), "loc %d %s", group->loc_id, doc);
        else
            snprintf(what, sizeof(what), "%d locs %s", group->loc_adds + changes > 0 ? group->loc_adds + changes : n, doc);
        break;
    }
    case EDITOR_CMD_SPAWN:
    case EDITOR_CMD_SPAWN_SUPPRESS:
    {
        int const n = group->spawn_adds + group->spawn_removes;
        if( main_kind == EDITOR_CMD_SPAWN_SUPPRESS && !n )
            snprintf(verb, sizeof(verb), "%s", group->suppress_adds ? "Suppress" : "Restore");
        else if( group->spawn_adds && group->spawn_adds == group->spawn_removes )
            snprintf(verb, sizeof(verb), "Move spawn");
        else if( group->spawn_adds && !group->spawn_removes )
            snprintf(verb, sizeof(verb), "Spawn");
        else if( group->spawn_removes && !group->spawn_adds )
            snprintf(verb, sizeof(verb), "Delete spawn");
        else
            snprintf(verb, sizeof(verb), "Edit spawns");
        if( n > 2 || group->suppress_adds + group->suppress_removes > 1 )
            snprintf(what, sizeof(what), "%d rows %s", n + group->suppress_adds + group->suppress_removes, doc);
        else
            snprintf(what, sizeof(what), "%s %s %d,%d", group->spawn_is_obj ? "obj" : "npc", group->name,
                group->tile_x0, group->tile_z0);
        break;
    }
    case EDITOR_CMD_SPAWN_EDIT:
    {
        int const n = group->spawn_edits;
        snprintf(verb, sizeof(verb), "%s",
            group->spawn_moves == n ? (n == 1 ? "Move spawn" : "Move spawns") : n == 1 ? "Edit spawn" : "Edit spawns");
        if( n > 1 )
            snprintf(what, sizeof(what), "%d rows %s", n, doc);
        else
            snprintf(what, sizeof(what), "%s %s %d,%d", group->spawn_is_obj ? "obj" : "npc", group->name,
                group->tile_x0, group->tile_z0);
        break;
    }
    case EDITOR_CMD_CFG_FIELD:
        snprintf(verb, sizeof(verb), "Cfg");
        if( group->kinds[EDITOR_CMD_CFG_FIELD] == 1 )
            snprintf(what, sizeof(what), "%s.%s", doc, group->field);
        else
            snprintf(what, sizeof(what), "%s (%d fields)", doc, group->kinds[EDITOR_CMD_CFG_FIELD]);
        break;
    case EDITOR_CMD_CFG_BLOCK:
        snprintf(verb, sizeof(verb), "Cfg %s",
            group->block_renames ? "rename" : group->block_adds ? "add" : "remove");
        snprintf(what, sizeof(what), "[%s] %s", group->field, doc);
        break;
    case EDITOR_CMD_IF_FIELD:
        snprintf(verb, sizeof(verb), "Iface");
        snprintf(what, sizeof(what), "%s %d.%s", doc, group->component, group->field);
        break;
    case EDITOR_CMD_IF_COMPONENT:
        snprintf(verb, sizeof(verb), "Iface %s",
            group->block_renames ? "move" : group->block_adds ? "add" : "remove");
        snprintf(what, sizeof(what), "component %d %s", group->component, doc);
        break;
    case EDITOR_CMD_TEXT_SPLICE:
        snprintf(verb, sizeof(verb), "Edit text");
        snprintf(what, sizeof(what), "%s (%d splice%s)", doc, group->kinds[EDITOR_CMD_TEXT_SPLICE],
            group->kinds[EDITOR_CMD_TEXT_SPLICE] == 1 ? "" : "s");
        break;
    }
    if( group->verb[0] )
        snprintf(verb, sizeof(verb), "%s", group->verb);
    if( kinds_used > 1 )
        snprintf(out, (size_t)out_size, "%s %s + %d %s", verb, what, group->cmd_count - group->kinds[main_kind],
            "more");
    else
        snprintf(out, (size_t)out_size, "%s %s", verb, what);
    return out;
}
