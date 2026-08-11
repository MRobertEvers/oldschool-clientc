#ifndef TORIDRAW_MAP_H
#define TORIDRAW_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_MAP_OK 0
#define TORIDRAW_MAP_NOMEM 1
#define TORIDRAW_MAP_BADARG 2

typedef uint64_t (*ToriDraw_MapHashFn)(
    const void* key,
    size_t key_size,
    void* arg);

typedef int (*ToriDraw_MapEqFn)(
    const void* a,
    const void* b,
    size_t key_size,
    void* arg);

typedef int (*ToriDraw_MapIterableFn)(
    void* entry,
    void* arg);

enum ToriDraw_MapAction
{
    TORIDRAW_MAP_FIND,
    TORIDRAW_MAP_INSERT,
    TORIDRAW_MAP_REMOVE
};

struct ToriDraw_MapConfig
{
    void* buffer;
    size_t buffer_size;

    size_t key_size;
    size_t entry_size;

    size_t capacity;
    ToriDraw_MapHashFn hash_fn_nullable;
    ToriDraw_MapEqFn eq_fn_nullable;
    ToriDraw_MapIterableFn iterable_fn_nullable;
    void* arg_nullable;
};

struct ToriDraw_Map;

void*
ToriDraw_MapBufferPtr(struct ToriDraw_Map* h);

struct ToriDraw_Map*
ToriDraw_MapNew(
    const struct ToriDraw_MapConfig* config,
    uint32_t flags);

void
ToriDraw_MapFree(struct ToriDraw_Map* h);

void*
ToriDraw_MapSearch(
    struct ToriDraw_Map* h,
    const void* key,
    enum ToriDraw_MapAction action);

int
ToriDraw_MapResize(
    struct ToriDraw_Map* h,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer);

struct ToriDraw_MapIter;

struct ToriDraw_Map*
ToriDraw_MapIterGetMap(struct ToriDraw_MapIter* it);

struct ToriDraw_MapIter*
ToriDraw_MapIterNew(struct ToriDraw_Map* h);

void
ToriDraw_MapIterFree(struct ToriDraw_MapIter* it);

void*
ToriDraw_MapIterNext(struct ToriDraw_MapIter* it);

uint32_t
ToriDraw_MapCount(struct ToriDraw_Map* h);

uint32_t
ToriDraw_MapCapacity(struct ToriDraw_Map* h);

size_t
ToriDraw_MapEntrySize(struct ToriDraw_Map* h);

size_t
ToriDraw_MapBufferSizeFor(
    size_t entry_size,
    size_t count);

#endif
