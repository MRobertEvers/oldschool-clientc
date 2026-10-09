#include <stdint.h>
#include "cp_text.h"
#include "tool_posix_compat.h"

#include <assert.h>
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
    assert(lines);
    assert(owned);
    if( lines->count == lines->capacity )
    {
        int next = lines->capacity ? lines->capacity * 2 : 16;
        char** grown = realloc(lines->lines, (size_t)next * sizeof(*grown));
        assert(grown);
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
    assert(n >= 0);
    char* buf = malloc((size_t)n + 1);
    assert(buf);
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
    assert(lines);
    assert(key);
    assert(value);
    size_t klen = strlen(key);
    size_t vlen = strlen(value);
    /* Worst case every byte escapes to two, plus one for a marker escape. */
    char* buf = malloc(klen + 1 + vlen * 2 + 2);
    assert(buf);
    memcpy(buf, key, klen);
    buf[klen] = '=';
    size_t w = klen + 1;
    /* A string that IS one of the two markers is escaped, so `name=\default`
     * is an item named "default" and `name=default` is an item with no name. */
    if( cp_value_is_default(value) || cp_value_is_empty(value) )
        buf[w++] = '\\';
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

int
cp_value_is_default(const char* raw)
{
    assert(raw);
    return strcmp(raw, CP_VALUE_DEFAULT) == 0;
}

int
cp_value_is_empty(const char* raw)
{
    assert(raw);
    return strcmp(raw, CP_VALUE_EMPTY) == 0;
}

void
cp_lines_add_default(
    struct CP_Lines* lines,
    const char* key)
{
    assert(key);
    cp_lines_addf(lines, "%s=" CP_VALUE_DEFAULT, key);
}

void
cp_lines_add_empty(
    struct CP_Lines* lines,
    const char* key)
{
    assert(key);
    cp_lines_addf(lines, "%s=" CP_VALUE_EMPTY, key);
}

/*
 * A trailing comment is `//` that is not escaped, as the server's line cleaner
 * has always read it, and a value never ends in a blank the reader would trim.
 * So a value that holds `//` (a wiki URL in a string param) is written `/\/`, and
 * a trailing blank `\ `. Done once, here, on the finished line, so no emitter has
 * to remember it; idempotent, so a line an emitter already escaped (db text)
 * comes through unchanged.
 */
static size_t
line_escape_size(const char* line)
{
    return strlen(line) * 2 + 1;
}

/** Is `s[at]` escaped -- preceded by an odd run of backslashes? */
static int
text_escaped(
    const char* s,
    size_t at)
{
    size_t run = 0;

    while( at > run && s[at - 1 - run] == '\\' )
        run++;
    return run & 1;
}

static void
line_escape(
    const char* line,
    char* out)
{
    const char* eq = strchr(line, '=');
    size_t length = strlen(line);
    size_t trailing = 0;
    size_t w = 0;

    if( !eq )
    {
        memcpy(out, line, length + 1);
        return;
    }
    while( trailing < length - (size_t)(eq + 1 - line) &&
           line[length - 1 - trailing] == ' ' && !text_escaped(line, length - 1 - trailing) )
        trailing++;
    for( size_t i = 0; i < length; i++ )
    {
        char c = line[i];

        if( line + i > eq )
        {
            /* The second slash of an unescaped `//`. */
            if( c == '/' && line[i - 1] == '/' && line + i - 1 > eq && !text_escaped(line, i - 1) )
                out[w++] = '\\';
            else if( c == ' ' && i >= length - trailing )
                out[w++] = '\\';
        }
        out[w++] = c;
    }
    out[w] = '\0';
}

void
cp_line_write(
    FILE* out,
    const char* line)
{
    char* escaped;

    assert(out);
    assert(line);
    escaped = malloc(line_escape_size(line));
    assert(escaped);
    line_escape(line, escaped);
    fprintf(out, "%s\n", escaped);
    free(escaped);
}

void
cp_lines_write(
    const struct CP_Lines* lines,
    const char* debugname,
    FILE* out)
{
    fprintf(out, "[%s]\n", debugname);
    for( int i = 0; i < lines->count; i++ )
        cp_line_write(out, lines->lines[i]);
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
        need += line_escape_size(lines->lines[i]) + 1;
    char* buf = malloc(need + 1);
    assert(buf);
    size_t w = (size_t)snprintf(buf, need + 1, "[%s]\n", debugname);
    for( int i = 0; i < lines->count; i++ )
    {
        line_escape(lines->lines[i], buf + w);
        w += strlen(buf + w);
        buf[w++] = '\n';
        buf[w] = '\0';
    }
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
    int* seen = NULL;
    uint32_t seen_capacity = 0;
    uint32_t seen_used = 0;

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
             * packer reached last would win silently. Hashed: a scan of every
             * earlier block was quadratic in the file, and all.loc has 62,000. */
            {
                uint32_t hash = 2166136261u;
                uint32_t slot;

                for( const char* c = name; *c; c++ )
                    hash = (hash ^ (uint8_t)*c) * 16777619u;
                if( (seen_used + 1) * 2 > seen_capacity )
                {
                    seen_capacity = seen_capacity ? seen_capacity * 2 : 1024;
                    free(seen);
                    seen = (int*)malloc(seen_capacity * sizeof(int));
                    assert(seen);
                    memset(seen, -1, seen_capacity * sizeof(int));
                    seen_used = 0;
                    for( int i = 0; i < file->count; i++ )
                    {
                        uint32_t h = 2166136261u;

                        for( const char* c = file->configs[i].debugname; *c; c++ )
                            h = (h ^ (uint8_t)*c) * 16777619u;
                        slot = h & (seen_capacity - 1);
                        while( seen[slot] >= 0 )
                            slot = (slot + 1) & (seen_capacity - 1);
                        seen[slot] = i;
                        seen_used++;
                    }
                }
                slot = hash & (seen_capacity - 1);
                while( seen[slot] >= 0 )
                {
                    if( strcmp(file->configs[seen[slot]].debugname, name) == 0 )
                    {
                        fprintf(stderr, "%s:%d: duplicate config: %s\n", path, line_no, name);
                        ok = 0;
                        break;
                    }
                    slot = (slot + 1) & (seen_capacity - 1);
                }
                if( ok )
                {
                    /* The block pushed just below takes index `file->count`. */
                    seen[slot] = file->count;
                    seen_used++;
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
        /* The value ends at an unescaped `//` (a comment) and loses its unescaped
         * trailing blanks: the server's line cleaner, and line_escape's inverse. */
        {
            char* cut = eq + 1;
            size_t end;

            for( ; *cut; cut++ )
            {
                if( *cut == '\\' && cut[1] )
                {
                    cut++;
                    continue;
                }
                if( cut[0] == '/' && cut[1] == '/' )
                {
                    *cut = '\0';
                    break;
                }
            }
            end = strlen(eq + 1);
            while( end > 0 && (eq[end] == ' ' || eq[end] == '\t') &&
                   !text_escaped(eq + 1, end - 1) )
                eq[end--] = '\0';
        }
        char* key = dup_range(line, (size_t)(eq - line));
        char* value = dup_range(eq + 1, strlen(eq + 1));
        if( !key || !value || !config_push_line(current, key, value, line_no) )
        {
            free(key);
            free(value);
            ok = 0;
            break;
        }
    }

    free(seen);
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
