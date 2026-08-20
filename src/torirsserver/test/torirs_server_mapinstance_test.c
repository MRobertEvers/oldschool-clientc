/* Session-local map-instance flags and integer registers. */

#include "mock230_mapinstance.h"

#include <limits.h>
#include <stdio.h>

static int g_fail;

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
            printf("  ok   %s == %lld\n", (msg), gv);                         \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,  \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    int handle;
    int recycled;

    mock230_mapinstance_reset();
    handle = mock230_mapinstance_alloc(cache_dir, 8, 8);
    CHECK_EQ(handle > 0, 1, "instance allocated");
    CHECK_EQ(mock230_mapinstance_set_owner(handle, 77), 1, "instance owner writes");
    CHECK_EQ(mock230_mapinstance_find_owner(77, 0), handle,
             "owner lookup accepts a zero flag mask");
    CHECK_EQ(mock230_mapinstance_flag_set(handle, 0x40000000, 1), 1,
             "high content-family flag writes");
    CHECK_EQ(mock230_mapinstance_find_owner(77, 0x40000000), handle,
             "owner lookup requires the requested family flag");
    CHECK_EQ(mock230_mapinstance_find_owner(77, 1), 0,
             "owner lookup rejects a missing flag");
    CHECK_EQ(mock230_mapinstance_var_get(handle, 0), 0, "register starts clear");
    CHECK_EQ(mock230_mapinstance_var_get(handle, MOCK230_MAPINSTANCE_VARS - 1), 0,
             "last register starts clear");
    CHECK_EQ(mock230_mapinstance_var_set(handle, 0, 42), 1, "first register writes");
    CHECK_EQ(mock230_mapinstance_var_set(handle, MOCK230_MAPINSTANCE_VARS - 1, INT_MIN), 1,
             "last register accepts signed values");
    CHECK_EQ(mock230_mapinstance_var_get(handle, 0), 42, "first register reads");
    CHECK_EQ(mock230_mapinstance_var_get(handle, MOCK230_MAPINSTANCE_VARS - 1), INT_MIN,
             "last register reads signed value");
    CHECK_EQ(mock230_mapinstance_var_set(handle, -1, 1), 0, "negative slot rejected");
    CHECK_EQ(mock230_mapinstance_var_set(handle, MOCK230_MAPINSTANCE_VARS, 1), 0,
             "past-end slot rejected");
    CHECK_EQ(mock230_mapinstance_var_get(handle, -1), 0, "invalid read is zero");

    CHECK_EQ(mock230_mapinstance_free(handle), 1, "instance freed");
    CHECK_EQ(mock230_mapinstance_var_get(handle, 0), 0, "dead instance reads zero");
    recycled = mock230_mapinstance_alloc(cache_dir, 8, 8);
    CHECK_EQ(recycled, handle, "allocator recycled handle");
    CHECK_EQ(mock230_mapinstance_var_get(recycled, 0), 0,
             "recycled instance erased first register");
    CHECK_EQ(mock230_mapinstance_var_get(recycled, MOCK230_MAPINSTANCE_VARS - 1), 0,
             "recycled instance erased last register");
    mock230_mapinstance_reset();

    if( g_fail )
    {
        printf("mock230_mapinstance_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("mock230_mapinstance_test: all checks passed\n");
    return 0;
}
