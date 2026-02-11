#ifndef WORDPACK_H
#define WORDPACK_H

#include "rscache/rsbuf.h"

#include <stddef.h>

/**
 * WordPack: RuneScape chat text compression.
 * Client-TS WordPack.ts - pack/unpack for MESSAGE_PUBLIC and MESSAGE_PRIVATE.
 */

void
wordpack_pack(
    struct RSBuffer* buffer,
    const char* str);

char*
wordpack_unpack(
    struct RSBuffer* buffer,
    int length);

#endif
