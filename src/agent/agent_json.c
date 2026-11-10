/*
 * The agent control server's JSON reader and writer (agent_json.h).
 */

#include "agent/agent_json.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the buffer ------------------------------------------------------------ */

void
AgentBuf_Init(struct AgentBuf* buf)
{
    assert(buf);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

void
AgentBuf_Free(struct AgentBuf* buf)
{
    if( !buf )
        return;
    ToriMem_Free(ToriMem_Net(), buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

void
AgentBuf_Append(
    struct AgentBuf* buf,
    const void* bytes,
    size_t count)
{
    assert(buf);
    if( count == 0 )
        return;
    assert(bytes);
    if( buf->length + count > buf->capacity )
    {
        size_t capacity = buf->capacity ? buf->capacity : 256;
        while( capacity < buf->length + count )
            capacity *= 2;
        buf->data = ToriMem_Realloc(ToriMem_Net(), buf->data, capacity);
        assert(buf->data);
        buf->capacity = capacity;
    }
    memcpy(buf->data + buf->length, bytes, count);
    buf->length += count;
}

void
AgentBuf_Consume(
    struct AgentBuf* buf,
    size_t count)
{
    assert(buf);
    assert(count <= buf->length);
    if( count == 0 )
        return;
    memmove(buf->data, buf->data + count, buf->length - count);
    buf->length -= count;
}

/* ---- the writer -------------------------------------------------------------- */

void
AgentJsonW_Init(
    struct AgentJsonWriter* w,
    struct AgentBuf* out)
{
    assert(w);
    assert(out);
    memset(w, 0, sizeof(*w));
    w->out = out;
}

/** The comma a value owes: none right after its key, none for the first
 *  member of its container. */
static void
jw_separate(struct AgentJsonWriter* w)
{
    if( w->after_key )
    {
        w->after_key = 0;
        return;
    }
    if( w->depth == 0 )
        return;
    if( !w->first[w->depth - 1] )
        AgentBuf_Append(w->out, ",", 1);
    w->first[w->depth - 1] = 0;
}

static void
jw_open(
    struct AgentJsonWriter* w,
    char bracket)
{
    assert(w);
    assert(w->depth < AGENT_JSON_DEPTH_MAX);
    jw_separate(w);
    AgentBuf_Append(w->out, &bracket, 1);
    w->first[w->depth] = 1;
    w->depth++;
}

static void
jw_close(
    struct AgentJsonWriter* w,
    char bracket)
{
    assert(w);
    assert(w->depth > 0);
    assert(!w->after_key);
    w->depth--;
    AgentBuf_Append(w->out, &bracket, 1);
}

void
AgentJsonW_ObjBegin(struct AgentJsonWriter* w)
{
    jw_open(w, '{');
}

void
AgentJsonW_ObjEnd(struct AgentJsonWriter* w)
{
    jw_close(w, '}');
}

void
AgentJsonW_ArrBegin(struct AgentJsonWriter* w)
{
    jw_open(w, '[');
}

void
AgentJsonW_ArrEnd(struct AgentJsonWriter* w)
{
    jw_close(w, ']');
}

/** Bytes of the well-formed UTF-8 sequence that starts at text[0] (2..4), or
 *  0 when the bytes there are not one (RFC 3629: no overlongs, no surrogates,
 *  nothing past U+10FFFF). `left` is how many bytes remain. */
static int
jw_utf8_run(
    const unsigned char* text,
    size_t left)
{
    unsigned char const lead = text[0];
    int need;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;

    if( lead >= 0xC2 && lead <= 0xDF )
        need = 1;
    else if( lead >= 0xE0 && lead <= 0xEF )
    {
        need = 2;
        if( lead == 0xE0 )
            low = 0xA0;
        if( lead == 0xED )
            high = 0x9F;
    }
    else if( lead >= 0xF0 && lead <= 0xF4 )
    {
        need = 3;
        if( lead == 0xF0 )
            low = 0x90;
        if( lead == 0xF4 )
            high = 0x8F;
    }
    else
        return 0;
    if( left < (size_t)need + 1 )
        return 0;
    if( text[1] < low || text[1] > high )
        return 0;
    for( int i = 2; i <= need; i++ )
        if( text[i] < 0x80 || text[i] > 0xBF )
            return 0;
    return need + 1;
}

static void
jw_string(
    struct AgentJsonWriter* w,
    const char* text,
    size_t count)
{
    const unsigned char* bytes = (const unsigned char*)text;
    size_t run_start = 0;
    size_t i = 0;

    AgentBuf_Append(w->out, "\"", 1);
    while( i < count )
    {
        unsigned char const c = bytes[i];
        char escape[8];
        int escape_length = 0;
        int skip = 1;

        if( c == '"' || c == '\\' )
        {
            escape[0] = '\\';
            escape[1] = (char)c;
            escape_length = 2;
        }
        else if( c == '\n' || c == '\r' || c == '\t' )
        {
            escape[0] = '\\';
            escape[1] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
            escape_length = 2;
        }
        else if( c < 0x20 )
            escape_length = snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)c);
        else if( c >= 0x80 )
        {
            int const run = jw_utf8_run(bytes + i, count - i);
            if( run > 0 )
            {
                /* Well-formed UTF-8 passes through as it is. */
                i += (size_t)run;
                continue;
            }
            /* A stray high byte: its Latin-1 code point. */
            escape_length = snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)c);
        }
        else
        {
            i++;
            continue;
        }
        AgentBuf_Append(w->out, text + run_start, i - run_start);
        AgentBuf_Append(w->out, escape, (size_t)escape_length);
        i += (size_t)skip;
        run_start = i;
    }
    AgentBuf_Append(w->out, text + run_start, count - run_start);
    AgentBuf_Append(w->out, "\"", 1);
}

void
AgentJsonW_Key(
    struct AgentJsonWriter* w,
    const char* name)
{
    assert(w);
    assert(name);
    assert(w->depth > 0);
    assert(!w->after_key);
    jw_separate(w);
    jw_string(w, name, strlen(name));
    AgentBuf_Append(w->out, ":", 1);
    w->after_key = 1;
}

void
AgentJsonW_StrN(
    struct AgentJsonWriter* w,
    const char* text,
    size_t count)
{
    assert(w);
    assert(text);
    jw_separate(w);
    jw_string(w, text, count);
}

void
AgentJsonW_Str(
    struct AgentJsonWriter* w,
    const char* text)
{
    assert(text);
    AgentJsonW_StrN(w, text, strlen(text));
}

void
AgentJsonW_Int(
    struct AgentJsonWriter* w,
    long long value)
{
    char text[32];
    int const length = snprintf(text, sizeof(text), "%lld", value);

    assert(w);
    jw_separate(w);
    AgentBuf_Append(w->out, text, (size_t)length);
}

void
AgentJsonW_Dbl(
    struct AgentJsonWriter* w,
    double value)
{
    char text[40];
    int length;

    assert(w);
    if( isnan(value) || isinf(value) )
    {
        AgentJsonW_Null(w);
        return;
    }
    jw_separate(w);
    length = snprintf(text, sizeof(text), "%.15g", value);
    AgentBuf_Append(w->out, text, (size_t)length);
}

void
AgentJsonW_Bool(
    struct AgentJsonWriter* w,
    int value)
{
    assert(w);
    jw_separate(w);
    if( value )
        AgentBuf_Append(w->out, "true", 4);
    else
        AgentBuf_Append(w->out, "false", 5);
}

void
AgentJsonW_Null(struct AgentJsonWriter* w)
{
    assert(w);
    jw_separate(w);
    AgentBuf_Append(w->out, "null", 4);
}

void
AgentJsonW_Raw(
    struct AgentJsonWriter* w,
    const char* json)
{
    assert(w);
    assert(json);
    assert(json[0]);
    jw_separate(w);
    AgentBuf_Append(w->out, json, strlen(json));
}

void
AgentJsonW_KStr(
    struct AgentJsonWriter* w,
    const char* name,
    const char* text)
{
    AgentJsonW_Key(w, name);
    AgentJsonW_Str(w, text);
}

void
AgentJsonW_KInt(
    struct AgentJsonWriter* w,
    const char* name,
    long long value)
{
    AgentJsonW_Key(w, name);
    AgentJsonW_Int(w, value);
}

void
AgentJsonW_KDbl(
    struct AgentJsonWriter* w,
    const char* name,
    double value)
{
    AgentJsonW_Key(w, name);
    AgentJsonW_Dbl(w, value);
}

void
AgentJsonW_KBool(
    struct AgentJsonWriter* w,
    const char* name,
    int value)
{
    AgentJsonW_Key(w, name);
    AgentJsonW_Bool(w, value);
}

void
AgentJsonW_KInts(
    struct AgentJsonWriter* w,
    const char* name,
    const int* values,
    int count)
{
    assert(values);
    assert(count > 0);
    AgentJsonW_Key(w, name);
    AgentJsonW_ArrBegin(w);
    for( int i = 0; i < count; i++ )
        AgentJsonW_Int(w, values[i]);
    AgentJsonW_ArrEnd(w);
}

void
AgentJsonW_KRect(
    struct AgentJsonWriter* w,
    const char* name,
    int x,
    int y,
    int width,
    int height)
{
    int const box[4] = { x, y, width, height };
    AgentJsonW_KInts(w, name, box, 4);
}

/* ---- the reader -------------------------------------------------------------- */

struct jr
{
    const char* text;
    size_t length;
    size_t at;
    struct AgentJsonDoc* doc;
    char* error;
    size_t error_size;
};

static int
jr_fail(
    struct jr* r,
    const char* what)
{
    snprintf(r->error, r->error_size, "the request is not valid JSON: %s at byte %u", what, (unsigned)r->at);
    return 0;
}

static void
jr_space(struct jr* r)
{
    while( r->at < r->length )
    {
        char const c = r->text[r->at];
        if( c != ' ' && c != '\t' && c != '\r' && c != '\n' )
            return;
        r->at++;
    }
}

static int
jr_hex4(
    struct jr* r,
    unsigned* out)
{
    unsigned value = 0;

    if( r->at + 4 > r->length )
        return 0;
    for( int i = 0; i < 4; i++ )
    {
        char const c = r->text[r->at + (size_t)i];
        unsigned digit;
        if( c >= '0' && c <= '9' )
            digit = (unsigned)(c - '0');
        else if( c >= 'a' && c <= 'f' )
            digit = (unsigned)(c - 'a') + 10u;
        else if( c >= 'A' && c <= 'F' )
            digit = (unsigned)(c - 'A') + 10u;
        else
            return 0;
        value = value * 16u + digit;
    }
    r->at += 4;
    *out = value;
    return 1;
}

static void
jr_pool_put(
    struct jr* r,
    char byte)
{
    /* The pool is the request's own length plus terminators: a decoded
     * string is never longer than its spelling. */
    assert(r->doc->pool_used < r->doc->pool_capacity);
    r->doc->pool[r->doc->pool_used++] = byte;
}

static void
jr_pool_utf8(
    struct jr* r,
    unsigned code)
{
    if( code < 0x80 )
        jr_pool_put(r, (char)code);
    else if( code < 0x800 )
    {
        jr_pool_put(r, (char)(0xC0 | (code >> 6)));
        jr_pool_put(r, (char)(0x80 | (code & 0x3F)));
    }
    else if( code < 0x10000 )
    {
        jr_pool_put(r, (char)(0xE0 | (code >> 12)));
        jr_pool_put(r, (char)(0x80 | ((code >> 6) & 0x3F)));
        jr_pool_put(r, (char)(0x80 | (code & 0x3F)));
    }
    else
    {
        jr_pool_put(r, (char)(0xF0 | (code >> 18)));
        jr_pool_put(r, (char)(0x80 | ((code >> 12) & 0x3F)));
        jr_pool_put(r, (char)(0x80 | ((code >> 6) & 0x3F)));
        jr_pool_put(r, (char)(0x80 | (code & 0x3F)));
    }
}

/** A string at r->at (on its opening quote), decoded into the pool. */
static int
jr_string(
    struct jr* r,
    const char** out)
{
    const char* start;

    if( r->at >= r->length || r->text[r->at] != '"' )
        return jr_fail(r, "expected a string");
    r->at++;
    start = r->doc->pool + r->doc->pool_used;
    for( ;; )
    {
        char c;

        if( r->at >= r->length )
            return jr_fail(r, "a string is not closed");
        c = r->text[r->at++];
        if( c == '"' )
            break;
        if( (unsigned char)c < 0x20 )
            return jr_fail(r, "a control character inside a string (write \\n, \\t)");
        if( c != '\\' )
        {
            jr_pool_put(r, c);
            continue;
        }
        if( r->at >= r->length )
            return jr_fail(r, "a string ends in a backslash");
        c = r->text[r->at++];
        if( c == '"' || c == '\\' || c == '/' )
            jr_pool_put(r, c);
        else if( c == 'n' )
            jr_pool_put(r, '\n');
        else if( c == 't' )
            jr_pool_put(r, '\t');
        else if( c == 'r' )
            jr_pool_put(r, '\r');
        else if( c == 'b' )
            jr_pool_put(r, '\b');
        else if( c == 'f' )
            jr_pool_put(r, '\f');
        else if( c == 'u' )
        {
            unsigned code;
            if( !jr_hex4(r, &code) )
                return jr_fail(r, "\\u wants four hex digits");
            if( code >= 0xD800 && code <= 0xDBFF )
            {
                unsigned low;
                if( r->at + 2 > r->length || r->text[r->at] != '\\' || r->text[r->at + 1] != 'u' )
                    return jr_fail(r, "half of a \\u surrogate pair");
                r->at += 2;
                if( !jr_hex4(r, &low) || low < 0xDC00 || low > 0xDFFF )
                    return jr_fail(r, "half of a \\u surrogate pair");
                code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            }
            else if( code >= 0xDC00 && code <= 0xDFFF )
                return jr_fail(r, "half of a \\u surrogate pair");
            if( code == 0 )
                return jr_fail(r, "\\u0000 cannot be carried in a string here");
            jr_pool_utf8(r, code);
        }
        else
            return jr_fail(r, "an unknown backslash escape");
    }
    jr_pool_put(r, '\0');
    *out = start;
    return 1;
}

static int
jr_word(
    struct jr* r,
    const char* word)
{
    size_t const length = strlen(word);
    if( r->at + length > r->length || memcmp(r->text + r->at, word, length) != 0 )
        return 0;
    r->at += length;
    return 1;
}

/** A string, number, true, false or null at r->at. */
static int
jr_scalar(
    struct jr* r,
    struct AgentJsonValue* out)
{
    char c;

    memset(out, 0, sizeof(*out));
    if( r->at >= r->length )
        return jr_fail(r, "a value is missing");
    c = r->text[r->at];
    if( c == '"' )
    {
        out->type = AGENT_JSON_STRING;
        return jr_string(r, &out->string);
    }
    if( c == 't' || c == 'f' || c == 'n' )
    {
        if( jr_word(r, "true") )
        {
            out->type = AGENT_JSON_BOOL;
            out->boolean = 1;
            return 1;
        }
        if( jr_word(r, "false") )
        {
            out->type = AGENT_JSON_BOOL;
            return 1;
        }
        if( jr_word(r, "null") )
        {
            out->type = AGENT_JSON_NULL;
            return 1;
        }
        return jr_fail(r, "expected true, false or null");
    }
    if( c == '-' || (c >= '0' && c <= '9') )
    {
        char digits[64];
        size_t count = 0;
        char* end = NULL;

        while( r->at + count < r->length && count < sizeof(digits) - 1 )
        {
            char const d = r->text[r->at + count];
            if( !((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') )
                break;
            digits[count++] = d;
        }
        digits[count] = '\0';
        out->type = AGENT_JSON_NUMBER;
        out->number = strtod(digits, &end);
        if( end == digits || *end != '\0' )
            return jr_fail(r, "a number is malformed");
        r->at += count;
        return 1;
    }
    if( c == '{' )
        return jr_fail(r, "a nested object (a request is one flat object)");
    if( c == '[' )
        return jr_fail(r, "an array inside an array");
    return jr_fail(r, "expected a value");
}

static int
jr_value(
    struct jr* r,
    struct AgentJsonValue* out)
{
    if( r->at < r->length && r->text[r->at] == '[' )
    {
        memset(out, 0, sizeof(*out));
        out->type = AGENT_JSON_ARRAY;
        out->first = r->doc->element_count;
        r->at++;
        jr_space(r);
        if( r->at < r->length && r->text[r->at] == ']' )
        {
            r->at++;
            return 1;
        }
        for( ;; )
        {
            if( r->doc->element_count >= AGENT_JSON_ELEMENTS_MAX )
                return jr_fail(r, "more array elements than a request may carry");
            if( !jr_scalar(r, &r->doc->elements[r->doc->element_count]) )
                return 0;
            r->doc->element_count++;
            out->count++;
            jr_space(r);
            if( r->at < r->length && r->text[r->at] == ',' )
            {
                r->at++;
                jr_space(r);
                continue;
            }
            if( r->at < r->length && r->text[r->at] == ']' )
            {
                r->at++;
                return 1;
            }
            return jr_fail(r, "expected ',' or ']' in an array");
        }
    }
    return jr_scalar(r, out);
}

int
AgentJson_Parse(
    struct AgentJsonDoc* doc,
    const char* text,
    size_t length,
    char* error,
    size_t error_size)
{
    struct jr r;

    assert(doc);
    assert(text);
    assert(error);
    assert(error_size > 0);

    memset(doc, 0, sizeof(*doc));
    error[0] = '\0';
    /* One terminator per string at most, and a string costs two quotes. */
    doc->pool_capacity = length + 1;
    doc->pool = ToriMem_Alloc(ToriMem_Net(), doc->pool_capacity);
    assert(doc->pool);

    r.text = text;
    r.length = length;
    r.at = 0;
    r.doc = doc;
    r.error = error;
    r.error_size = error_size;

    jr_space(&r);
    if( r.at >= r.length || r.text[r.at] != '{' )
        return jr_fail(&r, "a request is one object, {\"verb\": ...}; expected '{'");
    r.at++;
    jr_space(&r);
    if( r.at < r.length && r.text[r.at] == '}' )
        r.at++;
    else
    {
        for( ;; )
        {
            struct AgentJsonMember* member;

            if( doc->member_count >= AGENT_JSON_MEMBERS_MAX )
                return jr_fail(&r, "more members than a request may carry");
            member = &doc->members[doc->member_count];
            if( !jr_string(&r, &member->key) )
                return 0;
            /* A name given twice has no one value: refused, rather than the
             * first or the last quietly winning. */
            for( int i = 0; i < doc->member_count; i++ )
                if( strcmp(doc->members[i].key, member->key) == 0 )
                    return jr_fail(&r, "a name appears twice in the object, ending");
            jr_space(&r);
            if( r.at >= r.length || r.text[r.at] != ':' )
                return jr_fail(&r, "expected ':' after a name");
            r.at++;
            jr_space(&r);
            if( !jr_value(&r, &member->value) )
                return 0;
            doc->member_count++;
            jr_space(&r);
            if( r.at < r.length && r.text[r.at] == ',' )
            {
                r.at++;
                jr_space(&r);
                continue;
            }
            if( r.at < r.length && r.text[r.at] == '}' )
            {
                r.at++;
                break;
            }
            return jr_fail(&r, "expected ',' or '}'");
        }
    }
    jr_space(&r);
    if( r.at != r.length )
        return jr_fail(&r, "text after the closing '}' (one object per line)");
    return 1;
}

void
AgentJson_Free(struct AgentJsonDoc* doc)
{
    if( !doc )
        return;
    ToriMem_Free(ToriMem_Net(), doc->pool);
    doc->pool = NULL;
    doc->pool_used = 0;
    doc->pool_capacity = 0;
    doc->member_count = 0;
    doc->element_count = 0;
}

const struct AgentJsonValue*
AgentJson_Get(
    const struct AgentJsonDoc* doc,
    const char* key)
{
    assert(doc);
    assert(key);
    for( int i = 0; i < doc->member_count; i++ )
        if( strcmp(doc->members[i].key, key) == 0 )
            return &doc->members[i].value;
    return NULL;
}

const struct AgentJsonValue*
AgentJson_At(
    const struct AgentJsonDoc* doc,
    const struct AgentJsonValue* array,
    int index)
{
    assert(doc);
    assert(array);
    assert(array->type == AGENT_JSON_ARRAY);
    assert(index >= 0);
    assert(index < array->count);
    return &doc->elements[array->first + index];
}
