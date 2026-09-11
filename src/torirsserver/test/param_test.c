/*
 * The `*_param` family: the loc and struct tables, and the stack the result
 * lands on.
 *
 * Two halves, because the family has two independent ways to be wrong and
 * neither catches the other.
 *
 * ── Half 1: the table (no VM) ────────────────────────────────────────
 *
 * It re-decodes the loc and struct config groups with the rscache decoder
 * *directly* and then asks `ToriRSServer_LocParam` / `ToriRSServer_StructParam` for
 * every single row it saw. Exhaustive rather than spot-checked, and the reason
 * is the failure mode this family actually has: a record's own params arrive in
 * the order the cache wrote them, which is not ascending by key, so a binary
 * search over a table that was "sorted by construction" does not crash — it
 * misses, and a miss reads as "this record has no such param", which content
 * answers with the declared default. Wrong everywhere, loud nowhere. Spot
 * checks would have to be lucky; this cannot be.
 *
 * The test also counts how many records are out of key order and refuses to
 * pass if the answer is zero, because that would mean the assertion above had
 * quietly stopped testing anything.
 *
 * No ids are written in this file. Every record, param and value the assertions
 * use is found in the decoded data at run time — which is also what keeps it
 * honest when the cache is replaced.
 *
 * ── Half 2: the stack routing (with the VM) ──────────────────────────
 *
 * `struct_param` and `lc_param` are `runtime_typed`: the *param's declared
 * type* decides whether the result lands on the int stack or the string stack.
 * Pushing to the wrong one does not fail at the call — it leaves the two stacks
 * out of step for the rest of the script. So the assertion has to be a script
 * that puts the result in a typed local: `def_string $s = struct_param(...)`
 * pops the string stack, and a value pushed to the int stack underflows and
 * aborts there. An int-stack comparison would never see it.
 *
 * The script is compiled here, from source written here, against symbols whose
 * ids come out of the decoded tables — so `t_struct` and `t_param_str` below
 * are test names for whatever the cache turned out to hold.
 */

#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_paramtable.h"

#include "ss_opcode.h"
#include "ssc.h"
#include "ssvm.h"

#include <rscache.h>

#include <datatypes/dat2_config_loc.h>
#include <datatypes/dat2_config_struct.h>

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

/* ------------------------------------------------------------------ */
/* Locating the cache and the content tree                             */
/* ------------------------------------------------------------------ */

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
/* Half 1 — every decoded row must come back out                       */
/* ------------------------------------------------------------------ */

/*
 * What the exhaustive walk found, so the checks can be stated over it and the
 * numbers printed. `unordered` is the count of records whose own param list is
 * not ascending by key — the population the qsort exists for.
 */
struct TableAudit
{
    const char* label;
    int records;
    int with_params;
    int rows;
    int string_rows;
    int long_rows;
    int multi;
    int unordered;
    int misses;
    int wrong_int;
    int wrong_string;
    /* An (owner, key) pair the group demonstrably does *not* hold, for the
     * absent-row check. Found rather than assumed: `first_owner` carries params
     * but not `absent_key`. */
    int first_owner;
    int absent_key;
};

typedef const struct ToriRSServerParamRow* (*LookupFn)(int owner, int key);

static void
audit_record(
    struct TableAudit* audit,
    LookupFn lookup,
    int owner,
    const struct RSCache_Params* params)
{
    int prev = -1;
    int out_of_order = 0;

    audit->records++;
    if( params->count > 0 )
        audit->with_params++;
    if( params->count >= 2 )
        audit->multi++;

    for( int i = 0; i < params->count; i++ )
    {
        const struct ToriRSServerParamRow* row;

        if( i > 0 && params->keys[i] <= prev )
            out_of_order = 1;
        prev = params->keys[i];

        if( !params->values[i] )
            continue;
        if( params->kinds[i] == RSCACHE_PARAM_LONG )
        {
            /* Deliberately dropped by the table, so it must NOT come back. */
            audit->long_rows++;
            if( lookup(owner, params->keys[i]) )
                audit->misses++;
            continue;
        }

        audit->rows++;
        row = lookup(owner, params->keys[i]);
        if( !row )
        {
            if( audit->misses < 5 )
                printf("    miss: %s %d has no param %d in the table\n", audit->label,
                       owner, params->keys[i]);
            audit->misses++;
            continue;
        }
        if( params->kinds[i] == RSCACHE_PARAM_STRING )
        {
            audit->string_rows++;
            if( !row->sval || strcmp(row->sval, (const char*)params->values[i]) != 0 )
                audit->wrong_string++;
        }
        else
        {
            if( row->sval || row->ival != *(const int*)params->values[i] )
                audit->wrong_int++;
        }
    }

    if( out_of_order )
        audit->unordered++;

    /* The first record that carries params supplies the absent-row probe: any
     * key it does not hold will do, and key 0 is only skipped if it holds it. */
    if( audit->first_owner < 0 && params->count > 0 )
    {
        int candidate = 0;

        for( ;; )
        {
            int taken = 0;

            for( int i = 0; i < params->count; i++ )
                if( params->keys[i] == candidate )
                    taken = 1;
            if( !taken )
                break;
            candidate++;
        }
        audit->first_owner = owner;
        audit->absent_key = candidate;
    }
}

static void
report_audit(const struct TableAudit* audit)
{
    printf("  %s: %d records, %d with params, %d rows (%d string, %d long dropped), "
           "%d with >=2 params, %d of those out of key order\n",
           audit->label, audit->records, audit->with_params, audit->rows,
           audit->string_rows, audit->long_rows, audit->multi, audit->unordered);

    CHECK_EQ(audit->misses, 0, "every decoded row is findable");
    CHECK_EQ(audit->wrong_int, 0, "every int row reads back its own value");
    CHECK_EQ(audit->wrong_string, 0, "every string row reads back its own text");
    /*
     * If this ever hits zero the sort has stopped being load-bearing and the
     * check above has stopped proving anything — say so rather than pass.
     */
    CHECK(audit->unordered > 0,
          "the cache really does write params out of key order (the sort is load-bearing)");
    CHECK(audit->first_owner >= 0 && audit->rows > 0, "the group carries params at all");
}

static struct RSCache_Dat2Disk*
open_cache(
    const char* cache_dir,
    struct RSCache* profile)
{
    struct RSCache_Dat2Disk* disk;

    *profile = RSCache_ProfileZero();
    profile->game = RSCACHE_GAME_OLDSCHOOL;
    profile->epoch = RSCACHE_EPOCH_DAT2;
    profile->revision = TORIRSSERVER_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( disk )
        RSCache_Dat2DiskSetProfile(disk, profile);
    return disk;
}

static void
audit_struct_table(const char* cache_dir)
{
    struct TableAudit audit;
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = open_cache(cache_dir, &profile);
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;

    memset(&audit, 0, sizeof(audit));
    audit.label = "struct";
    audit.first_owner = -1;

    if( !disk )
    {
        CHECK(0, "struct audit: cache is readable");
        return;
    }
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_STRUCT);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        CHECK(0, "struct audit: the struct config group is present");
        return;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    for( int i = 0; files && i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigStruct record;

        if( files->file_sizes[i] <= 0 )
            continue;
        memset(&record, 0, sizeof(record));
        record.id = archive->file_ids[i];
        RSCache_Dat2ConfigStructDecodeInplace(&record, files->files[i], files->file_sizes[i]);
        audit_record(&audit, ToriRSServer_StructParam, archive->file_ids[i], &record.params);
        RSCache_Dat2ConfigStructFreeInplace(&record);
    }

    CHECK_EQ(ToriRSServer_StructInfoCount(), archive->file_count,
             "the loader saw every struct record");
    CHECK_EQ(ToriRSServer_StructInfoParamCount(), audit.rows,
             "the loader retained exactly the rows the decoder produced");

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    report_audit(&audit);
    if( audit.first_owner >= 0 )
        CHECK(ToriRSServer_StructParam(audit.first_owner, audit.absent_key) == NULL,
              "a param the record does not carry reports absent, not a neighbouring row");
    CHECK(ToriRSServer_StructParam(-1, 0) == NULL, "a negative struct id is absent, not a crash");
}

static void
audit_loc_table(const char* cache_dir)
{
    struct TableAudit audit;
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = open_cache(cache_dir, &profile);
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;

    memset(&audit, 0, sizeof(audit));
    audit.label = "loc";
    audit.first_owner = -1;

    if( !disk )
    {
        CHECK(0, "loc audit: cache is readable");
        return;
    }
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        CHECK(0, "loc audit: the loc config group is present");
        return;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_LOC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    for( int i = 0; files && i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;

        if( files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(&profile, files->files[i],
                                                    files->file_sizes[i]);
        if( !loc )
            continue;
        audit_record(&audit, ToriRSServer_LocParam, archive->file_ids[i], &loc->params);
        RSCache_Dat2ConfigLocFree(loc);
    }

    CHECK_EQ(ToriRSServer_LocInfoCount(), archive->file_count, "the loader saw every loc record");
    CHECK_EQ(ToriRSServer_LocInfoParamCount(), audit.rows,
             "the loader retained exactly the rows the decoder produced");

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    report_audit(&audit);
    if( audit.first_owner >= 0 )
        CHECK(ToriRSServer_LocParam(audit.first_owner, audit.absent_key) == NULL,
              "a param the record does not carry reports absent, not a neighbouring row");
}

/* ------------------------------------------------------------------ */
/* Half 2 — the stack the result lands on                              */
/* ------------------------------------------------------------------ */

/*
 * The four cases the script exercises, each located in the decoded data rather
 * than written down. `param_default` is a param declaring a non-zero `default=`
 * that the chosen struct does *not* carry, which is the only way to tell the
 * declared default apart from a plain 0.
 */
struct Subjects
{
    int struct_id;
    int param_str; /* declared `s`, stored as a string */
    const char* param_str_value;
    /*
     * Declared int-ish but NOT plain `i`. The type char is the cache's own
     * code and only `s` is a string; `1` is a boolean, `o` an obj, `d` a
     * coordinate and so on, and every one of them shares the single int stack.
     * Insisting on a non-`i` subject is what makes "treat only type=i as an
     * int" a failing mutation instead of an invisible one.
     */
    int param_int;
    char param_int_type;
    int param_int_value;
    int param_default; /* declared default != 0, absent from struct_id */
    int param_default_value;

    int loc_id;
    int loc_param; /* a key that is out of order within its own record */
    int loc_param_value;
};

/* The first struct record that carries both a string param and an int param,
 * both of which `configs/all.param` types the same way the record stores them.
 * The two must come from one record so the script can address a single id. */
static int
find_struct_subjects(
    const char* cache_dir,
    struct Subjects* out)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = open_cache(cache_dir, &profile);
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int found = 0;

    if( !disk )
        return 0;
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_STRUCT);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);

    for( int i = 0; files && !found && i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigStruct record;
        int str_key = -1;
        int int_key = -1;
        char int_type = 0;
        const char* str_value = NULL;
        int int_value = 0;
        int last_key = -1;
        int out_of_order = 0;

        if( files->file_sizes[i] <= 0 )
            continue;
        memset(&record, 0, sizeof(record));
        record.id = archive->file_ids[i];
        RSCache_Dat2ConfigStructDecodeInplace(&record, files->files[i], files->file_sizes[i]);

        for( int p = 0; p < record.params.count; p++ )
        {
            int key = record.params.keys[p];
            char declared = ToriRSServer_ContentParamType(key);

            if( p > 0 && key <= last_key )
                out_of_order = 1;
            last_key = key;

            if( !record.params.values[p] )
                continue;
            if( record.params.kinds[p] == RSCACHE_PARAM_STRING && declared == 's' &&
                str_key < 0 )
            {
                str_key = key;
                str_value = (const char*)record.params.values[p];
            }
            else if( record.params.kinds[p] == RSCACHE_PARAM_INT && declared &&
                     declared != 's' && declared != 'i' && int_key < 0 )
            {
                int_key = key;
                int_type = declared;
                int_value = *(const int*)record.params.values[p];
            }
        }

        /*
         * Insisting the record is *also* out of key order means the VM half
         * is exercising the sorted path too, not only the routing.
         */
        if( str_key >= 0 && int_key >= 0 && out_of_order )
        {
            out->struct_id = archive->file_ids[i];
            out->param_str = str_key;
            out->param_str_value = strdup(str_value);
            out->param_int = int_key;
            out->param_int_type = int_type;
            out->param_int_value = int_value;
            found = 1;
        }
        RSCache_Dat2ConfigStructFreeInplace(&record);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return found;
}

/*
 * A param declaring a non-zero default that `struct_id` does not carry.
 *
 * Two passes for the same reason `param_int` above is fussy: the first prefers
 * a declared type that is int-ish but not plain `i`, so a handler that only
 * recognises `i` routes it wrongly and the check fails. The second pass accepts
 * any non-string type, because "no such param exists in this cache" must not
 * silently skip the default check.
 */
static int
find_absent_default(struct Subjects* out)
{
    for( int pass = 0; pass < 2; pass++ )
    {
        for( int param = 0; param < 65536; param++ )
        {
            char declared = ToriRSServer_ContentParamType(param);
            int declared_default = ToriRSServer_ContentParamDefault(param);

            if( declared_default == 0 || declared == 's' )
                continue;
            if( pass == 0 && (declared == 0 || declared == 'i') )
                continue;
            if( ToriRSServer_StructParam(out->struct_id, param) )
                continue;
            out->param_default = param;
            out->param_default_value = declared_default;
            return 1;
        }
    }
    return 0;
}

/** The first loc record whose params are out of key order, and the key that
 *  proves it — the one a construction-ordered table would miss. */
static int
find_loc_subject(
    const char* cache_dir,
    struct Subjects* out)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = open_cache(cache_dir, &profile);
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int found = 0;

    if( !disk )
        return 0;
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_LOC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);

    for( int i = 0; files && !found && i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;

        if( files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(&profile, files->files[i],
                                                    files->file_sizes[i]);
        if( !loc )
            continue;
        for( int p = 1; p < loc->params.count; p++ )
        {
            char declared = ToriRSServer_ContentParamType(loc->params.keys[p]);

            if( loc->params.keys[p] > loc->params.keys[p - 1] )
                continue;
            if( !loc->params.values[p] || loc->params.kinds[p] != RSCACHE_PARAM_INT )
                continue;
            if( declared == 's' )
                continue;
            out->loc_id = archive->file_ids[i];
            out->loc_param = loc->params.keys[p];
            out->loc_param_value = *(const int*)loc->params.values[p];
            found = 1;
            break;
        }
        RSCache_Dat2ConfigLocFree(loc);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return found;
}

/* ---- the VM fixture ---- */

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
 * `ToriRSServer_OpsParam` never touches `struct ToriRSServer`, which is what makes
 * this possible at all: the domain file is a host callback in miniature and
 * binds with a NULL user — no world, no socket, no pack. A script here that
 * reached any other host command would fail to find one, which is the right
 * blast radius for a test of two opcodes.
 */
static int
host_command(struct SSVM_State* state, int opcode, int dot)
{
    return ToriRSServer_OpsParam(state, opcode, dot);
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
    snprintf(fixture->dir, sizeof(fixture->dir), "param_test_%d", (int)getpid());
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
    /*
     * Test names for whatever the cache turned out to hold. The ids come from
     * the decode above, so nothing here is a constant copied between trees.
     */
    SSC_SymbolsAdd(&fixture->symbols, "t_struct", subjects->struct_id, SSC_SYM_STRUCT, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_param_str", subjects->param_str, SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_param_int", subjects->param_int, SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_param_absent", subjects->param_default,
                   SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_loc", subjects->loc_id, SSC_SYM_LOC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "t_param_loc", subjects->loc_param, SSC_SYM_PARAM, NULL);

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
 * Run `name` and report the top of one stack.
 *
 * `out_str` is the whole point of the exercise: a proc declared `(string)`
 * leaves its result on the *string* stack, and if `struct_param` pushed the
 * value onto the int stack instead then `def_string $s = ...` inside the script
 * has already underflowed and aborted before this returns. Reading the string
 * stack here is what makes that abort visible as a failed assertion rather than
 * as a stack that happens to hold something.
 *
 * The string is copied out before `SSVM_StateRelease`, which frees the state's
 * pool — every string the VM holds belongs to that pool.
 */
static int
run_stack(
    struct Fixture* fixture,
    const char* name,
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

static void
test_stack_routing(const char* cache_dir)
{
    struct Fixture fixture;
    struct Subjects subjects;
    int32_t result = 0;
    char source[2048];

    memset(&subjects, 0, sizeof(subjects));
    printf("stack routing\n");

    if( !find_struct_subjects(cache_dir, &subjects) )
    {
        CHECK(0, "found a struct carrying a declared-string and a declared-int param");
        return;
    }
    if( !find_absent_default(&subjects) )
    {
        CHECK(0, "found a param with a non-zero default the struct does not carry");
        return;
    }
    if( !find_loc_subject(cache_dir, &subjects) )
    {
        CHECK(0, "found a loc whose params are out of key order");
        return;
    }

    printf("  subjects: struct %d (string param %d, int param %d declared '%c', "
           "absent param %d declared '%c' default %d), loc %d param %d\n",
           subjects.struct_id, subjects.param_str, subjects.param_int,
           subjects.param_int_type, subjects.param_default,
           ToriRSServer_ContentParamType(subjects.param_default),
           subjects.param_default_value, subjects.loc_id, subjects.loc_param);
    CHECK(subjects.param_int_type != 'i' && subjects.param_int_type != 's',
          "the int subject is a non-`i` int-ish type, so `i`-only routing would fail");

    /*
     * `def_string $s = struct_param(...)` is the assertion. A result pushed to
     * the int stack leaves the string stack empty and the assignment aborts
     * there, inside the VM — the comparison below never gets the chance to be
     * wrong, which is why an int-stack-only test would prove nothing.
     */
    snprintf(source, sizeof(source),
             "[proc,t_read_string]()(string)\n"
             "def_string $s = struct_param(t_struct, t_param_str);\n"
             "return($s);\n"
             "\n"
             "[proc,t_read_int]()(int)\n"
             "def_int $i = struct_param(t_struct, t_param_int);\n"
             "return($i);\n"
             "\n"
             "[proc,t_read_absent]()(int)\n"
             "def_int $i = struct_param(t_struct, t_param_absent);\n"
             "return($i);\n"
             "\n"
             "[proc,t_read_loc]()(int)\n"
             "def_int $i = lc_param(t_loc, t_param_loc);\n"
             "return($i);\n");

    if( !fixture_compile(&fixture, &subjects, source) )
        return;

    {
        char* text = NULL;

        if( run_stack(&fixture, "[proc,t_read_string]", NULL, &text) )
            CHECK(text && strcmp(text, subjects.param_str_value) == 0,
                  "a declared-string param lands on the string stack with its own text");
        free(text);
    }

    if( run_stack(&fixture, "[proc,t_read_int]", &result, NULL) )
        CHECK_EQ(result, subjects.param_int_value,
                 "a declared int-ish param lands on the int stack");

    if( run_stack(&fixture, "[proc,t_read_absent]", &result, NULL) )
        CHECK_EQ(result, subjects.param_default_value,
                 "an absent param reads the declared default, not 0");

    if( run_stack(&fixture, "[proc,t_read_loc]", &result, NULL) )
        CHECK_EQ(result, subjects.loc_param_value,
                 "lc_param finds a key its record wrote out of order");

    fixture_close(&fixture);
    free((void*)subjects.param_str_value);
}

/* ------------------------------------------------------------------ */

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;

    cache_dir = resolve_dir(TORIRSSERVER_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs239-content", content_scratch,
                              sizeof(content_scratch));

    printf("param tables (%s)\n", cache_dir);

    /* The declared types and defaults, which decide the stack. Without them
     * every param would fall back to its stored kind and the routing half
     * would be testing nothing. */
    ToriRSServer_ContentLoad(content_dir);

    if( !ToriRSServer_StructInfoLoad(cache_dir) )
        CHECK(0, "struct params loaded");
    if( !ToriRSServer_LocInfoLoad(cache_dir) )
        CHECK(0, "loc params loaded");

    audit_struct_table(cache_dir);
    audit_loc_table(cache_dir);
    test_stack_routing(cache_dir);

    ToriRSServer_StructInfoFree();
    ToriRSServer_LocInfoFree();
    ToriRSServer_ContentFree();

    if( g_fail )
    {
        printf("param_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("param_test: all checks passed\n");
    return 0;
}
