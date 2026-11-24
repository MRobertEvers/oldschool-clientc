#ifndef SRC_UI_UITREE_STRING_H
#define SRC_UI_UITREE_STRING_H

/*
 * UITree_String: a short UI-tree string that carries its own length.
 *
 * WHAT IT IS. A menu option's text (a component's ops, its op-base `option`,
 * its target verb and base, the submenu rows -- struct UITreeMenuOptions) as
 * `len` plus the bytes, NUL-terminated at text[len]. The same idea as the
 * CS2 VM's struct CS2VM2_Str (cs2vm2_str.h): whoever already knows the length
 * hands it over, and nobody measures again.
 *
 * WHAT IT REPLACES. These fields were `char[UITREE_MENU_OPTION_LEN]` written
 * with strncpy, which pads the whole 64 bytes with zeroes on every write --
 * 6,378 CC_SETOPs per bank open each cleared 64 bytes to store a word -- and
 * every reader that wanted the length (the menu builder, the plugin bridge,
 * the agent API) ran strlen over it again. A write now copies the bytes it was
 * given and one NUL; a read of the length is a load.
 *
 * WHY INLINE AND NOT ON THE TREE'S STRING POOL (UITreeStringPool,
 * uitree_pool.h). The text lives inside struct UITreeMenuOptions, a block the
 * tree already pools as a whole, copies by value (UITree_MenuOptionsSet,
 * a node spec's options, a checkpoint's copy) and reads on every menu build.
 * A pooled pointer per field would turn each of those copies into a deep copy
 * with its own release, and each read into a second cache line, to save 64
 * bytes the block already holds. Inline costs one byte per field.
 *
 * Capacity and truncation are what strncpy(dst, src, UITREE_STRING_CAP)
 * did: at most UITREE_STRING_CAP bytes are kept, the rest is dropped.
 *
 * Nothing here invalidates anything: a UITree_String is a value.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bytes of text a UITree_String holds (the old char[64] less its NUL). */
#define UITREE_STRING_CAP 63

struct UITree_String
{
    uint8_t len;
    char text[UITREE_STRING_CAP + 1];
};

/* The text, NUL-terminated; "" for a cleared or zeroed string. */
static inline char const*
UITree_StringText(struct UITree_String const* s)
{
    assert(s);
    return s->text;
}

static inline size_t
UITree_StringLen(struct UITree_String const* s)
{
    assert(s);
    return s->len;
}

static inline bool
UITree_StringEmpty(struct UITree_String const* s)
{
    assert(s);
    return s->len == 0;
}

static inline void
UITree_StringClear(struct UITree_String* s)
{
    assert(s);
    s->len = 0;
    s->text[0] = '\0';
}

/* Set from `len` bytes the caller has measured already (a CS2VM2_Str's text
 * and len, another UITree_String). No strlen, no padding. `bytes` may be NULL
 * only with len 0. */
static inline void
UITree_StringSet(
    struct UITree_String* s,
    char const* bytes,
    size_t len)
{
    assert(s);
    if( len > UITREE_STRING_CAP )
        len = UITREE_STRING_CAP;
    if( len > 0 )
    {
        assert(bytes);
        memmove(s->text, bytes, len);
    }
    s->text[len] = '\0';
    s->len = (uint8_t)len;
}

/* Set from a C string whose length nobody knows. Measures at most
 * UITREE_STRING_CAP + 1 bytes; NULL is "". For callers that hold only a
 * char* (cache decoders, plugins); a caller with a length uses
 * UITree_StringSet. */
static inline void
UITree_StringSetC(
    struct UITree_String* s,
    char const* cstr)
{
    size_t len = 0;
    assert(s);
    if( cstr )
    {
        while( len <= UITREE_STRING_CAP && cstr[len] != '\0' )
            len++;
    }
    UITree_StringSet(s, cstr, len);
}

static inline bool
UITree_StringEqualsBytes(
    struct UITree_String const* s,
    char const* bytes,
    size_t len)
{
    assert(s);
    if( len > UITREE_STRING_CAP )
        len = UITREE_STRING_CAP;
    return s->len == len && (len == 0 || memcmp(s->text, bytes, len) == 0);
}

static inline bool
UITree_StringEquals(
    struct UITree_String const* a,
    struct UITree_String const* b)
{
    assert(a);
    assert(b);
    return a->len == b->len && memcmp(a->text, b->text, a->len) == 0;
}

/* Equal to the C string `cstr` as UITree_StringSetC would store it. */
static inline bool
UITree_StringEqualsC(
    struct UITree_String const* s,
    char const* cstr)
{
    size_t len = 0;
    assert(s);
    if( cstr )
    {
        while( len <= UITREE_STRING_CAP && cstr[len] != '\0' )
            len++;
    }
    return UITree_StringEqualsBytes(s, cstr, len);
}

#endif
