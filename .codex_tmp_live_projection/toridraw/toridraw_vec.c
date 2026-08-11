#include "toridraw_vec.h"

#include <assert.h>
#include <string.h>

struct ToriDraw_Vec
{
    unsigned char* data;
    size_t element_size;
    size_t size;
    size_t capacity;
};

static inline void*
ToriDraw_VecElementAt(
    const struct ToriDraw_Vec* v,
    size_t index)
{
    return (void*)(v->data + index * v->element_size);
}

static int
ToriDraw_VecEnsureCapacity(
    struct ToriDraw_Vec* v,
    size_t required_capacity)
{
    if( required_capacity <= v->capacity )
        return TORIDRAW_VEC_OK;

    size_t new_capacity = v->capacity == 0 ? 8 : v->capacity * 2;
    while( new_capacity < required_capacity )
        new_capacity *= 2;

    return ToriDraw_VecReserve(v, new_capacity);
}

struct ToriDraw_Vec*
ToriDraw_VecNew(
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
ToriDraw_VecFree(struct ToriDraw_Vec* v)
{
    if( !v )
        return;

    free(v->data);
    free(v);
}

int
ToriDraw_VecReserve(
    struct ToriDraw_Vec* v,
    size_t new_capacity)
{
    assert(v);

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
ToriDraw_VecResize(
    struct ToriDraw_Vec* v,
    size_t new_size)
{
    assert(v);

    if( new_size > v->capacity )
    {
        int status = ToriDraw_VecReserve(v, new_size);
        if( status != TORIDRAW_VEC_OK )
            return status;
    }

    if( new_size > v->size )
    {
        size_t bytes_to_zero = (new_size - v->size) * v->element_size;
        memset(ToriDraw_VecElementAt(v, v->size), 0, bytes_to_zero);
    }

    v->size = new_size;
    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecShrinkToFit(struct ToriDraw_Vec* v)
{
    assert(v);

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
ToriDraw_VecSize(const struct ToriDraw_Vec* v)
{
    return v ? v->size : 0;
}

size_t
ToriDraw_VecCapacity(const struct ToriDraw_Vec* v)
{
    return v ? v->capacity : 0;
}

bool
ToriDraw_VecEmpty(const struct ToriDraw_Vec* v)
{
    return !v || v->size == 0;
}

void*
ToriDraw_VecGet(
    const struct ToriDraw_Vec* v,
    size_t index)
{
    assert(v);
    if( index >= v->size )
        return NULL;

    return ToriDraw_VecElementAt(v, index);
}

int
ToriDraw_VecSet(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element)
{
    assert(v);
    assert(element);
    if( index >= v->size )
        return TORIDRAW_VEC_BADARG;

    memcpy(ToriDraw_VecElementAt(v, index), element, v->element_size);
    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecPush(
    struct ToriDraw_Vec* v,
    const void* element)
{
    assert(v);
    assert(element);

    int status = ToriDraw_VecEnsureCapacity(v, v->size + 1);
    if( status != TORIDRAW_VEC_OK )
        return status;

    memcpy(ToriDraw_VecElementAt(v, v->size), element, v->element_size);
    v->size++;

    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecPop(
    struct ToriDraw_Vec* v,
    void* out_element)
{
    assert(v);
    if( v->size == 0 )
        return TORIDRAW_VEC_BADARG;

    v->size--;

    if( out_element )
        memcpy(out_element, ToriDraw_VecElementAt(v, v->size), v->element_size);

    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecInsert(
    struct ToriDraw_Vec* v,
    size_t index,
    const void* element)
{
    assert(v);
    assert(element);
    if( index > v->size )
        return TORIDRAW_VEC_BADARG;

    int status = ToriDraw_VecEnsureCapacity(v, v->size + 1);
    if( status != TORIDRAW_VEC_OK )
        return status;

    if( index < v->size )
    {
        memmove(
            ToriDraw_VecElementAt(v, index + 1),
            ToriDraw_VecElementAt(v, index),
            (v->size - index) * v->element_size);
    }

    memcpy(ToriDraw_VecElementAt(v, index), element, v->element_size);
    v->size++;

    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecRemove(
    struct ToriDraw_Vec* v,
    size_t index,
    void* out_element)
{
    assert(v);
    if( index >= v->size )
        return TORIDRAW_VEC_BADARG;

    if( out_element )
        memcpy(out_element, ToriDraw_VecElementAt(v, index), v->element_size);

    if( index < v->size - 1 )
    {
        memmove(
            ToriDraw_VecElementAt(v, index),
            ToriDraw_VecElementAt(v, index + 1),
            (v->size - index - 1) * v->element_size);
    }

    v->size--;
    return TORIDRAW_VEC_OK;
}

void
ToriDraw_VecClear(struct ToriDraw_Vec* v)
{
    if( v )
        v->size = 0;
}

int
ToriDraw_VecAppend(
    struct ToriDraw_Vec* v,
    const void* elements,
    size_t count)
{
    assert(v);
    assert(elements);
    if( count == 0 )
        return TORIDRAW_VEC_BADARG;

    int status = ToriDraw_VecEnsureCapacity(v, v->size + count);
    if( status != TORIDRAW_VEC_OK )
        return status;

    memcpy(ToriDraw_VecElementAt(v, v->size), elements, count * v->element_size);
    v->size += count;

    return TORIDRAW_VEC_OK;
}

int
ToriDraw_VecCopy(
    const struct ToriDraw_Vec* src,
    struct ToriDraw_Vec* dst)
{
    assert(src);
    assert(dst);

    if( src->element_size != dst->element_size )
        return TORIDRAW_VEC_BADARG;

    int status = ToriDraw_VecResize(dst, src->size);
    if( status != TORIDRAW_VEC_OK )
        return status;

    if( src->size > 0 )
        memcpy(dst->data, src->data, src->size * src->element_size);

    return TORIDRAW_VEC_OK;
}

void*
ToriDraw_VecData(const struct ToriDraw_Vec* v)
{
    return v ? v->data : NULL;
}

struct ToriDraw_VecIter
{
    struct ToriDraw_Vec* vec;
    size_t index;
};

struct ToriDraw_VecIter*
ToriDraw_VecIterNew(struct ToriDraw_Vec* v)
{
    assert(v);

    struct ToriDraw_VecIter* it = malloc(sizeof(struct ToriDraw_VecIter));
    if( !it )
        return NULL;

    it->vec = v;
    it->index = 0;
    return it;
}

void
ToriDraw_VecIterFree(struct ToriDraw_VecIter* it)
{
    free(it);
}

void*
ToriDraw_VecIterNext(struct ToriDraw_VecIter* it)
{
    assert(it);
    assert(it->vec);
    if( it->index >= it->vec->size )
        return NULL;

    void* element = ToriDraw_VecElementAt(it->vec, it->index);
    it->index++;
    return element;
}

void
ToriDraw_VecIterReset(struct ToriDraw_VecIter* it)
{
    if( it )
        it->index = 0;
}
