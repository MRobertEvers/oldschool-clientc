#include "hmap.c" /* or #include "hmap.h" and link separately */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -----------------------------
 * User-defined entry structure
 * ----------------------------- */

typedef struct Entry
{
    int key;
    int value;
} Entry;

/* Key hash: just hash the int */
static uint64_t
int_hash(const void* key, size_t key_size, void* arg)
{
    (void)key_size;
    (void)arg;
    uint64_t x = *(const int*)key;
    /* simple integer hash, ok for testing */
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    if( x == 0 )
        x = 1;
    return x;
}

/* Key equality */
static int
int_eq(const void* a, const void* b, size_t key_size, void* arg)
{
    (void)key_size;
    (void)arg;
    return *(const int*)a == *(const int*)b;
}

/* -----------------------------
 * Tests
 * ----------------------------- */

static void
test_basic_insert_find(void)
{
    printf("TEST: basic insert/find\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf,
            sizeof(buf),
            sizeof(Entry),
            sizeof(int), /* key_size   */
            32,          /* capacity   */
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Insert values */
    for( int i = 0; i < 20; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e != NULL);
        e->value = i * 10;
    }

    /* Look them up */
    for( int i = 0; i < 20; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_FIND);
        assert(e != NULL);
        assert(e->value == i * 10);
    }

    printf("  OK\n");
}

static void
test_update_existing(void)
{
    printf("TEST: update existing entry\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(&m, buf, sizeof(buf), sizeof(Entry), sizeof(int), 16, int_hash, int_eq, NULL) ==
        HMAP_OK);

    int key = 5;

    Entry* e = hmap_search(&m, &key, HMAP_INSERT);
    assert(e);
    e->value = 123;

    /* ENTER on existing should return same entry */
    Entry* e2 = hmap_search(&m, &key, HMAP_INSERT);
    assert(e2 == e);

    /* FIND returns it too */
    Entry* e3 = hmap_search(&m, &key, HMAP_FIND);
    assert(e3 == e);
    assert(e3->value == 123);

    /* Change value */
    e3->value = 999;

    /* Check after updating */
    Entry* e4 = hmap_search(&m, &key, HMAP_FIND);
    assert(e4->value == 999);

    printf("  OK\n");
}

static void
test_remove(void)
{
    printf("TEST: remove\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(&m, buf, sizeof(buf), sizeof(Entry), sizeof(int), 16, int_hash, int_eq, NULL) ==
        HMAP_OK);

    int k = 10;

    /* Insert */
    Entry* e = hmap_search(&m, &k, HMAP_INSERT);
    assert(e);
    e->value = 555;

    /* Remove */
    Entry* e2 = hmap_search(&m, &k, HMAP_REMOVE);
    assert(e2 != NULL);
    assert(e2->key == 10);
    assert(e2->value == 555);

    /* Find must return NULL */
    assert(hmap_search(&m, &k, HMAP_FIND) == NULL);

    printf("  OK\n");
}

static void
test_tombstone_reuse(void)
{
    printf("TEST: tombstone reuse\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(&m, buf, sizeof(buf), sizeof(Entry), sizeof(int), 8, int_hash, int_eq, NULL) ==
        HMAP_OK);

    /* Fill some entries that will collide */
    for( int i = 0; i < 5; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i;
    }

    /* Remove some of them → tombstones */
    int k = 2;
    assert(hmap_search(&m, &k, HMAP_REMOVE) != NULL);

    /* Insert new key while tombstone exists */
    int newk = 1002;
    Entry* e = hmap_search(&m, &newk, HMAP_INSERT);
    assert(e != NULL);
    e->value = 777;

    /* Should find it */
    Entry* e2 = hmap_search(&m, &newk, HMAP_FIND);
    assert(e2 != NULL && e2->value == 777);

    printf("  OK\n");
}

static void
test_full_table(void)
{
    printf("TEST: full table handling\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf,
            sizeof(buf),
            sizeof(Entry),
            sizeof(int),
            4, /* small capacity */
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Fill all 4 slots */
    for( int i = 0; i < 4; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i * 2;
    }

    /* Now enter must fail (no tombstones) */
    int k = 999;
    Entry* e = hmap_search(&m, &k, HMAP_INSERT);
    assert(e == NULL);

    printf("  OK\n");
}

static void
test_iteration(void)
{
    printf("TEST: iteration\n");

    uint8_t buf[4096];
    struct HMap m;

    assert(
        hmap_init(&m, buf, sizeof(buf), sizeof(Entry), sizeof(int), 32, int_hash, int_eq, NULL) ==
        HMAP_OK);

    /* Insert a bunch */
    for( int i = 0; i < 10; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i * 3;
    }

    /* Iterate */
    struct HMapIter* it = hmap_iter_new(&m);
    int seen = 0;

    Entry* e = NULL;
    while( (e = hmap_iter_next(it)) )
    {
        assert(e->value == e->key * 3);
        seen++;
    }

    assert(seen == 10);

    hmap_iter_free(it);

    printf("  OK\n");
}

static void
test_resize_grow(void)
{
    printf("TEST: resize grow\n");

    uint8_t buf_small[2048];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf_small,
            sizeof(buf_small),
            sizeof(Entry),
            sizeof(int),
            16,
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Insert some entries */
    for( int i = 0; i < 12; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i + 100;
    }

    /* Resize to a larger buffer */
    uint8_t buf_big[65536];
    void* oldbuf = NULL;

    assert(
        hmap_resize(
            &m,
            buf_big,
            sizeof(buf_big),
            128, /* new capacity */
            &oldbuf) == HMAP_OK);

    /* Old buffer returned properly */
    assert(oldbuf == buf_small);

    /* Ensure all entries survived */
    for( int i = 0; i < 12; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_FIND);
        assert(e);
        assert(e->value == i + 100);
    }

    /* Insert some new entries in the resized table */
    for( int i = 12; i < 40; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i + 200;
    }

    /* Check new entries */
    for( int i = 12; i < 40; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_FIND);
        assert(e);
        assert(e->value == i + 200);
    }

    printf("  OK\n");
}

static void
test_resize_shrink(void)
{
    printf("TEST: resize shrink\n");

    uint8_t buf_big[65536];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf_big,
            sizeof(buf_big),
            sizeof(Entry),
            sizeof(int),
            128,
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Insert fewer than target capacity so shrink is possible */
    for( int i = 0; i < 20; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i * 3;
    }

    /* Now shrink to small map (capacity 32) */
    uint8_t buf_small[4096];
    void* oldbuf = NULL;

    assert(hmap_resize(&m, buf_small, sizeof(buf_small), 32, &oldbuf) == HMAP_OK);

    /* Old buffer returned */
    assert(oldbuf == buf_big);

    /* All entries must still exist */
    for( int i = 0; i < 20; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_FIND);
        assert(e);
        assert(e->value == i * 3);
    }

    printf("  OK\n");
}

static void
test_resize_preserves_iteration(void)
{
    printf("TEST: resize preserves iteration\n");

    uint8_t buf_small[4096];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf_small,
            sizeof(buf_small),
            sizeof(Entry),
            sizeof(int),
            16,
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Insert entries */
    for( int i = 0; i < 10; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i * 10;
    }

    /* Resize up */
    uint8_t buf_big[16384];
    void* oldbuf = NULL;
    assert(hmap_resize(&m, buf_big, sizeof(buf_big), 64, &oldbuf) == HMAP_OK);

    /* Iterate and verify contents */
    struct HMapIter* it = hmap_iter_new(&m);
    int seen = 0;

    Entry* e = NULL;
    while( (e = hmap_iter_next(it)) )
    {
        assert(e->value == e->key * 10);
        seen++;
    }

    assert(seen == 10);

    hmap_iter_free(it);

    printf("  OK\n");
}

static void
test_resize_tombstone_cleanup(void)
{
    printf("TEST: resize removes tombstones\n");

    uint8_t buf_small[4096];
    struct HMap m;

    assert(
        hmap_init(
            &m,
            buf_small,
            sizeof(buf_small),
            sizeof(Entry),
            sizeof(int),
            16,
            int_hash,
            int_eq,
            NULL) == HMAP_OK);

    /* Insert a few entries */
    for( int i = 0; i < 8; i++ )
    {
        Entry* e = hmap_search(&m, &i, HMAP_INSERT);
        assert(e);
        e->value = i + 50;
    }

    /* Delete a couple — creates tombstones */
    assert(hmap_search(&m, &(int){ 2 }, HMAP_REMOVE) != NULL);
    assert(hmap_search(&m, &(int){ 5 }, HMAP_REMOVE) != NULL);

    /* Resize should eliminate tombstones */
    uint8_t buf_big[8192];
    void* oldbuf = NULL;
    assert(hmap_resize(&m, buf_big, sizeof(buf_big), 32, &oldbuf) == HMAP_OK);

    /* All remaining entries must remain correct */
    for( int i = 0; i < 8; i++ )
    {
        if( i == 2 || i == 5 )
            continue;
        Entry* e = hmap_search(&m, &i, HMAP_FIND);
        assert(e);
        assert(e->value == i + 50);
    }

    /* Tombstoned keys must NOT exist */
    assert(hmap_search(&m, &(int){ 2 }, HMAP_FIND) == NULL);
    assert(hmap_search(&m, &(int){ 5 }, HMAP_FIND) == NULL);

    printf("  OK\n");
}

// compile with:
// clang -std=c11 -Wall -Wextra -o test_hmap hmap_test.c
int
main(void)
{
    test_basic_insert_find();
    test_update_existing();
    test_remove();
    test_tombstone_reuse();
    test_full_table();
    test_iteration();

    test_resize_grow();
    test_resize_shrink();
    test_resize_preserves_iteration();
    test_resize_tombstone_cleanup();

    printf("\nAll tests passed.\n");
    return 0;
}
