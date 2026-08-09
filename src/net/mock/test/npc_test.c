/*
 * The `npc_*` / `nc_*` reads and the hunt searches: the record table, the
 * entity each op reads from, and the stack the answer lands on.
 *
 * Two halves, for the same reason param_test.c and loc_test.c have two: the
 * table and the dispatch fail independently and neither catches the other.
 *
 * ── Half 1: the table (no VM) ────────────────────────────────────────
 *
 * Re-decodes the whole npc config group with the rscache decoder directly and
 * then asks `mock230_npcinfo_record` about *every* record's `category` and
 * `ops[]`. Exhaustive rather than spot-checked: `category` is a new field on an
 * existing struct and the failure mode of forgetting to copy it is a table full
 * of zeroes, which reads as "the cache states no categories" — a plausible
 * sentence about data rather than an obvious bug.
 *
 * The half that matters most is the *nameless* records. `mock230_npcinfo()`
 * hides any record with no name behind a "Someone" placeholder, and 1,585 of
 * the records that carry a category have no name. So this half also asserts,
 * for a record it finds at run time, that the gated accessor reports 0 and the
 * ungated one reports the real category — which is the whole reason
 * `mock230_npcinfo_record` was added.
 *
 * ── Half 2: the entity and the stack (with the VM) ───────────────────
 *
 * `npc_param`, `npc_category` and `npc_hasop` read the **active npc**, which
 * this server holds as a slot in `state->host_tag` (+1, so zero means none) and
 * never as a pointer. `nc_category` reads an id off the stack. `npc_hunt`,
 * `npc_findcat` and `npc_huntall` read neither: they walk the roster from a
 * coord and *set* the active npc, so what they are checked on is which npc they
 * picked, not that they returned true.
 *
 * `npc_param` is the reason this half exists at all. Its result lands on the
 * stack the param's *declaration* names, so a value pushed to the int stack
 * where content wrote `def_string` underflows and aborts inside the VM — a
 * comparison on the int stack would never see it.
 *
 * `mock230_ops_npc` reaches `struct Mock230Server` for the roster and the
 * iterator, so unlike the loc and param tests this one binds a real (zeroed,
 * heap-allocated) server. It still needs no world, no socket and no pack: the
 * npcs are written straight into the pool.
 *
 * No npc, param, category or coord id is written in this file. Every subject is
 * found in the decoded data at run time.
 */

#include "mock230.h"
#include "mock230_content.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssc.h"
#include "ssvm.h"

#include <rscache.h>

#include <datatypes/dat2_config_npc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

static const char*
resolve_dir(
    const char* relative,
    char* scratch,
    size_t scratch_size)
{
    struct stat info;

    snprintf(scratch, scratch_size, "%s", relative);
    if( stat(scratch, &info) == 0 )
        return scratch;
    snprintf(scratch, scratch_size, "../%s", relative);
    return scratch;
}

/* ------------------------------------------------------------------ */
/* Opening the npc config group                                        */
/* ------------------------------------------------------------------ */

struct NpcGroup
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
};

static int
npc_group_open(
    struct NpcGroup* group,
    const char* cache_dir)
{
    int table;

    memset(group, 0, sizeof(*group));
    group->profile = RSCache_ProfileZero();
    group->profile.game = RSCACHE_GAME_OLDSCHOOL;
    group->profile.epoch = RSCACHE_EPOCH_DAT2;
    group->profile.revision = MOCK230_CACHE_REVISION;

    group->disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !group->disk )
        return 0;
    RSCache_Dat2DiskSetProfile(group->disk, &group->profile);
    table = RSCache_Dat2DiskTableId(group->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    group->archive =
        RSCache_Dat2DiskArchiveNewLoad(group->disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !group->archive )
    {
        RSCache_Dat2DiskFree(group->disk);
        group->disk = NULL;
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(group->disk, group->archive);
    /* The npc decoder branches on the group revision, exactly as
     * mock230_npcinfo.c does before decoding this archive. */
    RSCache_ProfileSetGroupRevision(&group->profile, RSCACHE_TYPE_NPC,
                                    group->archive->revision);
    group->files = RSCache_FileListNewFromDecode(
        group->archive->data, group->archive->data_size, group->archive->file_count);
    return group->files != NULL;
}

static void
npc_group_close(struct NpcGroup* group)
{
    RSCache_FileListFree(group->files);
    RSCache_Dat2DiskArchiveFree(group->archive);
    RSCache_Dat2DiskFree(group->disk);
    memset(group, 0, sizeof(*group));
}

/* ------------------------------------------------------------------ */
/* Subjects, all found at run time                                     */
/* ------------------------------------------------------------------ */

struct Subjects
{
    /** Has a category, an op2 (so it is huntable) and a name. */
    int hunt_npc;
    int hunt_category;
    /** A second record sharing hunt_category, for the findcat tie-break. */
    int hunt_npc_twin;
    /** Carries a category but no name — the ungated-accessor case. */
    int nameless_cat_npc;
    int nameless_category;
    /** Has an op1 but no op5, so hasop can be checked in both directions. */
    int hasop_npc;
    /** Carries a param whose declared type is *not* `i`, so a handler that
     *  routed only `i` to the int stack would send it to the wrong one. */
    int int_param_npc;
    int int_param_key;
    int int_param_value;
    char int_param_type;
    /** Carries a param the cache stored as a string. */
    int str_param_npc;
    int str_param_key;
    char str_param_value[128];
    /** Declares a default and is carried by nobody. */
    int default_param_key;
    int default_param_value;
    /*
     * An npc whose *content overlay* authors a `param=` row — the rank-1 half
     * of the merge. `death_drop` is the reason this matters: it exists only in
     * a server-band overlay and is nowhere in the cache record, so a cache-only
     * `npc_param` answers 0 and the drop tables add obj 0.
     */
    int authored_npc;
    int authored_key;
    int authored_value;
    /** The same, for a param the cache record *also* carries with a different
     *  value — the case that proves which rank wins. -1 when the tree has none. */
    int override_npc;
    int override_key;
    int override_value;
};

/* ------------------------------------------------------------------ */
/* Half 1 — every record must read back its own fields                 */
/* ------------------------------------------------------------------ */

static void
audit_records(
    const char* cache_dir,
    struct Subjects* out)
{
    struct NpcGroup group;
    int with_category = 0;
    int nameless_with_category = 0;
    int with_ops = 0;
    int category_wrong = 0;
    int ops_wrong = 0;
    int missing_row = 0;
    int records = 0;

    memset(out, 0, sizeof(*out));
    out->hunt_npc = -1;
    out->hunt_npc_twin = -1;
    out->nameless_cat_npc = -1;
    out->hasop_npc = -1;
    out->int_param_npc = -1;
    out->str_param_npc = -1;
    out->default_param_key = -1;
    out->authored_npc = -1;
    out->override_npc = -1;

    if( !npc_group_open(&group, cache_dir) )
    {
        CHECK(0, "the npc config group opens");
        return;
    }

    for( int i = 0; i < group.archive->file_count; i++ )
    {
        int id = group.archive->file_ids[i];
        struct RSCache_Dat2ConfigNpc* npc;
        const struct Mock230NpcInfo* info;
        int named;

        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(&group.profile, group.files->files[i],
                                                    group.files->file_sizes[i]);
        if( !npc )
            continue;
        records++;
        named = npc->name && strcmp(npc->name, "null") != 0;

        info = mock230_npcinfo_record(id);
        if( !info )
        {
            missing_row++;
            RSCache_Dat2ConfigNpcFree(npc);
            continue;
        }
        if( info->category != npc->category )
            category_wrong++;
        for( int op = 0; op < 5; op++ )
        {
            int want = npc->actions[op] != NULL;
            int got = info->ops[op] != NULL;

            if( want != got )
                ops_wrong++;
        }

        if( npc->category != 0 )
        {
            with_category++;
            if( !named )
            {
                nameless_with_category++;
                if( out->nameless_cat_npc < 0 )
                {
                    out->nameless_cat_npc = id;
                    out->nameless_category = npc->category;
                }
            }
        }
        if( npc->actions[0] || npc->actions[1] || npc->actions[2] || npc->actions[3] ||
            npc->actions[4] )
            with_ops++;

        /* A huntable subject: a category, an op2, and a name so the failure
         * message is readable. The twin has to be a *different* record with the
         * same category, which is what makes the findcat tie-break meaningful. */
        if( named && npc->category != 0 && npc->actions[1] )
        {
            if( out->hunt_npc < 0 )
            {
                out->hunt_npc = id;
                out->hunt_category = npc->category;
            }
            else if( out->hunt_npc_twin < 0 && npc->category == out->hunt_category )
            {
                out->hunt_npc_twin = id;
            }
        }
        /* An op1 but no op5 — both directions of hasop off one record. */
        if( out->hasop_npc < 0 && npc->actions[0] && !npc->actions[4] )
            out->hasop_npc = id;

        for( int p = 0; p < npc->params.count; p++ )
        {
            int key = npc->params.keys[p];
            char declared = mock230_content_param_type(key);

            if( !npc->params.values[p] )
                continue;
            if( npc->params.kinds[p] == 0 && out->int_param_npc < 0 && declared != 'i' &&
                declared != 0 && declared != 's' && declared != 'S' )
            {
                out->int_param_npc = id;
                out->int_param_key = key;
                out->int_param_value = *(const int*)npc->params.values[p];
                out->int_param_type = declared;
            }
            if( npc->params.kinds[p] != 0 && out->str_param_npc < 0 )
            {
                out->str_param_npc = id;
                out->str_param_key = key;
                snprintf(out->str_param_value, sizeof(out->str_param_value), "%s",
                         (const char*)npc->params.values[p]);
            }
        }

        RSCache_Dat2ConfigNpcFree(npc);
    }

    npc_group_close(&group);

    CHECK(records > 0, "the npc config group decodes");
    CHECK_EQ(missing_row, 0, "every decoded record has an ungated row");
    CHECK_EQ(category_wrong, 0, "every record's category reads back");
    CHECK_EQ(ops_wrong, 0, "every record's ops read back");
    printf("  note %d records, %d with a category (%d of them nameless), %d with ops\n",
           records, with_category, nameless_with_category, with_ops);

    /* The reason mock230_npcinfo_record exists, asserted rather than argued:
     * the gated accessor is *wrong* for this record and the ungated one is
     * right. If the cache ever stops holding a nameless categorised record this
     * fails loudly rather than quietly passing on an empty set. */
    CHECK(nameless_with_category > 0,
          "the cache has nameless records that carry a category");
    if( out->nameless_cat_npc >= 0 )
    {
        CHECK_EQ(mock230_npcinfo_record(out->nameless_cat_npc)->category,
                 out->nameless_category, "the ungated row answers for a nameless record");
        CHECK_EQ(mock230_npcinfo(out->nameless_cat_npc)->category, 0,
                 "the name-gated accessor would have answered 0");
    }

    /* An id past the end of the table is NULL, not a placeholder that looks
     * like a record with no category. */
    CHECK(mock230_npcinfo_record(-1) == NULL, "a negative id has no row");
}

/* ------------------------------------------------------------------ */
/* Half 2 — the VM                                                     */
/* ------------------------------------------------------------------ */

struct Fixture
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSVM_Provider provider;
    struct SSVM_Env env;
    char dir[256];
};

/*
 * The hosts under test, and nothing else.
 *
 * `mock230_ops_param` rides along because `mock230_push_typed_param` lives in
 * it; a script here that reached any other host command would find none, which
 * is the right blast radius.
 */
static int
host_command(struct SSVM_State* state, int opcode, int dot)
{
    if( mock230_ops_param(state, opcode, dot) )
        return 1;
    return mock230_ops_npc(state, opcode, dot);
}

static int
fixture_compile(
    struct Fixture* fixture,
    const struct Subjects* subjects,
    struct Mock230Server* srv,
    const char* source)
{
    char src_dir[300];
    char path[400];
    char command[900];
    struct SSC_Diag diag;
    struct SSVM_Error err;
    FILE* file;

    memset(fixture, 0, sizeof(*fixture));
    /* Under the build directory, not /tmp: the lane owns build_lane3 and
     * nothing else. */
    snprintf(fixture->dir, sizeof(fixture->dir), "npc_test_%d", (int)getpid());
    snprintf(src_dir, sizeof(src_dir), "%s/src", fixture->dir);
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", fixture->dir, src_dir);
    if( system(command) != 0 )
    {
        CHECK(0, "temporary script directory created");
        return 0;
    }

    snprintf(path, sizeof(path), "%s/test.rs2", src_dir);
    file = fopen(path, "wb");
    if( !file )
    {
        CHECK(0, "temporary script written");
        return 0;
    }
    fputs(source, file);
    fclose(file);

    SSC_SymbolsInit(&fixture->symbols);
    /* Test names for whatever the cache turned out to hold. */
    SSC_SymbolsAdd(&fixture->symbols, "t_hunt_npc", subjects->hunt_npc, SSC_SYM_NPC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_hunt_cat", subjects->hunt_category, SSC_SYM_CATEGORY,
                   NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_nameless_npc", subjects->nameless_cat_npc,
                   SSC_SYM_NPC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_nameless_cat", subjects->nameless_category,
                   SSC_SYM_CATEGORY, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_int_param", subjects->int_param_key, SSC_SYM_PARAM,
                   NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_str_param", subjects->str_param_key, SSC_SYM_PARAM,
                   NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_default_param", subjects->default_param_key,
                   SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_authored_param", subjects->authored_key,
                   SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_override_param",
                   subjects->override_npc >= 0 ? subjects->override_key
                                               : subjects->authored_key,
                   SSC_SYM_PARAM, NULL);

    fixture->compiler = SSC_New(&fixture->symbols);
    memset(&diag, 0, sizeof(diag));

    if( !SSC_CompileDir(fixture->compiler, src_dir, &diag) )
    {
        printf("  FAIL compile: %s:%d: %s\n", diag.file, diag.line, diag.message);
        g_fail++;
        return 0;
    }
    if( !SSC_Write(fixture->compiler, fixture->dir, &diag) )
    {
        printf("  FAIL write: %s\n", diag.message);
        g_fail++;
        return 0;
    }

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(&fixture->provider, fixture->dir, &err) )
    {
        printf("  FAIL reading back what we just wrote: %s\n", err.message);
        g_fail++;
        return 0;
    }

    SSVM_EnvInit(&fixture->env, &fixture->provider);
    SSVM_EnvBindHost(&fixture->env, srv, host_command);
    return 1;
}

static void
fixture_close(struct Fixture* fixture)
{
    char command[600];

    SSVM_EnvFree(&fixture->env);
    SSVM_ProviderFree(&fixture->provider);
    SSC_Free(fixture->compiler);
    SSC_SymbolsFree(&fixture->symbols);
    snprintf(command, sizeof(command), "rm -rf %s", fixture->dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", fixture->dir);
}

/*
 * Run `name` with `slot` as the active npc, and report the top of one stack.
 *
 * `slot < 0` runs with no active npc at all. The active npc is set the way the
 * server sets it — the pointer *and* `host_tag = slot + 1` — because the slot is
 * what the handlers actually resolve through and a fixture that set only the
 * pointer would pass while the real thing failed.
 */
static int
run_stack(
    struct Fixture* fixture,
    struct Mock230Server* srv,
    const char* name,
    int slot,
    int32_t* out,
    char** out_str)
{
    const struct SSVM_Script* script = SSVM_ProviderGetByName(&fixture->provider, name);
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int ok = 0;

    if( !script )
    {
        printf("  FAIL no script named %s\n", name);
        g_fail++;
        return 0;
    }
    state = SSVM_StateAlloc(&fixture->env, script, NULL, 0, NULL, 0);
    if( !state )
    {
        printf("  FAIL could not allocate a state for %s\n", name);
        g_fail++;
        return 0;
    }
    if( slot >= 0 )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
    }
    status = SSVM_Execute(state);
    if( status != SSVM_FINISHED )
    {
        printf("  FAIL %s: %s\n", name, SSVM_Backtrace(state));
        g_fail++;
    }
    else
    {
        if( out )
            *out = state->isp > 0 ? state->int_stack[state->isp - 1] : 0;
        if( out_str )
            *out_str = state->ssp > 0 && state->str_stack[state->ssp - 1]
                           ? strdup(state->str_stack[state->ssp - 1])
                           : NULL;
        ok = 1;
    }
    SSVM_StateRelease(state);
    return ok;
}

/** Runs `name` and passes only if the script aborts. */
static void
expect_abort(
    struct Fixture* fixture,
    struct Mock230Server* srv,
    const char* name,
    int slot,
    const char* msg)
{
    const struct SSVM_Script* script = SSVM_ProviderGetByName(&fixture->provider, name);
    struct SSVM_State* state;
    enum SSVM_Exec status;

    if( !script )
    {
        printf("  FAIL no script named %s\n", name);
        g_fail++;
        return;
    }
    state = SSVM_StateAlloc(&fixture->env, script, NULL, 0, NULL, 0);
    if( !state )
    {
        printf("  FAIL could not allocate a state for %s\n", name);
        g_fail++;
        return;
    }
    if( slot >= 0 )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
    }
    status = SSVM_Execute(state);
    CHECK(status == SSVM_ABORTED, msg);
    SSVM_StateRelease(state);
}

/** Runs `name` with no active npc and reports the slot the script left active. */
static int
run_finder(
    struct Fixture* fixture,
    const char* name,
    int32_t* out_found)
{
    const struct SSVM_Script* script = SSVM_ProviderGetByName(&fixture->provider, name);
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int slot = -1;

    if( !script )
    {
        printf("  FAIL no script named %s\n", name);
        g_fail++;
        return -1;
    }
    state = SSVM_StateAlloc(&fixture->env, script, NULL, 0, NULL, 0);
    if( !state )
    {
        printf("  FAIL could not allocate a state for %s\n", name);
        g_fail++;
        return -1;
    }
    status = SSVM_Execute(state);
    if( status != SSVM_FINISHED )
    {
        printf("  FAIL %s: %s\n", name, SSVM_Backtrace(state));
        g_fail++;
    }
    else
    {
        if( out_found )
            *out_found = state->isp > 0 ? state->int_stack[state->isp - 1] : 0;
        slot = (int)state->host_tag - 1;
    }
    SSVM_StateRelease(state);
    return slot;
}

/*
 * Three npcs on one level around a known tile, and the coord the scripts search
 * from.
 *
 * The coord literal in the scripts below is `0_50_50_20_20`, which the compiler
 * packs as level 0, x = 50*64+20, z = 50*64+20 — so the pool is filled relative
 * to that rather than to a number written twice.
 */
enum
{
    K_SEARCH_MX = 50,
    K_SEARCH_MZ = 50,
    K_SEARCH_LX = 20,
    K_SEARCH_LZ = 20,
    K_SEARCH_X = (K_SEARCH_MX * 64) + K_SEARCH_LX,
    K_SEARCH_Z = (K_SEARCH_MZ * 64) + K_SEARCH_LZ
};

static void
place_npc(
    struct Mock230Server* srv,
    int slot,
    int type,
    int dx,
    int dz)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

    memset(npc, 0, sizeof(*npc));
    npc->active = 1;
    npc->type = type;
    npc->level = 0;
    npc->x = K_SEARCH_X + dx;
    npc->z = K_SEARCH_Z + dz;
    npc->combat_target = -1;
    npc->death_tick = -1;
    npc->respawn_tick = -1;
    npc->despawn_tick = -1;
}

static void
test_ops(struct Mock230Server* srv, const struct Subjects* subjects)
{
    struct Fixture fixture;
    int32_t result = 0;
    char* text = NULL;
    char source[4096];
    int near_slot = 3;
    int tie_slot = 5;
    int far_slot = 7;
    int twin_slot = 11;
    int nameless_slot = 15;

    printf("\n-- the opcodes, through the VM --\n");

    if( subjects->hunt_npc < 0 || subjects->nameless_cat_npc < 0 ||
        subjects->hasop_npc < 0 || subjects->int_param_npc < 0 ||
        subjects->str_param_npc < 0 || subjects->default_param_key < 0 ||
        subjects->authored_npc < 0 )
    {
        CHECK(0, "every subject was found in the cache");
        return;
    }

    /*
     * The layout is what makes `npc_hunt`'s two easy-to-get-wrong details fail
     * separately, so it is worth reading rather than skimming. Offsets from the
     * search tile, with the slot each lands in:
     *
     *   near  (+1,  0)  slot 3    chebyshev 1   euclidean^2 1
     *   tie   (-1,  0)  slot 5    chebyshev 1   euclidean^2 1
     *   twin  (+1, +1)  slot 11   chebyshev 1   euclidean^2 2
     *   far   (+4,  0)  slot 7    chebyshev 4   euclidean^2 16
     *
     * The reference ranks by euclidean-squared and keeps the *last* candidate at
     * an equal distance (`<=`, NpcOps.ts:307). So the right answer is **slot 5**:
     * near and tie are joint-nearest and tie is later in the walk.
     *
     *   - rank by the chebyshev range the filter uses instead, and near, tie and
     *     twin are all at 1, so the last of the three (slot 11) wins;
     *   - keep the first at an equal distance (`<`) instead, and slot 3 wins.
     *
     * One assertion, three distinguishable answers.
     */
    place_npc(srv, near_slot, subjects->hunt_npc, 1, 0);
    place_npc(srv, tie_slot, subjects->hunt_npc, -1, 0);
    place_npc(srv, far_slot, subjects->hunt_npc, 4, 0);
    place_npc(srv, twin_slot,
              subjects->hunt_npc_twin >= 0 ? subjects->hunt_npc_twin : subjects->hunt_npc, 1,
              1);
    place_npc(srv, nameless_slot, subjects->nameless_cat_npc, 0, 1);
    srv->npc_slot_max = nameless_slot + 1;

    snprintf(source, sizeof(source),
             /* The active-npc reads. */
             "[proc,t_category]()(int)\n"
             "return(npc_category);\n"
             "\n"
             "[proc,t_nc_category]()(int)\n"
             "return(nc_category(t_nameless_npc));\n"
             "\n"
             "[proc,t_hasop1]()(int)\n"
             "return(npc_hasop(1));\n"
             "\n"
             "[proc,t_hasop2]()(int)\n"
             "return(npc_hasop(2));\n"
             "\n"
             "[proc,t_hasop5]()(int)\n"
             "return(npc_hasop(5));\n"
             "\n"
             /* npc_param, on both stacks and on the declared default. */
             "[proc,t_param_int]()(int)\n"
             "return(npc_param(t_int_param));\n"
             "\n"
             "[proc,t_param_str]()(string)\n"
             "def_string $s = npc_param(t_str_param);\n"
             "return($s);\n"
             "\n"
             "[proc,t_param_default]()(int)\n"
             "return(npc_param(t_default_param));\n"
             "\n"
             "[proc,t_param_authored]()(int)\n"
             "return(npc_param(t_authored_param));\n"
             "\n"
             "[proc,t_param_override]()(int)\n"
             "return(npc_param(t_override_param));\n"
             "\n"
             /* The searches. Each returns the boolean and leaves an active npc. */
             "[proc,t_hunt]()(int)\n"
             "return(npc_hunt(0_%d_%d_%d_%d, 8, 0));\n"
             "\n"
             "[proc,t_hunt_narrow]()(int)\n"
             "return(npc_hunt(0_%d_%d_%d_%d, 0, 0));\n"
             "\n"
             "[proc,t_findcat]()(int)\n"
             "return(npc_findcat(0_%d_%d_%d_%d, t_hunt_cat, 8, 0));\n"
             "\n"
             "[proc,t_findcat_miss]()(int)\n"
             "return(npc_findcat(0_%d_%d_%d_%d, t_nameless_cat, 8, 0));\n"
             "\n"
             "[proc,t_huntall]()(int)\n"
             "npc_huntall(0_%d_%d_%d_%d, 8, 0);\n"
             "return(1);\n"
             "\n"
             "[proc,t_huntall_narrow]()(int)\n"
             "npc_huntall(0_%d_%d_%d_%d, 0, 0);\n"
             "return(1);\n"
             "\n"
             "[proc,t_findallzone]()(int)\n"
             "npc_findallzone(0_%d_%d_%d_%d);\n"
             "return(1);\n",
             K_SEARCH_MX, K_SEARCH_MZ, K_SEARCH_LX, K_SEARCH_LZ, K_SEARCH_MX, K_SEARCH_MZ,
             K_SEARCH_LX, K_SEARCH_LZ, K_SEARCH_MX, K_SEARCH_MZ, K_SEARCH_LX, K_SEARCH_LZ,
             K_SEARCH_MX, K_SEARCH_MZ, K_SEARCH_LX, K_SEARCH_LZ, K_SEARCH_MX, K_SEARCH_MZ,
             K_SEARCH_LX, K_SEARCH_LZ, K_SEARCH_MX, K_SEARCH_MZ, K_SEARCH_LX, K_SEARCH_LZ,
             K_SEARCH_MX, K_SEARCH_MZ, K_SEARCH_LX, K_SEARCH_LZ);

    if( !fixture_compile(&fixture, subjects, srv, source) )
        return;

    /* ---- the active-npc reads ------------------------------------- */

    if( run_stack(&fixture, srv, "[proc,t_category]", near_slot, &result, NULL) )
        CHECK_EQ(result, subjects->hunt_category, "npc_category off the active npc");

    /* The ungated read, through the opcode this time: a nameless record still
     * has to answer its category. */
    if( run_stack(&fixture, srv, "[proc,t_category]", nameless_slot, &result, NULL) )
        CHECK_EQ(result, subjects->nameless_category,
                 "npc_category off a nameless active npc");

    /* nc_category takes the id off the stack, so it needs no active npc at
     * all — run it without one. */
    if( run_stack(&fixture, srv, "[proc,t_nc_category]", -1, &result, NULL) )
        CHECK_EQ(result, subjects->nameless_category, "nc_category off an id");

    place_npc(srv, near_slot, subjects->hasop_npc, 1, 0);
    if( run_stack(&fixture, srv, "[proc,t_hasop1]", near_slot, &result, NULL) )
        CHECK_EQ(result, 1, "npc_hasop(1) on a record that declares op1");
    if( run_stack(&fixture, srv, "[proc,t_hasop5]", near_slot, &result, NULL) )
        CHECK_EQ(result, 0, "npc_hasop(5) on a record that does not declare op5");
    place_npc(srv, near_slot, subjects->hunt_npc, 1, 0);
    if( run_stack(&fixture, srv, "[proc,t_hasop2]", near_slot, &result, NULL) )
        CHECK_EQ(result, 1, "npc_hasop(2) on a huntable record");

    /* ---- npc_param ------------------------------------------------- */

    place_npc(srv, near_slot, subjects->int_param_npc, 1, 0);
    if( run_stack(&fixture, srv, "[proc,t_param_int]", near_slot, &result, NULL) )
        CHECK_EQ(result, subjects->int_param_value, "npc_param reads the stored int");
    printf("  note the int subject's param is declared '%c', not 'i'\n",
           subjects->int_param_type);

    if( run_stack(&fixture, srv, "[proc,t_param_default]", near_slot, &result, NULL) )
        CHECK_EQ(result, subjects->default_param_value,
                 "npc_param falls back to the declared default");

    /*
     * The rank-1 half of the merge, and the regression this whole fallback
     * exists to prevent: an authored overlay param — `death_drop` is the one the
     * drop tables read — lives only in the content tree and is nowhere in the
     * cache record. A cache-only handler answers 0 here and `obj_add` gets
     * obj 0.
     */
    place_npc(srv, near_slot, subjects->authored_npc, 1, 0);
    srv->npcs[near_slot].def = mock230_content_npc(subjects->authored_npc);
    if( run_stack(&fixture, srv, "[proc,t_param_authored]", near_slot, &result, NULL) )
        CHECK_EQ(result, subjects->authored_value,
                 "npc_param reads a param authored only in the content overlay");

    if( subjects->override_npc >= 0 )
    {
        /* The same param on a record the cache also carries, with a different
         * value: rank 1 has to win. */
        place_npc(srv, near_slot, subjects->override_npc, 1, 0);
        srv->npcs[near_slot].def = mock230_content_npc(subjects->override_npc);
        if( run_stack(&fixture, srv, "[proc,t_param_override]", near_slot, &result, NULL) )
            CHECK_EQ(result, subjects->override_value,
                     "the authored overlay beats the cache record");
    }
    else
    {
        printf("  note no npc block overrides a param the cache also carries;"
               " precedence is unproven by this run\n");
    }
    srv->npcs[near_slot].def = NULL;

    place_npc(srv, near_slot, subjects->str_param_npc, 1, 0);
    if( run_stack(&fixture, srv, "[proc,t_param_str]", near_slot, NULL, &text) )
    {
        CHECK(text && strcmp(text, subjects->str_param_value) == 0,
              "npc_param puts a string param on the string stack");
        if( text && strcmp(text, subjects->str_param_value) != 0 )
            printf("       got \"%s\" want \"%s\"\n", text, subjects->str_param_value);
    }
    free(text);
    text = NULL;

    /* The slot is what resolves, not the pointer: park the script's npc and the
     * read has to abort rather than answer from a dead slot. */
    place_npc(srv, near_slot, subjects->hunt_npc, 1, 0);
    srv->npcs[near_slot].active = 0;
    expect_abort(&fixture, srv, "[proc,t_category]", near_slot,
                 "npc_category aborts when the active npc's slot went dead");
    srv->npcs[near_slot].active = 1;

    /* ---- the searches ---------------------------------------------- */

    place_npc(srv, near_slot, subjects->hunt_npc, 1, 0);
    {
        int found_slot = run_finder(&fixture, "[proc,t_hunt]", &result);

        CHECK_EQ(result, 1, "npc_hunt found something");
        /* See the layout comment above: slot 5 is the only answer consistent
         * with euclidean-squared ranking *and* the reference's last-wins tie. */
        CHECK_EQ(found_slot, tie_slot, "npc_hunt ranked euclidean and kept the last tie");
    }
    {
        /* distance 0 excludes every npc placed off the search tile. */
        int found_slot = run_finder(&fixture, "[proc,t_hunt_narrow]", &result);

        CHECK_EQ(result, 0, "npc_hunt at distance 0 finds nothing");
        (void)found_slot;
    }
    {
        int found_slot = run_finder(&fixture, "[proc,t_findcat]", &result);

        CHECK_EQ(result, 1, "npc_findcat found something");
        CHECK_EQ(found_slot, tie_slot, "npc_findcat ranked the same way npc_hunt does");
    }
    if( subjects->nameless_category != subjects->hunt_category )
    {
        /* The nameless subject is placed a tile away and carries a different
         * category — so this asks the filter to reject the near ones. */
        int found_slot = run_finder(&fixture, "[proc,t_findcat_miss]", &result);

        CHECK_EQ(result, 1, "npc_findcat matched the other category");
        CHECK_EQ(found_slot, nameless_slot, "npc_findcat crossed to the right record");
    }

    /*
     * huntall fills the shared iterator, which `npc_findnext` walks — and
     * findnext lives in mock230_scripts.c, which this fixture deliberately does
     * not bind. So the assertion is on the iterator itself, which is both
     * stricter and independent of the other file.
     */
    srv->iterator.count = 0;
    srv->iterator.kind = 0;
    if( run_finder(&fixture, "[proc,t_huntall]", &result) >= -1 )
    {
        int saw_near = 0;
        int saw_far = 0;
        int saw_nameless = 0;

        CHECK_EQ(srv->iterator.kind, SSVM_ENT_NPC, "npc_huntall claimed the npc iterator");
        for( int i = 0; i < srv->iterator.count; i++ )
        {
            if( srv->iterator.slots[i] == near_slot )
                saw_near = 1;
            if( srv->iterator.slots[i] == far_slot )
                saw_far = 1;
            if( srv->iterator.slots[i] == nameless_slot )
                saw_nameless = 1;
        }
        CHECK(saw_near && saw_far, "npc_huntall collected both huntable npcs");
        /* The op2 filter is the whole difference between huntall and
         * findallany. A nameless record with no op2 must not be in the list. */
        if( mock230_npcinfo_record(subjects->nameless_cat_npc) &&
            !mock230_npcinfo_record(subjects->nameless_cat_npc)->ops[1] )
            CHECK(!saw_nameless, "npc_huntall skipped the npc with no op2");
    }

    /*
     * The same search at distance 0, which is what proves the *arity*.
     *
     * Membership alone cannot: the coord literal packs to a number in the
     * millions, so a handler that popped the coord where the distance belongs
     * would search a radius of millions and still collect every subject. Asking
     * for an empty list is the assertion that swapping the two breaks.
     */
    srv->iterator.count = 0;
    srv->iterator.kind = 0;
    if( run_finder(&fixture, "[proc,t_huntall_narrow]", &result) >= -1 )
    {
        CHECK_EQ(srv->iterator.count, 0, "npc_huntall at distance 0 collects nothing");
    }

    /*
     * `npc_findallzone` fills the same iterator from the containing 8x8 zone,
     * and the layout separates it from a radius search without a second fixture.
     *
     * The search tile is (3220, 3220), so the zone is x/z 3216..3223:
     *
     *   near  (3221, 3220)  in       tie  (3219, 3220)  in
     *   twin  (3221, 3221)  in       nameless (3220, 3221)  in
     *   far   (3224, 3220)  **out** — one tile past the zone edge, and well
     *                       inside `npc_huntall`'s radius of 8, which collects it
     *
     * So `far` is the whole assertion: a body that reused `search_matches` with
     * some distance, or that centred an 8x8 box on the coord instead of masking
     * to the zone, would include it. And `nameless` being present is what
     * separates this from `npc_huntall`, whose op2 filter drops it.
     */
    srv->iterator.count = 0;
    srv->iterator.kind = 0;
    if( run_finder(&fixture, "[proc,t_findallzone]", &result) >= -1 )
    {
        int saw_near = 0;
        int saw_tie = 0;
        int saw_twin = 0;
        int saw_nameless = 0;
        int saw_far = 0;

        CHECK_EQ(srv->iterator.kind, SSVM_ENT_NPC,
                 "npc_findallzone claimed the npc iterator");
        for( int i = 0; i < srv->iterator.count; i++ )
        {
            if( srv->iterator.slots[i] == near_slot )
                saw_near = 1;
            if( srv->iterator.slots[i] == tie_slot )
                saw_tie = 1;
            if( srv->iterator.slots[i] == twin_slot )
                saw_twin = 1;
            if( srv->iterator.slots[i] == nameless_slot )
                saw_nameless = 1;
            if( srv->iterator.slots[i] == far_slot )
                saw_far = 1;
        }
        CHECK(saw_near && saw_tie && saw_twin && saw_nameless,
              "npc_findallzone collected every npc in the zone, filtered by nothing");
        CHECK(!saw_far, "npc_findallzone stopped at the zone edge, not at a radius");
    }

    fixture_close(&fixture);
}

/* ------------------------------------------------------------------ */

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;
    struct Subjects subjects;
    struct Mock230Server* srv;

    cache_dir = resolve_dir(MOCK230_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs239-content", content_scratch,
                              sizeof(content_scratch));

    printf("-- the npc record table --\n");

    /* The param declarations first: half 1 asks each param what it is declared
     * as while it picks subjects. */
    mock230_content_load(content_dir);
    if( !mock230_npcinfo_load(cache_dir) )
    {
        printf("  FAIL npc metadata did not load from %s\n", cache_dir);
        return 1;
    }

    audit_records(cache_dir, &subjects);

    /* A param nobody carries, so `npc_param` has to answer the declaration. Any
     * such param will do; the first one with a non-zero default is the one that
     * can tell "read the default" from "pushed zero". */
    for( int key = 0; key < 4096 && subjects.default_param_key < 0; key++ )
    {
        if( mock230_content_param_type(key) == 0 )
            continue;
        if( mock230_content_param_type(key) == 's' || mock230_content_param_type(key) == 'S' )
            continue;
        if( mock230_content_param_default(key) == 0 )
            continue;
        if( mock230_npc_param(subjects.int_param_npc, key) )
            continue;
        subjects.default_param_key = key;
        subjects.default_param_value = mock230_content_param_default(key);
    }

    /*
     * An npc block that authors a `param=` row, found by walking the content
     * tree's own npc defs rather than by naming one. The *override* subject is
     * the sharper case: the same param carried by the cache record with a
     * different value, so which rank wins is observable.
     */
    for( int id = 0; id < 40000 && (subjects.override_npc < 0 || subjects.authored_npc < 0);
         id++ )
    {
        const struct Mock230NpcDef* def = mock230_content_npc(id);

        if( !def || def->param_count <= 0 )
            continue;
        for( int i = 0; i < def->param_count && subjects.authored_npc < 0; i++ )
        {
            /* The death_drop shape: authored, and *absent* from the cache
             * record, so a cache-only read answers the declared default. */
            if( mock230_npc_param(id, def->params[i].key) )
                continue;
            /* And whose authored value differs from the param's declared
             * `default=` — otherwise "read the overlay" and "fall through to
             * the default" produce the same number and the check proves
             * nothing. */
            if( mock230_content_param_default(def->params[i].key) == def->params[i].value )
                continue;
            subjects.authored_npc = id;
            subjects.authored_key = def->params[i].key;
            subjects.authored_value = def->params[i].value;
        }
        for( int i = 0; i < def->param_count; i++ )
        {
            const struct Mock230NpcParam* row =
                mock230_npc_param(id, def->params[i].key);

            if( !row || row->sval || row->ival == def->params[i].value )
                continue;
            subjects.override_npc = id;
            subjects.override_key = def->params[i].key;
            subjects.override_value = def->params[i].value;
            break;
        }
    }
    /*
     * Every authored row must be retrievable by its id, not just the one the
     * subject search happened to pick. This is the assertion that covers
     * `death_drop` without naming it: it is one of these rows, and the drop
     * tables are the only reason `npc_param` had a special case at all.
     */
    {
        int defs_with_params = 0;
        int rows = 0;
        int unreachable = 0;

        for( int id = 0; id < 40000; id++ )
        {
            const struct Mock230NpcDef* def = mock230_content_npc(id);
            int32_t value = 0;

            if( !def || def->param_count <= 0 )
                continue;
            defs_with_params++;
            for( int i = 0; i < def->param_count; i++ )
            {
                rows++;
                if( !mock230_content_npc_param(def, def->params[i].key, &value) ||
                    value != def->params[i].value )
                    unreachable++;
            }
        }
        CHECK(rows > 0, "the content tree authors npc params at all");
        CHECK_EQ(unreachable, 0, "every authored npc param is reachable by id");
        printf("  note %d npc defs author %d params between them\n", defs_with_params, rows);
    }

    if( subjects.authored_npc >= 0 )
        printf("  note authored subject: npc %d param %d = %d%s\n", subjects.authored_npc,
               subjects.authored_key, subjects.authored_value,
               subjects.override_npc >= 0 ? "" : " (no cache-overriding case found)");

    srv = (struct Mock230Server*)calloc(1, sizeof(*srv));
    if( !srv )
    {
        printf("  FAIL could not allocate a server\n");
        return 1;
    }
    test_ops(srv, &subjects);
    free(srv);

    mock230_npcinfo_free();
    mock230_content_free();

    if( g_fail )
    {
        printf("\nnpc_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("\nnpc_test: all checks passed\n");
    return 0;
}
