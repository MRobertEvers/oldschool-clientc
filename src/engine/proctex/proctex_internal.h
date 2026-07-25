#ifndef TORIRS_PROCTEX_INTERNAL_H
#define TORIRS_PROCTEX_INTERNAL_H

/*
 * Shared internals for the procedural-texture evaluator. Included by
 * proctex_generator.c and the .u.c operation units it pulls in.
 */

#include "engine/proctex/proctex_generator.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PROCTEX_ONE 4096

struct proctex_cache
{
    int32_t* plane[3];
    uint8_t* done;
};

struct ProcTexGenerator
{
    ProcTexSpriteFn sprite_fn;
    ProcTexTextureFn texture_fn;
    void* user;

    const struct RSCache_Dat2ProcTexture* tex;
    int width;
    int height;
    int width_mask;
    int height_mask;
    int width_times_32;

    int32_t* h_gradient;
    int32_t* v_gradient;

    int32_t brightness_table[256];
    double brightness;

    struct proctex_cache* caches;
    int cache_count;

    /* Per-op lazy tables (curve/gradient) and typed aux state. */
    int32_t** tables;
    void** aux;

    int unsupported_count;
    bool is_transparent;
    uint8_t* visiting;
    bool cycle_detected;
};

extern int32_t PROCTEX_SINE[256];
extern int32_t PROCTEX_COSINE[256];
extern int8_t PROCTEX_INV_SQRT[32896];

static inline int32_t
proctex_clamp_i(int32_t value, int32_t lo, int32_t hi)
{
    if( value < lo )
        return lo;
    if( value > hi )
        return hi;
    return value;
}

static inline float
proctex_fround(double d)
{
    return (float)d;
}

/* Java util.Random — instance state, matching java-random / J2SE. */
struct proctex_jrand
{
    uint64_t seed;
};

static inline void
proctex_jrand_seed(struct proctex_jrand* r, int64_t seedval)
{
    r->seed = ((uint64_t)seedval ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
}

static inline int32_t
proctex_jrand_next(struct proctex_jrand* r, int bits)
{
    r->seed = (r->seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return (int32_t)(r->seed >> (48 - bits));
}

static inline int32_t
proctex_jrand_next_int(struct proctex_jrand* r)
{
    return proctex_jrand_next(r, 32);
}

static inline int
proctex_is_pow2(int32_t n)
{
    return n != 0 && (n & -n) == n;
}

/* nextIntJagex — rs-map-viewer's MathUtil.nextIntJagex */
static inline int32_t
proctex_next_int_jagex(struct proctex_jrand* r, int32_t bound)
{
    int32_t rnd;
    int32_t max_value;
    int32_t i78;

    assert(bound > 0);
    if( proctex_is_pow2(bound) )
    {
        uint32_t u = (uint32_t)proctex_jrand_next_int(r);
        return (int32_t)(((uint64_t)(uint32_t)bound * (uint64_t)u) >> 32);
    }
    max_value = (int32_t)(-0x80000000 - (int32_t)(0x100000000ULL % (uint32_t)bound));
    do
    {
        rnd = proctex_jrand_next_int(r);
    } while( rnd >= max_value );
    i78 = (rnd >> 31) & (bound - 1);
    return i78 + ((rnd + (int32_t)((uint32_t)rnd >> 31)) % bound);
}

const int8_t*
proctex_permutations(uint8_t seed);

bool
proctex_eval(struct ProcTexGenerator* gen, int op_index, int line);

const int32_t*
proctex_input_mono(
    struct ProcTexGenerator* gen,
    const struct RSCache_ProcTexOperation* op,
    int input,
    int line);

bool
proctex_input_colour(
    struct ProcTexGenerator* gen,
    const struct RSCache_ProcTexOperation* op,
    int input,
    int line,
    const int32_t* out[3]);

void
proctex_mark_all_done(struct proctex_cache* cache, int height);

/* Per-op evaluators — return false on hard failure. */
bool proctex_op_mixer(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_blur(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_emboss(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_perlin(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_voronoi(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_trig_warp(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_pseudo_random(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_tiling(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_hsl(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_mirror(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_line_noise(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_square_waveform(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_bricks(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_kaleidoscope(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_brightness(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_colour_edge(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_mono_edge(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_weave(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_herringbone(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_mandelbrot(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_sprite_source(struct ProcTexGenerator* gen, int op_index, int line, int tiling);
bool proctex_op_texture_source(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_irregular_bricks(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_op37(struct ProcTexGenerator* gen, int op_index, int line);
bool proctex_op_rasterizer(struct ProcTexGenerator* gen, int op_index, int line);

#endif
