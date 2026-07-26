#ifndef BENCH_VEC_H
#define BENCH_VEC_H

/*
 * STB-style growable vector.
 *
 * Usage:
 *   struct BenchObject* v = NULL;
 *   bench_vec_push(v, obj);
 *   size_t n = bench_vec_len(v);
 *   ...
 *   bench_vec_free(v);
 *
 * The pointer returned to the user is the start of the element array.
 * Length and capacity live in a header immediately before that pointer.
 */

#include "bench_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct BenchVecHeader
{
    size_t len;
    size_t cap;
};

#define BENCH_VEC_HEADER(a) \
    ((struct BenchVecHeader*)((char*)(a) - sizeof(struct BenchVecHeader)))

#define bench_vec_len(a) ((a) ? BENCH_VEC_HEADER(a)->len : (size_t)0)
#define bench_vec_cap(a) ((a) ? BENCH_VEC_HEADER(a)->cap : (size_t)0)

#define bench_vec_free(a)                      \
    do                                         \
    {                                          \
        if( (a) )                              \
            free(BENCH_VEC_HEADER(a));         \
        (a) = NULL;                            \
    } while( 0 )

#define bench_vec_push(a, v)                                              \
    do                                                                    \
    {                                                                     \
        if( !(a) || bench_vec_len(a) + 1 > bench_vec_cap(a) )             \
        {                                                                 \
            size_t _need = (a) ? bench_vec_cap(a) * 2 : 8;                \
            if( _need < bench_vec_len(a) + 1 )                            \
                _need = bench_vec_len(a) + 1;                             \
            size_t _bytes =                                               \
                sizeof(struct BenchVecHeader) + _need * sizeof(*(a));     \
            struct BenchVecHeader* _h =                                   \
                (struct BenchVecHeader*)realloc(                          \
                    (a) ? (void*)BENCH_VEC_HEADER(a) : NULL, _bytes);     \
            if( !_h )                                                     \
                bench_die("bench_vec realloc failed");                    \
            if( !(a) )                                                    \
                _h->len = 0;                                              \
            _h->cap = _need;                                              \
            (a) = (void*)((char*)_h + sizeof(struct BenchVecHeader));     \
        }                                                                 \
        (a)[BENCH_VEC_HEADER(a)->len++] = (v);                            \
    } while( 0 )

#endif /* BENCH_VEC_H */
