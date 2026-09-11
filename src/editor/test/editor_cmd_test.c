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
    command.kind = EDITOR_CMD_TILE;
    command.map_x = 50;
    command.map_z = 50;
    command.level = 0;
    command.x = 10;
    command.z = 12;
    command.tile_before = square->tiles[index];
    command.tile_after = square->tiles[index];
    command.tile_after.has_height = 1;
    command.tile_after.height = 42;

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
        command.kind = EDITOR_CMD_TILE;
        command.map_x = 50;
        command.map_z = 50;
        command.x = i;
        command.tile_before = square->tiles[index];
        command.tile_after = square->tiles[index];
        command.tile_after.underlay_id = 9;

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
    command.kind = EDITOR_CMD_LOC;
    command.map_x = 50;
    command.map_z = 50;
    command.has_after = 1;
    command.loc_after.loc_id = 1234;
    command.loc_after.shape = 10;
    command.loc_after.rotation = 2;
    command.loc_after.x = 5;
    command.loc_after.z = 6;

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
    command.kind = EDITOR_CMD_TILE;
    command.map_x = 50;
    command.map_z = 50;

    /* Interior height edit: nothing else needs rebuilding. */
    command.x = 32;
    command.z = 32;
    command.tile_after.has_height = 1;
    command.tile_after.height = 5;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 1, "interior edit rebuilds one square");

    /* Height edit on the west edge: the neighbour shares that corner. */
    command.x = 0;
    command.z = 32;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 2, "edge height edit rebuilds the neighbour too");

    /* Underlay edit four tiles in: inside the blend window, so it still
     * reaches across even though the tile is not on the border. */
    memset(&command.tile_after, 0, sizeof(command.tile_after));
    memset(&command.tile_before, 0, sizeof(command.tile_before));
    command.tile_after.underlay_id = 12;
    command.x = 4;
    command.z = 32;
    count = Editor_CmdRebuildSpan(&command, coords, 16);
    check(count == 2, "underlay edit near the seam rebuilds the neighbour");

    /* A corner touches three other squares. */
    command.x = 0;
    command.z = 0;
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

/** The server's spawn sections survive a load/save cycle untouched. */
static void
test_foreign_sections_survive(void)
{
    struct Editor_Square square;
    const char* source = "==== MAP ====\n"
                         "0 0 0: h5 u2\n"
                         "\n==== NPC ====\n"
                         "0 10 10: 3021\n"
                         "==== OBJ ====\n"
                         "0 11 11: 995 100\n";
    char* text;
    size_t size;

    Editor_SquareInit(&square, 50, 50);
    Editor_Jm2Parse(&square, source, strlen(source));

    check(square.foreign != NULL, "foreign sections were captured");
    check(strstr(square.foreign, "3021") != NULL, "npc spawn kept");
    check(strstr(square.foreign, "995 100") != NULL, "obj spawn kept");

    size = Editor_Jm2Emit(&square, NULL, 0);
    text = malloc(size + 1);
    Editor_Jm2Emit(&square, text, size + 1);

    check(strcmp(text, source) == 0, "square with foreign sections round-trips exactly");

    free(text);
    Editor_SquareFree(&square);
}

int
main(void)
{
    test_undo_restores_procedural_absence();
    test_emit_does_not_bake_procedural_heights();
    test_stroke_undoes_as_one_step();
    test_loc_commands();
    test_rebuild_span_crosses_borders();
    test_parse_reports_bad_input();
    test_foreign_sections_survive();

    printf("editor_cmd_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
