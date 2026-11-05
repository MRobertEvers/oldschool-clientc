/**
 * Document and command-layer behaviour.
 *
 * The cases here are the ones where an editor quietly does the wrong thing and
 * nobody notices until the diff: undo restoring a *value* where the file had
 * an absence, a stroke undoing one tile at a time, an edit at a square's edge
 * repainting only its own side of the seam.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_jm2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static struct Editor_Doc g_doc;

static struct Editor_Square*
open_square(void)
{
    struct Editor_Square* square;

    memset(&g_doc, 0, sizeof(g_doc));
    square = Editor_DocOpenSquare(&g_doc, 50, 50);
    return square;
}

/**
 * The case the whole authored/derived split exists for.
 *
 * A tile with no `h` token takes its height from the noise routine. Editing it
 * makes it authored; undoing has to give the tile back to the noise routine,
 * not freeze it at whatever height the renderer happened to resolve. A design
 * that snapshots the derived value cannot express the difference, and the tile
 * silently gains an `h` token it never had.
 */
static void
test_undo_restores_procedural_absence(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_Cmd command;
    struct Editor_UndoStack undo;
    const struct Editor_Tile* tile;
    int index = Editor_TileIndex(10, 12, 0);

    Editor_UndoInit(&undo);

    check(square->tiles[index].has_height == 0, "tile starts with no authored height");

    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
    command.u.tile.level = 0;
    command.u.tile.x = 10;
    command.u.tile.z = 12;
    command.u.tile.before = square->tiles[index];
    command.u.tile.after = square->tiles[index];
    command.u.tile.after.has_height = 1;
    command.u.tile.after.height = 42;

    Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD);
    Editor_UndoPush(&undo, &command);

    tile = &square->tiles[index];
    check(tile->has_height == 1 && tile->height == 42, "edit authors the height");
    check(square->dirty_map == 1, "edit marks the square dirty for save");

    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);

    tile = &square->tiles[index];
    check(tile->has_height == 0, "undo restores absence, not a frozen height");

    Editor_UndoRedo(&undo, &g_doc, NULL, NULL);
    tile = &square->tiles[index];
    check(tile->has_height == 1 && tile->height == 42, "redo re-authors the height");

    Editor_DocFree(&g_doc);
}

/** An emitted square must not gain an `h` token for a tile that never had one. */
static void
test_emit_does_not_bake_procedural_heights(void)
{
    struct Editor_Square* square = open_square();
    char* text;
    size_t size;

    /* One authored tile among 16k procedural ones. */
    square->tiles[Editor_TileIndex(1, 1, 0)].has_height = 1;
    square->tiles[Editor_TileIndex(1, 1, 0)].height = 7;
    square->tiles[Editor_TileIndex(2, 2, 0)].underlay_id = 5;

    size = Editor_Jm2Emit(square, NULL, 0);
    text = malloc(size + 1);
    Editor_Jm2Emit(square, text, size + 1);

    check(strstr(text, "0 1 1: h7") != NULL, "authored height is emitted");
    check(strstr(text, "0 2 2: u5") != NULL, "underlay-only tile is emitted");
    /* Exactly one `h` in the file: the tile that has one. */
    {
        int count = 0;
        for( const char* p = text; *p; p++ )
            if( *p == 'h' )
                count++;
        check(count == 1, "no other tile gained a height token");
    }

    free(text);
    Editor_DocFree(&g_doc);
}

/** A stroke is one undo step, however many tiles it touched. */
static void
test_stroke_undoes_as_one_step(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    int reverted;

    Editor_UndoInit(&undo);
    Editor_UndoStrokeBegin(&undo);
    for( int i = 0; i < 8; i++ )
    {
        struct Editor_Cmd command;
        int index = Editor_TileIndex(i, 0, 0);

        memset(&command, 0, sizeof(command));
        Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
        command.u.tile.x = i;
        command.u.tile.before = square->tiles[index];
        command.u.tile.after = square->tiles[index];
        command.u.tile.after.underlay_id = 9;

        Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD);
        Editor_UndoPush(&undo, &command);
    }
    Editor_UndoStrokeEnd(&undo);

    check(square->tiles[Editor_TileIndex(7, 0, 0)].underlay_id == 9, "stroke painted");

    reverted = Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    check(reverted == 8, "the whole stroke reverted in one undo");
    check(square->tiles[Editor_TileIndex(0, 0, 0)].underlay_id == 0, "first tile reverted");
    check(square->tiles[Editor_TileIndex(7, 0, 0)].underlay_id == 0, "last tile reverted");

    Editor_DocFree(&g_doc);
}

/** Loc add / delete / move round-trip through the command path. */
static void
test_loc_commands(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    struct Editor_Cmd command;

    Editor_UndoInit(&undo);

    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_after = 1;
    command.u.loc.after.loc_id = 1234;
    command.u.loc.after.shape = 10;
    command.u.loc.after.rotation = 2;
    command.u.loc.after.x = 5;
    command.u.loc.after.z = 6;

    Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD);
    Editor_UndoPush(&undo, &command);
    check(square->loc_count == 1, "loc added");
    check(square->dirty_loc == 1, "loc edit dirties the loc half");
    check(square->dirty_map == 0, "loc edit leaves the terrain half clean");

    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    check(square->loc_count == 0, "undo removed the loc");

    Editor_UndoRedo(&undo, &g_doc, NULL, NULL);
    check(square->loc_count == 1, "redo re-added the loc");

    /* A tile holds several locs at once in different layers; find must key on
     * shape or an edit to a wall would hit its decoration. */
    {
        struct Editor_Loc decor = { 999, 4, 0, 0, 5, 6 };
        Editor_SquareLocAdd(square, &decor);
        check(Editor_SquareLocFind(square, 0, 5, 6, 10) == 0, "finds the centrepiece");
        check(Editor_SquareLocFind(square, 0, 5, 6, 4) == 1, "finds the decor beside it");
        check(Editor_SquareLocFind(square, 0, 5, 6, 22) == -1, "absent shape is not found");
    }

    Editor_DocFree(&g_doc);
}

/* ---- locs: delete, undo, redo ------------------------------------------------
 *
 * The .jl2 the authority would save after an undo must be the file it held
 * before the edit, byte for byte: an undo that leaves a placement moved to the
 * end of the list is a diff the user never asked for, on every save after it.
 * The content tree's files are in cache order -- loc id, then level, x, z,
 * then shape -- which is what an undone delete has to land back in. */

static char*
emit_jl2(const struct Editor_Square* square)
{
    size_t size = Editor_Jl2Emit(square, NULL, 0);
    char* text = malloc(size + 1);
    if( !text )
        abort();
    Editor_Jl2Emit(square, text, size + 1);
    return text;
}

static struct Editor_Cmd
loc_delete_cmd(const struct Editor_Loc* loc)
{
    struct Editor_Cmd command;
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_before = 1;
    command.u.loc.before = *loc;
    return command;
}

static const char k_sorted_jl2[] = "==== LOC ====\n"
                                   "0 18 17: 85 0 2\n"
                                   "0 19 17: 85 0 1\n"
                                   "0 19 17: 85 4 1\n"
                                   "0 12 15: 114 10 2\n"
                                   "0 19 17: 179 22 3\n"
                                   "0 28 55: 179 22\n";

static void
test_loc_delete_undo_keeps_file_order(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    struct Editor_Cmd command;
    char* original;
    char* deleted;
    char* text;

    Editor_UndoInit(&undo);
    square->loaded = 1;
    Editor_Jl2Parse(square, k_sorted_jl2, strlen(k_sorted_jl2));
    original = emit_jl2(square);
    check(strcmp(original, k_sorted_jl2) == 0, "loc order: the sorted .jl2 round-trips");

    /* One delete from the middle of the list, undone. */
    command = loc_delete_cmd(&square->locs[1]);
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "loc order: the delete applies");
    Editor_UndoPush(&undo, &command);
    deleted = emit_jl2(square);
    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    text = emit_jl2(square);
    check(strcmp(text, original) == 0, "loc order: an undone delete is back where it stood, byte for byte");
    if( strcmp(text, original) != 0 )
        fprintf(stderr, "---- after undo:\n%s---- expected:\n%s", text, original);
    free(text);
    Editor_UndoRedo(&undo, &g_doc, NULL, NULL);
    text = emit_jl2(square);
    check(strcmp(text, deleted) == 0, "loc order: the redo deletes it again");
    free(text);
    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);

    /* Clear tile: every layer on (19, 17) in list order, one stroke, undone. */
    Editor_UndoStrokeBegin(&undo);
    for( int i = 0; i < square->loc_count; )
    {
        if( square->locs[i].x == 19 && square->locs[i].z == 17 )
        {
            command = loc_delete_cmd(&square->locs[i]);
            check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "loc order: a clear-tile delete applies");
            Editor_UndoPush(&undo, &command);
            continue;
        }
        i++;
    }
    Editor_UndoStrokeEnd(&undo);
    check(square->loc_count == 3, "loc order: clear tile took the three locs on (19, 17)");
    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == 3, "loc order: one undo step brings all three back");
    text = emit_jl2(square);
    check(strcmp(text, original) == 0, "loc order: an undone clear tile restores the file byte for byte");
    if( strcmp(text, original) != 0 )
        fprintf(stderr, "---- after undo:\n%s---- expected:\n%s", text, original);
    free(text);

    /* A new placement lands in cache order too: an add then an undo is the
     * file it was. */
    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_after = 1;
    command.u.loc.after.loc_id = 100;
    command.u.loc.after.shape = 10;
    command.u.loc.after.x = 3;
    command.u.loc.after.z = 4;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "loc order: an add applies");
    check(square->locs[3].loc_id == 100, "loc order: the added loc sits in cache order (after the 85s, before 114)");
    Editor_CmdApply(&g_doc, &command, EDITOR_CMD_INVERSE);
    text = emit_jl2(square);
    check(strcmp(text, original) == 0, "loc order: the add undone leaves the file as it was");
    free(text);

    free(deleted);
    free(original);
    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
}

/* A delete names a placement exactly. One that is not there (another window
 * deleted it first, or a stale selection) must change nothing and say so: the
 * authority that accepted it would record it, and its undo would then ADD a
 * loc that was never deleted -- a duplicate. */
static void
test_loc_delete_of_absent_refused(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    struct Editor_Cmd command;
    struct Editor_Loc wall = { 85, 0, 1, 0, 19, 17 };
    struct Editor_Loc other = { 86, 0, 1, 0, 19, 17 };

    Editor_UndoInit(&undo);
    square->loaded = 1;
    Editor_Jl2Parse(square, k_sorted_jl2, strlen(k_sorted_jl2));
    square->dirty_loc = 0;

    command = loc_delete_cmd(&wall);
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "absent: the first delete applies");
    Editor_UndoPush(&undo, &command);
    square->dirty_loc = 0;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 0, "absent: the same delete again is refused");
    check(square->loc_count == 5, "absent: and changed nothing");
    check(square->dirty_loc == 0, "absent: nor dirtied the square");
    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    check(square->loc_count == 6, "absent: one undo brings back one wall, no duplicate");

    /* Same tile and shape, a different loc: not this placement. */
    command = loc_delete_cmd(&other);
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 0,
        "absent: a delete naming another loc id at an occupied tile+shape is refused");
    check(Editor_SquareLocFind(square, 0, 19, 17, 0) >= 0, "absent: the wall that is there stays");

    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
}

/* (level, x, z, shape) names one placement -- no square in the content tree
 * holds two -- and every by-key path relies on it: the undo of an add, the
 * selection's resolve, the world's placement lookup. An add onto a taken key
 * is refused; the Place tool sends a replace there instead, whose undo puts
 * the occupant back. */
static void
test_loc_add_onto_taken_key(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    struct Editor_Cmd command;
    struct Editor_Loc tree = { 100, 10, 0, 0, 5, 6 };
    struct Editor_Loc rock = { 200, 10, 1, 0, 5, 6 };
    struct Editor_Loc decor = { 300, 22, 0, 0, 5, 6 };
    int index;

    Editor_UndoInit(&undo);
    square->loaded = 1;
    Editor_SquareLocAdd(square, &tree);

    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_after = 1;
    command.u.loc.after = rock;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 0,
        "taken key: an add onto a tile+shape already placed is refused");
    check(square->loc_count == 1, "taken key: and leaves the tile as it was");

    command.u.loc.after = decor;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "taken key: another layer on the tile is fine");
    Editor_UndoPush(&undo, &command);

    /* The replace the Place tool sends: the tree goes, the rock comes. */
    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, 50, 50);
    command.has_before = 1;
    command.has_after = 1;
    command.u.loc.before = tree;
    command.u.loc.after = rock;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD) == 1, "taken key: the replace applies");
    Editor_UndoPush(&undo, &command);
    index = Editor_SquareLocFind(square, 0, 5, 6, 10);
    check(index >= 0 && square->locs[index].loc_id == 200, "taken key: the rock stands there");
    check(square->loc_count == 2, "taken key: one centrepiece and the decor");
    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    index = Editor_SquareLocFind(square, 0, 5, 6, 10);
    check(index >= 0 && square->locs[index].loc_id == 100 && square->locs[index].rotation == 0,
        "taken key: undo puts the tree back, not the rock");
    check(square->loc_count == 2, "taken key: and nothing else moved");
    Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    check(square->loc_count == 1 && Editor_SquareLocFind(square, 0, 5, 6, 22) < 0,
        "taken key: the second undo takes the decor");

    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
}

/**
 * Edits at a square's edge must repaint the neighbour.
 *
 * Heights reach one tile past the border (the shared corner); underlay colour
 * reaches as far as the blend window. An editor that rebuilds only the edited
 * square leaves a visible seam.
 */
static void
test_rebuild_span_crosses_borders(void)
{
    struct Editor_Cmd command;
    int coords[32];
    int count;

    memset(&command, 0, sizeof(command));
    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);

    /* Interior height edit: nothing else needs rebuilding. */
    command.u.tile.x = 32;
    command.u.tile.z = 32;
    command.u.tile.after.has_height = 1;
    command.u.tile.after.height = 5;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 1, "interior edit rebuilds one square");

    /* Height edit on the west edge: the neighbour shares that corner. */
    command.u.tile.x = 0;
    command.u.tile.z = 32;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 2, "edge height edit rebuilds the neighbour too");

    /* Underlay edit four tiles in: inside the blend window, so it still
     * reaches across even though the tile is not on the border. */
    memset(&command.u.tile.after, 0, sizeof(command.u.tile.after));
    memset(&command.u.tile.before, 0, sizeof(command.u.tile.before));
    command.u.tile.after.underlay_id = 12;
    command.u.tile.x = 4;
    command.u.tile.z = 32;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 2, "underlay edit near the seam rebuilds the neighbour");

    /* A corner touches three other squares. */
    command.u.tile.x = 0;
    command.u.tile.z = 0;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 4, "corner edit rebuilds three neighbours");

    /* A loc is local however close to the edge it sits. */
    command.kind = EDITOR_CMD_LOC;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 1, "loc edit rebuilds only its own square");
}

/** Parse rejects malformed input as data rather than aborting. */
static void
test_parse_reports_bad_input(void)
{
    struct Editor_Square square;
    struct Editor_ParseResult result;
    const char* no_header = "0 0 0: h5 u2\n";
    const char* bad_coord = "==== MAP ====\n0 99 0: h5\n";
    const char* bad_token = "==== MAP ====\n0 1 1: q7\n";

    Editor_SquareInit(&square, 50, 50);

    result = Editor_Jm2Parse(&square, no_header, strlen(no_header));
    check(result.status == EDITOR_PARSE_BAD_HEADER, "missing header is reported");

    result = Editor_Jm2Parse(&square, bad_coord, strlen(bad_coord));
    check(result.status == EDITOR_PARSE_BAD_COORD, "out-of-range coord is reported");
    check(result.line == 2, "the offending line is named");

    result = Editor_Jm2Parse(&square, bad_token, strlen(bad_token));
    check(result.status == EDITOR_PARSE_BAD_TOKEN, "unknown token is reported");

    Editor_SquareFree(&square);
}

/** A section the editor does not own survives a load/save cycle untouched.
 *  Spawns are not such a section: they live in `.spawn` files, and cachepack
 *  refuses a jm2 that carries one. */
static void
test_foreign_sections_survive(void)
{
    struct Editor_Square square;
    const char* source = "==== MAP ====\n"
                         "0 0 0: h5 u2\n"
                         "\n==== NOTE ====\n"
                         "0 10 10: kept as written\n"
                         "second line\n";
    char* text;
    size_t size;

    Editor_SquareInit(&square, 50, 50);
    Editor_Jm2Parse(&square, source, strlen(source));

    check(square.foreign != NULL, "foreign sections were captured");
    check(strstr(square.foreign, "kept as written") != NULL, "first foreign line kept");
    check(strstr(square.foreign, "second line") != NULL, "second foreign line kept");

    size = Editor_Jm2Emit(&square, NULL, 0);
    text = malloc(size + 1);
    Editor_Jm2Emit(&square, text, size + 1);

    check(strcmp(text, source) == 0, "square with foreign sections round-trips exactly");

    free(text);
    Editor_SquareFree(&square);
}

/**
 * A wall run's release (workbench 6.3, edpanel_ops.h Editor_LocRunCommands)
 * is one stroke of LOC commands: adds on empty tiles, and where the run turns
 * on an end of an earlier run of the same loc, a REMOVE of the straight piece
 * and an ADD of the L on its tile (two keys: shape 0 and shape 2). One undo
 * must take the whole run back to the file it was, byte for byte -- the
 * straight end restored in its place in the list.
 */
static void
test_run_stroke_undoes_whole(void)
{
    struct Editor_Square* square = open_square();
    struct Editor_UndoStack undo;
    struct Editor_Loc const old_end = { 1902, 0, 1, 0, 14, 11 };
    struct Editor_Loc const other = { 85, 10, 0, 0, 2, 2 };
    char* before;
    char* after;
    int reverted;

    Editor_UndoInit(&undo);
    Editor_SquareLocAdd(square, &other);
    Editor_SquareLocAdd(square, &old_end);
    before = emit_jl2(square);

    Editor_UndoStrokeBegin(&undo);
    {
        struct Editor_Cmd commands[4];
        struct Editor_Loc const l_piece = { 1902, 2, 1, 0, 14, 11 };
        struct Editor_Loc const down1 = { 1902, 0, 2, 0, 14, 10 };
        struct Editor_Loc const down2 = { 1902, 0, 2, 0, 14, 9 };

        commands[0] = loc_delete_cmd(&old_end);
        for( int i = 1; i < 4; i++ )
        {
            Editor_CmdInitSquare(&commands[i], EDITOR_CMD_LOC, 50, 50);
            commands[i].has_after = 1;
        }
        commands[1].u.loc.after = l_piece;
        commands[2].u.loc.after = down1;
        commands[3].u.loc.after = down2;
        for( int i = 0; i < 4; i++ )
        {
            Editor_CmdApply(&g_doc, &commands[i], EDITOR_CMD_FORWARD);
            Editor_UndoPush(&undo, &commands[i]);
        }
    }
    Editor_UndoStrokeEnd(&undo);
    check(square->loc_count == 4 && Editor_SquareLocFind(square, 0, 14, 11, 0) < 0
              && Editor_SquareLocFind(square, 0, 14, 11, 2) >= 0,
        "run: the straight end is off its key and the L on its own");

    reverted = Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
    after = emit_jl2(square);
    check(reverted == 4, "run: one undo reverts the whole run");
    check(strcmp(before, after) == 0, "run: and the .jl2 is the file before it, byte for byte");
    free(after);
    check(Editor_UndoRedo(&undo, &g_doc, NULL, NULL) == 4 && Editor_SquareLocFind(square, 0, 14, 11, 2) >= 0,
        "run: redo puts the L back");

    free(before);
    Editor_DocFree(&g_doc);
}

int
main(void)
{
    test_undo_restores_procedural_absence();
    test_emit_does_not_bake_procedural_heights();
    test_stroke_undoes_as_one_step();
    test_loc_commands();
    test_loc_delete_undo_keeps_file_order();
    test_loc_delete_of_absent_refused();
    test_loc_add_onto_taken_key();
    test_rebuild_span_crosses_borders();
    test_parse_reports_bad_input();
    test_foreign_sections_survive();
    test_run_stroke_undoes_whole();

    printf("editor_cmd_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
