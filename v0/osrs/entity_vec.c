#include "entity_vec.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
entity_vec_init(
    struct EntityVec* vec,
    size_t elem_size,
    int32_t max_count)
{
    assert(vec != NULL);
    assert(elem_size > 0);
    assert(max_count > 0);
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
    vec->max_count = max_count;
    vec->elem_size = elem_size;
}

void
entity_vec_free(struct EntityVec* vec)
{
    if( !vec )
        return;
    free(vec->data);
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
    vec->elem_size = 0;
    vec->max_count = 0;
}

void*
entity_vec_at(
    struct EntityVec* vec,
    int32_t index)
{
    assert(vec != NULL);
    assert(index >= 0 && index < vec->count);
    return vec->data + (size_t)index * vec->elem_size;
}

void*
entity_vec_ensure(
    struct EntityVec* vec,
    int32_t index)
{
    assert(vec != NULL);
    assert(index >= 0 && index < vec->max_count);

    if( index >= vec->capacity )
    {
        int32_t min_cap = index + 1;
        int32_t new_cap = vec->capacity == 0 ? 64 : vec->capacity * 2;
        if( new_cap < min_cap )
            new_cap = min_cap;
        if( new_cap > vec->max_count )
            new_cap = vec->max_count;
        assert(min_cap <= vec->max_count);

        size_t new_bytes = (size_t)new_cap * vec->elem_size;
        uint8_t* p = (uint8_t*)realloc(vec->data, new_bytes);
        assert(p != NULL);
        memset(
            p + (size_t)vec->capacity * vec->elem_size,
            0,
            (size_t)(new_cap - vec->capacity) * vec->elem_size);
        vec->data = p;
        vec->capacity = new_cap;
    }

    if( index >= vec->count )
        vec->count = index + 1;

    return vec->data + (size_t)index * vec->elem_size;
}

void*
entity_vec_push(struct EntityVec* vec)
{
    assert(vec != NULL);
    assert(vec->count <= vec->max_count);
    return entity_vec_ensure(vec, vec->count);
}

int32_t
entity_vec_count(const struct EntityVec* vec)
{
    assert(vec != NULL);
    return vec->count;
}

void
entity_vec_reset(struct EntityVec* vec)
{
    assert(vec != NULL);
    vec->count = 0;
}
