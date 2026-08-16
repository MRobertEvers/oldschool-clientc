/*
 * The trigger lookup order: exact type, then category, then the bare `_`, and
 * nothing after that.
 *
 * This is `ScriptProvider.getByTrigger`, and it is the whole of what the engine
 * is allowed to do when a player clicks something (PORTING_GUIDE §6 phase 1
 * item 3). It is worth its own test for a reason the other script tests do not
 * cover: every way of getting it wrong still *finds a script*. Swap the first
 * two rungs and `[opheld1,_bones]` runs where `[opheld1,bones]` should have —
 * the right kind of thing happens to the right item and the wrong script did it.
 * Drop the third rung and every `_` wildcard in the tree stops firing, which
 * looks like content that was never written. Neither shows up as a crash, an
 * abort, or a wrong-looking log line.
 *
 * So the scripts here are synthetic: three bound to one trigger through three
 * different rungs, plus a fourth trigger that has only a wildcard. Each lookup
 * asserts *which* one came back, which is the only assertion that can tell the
 * orderings apart.
 *
 *   make -C src test-ss-provider
 */

#include "ss_meta.h"
#include <assert.h>
#include "ss_opcode.h"
#include "ss_trigger.h"
#include "ssvm_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/* The ids the fixture uses. Arbitrary, but `bones` and category 6 are the real
 * pair the shipped tree binds (`[opheld1,_bones]`), so a reader can map the
 * fixture onto the one case that exists in content today. */
#define FIXTURE_TYPE 526     /* "bones" */
#define FIXTURE_CATEGORY 6   /* the bones category */
#define OTHER_TYPE 1511      /* an obj in no category the fixture names */
#define OTHER_CATEGORY 59    /* misc_food */

/* ------------------------------------------------------------------ */
/* Building a container by hand                                        */
/* ------------------------------------------------------------------ */

/*
 * The stored key is the reference's i32 layout — `trigger | (kind << 8) |
 * (subject << 10)` — and not `SSVM_LookupKey`'s 64-bit widening of it. Writing
 * it out here rather than reusing the helper is deliberate: if the two ever
 * disagree, this test is the thing that should notice, and it cannot notice a
 * disagreement it was written in terms of.
 */
static int32_t
stored_key(int trigger, int kind, int32_t subject)
{
    return (int32_t)((uint32_t)trigger | ((uint32_t)kind << 8) | ((uint32_t)subject << 10));
}

struct Fixture
{
    const char* name;
    int32_t lookup_key;
};

/*
 * Encode the fixtures into a `script.dat` / `script.idx` pair.
 *
 * Bodies are concatenated with no per-entry offsets — the idx sizes are the only
 * way to find each one — so this mirrors what the compiler writes rather than
 * inventing a friendlier format. Returns 1 and fills the two buffers.
 */
static int
build_container(
    const struct Fixture* fixtures,
    int count,
    uint8_t** out_dat,
    size_t* out_dat_length,
    uint8_t** out_idx,
    size_t* out_idx_length)
{
    struct SSVM_Error err;
    uint8_t* dat;
    uint8_t* idx;
    size_t dat_capacity = 8 + ((size_t)count * 1024);
    size_t offset = 8;
    int i;

    SSVM_ErrorClear(&err);
    dat = (uint8_t*)calloc(1, dat_capacity);
    idx = (uint8_t*)calloc(1, 4 + ((size_t)count * 4));
    assert(dat);
    assert(idx);

    dat[0] = (uint8_t)(count >> 24);
    dat[1] = (uint8_t)(count >> 16);
    dat[2] = (uint8_t)(count >> 8);
    dat[3] = (uint8_t)count;
    dat[4] = (uint8_t)(SSVM_COMPILER_VERSION >> 24);
    dat[5] = (uint8_t)(SSVM_COMPILER_VERSION >> 16);
    dat[6] = (uint8_t)(SSVM_COMPILER_VERSION >> 8);
    dat[7] = (uint8_t)SSVM_COMPILER_VERSION;
    idx[0] = (uint8_t)(count >> 24);
    idx[1] = (uint8_t)(count >> 16);
    idx[2] = (uint8_t)(count >> 8);
    idx[3] = (uint8_t)count;

    for( i = 0; i < count; i++ )
    {
        struct SSVM_Script script;
        uint16_t opcodes[1];
        int32_t int_operands[1];
        char* string_operands[1];
        size_t written = 0;

        memset(&script, 0, sizeof(script));
        opcodes[0] = SS_OP_RETURN;
        int_operands[0] = 0;
        string_operands[0] = NULL;

        script.name = (char*)fixtures[i].name;
        script.source_path = (char*)"fixture.rs2";
        script.lookup_key = fixtures[i].lookup_key;
        script.op_count = 1;
        script.opcodes = opcodes;
        script.int_operands = int_operands;
        script.string_operands = string_operands;

        if( !SSVM_ScriptEncode(&script, dat + offset, dat_capacity - offset, &written, &err) )
        {
            printf("  FAIL encode fixture %d: %s\n", i, err.message);
            free(dat);
            free(idx);
            return 0;
        }
        idx[4 + (i * 4) + 0] = (uint8_t)(written >> 24);
        idx[4 + (i * 4) + 1] = (uint8_t)(written >> 16);
        idx[4 + (i * 4) + 2] = (uint8_t)(written >> 8);
        idx[4 + (i * 4) + 3] = (uint8_t)written;
        offset += written;
    }

    *out_dat = dat;
    *out_dat_length = offset;
    *out_idx = idx;
    *out_idx_length = 4 + ((size_t)count * 4);
    return 1;
}

/** The script's name, or "(none)", so a failure says which rung answered. */
static const char*
which(const struct SSVM_Script* script)
{
    return script ? script->name : "(none)";
}

static int
is(const struct SSVM_Script* script, const char* name)
{
    return script && script->name && strcmp(script->name, name) == 0;
}

int
main(void)
{
    /*
     * Rung 1, 2 and 3 on one trigger, and rung 3 alone on a second.
     *
     * `[opheld2,_]` is not decoration: it is what proves the chain runs all the
     * way to the wildcard when neither of the first two rungs exists, which is
     * the case a "return after the category miss" bug would break and the
     * type/category cases would not.
     */
    static const struct Fixture k_fixtures[] = {
        { "[opheld1,bones]", 0 },
        { "[opheld1,_bones]", 0 },
        { "[opheld1,_]", 0 },
        { "[opheld2,_]", 0 },
    };
    struct Fixture fixtures[sizeof(k_fixtures) / sizeof(k_fixtures[0])];
    const int count = (int)(sizeof(k_fixtures) / sizeof(k_fixtures[0]));
    struct SSVM_Provider provider;
    struct SSVM_Error err;
    uint8_t* dat = NULL;
    uint8_t* idx = NULL;
    size_t dat_length = 0;
    size_t idx_length = 0;
    const struct SSVM_Script* script;

    printf("trigger lookup order\n");

    memcpy(fixtures, k_fixtures, sizeof(fixtures));
    fixtures[0].lookup_key = stored_key(SS_TRIGGER_OPHELD1, SS_LOOKUP_TYPE, FIXTURE_TYPE);
    fixtures[1].lookup_key = stored_key(SS_TRIGGER_OPHELD1, SS_LOOKUP_CATEGORY, FIXTURE_CATEGORY);
    fixtures[2].lookup_key = stored_key(SS_TRIGGER_OPHELD1, SS_LOOKUP_GLOBAL, 0);
    fixtures[3].lookup_key = stored_key(SS_TRIGGER_OPHELD2, SS_LOOKUP_GLOBAL, 0);

    if( !build_container(fixtures, count, &dat, &dat_length, &idx, &idx_length) )
        return 1;

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadMem(&provider, dat, dat_length, idx, idx_length, &err) )
    {
        printf("  FAIL load: %s\n", err.message);
        free(dat);
        free(idx);
        return 1;
    }
    CHECK(provider.loaded == count, "the fixture container loads");
    CHECK(provider.duplicate_keys == 0, "with four distinct keys");

    /* ---- getByTrigger: the chain ---------------------------------- */

    script = SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD1, FIXTURE_TYPE,
                                       FIXTURE_CATEGORY);
    printf("       type+category -> %s\n", which(script));
    CHECK(is(script, "[opheld1,bones]"), "an exact type beats the category it is in");

    script = SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD1, OTHER_TYPE,
                                       FIXTURE_CATEGORY);
    printf("       category only -> %s\n", which(script));
    CHECK(is(script, "[opheld1,_bones]"), "a category answers a type nothing binds");

    script = SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD1, OTHER_TYPE,
                                       OTHER_CATEGORY);
    printf("       neither       -> %s\n", which(script));
    CHECK(is(script, "[opheld1,_]"), "the `_` wildcard is what is left, and it answers");

    script = SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD1, -1, -1);
    CHECK(is(script, "[opheld1,_]"), "a subjectless lookup lands on the wildcard too");

    /* The chain must reach the wildcard from a *miss on both* earlier rungs,
     * not only from having been asked with -1. This is the trigger with nothing
     * but a wildcard, asked with a real type and a real category. */
    script = SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD2, FIXTURE_TYPE,
                                       FIXTURE_CATEGORY);
    CHECK(is(script, "[opheld2,_]"),
          "two misses still reach the wildcard — the chain does not stop early");

    /* ...and nothing after that. A trigger with no script of any kind gets
     * nothing, rather than borrowing another trigger's wildcard. */
    CHECK(SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPHELD3, FIXTURE_TYPE,
                                    FIXTURE_CATEGORY) == NULL,
          "a trigger with no script at all resolves to nothing");
    CHECK(SSVM_ProviderGetByTrigger(&provider, SS_TRIGGER_OPNPC1, FIXTURE_TYPE,
                                    FIXTURE_CATEGORY) == NULL,
          "and does not borrow a different trigger's wildcard");

    /* ---- getByTriggerSpecific: one rung, no chain ------------------ */

    script = SSVM_ProviderGetByTriggerSpecific(&provider, SS_TRIGGER_OPHELD1, FIXTURE_TYPE,
                                               FIXTURE_CATEGORY);
    CHECK(is(script, "[opheld1,bones]"), "specific with a type finds the type");

    CHECK(SSVM_ProviderGetByTriggerSpecific(&provider, SS_TRIGGER_OPHELD1, OTHER_TYPE,
                                            FIXTURE_CATEGORY) == NULL,
          "specific with a type does NOT fall through to the category");

    script = SSVM_ProviderGetByTriggerSpecific(&provider, SS_TRIGGER_OPHELD1, -1,
                                               FIXTURE_CATEGORY);
    CHECK(is(script, "[opheld1,_bones]"), "specific with only a category finds the category");

    CHECK(SSVM_ProviderGetByTriggerSpecific(&provider, SS_TRIGGER_OPHELD1, -1,
                                            OTHER_CATEGORY) == NULL,
          "specific with only a category does NOT fall through to the wildcard");

    script = SSVM_ProviderGetByTriggerSpecific(&provider, SS_TRIGGER_OPHELD1, -1, -1);
    CHECK(is(script, "[opheld1,_]"), "specific with neither finds the wildcard");

    /* ---- the name index is a separate address space ---------------- */

    /* Name-addressed lookup must not be confused by the trigger index: this is
     * how `[if_button,orbs:runbutton]` is reached, since its component uid does
     * not fit a compiled key's 21-bit subject. */
    CHECK(is(SSVM_ProviderGetByName(&provider, "[opheld1,_bones]"), "[opheld1,_bones]"),
          "a category-bound script is still reachable by its full name");

    SSVM_ProviderFree(&provider);
    free(dat);
    free(idx);

    if( g_fail )
    {
        printf("ss_provider_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("ss_provider_test: all checks passed\n");
    return 0;
}
