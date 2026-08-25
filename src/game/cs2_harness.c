/*
 * The C half of the CS2 cross-client harness.
 *
 * Runs the same case list the official client runs
 * (Deobfuscator/instr/src/CS2Harness.java) and writes the same artifact per
 * case, so `tools/perf/cs2_trace_diff.py` can put the two instruction streams
 * side by side.
 *
 * Why a driven harness rather than comparing two clients playing the game: two
 * clients at the same point in the same content do not run the same scripts,
 * and the ones they share are `gosub`'d from different callers, so their
 * records interleave with a caller's frame and cannot be aligned by script id.
 * Measured on this tree: of 31 scripts the C client traced through login and 62
 * the official client traced, exactly one was common — and it began at pc 0
 * with eleven values already on the stack on one side and pc 64 on the other.
 * A case file removes all of that: same script, same arguments, same entry.
 *
 * Enabled by TORIRS_CS2_HARNESS=<cases.json>, output directory
 * TORIRS_CS2_HARNESS_OUT (default /tmp/cs2_harness_c). Nothing here runs
 * otherwise.
 */

#include "game/cs2_harness.h"
#include <assert.h>

#include "cs2vm2/cs2vm2.h"
#include "game/rs_cs2_dispatch.h"
#include "task_runner.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_MAX_CASES 4096
#define HARNESS_MAX_ARGS 32
#define HARNESS_TRACE_MAX 200000

struct HarnessCase
{
    char id[128];
    int script_id;
    int screenshot;
    int int_args[HARNESS_MAX_ARGS];
    int int_arg_count;
    char* str_args[HARNESS_MAX_ARGS];
    int str_arg_count;
};

/* Heap, not .bss: at HARNESS_TRACE_MAX x sizeof(CS2VM2_TraceRecord) this is
 * 5.34 MB of private commit charged to every client start, and the harness
 * only runs when TORIRS_CS2_HARNESS is set.  Owned by CS2Harness_Run, the
 * only path that can reach harness_dump_case. */
static struct CS2VM2_TraceRecord* s_trace;

/* ---------------------------------------------------------------------
 * Case file
 *
 * The same JSON subset the Java side accepts — objects, arrays, strings and
 * integers — parsed by hand on both sides so the two harnesses cannot disagree
 * about the case list because of a library difference.
 * ------------------------------------------------------------------ */

static char const*
harness_skip_ws(char const* p)
{
    while( *p && isspace((unsigned char)*p) )
        p++;
    return p;
}

/** The value text of `"name":` inside one object body, or NULL. */
static char const*
harness_field(char const* body, char const* end, char const* name, int* out_len)
{
    size_t name_len = strlen(name);
    for( char const* p = body; p + name_len + 2 < end; p++ )
    {
        if( *p != '"' || strncmp(p + 1, name, name_len) != 0 || p[1 + name_len] != '"' )
            continue;
        char const* q = harness_skip_ws(p + 2 + name_len);
        if( *q != ':' )
            continue;
        q = harness_skip_ws(q + 1);
        char const* start = q;
        int depth = 0;
        int in_string = 0;
        for( ; q < end; q++ )
        {
            if( in_string )
            {
                if( *q == '\\' )
                    q++;
                else if( *q == '"' )
                    in_string = 0;
                continue;
            }
            if( *q == '"' )
                in_string = 1;
            else if( *q == '[' || *q == '{' )
                depth++;
            else if( *q == ']' || *q == '}' )
            {
                if( depth == 0 )
                    break;
                depth--;
            }
            else if( *q == ',' && depth == 0 )
                break;
        }
        *out_len = (int)(q - start);
        return start;
    }
    return NULL;
}

static int
harness_parse_case(char const* body, char const* end, struct HarnessCase* out)
{
    memset(out, 0, sizeof(*out));
    out->script_id = -1;

    int len = 0;
    char const* field = harness_field(body, end, "scriptId", &len);
    if( field )
        out->script_id = (int)strtol(field, NULL, 10);

    field = harness_field(body, end, "id", &len);
    if( field && len >= 2 && field[0] == '"' )
    {
        int copy = len - 2;
        if( copy > (int)sizeof(out->id) - 1 )
            copy = (int)sizeof(out->id) - 1;
        memcpy(out->id, field + 1, (size_t)copy);
        out->id[copy] = '\0';
    }
    else
    {
        snprintf(out->id, sizeof(out->id), "script%d", out->script_id);
    }

    field = harness_field(body, end, "screenshot", &len);
    if( field && len >= 4 && strncmp(field, "true", 4) == 0 )
        out->screenshot = 1;

    field = harness_field(body, end, "intArgs", &len);
    if( field )
    {
        char const* p = field;
        char const* stop = field + len;
        while( p < stop && out->int_arg_count < HARNESS_MAX_ARGS )
        {
            while( p < stop && *p != '-' && !isdigit((unsigned char)*p) )
                p++;
            if( p >= stop )
                break;
            char* next = NULL;
            long value = strtol(p, &next, 10);
            if( next == p )
                break;
            out->int_args[out->int_arg_count++] = (int)value;
            p = next;
        }
    }

    field = harness_field(body, end, "stringArgs", &len);
    if( field )
    {
        char const* p = field;
        char const* stop = field + len;
        while( p < stop && out->str_arg_count < HARNESS_MAX_ARGS )
        {
            while( p < stop && *p != '"' )
                p++;
            if( p >= stop )
                break;
            p++;
            char const* text = p;
            while( p < stop && *p != '"' )
            {
                if( *p == '\\' && p + 1 < stop )
                    p++;
                p++;
            }
            int copy = (int)(p - text);
            char* dup = (char*)malloc((size_t)copy + 1);
            assert(dup);
            memcpy(dup, text, (size_t)copy);
            dup[copy] = '\0';
            out->str_args[out->str_arg_count++] = dup;
            if( p < stop )
                p++;
        }
    }
    return out->script_id >= 0;
}

static int
harness_load_cases(char const* path, struct HarnessCase* cases, int max_cases)
{
    FILE* fp = fopen(path, "rb");
    if( !fp )
    {
        fprintf(stderr, "cs2_harness: cannot open %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    assert(text);
    size_t read = fread(text, 1, (size_t)size, fp);
    text[read] = '\0';
    fclose(fp);

    int count = 0;
    char const* p = strstr(text, "\"cases\"");
    if( p )
    {
        char const* end = text + read;
        while( count < max_cases )
        {
            char const* start = strchr(p, '{');
            if( !start )
                break;
            int depth = 0;
            int in_string = 0;
            char const* q = start;
            for( ; q < end; q++ )
            {
                if( in_string )
                {
                    if( *q == '\\' )
                        q++;
                    else if( *q == '"' )
                        in_string = 0;
                    continue;
                }
                if( *q == '"' )
                    in_string = 1;
                else if( *q == '{' )
                    depth++;
                else if( *q == '}' && --depth == 0 )
                    break;
            }
            if( q >= end )
                break;
            if( harness_parse_case(start, q, &cases[count]) )
                count++;
            p = q + 1;
        }
    }
    free(text);
    return count;
}

/* ---------------------------------------------------------------------
 * Artifact
 * ------------------------------------------------------------------ */

static void
harness_write(
    char const* dir,
    struct HarnessCase const* item,
    int status,
    int steps)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/cs2_%s.json", dir, item->id);
    FILE* fp = fopen(path, "w");
    if( !fp )
    {
        fprintf(stderr, "cs2_harness: cannot write %s\n", path);
        return;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"caseId\": \"%s\",\n", item->id);
    fprintf(fp, "  \"scriptId\": %d,\n", item->script_id);
    fprintf(fp, "  \"status\": %d,\n", status);
    fprintf(fp, "  \"opcount\": %d,\n", steps);
    fprintf(fp, "  \"trace\": [\n");
    for( int i = 0; i < steps; i++ )
    {
        fprintf(
            fp,
            "    {\"step\": %d, \"scriptId\": %d, \"pc\": %d, \"opcode\": %d, "
            "\"operand\": %d, \"intSp\": %d, \"strSp\": %d, \"topInt\": %d}%s\n",
            i,
            s_trace[i].script_id,
            s_trace[i].pc,
            s_trace[i].opcode,
            s_trace[i].operand,
            s_trace[i].ints_top,
            s_trace[i].strs_top,
            s_trace[i].top_int,
            i + 1 < steps ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
}

/* ---------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------ */

int
CS2Harness_Run(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    char const* cases_path,
    char const* out_dir,
    CS2Harness_Shot_Fn shot,
    void* shot_user)
{
    /* calloc, not malloc: these replace zero-initialised .bss, and
     * harness_load_cases only fills the cases it actually parses. */
    struct HarnessCase* cases = calloc(HARNESS_MAX_CASES, sizeof(*cases));
    assert(cases);
    s_trace = calloc(HARNESS_TRACE_MAX, sizeof(*s_trace));
    assert(s_trace);

    int count = harness_load_cases(cases_path, cases, HARNESS_MAX_CASES);
    if( count <= 0 )
    {
        free(cases);
        free(s_trace);
        s_trace = NULL;
        return 0;
    }

    fprintf(stderr, "cs2_harness: %d case(s) from %s -> %s\n", count, cases_path, out_dir);

    for( int i = 0; i < count; i++ )
    {
        struct HarnessCase* item = &cases[i];

        /* The string-argument encoding the dispatch path already uses: bit i of
         * the mask marks position i as a string. The harness keeps ints and
         * strings in separate sequences, which is how the official client's
         * interpreter files them onto locals, so the mask marks the tail. */
        uint64_t str_mask = 0;
        char const* str_args[HARNESS_MAX_ARGS];
        for( int s = 0; s < item->str_arg_count && s < 64; s++ )
        {
            str_mask |= (uint64_t)1 << (item->int_arg_count + s);
            str_args[s] = item->str_args[s];
        }

        CS2VM2_TraceCaptureBegin(s_trace, HARNESS_TRACE_MAX);
        RS_CS2_RunScript(
            host,
            runner,
            item->script_id,
            item->int_arg_count > 0 ? item->int_args : NULL,
            item->int_arg_count + item->str_arg_count,
            str_mask,
            item->str_arg_count > 0 ? str_args : NULL,
            item->str_arg_count);
        /* RS_CS2_RunScript queues rather than runs: a script has to be read
         * out of the cache first, and the whole client is protothreaded around
         * that IO. Ending the capture straight after the call therefore records
         * nothing at all — which is exactly what the first run of this harness
         * produced, 46 artifacts with zero instructions each. Drain the runner
         * so the script has actually executed before the trace is taken. */
        TaskRunner_Drain(runner);
        int steps = CS2VM2_TraceCaptureEnd();

        harness_write(out_dir, item, 0, steps);

        /* The picture, when the case asked for one. A trace proves the two VMs
         * ran the same instructions; only the frame shows whether the interface
         * they built looks the same. */
        if( item->screenshot && shot )
        {
            char shot_path[512];
            snprintf(shot_path, sizeof(shot_path), "%s/cs2_%s.bmp", out_dir, item->id);
            shot(shot_user, shot_path);
        }

        for( int s = 0; s < item->str_arg_count; s++ )
        {
            free(item->str_args[s]);
            item->str_args[s] = NULL;
        }
    }
    fprintf(stderr, "cs2_harness: done\n");
    free(cases);
    free(s_trace);
    s_trace = NULL;
    return count;
}
