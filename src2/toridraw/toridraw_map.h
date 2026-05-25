#ifndef TORIDRAW_MAP_H
#define TORIDRAW_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_MAP_OK 0
#define TORIDRAW_MAP_NOMEM 1
#define TORIDRAW_MAP_BADARG 2

typedef uint64_t (*toridraw_map_hash_fn)(
    const void* key,
    size_t key_size,
    void* arg);

typedef int (*toridraw_map_eq_fn)(
    const void* a,
    const void* b,
    size_t key_size,
    void* arg);

typedef int (*toridraw_map_iterable_fn)(
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
    toridraw_map_hash_fn hash_fn_nullable;
    toridraw_map_eq_fn eq_fn_nullable;
    toridraw_map_iterable_fn iterable_fn_nullable;
    void* arg_nullable;
};

struct ToriDraw_Map;

void*
toridraw_map_buffer_ptr(struct ToriDraw_Map* h);

struct ToriDraw_Map*
toridraw_map_new(
    const struct ToriDraw_MapConfig* config,
    uint32_t flags);

void
toridraw_map_free(struct ToriDraw_Map* h);

void*
toridraw_map_search(
    struct ToriDraw_Map* h,
    const void* key,
    enum ToriDraw_MapAction action);

int
toridraw_map_resize(
    struct ToriDraw_Map* h,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer);

struct ToriDraw_MapIter;

struct ToriDraw_Map*
toridraw_map_iter_get_map(struct ToriDraw_MapIter* it);

struct ToriDraw_MapIter*
toridraw_map_iter_new(struct ToriDraw_Map* h);

void
toridraw_map_iter_free(struct ToriDraw_MapIter* it);

void*
toridraw_map_iter_next(struct ToriDraw_MapIter* it);

uint32_t
toridraw_map_count(struct ToriDraw_Map* h);

uint32_t
toridraw_map_capacity(struct ToriDraw_Map* h);

size_t
toridraw_map_entry_size(struct ToriDraw_Map* h);

size_t
toridraw_map_buffer_size_for(
    size_t entry_size,
    size_t count);

#endif
