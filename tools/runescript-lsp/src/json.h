#ifndef TOOLS_RUNESCRIPT_LSP_JSON_H
#define TOOLS_RUNESCRIPT_LSP_JSON_H

/*
 * A read-only JSON DOM, sized for LSP request bodies.
 *
 * Only the parse side is here: responses are written straight into a `struct
 * Buf` by the handler that knows their shape, which is both less code and less
 * indirection than building a tree to serialise it once.
 */

#include <stddef.h>

enum JsonKind
{
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

struct JsonValue
{
    enum JsonKind kind;
    int boolean;
    double number;
    char* string; /**< JSON_STRING: decoded UTF-8, NUL-terminated. */

    /** JSON_ARRAY and JSON_OBJECT. `keys` is NULL for arrays. */
    struct JsonValue** items;
    char** keys;
    int count;
};

/** NULL when `text` is not well-formed JSON. */
struct JsonValue*
Json_Parse(const char* text, size_t length);

void
Json_Free(struct JsonValue* value);

/** The member of an object, or NULL. Safe on NULL and on non-objects. */
const struct JsonValue*
Json_Get(const struct JsonValue* object, const char* key);

/** Walks a chain of object members: Json_Path(root, "params", "textDocument", NULL). */
const struct JsonValue*
Json_Path(const struct JsonValue* root, ...);

/** The nth element of an array, or NULL. */
const struct JsonValue*
Json_At(const struct JsonValue* array, int index);

const char*
Json_String(const struct JsonValue* value, const char* fallback);

double
Json_Number(const struct JsonValue* value, double fallback);

int
Json_Bool(const struct JsonValue* value, int fallback);

#endif
