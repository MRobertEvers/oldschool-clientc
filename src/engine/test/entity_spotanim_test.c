/*
 * The attached-graphic table (engine/entity_spotanim.h).
 *
 * Free entries are -1 so element 0 can be claimed; a lookup under another
 * owner hands the entry back as recycled rather than as a match; a claim takes
 * the first free entry with every slot empty and fails when the table is full;
 * a slot that changes graphic hands its model back once; and a merge is
 * rebuilt only when it is missing, the drawn set changed, or a drawn slot's
 * frame moved.
 *
 * Run: make -C src test-entity-spotanim
 */

#include "engine/entity_spotanim.h"
#include "mem/torirs_mem.h"

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

/* Never dereferenced: the table only stores and hands back the pointers. */
static char g_models[4];
#define MODEL(i) ((struct ToriDraw_Model*)&g_models[(i)])

static struct EntitySpotanims g_table;

static void
fresh(void)
{
    EntitySpotanims_Free(&g_table);
    memset(&g_table, 0, sizeof(g_table));
    EntitySpotanims_Init(&g_table);
}

static void
test_init_frees_every_entry(void)
{
    struct EntitySpotanim* recycled;

    fresh();
    recycled = &g_table.entries[5];
    for( int i = 0; i < g_table.capacity; i++ )
        TEST_ASSERT(g_table.entries[i].body_element_id == -1, "init marks every entry free");
    TEST_ASSERT(EntitySpotanims_Find(&g_table, 0, 0, &recycled) == NULL, "element 0 is not free");
    TEST_ASSERT(recycled == NULL, "a miss sets no recycled entry");
}

static void
test_claim_and_find(void)
{
    struct EntitySpotanim* recycled;
    struct EntitySpotanim* a;
    struct EntitySpotanim* b;

    fresh();
    a = EntitySpotanims_Claim(&g_table, 0, 100);
    TEST_ASSERT(a == &g_table.entries[0], "first claim takes the first entry");
    TEST_ASSERT(a->body_element_id == 0, "element 0 can be claimed");
    TEST_ASSERT(a->owner_entity_id == 100, "the owner is kept");
    TEST_ASSERT(a->combined == NULL && a->body == NULL, "nothing merged");
    TEST_ASSERT(a->slots == NULL && a->slot_count == 0, "no slots until the entity has graphics");
    EntitySpotanim_Slots(a, 3);
    TEST_ASSERT(a->slot_count == 3, "slots for the entity's three graphics");
    for( int i = 0; i < a->slot_count; i++ )
    {
        TEST_ASSERT(a->slots[i].spotanim_id == -1, "every slot empty");
        TEST_ASSERT(a->slots[i].applied_frame == -1, "no frame baked");
        TEST_ASSERT(!a->slots[i].merged, "none merged");
        TEST_ASSERT(a->slots[i].spot == NULL, "no spot model");
    }
    a->slots[1].spotanim_id = 44;
    EntitySpotanim_Slots(a, 20);
    TEST_ASSERT(a->slot_count == 20 && a->slots[1].spotanim_id == 44,
        "the slots grow to twenty graphics and keep the first three");
    TEST_ASSERT(a->slots[19].spotanim_id == -1, "a grown slot is empty");
    EntitySpotanim_Slots(a, 2);
    TEST_ASSERT(a->slot_count == 20, "never shrinks");
    b = EntitySpotanims_Claim(&g_table, 5, 200);
    TEST_ASSERT(b == &g_table.entries[1], "second claim takes the next free entry");

    TEST_ASSERT(EntitySpotanims_Find(&g_table, 5, 200, &recycled) == b, "found by element+owner");
    TEST_ASSERT(recycled == NULL, "a match is not recycled");
    TEST_ASSERT(EntitySpotanims_Find(&g_table, 5, 0, &recycled) == b, "owner 0 matches any owner");
    TEST_ASSERT(EntitySpotanims_Find(&g_table, 6, 200, &recycled) == NULL, "other element misses");
    TEST_ASSERT(recycled == NULL, "a miss is not recycled");

    TEST_ASSERT(EntitySpotanims_Find(&g_table, 5, 300, &recycled) == NULL, "other owner is no match");
    TEST_ASSERT(recycled == b, "other owner hands the entry back as recycled");

    /* A freed entry is claimed again before any later free one. */
    EntitySpotanim_Clear(a);
    TEST_ASSERT(a->body_element_id == -1 && a->owner_entity_id == 0, "clear frees the entry");
    TEST_ASSERT(EntitySpotanims_Claim(&g_table, 9, 400) == a, "the first free entry is reused");
}

/* A3: the 65th entity wearing a graphic used to get no entry, and its
 * graphic never drew. The table grows instead. */
static void
test_claim_grows_when_full(void)
{
    struct EntitySpotanim* recycled;
    int all_found = 1;

    fresh();
    for( int i = 0; i < ENTITY_SPOTANIMS_INITIAL; i++ )
        TEST_ASSERT(EntitySpotanims_Claim(&g_table, i, i + 1) != NULL, "claim while room");
    TEST_ASSERT(EntitySpotanims_Claim(&g_table, 1000, 1) != NULL, "a full table grows");
    TEST_ASSERT(g_table.capacity == 2 * ENTITY_SPOTANIMS_INITIAL, "to twice its size");
    for( int i = 0; i < ENTITY_SPOTANIMS_INITIAL; i++ )
        if( EntitySpotanims_Find(&g_table, i, i + 1, &recycled) == NULL )
            all_found = 0;
    TEST_ASSERT(all_found, "every earlier entry survives the growth");
    TEST_ASSERT(EntitySpotanims_Find(&g_table, 1000, 1, &recycled) != NULL, "and the new one");
    for( int i = 0; i < 300; i++ )
        TEST_ASSERT(EntitySpotanims_Claim(&g_table, 2000 + i, 1) != NULL, "a crowd of 300 more");
    EntitySpotanim_Clear(&g_table.entries[17]);
    TEST_ASSERT(EntitySpotanims_Claim(&g_table, 5000, 1) == &g_table.entries[17],
        "a freed entry is reused before the table grows again");
}

static void
test_slot_want(void)
{
    struct EntitySpotanimSlot slot = { .spotanim_id = 12, .applied_frame = 3, .spot = MODEL(0) };

    TEST_ASSERT(EntitySpotanim_SlotWant(&slot, 12) == NULL, "same graphic hands nothing back");
    TEST_ASSERT(slot.spot == MODEL(0) && slot.applied_frame == 3, "same graphic keeps its model");
    TEST_ASSERT(EntitySpotanim_SlotWant(&slot, 13) == MODEL(0), "new graphic hands the model back");
    TEST_ASSERT(slot.spotanim_id == 13, "new graphic is taken");
    TEST_ASSERT(slot.spot == NULL && slot.applied_frame == -1, "new graphic has no model or frame");
    TEST_ASSERT(EntitySpotanim_SlotWant(&slot, -1) == NULL, "ending with no model hands nothing");
    TEST_ASSERT(slot.spotanim_id == -1, "ended slot is empty");
}

static void
test_merge_rule(void)
{
    enum
    {
        N = 12
    };
    struct EntitySpotanim* e;
    int frames[N] = { 0 };
    bool draw[N] = { 0 };

    fresh();
    e = EntitySpotanims_Claim(&g_table, 3, 7);
    EntitySpotanim_Slots(e, N);
    frames[0] = 4;
    frames[2] = 9;
    frames[11] = 6;
    draw[0] = draw[2] = draw[11] = true;
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, N), "no merge yet: rebuild");
    EntitySpotanim_NoteMerged(e, MODEL(1), draw, frames, N);
    TEST_ASSERT(e->combined == MODEL(1), "merge noted");
    TEST_ASSERT(e->slots[0].merged && e->slots[2].merged && e->slots[11].merged && !e->slots[1].merged,
        "the drawn slots are the merged ones, the twelfth too");
    TEST_ASSERT(e->slots[0].applied_frame == 4 && e->slots[2].applied_frame == 9 &&
                    e->slots[11].applied_frame == 6,
        "frames baked");
    TEST_ASSERT(e->slots[1].applied_frame == -1, "undrawn slot bakes nothing");
    TEST_ASSERT(!EntitySpotanim_NeedsMerge(e, draw, frames, N), "same slots, same frames: keep");

    /* A frame in a slot that is not drawn is never read. */
    frames[1] = 77;
    TEST_ASSERT(!EntitySpotanim_NeedsMerge(e, draw, frames, N), "undrawn frame is not read");
    frames[11] = 10;
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, N), "a drawn frame moved: rebuild");
    frames[11] = 6;
    draw[2] = false;
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, N), "a slot left the set: rebuild");
    draw[2] = true;
    draw[7] = true;
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, N), "a slot joined the set: rebuild");
    draw[7] = false;

    /* A slot past spot_count is not drawn: a merged one there is a rebuild. */
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, 3), "the twelfth left: rebuild");

    EntitySpotanim_ForgetMerge(e);
    TEST_ASSERT(e->combined == NULL, "forgotten merge");
    for( int i = 0; i < e->slot_count; i++ )
        TEST_ASSERT(e->slots[i].applied_frame == -1 && !e->slots[i].merged, "forgotten frames");
    TEST_ASSERT(EntitySpotanim_NeedsMerge(e, draw, frames, N), "forgotten merge: rebuild");
}

int
main(void)
{
    ToriMem_Init();
    test_init_frees_every_entry();
    test_claim_and_find();
    test_claim_grows_when_full();
    test_slot_want();
    test_merge_rule();
    EntitySpotanims_Free(&g_table);
    if( g_failures )
    {
        printf("entity_spotanim_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("entity_spotanim_test: ok\n");
    return 0;
}
