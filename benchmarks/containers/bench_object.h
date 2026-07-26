#ifndef BENCH_OBJECT_H
#define BENCH_OBJECT_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Dummy payload sized to one cache line so each container row measures
 * the cost of reaching a realistic object, not a tiny key-only entry.
 */
struct BenchObject
{
    uint32_t id;
    uint32_t flags;
    float x, y, z, yaw;
    int32_t hp;
    int32_t level;
    uint64_t last_update_tick;
    char name[20];
};

_Static_assert(sizeof(struct BenchObject) == 64, "BenchObject must be 64 bytes");

enum
{
    BENCH_ID_MAX = 2048,
    BENCH_QUERY_COUNT = 4096,
    BENCH_WARMUP_PASSES = 4,
    BENCH_TIMED_PASSES = 24
};

static inline void
bench_object_init(struct BenchObject* obj, uint32_t id)
{
    assert(obj);
    memset(obj, 0, sizeof(*obj));
    obj->id = id;
    obj->flags = id * 17u + 3u;
    obj->x = (float)(id % 64);
    obj->y = (float)((id / 64) % 64);
    obj->z = (float)(id % 4);
    obj->yaw = (float)(id % 2048) * (3.14159265f / 1024.0f);
    obj->hp = (int32_t)(100 + (id % 900));
    obj->level = (int32_t)(1 + (id % 99));
    obj->last_update_tick = (uint64_t)id * 1000ull;
    snprintf(obj->name, sizeof(obj->name), "obj_%u", id);
}

/* ---- PRNG (xorshift32) ---- */

struct BenchRng
{
    uint32_t state;
};

static inline void
bench_rng_seed(struct BenchRng* rng, uint32_t seed)
{
    assert(rng);
    rng->state = seed ? seed : 1u;
}

static inline uint32_t
bench_rng_u32(struct BenchRng* rng)
{
    assert(rng);
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static inline uint32_t
bench_rng_range(struct BenchRng* rng, uint32_t n)
{
    assert(rng);
    assert(n > 0);
    return bench_rng_u32(rng) % n;
}

static inline void
bench_shuffle_u32(uint32_t* ids, size_t count, struct BenchRng* rng)
{
    assert(ids || count == 0);
    assert(rng);
    if( count < 2 )
        return;
    for( size_t i = count - 1; i > 0; i-- )
    {
        size_t j = (size_t)bench_rng_range(rng, (uint32_t)(i + 1));
        uint32_t tmp = ids[i];
        ids[i] = ids[j];
        ids[j] = tmp;
    }
}

/* ---- Timing ---- */

static inline double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* Fold a field into a checksum so the compiler cannot elide the lookup. */
static inline uint64_t
bench_object_checksum(const struct BenchObject* obj, uint64_t acc)
{
    assert(obj);
    return acc ^ ((uint64_t)obj->id + 0x9e3779b97f4a7c15ull) ^
           ((uint64_t)obj->flags << 1) ^ (uint64_t)obj->hp ^
           (uint64_t)obj->level ^ obj->last_update_tick;
}

static inline void
bench_die(const char* msg)
{
    fprintf(stderr, "bench fatal: %s\n", msg);
    exit(1);
}

#endif /* BENCH_OBJECT_H */
