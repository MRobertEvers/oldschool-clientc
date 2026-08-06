#include "cp_text.h"
#include "tool_posix_compat.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- writing ------------------------------------------------------------ */

void
cp_lines_init(struct CP_Lines* lines)
{
    memset(lines, 0, sizeof(*lines));
}

void
cp_lines_clear(struct CP_Lines* lines)
{
    for( int i = 0; i < lines->count; i++ )
        free(lines->lines[i]);
    lines->count = 0;
}

void
cp_lines_free(struct CP_Lines* lines)
{
    cp_lines_clear(lines);
    free(lines->lines);
    lines->lines = NULL;
    lines->capacity = 0;
}

static void
lines_push(
    struct CP_Lines* lines,
    char* owned)
{
    if( !owned )
        return;
    if( lines->count == lines->capacity )
    {
        int next = lines->capacity ? lines->capacity * 2 : 16;
        char** grown = realloc(lines->lines, (size_t)next * sizeof(*grown));
        if( !grown )
        {
            free(owned);
            return;
        }
        lines->lines = grown;
        lines->capacity = next;
    }
    lines->lines[lines->count++] = owned;
}

void
cp_lines_addf(
    struct CP_Lines* lines,
    const char* fmt,
    ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if( n < 0 )
    {
        va_end(ap2);
        return;
    }
    char* buf = malloc((size_t)n + 1);
    if( !buf )
    {
        va_end(ap2);
        return;
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    lines_push(lines, buf);
}

void
cp_lines_add_str(
    struct CP_Lines* lines,
    const char* key,
    const char* value)
{
    if( !value )
        return;
    size_t klen = strlen(key);
    size_t vlen = strlen(value);
    /* Worst case every byte escapes to two. */
    char* buf = malloc(klen + 1 + vlen * 2 + 1);
    if( !buf )
        return;
    memcpy(buf, key, klen);
    buf[klen] = '=';
    size_t w = klen + 1;
    for( size_t i = 0; i < vlen; i++ )
    {
        unsigned char c = (unsigned char)value[i];
        if( c == '\n' )
        {
            buf[w++] = '\\';
            buf[w++] = 'n';
        }
        else if( c == '\r' )
        {
            buf[w++] = '\\';
            buf[w++] = 'r';
        }
        else if( c == '\\' )
        {
            buf[w++] = '\\';
            buf[w++] = '\\';
        }
        else if( c == '[' && i == 0 )
        {
            /* Only at the start, and only so a value can never be re-read as a
             * block header. Mid-string brackets are common in item names. */
            buf[w++] = '\\';
            buf[w++] = '[';
        }
        else
        {
            buf[w++] = (char)c;
        }
    }
    buf[w] = '\0';
    lines_push(lines, buf);
}

void
cp_lines_write(
    const struct CP_Lines* lines,
    const char* debugname,
    FILE* out)
{
    fprintf(out, "[%s]\n", debugname);
    for( int i = 0; i < lines->count; i++ )
        fprintf(out, "%s\n", lines->lines[i]);
    fputc('\n', out);
}

/* ---- reading ------------------------------------------------------------ */

char*
cp_unescape(
    const char* value,
    char* out,
    int out_size)
{
    int w = 0;
    for( int i = 0; value[i] && w < out_size - 1; i++ )
    {
        if( value[i] == '\\' && value[i + 1] )
        {
            char n = value[++i];
            if( n == 'n' )
                out[w++] = '\n';
            else if( n == 'r' )
                out[w++] = '\r';
            else
                out[w++] = n; /* covers '\\' and '\[' */
        }
        else
        {
            out[w++] = value[i];
        }
    }
    out[w] = '\0';
    return out;
}

static struct CP_Config*
config_file_push(struct CP_ConfigFile* file)
{
    if( file->count == file->capacity )
    {
        int next = file->capacity ? file->capacity * 2 : 64;
        struct CP_Config* grown = realloc(file->configs, (size_t)next * sizeof(*grown));
        if( !grown )
            return NULL;
        file->configs = grown;
        file->capacity = next;
    }
    struct CP_Config* c = &file->configs[file->count++];
    memset(c, 0, sizeof(*c));
    return c;
}

static int
config_push_line(
    struct CP_Config* config,
    char* key,
    char* value,
    int line_no)
{
    if( config->count == config->capacity )
    {
        int next = config->capacity ? config->capacity * 2 : 8;
        struct CP_ConfigLine* grown = realloc(config->lines, (size_t)next * sizeof(*grown));
        if( !grown )
            return 0;
        config->lines = grown;
        config->capacity = next;
    }
    config->lines[config->count].key = key;
    config->lines[config->count].value = value;
    config->lines[config->count].line_no = line_no;
    config->count++;
    return 1;
}

static char*
dup_range(
    const char* start,
    size_t len)
{
    char* s = malloc(len + 1);
    if( !s )
        return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

char*
cp_lines_to_string(
    const struct CP_Lines* lines,
    const char* debugname,
    size_t* out_size)
{
    size_t need = strlen(debugname) + 4;
    for( int i = 0; i < lines->count; i++ )
        need += strlen(lines->lines[i]) + 1;
    char* buf = malloc(need + 1);
    if( !buf )
        return NULL;
    size_t w = (size_t)snprintf(buf, need + 1, "[%s]\n", debugname);
    for( int i = 0; i < lines->count; i++ )
        w += (size_t)snprintf(buf + w, need + 1 - w, "%s\n", lines->lines[i]);
    if( out_size )
        *out_size = w;
    return buf;
}

static int
config_file_read(
    struct CP_ConfigFile* file,
    FILE* f,
    const char* path);

int
cp_config_file_load(
    struct CP_ConfigFile* file,
    const char* path)
{
    memset(file, 0, sizeof(*file));
    FILE* f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "cachepack: cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }
    file->path = dup_range(path, strlen(path));
    return config_file_read(file, f, path);
}

int
cp_config_file_load_memory(
    struct CP_ConfigFile* file,
    const char* text,
    size_t size,
    const char* label)
{
    memset(file, 0, sizeof(*file));
    FILE* f = fmemopen((void*)text, size, "rb");
    if( !f )
        return 0;
    file->path = dup_range(label, strlen(label));
    return config_file_read(file, f, label);
}

/*
 * The `^constant` table, registered by cp_constants_load once per run.
 *
 * Borrowed pointers — the ctx owns the strings, and this file cannot see the
 * ctx (it is the low-level text reader). LostCity substitutes constants into
 * every config VALUE at parse time (PackShared.ts), and this is the one place
 * every value passes through.
 */
static char* const* g_constant_names;
static char* const* g_constant_values;
static int g_constant_count;

void
cp_text_set_constants(
    char* const* names,
    char* const* values,
    int count)
{
    g_constant_names = names;
    g_constant_values = values;
    g_constant_count = count;
}

/**
 * LostCity's substitution, token for token: a `^` starts a token that runs to
 * the next comma, space or end of value; a token the table names is replaced;
 * one it does not is left exactly as written (some values legitimately carry a
 * caret the packer does not know). Returns a malloc'd string either way.
 */
static char*
substitute_constants(const char* value)
{
    size_t cap = strlen(value) + 1;
    char* out;
    size_t at = 0;

    if( !g_constant_count || !strchr(value, '^') )
        return dup_range(value, strlen(value));

    /* Worst case every token grows; grow the buffer as needed instead of
     * guessing. */
    out = malloc(cap);
    if( !out )
        return NULL;
    for( const char* p = value; *p; )
    {
        const char* rep = NULL;
        size_t token_len = 0;

        if( *p == '^' )
        {
            const char* end = p + 1;
            while( *end && *end != ',' && *end != ' ' )
                end++;
            token_len = (size_t)(end - p);
            for( int i = 0; i < g_constant_count; i++ )
            {
                if( strlen(g_constant_names[i]) == token_len - 1 &&
                    strncmp(g_constant_names[i], p + 1, token_len - 1) == 0 )
                {
                    rep = g_constant_values[i];
                    break;
                }
            }
        }

        {
            const char* src = rep ? rep : p;
            size_t n = rep ? strlen(rep) : (token_len ? token_len : 1);

            if( at + n + 1 > cap )
            {
                char* grown;
                cap = (at + n + 1) * 2;
                grown = realloc(out, cap);
                if( !grown )
                {
                    free(out);
                    return NULL;
                }
                out = grown;
            }
            memcpy(out + at, src, n);
            at += n;
        }
        p += token_len ? token_len : 1;
    }
    out[at] = '\0';
    return out;
}

static int
config_file_read(
    struct CP_ConfigFile* file,
    FILE* f,
    const char* path)
{

    char* line = NULL;
    size_t cap = 0;
    ssize_t len;
    int line_no = 0;
    struct CP_Config* current = NULL;
    int ok = 1;

    while( (len = getline(&line, &cap, f)) > 0 )
    {
        line_no++;
        while( len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r') )
            line[--len] = '\0';
        if( len == 0 )
            continue;
        if( line[0] == '/' && line[1] == '/' )
            continue;

        if( line[0] == '[' )
        {
            if( line[len - 1] != ']' )
            {
                fprintf(stderr, "%s:%d: missing closing bracket: %s\n", path, line_no, line);
                ok = 0;
                break;
            }
            if( len == 2 )
            {
                fprintf(stderr, "%s:%d: empty config name\n", path, line_no);
                ok = 0;
                break;
            }
            char* name = dup_range(line + 1, (size_t)len - 2);
            /* A duplicate name is a lost record, not a merge: whichever block the
             * packer reached last would win silently. */
            for( int i = 0; i < file->count; i++ )
            {
                if( strcmp(file->configs[i].debugname, name) == 0 )
                {
                    fprintf(stderr, "%s:%d: duplicate config: %s\n", path, line_no, name);
                    ok = 0;
                    break;
                }
            }
            if( !ok )
            {
                free(name);
                break;
            }
            current = config_file_push(file);
            if( !current )
            {
                free(name);
                ok = 0;
                break;
            }
            current->debugname = name;
            continue;
        }

        if( !current )
        {
            fprintf(stderr, "%s:%d: property before any [name] block\n", path, line_no);
            ok = 0;
            break;
        }

        char* eq = strchr(line, '=');
        if( !eq )
        {
            fprintf(stderr, "%s:%d: missing property separator: %s\n", path, line_no, line);
            ok = 0;
            break;
        }
        char* key = dup_range(line, (size_t)(eq - line));
        char* value = substitute_constants(eq + 1);
        if( !key || !value || !config_push_line(current, key, value, line_no) )
        {
            free(key);
            free(value);
            ok = 0;
            break;
        }
    }

    free(line);
    fclose(f);
    if( !ok )
        cp_config_file_free(file);
    return ok;
}

void
cp_config_file_free(struct CP_ConfigFile* file)
{
    for( int i = 0; i < file->count; i++ )
    {
        struct CP_Config* c = &file->configs[i];
        for( int j = 0; j < c->count; j++ )
        {
            free(c->lines[j].key);
            free(c->lines[j].value);
        }
        free(c->lines);
        free(c->debugname);
    }
    free(file->configs);
    free(file->path);
    memset(file, 0, sizeof(*file));
}

const char*
cp_config_get(
    const struct CP_Config* config,
    const char* key)
{
    for( int i = 0; i < config->count; i++ )
    {
        if( strcmp(config->lines[i].key, key) == 0 )
            return config->lines[i].value;
    }
    return NULL;
}

/* ---- value helpers ------------------------------------------------------ */

int
cp_parse_int(
    const char* text,
    int* out)
{
    if( !text || !*text )
        return 0;
    char* end = NULL;
    errno = 0;
    long v = strtol(text, &end, 0);
    if( errno != 0 || end == text || *end != '\0' )
        return 0;
    if( v < INT32_MIN || v > INT32_MAX )
        return 0;
    *out = (int)v;
    return 1;
}

int
cp_parse_i64(
    const char* text,
    int64_t* out)
{
    if( !text || !*text )
        return 0;
    char* end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, 0);
    if( errno != 0 || end == text || *end != '\0' )
        return 0;
    *out = (int64_t)v;
    return 1;
}

int
cp_parse_bool(
    const char* text,
    bool* out)
{
    if( !text )
        return 0;
    if( strcmp(text, "yes") == 0 || strcmp(text, "true") == 0 || strcmp(text, "1") == 0 )
    {
        *out = true;
        return 1;
    }
    if( strcmp(text, "no") == 0 || strcmp(text, "false") == 0 || strcmp(text, "0") == 0 )
    {
        *out = false;
        return 1;
    }
    return 0;
}

int
cp_split(
    const char* text,
    char* scratch,
    char** fields,
    int max)
{
    strcpy(scratch, text);
    int n = 0;
    char* p = scratch;
    while( n < max )
    {
        fields[n++] = p;
        char* comma = strchr(p, ',');
        if( !comma )
            break;
        *comma = '\0';
        p = comma + 1;
    }
    return n;
}
