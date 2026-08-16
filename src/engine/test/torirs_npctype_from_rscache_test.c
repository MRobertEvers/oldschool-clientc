/*
 * NpcType.multiNpc (dat2 opcode 106): the decoder's `varbit_id`/`varp_index`/
 * `configs` fields must land on ToriRS_Npctype's `transform_*` fields
 * unmodified, since App_NpctypeResolveMultiId (app.c) feeds them straight to
 * VarPManager_ResolveTransform, the same function a loc's transform table
 * uses. `configs` already carries VarPManager_ResolveTransform's own -1
 * sentinel for a hidden entry, so this is a pass-through, not a translation --
 * the thing worth pinning is that it isn't dropped, which it was before this
 * struct had anywhere to put it.
 */
#include "engine/torirs_npctype_from_rscache.h"

#include <stdio.h>
#include <stdlib.h>
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

static void
test_dat2_carries_multinpc_fields(void)
{
    struct RSCache_Dat2ConfigNpc src;
    int configs[4] = {100, 101, -1, -1};
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.varbit_id = 42;
    src.varp_index = -1;
    src.configs = configs;
    src.configs_count = 4;

    npctype = ToriRS_NpctypeFromRSCacheDat2(999, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_varbit == 42, "varbit_id carried");
    TEST_ASSERT(npctype->transform_varp == -1, "varp_index carried");
    TEST_ASSERT(npctype->transform_count == 4, "configs_count carried");
    TEST_ASSERT(npctype->transforms != NULL, "transforms array allocated");
    TEST_ASSERT(
        npctype->transforms[0] == 100 && npctype->transforms[1] == 101 &&
            npctype->transforms[2] == -1 && npctype->transforms[3] == -1,
        "transforms array contents match configs verbatim");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

static void
test_dat2_no_multinpc_leaves_transform_count_zero(void)
{
    struct RSCache_Dat2ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));
    src.varbit_id = -1;
    src.varp_index = -1;

    npctype = ToriRS_NpctypeFromRSCacheDat2(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_count == 0, "no opcode 106 -> no transform table");
    TEST_ASSERT(npctype->transforms == NULL, "no opcode 106 -> no transforms array");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

static void
test_dat1_has_no_multinpc(void)
{
    struct RSCache_Dat1ConfigNpc src;
    struct ToriRS_Npctype* npctype;

    memset(&src, 0, sizeof(src));

    npctype = ToriRS_NpctypeFromRSCacheDat1(1, &src);
    TEST_ASSERT(npctype != NULL, "decode succeeds");
    TEST_ASSERT(npctype->transform_varbit == -1, "dat1 states no varbit switch");
    TEST_ASSERT(npctype->transform_varp == -1, "dat1 states no varp switch");
    TEST_ASSERT(npctype->transform_count == 0, "dat1 carries no multiNpc opcode");

    /* Not ToriRS_NpctypeFree: it pulls in the whole torirs_types.c
     * component-hook dependency chain for a struct this test built by hand
     * and with none of those fields set. Free directly. */
    free(npctype->transforms);
    free(npctype);
}

int
main(void)
{
    g_failures = 0;
    test_dat2_carries_multinpc_fields();
    test_dat2_no_multinpc_leaves_transform_count_zero();
    test_dat1_has_no_multinpc();
    return g_failures ? 1 : 0;
}
