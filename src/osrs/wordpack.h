#ifndef WORDPACK_H
#define WORDPACK_H

#include "osrs/rscache/shared/rscache_shared_rs_buffer.h"

#include <stddef.h>

/**
 * WordPack: RuneScape chat text compression.
 * Client-TS WordPack.ts - pack/unpack for MESSAGE_PUBLIC and MESSAGE_PRIVATE.
 */

void
wordpack_pack(
    struct RSCacheShared_RSBuffer* buffer,
    const char* str);

char*
wordpack_unpack(
    struct RSCacheShared_RSBuffer* buffer,
    int length);

#endif
