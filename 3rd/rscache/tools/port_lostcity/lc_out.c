#include "lc_out.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int
str_reserve(struct LC_Str* str, size_t extra)
{
    assert(str);
    if( str->len + extra + 1 <= str->cap )
        return 1;
    size_t cap = str->cap == 0 ? 4096 : str->cap;
    while( cap < str->len + extra + 1 )
        cap *= 2;
    char* data = realloc(str->data, cap);
    if( !data )
        return 0;
    str->data = data;
    str->cap = cap;
    return 1;
}

void
lc_str_addf(
    struct LC_Str* str,
    const char* fmt,
    ...)
{
    assert(str && fmt);

    va_list ap;
    va_start(ap, fmt);
    char stack[1024];
    int n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    if( n < 0 )
        return;

    if( (size_t)n < sizeof(stack) )
    {
        if( !str_reserve(str, (size_t)n) )
            return;
        memcpy(str->data + str->len, stack, (size_t)n + 1);
        str->len += (size_t)n;
        return;
    }

    if( !str_reserve(str, (size_t)n) )
        return;
    va_start(ap, fmt);
    vsnprintf(str->data + str->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    str->len += (size_t)n;
}

void
lc_str_free(struct LC_Str* str)
{
    if( !str )
        return;
    free(str->data);
    str->data = NULL;
    str->len = 0;
    str->cap = 0;
}

int
lc_out_init(
    struct LC_Out* out,
    const char* content_dir,
    const char* area,
    const char* prefix,
    int dry_run)
{
    assert(out && content_dir && area && prefix);
    memset(out, 0, sizeof(*out));
    snprintf(out->content, sizeof(out->content), "%s", content_dir);
    snprintf(out->area, sizeof(out->area), "%s", area);
    snprintf(out->prefix, sizeof(out->prefix), "%s", prefix);
    out->dry_run = dry_run;

    struct stat st;
    if( stat(content_dir, &st) != 0 )
    {
        fprintf(stderr, "content dir does not exist: %s\n", content_dir);
        return 0;
    }
    return 1;
}

/** mkdir -p for the directory portion of an absolute path. */
static void
mkdir_parents(const char* path)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    char* last = strrchr(buf, '/');
    if( !last )
        return;
    *last = '\0';
    for( char* p = buf + 1; *p; p++ )
    {
        if( *p != '/' )
            continue;
        *p = '\0';
        mkdir(buf, 0755);
        *p = '/';
    }
    mkdir(buf, 0755);
}

int
lc_out_write_file(
    struct LC_Out* out,
    const char* relpath,
    const void* data,
    size_t size)
{
    assert(out && relpath && (data || size == 0));

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", out->content, relpath);

    out->files_written++;
    out->bytes_written += (long)size;
    if( out->dry_run )
        return 1;

    mkdir_parents(path);
    FILE* f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    if( size > 0 && fwrite(data, 1, size, f) != size )
    {
        fprintf(stderr, "short write %s\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

void
lc_out_warn(
    struct LC_Out* out,
    const char* fmt,
    ...)
{
    assert(out && fmt);

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    char** grown = realloc(out->warnings, (size_t)(out->warning_count + 1) * sizeof(char*));
    if( !grown )
        return;
    out->warnings = grown;
    size_t n = strlen(buf);
    out->warnings[out->warning_count] = malloc(n + 1);
    if( !out->warnings[out->warning_count] )
        return;
    memcpy(out->warnings[out->warning_count], buf, n + 1);
    out->warning_count++;
}

static int
flush_one(
    struct LC_Out* out,
    const struct LC_Str* str,
    const char* ext)
{
    if( !str->data || str->len == 0 )
        return 1;
    char rel[1024];
    snprintf(
        rel, sizeof(rel), "scripts/%s/configs/%s.%s", out->area, out->prefix, ext);
    return lc_out_write_file(out, rel, str->data, str->len);
}

int
lc_out_flush_configs(struct LC_Out* out)
{
    assert(out);
    if( !flush_one(out, &out->npc_cfg, "npc") )
        return 0;
    if( !flush_one(out, &out->seq_cfg, "seq") )
        return 0;
    if( !flush_one(out, &out->spotanim_cfg, "spotanim") )
        return 0;
    if( !flush_one(out, &out->loc_cfg, "loc") )
        return 0;
    return 1;
}

void
lc_out_free(struct LC_Out* out)
{
    if( !out )
        return;
    lc_str_free(&out->npc_cfg);
    lc_str_free(&out->seq_cfg);
    lc_str_free(&out->spotanim_cfg);
    lc_str_free(&out->loc_cfg);
    for( int i = 0; i < out->warning_count; i++ )
        free(out->warnings[i]);
    free(out->warnings);
    memset(out, 0, sizeof(*out));
}
