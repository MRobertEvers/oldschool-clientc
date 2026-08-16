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
    TEST_ASSERT(LootStore_AuxCountTotal(&store) == 0, "no aux entries");

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
    printf("TEST: aux string lists\n");

    struct LootStore store;
    LootStore_Init(&store);

    /* Script 7200: _7409(2), then _7401(2, name, 0) loop */
    LootStore_AuxClear(&store, 2);
    LootStore_AuxUpsert(&store, 2, "Bones", 0);
    LootStore_AuxUpsert(&store, 2, "Coins", 0);
    LootStore_AuxUpsert(&store, 2, "Bones", 0); /* duplicate — no-op */

    TEST_ASSERT(LootStore_AuxCount(&store, 2) == 2, "2 entries in kind 2");
    TEST_ASSERT(LootStore_AuxCountTotal(&store) == 2, "2 total");

    /* op 7408 — lookup */
    TEST_ASSERT(LootStore_AuxLookup(&store, 2, "Bones", 0, 0) == 1, "Bones found");
    TEST_ASSERT(LootStore_AuxLookup(&store, 2, "Missing", 0, 0) == 0, "Missing not found");

    /* op 7406 — get by index */
    const char* s = LootStore_AuxGet(&store, 2, 0);
    TEST_ASSERT(strlen(s) > 0, "index 0 non-empty");

    /* Remove */
    LootStore_AuxRemove(&store, 2, "Bones", 0);
    TEST_ASSERT(LootStore_AuxCount(&store, 2) == 1, "1 entry after remove");
    TEST_ASSERT(LootStore_AuxLookup(&store, 2, "Bones", 0, 0) == 0, "Bones gone");

    /* Clear */
    LootStore_AuxClear(&store, 2);
    TEST_ASSERT(LootStore_AuxCount(&store, 2) == 0, "cleared");

    /* Ground Items highlight/filter use aux kinds 3/4 */
    LootStore_AuxUpsert(&store, 3, "Filter*", 0);
    LootStore_AuxUpsert(&store, 4, "Highlight*", 0);
    TEST_ASSERT(LootStore_AuxCount(&store, 3) == 1, "kind 3");
    TEST_ASSERT(LootStore_AuxCount(&store, 4) == 1, "kind 4");
    TEST_ASSERT(LootStore_AuxCountTotal(&store) == 2, "kinds 3+4 total");

    /* out of range kind */
    TEST_ASSERT(LootStore_AuxCount(&store, -1) == 0, "negative kind");
    TEST_ASSERT(LootStore_AuxCount(&store, 99) == 0, "oversize kind");

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
    LootStore_AuxUpsert(&store, 1, "scratch", 0);
    LootStore_AuxClear(&store, 1);
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
    int n = LootStore_BeginQuery(&store, 0, LootStore_AuxCountTotal(&store) + 10, 2);
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

    LootStore_AddKillLoot(&store, "Goblin", 100, 1, 5, 1);
    LootStore_AuxUpsert(&store, 1, "SrcName", 0);
    LootStore_ItemIgnoreAdd(&store, "Bones");
    LootStore_SourceIgnoreAdd(&store, "Goblin");

    LootStore_ResetAll(&store);

    TEST_ASSERT(LootStore_SourceCount(&store) == 0, "sources cleared");
    TEST_ASSERT(LootStore_AuxCountTotal(&store) == 0, "aux cleared");
    TEST_ASSERT(!LootStore_IsItemIgnored(&store, "Bones"), "item ignore cleared");
    TEST_ASSERT(!LootStore_IsSourceIgnored(&store, "Goblin"), "source ignore cleared");

    /* Usable after reset */
    LootStore_AddKillLoot(&store, "Imp", 200, 1, 10, 1);
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
