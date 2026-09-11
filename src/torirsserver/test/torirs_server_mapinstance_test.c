/* Session-local map-instance flags and integer registers. */

#include "torirs_server_mapinstance.h"

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

    ToriRSServer_MapInstanceReset();
    handle = ToriRSServer_MapInstanceAlloc(cache_dir, 8, 8);
    CHECK_EQ(handle > 0, 1, "instance allocated");
    CHECK_EQ(ToriRSServer_MapInstanceSetOwner(handle, 77), 1, "instance owner writes");
    CHECK_EQ(ToriRSServer_MapInstanceFindOwner(77, 0), handle,
             "owner lookup accepts a zero flag mask");
    CHECK_EQ(ToriRSServer_MapInstanceFlagSet(handle, 0x40000000, 1), 1,
             "high content-family flag writes");
    CHECK_EQ(ToriRSServer_MapInstanceFindOwner(77, 0x40000000), handle,
             "owner lookup requires the requested family flag");
    CHECK_EQ(ToriRSServer_MapInstanceFindOwner(77, 1), 0,
             "owner lookup rejects a missing flag");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, 0), 0, "register starts clear");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, TORIRSSERVER_MAPINSTANCE_VARS - 1), 0,
             "last register starts clear");
    CHECK_EQ(ToriRSServer_MapInstanceVarSet(handle, 0, 42), 1, "first register writes");
    CHECK_EQ(ToriRSServer_MapInstanceVarSet(handle, TORIRSSERVER_MAPINSTANCE_VARS - 1, INT_MIN), 1,
             "last register accepts signed values");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, 0), 42, "first register reads");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, TORIRSSERVER_MAPINSTANCE_VARS - 1), INT_MIN,
             "last register reads signed value");
    CHECK_EQ(ToriRSServer_MapInstanceVarSet(handle, -1, 1), 0, "negative slot rejected");
    CHECK_EQ(ToriRSServer_MapInstanceVarSet(handle, TORIRSSERVER_MAPINSTANCE_VARS, 1), 0,
             "past-end slot rejected");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, -1), 0, "invalid read is zero");

    /* The linger clock. Default on, per-group emptiness, armed only by real
     * occupancy — the invariants world_mapinstance_linger leans on. */
    CHECK_EQ(ToriRSServer_MapInstanceLinger(handle), TORIRSSERVER_MAPINSTANCE_LINGER_DEFAULT,
             "linger defaults on");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 0), 0,
             "never-entered instance never expires");
    CHECK_EQ(ToriRSServer_MapInstanceSetLinger(handle, 2), 1, "linger writes");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 1), 0, "occupied tick arms");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 0), 0, "first empty tick holds");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 1), 0, "reoccupation resets the clock");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 0), 0, "empty tick 1 of 2 holds");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 0), 1, "empty tick 2 of 2 expires");
    CHECK_EQ(ToriRSServer_MapInstanceSetLinger(handle, 0), 1, "linger opt-out writes");
    CHECK_EQ(ToriRSServer_MapInstanceLingerTick(handle, 0), 0,
             "opted-out instance never expires");
    CHECK_EQ(ToriRSServer_MapInstanceSetLingerGroup(handle, 9), 1, "linger group writes");
    CHECK_EQ(ToriRSServer_MapInstanceLingerGroup(handle), 9, "linger group reads");

    CHECK_EQ(ToriRSServer_MapInstanceFree(handle), 1, "instance freed");
    CHECK_EQ(ToriRSServer_MapInstanceLinger(handle), 0, "dead handle lingers zero");
    CHECK_EQ(ToriRSServer_MapInstanceLingerGroup(handle), 0, "dead handle has no group");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(handle, 0), 0, "dead instance reads zero");
    recycled = ToriRSServer_MapInstanceAlloc(cache_dir, 8, 8);
    CHECK_EQ(recycled, handle, "allocator recycled handle");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(recycled, 0), 0,
             "recycled instance erased first register");
    CHECK_EQ(ToriRSServer_MapInstanceVarGet(recycled, TORIRSSERVER_MAPINSTANCE_VARS - 1), 0,
             "recycled instance erased last register");
    CHECK_EQ(ToriRSServer_MapInstanceLinger(recycled), TORIRSSERVER_MAPINSTANCE_LINGER_DEFAULT,
             "recycled instance re-arms the default linger");
    CHECK_EQ(ToriRSServer_MapInstanceLingerGroup(recycled), 0,
             "recycled instance left the old tenant's group");
    ToriRSServer_MapInstanceReset();

    if( g_fail )
    {
        printf("ToriRSServer_MapInstanceTest: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("ToriRSServer_MapInstanceTest: all checks passed\n");
    return 0;
}
