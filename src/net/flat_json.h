#ifndef SRC_NET_FLAT_JSON_H
#define SRC_NET_FLAT_JSON_H

/*
 * One level of a JSON object, read by key: what an OAuth token response and a
 * JWT's claims are. Nested objects and arrays are skipped, not read; a document
 * that is not an object, or that is malformed before the key is reached, has no
 * keys at all.
 *
 * Not a JSON library and not meant to become one: the two callers (the
 * client's account flow, torirs_server_oauth.c) read a handful of flat string
 * and number fields from bodies this project's own servers write.
 */

#include <stddef.h>

/**
 * The string value of `key`, unescaped, into `out`. 1 when the key is present
 * with a string value that fits in `cap` (NUL included); 0 otherwise. A
 * `\uXXXX` escape outside ASCII decodes as '?'.
 */
int
FlatJson_String(
    const char* json,
    size_t len,
    const char* key,
    char* out,
    size_t cap);

/** The integer value of `key`. 1 when present as an integer; 0 otherwise. */
int
FlatJson_Int64(
    const char* json,
    size_t len,
    const char* key,
    long long* out);

#endif
