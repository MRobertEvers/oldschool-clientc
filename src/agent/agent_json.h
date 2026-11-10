#ifndef SRC_AGENT_AGENT_JSON_H
#define SRC_AGENT_AGENT_JSON_H

/**
 * The agent control server's JSON (docs/AGENT_API.md): one object per line
 * each way.
 *
 * A READER for exactly what a request is -- one flat object whose values are
 * strings, numbers, booleans, null, or arrays of those -- and a WRITER for
 * replies, which nest freely. Nothing else in the tree reads or writes JSON
 * at run time (3rd/ carries no JSON library), and a request has no use for
 * one: a nested object in a request is refused with a sentence rather than
 * parsed, so the protocol stays the shape a shell one-liner can type.
 *
 * Text out is always valid UTF-8 JSON: the client's own strings are a mix of
 * ASCII, UTF-8 (the keycap symbols) and CP1252 bytes typed into a field, so a
 * byte that is not part of a well-formed UTF-8 sequence is written as its
 * Latin-1 code point (\u00XX) instead of being passed through to break the
 * reader on the other end.
 *
 * Memory is ToriMem_Net()'s. Contract violations assert (CLAUDE.md).
 */

#include <stddef.h>

/* ---- a growable byte buffer ------------------------------------------------ */

struct AgentBuf
{
    char* data;
    size_t length;
    size_t capacity;
};

void
AgentBuf_Init(struct AgentBuf* buf);

/** Release the bytes. NULL is accepted (a deallocator). */
void
AgentBuf_Free(struct AgentBuf* buf);

void
AgentBuf_Append(
    struct AgentBuf* buf,
    const void* bytes,
    size_t count);

/** Drop the first `count` bytes (what a socket took, or a line just read). */
void
AgentBuf_Consume(
    struct AgentBuf* buf,
    size_t count);

/* ---- the writer -------------------------------------------------------------- */

/** Containers one reply may have open at once (the ui verb's deepest is 6). */
#define AGENT_JSON_DEPTH_MAX 16

struct AgentJsonWriter
{
    struct AgentBuf* out;
    int depth;
    /** Per open container: nothing written in it yet (no comma owed). */
    unsigned char first[AGENT_JSON_DEPTH_MAX];
    /** A key was just written: the next value is its, with no comma. */
    int after_key;
};

/** Write into `out`, appending after whatever it holds. */
void
AgentJsonW_Init(
    struct AgentJsonWriter* w,
    struct AgentBuf* out);

void
AgentJsonW_ObjBegin(struct AgentJsonWriter* w);

void
AgentJsonW_ObjEnd(struct AgentJsonWriter* w);

void
AgentJsonW_ArrBegin(struct AgentJsonWriter* w);

void
AgentJsonW_ArrEnd(struct AgentJsonWriter* w);

/** The next member's name; the value call that follows is its value. */
void
AgentJsonW_Key(
    struct AgentJsonWriter* w,
    const char* name);

void
AgentJsonW_Str(
    struct AgentJsonWriter* w,
    const char* text);

/** `count` bytes of `text` (a slice with no terminator of its own). */
void
AgentJsonW_StrN(
    struct AgentJsonWriter* w,
    const char* text,
    size_t count);

void
AgentJsonW_Int(
    struct AgentJsonWriter* w,
    long long value);

/** A finite number; NaN and the infinities (which JSON has no spelling for)
 *  are written as null. */
void
AgentJsonW_Dbl(
    struct AgentJsonWriter* w,
    double value);

void
AgentJsonW_Bool(
    struct AgentJsonWriter* w,
    int value);

void
AgentJsonW_Null(struct AgentJsonWriter* w);

/** A value that is already JSON text (the request's own id, echoed). */
void
AgentJsonW_Raw(
    struct AgentJsonWriter* w,
    const char* json);

/* Key and value in one call. */
void
AgentJsonW_KStr(
    struct AgentJsonWriter* w,
    const char* name,
    const char* text);

void
AgentJsonW_KInt(
    struct AgentJsonWriter* w,
    const char* name,
    long long value);

void
AgentJsonW_KDbl(
    struct AgentJsonWriter* w,
    const char* name,
    double value);

void
AgentJsonW_KBool(
    struct AgentJsonWriter* w,
    const char* name,
    int value);

/** "name":[x,y,w,h] -- every box in the API is spelled this way. */
void
AgentJsonW_KRect(
    struct AgentJsonWriter* w,
    const char* name,
    int x,
    int y,
    int width,
    int height);

/** "name":[a,b] and "name":[a,b,c] (a point; a tile and its level). */
void
AgentJsonW_KInts(
    struct AgentJsonWriter* w,
    const char* name,
    const int* values,
    int count);

/* ---- the reader -------------------------------------------------------------- */

enum AgentJsonType
{
    AGENT_JSON_NULL = 0,
    AGENT_JSON_BOOL,
    AGENT_JSON_NUMBER,
    AGENT_JSON_STRING,
    AGENT_JSON_ARRAY,
};

/** Members one request may carry, and array elements across all of them. */
#define AGENT_JSON_MEMBERS_MAX 32
#define AGENT_JSON_ELEMENTS_MAX 64

struct AgentJsonValue
{
    /** enum AgentJsonType. */
    int type;
    int boolean;
    double number;
    /** STRING: decoded, NUL-terminated, in the document's pool. */
    const char* string;
    /** ARRAY: its elements are elements[first .. first + count). */
    int first;
    int count;
};

struct AgentJsonMember
{
    const char* key;
    struct AgentJsonValue value;
};

struct AgentJsonDoc
{
    struct AgentJsonMember members[AGENT_JSON_MEMBERS_MAX];
    int member_count;
    struct AgentJsonValue elements[AGENT_JSON_ELEMENTS_MAX];
    int element_count;
    /** Every decoded key and string. Owned; freed by AgentJson_Free. */
    char* pool;
    size_t pool_used;
    size_t pool_capacity;
};

/**
 * Parse `length` bytes of `text` as one flat object into `doc` (which need
 * not be initialised; it is overwritten). Returns 1, or 0 with one sentence in
 * `error` saying what was wrong and where. Either way `doc` then owns a pool:
 * call AgentJson_Free on it.
 */
int
AgentJson_Parse(
    struct AgentJsonDoc* doc,
    const char* text,
    size_t length,
    char* error,
    size_t error_size);

/** Release the pool. NULL is accepted (a deallocator). */
void
AgentJson_Free(struct AgentJsonDoc* doc);

/** The member named `key`, or NULL when the object has none. */
const struct AgentJsonValue*
AgentJson_Get(
    const struct AgentJsonDoc* doc,
    const char* key);

/** Element `index` of an ARRAY value. Asserts the type and the range. */
const struct AgentJsonValue*
AgentJson_At(
    const struct AgentJsonDoc* doc,
    const struct AgentJsonValue* array,
    int index);

#endif
