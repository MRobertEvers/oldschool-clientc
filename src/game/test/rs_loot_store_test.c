/*
 * Unit test for the client-native loot tracker store.
 *
 * Mirrors varc_manager_test.c: standalone, no cache, no server. The assertions
 * reproduce the read loop in decompiled script 7166: AddKillLoot feeds the
 * store, then BeginQuery + QueryId + SourceName + SourceKillCount must return
 * non-zero rows. Opcode 7604 is kill count (scroll height → "Name x N"), not
 * total GP value.
 */

#include "game/rs_loot_store.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                      \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ======================================================================== */
/* 1. Init / Free / ResetAll                                                */
/* ======================================================================== */

static void
test_init_free(void)
{
    printf("TEST: init/free\n");

    struct LootStore store;
    LootStore_Init(&store);

    TEST_ASSERT(LootStore_SourceCount(&store) == 0, "empty after init");
    TEST_ASSERT(LootStore_VectorSize(&store, 1) == 0, "no string vector entries");

    LootStore_Free(&store);
    LootStore_Free(&store); /* double free safe */
}

/* ======================================================================== */
/* 2. AddKillLoot + source queries                                          */
/* ======================================================================== */

static void
test_add_kill_loot(void)
{
    printf("TEST: AddKillLoot + source queries\n");

    struct LootStore store;
    LootStore_Init(&store);

    /* One kill, two items — same event_id → kill_count == 1 */
    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    TEST_ASSERT(LootStore_SourceCount(&store) == 1, "one source");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Goblin") == 1, "first kill");

    LootStore_AddKillLoot(&store, "Goblin", 101, 3, 10, 1);
    TEST_ASSERT(LootStore_SourceCount(&store) == 1, "still one source");
    TEST_ASSERT(LootStore_SourceItemCount(&store, "Goblin") == 2, "two rows");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Goblin") == 1, "same event: still 1 kill");

    /* Same obj, new event → merge qty and bump kills */
    LootStore_AddKillLoot(&store, "Goblin", 100, 2, 5, 2);
    TEST_ASSERT(LootStore_SourceItemCount(&store, "Goblin") == 2, "merge same obj_id");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Goblin") == 2, "new event: 2 kills");

    int obj, qty;
    TEST_ASSERT(LootStore_RowByName(&store, "Goblin", 1, &obj, &qty), "row 1 exists");
    TEST_ASSERT(obj == 100, "row 0 obj");
    TEST_ASSERT(qty == 3, "row 0 merged qty");

    LootStore_AddKillLoot(&store, "Imp", 200, 1, 20, 10);
    TEST_ASSERT(LootStore_SourceCount(&store) == 2, "two sources");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Imp") == 1, "Imp one kill");

    TEST_ASSERT(LootStore_SourceItemCount(&store, "Unknown") == 0, "unknown source: 0 items");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Unknown") == 0, "unknown source: 0 kills");

    LootStore_Free(&store);
}

/*
 * What a row's `value` MEANS.
 *
 * It is the unit price -- `ObjType.cost` for one of them -- and never the
 * stack's total, which is what the plugin API has documented it to be since
 * the field existed. The store used to accumulate `value * qty` into it, and
 * the one reader that follows the stated contract multiplied by the quantity a
 * SECOND time: ten thousand coins at a cost of one came out of the Loot
 * Tracker's totals band as a hundred million gp.
 *
 * MUTATION: `row->value = value * qty` on the create arm reddens the first
 * assertion; `src->rows[i].value += value` on the merge arm reddens the third.
 */
static void
test_row_value_is_the_unit_price(void)
{
    printf("TEST: a row's value is what ONE of them costs\n");

    struct LootStore store;
    LootStore_Init(&store);

    /* Five thousand coins at a cache cost of one. */
    LootStore_AddKillLoot(&store, "Goblin", 995, 5000, 1, 1);
    TEST_ASSERT(store.source_count == 1, "one source");
    TEST_ASSERT(store.sources[0].rows[0].qty == 5000, "five thousand of them");
    TEST_ASSERT(
        store.sources[0].rows[0].value == 1,
        "and one gp each, not five thousand");

    /* A second kill of the same pile. The quantities add up; the unit price is
     * a property of the objtype and does not. */
    LootStore_AddKillLoot(&store, "Goblin", 995, 5000, 1, 2);
    TEST_ASSERT(store.sources[0].rows[0].qty == 10000, "ten thousand of them");
    TEST_ASSERT(
        store.sources[0].rows[0].value == 1,
        "still one gp each after a merge");
    TEST_ASSERT(
        (long long)store.sources[0].rows[0].value * store.sources[0].rows[0].qty ==
            10000,
        "so the pile is worth ten thousand gp, not a hundred million");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 3. BeginQuery + QueryId (script 7166 read loop)                          */
/* ======================================================================== */

static void
test_begin_query(void)
{
    printf("TEST: BeginQuery/QueryId (7166 read loop)\n");

    struct LootStore store;
    LootStore_Init(&store);

    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    LootStore_AddKillLoot(&store, "Imp", 200, 2, 10, 2);
    LootStore_AddKillLoot(&store, "Guard", 300, 1, 50, 3);

    /* Script 7166 line: _7605(0, min(source_count, 10), 1) */
    int n = LootStore_BeginQuery(&store, 0, 10, 1);
    TEST_ASSERT(n == 3, "query returns 3 sources");

    /* _7606(0..2) → source ids */
    int id0 = LootStore_QueryId(&store, 0);
    int id1 = LootStore_QueryId(&store, 1);
    int id2 = LootStore_QueryId(&store, 2);
    TEST_ASSERT(id0 > 0, "id0 > 0");
    TEST_ASSERT(id1 > 0, "id1 > 0");
    TEST_ASSERT(id2 > 0, "id2 > 0");
    TEST_ASSERT(id0 != id1 && id1 != id2 && id0 != id2, "ids are unique");

    /* _7602(id) → name */
    const char* name0 = LootStore_SourceName(&store, id0);
    TEST_ASSERT(strcmp(name0, "Goblin") == 0, "id → name roundtrip");

    /* _7604(name) → kill count */
    int kills = LootStore_SourceKillCount(&store, "Imp");
    TEST_ASSERT(kills == 1, "Imp kill count");

    /* out of range */
    TEST_ASSERT(LootStore_QueryId(&store, -1) == -1, "negative index → -1");
    TEST_ASSERT(LootStore_QueryId(&store, 99) == -1, "oversize index → -1");

    /* limit < total */
    n = LootStore_BeginQuery(&store, 0, 2, 1);
    TEST_ASSERT(n == 2, "limited query returns 2");

    /* start offset */
    n = LootStore_BeginQuery(&store, 1, 10, 1);
    TEST_ASSERT(n == 2, "offset query returns 2");

    /* kind=3 (second loop in 7166) */
    n = LootStore_BeginQuery(&store, 0, 10, 3);
    TEST_ASSERT(n == 3, "kind=3 query returns 3");
    id0 = LootStore_QueryId(&store, 0);
    const char* name3 = LootStore_SourceName(&store, id0);
    TEST_ASSERT(strlen(name3) > 0, "kind=3 id resolves to name");

    /* kind=2 (Clear-data cleanup in 7179) also walks sources */
    n = LootStore_BeginQuery(&store, 0, 10, 2);
    TEST_ASSERT(n == 3, "kind=2 query returns 3 sources");
    id0 = LootStore_QueryId(&store, 0);
    TEST_ASSERT(strlen(LootStore_SourceName(&store, id0)) > 0, "kind=2 id → name");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 4. Row access by id and by name                                          */
/* ======================================================================== */

static void
test_row_access(void)
{
    printf("TEST: row access by id and name\n");

    struct LootStore store;
    LootStore_Init(&store);

    LootStore_AddKillLoot(&store, "Goblin", 100, 5, 2, 1);
    LootStore_AddKillLoot(&store, "Goblin", 101, 3, 7, 1);

    TEST_ASSERT(LootStore_RowCountByName(&store, "Goblin") == 2, "2 rows by name");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Goblin") == 1, "one kill two rows");

    int n = LootStore_BeginQuery(&store, 0, 10, 1);
    TEST_ASSERT(n == 1, "one source");
    int id = LootStore_QueryId(&store, 0);

    TEST_ASSERT(LootStore_RowCountById(&store, id) == 2, "2 rows by id");

    int obj, qty;
    TEST_ASSERT(LootStore_RowById(&store, id, 1, &obj, &qty), "row 1 by id");
    TEST_ASSERT(obj == 100 && qty == 5, "row 1 values by id");

    TEST_ASSERT(LootStore_RowById(&store, id, 2, &obj, &qty), "row 2 by id");
    TEST_ASSERT(obj == 101 && qty == 3, "row 2 values by id");

    TEST_ASSERT(!LootStore_RowById(&store, id, 0, &obj, &qty), "0-based rejected");
    TEST_ASSERT(!LootStore_RowById(&store, id, 3, &obj, &qty), "row 3 out of range");
    TEST_ASSERT(!LootStore_RowById(&store, 9999, 1, &obj, &qty), "bad source id");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 5. Aux string lists (ops 7401/7404/7407/7408/7409)                       */
/* ======================================================================== */

static void
test_aux_lists(void)
{
    printf("TEST: string vectors (7400..7409)\n");

    struct LootStore store;
    LootStore_Init(&store);

    /* 7400 APPEND keeps duplicates; only 7401 APPEND_UNIQUE de-duplicates. */
    LootStore_VectorAppend(&store, 2, "Bones");
    LootStore_VectorAppend(&store, 2, "Bones");
    TEST_ASSERT(LootStore_VectorSize(&store, 2) == 2, "append keeps a duplicate");
    LootStore_VectorClear(&store, 2);

    /* Script 7200: _7409(2), then stringvector_addunique(2, name, 0) -- case
     * insensitive, since its flag is case_sensitive and it passes 0. */
    LootStore_VectorAppendUnique(&store, 2, "Bones", false);
    LootStore_VectorAppendUnique(&store, 2, "Coins", false);
    LootStore_VectorAppendUnique(&store, 2, "BONES", false);
    TEST_ASSERT(LootStore_VectorSize(&store, 2) == 2, "unique, case-insensitive: BONES is Bones");
    LootStore_VectorAppendUnique(&store, 2, "BONES", true);
    TEST_ASSERT(LootStore_VectorSize(&store, 2) == 3, "unique, case-sensitive: BONES is new");

    /* Order is the vector's, and erasing keeps it. */
    LootStore_VectorErase(&store, 2, "bones", false);
    TEST_ASSERT(LootStore_VectorSize(&store, 2) == 2, "erase removes one entry");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 0), "Coins") == 0, "order kept after erase (0)");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 1), "BONES") == 0, "order kept after erase (1)");

    /* 7402 INSERT, 7403 SET, 7405 REMOVEAT */
    LootStore_VectorInsert(&store, 2, 1, "Ashes");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 1), "Ashes") == 0, "insert lands at its index");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 2), "BONES") == 0, "insert shifts the rest up");
    LootStore_VectorSet(&store, 2, 0, "Feathers");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 0), "Feathers") == 0, "set replaces in place");
    LootStore_VectorEraseAt(&store, 2, 0);
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 0), "Ashes") == 0, "removeat shifts the rest down");
    TEST_ASSERT(strcmp(LootStore_VectorGet(&store, 2, 5), "") == 0, "get out of range is empty");

    /* 7408 CONTAINS: wildcard entries are patterns. */
    LootStore_VectorClear(&store, 3);
    LootStore_VectorAppend(&store, 3, "*rune*");
    TEST_ASSERT(LootStore_VectorContains(&store, 3, "Chaos rune", true, false), "wildcard matches");
    TEST_ASSERT(!LootStore_VectorContains(&store, 3, "Chaos rune", false, false), "no wildcard: literal only");
    TEST_ASSERT(!LootStore_VectorContains(&store, 3, "CHAOS RUNE", true, true), "case-sensitive wildcard");
    TEST_ASSERT(LootStore_VectorContains(&store, 3, "CHAOS RUNE", true, false), "case-insensitive wildcard");

    /* The cache uses ids up to 6; this store used to hold 5. */
    LootStore_VectorAppend(&store, 6, "sixth");
    TEST_ASSERT(LootStore_VectorSize(&store, 6) == 1, "vector 6 exists");

    /* Revisions: a real change bumps only its own vector's. */
    LootStore_VectorAppend(&store, 4, "Highlight*");
    uint64_t filter_revision = LootStore_VectorRevision(&store, 3);
    uint64_t highlight_revision = LootStore_VectorRevision(&store, 4);
    TEST_ASSERT(filter_revision && highlight_revision, "edits publish a revision");
    LootStore_VectorAppendUnique(&store, 3, "*rune*", true);
    TEST_ASSERT(LootStore_VectorRevision(&store, 3) == filter_revision, "a no-op unique append does not invalidate views");
    LootStore_VectorErase(&store, 3, "*rune*", true);
    TEST_ASSERT(LootStore_VectorRevision(&store, 3) != filter_revision &&
                    LootStore_VectorRevision(&store, 4) == highlight_revision,
        "an erase invalidates only its own vector");
    LootStore_VectorClear(&store, 4);
    TEST_ASSERT(LootStore_VectorRevision(&store, 4) != highlight_revision, "clear invalidates dependent views");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 6. Item vs source ignore + 1-based list accessors                        */
/* ======================================================================== */

static void
test_ignore_list(void)
{
    printf("TEST: item/source ignore lists\n");

    struct LootStore store;
    LootStore_Init(&store);

    TEST_ASSERT(!LootStore_IsItemIgnored(&store, "Bones"), "item not ignored");
    TEST_ASSERT(!LootStore_IsSourceIgnored(&store, "Goblin"), "source not ignored");

    LootStore_ItemIgnoreAdd(&store, "Bones");
    LootStore_ItemIgnoreAdd(&store, "Bones"); /* duplicate */
    LootStore_ItemIgnoreAdd(&store, "Coins");
    TEST_ASSERT(LootStore_IsItemIgnored(&store, "Bones"), "Bones ignored");
    TEST_ASSERT(LootStore_ItemIgnoreCount(&store) == 2, "2 item ignores");
    /* 7619/7620 are 1-based */
    TEST_ASSERT(strcmp(LootStore_ItemIgnoreName(&store, 1), "Bones") == 0, "1-based name 1");
    TEST_ASSERT(strcmp(LootStore_ItemIgnoreName(&store, 2), "Coins") == 0, "1-based name 2");
    TEST_ASSERT(strcmp(LootStore_ItemIgnoreName(&store, 0), "") == 0, "0 → empty");

    LootStore_SourceIgnoreAdd(&store, "Goblin");
    LootStore_SourceIgnoreAdd(&store, "Imp");
    TEST_ASSERT(LootStore_IsSourceIgnored(&store, "Goblin"), "Goblin source ignored");
    TEST_ASSERT(!LootStore_IsItemIgnored(&store, "Goblin"), "source ignore ≠ item");
    TEST_ASSERT(LootStore_SourceIgnoreCount(&store) == 2, "2 source ignores");
    TEST_ASSERT(strcmp(LootStore_SourceIgnoreName(&store, 1), "Goblin") == 0, "src 1-based");

    /* 1792/7200 must not circularly wipe: clearing aux leaves persistent lists */
    LootStore_VectorAppend(&store, 1, "scratch");
    LootStore_VectorClear(&store, 1);
    TEST_ASSERT(LootStore_SourceIgnoreCount(&store) == 2, "src ignore survives aux clear");
    TEST_ASSERT(LootStore_ItemIgnoreCount(&store) == 2, "item ignore survives aux clear");

    LootStore_ItemIgnoreRemove(&store, "Bones");
    TEST_ASSERT(!LootStore_IsItemIgnored(&store, "Bones"), "Bones removed");
    LootStore_SourceIgnoreRemove(&store, "Goblin");
    TEST_ASSERT(!LootStore_IsSourceIgnored(&store, "Goblin"), "Goblin removed");

    LootStore_ItemIgnoreClear(&store);
    TEST_ASSERT(LootStore_ItemIgnoreCount(&store) == 0, "item clear");
    TEST_ASSERT(LootStore_IsSourceIgnored(&store, "Imp"), "source untouched by item clear");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 7. ClearAll + ClearSourceByName + RemoveById (7613/7614/7615)            */
/* ======================================================================== */

static void
test_clear_and_remove(void)
{
    printf("TEST: ClearAll / ClearSourceByName / RemoveById\n");

    struct LootStore store;
    LootStore_Init(&store);

    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    LootStore_AddKillLoot(&store, "Imp", 200, 1, 10, 2);
    TEST_ASSERT(LootStore_SourceCount(&store) == 2, "two sources");

    /* 7614 clear-source-by-name (NOT ignore-add) */
    LootStore_ClearSourceByName(&store, "Goblin");
    TEST_ASSERT(LootStore_SourceCount(&store) == 1, "Goblin cleared");
    TEST_ASSERT(LootStore_SourceItemCount(&store, "Goblin") == 0, "Goblin gone");
    TEST_ASSERT(LootStore_SourceItemCount(&store, "Imp") == 1, "Imp remains");

    /* Script 7179 Clear-data sequence: 7614 then kind-2 query + 7615 */
    LootStore_AddKillLoot(&store, "Guard", 300, 1, 50, 3);
    LootStore_ClearSourceByName(&store, "Guard");
    int n = LootStore_BeginQuery(&store, 0, LootStore_DropLimit(&store), 2);
    for( int i = 0; i < n; i++ )
    {
        int id = LootStore_QueryId(&store, i);
        if( strcmp(LootStore_SourceName(&store, id), "Guard") == 0 )
            LootStore_RemoveById(&store, id);
    }
    TEST_ASSERT(LootStore_SourceItemCount(&store, "Guard") == 0, "clear-data sequence");

    /* RemoveById */
    LootStore_BeginQuery(&store, 0, 10, 1);
    int id0 = LootStore_QueryId(&store, 0);
    LootStore_RemoveById(&store, id0);
    TEST_ASSERT(LootStore_SourceCount(&store) == 0, "last source removed");

    LootStore_AddKillLoot(&store, "Imp", 200, 1, 10, 4);
    LootStore_RemoveById(&store, 9999);
    TEST_ASSERT(LootStore_SourceCount(&store) == 1, "remove bad id: no effect");

    LootStore_ClearAll(&store);
    TEST_ASSERT(LootStore_SourceCount(&store) == 0, "cleared all");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 8. ResetAll                                                              */
/* ======================================================================== */

static void
test_reset_all(void)
{
    printf("TEST: ResetAll\n");

    struct LootStore store;
    LootStore_Init(&store);
    uint64_t revision = LootStore_Revision(&store);
    TEST_ASSERT(revision != 0, "revision starts nonzero");

    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    TEST_ASSERT(LootStore_Revision(&store) > revision, "a drop advances revision");
    revision = LootStore_Revision(&store);
    LootStore_VectorAppend(&store, 1, "SrcName");
    LootStore_ItemIgnoreAdd(&store, "Bones");
    LootStore_SourceIgnoreAdd(&store, "Goblin");

    LootStore_ResetAll(&store);
    TEST_ASSERT(LootStore_Revision(&store) > revision, "reset cannot resurrect an old revision");
    revision = LootStore_Revision(&store);

    TEST_ASSERT(LootStore_SourceCount(&store) == 0, "sources cleared");
    TEST_ASSERT(LootStore_VectorSize(&store, 1) == 0, "string vectors cleared");
    TEST_ASSERT(!LootStore_IsItemIgnored(&store, "Bones"), "item ignore cleared");
    TEST_ASSERT(!LootStore_IsSourceIgnored(&store, "Goblin"), "source ignore cleared");

    /* Usable after reset */
    LootStore_AddKillLoot(&store, "Imp", 200, 1, 10, 1);
    TEST_ASSERT(LootStore_Revision(&store) > revision, "post-reset mutation advances revision");
    TEST_ASSERT(LootStore_SourceCount(&store) == 1, "usable after reset");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Imp") == 1, "kill after reset");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* 9. End-to-end: simulate script 7166 read loop after AddKillLoot          */
/* ======================================================================== */

static void
test_script_7166_loop(void)
{
    printf("TEST: script 7166 read loop simulation\n");

    struct LootStore store;
    LootStore_Init(&store);

    /* Goblin: two items, one kill (event 1); then second kill (event 2).
     * Guard: one kill. */
    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    LootStore_AddKillLoot(&store, "Goblin", 101, 2, 3, 1);
    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 2);
    LootStore_AddKillLoot(&store, "Guard", 300, 1, 100, 3);

    /* Script 7166 line 1: $int0 = _7601 */
    int source_count = LootStore_SourceCount(&store);
    TEST_ASSERT(source_count == 2, "two kill sources");

    /* Script 7166 line 5: $int5 = _7605(0, min($int0, 10), 1) */
    int limit = source_count < 10 ? source_count : 10;
    int n = LootStore_BeginQuery(&store, 0, limit, 1);
    TEST_ASSERT(n == 2, "query returns 2");

    /* The read loop: while ($int2 < $int5) — _7604 is kill count */
    int total_kills = 0;
    for( int i = 0; i < n; i++ )
    {
        int id = LootStore_QueryId(&store, i);            /* _7606(i) */
        TEST_ASSERT(id > 0, "valid id");

        const char* name = LootStore_SourceName(&store, id); /* _7602(id) */
        TEST_ASSERT(strlen(name) > 0, "non-empty name");

        int kills = LootStore_SourceKillCount(&store, name); /* _7604(name) */
        TEST_ASSERT(kills > 0, "positive kill count");
        total_kills += kills;
    }
    TEST_ASSERT(total_kills == 3, "Goblin x2 + Guard x1");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Goblin") == 2, "Goblin kills");
    TEST_ASSERT(LootStore_SourceKillCount(&store, "Guard") == 1, "Guard kills");

    LootStore_Free(&store);
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

int
main(void)
{
    test_init_free();
    test_add_kill_loot();
    test_row_value_is_the_unit_price();
    test_begin_query();
    test_row_access();
    test_aux_lists();
    test_ignore_list();
    test_clear_and_remove();
    test_reset_all();
    test_script_7166_loop();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
