#ifndef JBASE37_H
#define JBASE37_H

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Think of the uint64_t as 8 bytes.
 *
 * @param s
 * @return uint64_t
 */
uint64_t
strtobase37(const char* s);

/**
 * A max of 12 base37 characters can be packed into 8 bytes.
 *
 * A value that is not a name at all -- 0, a multiple of 37, one that does not
 * fit in twelve characters, or a buffer too small to hold what it decodes to
 * -- comes back as BASE37_INVALID_NAME, which is what the reference's
 * `fromBase37` does and what the friend/ignore lists are written to compare
 * against.
 */
void
base37tostr(
    uint64_t l,
    char* buffer,
    int buffer_size);

/**
 * The string base37tostr writes when the value it was handed is NOT a name.
 *
 * It is a decoder's failure report wearing the shape of a name, which is the
 * whole trouble with it: every `if( name[0] )` and `if name ~= ""` in the
 * client sails straight past it, and it ends up printed, stored or -- the
 * defect this constant was named for -- used as a directory. Anything that
 * treats the answer as an IDENTITY rather than as text to show the player
 * must ask Base37_IsInvalidName first.
 */
#define BASE37_INVALID_NAME "invalid_name"

/**
 * Is this decoded string the failure report above rather than a name?
 *
 * @param name a NUL-terminated string from base37tostr.
 */
bool
Base37_IsInvalidName(const char* name);

#endif