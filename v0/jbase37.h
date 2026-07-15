#ifndef JBASE37_H
#define JBASE37_H

#include <ctype.h>
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
 */
void
base37tostr(
    uint64_t l,
    char* buffer,
    int buffer_size);

#endif