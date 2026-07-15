#ifndef ENTITY_VEC_H
#define ENTITY_VEC_H

#include <stddef.h>
#include <stdint.h>

struct EntityVec
{
    uint8_t* data;
    int32_t count;
    int32_t capacity;
    int32_t max_count;
    size_t elem_size;
};

void
entity_vec_init(
    struct EntityVec* vec,
    size_t elem_size,
    int32_t max_count);

void
entity_vec_free(struct EntityVec* vec);

void*
entity_vec_at(
    struct EntityVec* vec,
    int32_t index);

void*
entity_vec_ensure(
    struct EntityVec* vec,
    int32_t index);

void*
entity_vec_push(struct EntityVec* vec);

int32_t
entity_vec_count(const struct EntityVec* vec);

void
entity_vec_reset(struct EntityVec* vec);

#define ENTITY_VEC_AT(vec, Type, idx) ((Type*)entity_vec_at(&(vec), (idx)))
#define ENTITY_VEC_ENSURE(vec, Type, idx) ((Type*)entity_vec_ensure(&(vec), (idx)))

#endif
