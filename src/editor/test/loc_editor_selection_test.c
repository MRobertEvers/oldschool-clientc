/*
 * What the loc editor is pointed at, and what moving it means.
 *
 * These are editing rules, so the cost of getting one wrong is a map that was
 * saved with the wrong thing in it. A move recorded from the tile it landed on
 * replays as nothing. A selection lost on a panel toggle makes every
 * multi-step edit a re-selection. A Reselect that reads the live hover targets
 * whatever the panel is sitting on top of, because reaching the menu row moved
 * the cursor there.
 */

#include "editor/loc_editor_selection.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** A 2x1 door at (40, 50) on the FIRST FLOOR, facing west. */
static struct LocEditorLocPlacement
a_door(void)
{
    struct LocEditorLocPlacement placement;
    placement.loc_id = 1234;
    placement.shape = 10;
    placement.angle = 0;
    placement.size_x = 2;
    placement.size_z = 1;
    placement.interactive = 1;
    placement.name = "Door";
    placement.scene_x = 40;
    placement.scene_z = 50;
    /* Not level 0: the ground floor is what an unrecorded level defaults to,
     * so a fixture standing on it cannot tell the two apart. */
    placement.level = 2;
    return placement;
}

static void
test_a_fresh_selection_is_pointed_at_nothing(void)
{
    struct LocEditorSelection selection;
    int x;
    int z;

    memset(&selection, 0x2b, sizeof(selection));
    LocEditorSelection_Reset(&selection);
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_NONE, "nothing is selected");
    TEST_ASSERT(!LocEditorSelection_HoverTile(&selection, &x, &z), "and nothing has been hovered");
}

static void
test_selecting_a_loc_takes_its_whole_placement(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_LOC, "a loc is up");
    TEST_ASSERT(selection.loc_id == 1234, "the loc's id");
    TEST_ASSERT(selection.shape == 10, "its shape");
    TEST_ASSERT(selection.angle == 0, "its angle");
    TEST_ASSERT(selection.size_x == 2 && selection.size_z == 1, "its footprint");
    TEST_ASSERT(selection.interactive == 1, "whether it can be interacted with");
    TEST_ASSERT(strcmp(selection.name, "Door") == 0, "and its name");
    TEST_ASSERT(selection.scene_x == 40 && selection.scene_z == 50, "at the tile it stands on");
    TEST_ASSERT(selection.level == 2, "on the level it stands on");
}

static void
test_an_unnamed_loc_selects_without_a_name(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement placement = a_door();

    /* Plenty of authored locs carry no name. The panel's extra row falls back
     * to the interactive flag for those, and it can only do that if what it
     * reads is an empty string rather than a pointer nobody set. */
    placement.name = NULL;
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(selection.name[0] == '\0', "an unnamed loc has an empty name, not a stale one");
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_LOC, "and is still selected");
}

static void
test_a_long_name_is_bounded(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement placement = a_door();
    char long_name[512];

    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    placement.name = long_name;
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(strlen(selection.name) == LOC_EDITOR_NAME_LEN - 1, "a long name is truncated");
    TEST_ASSERT(selection.name[LOC_EDITOR_NAME_LEN - 1] == '\0', "and stays a string");
}

static void
test_the_two_subjects_are_exclusive(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();

    /* One panel, one set of rows. Leave the old subject behind and the readout
     * describes a loc while the nudge buttons move a tile. */
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectTile(&selection, 12, 13, 1);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_LOC,
        "picking a loc drops the tile");

    LocEditorSelection_SelectTile(&selection, 12, 13, 1);
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_TILE,
        "picking a tile drops the loc");
    TEST_ASSERT(selection.loc_id < 0, "and leaves no loc id behind it");
}

static void
test_a_tile_selection_names_the_ground(void)
{
    struct LocEditorSelection selection;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectTile(&selection, 12, 13, 2);
    TEST_ASSERT(selection.scene_x == 12 && selection.scene_z == 13, "at the tile picked");
    /* The cache (mesh) level is the plane the map AUTHORED the floor on, which
     * on a bridge deck is not the plane it draws at. Both are kept because the
     * panel reads the authored one and everything else reads the draw one. */
    TEST_ASSERT(selection.terrain_level == 2, "on the plane the map authored it on");
    TEST_ASSERT(selection.level == 2, "which is where the panel edits");
}

static void
test_deselect_drops_whichever_subject_is_up(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    LocEditorSelection_Clear(&selection);
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_NONE, "a loc is dropped");

    /* One row, both subjects -- or Deselect appears to do nothing at all while
     * a tile is up, which reads as a dead button. */
    LocEditorSelection_SelectTile(&selection, 12, 13, 1);
    LocEditorSelection_Clear(&selection);
    TEST_ASSERT(
        LocEditorSelection_Subject(&selection) == LOC_EDITOR_SUBJECT_NONE, "and so is a tile");
}

static void
test_a_nudge_reports_both_ends(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();
    struct LocEditorMove move;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(LocEditorSelection_Nudge(&selection, 1, 0, &move), "a selected loc moves");
    /* The authored record needs where it came FROM as well as where it went.
     * Reconstructed after the fact, the recorded edit describes a move from
     * the tile it landed on to itself, which replays as nothing. */
    TEST_ASSERT(move.from_x == 40 && move.from_z == 50, "the move starts where the loc was");
    TEST_ASSERT(move.to_x == 41 && move.to_z == 50, "and ends one tile east");
    TEST_ASSERT(move.loc_id == 1234 && move.shape == 10, "carrying what is being moved");
    TEST_ASSERT(move.level == 2, "on the level it is on");
    /* A nudge turns nothing. Both angles are reported so the caller places
     * with one field instead of choosing between two. */
    TEST_ASSERT(move.from_angle == 0 && move.to_angle == 0, "and turning it no further");
}

static void
test_a_nudge_advances_the_placement(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();
    struct LocEditorMove move;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    LocEditorSelection_Nudge(&selection, 1, 0, &move);
    /* The next nudge has to start from where the last one finished, or holding
     * a nudge button moves the loc one tile and then keeps re-issuing that
     * same move from the original tile forever. */
    TEST_ASSERT(selection.scene_x == 41, "the selection follows the loc");
    LocEditorSelection_Nudge(&selection, 1, 0, &move);
    TEST_ASSERT(move.from_x == 41, "so the second move starts where the first ended");
    TEST_ASSERT(move.to_x == 42, "and goes one further");
    LocEditorSelection_Nudge(&selection, 0, -1, &move);
    TEST_ASSERT(move.from_x == 42 && move.from_z == 50, "and on the other axis too");
    TEST_ASSERT(move.to_x == 42 && move.to_z == 49, "which moves only that axis");
    /* Twice, because one nudge on an axis cannot tell whether that axis
     * advanced -- the first z move starts from the tile it was already on
     * either way. */
    LocEditorSelection_Nudge(&selection, 0, -1, &move);
    TEST_ASSERT(move.from_z == 49, "the second z move starts where the first ended");
    TEST_ASSERT(move.to_z == 48, "and goes one further");
}

static void
test_a_rotate_stays_on_its_tile(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();
    struct LocEditorMove move;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    TEST_ASSERT(LocEditorSelection_Rotate(&selection, &move), "a selected loc turns");
    /* Placing over the same tile replaces what is there, so no remove leg is
     * needed -- and reporting a different destination would delete the loc on
     * the way to turning it. */
    TEST_ASSERT(move.from_x == move.to_x && move.from_z == move.to_z, "a rotate moves nothing");
    TEST_ASSERT(move.from_angle == 0 && move.to_angle == 1, "and turns one step");
    TEST_ASSERT(selection.angle == 1, "the selection follows the turn");
}

static void
test_a_rotate_cycles_the_four_angles(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement placement = a_door();
    struct LocEditorMove move;
    int i;

    /* Starting one short of the wrap, because that is the step a modulo
     * written as a clamp or an increment gets wrong: the fourth press has to
     * come back to west, not stop at south or run off the end of the four
     * config angles the cache defines. */
    placement.angle = LOC_EDITOR_ANGLE_COUNT - 1;
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectLoc(&selection, &placement);
    LocEditorSelection_Rotate(&selection, &move);
    TEST_ASSERT(move.to_angle == 0, "the last angle wraps to the first");
    TEST_ASSERT(selection.angle == 0, "and the selection wraps with it");

    /* All the way round returns to where it started. */
    placement.angle = 0;
    LocEditorSelection_SelectLoc(&selection, &placement);
    for( i = 0; i < LOC_EDITOR_ANGLE_COUNT; i++ )
    {
        LocEditorSelection_Rotate(&selection, &move);
        TEST_ASSERT(
            selection.angle >= 0 && selection.angle < LOC_EDITOR_ANGLE_COUNT,
            "every angle is one the cache defines");
    }
    TEST_ASSERT(selection.angle == 0, "four turns is a full circle");
}

static void
test_nothing_selected_moves_nothing(void)
{
    struct LocEditorSelection selection;
    struct LocEditorMove move;

    LocEditorSelection_Reset(&selection);
    TEST_ASSERT(!LocEditorSelection_Nudge(&selection, 1, 0, &move), "an empty panel nudges nothing");
    TEST_ASSERT(!LocEditorSelection_Rotate(&selection, &move), "and turns nothing");
}

static void
test_the_ground_cannot_be_moved(void)
{
    struct LocEditorSelection selection;
    struct LocEditorMove move;

    /* A tile selection is a readout, which is the whole of what a tile can
     * offer. Nudging one would issue a loc edit with no loc in it -- against
     * whatever the last selected loc happened to be, on the tile the ground is
     * on. */
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_SelectTile(&selection, 12, 13, 1);
    TEST_ASSERT(!LocEditorSelection_Nudge(&selection, 1, 0, &move), "the ground does not move");
    TEST_ASSERT(!LocEditorSelection_Rotate(&selection, &move), "nor turn");
    TEST_ASSERT(selection.scene_x == 12 && selection.scene_z == 13, "and stays where it is");
}

static void
test_the_hover_memo_ignores_the_panel(void)
{
    struct LocEditorSelection selection;
    int x;
    int z;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_NoteHover(&selection, false, 20, 30);
    TEST_ASSERT(LocEditorSelection_HoverTile(&selection, &x, &z), "a world hover is remembered");
    TEST_ASSERT(x == 20 && z == 30, "as the tile it was over");

    /* The cursor moves onto the panel -- which it must, to reach Reselect --
     * and the world hover under it names whatever the panel is covering. Taken
     * as a target, Reselect picks that instead of what the user pointed at. */
    LocEditorSelection_NoteHover(&selection, true, 99, 99);
    LocEditorSelection_HoverTile(&selection, &x, &z);
    TEST_ASSERT(x == 20 && z == 30, "a hover over the panel does not replace it");
}

static void
test_the_hover_memo_ignores_empty_sky(void)
{
    struct LocEditorSelection selection;
    int x;
    int z;

    LocEditorSelection_Reset(&selection);
    LocEditorSelection_NoteHover(&selection, false, 20, 30);
    /* Off the world entirely: the sky, or a gap between islands. Taking it
     * would drop the last real target for a tile that is not there, and
     * Reselect would then have nothing to aim at. */
    LocEditorSelection_NoteHover(&selection, false, -1, -1);
    LocEditorSelection_HoverTile(&selection, &x, &z);
    TEST_ASSERT(x == 20 && z == 30, "a hover over nothing does not replace it");

    /* One axis off is off. Taking half of it is worse than taking none: the
     * memo then reads as having no tile at all, and the last real one is gone
     * as surely as if it had been overwritten. */
    LocEditorSelection_NoteHover(&selection, false, 40, -1);
    TEST_ASSERT(
        LocEditorSelection_HoverTile(&selection, &x, &z), "half a tile does not unset the memo");
    TEST_ASSERT(x == 20 && z == 30, "nor replace it");
    LocEditorSelection_NoteHover(&selection, false, -1, 40);
    TEST_ASSERT(LocEditorSelection_HoverTile(&selection, &x, &z), "on either axis");
    TEST_ASSERT(x == 20 && z == 30, "either way");
}

static void
test_the_hover_memo_outlives_a_selection(void)
{
    struct LocEditorSelection selection;
    struct LocEditorLocPlacement const placement = a_door();
    int x;
    int z;

    /* Selecting, moving and deselecting are all things done through the panel,
     * with the cursor on it. If any of them reset the memo, the Reselect that
     * follows would have nothing to aim at -- the panel is in the way of
     * re-establishing one. */
    LocEditorSelection_Reset(&selection);
    LocEditorSelection_NoteHover(&selection, false, 20, 30);
    LocEditorSelection_SelectLoc(&selection, &placement);
    LocEditorSelection_Clear(&selection);
    LocEditorSelection_SelectTile(&selection, 1, 2, 0);
    TEST_ASSERT(LocEditorSelection_HoverTile(&selection, &x, &z), "the memo survives all of it");
    TEST_ASSERT(x == 20 && z == 30, "unchanged");
}

int
main(void)
{
    test_a_fresh_selection_is_pointed_at_nothing();
    test_selecting_a_loc_takes_its_whole_placement();
    test_an_unnamed_loc_selects_without_a_name();
    test_a_long_name_is_bounded();
    test_the_two_subjects_are_exclusive();
    test_a_tile_selection_names_the_ground();
    test_deselect_drops_whichever_subject_is_up();
    test_a_nudge_reports_both_ends();
    test_a_nudge_advances_the_placement();
    test_a_rotate_stays_on_its_tile();
    test_a_rotate_cycles_the_four_angles();
    test_nothing_selected_moves_nothing();
    test_the_ground_cannot_be_moved();
    test_the_hover_memo_ignores_the_panel();
    test_the_hover_memo_ignores_empty_sky();
    test_the_hover_memo_outlives_a_selection();

    if( g_failures )
    {
        printf("loc_editor_selection: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loc_editor_selection: all checks passed\n");
    return 0;
}
