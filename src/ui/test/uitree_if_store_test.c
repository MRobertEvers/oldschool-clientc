/*
 * The persistent IF_SET* stores.
 *
 * The server sends IF_SETTEXT, IF_SETHIDE and IF_SETCOLOUR for components of
 * an interface that may not be mounted yet -- a quest panel's journal and bonus
 * texts arrive before the panel does. So the value is remembered, applied when
 * there is a node, and re-applied whenever the tree's generation moves,
 * because a mount or a bake replaces the nodes underneath.
 *
 * What is asserted:
 *
 *   - the store is KEYED, not appended. A component's value is a state, and
 *     last write wins. Appending would re-apply an old text after a new one on
 *     the next generation bump -- a quest panel reverting one line to what it
 *     said an hour ago, on a mount, with nothing in between to blame.
 *   - a cleared text is stored as the empty string, not forgotten. Forgetting
 *     it lets the cache's authored text reappear at the next generation bump,
 *     which reads as the server never having cleared it.
 *   - the text store OWNS its strings: re-setting one frees the old, and the
 *     caller's buffer can go away afterwards.
 *   - growth past the initial capacity keeps every entry.
 *   - the int store's value is stored verbatim, including 0, so "hidden =
 *     false" is a real answer rather than an absent one.
 *
 * Build and run:
 *   make -C src test-uitree-if-store
 */

#include "ui/uitree_if_store.h"

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

static void
test_int_store_is_keyed(void)
{
    struct UIIfIntStore store;

    memset(&store, 0, sizeof(store));

    CHECK(UIIfIntStore_Get(&store, 10, -1) == -1, "an empty store answered for a component");

    UIIfIntStore_Set(&store, 10, 0x336699);
    CHECK(store.count == 1, "count is %d after one set", store.count);
    CHECK(UIIfIntStore_Get(&store, 10, -1) == 0x336699, "the value was not stored");

    /* Last write wins, and does NOT append -- see the header. */
    UIIfIntStore_Set(&store, 10, 0xAABBCC);
    CHECK(store.count == 1, "a second set for one component grew the store to %d", store.count);
    CHECK(UIIfIntStore_Get(&store, 10, -1) == 0xAABBCC, "the second set did not win");

    /* 0 is a real value. "hidden = false" must be an answer, not an absence,
     * or a component the server explicitly un-hid falls back to whatever the
     * cache authored. */
    UIIfIntStore_Set(&store, 11, 0);
    CHECK(UIIfIntStore_Get(&store, 11, 1) == 0, "a stored 0 read as absent");
    CHECK(UIIfIntStore_Get(&store, 12, 1) == 1, "an absent component did not fall back");

    /* Past the initial capacity, everything survives. */
    for( int com_id = 100; com_id < 300; com_id++ )
        UIIfIntStore_Set(&store, com_id, com_id * 3);
    CHECK(store.count == 202, "count is %d after 200 more components", store.count);
    CHECK(UIIfIntStore_Get(&store, 10, -1) == 0xAABBCC, "growth lost the first entry");
    CHECK(UIIfIntStore_Get(&store, 299, -1) == 299 * 3, "growth lost the last entry");
    CHECK(UIIfIntStore_Get(&store, 200, -1) == 600, "growth lost a middle entry");

    UIIfIntStore_Free(&store);
    CHECK(store.count == 0 && store.entries == NULL, "free left the store populated");
    CHECK(store.applied_generation == 0, "free left a generation behind");
}

static void
test_text_store_owns_its_strings(void)
{
    struct UIIfTextStore store;
    char buffer[32];

    memset(&store, 0, sizeof(store));

    CHECK(UIIfTextStore_Get(&store, 10) == NULL, "an empty store answered for a component");

    /* The caller's buffer is copied, not borrowed -- the packet's payload does
     * not outlive the call that carried it. */
    strcpy(buffer, "Quest points: 12");
    UIIfTextStore_Set(&store, 10, buffer);
    memset(buffer, 'x', sizeof(buffer));
    CHECK(
        UIIfTextStore_Get(&store, 10) != NULL &&
            strcmp(UIIfTextStore_Get(&store, 10), "Quest points: 12") == 0,
        "the text was borrowed rather than copied: \"%s\"",
        UIIfTextStore_Get(&store, 10) ? UIIfTextStore_Get(&store, 10) : "(null)");

    /* Keyed, and the old string is released rather than leaked. */
    UIIfTextStore_Set(&store, 10, "Quest points: 13");
    CHECK(store.count == 1, "a second set for one component grew the store to %d", store.count);
    CHECK(strcmp(UIIfTextStore_Get(&store, 10), "Quest points: 13") == 0, "the second set lost");

    /*
     * A cleared caption is the EMPTY STRING, not an absent entry. Dropping the
     * entry would let the cache's authored text come back at the next
     * generation bump, which reads as the server never having cleared it.
     */
    UIIfTextStore_Set(&store, 10, "");
    CHECK(
        UIIfTextStore_Get(&store, 10) != NULL && UIIfTextStore_Get(&store, 10)[0] == '\0',
        "a cleared caption was forgotten instead of stored empty");
    CHECK(store.count == 1, "clearing a caption changed the count to %d", store.count);

    /* NULL means the same thing as "", for the same reason. */
    UIIfTextStore_Set(&store, 11, NULL);
    CHECK(
        UIIfTextStore_Get(&store, 11) != NULL && UIIfTextStore_Get(&store, 11)[0] == '\0',
        "NULL was not stored as the empty string");

    for( int com_id = 100; com_id < 300; com_id++ )
    {
        char label[32];

        snprintf(label, sizeof(label), "row %d", com_id);
        UIIfTextStore_Set(&store, com_id, label);
    }
    CHECK(store.count == 202, "count is %d after 200 more components", store.count);
    CHECK(strcmp(UIIfTextStore_Get(&store, 299), "row 299") == 0, "growth lost the last entry");
    CHECK(strcmp(UIIfTextStore_Get(&store, 100), "row 100") == 0, "growth lost the first of them");

    UIIfTextStore_Free(&store);
    CHECK(store.count == 0 && store.entries == NULL, "free left the store populated");
}

int
main(void)
{
    test_int_store_is_keyed();
    test_text_store_owns_its_strings();

    if( g_failures )
    {
        printf("uitree_if_store_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_if_store_test: OK\n");
    return 0;
}
