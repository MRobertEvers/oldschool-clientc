#ifndef WORDPACK_H
#define WORDPACK_H

#include <rsbuffer.h>

#include <stddef.h>

/**
 * WordPack: RuneScape chat text compression.
 * Client-TS WordPack.ts - pack/unpack for MESSAGE_PUBLIC and MESSAGE_PRIVATE.
 */

void
wordpack_pack(
    struct RSCache_Buffer* buffer,
    const char* str);

char*
wordpack_unpack(
    struct RSCache_Buffer* buffer,
    int length);

#endif
