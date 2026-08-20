#include "inv_manager.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define CHANGE_LOG_MAX 64

struct ChangeLog
{
    int ids[CHANGE_LOG_MAX];
    int count;
};

static void
change_log_reset(struct ChangeLog* log)
{
    log->count = 0;
}

static void
on_change(
    void* userdata,
    int container_id)
{
    struct ChangeLog* log = userdata;
    if( log->count < CHANGE_LOG_MAX )
        log->ids[log->count++] = container_id;
}

static void
test_init_free(void)
{
    printf("TEST: init/free\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    TEST_ASSERT(mgr.container_count == 0, "container_count zero");
    TEST_ASSERT(mgr.source_count == 0, "source_count zero");
    TEST_ASSERT(!InvManager_SelectionIsSet(&mgr), "selection unset");

    InvManager_Free(&mgr);
    InvManager_Free(&mgr); /* idempotent */
}

static void
test_resolve_sources(void)
{
    printf("TEST: resolve inventory/worn sources\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const inv = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    int const worn = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_WORN);
    TEST_ASSERT(inv != INV_MANAGER_SOURCE_INVALID, "resolve inventory");
    TEST_ASSERT(worn != INV_MANAGER_SOURCE_INVALID, "resolve worn");
    TEST_ASSERT(inv != worn, "distinct sources");

    TEST_ASSERT(
        InvManager_ContainerForSource(&mgr, inv) == INV_MANAGER_CONTAINER_BACKPACK,
        "inventory -> 93");
    TEST_ASSERT(
        InvManager_ContainerForSource(&mgr, worn) == INV_MANAGER_CONTAINER_WORN,
        "worn -> 94");
    TEST_ASSERT(
        InvManager_Size(&mgr, INV_MANAGER_CONTAINER_BACKPACK) ==
            INV_MANAGER_DEFAULT_BACKPACK_SLOTS,
        "backpack size 28");
    TEST_ASSERT(
        InvManager_Size(&mgr, INV_MANAGER_CONTAINER_WORN) == INV_MANAGER_DEFAULT_WORN_SLOTS,
        "worn size 14");

    int const again = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    TEST_ASSERT(again == inv, "resolve is idempotent");

    InvManager_Free(&mgr);
}

static void
test_ensure_bank_large(void)
{
    printf("TEST: ensure bank with large slot count\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const src = InvManager_EnsureContainer(&mgr, INV_MANAGER_CONTAINER_BANK, 100, "bank");
    TEST_ASSERT(src != INV_MANAGER_SOURCE_INVALID, "ensure bank");
    TEST_ASSERT(InvManager_Size(&mgr, INV_MANAGER_CONTAINER_BANK) == 100, "bank size 100");

    struct InvSlot slot = {
        .obj_id = 4151,
        .obj_count = 1,
        .scene_id = 42,
        .atlas_index = 3,
    };
    TEST_ASSERT(InvManager_SetSlot(&mgr, src, 99, &slot), "set last bank slot");

    struct InvSlot out;
    TEST_ASSERT(InvManager_GetSlot(&mgr, src, 99, &out), "get last bank slot");
    TEST_ASSERT(out.obj_id == 4151, "obj_id");
    TEST_ASSERT(out.obj_count == 1, "obj_count");
    TEST_ASSERT(out.scene_id == 42, "scene_id");
    TEST_ASSERT(out.atlas_index == 3, "atlas_index");

    TEST_ASSERT(!InvManager_GetSlot(&mgr, src, 100, &out), "oob slot false");

    InvManager_Free(&mgr);
}

static void
test_set_get_clear_slot(void)
{
    printf("TEST: set/get/clear slot\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const src = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    struct InvSlot slot = {
        .obj_id = 995,
        .obj_count = 1000,
        .scene_id = 7,
        .atlas_index = 1,
    };
    TEST_ASSERT(InvManager_SetSlot(&mgr, src, 0, &slot), "set slot");

    struct InvSlot out;
    TEST_ASSERT(InvManager_GetSlot(&mgr, src, 0, &out), "get slot");
    TEST_ASSERT(out.obj_id == 995 && out.obj_count == 1000, "values");
    TEST_ASSERT(out.scene_id == 7 && out.atlas_index == 1, "icon refs");

    TEST_ASSERT(InvManager_ClearSlot(&mgr, src, 0), "clear");
    TEST_ASSERT(InvManager_GetSlot(&mgr, src, 0, &out), "get cleared");
    TEST_ASSERT(out.obj_id == INV_MANAGER_EMPTY_OBJ_ID, "empty obj");
    TEST_ASSERT(out.scene_id == INV_MANAGER_NO_SCENE_ID, "no scene");

    TEST_ASSERT(!InvManager_SetSlot(&mgr, INV_MANAGER_SOURCE_INVALID, 0, &slot), "bad source");
    TEST_ASSERT(!InvManager_GetSlot(&mgr, src, -1, &out), "neg slot");

    InvManager_Free(&mgr);
}

static void
test_apply_full_partial(void)
{
    printf("TEST: apply full/partial\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const src = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    (void)src;

    int ids[4] = { 1, 2, 0, 4 };
    int counts[4] = { 1, 5, 0, 2 };
    TEST_ASSERT(
        InvManager_ApplyFull(&mgr, INV_MANAGER_CONTAINER_BACKPACK, ids, counts, 4),
        "apply full");

    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 0) == 1, "slot0");
    TEST_ASSERT(InvManager_GetNum(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 1) == 5, "slot1 count");
    TEST_ASSERT(
        InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 2) == INV_MANAGER_EMPTY_OBJ_ID,
        "slot2 empty");
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 3) == 4, "slot3");

    /* Remaining slots cleared. */
    TEST_ASSERT(
        InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 5) == INV_MANAGER_EMPTY_OBJ_ID,
        "slot5 cleared");

    int slots[2] = { 2, 5 };
    int pids[2] = { 99, 0 };
    int pcounts[2] = { 3, 0 };
    TEST_ASSERT(
        InvManager_ApplyPartial(
            &mgr, INV_MANAGER_CONTAINER_BACKPACK, slots, pids, pcounts, 2),
        "apply partial");
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 2) == 99, "partial set");
    TEST_ASSERT(InvManager_GetNum(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 2) == 3, "partial count");
    TEST_ASSERT(
        InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 5) == INV_MANAGER_EMPTY_OBJ_ID,
        "partial clear");
    /* Untouched slot stays. */
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 0) == 1, "untouched");

    InvManager_Free(&mgr);
}

static void
test_total_and_size(void)
{
    printf("TEST: total and size\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);

    int ids[5] = { 4151, 4151, 0, 4151, 100 };
    int counts[5] = { 1, 2, 0, 3, 10 };
    InvManager_ApplyFull(&mgr, INV_MANAGER_CONTAINER_BACKPACK, ids, counts, 5);

    TEST_ASSERT(InvManager_Total(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 4151) == 6, "total 4151");
    TEST_ASSERT(InvManager_Total(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 100) == 10, "total 100");
    TEST_ASSERT(InvManager_Total(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 999) == 0, "missing");
    TEST_ASSERT(
        InvManager_Size(&mgr, INV_MANAGER_CONTAINER_BACKPACK) ==
            INV_MANAGER_DEFAULT_BACKPACK_SLOTS,
        "size");
    TEST_ASSERT(InvManager_Size(&mgr, 9999) == 0, "unknown size");

    InvManager_Free(&mgr);
}

static void
test_change_callback(void)
{
    printf("TEST: change callback\n");

    struct InvManager mgr;
    struct ChangeLog log;
    InvManager_Init(&mgr);
    change_log_reset(&log);
    InvManager_SetChangeCallback(&mgr, on_change, &log);

    int const src = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    struct InvSlot slot = { .obj_id = 1, .obj_count = 1, .scene_id = -1, .atlas_index = 0 };
    InvManager_SetSlot(&mgr, src, 0, &slot);
    TEST_ASSERT(log.count == 1, "set notifies");
    TEST_ASSERT(log.ids[0] == INV_MANAGER_CONTAINER_BACKPACK, "container id");

    change_log_reset(&log);
    int ids[1] = { 2 };
    int counts[1] = { 1 };
    InvManager_ApplyFull(&mgr, INV_MANAGER_CONTAINER_BACKPACK, ids, counts, 1);
    TEST_ASSERT(log.count == 1, "apply full notifies");

    change_log_reset(&log);
    int slots[1] = { 1 };
    int pids[1] = { 3 };
    int pcounts[1] = { 1 };
    InvManager_ApplyPartial(&mgr, INV_MANAGER_CONTAINER_BACKPACK, slots, pids, pcounts, 1);
    TEST_ASSERT(log.count == 1, "apply partial notifies");

    InvManager_Free(&mgr);
}

static void
test_selection(void)
{
    printf("TEST: selection\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    TEST_ASSERT(!InvManager_SelectionIsSet(&mgr), "unset");
    InvManager_SelectionSet(&mgr, 0, 3);
    TEST_ASSERT(InvManager_SelectionIsSet(&mgr), "set");
    TEST_ASSERT(InvManager_SelectionMatches(&mgr, 0, 3), "match");
    TEST_ASSERT(!InvManager_SelectionMatches(&mgr, 0, 4), "no match slot");
    TEST_ASSERT(!InvManager_SelectionMatches(&mgr, 1, 3), "no match source");

    InvManager_SelectionClear(&mgr);
    TEST_ASSERT(!InvManager_SelectionIsSet(&mgr), "cleared");

    InvManager_Free(&mgr);
}

static void
test_swap_slots(void)
{
    printf("TEST: swap slots\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const src = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    struct InvSlot a = { .obj_id = 10, .obj_count = 1, .scene_id = 1, .atlas_index = 0 };
    struct InvSlot b = { .obj_id = 20, .obj_count = 5, .scene_id = 2, .atlas_index = 1 };
    InvManager_SetSlot(&mgr, src, 0, &a);
    InvManager_SetSlot(&mgr, src, 1, &b);
    InvManager_SelectionSet(&mgr, src, 0);

    TEST_ASSERT(InvManager_SwapSlots(&mgr, src, 0, 1), "swap");

    struct InvSlot out0, out1;
    InvManager_GetSlot(&mgr, src, 0, &out0);
    InvManager_GetSlot(&mgr, src, 1, &out1);
    TEST_ASSERT(out0.obj_id == 20 && out0.obj_count == 5, "slot0 was b");
    TEST_ASSERT(out1.obj_id == 10 && out1.obj_count == 1, "slot1 was a");
    TEST_ASSERT(InvManager_SelectionMatches(&mgr, src, 1), "selection followed");

    TEST_ASSERT(!InvManager_SwapSlots(&mgr, src, 0, 99), "oob false");
    TEST_ASSERT(InvManager_SwapSlots(&mgr, src, 0, 0), "same slot ok");

    InvManager_Free(&mgr);
}

static void
test_clear_selected_slot_deselects(void)
{
    printf("TEST: clear selected slot deselects\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    int const src = InvManager_ResolveSource(&mgr, INV_MANAGER_SOURCE_NAME_BACKPACK);
    struct InvSlot slot = { .obj_id = 1, .obj_count = 1, .scene_id = -1, .atlas_index = 0 };
    InvManager_SetSlot(&mgr, src, 2, &slot);
    InvManager_SelectionSet(&mgr, src, 2);
    InvManager_ClearSlot(&mgr, src, 2);
    TEST_ASSERT(!InvManager_SelectionIsSet(&mgr), "deselected");

    InvManager_Free(&mgr);
}

/*
 * The bug this pins: a container sized from the first packet about it, then
 * unable to hold anything past that size for the rest of the session.
 *
 * ToriRSServer writes UPDATE_INV_FULL's capacity as the *used prefix* of the
 * container, so a login that restored 17 backpack items announces 17 slots.
 * Every UPDATE_INV_PARTIAL after that names a real slot 0..27, and the eleven
 * above the prefix were being dropped: the server placed the item (it fills the
 * first free slot, and always did), the client never heard about it, and the
 * inventory grid drew an empty cell there. `::give` then said "did not fit"
 * while the player was looking at cells that only existed server-side.
 */
static void
test_container_grows_past_first_capacity(void)
{
    printf("TEST: a container widens past the capacity it was first announced with\n");

    struct InvManager mgr;
    InvManager_Init(&mgr);

    /* Login: UPDATE_INV_FULL for the backpack, capacity = the used prefix. */
    int const src =
        InvManager_EnsureContainer(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 17, "server-inv");
    TEST_ASSERT(src != INV_MANAGER_SOURCE_INVALID, "backpack registered");

    /* UPDATE_INV_PARTIAL: the server put a raw chicken in slot 17. */
    struct InvSlot chicken = { .obj_id = 2138, .obj_count = 1, .scene_id = -1, .atlas_index = 0 };
    TEST_ASSERT(InvManager_SetSlot(&mgr, src, 17, &chicken), "slot past the prefix accepted");

    struct InvSlot out;
    TEST_ASSERT(InvManager_GetSlot(&mgr, src, 17, &out), "slot 17 readable");
    TEST_ASSERT(out.obj_id == 2138, "slot 17 holds the chicken");
    TEST_ASSERT(InvManager_Size(&mgr, INV_MANAGER_CONTAINER_BACKPACK) >= 18, "container widened");

    /* And the whole 28-slot backpack is reachable, one partial at a time. */
    struct InvSlot last = { .obj_id = 2140, .obj_count = 1, .scene_id = -1, .atlas_index = 0 };
    TEST_ASSERT(InvManager_SetSlot(&mgr, src, 27, &last), "last backpack slot accepted");
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 27) == 2140, "slot 27");

    /* A later, shorter FULL still clears its tail — that is the server saying
     * those slots are empty, not that the container shrank. */
    int const ids[3] = { 995, 0, 0 };
    int const counts[3] = { 12, 0, 0 };
    TEST_ASSERT(InvManager_ApplyFull(&mgr, INV_MANAGER_CONTAINER_BACKPACK, ids, counts, 3),
                "short full applied");
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BACKPACK, 27) ==
                    INV_MANAGER_EMPTY_OBJ_ID,
                "tail cleared");
    TEST_ASSERT(InvManager_Size(&mgr, INV_MANAGER_CONTAINER_BACKPACK) >= 28, "and still 28 wide");

    /* A FULL wider than the container widens it rather than truncating: the
     * bank's shape, where every update is a whole-container resend. */
    int bank_ids[40];
    int bank_counts[40];
    for( int i = 0; i < 40; i++ )
    {
        bank_ids[i] = 1000 + i;
        bank_counts[i] = 1;
    }
    TEST_ASSERT(InvManager_EnsureContainer(&mgr, INV_MANAGER_CONTAINER_BANK, 8, "bank") !=
                    INV_MANAGER_SOURCE_INVALID,
                "bank registered at its prefix");
    TEST_ASSERT(InvManager_ApplyFull(&mgr, INV_MANAGER_CONTAINER_BANK, bank_ids, bank_counts, 40),
                "wider bank full applied");
    TEST_ASSERT(InvManager_GetObj(&mgr, INV_MANAGER_CONTAINER_BANK, 39) == 1039,
                "bank slot 39 survived");

    InvManager_Free(&mgr);
}

int
main(void)
{
    test_init_free();
    test_resolve_sources();
    test_ensure_bank_large();
    test_set_get_clear_slot();
    test_apply_full_partial();
    test_total_and_size();
    test_change_callback();
    test_selection();
    test_swap_slots();
    test_clear_selected_slot_deselects();
    test_container_grows_past_first_capacity();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
