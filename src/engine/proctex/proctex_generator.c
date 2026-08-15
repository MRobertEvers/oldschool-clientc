#include "engine/proctex/proctex_internal.h"
#include <assert.h>

#include <stdio.h>

/*
 * Shared trig tables, indexed 0..255 over a full turn, scaled by 4096, plus the inverse
 * square-root table the emboss and edge operations index. Definitions live here; the ops unit
 * sees them via proctex_internal.h.
 */
int32_t PROCTEX_SINE[256];
int32_t PROCTEX_COSINE[256];
int8_t PROCTEX_INV_SQRT[32896];
static bool proctex_tables_ready = false;

static void
proctex_init_trig(void)
{
    if( proctex_tables_ready )
        return;
    for( int i = 0; i < 256; i++ )
    {
        double d = ((double)i / 255.0) * 6.283185307179586;
        PROCTEX_SINE[i] = (int32_t)(sin(d) * 4096.0);
        PROCTEX_COSINE[i] = (int32_t)(cos(d) * 4096.0);
    }
    /* Lower-triangular over (x, y) with y <= x, exactly as the reference builds it — the
     * consumers index it with the pair already ordered, so the packing is part of the
     * contract, not a space optimisation. */
    {
        int i = 0;
        for( int x = 0; x < 256; x++ )
            for( int y = 0; y <= x; y++ )
                PROCTEX_INV_SQRT[i++] =
                    (int8_t)(int)(255.0 / sqrt((double)proctex_fround(
                                              (double)(x * x + y * y + 65535) / 65535.0)));
    }
    proctex_tables_ready = true;
}

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
    /* Ported in proctex_ops.u.c. */
    case RSCACHE_PROCTEX_MIXER:
    case RSCACHE_PROCTEX_BLUR:
    case RSCACHE_PROCTEX_EMBOSS:
    case RSCACHE_PROCTEX_PERLIN_NOISE:
    case RSCACHE_PROCTEX_VORONOI_NOISE:
    case RSCACHE_PROCTEX_TRIG_WARP:
    case RSCACHE_PROCTEX_PSEUDO_RANDOM_NOISE:
    case RSCACHE_PROCTEX_TILING:
    case RSCACHE_PROCTEX_HSL:
    case RSCACHE_PROCTEX_MIRROR:
    case RSCACHE_PROCTEX_SQUARE_WAVEFORM:
    case RSCACHE_PROCTEX_BRIGHTNESS:
    case RSCACHE_PROCTEX_MONO_EDGE_DETECT:
    case RSCACHE_PROCTEX_COLOUR_EDGE_DETECT:
    case RSCACHE_PROCTEX_WEAVE:
    case RSCACHE_PROCTEX_HERRINGBONE:
    case RSCACHE_PROCTEX_MANDELBROT:
    case RSCACHE_PROCTEX_OP37:
    case RSCACHE_PROCTEX_SPRITE_SOURCE:
    case RSCACHE_PROCTEX_TILING_SPRITE:
    case RSCACHE_PROCTEX_TEXTURE_SOURCE:
    /* The pattern generators and the vector rasteriser. These three plus line_noise render
     * the whole image at once rather than per scanline — see proctex_mark_all_done. */
    case RSCACHE_PROCTEX_BRICKS:
    case RSCACHE_PROCTEX_IRREGULAR_BRICKS:
    case RSCACHE_PROCTEX_LINE_NOISE:
    case RSCACHE_PROCTEX_KALEIDOSCOPE:
    case RSCACHE_PROCTEX_RASTERIZER:
        return true;
    default:
        /* Every operation the format defines now has an evaluator, so nothing should reach
         * here. It stays because the type comes off the wire: a record naming an operation
         * outside the enum must fall back and be counted, not be trusted. */
        return false;
    }
}

bool
ProcTexGenerator_IsFullySupported(
    const struct RSCache_Dat2ProcTexture* texture,
    int* out_first_unsupported_type)
{
    assert(texture);
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
    assert(gen);
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
    if( gen->aux )
    {
        for( int i = 0; i < gen->cache_count; i++ )
            free(gen->aux[i]);
        free(gen->aux);
        gen->aux = NULL;
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
        assert(gen->h_gradient);
        for( int i = 0; i < size; i++ )
            gen->h_gradient[i] = (i << 12) / size;
        gen->width = size;
        /* Masks are used as `& mask` wraparound by the warp/mirror/noise operations, so they
         * assume a power-of-two size. Every bake size in use (64, 128) is one. */
        gen->width_mask = size - 1;
        gen->width_times_32 = size * 32;
    }
    if( gen->height != size )
    {
        free(gen->v_gradient);
        gen->v_gradient = malloc((size_t)size * sizeof(int32_t));
        assert(gen->v_gradient);
        for( int i = 0; i < size; i++ )
            gen->v_gradient[i] = (i << 12) / size;
        gen->height = size;
        gen->height_mask = size - 1;
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
    gen->caches =
        calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1), sizeof(*gen->caches));
    gen->tables =
        calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1), sizeof(*gen->tables));
    gen->visiting = calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1), 1);
    gen->aux = calloc((size_t)(gen->cache_count > 0 ? gen->cache_count : 1), sizeof(*gen->aux));
    assert(gen->aux);
    if( !gen->caches || !gen->tables || !gen->visiting )
        return false;

    for( int i = 0; i < gen->cache_count; i++ )
    {
        int planes = texture->operations[i].is_monochrome ? 1 : 3;
        gen->caches[i].done = calloc((size_t)size, 1);
        assert(gen->caches[i].done);
        for( int p = 0; p < planes; p++ )
        {
            gen->caches[i].plane[p] = calloc((size_t)size * (size_t)size, sizeof(int32_t));
            assert(gen->caches[i].plane[p]);
        }
    }
    return true;
}

/* --- evaluation -------------------------------------------------------------------------- */

/* Forward declaration; defined below and used by the ops unit. */

/** Monochrome view of input `n` of `op_index`, for `line`. Never NULL. */
const int32_t*
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
bool
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
    assert(table);
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

/*
 * The six built-in gradient palettes.
 *
 * A gradient operation either carries its stops inline (`preset == 0`) or names one of these.
 * They are constants in the client, not cache data, so they have to be transcribed — from
 * rs-map-viewer's GradientOperation.setGradientPreset. Positions and channels are 12.4, and
 * the channels are already pre-shifted the same way an inline stop is (`byte << 4`), so both
 * paths feed the same interpolator.
 *
 * Order matters and is not sorted in the source: the reference assigns the four slots of each
 * stop in a scrambled order but the *stops themselves* ascend by position, which is what the
 * interpolator's linear scan relies on. They are written here in position order.
 */
static const struct RSCache_ProcTexGradientStop PROCTEX_GRADIENT_PRESETS[7][16] = {
    /* [0] unused: presets are 1-based. */
    { { 0, 0, 0, 0 } },
    /* 1: black to white. Also the fallback when a record names no gradient at all. */
    { { 0, 0, 0, 0 }, { 4096, 4096, 4096, 4096 } },
    /* 2: browns — bark and dirt. */
    { { 0, 2650, 2602, 2361 },
      { 2867, 2313, 1799, 1558 },
      { 3072, 2618, 1734, 1413 },
      { 3276, 2296, 1220, 947 },
      { 3481, 2072, 963, 722 },
      { 3686, 2730, 2152, 1766 },
      { 3891, 2232, 1060, 915 },
      { 4096, 1686, 1413, 1140 } },
    /* 3: full hue wheel. */
    { { 0, 0, 0, 4096 },
      { 663, 0, 4096, 4096 },
      { 1363, 0, 4096, 0 },
      { 2048, 4096, 4096, 0 },
      { 2727, 4096, 0, 0 },
      { 3411, 4096, 0, 4096 },
      { 4096, 0, 0, 4096 } },
    /* 4: fire — black through red and orange to white. */
    { { 0, 0, 0, 0 },
      { 1843, 0, 0, 1493 },
      { 2457, 0, 0, 2939 },
      { 2781, 0, 1124, 3565 },
      { 3481, 546, 3084, 4031 },
      { 4096, 4096, 4096, 4096 } },
    /* 5: a 16-stop ramp through blues and greys. */
    { { 0, 80, 192, 321 },
      { 155, 321, 449, 562 },
      { 389, 578, 690, 803 },
      { 671, 947, 995, 1140 },
      { 897, 1285, 1397, 1509 },
      { 1175, 1525, 1429, 1413 },
      { 1368, 1734, 1461, 1333 },
      { 1507, 1413, 1525, 1702 },
      { 1736, 1108, 1590, 2056 },
      { 2088, 1766, 2056, 2666 },
      { 2355, 2409, 2586, 3276 },
      { 2691, 3116, 3148, 3228 },
      { 3031, 3806, 3710, 3196 },
      { 3522, 3437, 3421, 3019 },
      { 3727, 3116, 3148, 3228 },
      { 4096, 2377, 2505, 2746 } },
    /* 6: cyan to yellow. */
    { { 2048, 0, 4096, 0 }, { 2867, 4096, 4096, 0 }, { 3276, 4096, 4096, 0 }, { 4096, 4096, 0, 0 } },
};

static const int PROCTEX_GRADIENT_PRESET_COUNTS[7] = { 0, 2, 8, 7, 6, 16, 4 };

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
    assert(table);

    /*
     * No inline stops. Three cases, and they do not collapse:
     *
     *   - the record has no gradient field at all -> the reference's init() substitutes
     *     preset 1, black to white;
     *   - the field is present, names preset 0 and then lists zero stops -> the reference
     *     keeps its (empty) stop list, so init() does NOT substitute and fillTable leaves the
     *     table black. Reproduced by leaving the calloc'd table alone;
     *   - the field names a preset outside the six -> the reference throws. Nothing to
     *     render, so it is counted unsupported and the texture is refused.
     */
    if( count <= 0 )
    {
        int preset = op->u.gradient.preset;
        bool field_present = (op->field_present & 1u) != 0;

        if( preset == 0 && field_present )
        {
            gen->tables[op_index] = table;
            return table;
        }
        if( preset == 0 )
            preset = 1;
        if( preset > 6 )
        {
            gen->unsupported_count++;
            for( int i = 0; i < 257; i++ )
            {
                int value = proctex_clamp_i((i * 255) / 256, 0, 255);
                table[i] = (value << 16) | (value << 8) | value;
            }
            gen->tables[op_index] = table;
            return table;
        }
        stops = PROCTEX_GRADIENT_PRESETS[preset];
        count = PROCTEX_GRADIENT_PRESET_COUNTS[preset];
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

bool
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

    /*
     * Operations living in proctex_ops.u.c. They write straight into this operation's cache
     * planes, so they need the index rather than the buffers, and they return false only on a
     * hard failure (a missing input) — not on an unsupported feature.
     */
    case RSCACHE_PROCTEX_MIXER:
        if( !proctex_op_mixer(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_BLUR:
        if( !proctex_op_blur(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_EMBOSS:
        if( !proctex_op_emboss(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_PERLIN_NOISE:
        if( !proctex_op_perlin(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_VORONOI_NOISE:
        if( !proctex_op_voronoi(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_TRIG_WARP:
        if( !proctex_op_trig_warp(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_PSEUDO_RANDOM_NOISE:
        if( !proctex_op_pseudo_random(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_TILING:
        if( !proctex_op_tiling(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_HSL:
        if( !proctex_op_hsl(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_MIRROR:
        if( !proctex_op_mirror(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_SQUARE_WAVEFORM:
        if( !proctex_op_square_waveform(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_BRIGHTNESS:
        if( !proctex_op_brightness(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_MONO_EDGE_DETECT:
        if( !proctex_op_mono_edge(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_COLOUR_EDGE_DETECT:
        if( !proctex_op_colour_edge(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_WEAVE:
        if( !proctex_op_weave(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_HERRINGBONE:
        if( !proctex_op_herringbone(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_MANDELBROT:
        if( !proctex_op_mandelbrot(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_OP37:
        if( !proctex_op_op37(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_KALEIDOSCOPE:
        if( !proctex_op_kaleidoscope(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_BRICKS:
        if( !proctex_op_bricks(gen, op_index, line) )
            goto fail;
        break;
    /*
     * Whole-image operations: they fill every scanline of their own cache on the first
     * request and mark it all resident, because their output is not decomposable per line —
     * line_noise strokes cross scanlines, an irregular brick's top depends on the row below
     * it, and a rasterised shape spans whatever it spans.
     */
    case RSCACHE_PROCTEX_LINE_NOISE:
        if( !proctex_op_line_noise(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_IRREGULAR_BRICKS:
        if( !proctex_op_irregular_bricks(gen, op_index, line) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_RASTERIZER:
        if( !proctex_op_rasterizer(gen, op_index, line) )
            goto fail;
        break;
    /*
     * These two reach outside the graph — to a sprite, or to another texture's program. The
     * host resolves both, having made the dependency closure resident before the bake started;
     * a false return here means the dependency genuinely is not available, which fails the
     * texture rather than substituting anything.
     */
    case RSCACHE_PROCTEX_SPRITE_SOURCE:
        if( !proctex_op_sprite_source(gen, op_index, line, 0) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_TILING_SPRITE:
        if( !proctex_op_sprite_source(gen, op_index, line, 1) )
            goto fail;
        break;
    case RSCACHE_PROCTEX_TEXTURE_SOURCE:
        if( !proctex_op_texture_source(gen, op_index, line) )
            goto fail;
        break;

    default:
        /*
         * Not ported yet. Mid-grey is a deliberately neutral, obviously-flat result: it keeps
         * the graph evaluable so the rest of the texture can be inspected, while the count
         * lets the caller refuse to publish the image. It is never a claim of correctness.
         */
        if( gen->unsupported_count == 0 && getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(
                stderr,
                "  proctex %d: op %d type %d (%s) has no evaluator — flat grey\n",
                gen->tex->id,
                op_index,
                op->type,
                RSCache_ProcTexOpName(op->type));
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
    /* An evaluator refused. This is distinct from an unported operation (which returns a flat
     * value and bumps unsupported_count): it means a dependency the graph needs is genuinely
     * absent — a missing input wiring, or a sprite/texture the host could not resolve. Worth
     * naming, because the two look identical from the caller's `render failed`. */
    if( getenv("TORIRS_PROCTEX_DEBUG") )
        fprintf(
            stderr,
            "  proctex: op %d type %d (%s) REFUSED at line %d (inputs=%d)\n",
            op_index,
            op->type,
            RSCache_ProcTexOpName(op->type),
            line,
            op->input_count);
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
    /* The refusals below are silent by default but each one means something different, and
     * they are indistinguishable from the caller's `render failed`. Name them so a texture
     * that will not bake can be diagnosed without a debugger. */
    if( size <= 0 )
        return false;
    assert(gen);
    assert(texture);
    assert(out_argb);
    if( texture->operation_count <= 0 || texture->colour_op < 0 )
    {
        if( getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(
                stderr,
                "  proctex %d: no output — %d operations, colour_op %d\n",
                texture->id,
                texture->operation_count,
                texture->colour_op);
        return false;
    }
    if( !proctex_setup(gen, texture, size, brightness) )
    {
        if( getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(stderr, "  proctex %d: setup failed at size %d\n", texture->id, size);
        return false;
    }

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
            colour[0] = colour[1] = colour[2] = gen->caches[texture->colour_op].plane[0] + offset;
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
    {
        /* An operation is wired, directly or through a chain, to its own output. The format
         * cannot express that meaningfully and nothing in the cache validates it, so it is a
         * property of the record rather than of this port. */
        if( getenv("TORIRS_PROCTEX_DEBUG") )
            fprintf(stderr, "  proctex %d: operation graph has a cycle\n", texture->id);
        return false;
    }

    if( out_unsupported )
        *out_unsupported = gen->unsupported_count;
    if( out_transparent )
        *out_transparent = gen->is_transparent;
    return true;
}

/* Operation evaluators live in their own unit, included here so they share this file's
 * statics and stay out of the Makefile (the same arrangement world_builder.c uses). */
#include "engine/proctex/proctex_ops.u.c"
