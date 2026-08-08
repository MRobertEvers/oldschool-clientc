#include "varp_manager.h"

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
    int varp_id)
{
    struct ChangeLog* log = userdata;
    if( log->count < CHANGE_LOG_MAX )
        log->ids[log->count++] = varp_id;
}

static void
test_init_free(void)
{
    printf("TEST: init/free\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    for( int i = 0; i < VARP_MANAGER_READBIT_MAX; i++ )
        TEST_ASSERT(mgr.readbit[i] == ((1 << i) - 1), "readbit mask");

    TEST_ASSERT(mgr.varp_count == 0, "varp_count zero");
    TEST_ASSERT(mgr.varbit_count == 0, "varbit_count zero");
    TEST_ASSERT(mgr.var == NULL && mgr.var_serv == NULL, "arrays null");

    VarPManager_Free(&mgr);
    VarPManager_Free(&mgr); /* second free on emptied mgr is safe */
}

static void
test_set_varp_types_defaults(void)
{
    printf("TEST: set varp types / defaults\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType types[3] = {
        { .clientcode = 0 },
        { .clientcode = 1 },
        { .clientcode = 3 },
    };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 3), "SetVarpTypes");

    TEST_ASSERT(mgr.varp_count == 3, "varp_count");
    for( int i = 0; i < 3; i++ )
        TEST_ASSERT(VarPManager_GetVarp(&mgr, i) == 0, "default varp zero");

    TEST_ASSERT(VarPManager_GetVarp(&mgr, -1) == 0, "oob negative");
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 99) == 0, "oob high");

    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 0) == 0, "clientcode 0");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 1) == 1, "clientcode 1");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 2) == 3, "clientcode 2");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 99) == 0, "clientcode oob");

    VarPManager_Free(&mgr);
}

static void
test_get_varbit(void)
{
    printf("TEST: set varbit types / get varbit\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType varp_types[1] = { { .clientcode = 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, varp_types, 1), "SetVarpTypes");

    /* bits [0, 3) of basevar 0 */
    struct VarBitType vb = { .basevar = 0, .startbit = 0, .endbit = 3 };
    TEST_ASSERT(VarPManager_SetVarbitTypes(&mgr, &vb, 1), "SetVarbitTypes");

    mgr.var[0] = 0x5; /* 0b101 */
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 5, "get varbit 5");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, -1) == 0, "varbit oob neg");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 9) == 0, "varbit oob high");

    VarPManager_Free(&mgr);
}

static void
test_apply_small_large(void)
{
    printf("TEST: apply small/large + callback\n");

    struct VarPManager mgr;
    struct ChangeLog log;
    VarPManager_Init(&mgr);
    change_log_reset(&log);
    VarPManager_SetChangeCallback(&mgr, on_change, &log);

    struct VarPType types[2] = { { 0 }, { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 2), "SetVarpTypes");

    VarPManager_ApplySmall(&mgr, 0, 42);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 0) == 42, "apply small var");
    TEST_ASSERT(mgr.var_serv[0] == 42, "apply small var_serv");
    TEST_ASSERT(log.count == 1 && log.ids[0] == 0, "callback on small");

    change_log_reset(&log);
    VarPManager_ApplySmall(&mgr, 0, 42);
    TEST_ASSERT(log.count == 0, "no callback on same small");

    change_log_reset(&log);
    VarPManager_ApplyLarge(&mgr, 1, 0x12345678);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 1) == 0x12345678, "apply large var");
    TEST_ASSERT(mgr.var_serv[1] == 0x12345678, "apply large var_serv");
    TEST_ASSERT(log.count == 1 && log.ids[0] == 1, "callback on large");

    VarPManager_Free(&mgr);
}

static void
test_optimistic_and_sync(void)
{
    printf("TEST: optimistic + sync\n");

    struct VarPManager mgr;
    struct ChangeLog log;
    VarPManager_Init(&mgr);
    change_log_reset(&log);
    VarPManager_SetChangeCallback(&mgr, on_change, &log);

    struct VarPType types[1] = { { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 1), "SetVarpTypes");

    VarPManager_ApplyLarge(&mgr, 0, 10);
    change_log_reset(&log);

    VarPManager_SetVarpOptimistic(&mgr, 0, 99);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 0) == 99, "optimistic var");
    TEST_ASSERT(mgr.var_serv[0] == 10, "var_serv unchanged");
    TEST_ASSERT(log.count == 1 && log.ids[0] == 0, "optimistic callback");

    change_log_reset(&log);
    VarPManager_ApplySync(&mgr);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 0) == 10, "sync restored");
    TEST_ASSERT(mgr.var_serv[0] == 10, "var_serv still 10");
    TEST_ASSERT(log.count == 1 && log.ids[0] == 0, "sync callback");

    change_log_reset(&log);
    VarPManager_ApplySync(&mgr);
    TEST_ASSERT(log.count == 0, "sync noop no callback");

    VarPManager_Free(&mgr);
}

static void
test_set_varbit_optimistic(void)
{
    printf("TEST: set varbit optimistic\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType types[1] = { { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 1), "SetVarpTypes");

    /* Two ranges in same basevar: bits [0,3) and [4,6) */
    struct VarBitType vbs[2] = {
        { .basevar = 0, .startbit = 0, .endbit = 3 },
        { .basevar = 0, .startbit = 4, .endbit = 6 },
    };
    TEST_ASSERT(VarPManager_SetVarbitTypes(&mgr, vbs, 2), "SetVarbitTypes");

    /* Seed: low=5 (0b101), high=2 (0b10) at bits 4-5 → value = 5 | (2<<4) = 0x25 */
    mgr.var[0] = 0x25;
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 5, "low bits before");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 1) == 2, "high bits before");

    VarPManager_SetVarbitOptimistic(&mgr, 0, 3); /* write 0b011 into low */
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 3, "low bits after");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 1) == 2, "high bits preserved");
    TEST_ASSERT(mgr.var_serv[0] == 0, "var_serv not updated by optimistic");

    VarPManager_Free(&mgr);
}

static void
test_load_varp_dat(void)
{
    printf("TEST: load varp.dat\n");

    /* g2 count=2
     * entry0: opcode 0 (empty)
     * entry1: opcode 5, g2 clientcode=3, opcode 0
     */
    const uint8_t dat[] = {
        0x00, 0x02,       /* count = 2 */
        0x00,             /* entry 0 terminator */
        0x05, 0x00, 0x03, /* opcode 5, clientcode 3 */
        0x00,             /* entry 1 terminator */
    };

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    TEST_ASSERT(VarPManager_LoadVarpDat(&mgr, dat, sizeof(dat)), "LoadVarpDat");
    TEST_ASSERT(mgr.varp_count == 2, "varp count");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 0) == 0, "entry0 clientcode");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 1) == 3, "entry1 clientcode");
    TEST_ASSERT(
        VarPManager_GetVarp(&mgr, 0) == 0 && VarPManager_GetVarp(&mgr, 1) == 0, "defaults zero");

    VarPManager_Free(&mgr);
}

static void
test_load_varbit_dat(void)
{
    printf("TEST: load varbit.dat\n");

    /* Need a varp first to seed values */
    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType types[1] = { { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 1), "SetVarpTypes");

    /* g2 count=1
     * opcode 1: basevar=0, startbit=1, endbit=4, opcode 0
     */
    const uint8_t dat[] = {
        0x00, 0x01,       /* count = 1 */
        0x01, 0x00, 0x00, /* opcode 1, basevar 0 */
        0x01, 0x04,       /* startbit 1, endbit 4 */
        0x00,             /* terminator */
    };

    TEST_ASSERT(VarPManager_LoadVarbitDat(&mgr, dat, sizeof(dat)), "LoadVarbitDat");
    TEST_ASSERT(mgr.varbit_count == 1, "varbit count");
    TEST_ASSERT(mgr.varbit_types[0].basevar == 0, "basevar");
    TEST_ASSERT(mgr.varbit_types[0].startbit == 1, "startbit");
    TEST_ASSERT(mgr.varbit_types[0].endbit == 4, "endbit");

    /*
     * `endbit` is inclusive, so this varbit is bits 1..4 — four bits wide.
     *
     * The value matters. This used to read `var[0] = 0x0E` (0b01110) and expect 7,
     * which passes whether the width is taken as 3 bits or 4 because bit 4 is zero
     * either way — so it could not tell the two apart, and the off-by-one it was
     * meant to catch went unnoticed. 0x1E (0b11110) sets bit 4, so the two
     * readings now disagree: 4 bits gives 15, 3 bits gives 7.
     */
    mgr.var[0] = 0x1E;
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 15, "four-bit varbit reads all four bits");

    mgr.var[0] = 0x0E;
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 7, "and the high bit clear reads 7");

    /* A write must come back as the same value, or reader and writer disagree. */
    VarPManager_SetVarbitOptimistic(&mgr, 0, 11);
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 11, "set then get round-trips");

    VarPManager_Free(&mgr);
}

/*
 * A single-bit varbit: startbit == endbit. The commonest shape in a real cache —
 * varbits 0..4 of the rev-230 cache are five consecutive one-bit flags in varp 318 —
 * and the case a width of `endbit - startbit` rejects outright, masking to zero and
 * reading 0 for every possible varp value.
 */
static void
test_single_bit_varbit(void)
{
    printf("TEST: single-bit varbit\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType varps[1] = { { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, varps, 1), "SetVarpTypes");

    /* Three flags packed into bits 0, 1 and 2 of varp 0. */
    struct VarBitType bits[3] = {
        { .basevar = 0, .startbit = 0, .endbit = 0 },
        { .basevar = 0, .startbit = 1, .endbit = 1 },
        { .basevar = 0, .startbit = 2, .endbit = 2 },
    };
    TEST_ASSERT(VarPManager_SetVarbitTypes(&mgr, bits, 3), "SetVarbitTypes");

    mgr.var[0] = 0b101;
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 1, "bit 0 set");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 1) == 0, "bit 1 clear");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 2) == 1, "bit 2 set");

    /* Writing one flag must not disturb its neighbours. */
    VarPManager_SetVarbitOptimistic(&mgr, 1, 1);
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 1, "bit 0 untouched");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 1) == 1, "bit 1 now set");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 2) == 1, "bit 2 untouched");

    VarPManager_SetVarbitOptimistic(&mgr, 2, 0);
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 2) == 0, "bit 2 cleared");
    TEST_ASSERT(VarPManager_GetVarbit(&mgr, 0) == 1, "bit 0 still set");

    VarPManager_Free(&mgr);
}

static void
test_resolve_transform(void)
{
    printf("TEST: resolve transform\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    struct VarPType types[2] = { { 0 }, { 0 } };
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 2), "SetVarpTypes");

    struct VarBitType vb = { .basevar = 0, .startbit = 0, .endbit = 4 };
    TEST_ASSERT(VarPManager_SetVarbitTypes(&mgr, &vb, 1), "SetVarbitTypes");

    /* transforms: [100, 200, 300] — last is fallback */
    const int transforms[] = { 100, 200, 300 };

    TEST_ASSERT(VarPManager_ResolveTransform(&mgr, NULL, 0, -1, -1) == -1, "empty transforms");

    /* Prefer varbit: index 1 → 200 */
    mgr.var[0] = 1;
    TEST_ASSERT(VarPManager_ResolveTransform(&mgr, transforms, 3, 0, 1) == 200, "prefer varbit");

    /* Varbit disabled (-1), use varp id 1 with value 0 → 100 */
    mgr.var[1] = 0;
    TEST_ASSERT(VarPManager_ResolveTransform(&mgr, transforms, 3, -1, 1) == 100, "varp fallback");

    /* Index past transform_count-1 → last entry */
    mgr.var[0] = 99;
    TEST_ASSERT(
        VarPManager_ResolveTransform(&mgr, transforms, 3, 0, -1) == 300, "oob index uses last");

    /* transforms[index] == -1 → last entry */
    const int with_hide[] = { -1, 200, 300 };
    mgr.var[0] = 0;
    TEST_ASSERT(
        VarPManager_ResolveTransform(&mgr, with_hide, 3, 0, -1) == 300, "hide entry uses last");

    VarPManager_Free(&mgr);
}

/*
 * Untyped mode — the dat1 boot, where varbit.dat loads but no varp type table ever
 * does. The first server VARP grows the var arrays on demand, which moves
 * `varp_count` well past zero while `varp_types` stays NULL. Every accessor bounded
 * by `varp_count` must survive that; GetClientcode did not, and segfaulted on the
 * first VARP packet of a rev-254 login.
 */
static void
test_untyped_mode_accessors(void)
{
    printf("TEST: untyped mode accessors\n");

    struct VarPManager mgr;
    VarPManager_Init(&mgr);

    TEST_ASSERT(mgr.varp_types == NULL, "no type table");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 18) == 0, "clientcode before any growth");

    VarPManager_ApplySmall(&mgr, 18, 7);
    TEST_ASSERT(mgr.varp_types == NULL, "still no type table");
    TEST_ASSERT(mgr.varp_count > 18, "var arrays grew past the id");
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 18) == 7, "value landed");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 18) == 0, "clientcode inside grown capacity");

    /* The whole grown range, not just the id that caused the growth. */
    int nonzero = 0;
    for( int i = 0; i < mgr.varp_count; i++ )
        nonzero += VarPManager_GetClientcode(&mgr, i) != 0;
    TEST_ASSERT(nonzero == 0, "clientcode across the whole capacity");

    VarPManager_ApplyLarge(&mgr, 300, 1234);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 300) == 1234, "second growth value");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 300) == 0, "clientcode after second growth");

    VarPManager_ApplySync(&mgr);
    VarPManager_ResetAll(&mgr);
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 300) == 0, "reset clears");

    VarPManager_Free(&mgr);
}

/*
 * A varp ABOVE the loaded type table.
 *
 * The reference cannot reach this: its varp array is sized from the cache
 * varplayer table. This tree can, because content allocates its own varps past
 * the cache's highest id (mock230.h MOCK230_VARP_SERVER_HEADROOM) — so once the
 * dat2 varplayer loader installs a real table, "id beyond the table" stops
 * being hypothetical. Dropping those writes is silent and total: the value
 * never lands, no hook fires, and nothing reports it.
 *
 * The two halves are separate claims. The VALUE must land (the arrays grow),
 * and the CLIENTCODE must read 0 without walking off the type table.
 */
static void
test_varp_above_the_type_table(void)
{
    printf("TEST: a varp above the type table still stores\n");

    struct VarPManager mgr;
    struct VarPType types[3] = {
        { .clientcode = 0 }, { .clientcode = 18 }, { .clientcode = 22 }
    };

    VarPManager_Init(&mgr);
    TEST_ASSERT(VarPManager_SetVarpTypes(&mgr, types, 3), "SetVarpTypes");
    TEST_ASSERT(mgr.varp_type_count == 3, "type table is 3 long");

    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 1) == 18, "in-table clientcode");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 2) == 22, "in-table clientcode");

    VarPManager_ApplySmall(&mgr, 900, 5);
    TEST_ASSERT(mgr.varp_count > 900, "the var arrays grew past the type table");
    TEST_ASSERT(mgr.varp_type_count == 3, "growth does not extend the type table");
    TEST_ASSERT(VarPManager_GetVarp(&mgr, 900) == 5, "the value landed");
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 900) == 0, "and drives no client behaviour");

    /* The in-table codes survive the growth — the realloc copies, not resets. */
    TEST_ASSERT(VarPManager_GetClientcode(&mgr, 1) == 18, "in-table clientcode after growth");

    VarPManager_Free(&mgr);
}

int
main(void)
{
    test_init_free();
    test_set_varp_types_defaults();
    test_get_varbit();
    test_apply_small_large();
    test_optimistic_and_sync();
    test_set_varbit_optimistic();
    test_load_varp_dat();
    test_load_varbit_dat();
    test_single_bit_varbit();
    test_resolve_transform();
    test_untyped_mode_accessors();
    test_varp_above_the_type_table();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All tests passed.\n");
    return 0;
}
