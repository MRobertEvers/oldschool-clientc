/*
 * Run the magic book's sort out of a real cache, and count what it costs.
 *
 *   spellbook_bench <cache dir> [element count]
 *
 * `[proc,magic_spellbook_redraw]` (script 2611) lays the spellbook out and sorts
 * it with script 2621 — a quicksort that recurses once per partition and, per
 * the note on CS2VM_MAX_FRAMES, reaches depth 70 on the standard book. It is the
 * only deeply recursive script in this cache.
 *
 * It needed its own harness because the client cannot be made to run it: the
 * embedded mock server never opens the spellbook interface, so no sidebutton
 * lays it out and a headless boot measures a login screen. Here the script is
 * loaded straight out of table 12, handed an array and a range, and run — so
 * "before" and "after" are the same program from two caches, with nothing else
 * in the frame to hide behind.
 *
 * What is measured is executed opcodes and executed `gosub`s, both exact from
 * the VM's own instruction trace. Wall time is printed but is not the point;
 * the sort is microseconds and the machine is a laptop.
 */
#include "cs2vm2/cs2_opcode.h"
#include "engine/cs2_opcode_dialect.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cs2vm2_script_from_rscache.h"
#include "rscache.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SPELLBOOK_SORT 2621
#define TRACE_CAPACITY (4 * 1024 * 1024)

/*
 * The host, reduced to what the sort actually asks for.
 *
 * 2621 reads its comparison key through `enum`, so the answer decides the
 * sort's shape. Returning the key back makes the comparison the identity on the
 * element value, which is what gives a deterministic, reproducible number —
 * the real enum would make the result depend on cache contents that are not
 * what is being measured.
 */
static int
bench_host_pushscript(struct CS2VM2_Thread* vm, int script_id);

static int
bench_host(struct CS2VM2_Thread* vm, struct CS2VM_HostRequest* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        return CS2VM2_PushInt(vm, request->u.enum_lookup.key);
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return bench_host_pushscript(vm, request->u.push_script.script_id);
    default:
        /* Anything else is not part of the sort; answer zero and continue so a
         * stray op cannot end the run and silently shorten the measurement. */
        return CS2VM2_PushInt(vm, 0);
    }
}

static struct CS2VM2_Script*
load_from_cache(const char* cache_dir, int script_id);

/** Decode one clientscript container into a VM script. */
static struct CS2VM2_Script*
decode_script(const uint8_t* bytes, int size, int script_id, const struct RSCache* profile)
{
    struct RSCache_ClientScript* decoded = RSCache_ClientScriptNewFromDecodeFlags(
        script_id, bytes, size, RSCache_ClientScriptFlags(profile));
    if( !decoded )
        return NULL;
    struct CS2VM2_Script* script = (struct CS2VM2_Script*)calloc(1, sizeof(*script));
    CS2VM2_ScriptInit(script);
    if( !CS2VM2_ScriptFromRSCache(&decoded->script, script, CS2_OPCODE_DIALECT_CANONICAL) )
    {
        free(script);
        script = NULL;
    }
    RSCache_ClientScriptFree(decoded);
    return script;
}

/**
 * Load the script from a cache directory, or from a bare container file.
 *
 * The file form is what `cs2 optimize --dump` writes, and it is the only way to
 * get at the optimized version of some scripts: the packer declines 550 of this
 * tree's clientscripts for a param it cannot type, and 2621 is one of them, so
 * "after" cannot be read out of a packed cache.
 */
static struct CS2VM2_Script*
load_script(const char* source, int script_id)
{
    struct RSCache file_profile = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, RSCACHE_QUIRK_NONE);
    /* `fopen` on a directory succeeds on macOS and then reads nothing, so the
     * kind has to be asked for rather than inferred from the open. */
    struct stat info;
    if( stat(source, &info) == 0 && S_ISDIR(info.st_mode) )
        return load_from_cache(source, script_id);

    FILE* file = fopen(source, "rb");
    if( file )
    {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t* bytes = (uint8_t*)malloc((size_t)(size > 0 ? size : 1));
        size_t read = bytes ? fread(bytes, 1, (size_t)size, file) : 0;
        fclose(file);
        struct CS2VM2_Script* script =
            read == (size_t)size ? decode_script(bytes, (int)size, script_id, &file_profile)
                                 : NULL;
        free(bytes);
        if( !script )
            fprintf(stderr, "spellbook_bench: %s is not a clientscript\n", source);
        return script;
    }
    return load_from_cache(source, script_id);
}

static struct CS2VM2_Script*
load_from_cache(const char* cache_dir, int script_id)
{
    /* The identity is stated, never guessed — the same rule the library holds
     * to, and it decides the clientscript trailer's shape. */
    struct RSCache profile = RSCache_ProfileForIdentity(
        RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, RSCACHE_QUIRK_NONE);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "spellbook_bench: cannot open %s\n", cache_dir);
        return NULL;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, script_id);
    struct CS2VM2_Script* script = NULL;
    if( archive )
    {
        struct RSCache_ClientScript* decoded = RSCache_ClientScriptNewFromArchive(
            archive, script_id, RSCache_ClientScriptFlags(&profile));
        if( decoded )
        {
            script = (struct CS2VM2_Script*)calloc(1, sizeof(*script));
            CS2VM2_ScriptInit(script);
            if( !CS2VM2_ScriptFromRSCache(&decoded->script, script,
                                          CS2_OPCODE_DIALECT_CANONICAL) )
            {
                free(script);
                script = NULL;
            }
            RSCache_ClientScriptFree(decoded);
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }
    RSCache_Dat2DiskFree(disk);
    if( !script )
        fprintf(stderr, "spellbook_bench: script %d is not in %s\n", script_id, cache_dir);
    return script;
}

struct result
{
    long long opcodes;
    long long gosubs;
    long long max_depth;
    double micros;
    int ok;
};

/*
 * A caller for the sort, assembled by hand.
 *
 * There is no way in from outside: the array handle lives in a *string local*
 * of the calling frame and is passed as an ordinary string argument, so nothing
 * short of a real `gosub` can hand the sort an array. So this builds one —
 * define the array, fill it, push (lo, hi, key) and the handle, call.
 *
 * The fill is unrolled rather than looped because its cost does not matter: the
 * trace carries a script id per instruction, so everything below counts only
 * the records belonging to 2621 and the caller is invisible in the numbers.
 */
struct caller
{
    struct CS2VM2_Script script;
    int capacity;
};

static void
emit(struct CS2VM2_Script* script, int opcode, int operand)
{
    script->opcodes[script->op_count] = (uint16_t)opcode;
    script->int_operands[script->op_count] = operand;
    script->op_count++;
}

static void
build_caller(struct caller* out, int count, int sort_id)
{
    memset(out, 0, sizeof(*out));
    CS2VM2_ScriptInit(&out->script);
    out->capacity = count * 6 + 32;
    out->script.script_id = 9999;
    out->script.local_int_count = 2;
    out->script.local_string_count = 1;
    out->script.opcodes = (uint16_t*)calloc((size_t)out->capacity, sizeof(uint16_t));
    out->script.int_operands = (int*)calloc((size_t)out->capacity, sizeof(int));
    out->script.string_operands = (char**)calloc((size_t)out->capacity, sizeof(char*));

    /* `define_array` packs the receiving string local in the high half of its
     * operand and the element type letter in the low. */
    emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, count);
    emit(&out->script, CS2_OP_DEFINE_ARRAY, (0 << 16) | 'i');

    /* Descending values, so the partitioning has real work to do rather than
     * finding the array already in order. */
    for( int i = 0; i < count; i++ )
    {
        emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, i);
        emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, count - i);
        emit(&out->script, CS2_OP_POP_ARRAY_INT, 0);
    }

    emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, 0);         /* lo  */
    emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, count - 1); /* hi  */
    emit(&out->script, CS2_OP_PUSH_CONSTANT_INT, 0);         /* key */
    emit(&out->script, CS2_OP_PUSH_STRING_LOCAL, 0);         /* the handle */
    emit(&out->script, CS2_OP_GOSUB_WITH_PARAMS, sort_id);
    emit(&out->script, CS2_OP_RETURN, 0);
}

static void
free_caller(struct caller* out)
{
    free(out->script.opcodes);
    free(out->script.int_operands);
    free(out->script.string_operands);
}

/** The sort is the only script the caller ever asks for. */
static struct CS2VM2_Script* g_sort;

static int
bench_host_pushscript(struct CS2VM2_Thread* vm, int script_id)
{
    if( script_id != SPELLBOOK_SORT || !g_sort )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushCallScript(vm, g_sort);
}

static struct result
run_sort(struct CS2VM2_Script* sort, int count, struct CS2VM2_TraceRecord* trace)
{
    struct result out;
    memset(&out, 0, sizeof(out));

    struct caller caller;
    build_caller(&caller, count, SPELLBOOK_SORT);
    g_sort = sort;

    struct CS2VM2* vm = CS2VM2_Acquire();
    if( !vm )
    {
        free_caller(&caller);
        return out;
    }
    CS2VM2_BindHost(vm, NULL, bench_host);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(vm);
    CS2VM2_ResetRuntime(thread);
    if( CS2VM2_PushCallScript(thread, &caller.script) != CS2VM_EXECNO_OK )
    {
        CS2VM2_Release(vm);
        free_caller(&caller);
        return out;
    }

    CS2VM2_TraceCaptureBegin(trace, TRACE_CAPACITY);
    struct timespec started;
    struct timespec finished;
    clock_gettime(CLOCK_MONOTONIC, &started);
    int status = CS2VM2_RunScript(thread);
    clock_gettime(CLOCK_MONOTONIC, &finished);
    int captured = CS2VM2_TraceCaptureEnd();

    out.micros = (double)(finished.tv_sec - started.tv_sec) * 1e6 +
                 (double)(finished.tv_nsec - started.tv_nsec) / 1e3;
    /*
     * Only the sort's own instructions; the caller's fill is not the subject.
     *
     * Depth is tracked alongside because it is the number with a hard limit
     * behind it: `CS2VM_MAX_FRAMES` is 128, and the comment on it records that
     * this sort already reaches 70 on the standard spellbook. Halving the
     * recursion is worth more as headroom than as speed.
     */
    long long depth = 0;
    for( int i = 0; i < captured; i++ )
    {
        if( trace[i].opcode == CS2_OP_GOSUB_WITH_PARAMS )
            depth++;
        else if( trace[i].opcode == CS2_OP_RETURN )
            depth--;
        if( trace[i].script_id != SPELLBOOK_SORT )
            continue;
        out.opcodes++;
        out.gosubs += trace[i].opcode == CS2_OP_GOSUB_WITH_PARAMS;
        if( depth > out.max_depth )
            out.max_depth = depth;
    }
    out.ok = status != CS2VM_EXECNO_ERROR && out.opcodes > 0;

    CS2VM2_Release(vm);
    free_caller(&caller);
    return out;
}

int
main(int argc, char** argv)
{
    if( argc < 2 )
    {
        fprintf(stderr, "usage: spellbook_bench <cache dir | script file> [count]\n");
        return 2;
    }
    int count = argc > 2 ? atoi(argv[2]) : 60;

    struct CS2VM2_Script* script = load_script(argv[1], SPELLBOOK_SORT);
    if( !script )
        return 1;

    struct CS2VM2_TraceRecord* trace =
        (struct CS2VM2_TraceRecord*)malloc(TRACE_CAPACITY * sizeof(*trace));
    if( !trace )
    {
        fprintf(stderr, "spellbook_bench: out of memory for the trace\n");
        return 1;
    }

    /* One warm run, then the best of several — the trace buffer is large and
     * the first touch of it is a page-fault storm, not the VM. */
    struct result best;
    memset(&best, 0, sizeof(best));
    for( int attempt = 0; attempt < 6; attempt++ )
    {
        struct result one = run_sort(script, count, trace);
        if( !one.ok )
        {
            fprintf(stderr, "spellbook_bench: the sort did not complete\n");
            free(trace);
            return 1;
        }
        if( attempt == 0 )
            continue;
        if( best.opcodes == 0 || one.micros < best.micros )
            best = one;
    }

    printf("%-46s n=%-5d opcodes=%-8lld gosubs=%-5lld depth=%-4lld %6.1f us\n", argv[1],
           count, best.opcodes, best.gosubs, best.max_depth, best.micros);
    free(trace);
    CS2VM2_ScriptFree(script);
    free(script);
    CS2VM2_PoolDrain();
    return 0;
}
