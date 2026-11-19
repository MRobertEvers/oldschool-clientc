/*
 * The player account server's shared vocabulary. See player_proto.h.
 */
#include "player_proto.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void
PlayerProto_Clean(
    char const* text,
    char* out,
    size_t cap)
{
    size_t i = 0;

    assert(text);
    assert(out);
    assert(cap > 0);
    for( ; text[i] && i + 1 < cap; i++ )
        out[i] = text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ? ' ' : text[i];
    out[i] = '\0';
}

int
PlayerProto_Fields(
    char* line,
    char** fields,
    int max)
{
    int count = 0;

    assert(line);
    assert(fields);
    while( count < max )
    {
        char* tab = strchr(line, '\t');

        fields[count++] = line;
        if( !tab )
            break;
        *tab = '\0';
        line = tab + 1;
    }
    return count;
}

void
PlayerProto_FormAdd(
    char* out,
    size_t cap,
    char const* name,
    char const* value)
{
    static char const hex[] = "0123456789ABCDEF";
    size_t len;

    assert(out);
    assert(name);
    assert(value);
    len = strlen(out);
    snprintf(out + len, cap - len, "%s%s=", len ? "&" : "", name);
    len = strlen(out);
    for( ; *value && len + 4 < cap; value++ )
    {
        unsigned char c = (unsigned char)*value;

        if( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' )
            out[len++] = (char)c;
        else
        {
            out[len++] = '%';
            out[len++] = hex[c >> 4];
            out[len++] = hex[c & 15];
        }
    }
    out[len] = '\0';
}

static int
hex_value(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    c = (char)tolower((unsigned char)c);
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    return -1;
}

int
PlayerProto_FormGet(
    char const* data,
    size_t len,
    char const* name,
    char* out,
    size_t cap)
{
    size_t name_len;
    size_t at = 0;

    assert(name);
    assert(out);
    assert(cap > 0);
    out[0] = '\0';
    /* A request with no body has nothing named in it. */
    if( len == 0 )
        return 0;
    assert(data);
    name_len = strlen(name);
    while( at < len )
    {
        size_t end = at;

        while( end < len && data[end] != '&' )
            end++;
        if( end - at > name_len && strncmp(data + at, name, name_len) == 0 &&
            data[at + name_len] == '=' )
        {
            size_t o = 0;

            for( size_t i = at + name_len + 1; i < end; i++ )
            {
                char c = data[i];

                if( c == '+' )
                    c = ' ';
                else if( c == '%' && i + 2 < end && hex_value(data[i + 1]) >= 0 &&
                         hex_value(data[i + 2]) >= 0 )
                {
                    c = (char)(hex_value(data[i + 1]) * 16 + hex_value(data[i + 2]));
                    i += 2;
                }
                if( o + 1 >= cap )
                {
                    out[0] = '\0';
                    return 0;
                }
                out[o++] = c;
            }
            out[o] = '\0';
            return 1;
        }
        at = end + 1;
    }
    return 0;
}

int
PlayerProto_Lines(
    char* text,
    char** lines,
    int max)
{
    int count = 0;

    assert(lines);
    while( text && *text && count < max )
    {
        char* next = strchr(text, '\n');

        if( next )
            *next++ = '\0';
        lines[count++] = text;
        text = next;
    }
    return count;
}
