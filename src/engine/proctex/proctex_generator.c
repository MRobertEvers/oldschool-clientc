#include "engine/proctex/proctex_generator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 12.4 fixed point: 4096 == 1.0. */
#define PROCTEX_ONE 4096

/* Shared trig tables, indexed 0..255 over a full turn, scaled by 4096. Built once. */
static int32_t PROCTEX_SINE[256];
static int32_t PROCTEX_COSINE[256];
static bool proctex_trig_ready = false;

static void
proctex_init_trig(void)
{
    if( proctex_trig_ready )
        return;
    for( int i = 0; i < 256; i++ )
    {
        double d = ((double)i / 255.0) * 6.283185307179586;
        PROCTEX_SINE[i] = (int32_t)(sin(d) * 4096.0);
        PROCTEX_COSINE[i] = (int32_t)(cos(d) * 4096.0);
    }
    proctex_trig_ready = true;
}

/** Per-operation scanline cache. Colour ops use all three planes, monochrome only [0]. */
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

    int32_t* h_gradient; /* width entries: (x << 12) / width */
    int32_t* v_gradient; /* height entries: (y << 12) / height */

    int32_t brightness_table[256];
    double brightness;

    struct proctex_cache* caches;
    int cache_count;

    /* Per-operation precomputed lookup tables, built lazily on first use:
     * curve -> 257 monochrome values, gradient -> 257 packed RGB values. */
    int32_t** tables;

    int unsupported_count;
    bool is_transparent;
    /* Guards against a cyclic graph in a malformed record; the format cannot express one but
     * nothing validates that, and a cycle here would recurse until the stack died. */
    uint8_t* visiting;
    bool cycle_detected;
};

/* --- which operations are ported ------------------------------------------------------- */

/*
 * Ported evaluators. The set was chosen by measuring which operations real 643 textures
 * actually use, not by reading order: see B18 in EXCEPTIONS.md for the histogram. Everything
 * absent here falls back to mid-grey and is counted.
 */
bool
ProcTexGenerator_SupportsOp(int op_type)
{
    switch( op_type )
    {
    case RSCACHE_PROCTEX_CONST_MONO:
    case RSCACHE_PROCTEX_CONST_COLOUR:
    case RSCACHE_PROCTEX_H_GRADIENT:
    case RSCACHE_PROCTEX_V_GRADIENT:
    case RSCACHE_PROCTEX_CLAMP:
    case RSCACHE_PROCTEX_ARITHMETIC:
    case RSCACHE_PROCTEX_CURVE:
    case RSCACHE_PROCTEX_GRADIENT:
    case RSCACHE_PROCTEX_COLOUR_STRIP:
    case RSCACHE_PROCTEX_DIAGONAL_GRADIENT:
    case RSCACHE_PROCTEX_INVERT:
    case RSCACHE_PROCTEX_GRAYSCALE:
    case RSCACHE_PROCTEX_BINARY:
    case RSCACHE_PROCTEX_RANGE:
        return true;
    default:
        return false;
    }
}

bool
ProcTexGenerator_IsFullySupported(
    const struct RSCache_Dat2ProcTexture* texture,
    int* out_first_unsupported_type)
{
    if( !texture )
        return false;
    for( int i = 0; i < texture->operation_count; i++ )
    {
        if( !ProcTexGenerator_SupportsOp(texture->operations[i].type) )
        {
            if( out_first_unsupported_type )
                *out_first_unsupported_type = texture->operations[i].type;
            return false;
        }
    }
    return true;
}

/* --- lifecycle ------------------------------------------------------------------------- */

struct ProcTexGenerator*
ProcTexGenerator_New(
    ProcTexSpriteFn sprite_fn,
    ProcTexTextureFn texture_fn,
    void* user)
{
    struct ProcTexGenerator* gen;

    proctex_init_trig();

    gen = calloc(1, sizeof(*gen));
    if( !gen )
        return NULL;
    gen->sprite_fn = sprite_fn;
    gen->texture_fn = texture_fn;
    gen->user = user;
    gen->brightness = -1.0;
    return gen;
}

static void
proctex_free_caches(struct ProcTexGenerator* gen)
{
    if( gen->caches )
    {
        for( int i = 0; i < gen->cache_count; i++ )
        {
            for( int p = 0; p < 3; p++ )
                free(gen->caches[i].plane[p]);
            free(gen->caches[i].done);
        }
        free(gen->caches);
        gen->caches = NULL;
    }
    if( gen->tables )
    {
        for( int i = 0; i < gen->cache_count; i++ )
            free(gen->tables[i]);
        free(gen->tables);
        gen->tables = NULL;
    }
    free(gen->visiting);
    gen->visiting = NULL;
    gen->cache_count = 0;
}

void
ProcTexGenerator_Free(struct ProcTexGenerator* gen)
{
    if( !gen )
        return;
    proctex_free_caches(gen);
    free(gen->h_gradient);
    free(gen->v_gradient);
    free(gen);
}

/* --- setup ----------------------------------------------------------------------------- */

static bool
proctex_setup(
    struct ProcTexGenerator* gen,
    const struct RSCache_Dat2ProcTexture* texture,
    int size,
    double brightness)
{
    gen->tex = texture;
    gen->unsupported_count = 0;
    gen->is_transparent = false;
    gen->cycle_detected = false;

    if( gen->width != size )
    {
        free(gen->h_gradient);
        gen->h_gradient = malloc((size_t)size * sizeof(int32_t));
        if( !gen->h_gradient )
            return false;
        for( int i = 0; i < size; i++ )
            gen->h_gradient[i] = (i << 12) / size;
        gen->width = size;
    }
    if( gen->height != size )
    {
        free(gen->v_gradient);
        gen->v_gradient = malloc((size_t)size * sizeof(int32_t));
        if( !gen->v_gradient )
            return false;
        for( int i = 0; i < size; i++ )
            gen->v_gradient[i] = (i << 12) / size;
        gen->height = size;
    }

    if( gen->brightness != brightness )
    {
        for( int i = 0; i < 256; i++ )
        {
            int v = (int)(pow((double)i / 255.0, brightness) * 255.0);
            gen->brightness_table[i] = v > 255 ? 255 : v;
        }
        gen->brightness = brightness;
    }

    proctex_free_caches(gen);
    gen->cache_count = texture->operation_count;
    gen->caches = calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1),
                         sizeof(*gen->caches));
    gen->tables = calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1),
                         sizeof(*gen->tables));
    gen->visiting = calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1), 1);
    if( !gen->caches || !gen->tables || !gen->visiting )
        return false;

    for( int i = 0; i < gen->cache_count; i++ )
    {
        int planes = texture->operations[i].is_monochrome ? 1 : 3;
        gen->caches[i].done = calloc((size_t)size, 1);
        if( !gen->caches[i].done )
            return false;
        for( int p = 0; p < planes; p++ )
        {
            gen->caches[i].plane[p] = calloc((size_t)size * (size_t)size, sizeof(int32_t));
            if( !gen->caches[i].plane[p] )
                return false;
        }
    }
    return true;
}

/* --- evaluation -------------------------------------------------------------------------- */

static bool
proctex_eval(
    struct ProcTexGenerator* gen,
    int op_index,
    int line);

/** Monochrome view of input `n` of `op_index`, for `line`. Never NULL. */
static const int32_t*
proctex_input_mono(
    struct ProcTexGenerator* gen,
    const struct RSCache_ProcTexOperation* op,
    int input,
    int line)
{
    static int32_t zero[1] = { 0 };
    int src;

    if( input >= op->input_count )
        return NULL;
    src = op->inputs[input];
    if( src < 0 || src >= gen->cache_count )
        return NULL;
    if( !proctex_eval(gen, src, line) )
        return NULL;
    (void)zero;
    /* A colour operation read as monochrome yields its red plane — the reference's rule. */
    return gen->caches[src].plane[0] + (size_t)line * (size_t)gen->width;
}

/** Colour view of input `n`. Fills `out[3]`; a monochrome source is broadcast. */
static bool
proctex_input_colour(
    struct ProcTexGenerator* gen,
    const struct RSCache_ProcTexOperation* op,
    int input,
    int line,
    const int32_t* out[3])
{
    int src;
    size_t offset;

    if( input >= op->input_count )
        return false;
    src = op->inputs[input];
    if( src < 0 || src >= gen->cache_count )
        return false;
    if( !proctex_eval(gen, src, line) )
        return false;

    offset = (size_t)line * (size_t)gen->width;
    if( gen->tex->operations[src].is_monochrome )
    {
        out[0] = out[1] = out[2] = gen->caches[src].plane[0] + offset;
    }
    else
    {
        out[0] = gen->caches[src].plane[0] + offset;
        out[1] = gen->caches[src].plane[1] + offset;
        out[2] = gen->caches[src].plane[2] + offset;
    }
    return true;
}

static int32_t
proctex_clamp_i(
    int32_t value,
    int32_t lo,
    int32_t hi)
{
    if( value < lo )
        return lo;
    if( value > hi )
        return hi;
    return value;
}

/* --- curve / gradient lookup tables ---------------------------------------------------- */

/* Markers default to a straight 0..4096 ramp when the record carried none, matching the
 * reference's init(). Two are the minimum the interpolators can work with. */
static void
proctex_curve_markers(
    const struct RSCache_ProcTexOperation* op,
    struct RSCache_ProcTexMarker* out,
    int* out_count)
{
    if( op->u.curve.marker_count >= 2 )
    {
        for( int i = 0; i < op->u.curve.marker_count; i++ )
            out[i] = op->u.curve.markers[i];
        *out_count = op->u.curve.marker_count;
        return;
    }
    out[0].key = 0;
    out[0].value = 0;
    out[1].key = PROCTEX_ONE;
    out[1].value = PROCTEX_ONE;
    *out_count = 2;
}

/* markers[] extended past both ends by reflection, for the cubic mode. */
static struct RSCache_ProcTexMarker
proctex_curve_marker_at(
    const struct RSCache_ProcTexMarker* markers,
    int count,
    int index)
{
    struct RSCache_ProcTexMarker result;
    if( index >= 0 && index < count )
        return markers[index];
    if( index < 0 )
    {
        result.key = markers[0].key + markers[0].key - markers[1].key;
        result.value = markers[0].value - markers[1].value + markers[0].value;
        return result;
    }
    result.key = markers[count - 1].key - markers[count - 2].key + markers[count - 1].key;
    result.value = markers[count - 1].value - markers[count - 2].value + markers[count - 1].value;
    return result;
}

static int32_t*
proctex_curve_table(
    struct ProcTexGenerator* gen,
    int op_index)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct RSCache_ProcTexMarker markers[18];
    int count = 0;
    int32_t* table;

    if( gen->tables[op_index] )
        return gen->tables[op_index];

    table = malloc(257 * sizeof(int32_t));
    if( !table )
        return NULL;
    proctex_curve_markers(op, markers, &count);

    for( int index = 0; index < 257; index++ )
    {
        int key = index * 16;
        int mark_index;
        const struct RSCache_ProcTexMarker* prev;
        const struct RSCache_ProcTexMarker* next;
        int interp;
        int out;

        for( mark_index = 1; mark_index < count - 1; mark_index++ )
            if( markers[mark_index].key > key )
                break;

        prev = &markers[mark_index - 1];
        next = &markers[mark_index];
        if( next->key == prev->key )
            interp = 0;
        else
            interp = ((key - prev->key) * PROCTEX_ONE) / (next->key - prev->key);

        switch( op->u.curve.interp_mode )
        {
        case 2:
        {
            /* Catmull-Rom-ish cubic over four control values. */
            int v0 = proctex_curve_marker_at(markers, count, mark_index - 2).value;
            int v1 = prev->value;
            int v2 = next->value;
            int v3 = proctex_curve_marker_at(markers, count, mark_index + 1).value;
            int x_sq = (interp * interp) / PROCTEX_ONE;
            int a = v1 - v0 + (v3 - v2);
            int b = v0 - v1 - a;
            int c = v2 - v0;
            int term_a = (x_sq * ((interp * a) >> 12)) >> 12;
            int term_b = (x_sq * b) / PROCTEX_ONE;
            int term_c = (interp * c) / PROCTEX_ONE;
            out = term_c + term_a + term_b + v1;
            break;
        }
        case 1:
        {
            /* Cosine ease. The `& 8187` is the reference's own masking; kept verbatim
             * because it is load-bearing for which table entry is picked. */
            int n_mul = (PROCTEX_ONE - PROCTEX_COSINE[((interp & 8187) / 32) & 0xFF]) / 2;
            int p_mul = PROCTEX_ONE - n_mul;
            out = (p_mul * prev->value + next->value * n_mul) / PROCTEX_ONE;
            break;
        }
        default:
        {
            int n_mul = interp;
            int p_mul = PROCTEX_ONE - n_mul;
            out = (p_mul * prev->value + next->value * n_mul) / PROCTEX_ONE;
            break;
        }
        }

        if( out <= -32768 )
            out = -32767;
        if( out >= 32768 )
            out = 32767;
        table[index] = out;
    }

    gen->tables[op_index] = table;
    return table;
}

static int32_t*
proctex_gradient_table(
    struct ProcTexGenerator* gen,
    int op_index)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    const struct RSCache_ProcTexGradientStop* stops = op->u.gradient.stops;
    int count = op->u.gradient.stop_count;
    int32_t* table;

    if( gen->tables[op_index] )
        return gen->tables[op_index];

    table = calloc(257, sizeof(int32_t));
    if( !table )
        return NULL;

    /*
     * A non-zero `preset` names a built-in gradient we do not have the table for. Rather than
     * invent one, fall back to a black-to-white ramp and count the operation as unsupported so
     * the texture is refused — an invented palette would render confidently wrong.
     */
    if( count <= 0 )
    {
        gen->unsupported_count++;
        for( int i = 0; i < 257; i++ )
        {
            int v = proctex_clamp_i((i * 255) / 256, 0, 255);
            table[i] = (v << 16) | (v << 8) | v;
        }
        gen->tables[op_index] = table;
        return table;
    }

    for( int i = 0; i < 257; i++ )
    {
        int key = i << 4;
        int idx = 0;
        int r;
        int g;
        int b;

        while( idx < count && stops[idx].position <= key )
            idx++;

        if( idx < count )
        {
            if( idx > 0 )
            {
                const struct RSCache_ProcTexGradientStop* p = &stops[idx - 1];
                const struct RSCache_ProcTexGradientStop* n = &stops[idx];
                int span = n->position - p->position;
                int n_mod = span ? (((key - p->position) << 12) / span) : 0;
                int p_mod = PROCTEX_ONE - n_mod;
                r = (p->r * p_mod + n->r * n_mod) >> 12;
                g = (p->g * p_mod + n->g * n_mod) >> 12;
                b = (n->b * n_mod + p->b * p_mod) >> 12;
            }
            else
            {
                r = stops[idx].r;
                g = stops[idx].g;
                b = stops[idx].b;
            }
        }
        else
        {
            r = stops[count - 1].r;
            g = stops[count - 1].g;
            b = stops[count - 1].b;
        }

        r = proctex_clamp_i(r >> 4, 0, 255);
        g = proctex_clamp_i(g >> 4, 0, 255);
        b = proctex_clamp_i(b >> 4, 0, 255);
        table[i] = (r << 16) | (g << 8) | b;
    }

    gen->tables[op_index] = table;
    return table;
}

/* --- arithmetic ------------------------------------------------------------------------ */

/* One channel of the 12 blend modes. Transcribed from ArithmeticOperation; the asymmetries
 * (which operand is which in modes 6/7/8/12) are the reference's and are deliberate. */
static int32_t
proctex_arith(
    int mode,
    int32_t a,
    int32_t b)
{
    switch( mode )
    {
    case 1:
        return a + b;
    case 2:
        return a - b;
    case 3:
        return (b * a) / PROCTEX_ONE;
    case 4:
        return b == 0 ? PROCTEX_ONE : (a * PROCTEX_ONE) / b;
    case 5:
        return PROCTEX_ONE - ((PROCTEX_ONE - a) * (PROCTEX_ONE - b)) / PROCTEX_ONE;
    case 6:
        return b < 2048 ? (b * a) / 2048
                        : PROCTEX_ONE - (((PROCTEX_ONE - a) * (PROCTEX_ONE - b)) / 2048);
    case 7:
        return a == PROCTEX_ONE ? PROCTEX_ONE : (b * PROCTEX_ONE) / (PROCTEX_ONE - a);
    case 8:
        return a == 0 ? 0 : PROCTEX_ONE - ((PROCTEX_ONE - b) * PROCTEX_ONE) / a;
    case 9:
        return a < b ? a : b;
    case 10:
        return a > b ? a : b;
    case 11:
        return b < a ? a - b : b - a;
    case 12:
        return b + a - (b * a) / 2048;
    default:
        return a;
    }
}

/* --- the evaluator --------------------------------------------------------------------- */

static bool
proctex_eval(
    struct ProcTexGenerator* gen,
    int op_index,
    int line)
{
    const struct RSCache_ProcTexOperation* op;
    struct proctex_cache* cache;
    int width = gen->width;
    int32_t* out0;
    int32_t* out1;
    int32_t* out2;
    size_t offset;

    if( op_index < 0 || op_index >= gen->cache_count )
        return false;
    if( line < 0 || line >= gen->height )
        return false;

    cache = &gen->caches[op_index];
    if( cache->done[line] )
        return true;

    /* A malformed record could wire a cycle; the format has no way to express one but nothing
     * checks, and unguarded recursion would blow the stack instead of failing. */
    if( gen->visiting[op_index] )
    {
        gen->cycle_detected = true;
        return false;
    }
    gen->visiting[op_index] = 1;

    op = &gen->tex->operations[op_index];
    offset = (size_t)line * (size_t)width;
    out0 = cache->plane[0] + offset;
    out1 = cache->plane[1] ? cache->plane[1] + offset : NULL;
    out2 = cache->plane[2] ? cache->plane[2] + offset : NULL;

    switch( op->type )
    {
    case RSCACHE_PROCTEX_CONST_MONO:
        for( int x = 0; x < width; x++ )
            out0[x] = op->u.const_mono.constant;
        break;

    case RSCACHE_PROCTEX_CONST_COLOUR:
        for( int x = 0; x < width; x++ )
        {
            out0[x] = op->u.const_colour.r;
            out1[x] = op->u.const_colour.g;
            out2[x] = op->u.const_colour.b;
        }
        break;

    case RSCACHE_PROCTEX_H_GRADIENT:
        for( int x = 0; x < width; x++ )
            out0[x] = gen->h_gradient[x];
        break;

    case RSCACHE_PROCTEX_V_GRADIENT:
        for( int x = 0; x < width; x++ )
            out0[x] = gen->v_gradient[line];
        break;

    case RSCACHE_PROCTEX_DIAGONAL_GRADIENT:
    {
        int in_param = gen->v_gradient[line];
        int n_param = (in_param - 2048) >> 1;
        for( int x = 0; x < width; x++ )
        {
            int in_value = gen->h_gradient[x];
            int n_value = (in_value - 2048) >> 1;
            int value;
            if( op->u.diagonal_gradient.mix_mode == 0 )
            {
                value = (in_value - in_param) * op->u.diagonal_gradient.steepness;
            }
            else
            {
                int sq_sum = (n_param * n_param + n_value * n_value) >> 12;
                value = (int)(4096.0 * sqrt((double)sq_sum / 4096.0));
                value = (int)((double)op->u.diagonal_gradient.steepness * (double)value *
                              3.141592653589793);
            }
            value -= value & ~0xFFF;
            if( op->u.diagonal_gradient.interpolation_mode == 0 )
            {
                value = (PROCTEX_SINE[(value >> 4) & 0xFF] + PROCTEX_ONE) >> 1;
            }
            else if( op->u.diagonal_gradient.interpolation_mode == 2 )
            {
                value -= 2048;
                if( value < 0 )
                    value = -value;
                value = (2048 - value) << 1;
            }
            out0[x] = value;
        }
        break;
    }

    case RSCACHE_PROCTEX_CURVE:
    {
        const int32_t* input = proctex_input_mono(gen, op, 0, line);
        int32_t* table = proctex_curve_table(gen, op_index);
        if( !input || !table )
            goto fail;
        for( int x = 0; x < width; x++ )
        {
            int value = input[x] / 16;
            out0[x] = table[proctex_clamp_i(value, 0, 256)];
        }
        break;
    }

    case RSCACHE_PROCTEX_GRADIENT:
    {
        const int32_t* input = proctex_input_mono(gen, op, 0, line);
        int32_t* table = proctex_gradient_table(gen, op_index);
        if( !input || !table )
            goto fail;
        for( int x = 0; x < width; x++ )
        {
            int rgb = table[proctex_clamp_i(input[x] / 16, 0, 256)];
            out0[x] = ((rgb >> 16) & 0xFF) << 4;
            out1[x] = ((rgb >> 8) & 0xFF) << 4;
            out2[x] = (rgb & 0xFF) << 4;
        }
        break;
    }

    case RSCACHE_PROCTEX_CLAMP:
        if( op->is_monochrome )
        {
            const int32_t* input = proctex_input_mono(gen, op, 0, line);
            if( !input )
                goto fail;
            for( int x = 0; x < width; x++ )
                out0[x] = proctex_clamp_i(input[x], op->u.clamp.min, op->u.clamp.max);
        }
        else
        {
            const int32_t* in[3];
            if( !proctex_input_colour(gen, op, 0, line, in) )
                goto fail;
            for( int x = 0; x < width; x++ )
            {
                out0[x] = proctex_clamp_i(in[0][x], op->u.clamp.min, op->u.clamp.max);
                out1[x] = proctex_clamp_i(in[1][x], op->u.clamp.min, op->u.clamp.max);
                out2[x] = proctex_clamp_i(in[2][x], op->u.clamp.min, op->u.clamp.max);
            }
        }
        break;

    case RSCACHE_PROCTEX_RANGE:
    {
        /* field2 is derived, not stored: the reference computes it as field1 - field0. */
        int scale = op->u.range.field1 - op->u.range.field0;
        if( op->is_monochrome )
        {
            const int32_t* input = proctex_input_mono(gen, op, 0, line);
            if( !input )
                goto fail;
            for( int x = 0; x < width; x++ )
                out0[x] = ((scale * input[x]) >> 12) + op->u.range.field0;
        }
        else
        {
            const int32_t* in[3];
            if( !proctex_input_colour(gen, op, 0, line, in) )
                goto fail;
            for( int x = 0; x < width; x++ )
            {
                out0[x] = ((scale * in[0][x]) >> 12) + op->u.range.field0;
                out1[x] = ((scale * in[1][x]) >> 12) + op->u.range.field0;
                out2[x] = ((scale * in[2][x]) >> 12) + op->u.range.field0;
            }
        }
        break;
    }

    case RSCACHE_PROCTEX_BINARY:
    {
        const int32_t* input = proctex_input_mono(gen, op, 0, line);
        if( !input )
            goto fail;
        for( int x = 0; x < width; x++ )
            out0[x] = (input[x] >= op->u.binary.min_value && input[x] <= op->u.binary.max_value)
                          ? PROCTEX_ONE
                          : 0;
        break;
    }

    case RSCACHE_PROCTEX_GRAYSCALE:
    {
        const int32_t* in[3];
        if( !proctex_input_colour(gen, op, 0, line, in) )
            goto fail;
        for( int x = 0; x < width; x++ )
            out0[x] = (in[0][x] + in[1][x] + in[2][x]) / 3;
        break;
    }

    case RSCACHE_PROCTEX_INVERT:
        if( op->is_monochrome )
        {
            const int32_t* input = proctex_input_mono(gen, op, 0, line);
            if( !input )
                goto fail;
            for( int x = 0; x < width; x++ )
                out0[x] = PROCTEX_ONE - input[x];
        }
        else
        {
            const int32_t* in[3];
            if( !proctex_input_colour(gen, op, 0, line, in) )
                goto fail;
            for( int x = 0; x < width; x++ )
            {
                out0[x] = PROCTEX_ONE - in[0][x];
                out1[x] = PROCTEX_ONE - in[1][x];
                out2[x] = PROCTEX_ONE - in[2][x];
            }
        }
        break;

    case RSCACHE_PROCTEX_COLOUR_STRIP:
    {
        const int32_t* in[3];
        if( !proctex_input_colour(gen, op, 0, line, in) )
            goto fail;
        for( int x = 0; x < width; x++ )
        {
            int32_t vr = in[0][x];
            int32_t vg = in[1][x];
            int32_t vb = in[2][x];
            /* A non-grey input passes the strip colour straight through; a grey one is
             * modulated by it. */
            if( vr != vb || vb != vg )
            {
                out0[x] = op->u.colour_strip.r;
                out1[x] = op->u.colour_strip.g;
                out2[x] = op->u.colour_strip.b;
            }
            else
            {
                out0[x] = (op->u.colour_strip.r * vr) >> 12;
                out1[x] = (op->u.colour_strip.g * vg) >> 12;
                out2[x] = (op->u.colour_strip.b * vb) >> 12;
            }
        }
        break;
    }

    case RSCACHE_PROCTEX_ARITHMETIC:
        if( op->is_monochrome )
        {
            const int32_t* a = proctex_input_mono(gen, op, 0, line);
            const int32_t* b = proctex_input_mono(gen, op, 1, line);
            if( !a || !b )
                goto fail;
            for( int x = 0; x < width; x++ )
                out0[x] = proctex_arith(op->u.arithmetic.operation, a[x], b[x]);
        }
        else
        {
            const int32_t* a[3];
            const int32_t* b[3];
            if( !proctex_input_colour(gen, op, 0, line, a) ||
                !proctex_input_colour(gen, op, 1, line, b) )
                goto fail;
            for( int x = 0; x < width; x++ )
            {
                out0[x] = proctex_arith(op->u.arithmetic.operation, a[0][x], b[0][x]);
                out1[x] = proctex_arith(op->u.arithmetic.operation, a[1][x], b[1][x]);
                out2[x] = proctex_arith(op->u.arithmetic.operation, a[2][x], b[2][x]);
            }
        }
        break;

    default:
        /*
         * Not ported yet. Mid-grey is a deliberately neutral, obviously-flat result: it keeps
         * the graph evaluable so the rest of the texture can be inspected, while the count
         * lets the caller refuse to publish the image. It is never a claim of correctness.
         */
        gen->unsupported_count++;
        for( int x = 0; x < width; x++ )
        {
            out0[x] = PROCTEX_ONE / 2;
            if( out1 )
                out1[x] = PROCTEX_ONE / 2;
            if( out2 )
                out2[x] = PROCTEX_ONE / 2;
        }
        break;
    }

    gen->visiting[op_index] = 0;
    cache->done[line] = 1;
    return true;

fail:
    gen->visiting[op_index] = 0;
    return false;
}

/* --- render ---------------------------------------------------------------------------- */

bool
ProcTexGenerator_Render(
    struct ProcTexGenerator* gen,
    const struct RSCache_Dat2ProcTexture* texture,
    int size,
    double brightness,
    int32_t* out_argb,
    int* out_unsupported,
    bool* out_transparent)
{
    if( !gen || !texture || !out_argb || size <= 0 )
        return false;
    if( texture->operation_count <= 0 || texture->colour_op < 0 )
        return false;
    if( !proctex_setup(gen, texture, size, brightness) )
        return false;

    for( int line = 0; line < size; line++ )
    {
        const int32_t* colour[3];
        const int32_t* alpha = NULL;
        size_t offset = (size_t)line * (size_t)size;
        int dst_line = texture->flip_v ? (size - 1 - line) : line;

        if( !proctex_eval(gen, texture->colour_op, line) )
            return false;
        if( texture->operations[texture->colour_op].is_monochrome )
        {
            colour[0] = colour[1] = colour[2] =
                gen->caches[texture->colour_op].plane[0] + offset;
        }
        else
        {
            colour[0] = gen->caches[texture->colour_op].plane[0] + offset;
            colour[1] = gen->caches[texture->colour_op].plane[1] + offset;
            colour[2] = gen->caches[texture->colour_op].plane[2] + offset;
        }

        if( texture->alpha_op >= 0 && proctex_eval(gen, texture->alpha_op, line) )
            alpha = gen->caches[texture->alpha_op].plane[0] + offset;

        for( int x = 0; x < size; x++ )
        {
            int r = proctex_clamp_i(colour[0][x] >> 4, 0, 255);
            int g = proctex_clamp_i(colour[1][x] >> 4, 0, 255);
            int b = proctex_clamp_i(colour[2][x] >> 4, 0, 255);
            int a;

            r = gen->brightness_table[r];
            g = gen->brightness_table[g];
            b = gen->brightness_table[b];

            if( r != 0 || g != 0 || b != 0 )
                a = alpha ? proctex_clamp_i(alpha[x] >> 4, 0, 255) : 255;
            else
                a = 0;

            if( a != 255 )
                gen->is_transparent = true;

            out_argb[(size_t)dst_line * (size_t)size + (size_t)x] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    if( gen->cycle_detected )
        return false;

    if( out_unsupported )
        *out_unsupported = gen->unsupported_count;
    if( out_transparent )
        *out_transparent = gen->is_transparent;
    return true;
}
