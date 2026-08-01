/*
 * The `loc_*` / `lc_*` config reads: the loc table, and the stack and the
 * *entity* each op reads from.
 *
 * Two halves, for the same reason param_test.c has two: the table and the
 * dispatch fail independently and neither catches the other.
 *
 * ── Half 1: the table (no VM) ────────────────────────────────────────
 *
 * Re-decodes the whole loc config group with the rscache decoder directly and
 * then asks `mock230_loc_name` / `mock230_loc_footprint` / `mock230_loc_known`
 * about *every* record. Exhaustive rather than spot-checked because both new
 * tables are sparse binary searches over a subset of the id space — a name
 * table holding 30,033 of 62,194 ids and a footprint table holding 17,309 — and
 * the failure mode of a wrong index is an answer belonging to a neighbouring
 * record. That is a wrong string, not a crash, and a spot check would have to
 * be lucky to land on one.
 *
 * The 1x1 majority is asserted too, and is not filler: the footprint table
 * stores only the overrides, so "id not found" has to answer the decoder's own
 * default. A version that answered 0x0 would pass every check that only looked
 * at the 17,309 records which are in the table.
 *
 * ── Half 2: the entity and the stack (with the VM) ───────────────────
 *
 * `loc_param` and `loc_name` read the **active loc**; `lc_name`, `lc_width` and
 * `lc_length` read a loc named on the stack. So this half builds a real scene,
 * adds real locs to it, and sets the active-loc pointer the way the server
 * does — as `slot + 1`, never as a pointer. A fixture that faked the slot would
 * prove nothing about the convention that exists so a script suspended across a
 * scene rebuild does not resume onto a reallocated array.
 *
 * `loc_name` is the one string-stack op in the family, so its assertion is
 * `def_string $s = loc_name;` — a value pushed to the int stack underflows and
 * aborts inside the VM, where an int comparison would never see it.
 *
 * `loc_param` pops **one** int off the stack and `lc_param` pops **two**. The
 * script here reads a param whose record wrote it out of key order *and* uses
 * the value afterwards, so both the sort and the arity are load-bearing.
 *
 * No ids are written in this file. Every loc, param and value is found in the
 * decoded data at run time.
 */

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_paramtable.h"
#include "mock230_scene.h"

#include "ss_opcode.h"
#include "ssc.h"
#include "ssvm.h"

#include <rscache.h>

#include <datatypes/dat2_config_loc.h>

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

/* The same ../ fallback every loader here carries: the binary is run from the
 * repo root by `make` and from src/ by hand. */
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
/* Opening the loc config group                                        */
/* ------------------------------------------------------------------ */

struct LocGroup
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
};

static int
loc_group_open(
    struct LocGroup* group,
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
        RSCache_Dat2DiskArchiveNewLoad(group->disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    if( !group->archive )
    {
        RSCache_Dat2DiskFree(group->disk);
        group->disk = NULL;
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(group->disk, group->archive);
    /* The loc decoder branches on the group revision, exactly as
     * mock230_locinfo.c and mock230_scene.c do before decoding this archive. */
    RSCache_ProfileSetGroupRevision(&group->profile, RSCACHE_TYPE_LOC,
                                    group->archive->revision);
    group->files = RSCache_FileListNewFromDecode(
        group->archive->data, group->archive->data_size, group->archive->file_count);
    return group->files != NULL;
}

static void
loc_group_close(struct LocGroup* group)
{
    RSCache_FileListFree(group->files);
    RSCache_Dat2DiskArchiveFree(group->archive);
    RSCache_Dat2DiskFree(group->disk);
    memset(group, 0, sizeof(*group));
}

/* ------------------------------------------------------------------ */
/* Half 1 — every record must read back its own fields                 */
/* ------------------------------------------------------------------ */

static void
audit_loc_fields(const char* cache_dir)
{
    struct LocGroup group;
    int records = 0;
    int named = 0;
    int nameless = 0;
    int sized = 0;
    int square = 0;
    int wrong_name = 0;
    int wrong_name_absent = 0;
    int wrong_size = 0;
    int wrong_known = 0;
    int max_id = -1;

    printf("loc config fields\n");
    if( !loc_group_open(&group, cache_dir) )
    {
        CHECK(0, "the loc config group is readable");
        return;
    }

    for( int i = 0; i < group.archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;
        int id = group.archive->file_ids[i];
        const char* stored;
        int width = 0;
        int length = 0;

        if( group.files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(&group.profile, group.files->files[i],
                                                    group.files->file_sizes[i]);
        if( !loc )
            continue;
        records++;
        if( id > max_id )
            max_id = id;

        if( !mock230_loc_known(id) )
        {
            if( wrong_known < 5 )
                printf("    loc %d decodes but reports unknown\n", id);
            wrong_known++;
        }

        stored = mock230_loc_name(id);
        if( loc->name )
        {
            named++;
            if( !stored || strcmp(stored, loc->name) != 0 )
            {
                if( wrong_name < 5 )
                    printf("    loc %d: name is \"%s\", table says \"%s\"\n", id, loc->name,
                           stored ? stored : "(none)");
                wrong_name++;
            }
        }
        else
        {
            nameless++;
            /* A table that answered a neighbouring row would show up here and
             * nowhere else: 32,161 records are nameless, so "returns something"
             * is a much weaker check than "returns nothing". */
            if( stored )
            {
                if( wrong_name_absent < 5 )
                    printf("    loc %d has no name but the table says \"%s\"\n", id, stored);
                wrong_name_absent++;
            }
        }

        mock230_loc_footprint(id, &width, &length);
        if( loc->size_x != 1 || loc->size_z != 1 )
            sized++;
        else
            square++;
        if( width != loc->size_x || length != loc->size_z )
        {
            if( wrong_size < 5 )
                printf("    loc %d is %dx%d, table says %dx%d\n", id, loc->size_x,
                       loc->size_z, width, length);
            wrong_size++;
        }

        RSCache_Dat2ConfigLocFree(loc);
    }

    printf("  %d records: %d named, %d nameless, %d with a non-1x1 footprint\n", records,
           named, nameless, sized);

    CHECK_EQ(wrong_known, 0, "every decoded record reports known");
    CHECK_EQ(wrong_name, 0, "every named record reads back its own name");
    CHECK_EQ(wrong_name_absent, 0, "every nameless record reads back nothing");
    CHECK_EQ(wrong_size, 0, "every record reads back its own footprint");
    CHECK_EQ(mock230_locinfo_count(), group.archive->file_count,
             "the loader saw every loc record");
    CHECK_EQ(mock230_locinfo_name_count(), named,
             "the loader retained exactly the names the decoder produced");
    CHECK_EQ(mock230_locinfo_size_count(), sized,
             "the loader retained exactly the footprint overrides");

    /*
     * Both populations have to be non-empty or the two assertions above stop
     * discriminating: with no nameless record "reads back nothing" is vacuous,
     * and with no 1x1 record the default path is never taken.
     */
    CHECK(nameless > 0 && named > 0, "the cache holds both named and nameless locs");
    CHECK(square > 0 && sized > 0, "the cache holds both 1x1 and larger locs");

    CHECK(!mock230_loc_known(-1), "a negative loc id is not known");
    CHECK(!mock230_loc_known(max_id + 1), "an id past the last record is not known");
    CHECK(mock230_loc_name(-1) == NULL, "a negative loc id has no name, and does not crash");
    {
        int width = 0;
        int length = 0;

        mock230_loc_footprint(max_id + 1, &width, &length);
        CHECK(width == 1 && length == 1, "an unknown id reports the decoder's 1x1 default");
    }

    loc_group_close(&group);
}

/* ------------------------------------------------------------------ */
/* Half 2 — subjects, found in the data                                */
/* ------------------------------------------------------------------ */

/*
 * Every subject is located by decoding, never written down.
 *
 * `param_*` is a loc whose own param list is NOT ascending by key, so a table
 * "sorted by construction" misses it — 525 of the 599 loc records carrying two
 * or more params are like that, which is why the sort is the thing most worth
 * proving. `wide_*` insists width != length so that transposing the two is a
 * failing mutation rather than an invisible one.
 */
struct Subjects
{
    int param_loc;
    int param_key;
    int param_value;

    int named_loc;
    char* named_text;

    int nameless_loc;

    int wide_loc;
    int wide_width;
    int wide_length;

    int unknown_loc;
};

static int
find_subjects(
    const char* cache_dir,
    struct Subjects* out)
{
    struct LocGroup group;
    int max_id = -1;

    memset(out, 0, sizeof(*out));
    out->param_loc = -1;
    out->named_loc = -1;
    out->nameless_loc = -1;
    out->wide_loc = -1;

    if( !loc_group_open(&group, cache_dir) )
        return 0;

    for( int i = 0; i < group.archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;
        int id = group.archive->file_ids[i];

        if( group.files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(&group.profile, group.files->files[i],
                                                    group.files->file_sizes[i]);
        if( !loc )
            continue;
        if( id > max_id )
            max_id = id;

        if( out->param_loc < 0 )
        {
            for( int p = 1; p < loc->params.count; p++ )
            {
                if( loc->params.keys[p] > loc->params.keys[p - 1] )
                    continue;
                if( !loc->params.values[p] || loc->params.kinds[p] != RSCACHE_PARAM_INT )
                    continue;
                /* A declared-string param would route onto the string stack,
                 * which is a different assertion; keep this one an int. */
                if( mock230_content_param_type(loc->params.keys[p]) == 's' )
                    continue;
                out->param_loc = id;
                out->param_key = loc->params.keys[p];
                out->param_value = *(const int*)loc->params.values[p];
                break;
            }
        }
        if( out->named_loc < 0 && loc->name && loc->name[0] )
        {
            out->named_loc = id;
            out->named_text = strdup(loc->name);
        }
        if( out->nameless_loc < 0 && !loc->name )
            out->nameless_loc = id;
        if( out->wide_loc < 0 && loc->size_x != loc->size_z )
        {
            out->wide_loc = id;
            out->wide_width = loc->size_x;
            out->wide_length = loc->size_z;
        }
        RSCache_Dat2ConfigLocFree(loc);
    }

    out->unknown_loc = max_id + 1;
    loc_group_close(&group);
    return out->param_loc >= 0 && out->named_loc >= 0 && out->nameless_loc >= 0 &&
           out->wide_loc >= 0;
}

/* ------------------------------------------------------------------ */
/* Half 2 — the VM fixture                                             */
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
 * The host under test, and nothing else.
 *
 * `mock230_ops_loc` reaches the scene and the loc table but never `struct
 * Mock230Server`, so it binds with a NULL user — no world, no socket, no pack.
 * `mock230_ops_param` is bound alongside it only because it owns
 * `mock230_push_typed_param`'s siblings; a script here that reached any other
 * host command would find none, which is the right blast radius.
 */
static int
host_command(struct SSVM_State* state, int opcode, int dot)
{
    if( mock230_ops_param(state, opcode, dot) )
        return 1;
    return mock230_ops_loc(state, opcode, dot);
}

static int
fixture_compile(
    struct Fixture* fixture,
    const struct Subjects* subjects,
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
    snprintf(fixture->dir, sizeof(fixture->dir), "loc_test_%d", (int)getpid());
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
    SSC_SymbolsAdd(&fixture->symbols, "t_loc_param", subjects->param_loc, SSC_SYM_LOC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_param", subjects->param_key, SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_loc_named", subjects->named_loc, SSC_SYM_LOC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_loc_nameless", subjects->nameless_loc, SSC_SYM_LOC,
                   NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_loc_wide", subjects->wide_loc, SSC_SYM_LOC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_loc_unknown", subjects->unknown_loc, SSC_SYM_LOC,
                   NULL);

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
    SSVM_EnvBindHost(&fixture->env, NULL, host_command);
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
 * Run `name` with `slot` as the active loc, and report the top of one stack.
 *
 * `slot < 0` runs with no active loc at all, which is how the pointer
 * requirement is exercised. The active entity is stored as `slot + 1` — the
 * server's own convention, so that a non-NULL pointer means "a loc is active"
 * without the VM having to know what a scene slot is.
 *
 * The string is copied out before `SSVM_StateRelease`, which frees the state's
 * pool — every string the VM holds belongs to that pool.
 */
static int
run_stack(
    struct Fixture* fixture,
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
        SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
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
        SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
    status = SSVM_Execute(state);
    CHECK(status == SSVM_ABORTED, msg);
    SSVM_StateRelease(state);
}

static void
test_ops(const char* cache_dir)
{
    struct Fixture fixture;
    struct Subjects subjects;
    int32_t result = 0;
    char source[3072];
    int param_slot;
    int named_slot;
    int nameless_slot;
    int base_x;
    int base_z;

    printf("loc opcodes\n");

    if( !find_subjects(cache_dir, &subjects) )
    {
        CHECK(0, "found a loc with an out-of-order param, a named loc, a nameless loc "
                 "and a non-square loc");
        return;
    }
    printf("  subjects: param loc %d key %d = %d, named loc %d \"%s\", nameless loc %d, "
           "non-square loc %d (%dx%d), unknown id %d\n",
           subjects.param_loc, subjects.param_key, subjects.param_value, subjects.named_loc,
           subjects.named_text, subjects.nameless_loc, subjects.wide_loc, subjects.wide_width,
           subjects.wide_length, subjects.unknown_loc);
    CHECK(subjects.wide_width != subjects.wide_length,
          "the footprint subject is not square, so transposing width and length fails");

    /*
     * A real scene, at the same zone the server's own selftest builds, and real
     * locs added to it — the active loc has to be a slot the scene will hand
     * back or the slot-not-pointer convention is not what is under test.
     */
    if( !mock230_scene_build(cache_dir, 426, 408) )
    {
        CHECK(0, "the scene builds (map squares and XTEA keys are present)");
        return;
    }
    base_x = mock230_scene_base_x();
    base_z = mock230_scene_base_z();
    /* Shape 10 is a plain centrepiece; the ops here read only `loc_id`, and a
     * tile well inside the 104x104 scene keeps the footprint in bounds. */
    param_slot = mock230_scene_add_loc(base_x + 50, base_z + 50, 0, subjects.param_loc, 10, 0);
    named_slot = mock230_scene_add_loc(base_x + 52, base_z + 50, 0, subjects.named_loc, 10, 0);
    nameless_slot =
        mock230_scene_add_loc(base_x + 54, base_z + 50, 0, subjects.nameless_loc, 10, 0);
    CHECK(param_slot >= 0 && named_slot >= 0 && nameless_slot >= 0,
          "the three subject locs went into the scene");
    if( param_slot < 0 || named_slot < 0 || nameless_slot < 0 )
        return;

    /*
     * `def_string $s = loc_name;` is the string-stack assertion: a result
     * pushed to the int stack leaves the string stack empty and the assignment
     * aborts inside the VM before the comparison below runs.
     *
     * `~loc_param_then_int` reads the param and then an int-stack constant, so
     * an arity slip — popping two where the signature pops one — shows up as a
     * wrong *second* value rather than as nothing at all.
     */
    snprintf(source, sizeof(source),
             "[proc,t_read_param]()(int)\n"
             "def_int $i = loc_param(t_param);\n"
             "return($i);\n"
             "\n"
             "[proc,t_read_loc_name]()(string)\n"
             "def_string $s = loc_name;\n"
             "return($s);\n"
             "\n"
             "[proc,t_read_lc_name]()(string)\n"
             "def_string $s = lc_name(t_loc_named);\n"
             "return($s);\n"
             "\n"
             "[proc,t_read_lc_name_nameless]()(string)\n"
             "def_string $s = lc_name(t_loc_nameless);\n"
             "return($s);\n"
             "\n"
             "[proc,t_read_width]()(int)\n"
             "def_int $i = lc_width(t_loc_wide);\n"
             "return($i);\n"
             "\n"
             "[proc,t_read_length]()(int)\n"
             "def_int $i = lc_length(t_loc_wide);\n"
             "return($i);\n"
             "\n"
             "[proc,t_read_lc_name_unknown]()(string)\n"
             "def_string $s = lc_name(t_loc_unknown);\n"
             "return($s);\n");

    if( !fixture_compile(&fixture, &subjects, source) )
        return;

    if( run_stack(&fixture, "[proc,t_read_param]", param_slot, &result, NULL) )
        CHECK_EQ(result, subjects.param_value,
                 "loc_param reads the active loc, and finds a key its record wrote "
                 "out of order");

    {
        char* text = NULL;

        if( run_stack(&fixture, "[proc,t_read_loc_name]", named_slot, NULL, &text) )
            CHECK(text && strcmp(text, subjects.named_text) == 0,
                  "loc_name lands on the string stack with the active loc's own name");
        free(text);
    }

    {
        char* text = NULL;

        if( run_stack(&fixture, "[proc,t_read_lc_name]", -1, NULL, &text) )
            CHECK(text && strcmp(text, subjects.named_text) == 0,
                  "lc_name reads the loc it is given, with no active loc at all");
        free(text);
    }

    {
        char* text = NULL;

        /*
         * The reference's own fallback (`name ?? 'null'`), not a name this file
         * invented — and the one case where the sparse name table has to answer
         * "nothing" rather than a neighbour's string.
         */
        if( run_stack(&fixture, "[proc,t_read_lc_name_nameless]", -1, NULL, &text) )
            CHECK(text && strcmp(text, "null") == 0,
                  "a nameless loc reads back the reference's 'null', not a neighbour's name");
        free(text);
    }

    if( run_stack(&fixture, "[proc,t_read_width]", -1, &result, NULL) )
        CHECK_EQ(result, subjects.wide_width, "lc_width is the record's size_x");
    if( run_stack(&fixture, "[proc,t_read_length]", -1, &result, NULL) )
        CHECK_EQ(result, subjects.wide_length, "lc_length is the record's size_z");

    /*
     * The two loud paths. Neither can be reached from content that is right, so
     * both would rot unasserted: an id past the end of the config must abort
     * rather than read as a nameless 1x1 loc, and the active-loc ops must abort
     * rather than act on whatever slot 0 happens to hold.
     */
    expect_abort(&fixture, "[proc,t_read_lc_name_unknown]", -1,
                 "lc_name on an id the config does not hold aborts");
    expect_abort(&fixture, "[proc,t_read_loc_name]", -1,
                 "loc_name with no active loc aborts");
    expect_abort(&fixture, "[proc,t_read_param]", -1,
                 "loc_param with no active loc aborts");

    fixture_close(&fixture);
    mock230_scene_free();
    free(subjects.named_text);
}

/* ------------------------------------------------------------------ */

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;

    cache_dir = resolve_dir(MOCK230_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs239-content", content_scratch,
                              sizeof(content_scratch));

    printf("loc config reads (%s)\n", cache_dir);

    /* The declared param types, which decide which stack `loc_param`'s result
     * lands on. Without them every param falls back to its stored kind. */
    mock230_content_load(content_dir);

    if( !mock230_locinfo_load(cache_dir) )
        CHECK(0, "loc configs loaded");

    audit_loc_fields(cache_dir);
    test_ops(cache_dir);

    mock230_locinfo_free();
    mock230_content_free();

    if( g_fail )
    {
        printf("loc_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("loc_test: all checks passed\n");
    return 0;
}
