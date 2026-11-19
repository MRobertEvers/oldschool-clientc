/*
 * The PLAYER_INFO / NPC_INFO decoders' kept op lists.
 *
 * The first borrow allocates the slot's list and every later one hands back
 * the same memory; a borrow while the kept list is lent gets a list of its
 * own, freed on its release, and the kept list stays lent until it comes
 * back. Free forgets both kept lists.
 *
 * rsprot_info_list_init is stubbed: the real one lives in the rsprot unity
 * objects, and all this needs from it is that `list` points at what it was
 * given.
 *
 *   make -C src test-entity-info-scratch
 */
#include "game/entity_info_scratch.h"
#include "streams/rsprot_info.h"

#include <stdio.h>
#include <string.h>

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

void
rsprot_info_list_init(
    RsprotInfoList* list,
    RsprotInfoOp* ops,
    int cap,
    uint8_t* bytes,
    int bytes_cap)
{
    memset(list, 0, sizeof(*list));
    list->ops = ops;
    list->cap = cap;
    list->bytes = bytes;
    list->bytes_cap = bytes_cap;
}

static void
test_borrow_keeps_one_list(void)
{
    struct EntityInfoScratch scratch;
    struct RsprotInfoList list;
    struct RsprotInfoOp* first_ops;
    uint8_t* first_bytes;

    memset(&scratch, 0, sizeof(scratch));
    memset(&list, 0, sizeof(list));

    EntityInfoScratch_Borrow(&scratch.npc, &list);
    CHECK(scratch.npc.ops != NULL, "the first borrow did not allocate the kept ops");
    CHECK(scratch.npc.bytes != NULL, "the first borrow did not allocate the kept bytes");
    CHECK(scratch.npc.busy == 1, "a lent list is not marked busy");
    CHECK(list.ops == scratch.npc.ops, "the list is not the kept ops");
    CHECK(list.bytes == scratch.npc.bytes, "the list is not the kept bytes");
    CHECK(list.cap == ENTITY_INFO_SCRATCH_OPS_MAX, "the list cap is %d", list.cap);
    CHECK(list.bytes_cap == ENTITY_INFO_SCRATCH_BYTES_MAX, "the bytes cap is %d", list.bytes_cap);
    CHECK(scratch.player.ops == NULL, "borrowing the npc slot allocated the player slot");
    first_ops = scratch.npc.ops;
    first_bytes = scratch.npc.bytes;

    EntityInfoScratch_Release(&scratch.npc, &list);
    CHECK(list.ops == NULL, "a released list still points at its ops");
    CHECK(scratch.npc.busy == 0, "a released kept list is still busy");
    CHECK(scratch.npc.ops == first_ops, "release dropped the kept ops");

    /* The next packet reuses the same memory. */
    EntityInfoScratch_Borrow(&scratch.npc, &list);
    CHECK(list.ops == first_ops, "a second borrow did not reuse the kept ops");
    CHECK(list.bytes == first_bytes, "a second borrow did not reuse the kept bytes");
    EntityInfoScratch_Release(&scratch.npc, &list);

    EntityInfoScratch_Free(&scratch);
    CHECK(scratch.npc.ops == NULL, "Free kept the npc ops");
    CHECK(scratch.npc.bytes == NULL, "Free kept the npc bytes");
    CHECK(scratch.npc.busy == 0, "Free left the npc slot busy");
}

static void
test_busy_slot_lends_a_fallback(void)
{
    struct EntityInfoScratch scratch;
    struct RsprotInfoList kept;
    struct RsprotInfoList fallback;

    memset(&scratch, 0, sizeof(scratch));
    memset(&kept, 0, sizeof(kept));
    memset(&fallback, 0, sizeof(fallback));

    EntityInfoScratch_Borrow(&scratch.player, &kept);
    EntityInfoScratch_Borrow(&scratch.player, &fallback);
    CHECK(fallback.ops != NULL, "a busy slot lent no list");
    CHECK(fallback.ops != kept.ops, "a busy slot lent its kept list twice");
    CHECK(fallback.bytes != kept.bytes, "a busy slot lent its kept bytes twice");
    CHECK(fallback.cap == ENTITY_INFO_SCRATCH_OPS_MAX, "the fallback cap is %d", fallback.cap);
    CHECK(scratch.player.ops == kept.ops, "the fallback replaced the kept list");

    /* Handing the fallback back frees it and leaves the kept list lent. */
    EntityInfoScratch_Release(&scratch.player, &fallback);
    CHECK(fallback.ops == NULL, "a released fallback still points at its ops");
    CHECK(scratch.player.busy == 1, "releasing the fallback freed the kept list");

    EntityInfoScratch_Release(&scratch.player, &kept);
    CHECK(scratch.player.busy == 0, "the kept list did not come back");

    EntityInfoScratch_Free(&scratch);
    CHECK(scratch.player.ops == NULL, "Free kept the player ops");
}

static void
test_release_of_nothing_is_left_alone(void)
{
    struct EntityInfoScratch scratch;
    struct RsprotInfoList never;

    memset(&scratch, 0, sizeof(scratch));
    memset(&never, 0, sizeof(never));

    /* A task freed before it ever borrowed hands back an empty list. */
    EntityInfoScratch_Release(&scratch.npc, &never);
    CHECK(scratch.npc.busy == 0, "releasing nothing changed the slot");
    CHECK(scratch.npc.ops == NULL, "releasing nothing allocated");

    /* Free on scratch that never lent. */
    EntityInfoScratch_Free(&scratch);
    CHECK(scratch.npc.ops == NULL, "Free on unlent scratch allocated");
}

int
main(void)
{
    test_borrow_keeps_one_list();
    test_busy_slot_lends_a_fallback();
    test_release_of_nothing_is_left_alone();

    if( g_failures )
    {
        printf("entity_info_scratch_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("entity_info_scratch_test: OK\n");
    return 0;
}
