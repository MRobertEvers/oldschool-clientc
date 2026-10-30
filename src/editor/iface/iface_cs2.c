#include "editor/iface/iface_cs2.h"

#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define EDITOR_CS2_CAN_SPAWN 1
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#else
#define EDITOR_CS2_CAN_SPAWN 0
#endif

/* ---- compile ------------------------------------------------------------- */

void
EditorCs2_CompileTakeLine(
    struct EditorCs2CompileResult* result,
    const char* line)
{
    int compiled = 0;
    int failed = 0;

    assert(result);
    assert(line);

    if( sscanf(line, "compiled %d, failed %d", &compiled, &failed) == 2 )
    {
        result->compiled = compiled;
        result->failed = failed;
    }
    if( result->line_count >= EDITOR_CS2_COMPILE_LINES )
        return;
    snprintf(result->lines[result->line_count], EDITOR_CS2_COMPILE_LINE_MAX, "%s", line);
    if( strncmp(line, "FAIL ", 5) == 0 )
        result->declines[result->decline_count++] = result->line_count;
    result->line_count++;
}

#if EDITOR_CS2_CAN_SPAWN
static int
make_dir(const char* path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

/** Remove every `.cs2` in `dir`: the compiler takes the whole directory. */
static void
clear_sources(const char* dir)
{
    DIR* listing = opendir(dir);
    struct dirent* entry;

    while( listing && (entry = readdir(listing)) )
    {
        size_t n = strlen(entry->d_name);
        char path[2048];
        if( n < 5 || strcmp(entry->d_name + n - 4, ".cs2") != 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        unlink(path);
    }
    if( listing )
        closedir(listing);
}
#endif

enum EditorCs2CompileStatus
EditorCs2_Compile(
    const struct EditorCs2CompileJob* job,
    struct EditorCs2CompileResult* result)
{
    assert(job);
    assert(result);
    assert(job->cs2_bin);
    assert(job->cache_dir);
    assert(job->work_dir);
    assert(job->script_name);
    assert(job->source || job->source_length == 0);

    memset(result, 0, sizeof(*result));
    result->script_id = -1;
    result->status = EDITOR_CS2_NOT_RUN;
    {
        const char* p = job->source;
        size_t i = 0;
        while( i < job->source_length && (p[i] == ' ' || p[i] == '\n' || p[i] == '\r') )
            i++;
        if( i + 2 < job->source_length && p[i] == '/' && p[i + 1] == '/' )
            result->script_id = atoi(p + i + 2);
    }

#if EDITOR_CS2_CAN_SPAWN
    {
        char src_dir[1200];
        char out_dir[1200];
        char path[1400];
        FILE* f;
        int pipe_fd[2];
        posix_spawn_file_actions_t actions;
        pid_t pid;
        const char* argv[16];
        int argc = 0;
        char pending[EDITOR_CS2_COMPILE_LINE_MAX];
        size_t pending_length = 0;
        int status = 0;

        if( access(job->cs2_bin, X_OK) != 0 )
        {
            EditorCs2_CompileTakeLine(result, "cs2: the compiler binary is not built");
            return result->status;
        }
        snprintf(src_dir, sizeof(src_dir), "%s/src", job->work_dir);
        snprintf(out_dir, sizeof(out_dir), "%s/out", job->work_dir);
        if( !make_dir(job->work_dir) || !make_dir(src_dir) || !make_dir(out_dir) )
        {
            EditorCs2_CompileTakeLine(result, "cs2: cannot make the scratch directory");
            return result->status;
        }
        clear_sources(src_dir);
        snprintf(path, sizeof(path), "%s/%s.cs2", src_dir, job->script_name);
        f = fopen(path, "wb");
        if( !f || fwrite(job->source, 1, job->source_length, f) != job->source_length )
        {
            if( f )
                fclose(f);
            EditorCs2_CompileTakeLine(result, "cs2: cannot write the source");
            return result->status;
        }
        fclose(f);

        argv[argc++] = job->cs2_bin;
        argv[argc++] = "compile";
        argv[argc++] = "--src";
        argv[argc++] = src_dir;
        argv[argc++] = "--cache";
        argv[argc++] = job->cache_dir;
        argv[argc++] = "--rev";
        argv[argc++] = job->revision ? job->revision : "osrs241";
        if( job->names_dir )
        {
            argv[argc++] = "--names";
            argv[argc++] = job->names_dir;
        }
        argv[argc++] = "--out";
        argv[argc++] = out_dir;
        argv[argc] = NULL;

        if( pipe(pipe_fd) != 0 )
        {
            EditorCs2_CompileTakeLine(result, "cs2: no pipe");
            return result->status;
        }
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], 1);
        posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], 2);
        posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
        if( posix_spawn(&pid, job->cs2_bin, &actions, NULL, (char* const*)argv, environ) != 0 )
        {
            posix_spawn_file_actions_destroy(&actions);
            close(pipe_fd[0]);
            close(pipe_fd[1]);
            EditorCs2_CompileTakeLine(result, "cs2: could not start the compiler");
            return result->status;
        }
        posix_spawn_file_actions_destroy(&actions);
        close(pipe_fd[1]);
        for( ;; )
        {
            char chunk[1024];
            ssize_t got = read(pipe_fd[0], chunk, sizeof(chunk));
            if( got < 0 && errno == EINTR )
                continue;
            if( got <= 0 )
                break;
            for( ssize_t i = 0; i < got; i++ )
            {
                if( chunk[i] == '\n' || pending_length + 1 >= sizeof(pending) )
                {
                    pending[pending_length] = '\0';
                    EditorCs2_CompileTakeLine(result, pending);
                    pending_length = 0;
                    if( chunk[i] == '\n' )
                        continue;
                }
                pending[pending_length++] = chunk[i];
            }
        }
        if( pending_length )
        {
            pending[pending_length] = '\0';
            EditorCs2_CompileTakeLine(result, pending);
        }
        close(pipe_fd[0]);
        while( waitpid(pid, &status, 0) < 0 && errno == EINTR )
            ;
        result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if( result->exit_code == 0 && result->failed == 0 && result->compiled > 0
            && result->decline_count == 0 )
        {
            result->status = EDITOR_CS2_COMPILED;
            if( result->script_id >= 0 )
                snprintf(result->output_path, sizeof(result->output_path), "%s/%d", out_dir, result->script_id);
        }
        else
            result->status = EDITOR_CS2_DECLINED;
    }
#else
    EditorCs2_CompileTakeLine(result, "cs2: compiling needs a desktop build (no process spawn here)");
#endif
    return result->status;
}

/* ---- trace --------------------------------------------------------------- */

void
EditorCs2Trace_Init(
    struct EditorCs2Trace* trace,
    int capacity)
{
    assert(trace);
    assert(capacity > 0);
    memset(trace, 0, sizeof(*trace));
    trace->records = ToriMem_Calloc(ToriMem_Editor(), (size_t)capacity, sizeof(*trace->records));
    assert(trace->records);
    trace->capacity = capacity;
}

void
EditorCs2Trace_Free(struct EditorCs2Trace* trace)
{
    if( !trace )
        return;
    if( trace->capturing )
        CS2VM2_TraceCaptureEnd();
    ToriMem_Free(ToriMem_Editor(), trace->records);
    memset(trace, 0, sizeof(*trace));
}

void
EditorCs2Trace_Begin(struct EditorCs2Trace* trace)
{
    assert(trace);
    assert(trace->records);
    trace->count = 0;
    trace->capturing = 1;
    CS2VM2_TraceCaptureBegin(trace->records, trace->capacity);
}

int
EditorCs2Trace_End(struct EditorCs2Trace* trace)
{
    assert(trace);
    if( !trace->capturing )
        return trace->count;
    trace->count = CS2VM2_TraceCaptureEnd();
    trace->capturing = 0;
    return trace->count;
}

void
EditorCs2Trace_Format(
    const struct EditorCs2Trace* trace,
    int index,
    char* out,
    size_t out_size)
{
    const struct CS2VM2_TraceRecord* record;
    const char* name;

    assert(trace);
    assert(out);
    assert(index >= 0);
    assert(index < trace->count);

    record = &trace->records[index];
    name = CS2_OpCode_String(record->opcode);
    snprintf(out, out_size, "script %d pc %d %s(%d) %d  istack %d sstack %d top %d", record->script_id,
             record->pc, name ? name : "_unknown", record->opcode, record->operand, record->ints_top,
             record->strs_top, record->top_int);
}

/* ---- watch --------------------------------------------------------------- */

int
EditorCs2Watch_Parse(
    const char* text,
    int* out_kind,
    int* out_id)
{
    static const struct
    {
        const char* word;
        int kind;
    } words[] = {
        { "varcstr", EDITOR_CS2_WATCH_VARCSTR }, { "vcs", EDITOR_CS2_WATCH_VARCSTR },
        { "varbit", EDITOR_CS2_WATCH_VARBIT },   { "vb", EDITOR_CS2_WATCH_VARBIT },
        { "varp", EDITOR_CS2_WATCH_VARP },       { "vp", EDITOR_CS2_WATCH_VARP },
        { "varc", EDITOR_CS2_WATCH_VARC },       { "vc", EDITOR_CS2_WATCH_VARC },
    };
    char word[16];
    int id;
    char tail;

    assert(text);
    assert(out_kind);
    assert(out_id);

    if( sscanf(text, " %15s %d %c", word, &id, &tail) != 2 || id < 0 )
        return 0;
    for( size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++ )
    {
        if( strcmp(word, words[i].word) == 0 )
        {
            *out_kind = words[i].kind;
            *out_id = id;
            return 1;
        }
    }
    return 0;
}

int
EditorCs2Watch_Add(
    struct EditorCs2Watch* watch,
    int kind,
    int id)
{
    struct EditorCs2WatchRow* row;

    assert(watch);
    if( watch->count >= EDITOR_CS2_WATCH_MAX )
        return -1;
    for( int i = 0; i < watch->count; i++ )
    {
        if( watch->rows[i].kind == kind && watch->rows[i].id == id )
            return -1;
    }
    row = &watch->rows[watch->count];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    row->id = id;
    row->age = -1;
    return watch->count++;
}

void
EditorCs2Watch_Remove(
    struct EditorCs2Watch* watch,
    int index)
{
    assert(watch);
    if( index < 0 || index >= watch->count )
        return;
    memmove(&watch->rows[index], &watch->rows[index + 1],
            sizeof(watch->rows[0]) * (size_t)(watch->count - index - 1));
    watch->count--;
}

void
EditorCs2Watch_Set(
    struct EditorCs2Watch* watch,
    int index,
    int value,
    const char* text)
{
    struct EditorCs2WatchRow* row;
    int changed;

    assert(watch);
    assert(index >= 0);
    assert(index < watch->count);

    row = &watch->rows[index];
    changed = row->age < 0 || row->value != value || (text && strcmp(row->text, text) != 0);
    row->value = value;
    if( text )
        snprintf(row->text, sizeof(row->text), "%s", text);
    row->age = changed ? 0 : row->age + 1;
}

void
EditorCs2Watch_Format(
    const struct EditorCs2Watch* watch,
    int index,
    char* out,
    size_t out_size)
{
    static const char* const kinds[] = { "varp", "varbit", "varc", "varcstr" };
    const struct EditorCs2WatchRow* row;

    assert(watch);
    assert(out);
    assert(index >= 0);
    assert(index < watch->count);

    row = &watch->rows[index];
    if( row->kind == EDITOR_CS2_WATCH_VARCSTR )
        snprintf(out, out_size, "%s %d = \"%s\"%s", kinds[row->kind], row->id, row->text,
                 row->age == 0 ? "  *" : "");
    else
        snprintf(out, out_size, "%s %d = %d%s", kinds[row->kind], row->id, row->value,
                 row->age == 0 ? "  *" : "");
}

/* ---- the CODE editor ----------------------------------------------------- */

void
EditorCs2Code_Init(struct EditorCs2Code* code)
{
    assert(code);
    memset(code, 0, sizeof(*code));
}

void
EditorCs2Code_Free(struct EditorCs2Code* code)
{
    if( !code )
        return;
    ToriMem_Free(ToriMem_Editor(), code->starts);
    memset(code, 0, sizeof(*code));
}

void
EditorCs2Code_Sync(
    struct EditorCs2Code* code,
    const char* doc_key,
    const char* text,
    size_t length)
{
    int lines = 1;

    assert(code);
    assert(doc_key);
    assert(text || length == 0);

    snprintf(code->doc_key, sizeof(code->doc_key), "%s", doc_key);
    for( size_t i = 0; i < length; i++ )
        lines += text[i] == '\n';
    /* A text ending in '\n' has no line after it. */
    if( length > 0 && text[length - 1] == '\n' )
        lines--;
    if( length == 0 )
        lines = 1;
    if( lines + 1 > code->capacity )
    {
        code->capacity = lines + 1;
        code->starts = ToriMem_Realloc(ToriMem_Editor(), code->starts, sizeof(size_t) * (size_t)code->capacity);
        assert(code->starts);
    }
    code->line_count = 0;
    code->starts[code->line_count++] = 0;
    for( size_t i = 0; i < length; i++ )
    {
        if( text[i] == '\n' && i + 1 < length )
            code->starts[code->line_count++] = i + 1;
    }
    code->starts[code->line_count] = length;
}

/** [start, content_end) of line `line`, and where its line ending ends. */
static int
line_extent(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    size_t* out_start,
    size_t* out_content_end,
    size_t* out_end)
{
    size_t start;
    size_t end;
    size_t content;

    if( line < 0 || line >= code->line_count )
        return 0;
    start = code->starts[line];
    end = code->starts[line + 1];
    content = end;
    if( content > start && text[content - 1] == '\n' )
        content--;
    if( content > start && text[content - 1] == '\r' )
        content--;
    *out_start = start;
    *out_content_end = content;
    *out_end = end;
    return 1;
}

void
EditorCs2Code_Line(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    char* out,
    size_t out_size)
{
    size_t start;
    size_t content;
    size_t end;

    assert(code);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !line_extent(code, text, line, &start, &content, &end) )
        return;
    snprintf(out, out_size, "%.*s", (int)(content - start), text + start);
}

static int
plan_splice(
    const struct EditorCs2Code* code,
    size_t offset,
    const char* before,
    size_t before_length,
    const char* after,
    size_t after_length,
    struct Editor_Cmd* out)
{
    if( before_length + after_length > EDITOR_CMD_SPLICE_MAX || offset > 0xFFFFFFFFu )
        return 0;
    if( before_length == after_length && memcmp(before, after, before_length) == 0 )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_TEXT, code->doc_key, EDITOR_CMD_TEXT_SPLICE);
    out->has_before = 1;
    out->has_after = 1;
    out->u.text_splice.offset = (uint32_t)offset;
    out->u.text_splice.before_len = (uint32_t)before_length;
    out->u.text_splice.after_len = (uint32_t)after_length;
    memcpy(out->u.text_splice.text, before, before_length);
    memcpy(out->u.text_splice.text + before_length, after, after_length);
    return 1;
}

int
EditorCs2Code_PlanReplace(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    const char* new_text,
    struct Editor_Cmd* out)
{
    size_t start;
    size_t content;
    size_t end;

    assert(code);
    assert(new_text);
    assert(out);
    if( !line_extent(code, text, line, &start, &content, &end) || strchr(new_text, '\n') )
        return 0;
    return plan_splice(code, start, text + start, content - start, new_text, strlen(new_text), out);
}

int
EditorCs2Code_PlanSplit(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    int col,
    struct Editor_Cmd* out)
{
    size_t start;
    size_t content;
    size_t end;

    assert(code);
    assert(out);
    if( !line_extent(code, text, line, &start, &content, &end) || col < 0
        || (size_t)col > content - start )
        return 0;
    return plan_splice(code, start + (size_t)col, "", 0, "\n", 1, out);
}

int
EditorCs2Code_PlanJoin(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    struct Editor_Cmd* out)
{
    size_t start;
    size_t content;
    size_t end;

    assert(code);
    assert(out);
    if( !line_extent(code, text, line, &start, &content, &end) || line + 1 >= code->line_count
        || end == content )
        return 0;
    return plan_splice(code, content, text + content, end - content, "", 0, out);
}

static int
is_word(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == ':';
}

int
EditorCs2_Highlight(
    const char* line,
    struct EditorCs2Span* out,
    int max)
{
    static const char* const keywords[] = {
        "if",      "else",       "while",   "return",  "switch_int", "switch_string", "case",
        "default", "def_int",    "def_string", "def_boolean", "def_long", "true", "false",
        "calc",    "null",       "for",     "def_component", "def_obj", "def_coord",
    };
    int written = 0;
    int i = 0;
    int length;

    assert(line);
    assert(out || max == 0);

    length = (int)strlen(line);
    if( line[0] == '[' )
    {
        const char* close = strchr(line, ']');
        if( close && written < max )
            out[written++] = (struct EditorCs2Span){ 0, (int)(close - line) + 1, EDITOR_CS2_TOKEN_HEADER };
        i = close ? (int)(close - line) + 1 : length;
    }
    while( i < length && written < max )
    {
        char c = line[i];
        int start = i;

        if( c == '/' && line[i + 1] == '/' )
        {
            out[written++] = (struct EditorCs2Span){ i, length - i, EDITOR_CS2_TOKEN_COMMENT };
            break;
        }
        if( c == '"' )
        {
            i++;
            while( i < length && line[i] != '"' )
            {
                if( line[i] == '\\' && i + 1 < length )
                    i++;
                i++;
            }
            if( i < length )
                i++;
            out[written++] = (struct EditorCs2Span){ start, i - start, EDITOR_CS2_TOKEN_STRING };
            continue;
        }
        if( c == '$' || c == '%' || c == '~' || c == '^' || c == '@' )
        {
            i++;
            while( i < length && is_word(line[i]) )
                i++;
            out[written++] = (struct EditorCs2Span){
                start, i - start,
                c == '$'   ? EDITOR_CS2_TOKEN_LOCAL
                : c == '%' ? EDITOR_CS2_TOKEN_VAR
                           : EDITOR_CS2_TOKEN_CALL
            };
            continue;
        }
        if( isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)line[i + 1])
                                          && (i == 0 || !is_word(line[i - 1]))) )
        {
            i++;
            while( i < length && (isalnum((unsigned char)line[i])) )
                i++;
            out[written++] = (struct EditorCs2Span){ start, i - start, EDITOR_CS2_TOKEN_NUMBER };
            continue;
        }
        if( isalpha((unsigned char)c) || c == '_' )
        {
            while( i < length && (isalnum((unsigned char)line[i]) || line[i] == '_') )
                i++;
            for( size_t k = 0; k < sizeof(keywords) / sizeof(keywords[0]); k++ )
            {
                if( strlen(keywords[k]) == (size_t)(i - start)
                    && memcmp(keywords[k], line + start, (size_t)(i - start)) == 0 )
                {
                    out[written++] = (struct EditorCs2Span){ start, i - start, EDITOR_CS2_TOKEN_KEYWORD };
                    break;
                }
            }
            continue;
        }
        i++;
    }
    return written;
}

/* ---- harness ------------------------------------------------------------- */

static void
json_string(
    FILE* f,
    const char* text)
{
    fputc('"', f);
    for( const char* c = text; *c; c++ )
    {
        if( *c == '"' || *c == '\\' )
            fprintf(f, "\\%c", *c);
        else if( (unsigned char)*c < 0x20 )
            fprintf(f, "\\u%04x", (unsigned char)*c);
        else
            fputc(*c, f);
    }
    fputc('"', f);
}

int
EditorCs2_WriteHarnessCase(
    const char* path,
    const char* case_id,
    int script_id,
    const int* int_args,
    int int_arg_count,
    const char* const* string_args,
    int string_arg_count,
    int screenshot)
{
    FILE* f;

    assert(path);
    assert(case_id);
    assert(int_args || int_arg_count == 0);
    assert(string_args || string_arg_count == 0);

    f = fopen(path, "wb");
    if( !f )
        return 0;
    fprintf(f, "{\n \"version\": 1,\n \"description\": \"Written by the interface editor's CS2 runner.\",\n");
    fprintf(f, " \"cases\": [\n  {\"id\": ");
    json_string(f, case_id);
    fprintf(f, ", \"scriptId\": %d, \"intArgs\": [", script_id);
    for( int i = 0; i < int_arg_count; i++ )
        fprintf(f, i ? ", %d" : "%d", int_args[i]);
    fprintf(f, "], \"stringArgs\": [");
    for( int i = 0; i < string_arg_count; i++ )
    {
        if( i )
            fprintf(f, ", ");
        json_string(f, string_args[i] ? string_args[i] : "");
    }
    fprintf(f, "]");
    if( screenshot )
        fprintf(f, ", \"screenshot\": true, \"settleMs\": 600");
    fprintf(f, "}\n ]\n}\n");
    return fclose(f) == 0;
}
