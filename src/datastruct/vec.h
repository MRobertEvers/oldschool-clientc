#ifndef VEC_H
#define VEC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define VEC_OK 0
#define VEC_NOMEM 1
#define VEC_BADARG 2
#define VEC_OUT_OF_BOUNDS 3

struct Vec;

/* Constructor/Destructor */
struct Vec* vec_new(size_t element_size, size_t initial_capacity);
void vec_free(struct Vec* v);

/* Initialization with caller-provided buffer */
int vec_init(
    struct Vec* v, void* buffer, size_t buffer_size, size_t element_size, size_t initial_capacity);

/* Capacity operations */
int vec_reserve(struct Vec* v, size_t new_capacity);
int vec_resize(struct Vec* v, size_t new_size);
int vec_shrink_to_fit(struct Vec* v);

/* Size operations */
size_t vec_size(const struct Vec* v);
size_t vec_capacity(const struct Vec* v);
bool vec_empty(const struct Vec* v);

/* Element access */
void* vec_get(const struct Vec* v, size_t index);
int vec_set(struct Vec* v, size_t index, const void* element);

/* Element operations */
int vec_push(struct Vec* v, const void* element);
int vec_pop(struct Vec* v, void* out_element);
int vec_insert(struct Vec* v, size_t index, const void* element);
int vec_remove(struct Vec* v, size_t index, void* out_element);
void vec_clear(struct Vec* v);

/* Bulk operations */
int vec_append(struct Vec* v, const void* elements, size_t count);
int vec_copy(const struct Vec* src, struct Vec* dst);

/* Buffer access (for direct manipulation) */
void* vec_data(const struct Vec* v);

/**
 * Iterator for vector elements
 */
struct VecIter;

struct VecIter* vec_iter_new(struct Vec* v);
void vec_iter_free(struct VecIter* it);
void* vec_iter_next(struct VecIter* it);
void vec_iter_reset(struct VecIter* it);

#endif
