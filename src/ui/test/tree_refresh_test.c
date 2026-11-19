/*
 * The one-shot tree refresh: a raise is taken exactly once, two raises are one
 * refresh, and looking does not take it.
 *
 * A request taken twice reflows the tree on a frame nothing changed; one never
 * taken leaves the tree laid out for the state before the async work landed.
 *
 *   make -C src test-tree-refresh
 */
#include "ui/tree_refresh.h"

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

int
main(void)
{
    struct UITreeRefresh refresh;

    memset(&refresh, 0, sizeof(refresh));

    printf("TEST: a zeroed request is not pending and takes nothing\n");
    CHECK(!UITreeRefresh_Pending(&refresh), "not pending");
    CHECK(!UITreeRefresh_Take(&refresh), "nothing to take");

    printf("TEST: a raise is pending until taken, and taken once\n");
    UITreeRefresh_Raise(&refresh);
    CHECK(UITreeRefresh_Pending(&refresh), "pending after raise");
    CHECK(UITreeRefresh_Pending(&refresh), "looking does not take it");
    CHECK(UITreeRefresh_Take(&refresh), "taken");
    CHECK(!UITreeRefresh_Pending(&refresh), "cleared by the take");
    CHECK(!UITreeRefresh_Take(&refresh), "not taken twice");

    printf("TEST: two raises before a take are one refresh\n");
    UITreeRefresh_Raise(&refresh);
    UITreeRefresh_Raise(&refresh);
    CHECK(UITreeRefresh_Take(&refresh), "taken");
    CHECK(!UITreeRefresh_Take(&refresh), "only once");

    if( g_failures )
    {
        printf("tree_refresh_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("tree_refresh_test: all passed\n");
    return 0;
}
