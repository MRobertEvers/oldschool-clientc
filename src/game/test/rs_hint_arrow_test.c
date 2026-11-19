/*
 * The hint arrow's two foldings: the wire's clear to 0, and the four tile-edge
 * forms to one tile form plus an anchor inside the tile.
 *
 * A fold that goes wrong does not fail loudly. An edge form left unfolded is
 * an arrow no pass draws; an anchor on the wrong axis floats the arrow over
 * the wrong side of the tile; and an offset kept from the previous packet
 * moves a centred arrow to the edge the last one named.
 *
 *   make -C src test-rs-hint-arrow
 */
#include "game/rs_hint_arrow.h"

#include <stdio.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_entity_forms_keep_their_type_and_id(void)
{
    struct RS_HintArrow arrow;

    printf("TEST: an npc slot and a player pid are stored as the wire gave them\n");

    RS_HintArrow_Clear(&arrow);
    CHECK(arrow.type == 0, "a cleared arrow has type %d", arrow.type);

    RS_HintArrow_Set(&arrow, 1, 41, 0, 0);
    CHECK(arrow.type == RS_HINT_ARROW_NPC, "type 1 stored as %d", arrow.type);
    CHECK(arrow.target == 41, "npc slot stored as %d", arrow.target);

    RS_HintArrow_Set(&arrow, 10, 7, 0, 0);
    CHECK(arrow.type == RS_HINT_ARROW_PLAYER, "type 10 stored as %d", arrow.type);
    CHECK(arrow.target == 7, "player pid stored as %d", arrow.target);
}

static void
test_tile_forms_fold_to_one_with_an_anchor(void)
{
    static struct
    {
        int wire_type;
        int offset_x;
        int offset_z;
    } const cases[] = {
        { 2, 64, 64 }, { 3, 0, 64 }, { 4, 128, 64 }, { 5, 64, 0 }, { 6, 64, 128 },
    };
    struct RS_HintArrow arrow;

    printf("TEST: types 2..6 are one tile form, anchored by the type\n");

    RS_HintArrow_Clear(&arrow);
    for( unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        RS_HintArrow_Set(&arrow, cases[i].wire_type, 3200, 3300, 60);
        CHECK(arrow.type == RS_HINT_ARROW_COORD, "type %d stored as %d", cases[i].wire_type,
            arrow.type);
        CHECK(arrow.target == 3200, "type %d: x stored as %d", cases[i].wire_type, arrow.target);
        CHECK(arrow.tile_z == 3300, "type %d: z stored as %d", cases[i].wire_type, arrow.tile_z);
        CHECK(arrow.height == 60, "type %d: height stored as %d", cases[i].wire_type, arrow.height);
        CHECK(arrow.offset_x == cases[i].offset_x, "type %d: offset_x %d, wanted %d",
            cases[i].wire_type, arrow.offset_x, cases[i].offset_x);
        CHECK(arrow.offset_z == cases[i].offset_z, "type %d: offset_z %d, wanted %d",
            cases[i].wire_type, arrow.offset_z, cases[i].offset_z);
    }
}

static void
test_every_packet_resets_the_anchor(void)
{
    struct RS_HintArrow arrow;

    printf("TEST: a packet's anchor does not carry into the next packet\n");

    RS_HintArrow_Clear(&arrow);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_COORD_EAST, 10, 20, 0);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_COORD, 10, 20, 0);
    CHECK(arrow.offset_x == 64, "a centred arrow after an east one has offset_x %d",
        arrow.offset_x);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_NPC, 5, 0, 0);
    CHECK(arrow.offset_x == 64 && arrow.offset_z == 64,
        "an npc arrow kept the offsets %d,%d", arrow.offset_x, arrow.offset_z);
}

static void
test_the_wire_clear_is_zero_and_overwrites(void)
{
    struct RS_HintArrow arrow;

    printf("TEST: 255 stores type 0 and still overwrites the other fields\n");

    RS_HintArrow_Clear(&arrow);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_COORD_NORTH, 3200, 3300, 60);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_WIRE_CLEAR, 9, 8, 7);
    CHECK(arrow.type == 0, "255 stored as %d", arrow.type);
    CHECK(arrow.target == 9 && arrow.tile_z == 8 && arrow.height == 7,
        "the clear kept %d,%d,%d", arrow.target, arrow.tile_z, arrow.height);
    CHECK(arrow.offset_x == 64 && arrow.offset_z == 64,
        "the clear kept the offsets %d,%d", arrow.offset_x, arrow.offset_z);
}

static void
test_coord_fine_units(void)
{
    struct RS_HintArrow arrow;

    printf("TEST: a tile arrow's fine position is the tile from the base, times 128, plus its anchor\n");

    RS_HintArrow_Clear(&arrow);
    RS_HintArrow_Set(&arrow, RS_HINT_ARROW_COORD_WEST, 3210, 3305, 0);
    CHECK(RS_HintArrow_CoordFineX(&arrow, 3200) == 10 * 128 + 0, "fine x %d",
        RS_HintArrow_CoordFineX(&arrow, 3200));
    CHECK(RS_HintArrow_CoordFineZ(&arrow, 3300) == 5 * 128 + 64, "fine z %d",
        RS_HintArrow_CoordFineZ(&arrow, 3300));
    /* A tile west of the base is outside the loaded window, which is what the
     * minimap's rim arrow is for: negative, not clamped. */
    CHECK(RS_HintArrow_CoordFineX(&arrow, 3220) == -10 * 128, "fine x west of the base %d",
        RS_HintArrow_CoordFineX(&arrow, 3220));
}

int
main(void)
{
    test_entity_forms_keep_their_type_and_id();
    test_tile_forms_fold_to_one_with_an_anchor();
    test_every_packet_resets_the_anchor();
    test_the_wire_clear_is_zero_and_overwrites();
    test_coord_fine_units();

    if( g_failures )
    {
        printf("rs_hint_arrow_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_hint_arrow_test: OK\n");
    return 0;
}
