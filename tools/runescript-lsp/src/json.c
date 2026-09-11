#include "json.h"

#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct Parser
{
    const char* text;
    size_t length;
    size_t pos;
    int failed;
};

static struct JsonValue*
parse_value(struct Parser* parser);

static struct JsonValue*
new_value(enum JsonKind kind)
{
    struct JsonValue* value = (struct JsonValue*)calloc(1, sizeof(*value));

    assert(value);
    value->kind = kind;
    return value;
}

static void
skip_space(struct Parser* parser)
{
    while( parser->pos < parser->length )
    {
        char c = parser->text[parser->pos];

        if( c == ' ' || c == '\t' || c == '\n' || c == '\r' )
            parser->pos++;
        else
            break;
    }
}

static int
peek(struct Parser* parser)
{
    return parser->pos < parser->length ? (unsigned char)parser->text[parser->pos] : -1;
}

static int
expect(struct Parser* parser, char c)
{
    if( peek(parser) != c )
    {
        parser->failed = 1;
        return 0;
    }
    parser->pos++;
    return 1;
}

/** Append one code point to `out` as UTF-8. */
static void
append_utf8(struct Buf* out, unsigned int code_point)
{
    if( code_point < 0x80 )
    {
        Buf_AppendChar(out, (char)code_point);
    }
    else if( code_point < 0x800 )
    {
        Buf_AppendChar(out, (char)(0xC0 | (code_point >> 6)));
        Buf_AppendChar(out, (char)(0x80 | (code_point & 0x3F)));
    }
    else if( code_point < 0x10000 )
    {
        Buf_AppendChar(out, (char)(0xE0 | (code_point >> 12)));
        Buf_AppendChar(out, (char)(0x80 | ((code_point >> 6) & 0x3F)));
        Buf_AppendChar(out, (char)(0x80 | (code_point & 0x3F)));
    }
    else
    {
        Buf_AppendChar(out, (char)(0xF0 | (code_point >> 18)));
        Buf_AppendChar(out, (char)(0x80 | ((code_point >> 12) & 0x3F)));
        Buf_AppendChar(out, (char)(0x80 | ((code_point >> 6) & 0x3F)));
        Buf_AppendChar(out, (char)(0x80 | (code_point & 0x3F)));
    }
}

static int
read_hex4(struct Parser* parser, unsigned int* out)
{
    unsigned int value = 0;
    int i;

    for( i = 0; i < 4; i++ )
    {
        int c = peek(parser);

        if( c >= '0' && c <= '9' )
            value = value * 16 + (unsigned int)(c - '0');
        else if( c >= 'a' && c <= 'f' )
            value = value * 16 + (unsigned int)(c - 'a' + 10);
        else if( c >= 'A' && c <= 'F' )
            value = value * 16 + (unsigned int)(c - 'A' + 10);
        else
            return 0;
        parser->pos++;
    }
    *out = value;
    return 1;
}

static char*
parse_string_body(struct Parser* parser)
{
    struct Buf out = { 0 };

    if( !expect(parser, '"') )
        return NULL;

    for( ;; )
    {
        int c = peek(parser);

        if( c < 0 )
        {
            parser->failed = 1;
            Buf_Free(&out);
            return NULL;
        }
        parser->pos++;

        if( c == '"' )
            break;

        if( c != '\\' )
        {
            Buf_AppendChar(&out, (char)c);
            continue;
        }

        c = peek(parser);
        parser->pos++;
        switch( c )
        {
        case '"':
            Buf_AppendChar(&out, '"');
            break;
        case '\\':
            Buf_AppendChar(&out, '\\');
            break;
        case '/':
            Buf_AppendChar(&out, '/');
            break;
        case 'b':
            Buf_AppendChar(&out, '\b');
            break;
        case 'f':
            Buf_AppendChar(&out, '\f');
            break;
        case 'n':
            Buf_AppendChar(&out, '\n');
            break;
        case 'r':
            Buf_AppendChar(&out, '\r');
            break;
        case 't':
            Buf_AppendChar(&out, '\t');
            break;
        case 'u':
        {
            unsigned int unit = 0;

            if( !read_hex4(parser, &unit) )
            {
                parser->failed = 1;
                Buf_Free(&out);
                return NULL;
            }
            /* A high surrogate carries only half a code point; the low half
             * is the next escape, and pairing them here is what keeps an
             * emoji in a document out of the index as two broken bytes. */
            if( unit >= 0xD800 && unit <= 0xDBFF && peek(parser) == '\\' &&
                parser->pos + 1 < parser->length && parser->text[parser->pos + 1] == 'u' )
            {
                unsigned int low = 0;

                parser->pos += 2;
                if( !read_hex4(parser, &low) )
                {
                    parser->failed = 1;
                    Buf_Free(&out);
                    return NULL;
                }
                if( low >= 0xDC00 && low <= 0xDFFF )
                    unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                else
                    append_utf8(&out, low);
            }
            append_utf8(&out, unit);
            break;
        }
        default:
            parser->failed = 1;
            Buf_Free(&out);
            return NULL;
        }
    }

    if( !out.data )
        Buf_AppendStr(&out, "");
    return out.data;
}

static struct JsonValue*
parse_object(struct Parser* parser)
{
    struct JsonValue* object = new_value(JSON_OBJECT);
    int capacity = 0;

    expect(parser, '{');
    skip_space(parser);
    if( peek(parser) == '}' )
    {
        parser->pos++;
        return object;
    }

    for( ;; )
    {
        char* key;
        struct JsonValue* value;

        skip_space(parser);
        key = parse_string_body(parser);
        if( !key )
        {
            parser->failed = 1;
            return object;
        }
        skip_space(parser);
        if( !expect(parser, ':') )
        {
            free(key);
            return object;
        }
        value = parse_value(parser);
        if( !value )
        {
            free(key);
            parser->failed = 1;
            return object;
        }

        if( object->count == capacity )
        {
            capacity = capacity ? capacity * 2 : 8;
            object->items =
                (struct JsonValue**)realloc(object->items,
                                            (size_t)capacity * sizeof(*object->items));
            object->keys = (char**)realloc(object->keys, (size_t)capacity * sizeof(*object->keys));
            assert(object->items);
            assert(object->keys);
        }
        object->keys[object->count] = key;
        object->items[object->count] = value;
        object->count++;

        skip_space(parser);
        if( peek(parser) == ',' )
        {
            parser->pos++;
            continue;
        }
        break;
    }
    expect(parser, '}');
    return object;
}

static struct JsonValue*
parse_array(struct Parser* parser)
{
    struct JsonValue* array = new_value(JSON_ARRAY);
    int capacity = 0;

    expect(parser, '[');
    skip_space(parser);
    if( peek(parser) == ']' )
    {
        parser->pos++;
        return array;
    }

    for( ;; )
    {
        struct JsonValue* value = parse_value(parser);

        if( !value )
        {
            parser->failed = 1;
            return array;
        }
        if( array->count == capacity )
        {
            capacity = capacity ? capacity * 2 : 8;
            array->items = (struct JsonValue**)realloc(
                array->items, (size_t)capacity * sizeof(*array->items));
            assert(array->items);
        }
        array->items[array->count++] = value;

        skip_space(parser);
        if( peek(parser) == ',' )
        {
            parser->pos++;
            continue;
        }
        break;
    }
    expect(parser, ']');
    return array;
}

static struct JsonValue*
parse_value(struct Parser* parser)
{
    int c;

    skip_space(parser);
    c = peek(parser);

    switch( c )
    {
    case '{':
        return parse_object(parser);
    case '[':
        return parse_array(parser);
    case '"':
    {
        struct JsonValue* value = new_value(JSON_STRING);

        value->string = parse_string_body(parser);
        if( !value->string )
        {
            Json_Free(value);
            return NULL;
        }
        return value;
    }
    case 't':
        if( parser->length - parser->pos >= 4 &&
            memcmp(parser->text + parser->pos, "true", 4) == 0 )
        {
            struct JsonValue* value = new_value(JSON_BOOL);

            value->boolean = 1;
            parser->pos += 4;
            return value;
        }
        parser->failed = 1;
        return NULL;
    case 'f':
        if( parser->length - parser->pos >= 5 &&
            memcmp(parser->text + parser->pos, "false", 5) == 0 )
        {
            parser->pos += 5;
            return new_value(JSON_BOOL);
        }
        parser->failed = 1;
        return NULL;
    case 'n':
        if( parser->length - parser->pos >= 4 &&
            memcmp(parser->text + parser->pos, "null", 4) == 0 )
        {
            parser->pos += 4;
            return new_value(JSON_NULL);
        }
        parser->failed = 1;
        return NULL;
    default:
        break;
    }

    if( c == '-' || (c >= '0' && c <= '9') )
    {
        struct JsonValue* value = new_value(JSON_NUMBER);
        char* end = NULL;

        value->number = strtod(parser->text + parser->pos, &end);
        if( end == parser->text + parser->pos )
        {
            Json_Free(value);
            parser->failed = 1;
            return NULL;
        }
        parser->pos = (size_t)(end - parser->text);
        return value;
    }

    parser->failed = 1;
    return NULL;
}

struct JsonValue*
Json_Parse(const char* text, size_t length)
{
    struct Parser parser = { 0 };
    struct JsonValue* root;

    assert(text);
    parser.text = text;
    parser.length = length;

    root = parse_value(&parser);
    if( !root || parser.failed )
    {
        Json_Free(root);
        return NULL;
    }
    return root;
}

void
Json_Free(struct JsonValue* value)
{
    int i;

    if( !value )
        return;

    for( i = 0; i < value->count; i++ )
    {
        Json_Free(value->items[i]);
        if( value->keys )
            free(value->keys[i]);
    }
    free(value->items);
    free(value->keys);
    free(value->string);
    free(value);
}

const struct JsonValue*
Json_Get(const struct JsonValue* object, const char* key)
{
    int i;

    assert(key);
    if( !object || object->kind != JSON_OBJECT )
        return NULL;
    for( i = 0; i < object->count; i++ )
    {
        if( strcmp(object->keys[i], key) == 0 )
            return object->items[i];
    }
    return NULL;
}

const struct JsonValue*
Json_Path(const struct JsonValue* root, ...)
{
    const struct JsonValue* value = root;
    va_list keys;
    const char* key;

    va_start(keys, root);
    while( (key = va_arg(keys, const char*)) != NULL )
        value = Json_Get(value, key);
    va_end(keys);
    return value;
}

const struct JsonValue*
Json_At(const struct JsonValue* array, int index)
{
    if( !array || array->kind != JSON_ARRAY )
        return NULL;
    if( index < 0 || index >= array->count )
        return NULL;
    return array->items[index];
}

const char*
Json_String(const struct JsonValue* value, const char* fallback)
{
    if( !value || value->kind != JSON_STRING )
        return fallback;
    return value->string;
}

double
Json_Number(const struct JsonValue* value, double fallback)
{
    if( !value || value->kind != JSON_NUMBER )
        return fallback;
    return value->number;
}

int
Json_Bool(const struct JsonValue* value, int fallback)
{
    if( !value || value->kind != JSON_BOOL )
        return fallback;
    return value->boolean;
}
