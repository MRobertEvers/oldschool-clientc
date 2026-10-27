/*
 * One level of a JSON object. See flat_json.h.
 */
#include "flat_json.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct FlatJsonScan
{
    const char* at;
    const char* end;
};

static void
skip_space(struct FlatJsonScan* scan)
{
    while( scan->at < scan->end &&
           (*scan->at == ' ' || *scan->at == '\t' || *scan->at == '\n' || *scan->at == '\r') )
        scan->at++;
}

static int
hex_digit(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

/*
 * A string at `scan->at` (which is its opening quote), decoded into `out` when
 * `out` is non-NULL. 1 on a whole string; 0 when it is malformed, or does not
 * fit -- in which case the scan is no longer meaningful.
 */
static int
read_string(
    struct FlatJsonScan* scan,
    char* out,
    size_t cap)
{
    size_t used = 0;

    if( scan->at >= scan->end || *scan->at != '"' )
        return 0;
    scan->at++;
    while( scan->at < scan->end && *scan->at != '"' )
    {
        char ch = *scan->at++;

        if( ch == '\\' )
        {
            if( scan->at >= scan->end )
                return 0;
            ch = *scan->at++;
            if( ch == 'b' )
                ch = '\b';
            else if( ch == 'f' )
                ch = '\f';
            else if( ch == 'n' )
                ch = '\n';
            else if( ch == 'r' )
                ch = '\r';
            else if( ch == 't' )
                ch = '\t';
            else if( ch == 'u' )
            {
                int code = 0;

                if( scan->end - scan->at < 4 )
                    return 0;
                for( int i = 0; i < 4; i++ )
                {
                    int digit = hex_digit(scan->at[i]);

                    if( digit < 0 )
                        return 0;
                    code = code << 4 | digit;
                }
                scan->at += 4;
                ch = code > 0 && code < 0x80 ? (char)code : '?';
            }
            else if( ch != '"' && ch != '\\' && ch != '/' )
                return 0;
        }
        if( out )
        {
            if( used + 1 >= cap )
                return 0;
            out[used++] = ch;
        }
    }
    if( scan->at >= scan->end )
        return 0;
    scan->at++; /* the closing quote */
    if( out )
        out[used] = '\0';
    return 1;
}

/* Past one value of any kind. 0 when malformed. */
static int
skip_value(struct FlatJsonScan* scan)
{
    int depth = 0;

    skip_space(scan);
    if( scan->at >= scan->end )
        return 0;
    if( *scan->at == '"' )
        return read_string(scan, NULL, 0);
    if( *scan->at != '{' && *scan->at != '[' )
    {
        /* A number, true, false or null: up to the next delimiter. */
        const char* start = scan->at;

        while( scan->at < scan->end && *scan->at != ',' && *scan->at != '}' &&
               *scan->at != ']' && *scan->at != ' ' && *scan->at != '\n' && *scan->at != '\r' &&
               *scan->at != '\t' )
            scan->at++;
        return scan->at > start;
    }
    /* An object or array, by depth, with strings skipped whole so a brace
     * inside one does not count. */
    while( scan->at < scan->end )
    {
        char ch = *scan->at;

        if( ch == '"' )
        {
            if( !read_string(scan, NULL, 0) )
                return 0;
            continue;
        }
        scan->at++;
        if( ch == '{' || ch == '[' )
            depth++;
        else if( ch == '}' || ch == ']' )
        {
            if( --depth == 0 )
                return 1;
        }
    }
    return 0;
}

/* Leave the scan at the value of `key`. 1 when found. */
static int
find_key(
    const char* json,
    size_t len,
    const char* key,
    struct FlatJsonScan* scan)
{
    char name[128];

    scan->at = json;
    scan->end = json + len;
    skip_space(scan);
    if( scan->at >= scan->end || *scan->at != '{' )
        return 0;
    scan->at++;
    for( ;; )
    {
        skip_space(scan);
        if( scan->at < scan->end && *scan->at == '}' )
            return 0;
        if( !read_string(scan, name, sizeof(name)) )
            return 0;
        skip_space(scan);
        if( scan->at >= scan->end || *scan->at != ':' )
            return 0;
        scan->at++;
        skip_space(scan);
        if( strcmp(name, key) == 0 )
            return 1;
        if( !skip_value(scan) )
            return 0;
        skip_space(scan);
        if( scan->at < scan->end && *scan->at == ',' )
            scan->at++;
        else
            return 0;
    }
}

int
FlatJson_String(
    const char* json,
    size_t len,
    const char* key,
    char* out,
    size_t cap)
{
    struct FlatJsonScan scan;

    assert(json);
    assert(key);
    assert(out);
    if( !find_key(json, len, key, &scan) )
        return 0;
    return read_string(&scan, out, cap);
}

int
FlatJson_Int64(
    const char* json,
    size_t len,
    const char* key,
    long long* out)
{
    struct FlatJsonScan scan;
    char digits[24];
    size_t used = 0;
    char* end;

    assert(json);
    assert(key);
    assert(out);
    if( !find_key(json, len, key, &scan) )
        return 0;
    while( scan.at < scan.end && used + 1 < sizeof(digits) &&
           ((*scan.at >= '0' && *scan.at <= '9') || (*scan.at == '-' && used == 0)) )
        digits[used++] = *scan.at++;
    digits[used] = '\0';
    if( used == 0 || (used == 1 && digits[0] == '-') )
        return 0;
    *out = strtoll(digits, &end, 10);
    return *end == '\0';
}
