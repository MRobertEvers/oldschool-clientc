#include "vec.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct Vec
{
    unsigned char* buffer;          /* aligned buffer for elements */
    unsigned char* original_buffer; /* original buffer pointer (for external buffers) */
    size_t element_size;            /* size of each element */
    size_t size;                    /* current number of elements */
    size_t capacity;                /* current capacity */
    bool owns_buffer;               /* whether we allocated the buffer */
};

/*-----------------------------------------------------------
 * Helper functions
 *----------------------------------------------------------*/

static inline size_t
vec_align_up_size(
    size_t x,
    size_t align)
{
    if( align == 0 )
        return x;
    size_t rem = x % align;
    if( rem == 0 )
        return x;
    return x + (align - rem);
}

static inline unsigned char*
vec_align_up_ptr(
    unsigned char* p,
    size_t align)
{
    uintptr_t ip = (uintptr_t)p;
    uintptr_t aligned = (ip + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
    return (unsigned char*)aligned;
}

static inline void*
vec_element_at(
    const struct Vec* v,
    size_t index)
{
    return (void*)(v->buffer + index * v->element_size);
}

static inline int
vec_ensure_capacity(
    struct Vec* v,
    size_t required_capacity)
{
    if( required_capacity <= v->capacity )
        return VEC_OK;

    /* Grow capacity exponentially */
    size_t new_capacity = v->capacity == 0 ? 8 : v->capacity * 2;
    while( new_capacity < required_capacity )
        new_capacity *= 2;

    return vec_reserve(v, new_capacity);
}

/*-----------------------------------------------------------
 * Public API implementation
 *----------------------------------------------------------*/

int
vec_init(
    struct Vec* v,
    void* buffer,
    size_t buffer_size,
    size_t element_size,
    size_t initial_capacity)
{
    if( !v || !buffer || element_size == 0 )
        return VEC_BADARG;

    memset(v, 0, sizeof(*v));

    const size_t align = 16; /* same alignment as hmap */
    unsigned char* base = (unsigned char*)buffer;
    unsigned char* aligned = vec_align_up_ptr(base, align);

    if( aligned > base + buffer_size )
        return VEC_NOMEM;

    size_t remaining = (size_t)((base + buffer_size) - aligned);
    size_t needed = initial_capacity * element_size;

    if( needed > remaining )
        return VEC_NOMEM;

    v->buffer = aligned;
    v->original_buffer = buffer;
    v->element_size = element_size;
    v->size = 0;
    v->capacity = initial_capacity;
    v->owns_buffer = false;

    return VEC_OK;
}

struct Vec*
vec_new(
    size_t element_size,
    size_t initial_capacity)
{
    if( element_size == 0 )
        return NULL;

    struct Vec* v = malloc(sizeof(struct Vec));
    if( !v )
        return NULL;

    memset(v, 0, sizeof(struct Vec));

    size_t buffer_size = initial_capacity * element_size;
    if( buffer_size == 0 )
        buffer_size = 16; /* minimum buffer size */

    void* buffer = malloc(buffer_size);
    if( !buffer )
    {
        free(v);
        return NULL;
    }

    int status = vec_init(v, buffer, buffer_size, element_size, initial_capacity);
    if( status != VEC_OK )
    {
        free(buffer);
        free(v);
        return NULL;
    }

    v->owns_buffer = true;
    return v;
}

void
vec_free(struct Vec* v)
{
    if( !v )
        return;

    if( v->owns_buffer && v->original_buffer )
        free(v->original_buffer);

    free(v);
}

int
vec_reserve(
    struct Vec* v,
    size_t new_capacity)
{
    if( !v )
        return VEC_BADARG;

    if( new_capacity <= v->capacity )
        return VEC_OK;

    size_t new_buffer_size = new_capacity * v->element_size;
    void* new_buffer = malloc(new_buffer_size);
    if( !new_buffer )
        return VEC_NOMEM;

    /* Copy existing elements */
    if( v->size > 0 )
    {
        memcpy(new_buffer, v->buffer, v->size * v->element_size);
    }

    /* Free old buffer if we own it */
    if( v->owns_buffer && v->original_buffer )
        free(v->original_buffer);

    /* Update vector */
    v->buffer = new_buffer;
    v->original_buffer = new_buffer;
    v->capacity = new_capacity;
    v->owns_buffer = true;

    return VEC_OK;
}

int
vec_resize(
    struct Vec* v,
    size_t new_size)
{
    if( !v )
        return VEC_BADARG;

    if( new_size > v->capacity )
    {
        int status = vec_reserve(v, new_size);
        if( status != VEC_OK )
            return status;
    }

    /* If growing, zero out new elements */
    if( new_size > v->size )
    {
        size_t bytes_to_zero = (new_size - v->size) * v->element_size;
        memset(vec_element_at(v, v->size), 0, bytes_to_zero);
    }

    v->size = new_size;
    return VEC_OK;
}

int
vec_shrink_to_fit(struct Vec* v)
{
    if( !v || !v->owns_buffer )
        return VEC_BADARG;

    if( v->size == v->capacity )
        return VEC_OK;

    if( v->size == 0 )
    {
        free(v->original_buffer);
        v->buffer = NULL;
        v->original_buffer = NULL;
        v->capacity = 0;
        return VEC_OK;
    }

    size_t new_buffer_size = v->size * v->element_size;
    void* new_buffer = malloc(new_buffer_size);
    if( !new_buffer )
        return VEC_NOMEM;

    memcpy(new_buffer, v->buffer, v->size * v->element_size);

    free(v->original_buffer);

    v->buffer = new_buffer;
    v->original_buffer = new_buffer;
    v->capacity = v->size;

    return VEC_OK;
}

size_t
vec_size(const struct Vec* v)
{
    return v ? v->size : 0;
}

size_t
vec_capacity(const struct Vec* v)
{
    return v ? v->capacity : 0;
}

bool
vec_empty(const struct Vec* v)
{
    return !v || v->size == 0;
}

void*
vec_get(
    const struct Vec* v,
    size_t index)
{
    if( !v || index >= v->size )
        return NULL;

    return vec_element_at(v, index);
}

int
vec_set(
    struct Vec* v,
    size_t index,
    const void* element)
{
    if( !v || index >= v->size || !element )
        return VEC_BADARG;

    memcpy(vec_element_at(v, index), element, v->element_size);
    return VEC_OK;
}

int
vec_push(
    struct Vec* v,
    const void* element)
{
    if( !v || !element )
        return VEC_BADARG;

    int status = vec_ensure_capacity(v, v->size + 1);
    if( status != VEC_OK )
        return status;

    memcpy(vec_element_at(v, v->size), element, v->element_size);
    v->size++;

    return VEC_OK;
}

int
vec_pop(
    struct Vec* v,
    void* out_element)
{
    if( !v || v->size == 0 )
        return VEC_BADARG;

    v->size--;

    if( out_element )
        memcpy(out_element, vec_element_at(v, v->size), v->element_size);

    return VEC_OK;
}

int
vec_insert(
    struct Vec* v,
    size_t index,
    const void* element)
{
    if( !v || index > v->size || !element )
        return VEC_BADARG;

    int status = vec_ensure_capacity(v, v->size + 1);
    if( status != VEC_OK )
        return status;

    /* Shift elements to make room */
    if( index < v->size )
    {
        memmove(
            vec_element_at(v, index + 1),
            vec_element_at(v, index),
            (v->size - index) * v->element_size);
    }

    memcpy(vec_element_at(v, index), element, v->element_size);
    v->size++;

    return VEC_OK;
}

int
vec_remove(
    struct Vec* v,
    size_t index,
    void* out_element)
{
    if( !v || index >= v->size )
        return VEC_BADARG;

    if( out_element )
        memcpy(out_element, vec_element_at(v, index), v->element_size);

    /* Shift elements to fill the gap */
    if( index < v->size - 1 )
    {
        memmove(
            vec_element_at(v, index),
            vec_element_at(v, index + 1),
            (v->size - index - 1) * v->element_size);
    }

    v->size--;
    return VEC_OK;
}

void
vec_clear(struct Vec* v)
{
    if( v )
        v->size = 0;
}

int
vec_append(
    struct Vec* v,
    const void* elements,
    size_t count)
{
    if( !v || !elements || count == 0 )
        return VEC_BADARG;

    int status = vec_ensure_capacity(v, v->size + count);
    if( status != VEC_OK )
        return status;

    memcpy(vec_element_at(v, v->size), elements, count * v->element_size);
    v->size += count;

    return VEC_OK;
}

int
vec_copy(
    const struct Vec* src,
    struct Vec* dst)
{
    if( !src || !dst )
        return VEC_BADARG;

    if( src->element_size != dst->element_size )
        return VEC_BADARG;

    int status = vec_resize(dst, src->size);
    if( status != VEC_OK )
        return status;

    memcpy(dst->buffer, src->buffer, src->size * src->element_size);
    return VEC_OK;
}

void*
vec_data(const struct Vec* v)
{
    return v ? v->buffer : NULL;
}

/*-----------------------------------------------------------
 * Iterator implementation
 *----------------------------------------------------------*/

struct VecIter
{
    struct Vec* vec;
    size_t index;
};

struct VecIter*
vec_iter_new(struct Vec* v)
{
    if( !v )
        return NULL;

    struct VecIter* it = malloc(sizeof(struct VecIter));
    if( !it )
        return NULL;

    it->vec = v;
    it->index = 0;

    return it;
}

void
vec_iter_free(struct VecIter* it)
{
    free(it);
}

void*
vec_iter_next(struct VecIter* it)
{
    if( !it || !it->vec || it->index >= it->vec->size )
        return NULL;

    void* element = vec_element_at(it->vec, it->index);
    it->index++;

    return element;
}

void
vec_iter_reset(struct VecIter* it)
{
    if( it )
        it->index = 0;
}
