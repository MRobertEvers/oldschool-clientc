#ifndef TORIDRAW_VEC_H
#define TORIDRAW_VEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define TORIDRAW_VEC_OK 0
#define TORIDRAW_VEC_NOMEM 1
#define TORIDRAW_VEC_BADARG 2
#define TORIDRAW_VEC_OUT_OF_BOUNDS 3

struct ToriDraw_Vec;

struct ToriDraw_Vec*
toridraw_vec_new(
    size_t element_size,
    size_t initial_capacity);

void
toridraw_vec_free(struct ToriDraw_Vec* v);

int
toridraw_vec_reserve(
    struct ToriDraw_Vec* v,
    size_t new_capacity);

int
toridraw_vec_resize(
    struct ToriDraw_Vec* v,
    size_t new_size);

int
toridraw_vec_shrink_to_fit(struct ToriDraw_Vec* v);

size_t
toridraw_vec_size(const struct ToriDraw_Vec* v);

size_t
toridraw_vec_capacity(const struct ToriDraw_Vec* v);

bool
toridraw_vec_empty(const struct ToriDraw_Vec* v);

void*
toridraw_vec_get(
    const struct ToriDraw_Vec* v,
    size_t index);

int
toridraw_vec_set(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element);

int
toridraw_vec_push(
    struct ToriDraw_Vec* v,
    const void* element);

int
toridraw_vec_pop(
    struct ToriDraw_Vec* v,
    void* out_element);

int
toridraw_vec_insert(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element);

int
toridraw_vec_remove(
    struct ToriDraw_Vec* v,
    size_t index,
    void* out_element);

void
toridraw_vec_clear(struct ToriDraw_Vec* v);

int
toridraw_vec_append(
    struct ToriDraw_Vec* v,
    const void* elements,
    size_t count);

int
toridraw_vec_copy(
    const struct ToriDraw_Vec* src,
    struct ToriDraw_Vec* dst);

void*
toridraw_vec_data(const struct ToriDraw_Vec* v);

struct ToriDraw_VecIter;

struct ToriDraw_VecIter*
toridraw_vec_iter_new(struct ToriDraw_Vec* v);

void
toridraw_vec_iter_free(struct ToriDraw_VecIter* it);

void*
toridraw_vec_iter_next(struct ToriDraw_VecIter* it);

void
toridraw_vec_iter_reset(struct ToriDraw_VecIter* it);

#endif
