/*
 * A plugin's panel wells: what is staged while one well draws, and what is
 * kept between draws.
 *
 * A push with no well open has to go nowhere. Past the bound a push is
 * dropped and the overflow remembered, and a well that overflowed must not
 * replace its last complete picture with a cut-short one. Retiring one owner
 * must leave every other owner's run as it was, in order. Every change to
 * what the kept list shows moves the revision, and the revision skips 0.
 *
 * Run: make -C src test-panel-overlay-stage
 */

#include "render/panel_overlay_stage.h"

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

/* Big enough that a stack copy is a bad idea. */
static struct PanelOverlayStage g_stage;

static struct UITreeEntityOverlay
a_rect(int x)
{
    struct UITreeEntityOverlay item;
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = x;
    item.y = 1;
    item.w = 1;
    item.h = 1;
    return item;
}

/* Open a well at (100, 200), scale 2, clipped to a box that holds every
 * primitive these tests push. */
static void
begin_well(struct PanelOverlayStage* stage)
{
    PanelOverlayStage_Begin(stage, 100, 200, 2, 100, 200, 4000, 4000);
}

static int
draw_well(
    struct PanelOverlayStage* stage,
    uint32_t generation,
    uint32_t owner,
    int row,
    int first_x,
    int primitives)
{
    int committed;

    begin_well(stage);
    for( int i = 0; i < primitives; i++ )
    {
        struct UITreeEntityOverlay const item = a_rect(first_x + i);
        PanelOverlayStage_Push(stage, &item);
    }
    committed = PanelOverlayStage_Commit(stage, generation, owner, row) ? 1 : 0;
    PanelOverlayStage_End(stage);
    return committed;
}

static void
test_drop_with_no_well_open(void)
{
    struct UITreeEntityOverlay const item = a_rect(3);

    memset(&g_stage, 0, sizeof(g_stage));
    PanelOverlayStage_Reset(&g_stage, 7);
    TEST_ASSERT(!PanelOverlayStage_Push(&g_stage, &item), "a push with no well open is refused");
    TEST_ASSERT(g_stage.staged_count == 0, "and nothing is staged");
    TEST_ASSERT(!g_stage.overflow, "a refusal for no well is not an overflow");
    TEST_ASSERT(PanelOverlayStage_StagedCount(&g_stage) == 0, "no well open counts none");

    begin_well(&g_stage);
    TEST_ASSERT(PanelOverlayStage_Push(&g_stage, &item), "an open well keeps a push");
    TEST_ASSERT(PanelOverlayStage_StagedCount(&g_stage) == 1, "and counts it");
    TEST_ASSERT(g_stage.staged[0].x == 106 && g_stage.staged[0].w == 2,
                "staged in panel space: scaled about the well's origin");
    TEST_ASSERT(g_stage.staged[0].clip_x == 100 && g_stage.staged[0].clip_w == 4000,
                "and cut to the well's clip");
    PanelOverlayStage_End(&g_stage);
    TEST_ASSERT(PanelOverlayStage_StagedCount(&g_stage) == 0, "a closed well counts none");
    TEST_ASSERT(!PanelOverlayStage_Push(&g_stage, &item), "and refuses again once closed");
}

static void
test_bound_and_overflow(void)
{
    uint32_t revision;

    memset(&g_stage, 0, sizeof(g_stage));
    PanelOverlayStage_Reset(&g_stage, 7);
    TEST_ASSERT(draw_well(&g_stage, 7, 11, 0, 0, 3) == 1, "a well that fits commits");
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 3, "its run is kept");
    revision = PanelOverlayStage_Revision(&g_stage);

    begin_well(&g_stage);
    for( int i = 0; i < PANEL_OVERLAY_STAGE_MAX; i++ )
    {
        struct UITreeEntityOverlay const item = a_rect(i);
        TEST_ASSERT(PanelOverlayStage_Push(&g_stage, &item), "every push up to the bound is kept");
    }
    TEST_ASSERT(!g_stage.overflow, "filling to the bound is not an overflow");
    {
        struct UITreeEntityOverlay const item = a_rect(0);
        TEST_ASSERT(!PanelOverlayStage_Push(&g_stage, &item), "the push past the bound is refused");
    }
    TEST_ASSERT(g_stage.overflow, "and remembered");
    TEST_ASSERT(PanelOverlayStage_StagedCount(&g_stage) == PANEL_OVERLAY_STAGE_MAX,
                "the staged list stops at the bound");
    TEST_ASSERT(!PanelOverlayStage_Commit(&g_stage, 7, 11, 0), "an overflowed well does not commit");
    PanelOverlayStage_End(&g_stage);
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 3, "the last complete picture stands");
    TEST_ASSERT(PanelOverlayStage_Revision(&g_stage) == revision, "and nothing it shows moved");

    TEST_ASSERT(draw_well(&g_stage, 7, 11, 0, 0, 0) == 0, "a well that drew nothing declines");
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 3, "and keeps its picture");

    TEST_ASSERT(draw_well(&g_stage, 8, 11, 0, 0, 2) == 0, "a commit for another generation is refused");
    TEST_ASSERT(draw_well(&g_stage, 7, 0, 0, 0, 2) == 0, "owner 0 owns nothing and cannot commit");

    /* Other owners' runs and the new one must fit together. */
    TEST_ASSERT(draw_well(&g_stage, 7, 12, 1, 0, PANEL_OVERLAY_STAGE_MAX - 3) == 1,
                "a second owner fills the rest");
    TEST_ASSERT(draw_well(&g_stage, 7, 13, 2, 0, 1) == 0, "a third that does not fit is refused");
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == PANEL_OVERLAY_STAGE_MAX,
                "and the kept list is untouched");
    TEST_ASSERT(draw_well(&g_stage, 7, 12, 1, 0, PANEL_OVERLAY_STAGE_MAX - 3) == 1,
                "an owner replacing its own run counts only the others against the bound");
}

static void
test_owner_removal(void)
{
    struct UITreeEntityOverlay item;
    uint32_t owner;
    uint32_t revision;
    int row;

    memset(&g_stage, 0, sizeof(g_stage));
    PanelOverlayStage_Reset(&g_stage, 7);
    draw_well(&g_stage, 7, 11, 0, 0, 2);
    draw_well(&g_stage, 7, 12, 1, 10, 3);
    draw_well(&g_stage, 7, 13, 2, 20, 1);
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 6, "three runs kept");

    revision = PanelOverlayStage_Revision(&g_stage);
    TEST_ASSERT(!PanelOverlayStage_Retire(&g_stage, 99), "retiring an owner with no run drops nothing");
    TEST_ASSERT(!PanelOverlayStage_Retire(&g_stage, 0), "owner 0 owns nothing");
    TEST_ASSERT(PanelOverlayStage_Revision(&g_stage) == revision, "and neither moves the revision");

    TEST_ASSERT(PanelOverlayStage_Retire(&g_stage, 12), "retiring a kept owner drops its run");
    TEST_ASSERT(PanelOverlayStage_Revision(&g_stage) != revision, "and moves the revision");
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 3, "only that run left");
    for( int i = 0; i < PanelOverlayStage_KeptCount(&g_stage); i++ )
    {
        TEST_ASSERT(PanelOverlayStage_Kept(&g_stage, i, &item, &owner, &row), "a kept index reads");
        TEST_ASSERT(owner != 12, "no primitive of the retired owner remains");
    }
    PanelOverlayStage_Kept(&g_stage, 2, &item, &owner, &row);
    TEST_ASSERT(owner == 13 && row == 2 && item.x == 140, "the others keep their order, row and place");

    /* A replaced run moves to the end; the others stay in order. */
    draw_well(&g_stage, 7, 11, 0, 30, 1);
    PanelOverlayStage_Kept(&g_stage, 0, &item, &owner, &row);
    TEST_ASSERT(owner == 13, "the untouched owner compacts to the front");
    PanelOverlayStage_Kept(&g_stage, 1, &item, &owner, &row);
    TEST_ASSERT(owner == 11 && item.x == 160, "the redrawn owner's new run follows");

    TEST_ASSERT(PanelOverlayStage_Move(&g_stage, 11, 5, -5), "moving a kept run");
    PanelOverlayStage_Kept(&g_stage, 1, &item, &owner, &row);
    TEST_ASSERT(item.x == 165 && item.clip_y == 195, "shifts it, clip included");
    TEST_ASSERT(!PanelOverlayStage_Move(&g_stage, 11, 0, 0), "a zero move is no move");

    TEST_ASSERT(PanelOverlayStage_StageOwnerLocal(&g_stage, 11, 165, 195) == 1,
                "one owner's run copies out for rastering");
    TEST_ASSERT(PanelOverlayStage_Staged(&g_stage)[0].x == 0, "in well-local coordinates");
    TEST_ASSERT(PanelOverlayStage_StagedCount(&g_stage) == 0, "without opening a well");

    TEST_ASSERT(PanelOverlayStage_Reset(&g_stage, 0), "a reset with runs kept moves the revision");
    TEST_ASSERT(PanelOverlayStage_KeptCount(&g_stage) == 0, "and drops them all");
    TEST_ASSERT(!PanelOverlayStage_Kept(&g_stage, 0, &item, &owner, &row),
                "nothing reads while the list belongs to no selection");
    TEST_ASSERT(!PanelOverlayStage_Reset(&g_stage, 0), "an empty reset to the same generation does not");
}

static void
test_revision_skips_zero(void)
{
    memset(&g_stage, 0, sizeof(g_stage));
    g_stage.revision = 0xFFFFFFFFu;
    PanelOverlayStage_Bump(&g_stage);
    TEST_ASSERT(PanelOverlayStage_Revision(&g_stage) == 1, "the revision wraps past 0");
}

int
main(void)
{
    test_drop_with_no_well_open();
    test_bound_and_overflow();
    test_owner_removal();
    test_revision_skips_zero();
    if( g_failures )
    {
        printf("panel_overlay_stage_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("panel_overlay_stage_test: ok\n");
    return 0;
}
