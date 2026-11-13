#ifndef SRC_REPLAY_REPLAY_CHECKPOINT_UTIL_H
#define SRC_REPLAY_REPLAY_CHECKPOINT_UTIL_H

/*
 * Small helpers shared by every module's session-checkpoint functions
 * (replay/replay_checkpoint.h). Header-only on purpose: the modules that own
 * the state (varps, invs, the UI tree, the CS2 host, ...) are linked into test
 * binaries that do not link the replay module, and a checkpoint function in
 * one of them must not drag a new object in.
 *
 * A checkpoint is a same-process, same-build COPY: arrays are duplicated
 * whole (capacity included, so a restore leaves the live store exactly the
 * shape it had), strings are duplicated, and nothing is serialised.
 */

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** `bytes` of `src` in `group`, or NULL for a NULL source. */
static inline void*
Ckpt_Dup(
    struct ToriMem_Group* group,
    void const* src,
    size_t bytes)
{
    if( !src )
        return NULL;
    return ToriMem_Memdup(group, src, bytes);
}

/** A string copy in `group`, or NULL for a NULL source. */
static inline char*
Ckpt_StrDup(
    struct ToriMem_Group* group,
    char const* src)
{
    if( !src )
        return NULL;
    return ToriMem_Strdup(group, src);
}

/** Write the first difference found, once (a `why` already written is kept). */
static inline void
Ckpt_Why(
    char* why,
    int why_cap,
    char const* fmt,
    ...)
{
    va_list ap;

    if( !why || why_cap <= 0 || why[0] )
        return;
    va_start(ap, fmt);
    vsnprintf(why, (size_t)why_cap, fmt, ap);
    va_end(ap);
}

/** Two possibly-NULL strings: NULL and "" are different states. */
static inline int
Ckpt_StrEq(
    char const* a,
    char const* b)
{
    if( !a || !b )
        return a == b;
    return strcmp(a, b) == 0;
}

/* Compare one field of two structs `a` and `b` by its bytes; on a difference,
 * name it in `why` and return 0 from the calling function. For scalars and
 * arrays of scalars (no padding inside). */
#define CKPT_EQ_FIELD(a, b, field, why, why_cap)                                                  \
    do                                                                                             \
    {                                                                                              \
        if( memcmp(&(a)->field, &(b)->field, sizeof((a)->field)) != 0 )                            \
        {                                                                                          \
            Ckpt_Why((why), (why_cap), "%s", #field);                                              \
            return 0;                                                                              \
        }                                                                                          \
    } while( 0 )

/* The same for a scalar int field, printing both values. */
#define CKPT_EQ_INT(a, b, field, why, why_cap)                                                     \
    do                                                                                             \
    {                                                                                              \
        if( (a)->field != (b)->field )                                                             \
        {                                                                                          \
            Ckpt_Why((why), (why_cap), "%s %lld != %lld", #field, (long long)(a)->field,           \
                     (long long)(b)->field);                                                       \
            return 0;                                                                              \
        }                                                                                          \
    } while( 0 )

#endif
