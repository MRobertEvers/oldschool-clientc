#ifndef TRSPK_FLAGS_H
#define TRSPK_FLAGS_H

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_FLAG(n) (1u << (n))

static inline void
trspk_flags_set(
    uint32_t* flags,
    uint32_t mask)
{
    *flags |= mask;
}

static inline void
trspk_flags_clear(
    uint32_t* flags,
    uint32_t mask)
{
    *flags &= ~mask;
}

static inline bool
trspk_flags_test(
    uint32_t flags,
    uint32_t mask)
{
    return (flags & mask) != 0;
}

#endif
