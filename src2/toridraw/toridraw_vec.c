#include "toridraw_vec.h"

#include <string.h>

struct ToriDraw_Vec
{
    unsigned char* data;
    size_t element_size;
    size_t size;
    size_t capacity;
};

static inline void*
toridraw_vec_element_at(
    const struct ToriDraw_Vec* v,
    size_t index)
{
    return (void*)(v->data + index * v->element_size);
}

static int
toridraw_vec_ensure_capacity(
    struct ToriDraw_Vec* v,
    size_t required_capacity)
{
    if( required_capacity <= v->capacity )
        return TORIDRAW_VEC_OK;

    size_t new_capacity = v->capacity == 0 ? 8 : v->capacity * 2;
    while( new_capacity < required_capacity )
        new_capacity *= 2;

    return toridraw_vec_reserve(v, new_capacity);
}

struct ToriDraw_Vec*
toridraw_vec_new(
    size_t element_size,
    size_t initial_capacity)
{
    if( element_size == 0 )
        return NULL;

    struct ToriDraw_Vec* v = malloc(sizeof(struct ToriDraw_Vec));
    if( !v )
        return NULL;

    v->element_size = element_size;
    v->size = 0;
    v->capacity = initial_capacity;

    if( initial_capacity > 0 )
    {
        v->data = malloc(initial_capacity * element_size);
        if( !v->data )
        {
            free(v);
            return NULL;
        }
    }
    else
    {
        v->data = NULL;
    }

    return v;
}

void
toridraw_vec_free(struct ToriDraw_Vec* v)
{
    if( !v )
        return;

    free(v->data);
    free(v);
}

int
toridraw_vec_reserve(
    struct ToriDraw_Vec* v,
    size_t new_capacity)
{
    if( !v )
        return TORIDRAW_VEC_BADARG;

    if( new_capacity <= v->capacity )
        return TORIDRAW_VEC_OK;

    size_t new_bytes = new_capacity * v->element_size;
    unsigned char* new_data = realloc(v->data, new_bytes);
    if( !new_data )
        return TORIDRAW_VEC_NOMEM;

    v->data = new_data;
    v->capacity = new_capacity;
    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_resize(
    struct ToriDraw_Vec* v,
    size_t new_size)
{
    if( !v )
        return TORIDRAW_VEC_BADARG;

    if( new_size > v->capacity )
    {
        int status = toridraw_vec_reserve(v, new_size);
        if( status != TORIDRAW_VEC_OK )
            return status;
    }

    if( new_size > v->size )
    {
        size_t bytes_to_zero = (new_size - v->size) * v->element_size;
        memset(toridraw_vec_element_at(v, v->size), 0, bytes_to_zero);
    }

    v->size = new_size;
    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_shrink_to_fit(struct ToriDraw_Vec* v)
{
    if( !v )
        return TORIDRAW_VEC_BADARG;

    if( v->size == v->capacity )
        return TORIDRAW_VEC_OK;

    if( v->size == 0 )
    {
        free(v->data);
        v->data = NULL;
        v->capacity = 0;
        return TORIDRAW_VEC_OK;
    }

    size_t new_bytes = v->size * v->element_size;
    unsigned char* new_data = realloc(v->data, new_bytes);
    if( !new_data )
        return TORIDRAW_VEC_NOMEM;

    v->data = new_data;
    v->capacity = v->size;
    return TORIDRAW_VEC_OK;
}

size_t
toridraw_vec_size(const struct ToriDraw_Vec* v)
{
    return v ? v->size : 0;
}

size_t
toridraw_vec_capacity(const struct ToriDraw_Vec* v)
{
    return v ? v->capacity : 0;
}

bool
toridraw_vec_empty(const struct ToriDraw_Vec* v)
{
    return !v || v->size == 0;
}

void*
toridraw_vec_get(
    const struct ToriDraw_Vec* v,
    size_t index)
{
    if( !v || index >= v->size )
        return NULL;

    return toridraw_vec_element_at(v, index);
}

int
toridraw_vec_set(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element)
{
    if( !v || index >= v->size || !element )
        return TORIDRAW_VEC_BADARG;

    memcpy(toridraw_vec_element_at(v, index), element, v->element_size);
    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_push(
    struct ToriDraw_Vec* v,
    const void* element)
{
    if( !v || !element )
        return TORIDRAW_VEC_BADARG;

    int status = toridraw_vec_ensure_capacity(v, v->size + 1);
    if( status != TORIDRAW_VEC_OK )
        return status;

    memcpy(toridraw_vec_element_at(v, v->size), element, v->element_size);
    v->size++;

    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_pop(
    struct ToriDraw_Vec* v,
    void* out_element)
{
    if( !v || v->size == 0 )
        return TORIDRAW_VEC_BADARG;

    v->size--;

    if( out_element )
        memcpy(out_element, toridraw_vec_element_at(v, v->size), v->element_size);

    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_insert(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element)
{
    if( !v || index > v->size || !element )
        return TORIDRAW_VEC_BADARG;

    int status = toridraw_vec_ensure_capacity(v, v->size + 1);
    if( status != TORIDRAW_VEC_OK )
        return status;

    if( index < v->size )
    {
        memmove(
            toridraw_vec_element_at(v, index + 1),
            toridraw_vec_element_at(v, index),
            (v->size - index) * v->element_size);
    }

    memcpy(toridraw_vec_element_at(v, index), element, v->element_size);
    v->size++;

    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_remove(
    struct ToriDraw_Vec* v,
    size_t index,
    void* out_element)
{
    if( !v || index >= v->size )
        return TORIDRAW_VEC_BADARG;

    if( out_element )
        memcpy(out_element, toridraw_vec_element_at(v, index), v->element_size);

    if( index < v->size - 1 )
    {
        memmove(
            toridraw_vec_element_at(v, index),
            toridraw_vec_element_at(v, index + 1),
            (v->size - index - 1) * v->element_size);
    }

    v->size--;
    return TORIDRAW_VEC_OK;
}

void
toridraw_vec_clear(struct ToriDraw_Vec* v)
{
    if( v )
        v->size = 0;
}

int
toridraw_vec_append(
    struct ToriDraw_Vec* v,
    const void* elements,
    size_t count)
{
    if( !v || !elements || count == 0 )
        return TORIDRAW_VEC_BADARG;

    int status = toridraw_vec_ensure_capacity(v, v->size + count);
    if( status != TORIDRAW_VEC_OK )
        return status;

    memcpy(toridraw_vec_element_at(v, v->size), elements, count * v->element_size);
    v->size += count;

    return TORIDRAW_VEC_OK;
}

int
toridraw_vec_copy(
    const struct ToriDraw_Vec* src,
    struct ToriDraw_Vec* dst)
{
    if( !src || !dst )
        return TORIDRAW_VEC_BADARG;

    if( src->element_size != dst->element_size )
        return TORIDRAW_VEC_BADARG;

    int status = toridraw_vec_resize(dst, src->size);
    if( status != TORIDRAW_VEC_OK )
        return status;

    if( src->size > 0 )
        memcpy(dst->data, src->data, src->size * src->element_size);

    return TORIDRAW_VEC_OK;
}

void*
toridraw_vec_data(const struct ToriDraw_Vec* v)
{
    return v ? v->data : NULL;
}

struct ToriDraw_VecIter
{
    struct ToriDraw_Vec* vec;
    size_t index;
};

struct ToriDraw_VecIter*
toridraw_vec_iter_new(struct ToriDraw_Vec* v)
{
    if( !v )
        return NULL;

    struct ToriDraw_VecIter* it = malloc(sizeof(struct ToriDraw_VecIter));
    if( !it )
        return NULL;

    it->vec = v;
    it->index = 0;
    return it;
}

void
toridraw_vec_iter_free(struct ToriDraw_VecIter* it)
{
    free(it);
}

void*
toridraw_vec_iter_next(struct ToriDraw_VecIter* it)
{
    if( !it || !it->vec || it->index >= it->vec->size )
        return NULL;

    void* element = toridraw_vec_element_at(it->vec, it->index);
    it->index++;
    return element;
}

void
toridraw_vec_iter_reset(struct ToriDraw_VecIter* it)
{
    if( it )
        it->index = 0;
}
