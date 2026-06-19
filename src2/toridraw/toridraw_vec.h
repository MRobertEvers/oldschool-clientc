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
ToriDraw_VecNew(
    size_t element_size,
    size_t initial_capacity);

void
ToriDraw_VecFree(struct ToriDraw_Vec* v);

int
ToriDraw_VecReserve(
    struct ToriDraw_Vec* v,
    size_t new_capacity);

int
ToriDraw_VecResize(
    struct ToriDraw_Vec* v,
    size_t new_size);

int
ToriDraw_VecShrinkToFit(struct ToriDraw_Vec* v);

size_t
ToriDraw_VecSize(const struct ToriDraw_Vec* v);

size_t
ToriDraw_VecCapacity(const struct ToriDraw_Vec* v);

bool
ToriDraw_VecEmpty(const struct ToriDraw_Vec* v);

void*
ToriDraw_VecGet(
    const struct ToriDraw_Vec* v,
    size_t index);

int
ToriDraw_VecSet(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element);

int
ToriDraw_VecPush(
    struct ToriDraw_Vec* v,
    const void* element);

int
ToriDraw_VecPop(
    struct ToriDraw_Vec* v,
    void* out_element);

int
ToriDraw_VecInsert(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element);

int
ToriDraw_VecRemove(
    struct ToriDraw_Vec* v,
    size_t index,
    void* out_element);

void
ToriDraw_VecClear(struct ToriDraw_Vec* v);

int
ToriDraw_VecAppend(
    struct ToriDraw_Vec* v,
    const void* elements,
    size_t count);

int
ToriDraw_VecCopy(
    const struct ToriDraw_Vec* src,
    struct ToriDraw_Vec* dst);

void*
ToriDraw_VecData(const struct ToriDraw_Vec* v);

struct ToriDraw_VecIter;

struct ToriDraw_VecIter*
ToriDraw_VecIterNew(struct ToriDraw_Vec* v);

void
ToriDraw_VecIterFree(struct ToriDraw_VecIter* it);

void*
ToriDraw_VecIterNext(struct ToriDraw_VecIter* it);

void
ToriDraw_VecIterReset(struct ToriDraw_VecIter* it);

#endif
