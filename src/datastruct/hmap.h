#ifndef HMAP_H
#define HMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HMAP_OK 0
#define HMAP_NOMEM 1
#define HMAP_BADARG 2

/*
 * Hash callback:
 *   - key points to the key field inside the entry
 *   - key_size is the size of the key bytes
 *   - arg is user-supplied context
 *
 * Must never return 0; if so, the table will map it to 1.
 */
typedef uint64_t (*hmap_hash_fn)(const void* key, size_t key_size, void* arg);

/*
 * Equality callback:
 *   - return nonzero if equal
 */
typedef int (*hmap_eq_fn)(const void* a, const void* b, size_t key_size, void* arg);

enum HMapAction
{
    HMAP_FIND,
    HMAP_INSERT,
    HMAP_REMOVE
};

struct HashConfig
{
    void* buffer;
    size_t buffer_size;

    size_t key_size;
    size_t entry_size;

    size_t capacity;
    hmap_hash_fn hash_fn_nullable;
    hmap_eq_fn eq_fn_nullable;
    void* arg_nullable;
};

struct HMap;

void* hmap_buffer_ptr(struct HMap* h);

struct HMap* hmap_new(const struct HashConfig* config, uint32_t flags);
void hmap_free(struct HMap* h);
void* hmap_search(struct HMap* h, const void* key, enum HMapAction action);
int hmap_resize(
    struct HMap* h,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer);

struct HMapIter;

struct HMapIter* hmap_iter_new(struct HMap* h);
void hmap_iter_free(struct HMapIter* it);
void* hmap_iter_begin(struct HMapIter* it);
void* hmap_iter_next(struct HMapIter* it);

#endif