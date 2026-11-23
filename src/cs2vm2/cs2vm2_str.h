#ifndef CS2VM2_STR_H
#define CS2VM2_STR_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A CS2 string: its length, the capacity of its buffer, and the text.
 *
 * Every string the VM handles -- on the operand stack, in a frame local, in a
 * string array's cells, a script's constant -- is one of these, so the VM
 * never scans for a NUL to learn a length. `text` is NUL-terminated as well, so
 * a host that only reads it hands `->text` on as an ordinary C string.
 *
 * Made only by the thread's pool (CS2VM2_StrAlloc / _StrDup / _StrDupLen /
 * _StrFmt / _StrEmpty in cs2vm2.h; see cs2vm2_strpool.h for the lifetime) or,
 * for a script's constants, by the script (cs2vm2_script.h). A C string from
 * anywhere else goes in through CS2VM2_StrDup.
 *
 *   len  characters in `text`, not counting the NUL.
 *   cap  characters `text` can hold, not counting the NUL. An op that reserves
 *        more than it writes (ESCAPE, REMOVETAGS) says what it wrote with
 *        CS2VM2_StrSetLen.
 *
 * NULL is a string too: a string local never assigned, a host that answers
 * "no value". Readers treat it as "".
 */
struct CS2VM2_Str
{
    uint32_t len;
    uint32_t cap;
    char text[];
};

/* `s` now holds `len` characters (no more than its capacity): set the length
 * and terminate there. */
static inline void
CS2VM2_StrSetLen(
    struct CS2VM2_Str* s,
    uint32_t len)
{
    assert(s);
    assert(len <= s->cap);
    s->len = len;
    s->text[len] = '\0';
}

/* The text of `s`, "" for NULL: what a host request or a C API is handed. */
static inline char const*
CS2VM2_StrText(struct CS2VM2_Str const* s)
{
    return s ? s->text : "";
}

/* The text of `s`, NULL for NULL: for a C API that tells "no value" from "". */
static inline char const*
CS2VM2_StrCStr(struct CS2VM2_Str const* s)
{
    return s ? s->text : NULL;
}

/* The length of `s`, 0 for NULL. */
static inline uint32_t
CS2VM2_StrLen(struct CS2VM2_Str const* s)
{
    return s ? s->len : 0u;
}

#endif /* CS2VM2_STR_H */
