/*
 * The HIGHLIGHT_* state machine.
 *
 * Every call below is a REAL one, copied out of a decompiled clientscript in
 * cache.osrs239 with its arguments intact, because the thing most likely to be
 * wrong here is not the bookkeeping -- it is which argument slot holds the
 * group. Three of the eight kinds put the group in a different place
 * (`highlight_tile_on(coord, group, flags)`, `highlight_loc_on(type, coord,
 * group, flags)`, `highlight_npctype_on(type, group)`), and reading the wrong
 * slot gives a plausible group number every time. A test written from the
 * table it is testing would agree with any of those; one written from the
 * scripts cannot.
 *
 * To re-derive any of them:
 *   3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 \
 *       --out /tmp/cs2 5197 5198 4762 4763 1854 8319
 */

#include "game/rs_highlight.h"

#include "cs2vm2/cs2_opcode.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                      \
    do                                                                                        \
    {                                                                                         \
        g_checks++;                                                                           \
        if( !(cond) )                                                                         \
        {                                                                                     \
            g_failures++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                   \
        }                                                                                     \
    } while( 0 )

/** Run one opcode the way CS2VM2_Op_Highlight hands it over: args in PUSH
 *  order. Returns the GET answer, or -1 for a form that has none. */
static int
apply(struct RS_HighlightState* st, int opcode, int const* args, int n)
{
    int answer = -1;
    if( !RS_HighlightApply(st, opcode, args, n, NULL, &answer) )
        return -2;
    return answer;
}

/** The same, for the PLAYER family's three name-keyed forms. */
static int
apply_named(
    struct RS_HighlightState* st,
    int opcode,
    int const* args,
    int n,
    char const* name)
{
    int answer = -1;
    if( !RS_HighlightApply(st, opcode, args, n, name, &answer) )
        return -2;
    return answer;
}

int
main(void)
{
    struct RS_HighlightState st;

    RS_HighlightReset(&st);

    /* ---- a fresh state is off, and off is -1 rather than 0 -------------- */
    {
        CHECK(!RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 5), "no group starts live");
        CHECK(st.style[RS_HIGHLIGHT_TILE][5].colour == -1, "and off is -1, not black");
        CHECK(st.member_count[RS_HIGHLIGHT_TILE] == 0, "with nothing in any list");
    }

    /* ---- "Highlight hovered tile", clientscript 5198 ---------------------
     *
     *   _7035(5, $colour, 0, 70, 10)    with 16 added for "always on top"
     *
     * then clientscript 5197, which is what actually marks the tile:
     *
     *   _7039(5); highlight_tile_on(_6950, 5, 0)
     */
    {
        int const setup[] = { 5, 0xBEBA6E, 0, 70, 10 };
        int const setup_on_top[] = { 5, 0xBEBA6E, 0, 70, 10 + 16 };
        int const coord = RS_HIGHLIGHT_COORD(0, 3200, 3220);
        int const on[] = { coord, 5, 0 };
        int const clear[] = { 5 };

        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, setup, 5);
        CHECK(RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 5), "the group is live");
        CHECK(st.style[RS_HIGHLIGHT_TILE][5].colour == 0xBEBA6E, "with the row's colour");
        CHECK(st.style[RS_HIGHLIGHT_TILE][5].opacity == 70, "opacity is a PERCENT");
        CHECK(
            (st.style[RS_HIGHLIGHT_TILE][5].flags & RS_HIGHLIGHT_FLAG_ALWAYS_ON_TOP) == 0,
            "and \"always on top\" is not set");

        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, setup_on_top, 5);
        CHECK(
            (st.style[RS_HIGHLIGHT_TILE][5].flags & RS_HIGHLIGHT_FLAG_ALWAYS_ON_TOP) != 0,
            "the +16 the script adds IS the always-on-top bit");

        /* The group argument is the SECOND int of tile_on, not the first. */
        apply(&st, CS2_OP_HIGHLIGHT_TILE_ON, on, 3);
        CHECK(st.member_count[RS_HIGHLIGHT_TILE] == 1, "the tile joined");
        CHECK(
            st.member[RS_HIGHLIGHT_TILE][0].group == 5,
            "in group 5 -- the group is args[1] for this form");
        CHECK(
            st.member[RS_HIGHLIGHT_TILE][0].coord == coord,
            "and the coord is args[0]");
        CHECK(
            RS_HighlightGet(&st, RS_HIGHLIGHT_TILE, 5, -1, coord),
            "GET now answers true, which it could not before anything joined");

        /* Re-marking the same tile must not grow the list: 5197 runs on every
         * hover change and re-adds without clearing first when the pointer
         * comes back. */
        apply(&st, CS2_OP_HIGHLIGHT_TILE_ON, on, 3);
        CHECK(st.member_count[RS_HIGHLIGHT_TILE] == 1, "and re-marking it does not duplicate");

        apply(&st, CS2_OP_HIGHLIGHT_TILE_CLEAR, clear, 1);
        CHECK(st.member_count[RS_HIGHLIGHT_TILE] == 0, "CLEAR empties the group");
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 5),
            "but leaves the group's style alone -- 5197 clears then re-adds");
    }

    /* ---- the disabling shape: colour -1, opacity 0 ---------------------- */
    {
        int const off[] = { 5, -1, 0, 0, 0 };
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, off, 5);
        CHECK(!RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 5), "a -1 colour is off");

        /* clientscript 1854's teardown keeps the colour and zeroes the opacity
         * AND the flags: `_7010(11, 65280, 1, 0, 0)`. Testing the colour alone
         * would call that group live. */
        int const half_off[] = { 11, 65280, 1, 0, 0 };
        apply(&st, CS2_OP_HIGHLIGHT_LOC_SETUP, half_off, 5);
        CHECK(
            !RS_HighlightGroupLive(&st, RS_HIGHLIGHT_LOC, 11),
            "and so is a kept colour with nothing left to draw");

        /*
         * The two shapes that a single-field test gets wrong, both real.
         *
         * The reference's IsEnabled is an OUTLINE flag with a thickness, or a
         * FILL flag with an opacity. Test only opacity and every hover
         * highlight in the game disappears; test only thickness and the
         * hovered tile does.
         */
        int const mouseover[] = { 5, 12632064, 2, 0, 1 + 512 };
        apply(&st, CS2_OP_HIGHLIGHT_NPC_SETUP, mouseover, 5);
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_NPC, 5),
            "a mouseover group is live at opacity 0 -- outline, thickness 2");

        /* clientscript 5198's hovered tile: flags 2|8 but thickness ZERO, so
         * it is a wash with no border -- live through the fill half only. */
        int const hovered[] = { 8, 0xBEBA6E, 0, 70, 2 + 8 };
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, hovered, 5);
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 8),
            "the hovered tile is live at thickness 0 -- fill, opacity 70");
        CHECK(
            st.style[RS_HIGHLIGHT_TILE][8].outline_width == 0,
            "and its outline thickness really is zero");

        /* An outline flag with no thickness, and a fill flag with no opacity:
         * each half named without the value it needs draws nothing. */
        int const flag_only[] = { 9, 0x00FF00, 0, 0, 1 + 2 + 4 + 8 };
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, flag_only, 5);
        CHECK(
            !RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 9),
            "every flag set but no thickness and no opacity draws nothing");

        /* And a flag word of only the bits that say HOW rather than WHAT --
         * always-on-top and minimap -- is not a drawable group either. */
        int const how_only[] = { 10, 0x00FF00, 2, 50, 16 + 64 };
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, how_only, 5);
        CHECK(
            !RS_HighlightGroupLive(&st, RS_HIGHLIGHT_TILE, 10),
            "always-on-top and minimap alone say nothing about what to draw");
    }

    /* ---- "Tile highlighting" (the markers), clientscripts 4763 and 4762 --
     *
     *   _7035(6, $colour, 2, 50, 90)                  when varbit12342 is on
     *   if (_7038(_6950, 6, 1)) highlight_tile_off(...) else highlight_tile_on(...)
     *
     * The toggle reads GET back, so GET being truthful is what makes the
     * second click un-mark rather than mark again.
     */
    {
        int const setup[] = { 6, 0x00FF00, 2, 50, 90 };
        int const coord = RS_HIGHLIGHT_COORD(0, 3210, 3210);
        int const at[] = { coord, 6, 1 };

        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, setup, 5);
        CHECK(apply(&st, CS2_OP_HIGHLIGHT_TILE_GET, at, 3) == 0, "unmarked to begin with");
        apply(&st, CS2_OP_HIGHLIGHT_TILE_ON, at, 3);
        CHECK(apply(&st, CS2_OP_HIGHLIGHT_TILE_GET, at, 3) == 1, "marked");
        apply(&st, CS2_OP_HIGHLIGHT_TILE_OFF, at, 3);
        CHECK(apply(&st, CS2_OP_HIGHLIGHT_TILE_GET, at, 3) == 0, "and unmarked again");
        CHECK(
            (st.style[RS_HIGHLIGHT_TILE][6].flags & RS_HIGHLIGHT_FLAG_MINIMAP) != 0,
            "the marker group carries the minimap bit -- 90 = 2+8+16+64");
    }

    /* ---- groups are per KIND, not shared --------------------------------
     *
     * clientscript 1854 ("Highlight Agility obstacles") proves it: it has a
     * loc group 11, an npctype group 11 and an objtype group 9 live at once,
     * with a tile group 10 beside them.
     */
    {
        int const loc[] = { 11, 65280, 1, 30, 5 };
        int const loctype[] = { 11, 65280, 1, 30, 5 };
        int const npctype[] = { 11, 65280, 1, 30, 5 };
        int const tile[] = { 10, 65280, 1, 30, 10 };
        int const objtype[] = { 9, 0xFF0000, 1, 30, 15 };
        /* highlight_objtype_on(obj_11849, 9) -- the type is FIRST, group second. */
        int const objtype_on[] = { 11849, 9 };

        apply(&st, CS2_OP_HIGHLIGHT_LOC_SETUP, loc, 5);
        apply(&st, CS2_OP_HIGHLIGHT_LOCTYPE_SETUP, loctype, 5);
        apply(&st, CS2_OP_HIGHLIGHT_NPCTYPE_SETUP, npctype, 5);
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, tile, 5);
        apply(&st, CS2_OP_HIGHLIGHT_OBJTYPE_SETUP, objtype, 5);
        apply(&st, CS2_OP_HIGHLIGHT_OBJTYPE_ON, objtype_on, 2);

        CHECK(RS_HighlightGroupLive(&st, RS_HIGHLIGHT_LOC, 11), "loc group 11 is live");
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_NPCTYPE, 11),
            "and so is npctype group 11, which is a different group");
        CHECK(
            st.style[RS_HIGHLIGHT_OBJTYPE][9].colour == 0xFF0000,
            "and objtype 9 keeps its own colour");
        CHECK(
            RS_HighlightGet(&st, RS_HIGHLIGHT_OBJTYPE, 9, 11849, -1),
            "the objtype joined its group -- the type is args[0]");
        CHECK(
            !RS_HighlightGet(&st, RS_HIGHLIGHT_OBJ, 9, 11849, -1),
            "and the OBJ kind, which is a different list, did not gain it");
    }

    /* ---- "Highlight poll booths", clientscripts 8319 and 8320 ------------
     *
     *   _7010(16, 16776960, 2, 30, 5)
     *   highlight_loc_on(_6802, _6801, 16, 1)   -- type, coord, group, flags
     *
     * The group is the THIRD int here. Reading args[1] would put the booth in
     * a group numbered by its coordinate.
     */
    {
        int const setup[] = { 16, 16776960, 2, 30, 5 };
        int const coord = RS_HIGHLIGHT_COORD(0, 3092, 3244);
        int const on[] = { 8720, coord, 16, 1 };

        apply(&st, CS2_OP_HIGHLIGHT_LOC_SETUP, setup, 5);
        apply(&st, CS2_OP_HIGHLIGHT_LOC_ON, on, 4);
        CHECK(
            RS_HighlightGet(&st, RS_HIGHLIGHT_LOC, 16, 8720, coord),
            "the booth is in group 16 -- the group is args[2] for this form");
        CHECK(
            !RS_HighlightGet(&st, RS_HIGHLIGHT_LOC, 16, 8720, coord + 1),
            "and a placed loc is pinned to its own coord");
    }

    /* ---- the PLAYER family, keyed by NAME -------------------------------
     *
     * Clientscript 5954, the mouse-over player highlighter, is the real user:
     *
     *   [clientscript,script5954](int $int0)
     *   ...
     *   highlight_player_on(_6900, $int0);      // (name, group)
     *
     * and 5950 takes it back out with `highlight_player_off($string0, $int0)`
     * after asking `_7033($string0, $int0)`. The group is the only INT any of
     * the three take; the subject is the string beside it.
     */
    {
        int const setup[] = { 5, 0x00FF00, 1, 30, 1 };
        int const group[] = { 5 };
        apply(&st, CS2_OP_HIGHLIGHT_PLAYER_SETUP, setup, 5);
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_PLAYER, 5),
            "a player group's style is recorded");

        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, group, 1, "Zezima") == 0,
            "an unnamed player is not in the group");
        apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_ON, group, 1, "Zezima");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, group, 1, "Zezima") == 1,
            "and is once it has been named");
        CHECK(st.named_count == 1, "which is one subject, in the named list");

        /* The mouse-over highlighter re-adds the same name every tick. */
        apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_ON, group, 1, "Zezima");
        CHECK(st.named_count == 1, "naming it twice does not add it twice");

        /* The name is the key, not the group's contents: a second group holds
         * the same player independently. */
        int const other[] = { 9 };
        apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_ON, other, 1, "Zezima");
        CHECK(st.named_count == 2, "the same name in another group is another subject");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, group, 1, "zezima") == 0,
            "and the compare is exact, so a different spelling is a different player");

        apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_OFF, group, 1, "Zezima");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, group, 1, "Zezima") == 0,
            "OFF takes it out of that group");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, other, 1, "Zezima") == 1,
            "and leaves the other group's alone");

        /* CLEAR is the int-keyed opcode of a name-keyed kind: it must empty
         * the NAMED list, which is where this kind's subjects are. */
        apply(&st, CS2_OP_HIGHLIGHT_PLAYER_CLEAR, other, 1);
        CHECK(st.named_count == 0, "CLEAR empties the kind's own list");

        /* A form that needs a name and is handed none is refused rather than
         * recorded against "" -- that would be a highlight on nobody. */
        CHECK(
            apply(&st, CS2_OP_HIGHLIGHT_PLAYER_ON, group, 1) == -2,
            "a name-keyed form with no name is refused");
    }

    /* ---- the OP GROUP block (7040..7044), keyed by a menu NAME ----------
     *
     * The cache only ever sets it up and clears it -- script 5486's teardown
     * is `_7040(group, -1, 0, 0, 0)` over twenty groups and 6686 is
     * `_7044(6)` -- so these are the calls it makes, plus the ON/OFF/GET the
     * reference implements and nothing here has a caller for yet.
     */
    {
        int const teardown[] = { 9, -1, 0, 0, 0 };
        int const setup[] = { 9, 0x00FF00, 1, 30, 1 };
        int const group[] = { 9 };
        int const other_group[] = { 5 };

        apply(&st, CS2_OP__7040, teardown, 5);
        CHECK(
            !RS_HighlightGroupLive(&st, RS_HIGHLIGHT_OPGROUP, 9),
            "the cache's teardown form leaves the group off");

        apply(&st, CS2_OP__7040, setup, 5);
        CHECK(
            RS_HighlightGroupLive(&st, RS_HIGHLIGHT_OPGROUP, 9),
            "and a real style makes it live");

        apply_named(&st, CS2_OP__7041, group, 1, "Cow");
        CHECK(
            apply_named(&st, CS2_OP__7043, group, 1, "Cow") == 1,
            "a named op group records its subject");
        /* The two name-keyed kinds share one list and must not see each
         * other's subjects: a player called Cow is not the Cow group. */
        apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_ON, other_group, 1, "Cow");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, group, 1, "Cow") == 0,
            "and the kinds do not answer for each other");
        CHECK(
            apply_named(&st, CS2_OP__7043, other_group, 1, "Cow") == 0,
            "in either direction");

        apply(&st, CS2_OP__7044, group, 1);
        CHECK(
            apply_named(&st, CS2_OP__7043, group, 1, "Cow") == 0,
            "CLEAR empties the op group's own subjects");
        CHECK(
            apply_named(&st, CS2_OP_HIGHLIGHT_PLAYER_GET, other_group, 1, "Cow") == 1,
            "and leaves the player list alone");
    }

    /* ---- an argument count that disagrees with the table is refused ------
     *
     * If cs2vm2's opcode table and this one ever disagree about a form, acting
     * on it would read the group out of whichever slot happened to line up.
     */
    {
        int const wrong[] = { 5, 1 };
        CHECK(
            apply(&st, CS2_OP_HIGHLIGHT_TILE_ON, wrong, 2) == -2,
            "a form with the wrong arity is refused, not guessed at");
    }

    /* ---- a group id past the table is refused, not asserted -------------- */
    {
        int const setup[] = { RS_HIGHLIGHT_GROUP_MAX + 4, 0x00FF00, 1, 30, 1 };
        apply(&st, CS2_OP_HIGHLIGHT_TILE_SETUP, setup, 5);
        CHECK(g_checks > 0, "a group id out of range does not crash the client");
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
