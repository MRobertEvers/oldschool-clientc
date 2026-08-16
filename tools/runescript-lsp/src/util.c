#include "util.h"

#include "platform.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Buffers                                                             */
/* ------------------------------------------------------------------ */

static void
buf_reserve(struct Buf* buf, size_t extra)
{
    size_t needed = buf->length + extra + 1;
    size_t capacity;
    char* grown;

    assert(buf);
    if( needed <= buf->capacity )
        return;

    capacity = buf->capacity ? buf->capacity : 256;
    while( capacity < needed )
        capacity *= 2;

    grown = (char*)realloc(buf->data, capacity);
    assert(grown);
    buf->data = grown;
    buf->capacity = capacity;
}

void
Buf_Free(struct Buf* buf)
{
    if( !buf )
        return;
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

void
Buf_Reset(struct Buf* buf)
{
    assert(buf);
    buf->length = 0;
    if( buf->data )
        buf->data[0] = '\0';
}

void
Buf_Append(struct Buf* buf, const char* text, size_t length)
{
    assert(buf);
    assert(text);
    buf_reserve(buf, length);
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
}

void
Buf_AppendStr(struct Buf* buf, const char* text)
{
    assert(text);
    Buf_Append(buf, text, strlen(text));
}

void
Buf_AppendChar(struct Buf* buf, char c)
{
    Buf_Append(buf, &c, 1);
}

void
Buf_Printf(struct Buf* buf, const char* fmt, ...)
{
    va_list args;
    va_list retry;
    int needed;

    assert(buf);
    assert(fmt);

    va_start(args, fmt);
    va_copy(retry, args);
    needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if( needed < 0 )
    {
        va_end(retry);
        return;
    }

    buf_reserve(buf, (size_t)needed);
    vsnprintf(buf->data + buf->length, (size_t)needed + 1, fmt, retry);
    va_end(retry);
    buf->length += (size_t)needed;
}

void
Buf_AppendJsonString(struct Buf* buf, const char* text)
{
    const unsigned char* p;

    assert(buf);
    Buf_AppendChar(buf, '"');
    for( p = (const unsigned char*)(text ? text : ""); *p; p++ )
    {
        switch( *p )
        {
        case '"':
            Buf_AppendStr(buf, "\\\"");
            break;
        case '\\':
            Buf_AppendStr(buf, "\\\\");
            break;
        case '\n':
            Buf_AppendStr(buf, "\\n");
            break;
        case '\r':
            Buf_AppendStr(buf, "\\r");
            break;
        case '\t':
            Buf_AppendStr(buf, "\\t");
            break;
        default:
            /* Control characters must be escaped; everything else, UTF-8
             * bytes included, goes out as-is — JSON is defined over text and
             * the transport is already UTF-8. */
            if( *p < 0x20 )
                Buf_Printf(buf, "\\u%04x", (unsigned)*p);
            else
                Buf_AppendChar(buf, (char)*p);
            break;
        }
    }
    Buf_AppendChar(buf, '"');
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

char*
Str_Dup(const char* text)
{
    assert(text);
    return Str_DupN(text, strlen(text));
}

char*
Str_DupN(const char* text, size_t length)
{
    char* copy;

    assert(text);
    copy = (char*)malloc(length + 1);
    assert(copy);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

int
Str_CaseCmp(const char* a, const char* b)
{
    assert(a);
    assert(b);
    for( ; *a && *b; a++, b++ )
    {
        int x = tolower((unsigned char)*a);
        int y = tolower((unsigned char)*b);

        if( x != y )
            return x - y;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int
Str_HasSuffix(const char* text, const char* suffix)
{
    size_t text_length;
    size_t suffix_length;

    assert(text);
    assert(suffix);
    text_length = strlen(text);
    suffix_length = strlen(suffix);
    if( suffix_length > text_length )
        return 0;
    return Str_CaseCmp(text + text_length - suffix_length, suffix) == 0;
}

const char*
Str_Extension(const char* path)
{
    const char* base;
    const char* dot;

    assert(path);
    base = Str_Basename(path);
    dot = strrchr(base, '.');
    return dot ? dot + 1 : "";
}

const char*
Str_Basename(const char* path)
{
    const char* base;
    const char* p;

    assert(path);
    base = path;
    for( p = path; *p; p++ )
    {
        if( Plat_IsPathSeparator(*p) )
            base = p + 1;
    }
    return base;
}

/* ------------------------------------------------------------------ */
/* URIs                                                                */
/* ------------------------------------------------------------------ */

static int
hex_value(int c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

char*
Uri_ToPath(const char* uri)
{
    struct Buf out = { 0 };
    const char* p;

    if( !uri )
        return NULL;

    /* Only file: URIs name something on this disk. Anything else — an
     * untitled: buffer, a git: diff view — has no path, and saying so is what
     * keeps the caller from indexing a phantom. */
    if( strncmp(uri, "file://", 7) != 0 )
        return NULL;

    p = uri + 7;
    /* file://host/path is not something an editor sends; file:///path is. */
    while( *p && *p != '/' )
        p++;

    /* `file:///C:/a/b` is a Windows path whose root is the drive letter, so
     * the leading slash is part of the URI's grammar and not of the path.
     * Keeping it produces `/C:/a/b`, which opens nothing. */
    if( p[0] == '/' && isalpha((unsigned char)p[1]) && (p[2] == ':' || (p[2] == '%' && p[3] == '3')) )
        p++;

    for( ; *p; p++ )
    {
        if( *p == '%' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0 )
        {
            Buf_AppendChar(&out, (char)(hex_value(p[1]) * 16 + hex_value(p[2])));
            p += 2;
        }
        else
        {
            Buf_AppendChar(&out, *p);
        }
    }

    if( !out.data )
        Buf_AppendStr(&out, "");
    return out.data;
}

char*
Uri_FromPath(const char* path)
{
    struct Buf out = { 0 };
    const unsigned char* p;

    assert(path);
    Buf_AppendStr(&out, "file://");
    /* An absolute POSIX path already begins with the separator that follows
     * the authority; a Windows one begins with a drive letter and needs it. */
    if( !Plat_IsPathSeparator(path[0]) )
        Buf_AppendChar(&out, '/');

    for( p = (const unsigned char*)path; *p; p++ )
    {
        if( Plat_IsPathSeparator((char)*p) )
            Buf_AppendChar(&out, '/');
        else if( isalnum(*p) || *p == '.' || *p == '-' || *p == '_' || *p == '~' || *p == ':' )
            Buf_AppendChar(&out, (char)*p);
        else
            Buf_Printf(&out, "%%%02X", (unsigned)*p);
    }
    return out.data;
}

/* ------------------------------------------------------------------ */
/* UTF-16 columns                                                      */
/* ------------------------------------------------------------------ */

uint32_t
Utf16_ColumnFromBytes(const char* line, size_t bytes)
{
    uint32_t units = 0;
    size_t i = 0;

    assert(line);
    while( i < bytes )
    {
        unsigned char c = (unsigned char)line[i];

        if( c < 0x80 )
        {
            i += 1;
            units += 1;
        }
        else if( (c & 0xE0) == 0xC0 )
        {
            i += 2;
            units += 1;
        }
        else if( (c & 0xF0) == 0xE0 )
        {
            i += 3;
            units += 1;
        }
        else if( (c & 0xF8) == 0xF0 )
        {
            /* Outside the BMP: two UTF-16 units for one code point. */
            i += 4;
            units += 2;
        }
        else
        {
            i += 1;
            units += 1;
        }
    }
    return units;
}

size_t
Utf16_BytesFromColumn(const char* line, size_t line_bytes, uint32_t column)
{
    uint32_t units = 0;
    size_t i = 0;

    assert(line);
    while( i < line_bytes && units < column )
    {
        unsigned char c = (unsigned char)line[i];

        if( c < 0x80 )
        {
            i += 1;
            units += 1;
        }
        else if( (c & 0xE0) == 0xC0 )
        {
            i += 2;
            units += 1;
        }
        else if( (c & 0xF0) == 0xE0 )
        {
            i += 3;
            units += 1;
        }
        else if( (c & 0xF8) == 0xF0 )
        {
            i += 4;
            units += 2;
        }
        else
        {
            i += 1;
            units += 1;
        }
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

char*
File_Read(const char* path, size_t* out_length)
{
    FILE* file;
    long size;
    char* data;
    size_t read;

    assert(path);
    if( out_length )
        *out_length = 0;

    file = fopen(path, "rb");
    if( !file )
        return NULL;

    if( fseek(file, 0, SEEK_END) != 0 )
    {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    rewind(file);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }

    data = (char*)malloc((size_t)size + 1);
    assert(data);
    read = fread(data, 1, (size_t)size, file);
    fclose(file);

    data[read] = '\0';
    if( out_length )
        *out_length = read;
    return data;
}
