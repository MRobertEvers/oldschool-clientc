#ifndef SRC_CONTENT_CONTENT_VALUE_H
#define SRC_CONTENT_CONTENT_VALUE_H

/*
 * The two marker values of the full-key config text, and the one escape that
 * keeps them apart from a string that happens to spell them.
 *
 * Every block of `configs/all.<type>` (and every NEW record anywhere in the
 * content tree) states every key of its type. A field the record does not set
 * is written with a marker instead of being left out:
 *
 *     name=default      the key is ABSENT -- behave exactly as if the line were
 *                       not there (the default, a skip, "no value")
 *     recol=empty       a LIST key that is PRESENT with zero entries
 *     name=\default     an ordinary string that reads "default"
 *
 * A marker is the only line for its key, and sits on the bare key of a list or
 * numbered family (`recol=default`, `param=empty`, never `param=foo,default`).
 * A rank-1 overlay block stays partial and may say `key=default` to CLEAR a
 * field a lower rank stated.
 *
 * The test is on the RAW value, after `key=value` is split and the reader's
 * own whitespace/comment trim, and BEFORE any unescape. That is why it is one
 * call at the split rather than a check at each consumer: a consumer that sees
 * `default` has already lost the information that it was a marker.
 *
 * The failure this exists to prevent is silent: `category=default` read by
 * `atoi` is category 0, `name=default` is an item named "default",
 * `param=default` is a param called "default", `op1=default` is a menu option
 * called "default". cachepack writes the markers
 * (3rd/rscache/tools/cachepack/cp_text.c, CP_VALUE_DEFAULT / CP_VALUE_EMPTY);
 * the C readers under src/ read them through this header. The spellings here
 * must match those, byte for byte.
 *
 * Header-only on purpose: the readers live in torirsserver (several link
 * lines), sscompile and the tests, and a .c file would have to be added to
 * every one of them.
 */

#include <assert.h>
#include <string.h>

#define CONTENT_VALUE_DEFAULT "default"
#define CONTENT_VALUE_EMPTY "empty"

enum ContentValueMarker
{
    /** An ordinary value. Use it. */
    CONTENT_VALUE_PLAIN = 0,
    /** `key=default`: the key is absent. Treat the line as missing. */
    CONTENT_VALUE_IS_DEFAULT,
    /** `key=empty`: a list key present with zero entries. */
    CONTENT_VALUE_IS_EMPTY,
};

/**
 * Classify a raw config value, and undo the marker escape in place.
 *
 * Returns CONTENT_VALUE_IS_DEFAULT / CONTENT_VALUE_IS_EMPTY for the two bare
 * markers. Anything else is CONTENT_VALUE_PLAIN, and if it was exactly
 * `\default` or `\empty` the leading backslash is removed so the reader sees
 * the literal word -- the only escape this change introduces, undone even by a
 * reader that unescapes nothing else. Every other byte is left as it was: a
 * reader that did not unescape `\\` or `\n` before still does not.
 */
static inline enum ContentValueMarker
ContentValue_Marker(char* value)
{
    assert(value);
    if( strcmp(value, CONTENT_VALUE_DEFAULT) == 0 )
        return CONTENT_VALUE_IS_DEFAULT;
    if( strcmp(value, CONTENT_VALUE_EMPTY) == 0 )
        return CONTENT_VALUE_IS_EMPTY;
    if( value[0] == '\\' && (strcmp(value + 1, CONTENT_VALUE_DEFAULT) == 0 ||
                             strcmp(value + 1, CONTENT_VALUE_EMPTY) == 0) )
        memmove(value, value + 1, strlen(value + 1) + 1);
    return CONTENT_VALUE_PLAIN;
}

/** Either marker: the line carries no value to apply. */
static inline int
ContentValue_IsMarker(enum ContentValueMarker marker)
{
    return marker != CONTENT_VALUE_PLAIN;
}

/**
 * The same test for a reader that matches `key=` by prefix and never splits
 * the line: `line` is the whole trimmed line, `key_length` the length of the
 * `key=` prefix it already matched (so `line + key_length` is the value).
 */
static inline enum ContentValueMarker
ContentValue_LineMarker(
    char* line,
    size_t key_length)
{
    assert(line);
    assert(strlen(line) >= key_length);
    return ContentValue_Marker(line + key_length);
}

#endif
