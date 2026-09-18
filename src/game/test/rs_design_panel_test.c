/*
 * The character-design panel's client-side arrow step.
 *
 * Two things here are worth a test and neither shows up as a crash.
 *
 * The first is WHICH row an arrow is. The panel numbers its components four to
 * a row, so an off-by-one reads as "the hairstyle arrow changes the jaw" --
 * for one tick, until the server's appearance corrects it, which is exactly
 * long enough to look like a rendering glitch.
 *
 * The second is WHERE the step lands. The server steps an INDEX into a
 * filtered per-bodypart list (`design_kit`, generated from the cache's idk
 * table); the client steps the KIT ID it is standing on, skipping the ids that
 * are not this part's. Those two are the same walk, and this is where that is
 * checked: a fake idk table with holes in it -- unselectable kits, other body
 * parts, the female half -- and the prediction has to land on the same entry
 * an index would.
 */

#include "game/rs_design_panel.h"

#include "engine/cache_provider.h"
#include "engine/entity_model_build.h"
#include "engine/torirs_types.h"
#include "net/rev/packets/pkt_player_appearance.h"

#include <stdio.h>
#include <stdlib.h>
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

/* ------------------------------------------------------------------ */
/* Which child is which arrow                                          */
/* ------------------------------------------------------------------ */

static void
test_arrow_mapping(void)
{
    struct RS_DesignArrow arrow;

    /* The seven body-part rows, `head_left` at 15. */
    CHECK(RS_DesignPanel_Arrow(15, &arrow), "15 is an arrow");
    CHECK(arrow.kind == RS_DESIGN_ARROW_KIT && arrow.row == 0 && arrow.step == -1,
          "15 is head, left");
    CHECK(RS_DesignPanel_Arrow(16, &arrow), "16 is an arrow");
    CHECK(arrow.kind == RS_DESIGN_ARROW_KIT && arrow.row == 0 && arrow.step == 1,
          "16 is head, right");
    CHECK(RS_DesignPanel_Arrow(39, &arrow) && arrow.kind == RS_DESIGN_ARROW_KIT &&
              arrow.row == 6 && arrow.step == -1,
          "39 is feet, left -- the last body row");
    CHECK(RS_DesignPanel_Arrow(40, &arrow) && arrow.kind == RS_DESIGN_ARROW_KIT &&
              arrow.row == 6 && arrow.step == 1,
          "40 is feet, right");

    /* The five colour rows, `hair_left` at 46. */
    CHECK(RS_DesignPanel_Arrow(46, &arrow) && arrow.kind == RS_DESIGN_ARROW_COLOUR &&
              arrow.row == 0 && arrow.step == -1,
          "46 is the hair colour, left");
    CHECK(RS_DesignPanel_Arrow(63, &arrow) && arrow.kind == RS_DESIGN_ARROW_COLOUR &&
              arrow.row == 4 && arrow.step == 1,
          "63 is skin, right -- the last colour row");

    /* The other two components of each row are the label and its backing. */
    CHECK(!RS_DesignPanel_Arrow(13, &arrow), "13 (`head`) is the row, not an arrow");
    CHECK(!RS_DesignPanel_Arrow(14, &arrow), "14 (`head_text`) is the label");
    CHECK(!RS_DesignPanel_Arrow(17, &arrow), "17 (`jaw`) is the next row's backing");
    CHECK(!RS_DesignPanel_Arrow(18, &arrow), "18 (`jaw_text`) is its label");
    CHECK(arrow.kind == RS_DESIGN_ARROW_NONE, "a refused child leaves no arrow behind");

    /* And the gaps on either side of the two runs. */
    CHECK(!RS_DesignPanel_Arrow(0, &arrow), "0 (`universe`) is not an arrow");
    CHECK(!RS_DesignPanel_Arrow(41, &arrow), "41 (`contents_right`) is past the body rows");
    CHECK(!RS_DesignPanel_Arrow(44, &arrow), "44 (`hair`) is a colour row's backing");
    CHECK(!RS_DesignPanel_Arrow(64, &arrow), "64 (`contents_bottom`) is past the colour rows");
    CHECK(!RS_DesignPanel_Arrow(74, &arrow), "74 (`confirm`) keeps its server round trip");
    CHECK(!RS_DesignPanel_Arrow(-1, &arrow), "a negative child is not an arrow");
}

/* ------------------------------------------------------------------ */
/* Colours: the client already owns the palettes                       */
/* ------------------------------------------------------------------ */

static void
test_colour_step(struct CacheProvider* provider)
{
    int slots[12] = { 0 };
    int identkit[12] = { 0 };
    int colors[5] = { 0, 0, 0, 0, 0 };
    struct RS_DesignArrow left = { RS_DESIGN_ARROW_COLOUR, 0, -1 };
    struct RS_DesignArrow right = { RS_DESIGN_ARROW_COLOUR, 0, 1 };
    int const hair = PlayerModel_DesignColourCount(0);

    CHECK(hair > 1, "the hair palette has entries to step through");

    CHECK(RS_DesignPanel_Apply(provider, &right, 0, slots, identkit, colors),
          "the right arrow moves the hair colour");
    CHECK(colors[0] == 1, "one step right from 0 is 1");
    CHECK(colors[1] == 0 && colors[4] == 0, "and no other row moved");

    colors[0] = 0;
    CHECK(RS_DesignPanel_Apply(provider, &left, 0, slots, identkit, colors),
          "the left arrow moves it too");
    CHECK(colors[0] == hair - 1, "left from 0 wraps to the end of the palette");

    colors[0] = hair - 1;
    CHECK(RS_DesignPanel_Apply(provider, &right, 0, slots, identkit, colors),
          "and the other end wraps as well");
    CHECK(colors[0] == 0, "right from the last entry is 0");

    /* A colour row never touches the body. */
    CHECK(slots[8] == 0 && identkit[8] == 0, "a colour step leaves the kits alone");
}

/* ------------------------------------------------------------------ */
/* Kits: the same walk as the server's index                           */
/* ------------------------------------------------------------------ */

/*
 * A table shaped like the real one's awkward parts: the kits of one body part
 * are NOT contiguous ids, some are marked unselectable, and the female half is
 * the same parts at +7.
 *
 *   id  0  1  2  3  4  5  6  7  8
 *   part 0  0  1  0  0  7  0  1  0
 *   sel  y  n  y  y  y  y  y  y  y
 *
 * So male hair (part 0) offers 0, 3, 4, 6, 8 in that order -- which is the
 * list the server's enum would hold, and its indices 0..4.
 */
static void
add_idk(
    struct CacheProvider* provider,
    int id,
    int body_part,
    int not_selectable)
{
    struct ToriRS_Idk* idk = calloc(1, sizeof(*idk));

    if( !idk )
        abort();
    idk->id = id;
    idk->body_part_id = body_part;
    idk->not_selectable = not_selectable ? true : false;
    CacheProvider_IdkAdd(provider, id, idk);
}

static void
build_kit_table(struct CacheProvider* provider)
{
    add_idk(provider, 0, 0, 0);
    add_idk(provider, 1, 0, 1);
    add_idk(provider, 2, 1, 0);
    add_idk(provider, 3, 0, 0);
    add_idk(provider, 4, 0, 0);
    add_idk(provider, 5, 7, 0);
    add_idk(provider, 6, 0, 0);
    add_idk(provider, 7, 1, 0);
    add_idk(provider, 8, 0, 0);
}

/* Step the hair row once and answer with the kit it landed on. */
static int
step_hair(
    struct CacheProvider* provider,
    int from_kit,
    int step,
    int gender,
    int worn_obj)
{
    struct RS_DesignArrow arrow = { RS_DESIGN_ARROW_KIT, 0, 0 };
    int slots[12] = { 0 };
    int identkit[12] = { 0 };
    int colors[5] = { 0 };
    int const wearpos = PlayerModel_DesignPartWearpos(0);

    arrow.step = step;
    identkit[wearpos] = Appearance_PackKit(from_kit);
    slots[wearpos] = worn_obj >= 0 ? Appearance_PackObj(worn_obj) : Appearance_PackKit(from_kit);

    if( !RS_DesignPanel_Apply(provider, &arrow, gender, slots, identkit, colors) )
        return -1;

    /* The drawn layer follows the body underneath unless an obj is over it --
     * the same rule the server's own encoder applies. */
    if( worn_obj >= 0 )
        CHECK(Appearance_SlotObj(slots[wearpos]) == worn_obj,
              "a worn obj is not replaced by the kit under it");
    else
        CHECK(Appearance_SlotKit(slots[wearpos]) == Appearance_SlotKit(identkit[wearpos]),
              "an uncovered slot draws the kit that was just chosen");
    return Appearance_SlotKit(identkit[wearpos]);
}

static void
test_kit_step(struct CacheProvider* provider)
{
    /* Male hair: 0, 3, 4, 6, 8. */
    CHECK(step_hair(provider, 0, 1, 0, -1) == 3, "right from 0 skips the unselectable 1");
    CHECK(step_hair(provider, 3, 1, 0, -1) == 4, "and then takes the next one");
    CHECK(step_hair(provider, 4, 1, 0, -1) == 6, "skipping the other part's kit at 5");
    CHECK(step_hair(provider, 8, 1, 0, -1) == 0, "the end of the list wraps to its start");
    CHECK(step_hair(provider, 0, -1, 0, -1) == 8, "and the start wraps to the end");
    CHECK(step_hair(provider, 3, -1, 0, -1) == 0, "left steps the same list backwards");

    /* Female hair is body part 7, and this table holds exactly one: the arrow
     * has nowhere to go, so nothing is predicted and the tick answers. */
    CHECK(step_hair(provider, 5, 1, 1, -1) == -1,
          "a row whose part offers one kit declines to predict");

    /* A kit under a worn obj still steps -- it is the body underneath. */
    CHECK(step_hair(provider, 0, 1, 0, 1042) == 3, "the body steps under worn equipment");

    /* And a row holding no kit at all has nothing to step from. */
    {
        struct RS_DesignArrow arrow = { RS_DESIGN_ARROW_KIT, 0, 1 };
        int slots[12] = { 0 };
        int identkit[12] = { 0 };
        int colors[5] = { 0 };

        CHECK(!RS_DesignPanel_Apply(provider, &arrow, 0, slots, identkit, colors),
              "an empty row is left to the server");
    }
}

int
main(void)
{
    struct CacheProvider provider;

    memset(&provider, 0, sizeof(provider));
    CacheProvider_InitEngineCaches(&provider);
    build_kit_table(&provider);

    test_arrow_mapping();
    test_colour_step(&provider);
    test_kit_step(&provider);

    CacheProvider_FreeEngineCaches(&provider);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
