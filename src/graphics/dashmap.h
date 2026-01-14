#ifndef DASHMAP_H
#define DASHMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DASHMAP_OK 0
#define DASHMAP_NOMEM 1
#define DASHMAP_BADARG 2

/*
 * Hash callback:
 *   - key points to the key field inside the entry
 *   - key_size is the size of the key bytes
 *   - arg is user-supplied context
 *
 * Must never return 0; if so, the table will map it to 1.
 */
typedef uint64_t (*dashmap_hash_fn)(
    const void* key,
    size_t key_size,
    void* arg);

/*
 * Equality callback:
 *   - return nonzero if equal
 */
typedef int (*dashmap_eq_fn)(
    const void* a,
    const void* b,
    size_t key_size,
    void* arg);

typedef int (*dashmap_iterable_fn)(
    void* entry,
    void* arg);

enum DashMapAction
{
    DASHMAP_FIND,
    DASHMAP_INSERT,
    DASHMAP_REMOVE
};

struct DashMapConfig
{
    void* buffer;
    size_t buffer_size;

    size_t key_size;
    size_t entry_size;

    size_t capacity;
    dashmap_hash_fn hash_fn_nullable;
    dashmap_eq_fn eq_fn_nullable;
    dashmap_iterable_fn iterable_fn_nullable;
    void* arg_nullable;
};

struct DashMap;

void*
dashmap_buffer_ptr(struct DashMap* h);

struct DashMap*
dashmap_new(
    const struct DashMapConfig* config,
    uint32_t flags);
void
dashmap_free(struct DashMap* h);
void*
dashmap_search(
    struct DashMap* h,
    const void* key,
    enum DashMapAction action);
int
dashmap_resize(
    struct DashMap* h,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer);

/**
 * Iterate map elements
 */
struct DashMapIter;

struct DashMap*
dashmap_iter_get_map(struct DashMapIter* it);

struct DashMapIter*
dashmap_iter_new(struct DashMap* h);
void
dashmap_iter_free(struct DashMapIter* it);
void*
dashmap_iter_next(struct DashMapIter* it);

#endif