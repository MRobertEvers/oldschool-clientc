#include "title_flames.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Structure of the effect, since the loops below are terse.
 *
 *   heat[]  is the fire: one intensity 0..255 per cell of a 128x256 grid.
 *           Each tick seeds the bottom row and a band of embers, diffuses
 *           upward, and subtracts the cooling map. That subtraction is the
 *           whole trick — without it the grid saturates to a white slab.
 *
 *   cool[]  is the cooling map: smoothed noise, regenerated every time the
 *           read cursor laps it, with a rune silhouette punched to zero so the
 *           fire burns *hotter* where the rune is and the shape reads out of
 *           the flame rather than being drawn on top of it.
 *
 *   line[]  is a per-row horizontal shear, scrolling upward one row per tick,
 *           with a fresh value pushed in at the bottom from three summed sines.
 *           It is what makes the fire lean rather than rise straight.
 *
 * Every magic number here (10..116, 100 embers, 5000 noise cells, 20 smoothing
 * passes, /5, 22, 103, 1024, 2000) is the reference client's. They are not
 * derived from anything and changing them changes the look, so they are left as
 * literals at their use sites rather than named into a false generality.
 */

#define GRID_W TITLE_FLAMES_WIDTH
#define GRID_H TITLE_FLAMES_HEIGHT
/** 128 wide, so a row step is a shift. The reference depends on this too. */
#define GRID_SHIFT 7
#define GRID_CELLS (GRID_W * GRID_H)
/** cool[] is indexed with a wrapping mask, which needs the size to be a power of two. */
#define GRID_MASK (GRID_CELLS - 1)

/** Where the fire starts inside the panel: row 9, and 24 columns in on the right. */
#define PANEL_TOP_ROW 9
#define PANEL_RIGHT_INSET 24

#define PALETTE_SIZE 256

const int TitleFlames_ClassicRamps[TITLE_FLAMES_RAMP_ROWS][TITLE_FLAMES_RAMP_STOPS] = {
    { 0x000000, 0xFF0000, 0xFFFF00, 0xFFFFFF, 0xFFFFFF },
    { 0x000000, 0x00FF00, 0x00FFFF, 0xFFFFFF, 0xFFFFFF },
    { 0x000000, 0x0000FF, 0xFF00FF, 0xFFFFFF, 0xFFFFFF },
};

struct TitleFlames
{
    uint32_t palette[TITLE_FLAMES_RAMP_ROWS][PALETTE_SIZE];
    uint32_t working[PALETTE_SIZE];

    int32_t* heat;
    int32_t* heat_next;
    int32_t* cool;
    int32_t* cool_next;

    int line[GRID_H];

    int cool_cursor;
    /** Countdowns for the fade to palette 1 and to palette 2. At most one runs. */
    int fade_to_1;
    int fade_to_2;
    /** Wrapping phase of the three shear oscillators. */
    uint32_t phase_14;
    uint32_t phase_15;
    uint32_t phase_16;

    const struct TitleFlames_Mask* masks;
    int mask_count;

    uint32_t rng;
};

/*
 * Three sine oscillators, without libm.
 *
 * The reference computes sin(cycle/14), sin(cycle/15) and sin(cycle/16) on
 * doubles. Calling into libm for that would make this module need -lm, which
 * the client does not link today and which is a strange thing for a decorative
 * effect to demand. So the phase is accumulated instead: a uint32 turn counter
 * per oscillator, incremented by a fixed step and left to wrap, which *is* the
 * modulo-2pi that sin would have done.
 *
 * A step is 2^32 / (2*pi*period) phase units per tick. The rounding error is
 * about two parts in a billion of the frequency; the value ends up scaled by 16
 * and truncated to a whole pixel, so nothing survives to be seen.
 *
 * Measured against the reference expression over 200,000 cycles: worst error
 * 0.047 of a pixel, and the truncated shear differs by one in 1.15% of cycles,
 * always at a rounding boundary. The shear spans -41..41 either way.
 */
#define PHASE_STEP_14 48826091u
#define PHASE_STEP_15 45571018u
#define PHASE_STEP_16 42722830u

/**
 * sin(2*pi * phase / 2^32), to about two parts in a thousand.
 *
 * The parabola 4z(1-|z|) is the classic half-turn approximation and is ~5% out;
 * the second line is the standard weighted refinement that pulls it to ~0.2%.
 * Exact at the quarter turns, which is the part that matters — those are the
 * extremes the shear is measured by.
 */
static double
phase_sin(uint32_t phase)
{
    /* Reinterpreting as signed maps the turn onto [-1, 1), which is the domain
     * the parabola wants, and does the wrap for free. */
    double z = (double)(int32_t)phase / 2147483648.0;
    double abs_z = z < 0.0 ? -z : z;
    double y = 4.0 * z * (1.0 - abs_z);
    double abs_y = y < 0.0 ? -y : y;

    return 0.775 * y + 0.225 * y * abs_y;
}

static uint32_t
next_random(struct TitleFlames* flames)
{
    uint32_t x = flames->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    flames->rng = x;
    return x;
}

/** `Math.random() * bound | 0` — a non-negative int below `bound`. */
static int
random_below(struct TitleFlames* flames, int bound)
{
    assert(bound > 0);
    return (int)(next_random(flames) % (uint32_t)bound);
}

/**
 * One step between two stops: a per-channel blend, 64 steps to a segment.
 *
 * The `+ 1` on the delta is the reference's and is not a rounding fix — it is
 * what makes the last step of a segment reach the next stop exactly, so the
 * four segments meet without a seam.
 */
static uint32_t
blend_stop(uint32_t from, uint32_t to, int step)
{
    int r0 = (int)((from >> 16) & 0xFF);
    int g0 = (int)((from >> 8) & 0xFF);
    int b0 = (int)(from & 0xFF);
    int r1 = (int)((to >> 16) & 0xFF);
    int g1 = (int)((to >> 8) & 0xFF);
    int b1 = (int)(to & 0xFF);
    int r = ((r1 - r0 + 1) * step / 64 + r0) & 0xFF;
    int g = ((g1 - g0 + 1) * step / 64 + g0) & 0xFF;
    int b = ((b1 - b0 + 1) * step / 64 + b0) & 0xFF;

    return (uint32_t)((r << 16) | (g << 8) | b);
}

/** Five stops become 256 entries: four segments of 64. */
static void
expand_ramp(const int stops[TITLE_FLAMES_RAMP_STOPS], uint32_t out[PALETTE_SIZE])
{
    assert(stops);
    assert(out);

    for( int segment = 0; segment < TITLE_FLAMES_RAMP_STOPS - 1; segment++ )
    {
        uint32_t from = (uint32_t)stops[segment] & 0xFFFFFF;
        uint32_t to = (uint32_t)stops[segment + 1] & 0xFFFFFF;

        for( int step = 0; step < 64; step++ )
            out[segment * 64 + step] = blend_stop(from, to, step);
    }
}

/**
 * Blend two packed RGBs, `alpha` in 0..256 weighting `to`.
 *
 * Both channel groups are carried in one multiply each — the classic
 * two-mask trick. Everything is unsigned so the 0xFF00FF00 term does not
 * become negative on the way through the shift.
 */
static uint32_t
blend_rgb(uint32_t from, uint32_t to, uint32_t alpha)
{
    uint32_t inverse = 256u - alpha;
    uint32_t rb = ((from & 0xFF00FFu) * inverse + (to & 0xFF00FFu) * alpha) & 0xFF00FF00u;
    uint32_t g = ((from & 0xFF00u) * inverse + (to & 0xFF00u) * alpha) & 0xFF0000u;

    return ((rb + g) >> 8) & 0xFFFFFFu;
}

/**
 * Rebuild the cooling map: noise, smoothed, then a rune punched out of it.
 *
 * The rune is set to zero rather than drawn: cool[] is *subtracted* from the
 * fire, so a hole in it is a place the fire is not cooled, and the shape
 * appears as the brightest part of the flame.
 */
static void
generate_cooling_map(struct TitleFlames* flames)
{
    const struct TitleFlames_Mask* mask = NULL;

    assert(flames);

    memset(flames->cool, 0, (size_t)GRID_CELLS * sizeof(*flames->cool));
    for( int i = 0; i < 5000; i++ )
        flames->cool[random_below(flames, GRID_CELLS)] = random_below(flames, 256);

    for( int pass = 0; pass < 20; pass++ )
    {
        int32_t* swap;

        for( int y = 1; y < GRID_H - 1; y++ )
        {
            for( int x = 1; x < GRID_W - 1; x++ )
            {
                int i = x + (y << GRID_SHIFT);

                flames->cool_next[i] =
                    (flames->cool[i - 1] + flames->cool[i + 1] + flames->cool[i - GRID_W] +
                     flames->cool[i + GRID_W]) /
                    4;
            }
        }
        swap = flames->cool;
        flames->cool = flames->cool_next;
        flames->cool_next = swap;
    }

    if( flames->mask_count > 0 )
        mask = &flames->masks[random_below(flames, flames->mask_count)];
    if( !mask )
        return;

    assert(mask->pixels);
    for( int y = 0; y < mask->height; y++ )
    {
        for( int x = 0; x < mask->width; x++ )
        {
            int px, py;

            if( mask->pixels[y * mask->width + x] == 0 )
                continue;
            px = x + mask->origin_x;
            py = y + mask->origin_y;
            /* The reference trusts the sprite to fit. A mask is caller data, so
             * this one checks rather than corrupting the grid behind it. */
            if( px < 0 || px >= GRID_W || py < 0 || py >= GRID_H )
                continue;
            flames->cool[px + (py << GRID_SHIFT)] = 0;
        }
    }
}

/** One simulation tick. Two of these make a drawn frame. */
static void
step_once(struct TitleFlames* flames)
{
    assert(flames);

    /* Seed the bottom row, thinly, so the base flickers rather than glows flat. */
    for( int x = 10; x < 117; x++ )
    {
        if( random_below(flames, 100) < 50 )
            flames->heat[x + ((GRID_H - 2) << GRID_SHIFT)] = 255;
    }
    /* Embers through the lower half. */
    for( int i = 0; i < 100; i++ )
    {
        int x = random_below(flames, 124) + 2;
        int y = random_below(flames, 128) + 128;

        flames->heat[x + (y << GRID_SHIFT)] = 192;
    }

    for( int y = 1; y < GRID_H - 1; y++ )
    {
        for( int x = 1; x < GRID_W - 1; x++ )
        {
            int i = x + (y << GRID_SHIFT);

            flames->heat_next[i] = (flames->heat[i - 1] + flames->heat[i + 1] +
                                    flames->heat[i - GRID_W] + flames->heat[i + GRID_W]) /
                                   4;
        }
    }

    /* The cooling map is read at a moving offset, so the same noise cools a
     * different part of the fire each tick; a lap of it earns a fresh map. */
    flames->cool_cursor += GRID_W;
    if( flames->cool_cursor > GRID_CELLS )
    {
        flames->cool_cursor -= GRID_CELLS;
        generate_cooling_map(flames);
    }

    for( int y = 1; y < GRID_H - 1; y++ )
    {
        for( int x = 1; x < GRID_W - 1; x++ )
        {
            int i = x + (y << GRID_SHIFT);
            /* Sample one row down: the fire rises because each cell takes the
             * diffused value from below it. */
            int value =
                flames->heat_next[i + GRID_W] - flames->cool[(i + flames->cool_cursor) & GRID_MASK] / 5;

            flames->heat[i] = value < 0 ? 0 : value;
        }
    }

    /* Scroll the shear upward and push a new value in at the bottom. Three
     * sines with no common period, so the lean never visibly repeats. */
    for( int y = 0; y < GRID_H - 1; y++ )
        flames->line[y] = flames->line[y + 1];
    flames->line[GRID_H - 1] = (int)(phase_sin(flames->phase_14) * 16.0 +
                                     phase_sin(flames->phase_15) * 14.0 +
                                     phase_sin(flames->phase_16) * 12.0);

    if( flames->fade_to_1 > 0 )
        flames->fade_to_1 -= 4;
    if( flames->fade_to_2 > 0 )
        flames->fade_to_2 -= 4;
    /* Only start a fade from rest, so the two never overlap and the working
     * palette always has exactly one journey to be partway through. */
    if( flames->fade_to_1 == 0 && flames->fade_to_2 == 0 )
    {
        int roll = random_below(flames, 2000);

        if( roll == 0 )
            flames->fade_to_1 = 1024;
        else if( roll == 1 )
            flames->fade_to_2 = 1024;
    }
}

/**
 * Resolve the working palette for this frame.
 *
 * A fade counts 1024 down to 0 and is three phases, not a straight lerp: fade
 * out over the first 256 ticks, hold the destination palette for 512, fade back
 * over the last 256. That hold is why the fire visibly *is* green for a moment
 * rather than only passing through it.
 */
static void
resolve_palette(struct TitleFlames* flames)
{
    const uint32_t* base = flames->palette[0];
    const uint32_t* other;
    int fade;

    assert(flames);

    if( flames->fade_to_1 > 0 )
    {
        other = flames->palette[1];
        fade = flames->fade_to_1;
    }
    else if( flames->fade_to_2 > 0 )
    {
        other = flames->palette[2];
        fade = flames->fade_to_2;
    }
    else
    {
        memcpy(flames->working, base, sizeof(flames->working));
        return;
    }

    for( int i = 0; i < PALETTE_SIZE; i++ )
    {
        if( fade > 768 )
            flames->working[i] = blend_rgb(base[i], other[i], (uint32_t)(1024 - fade));
        else if( fade > 256 )
            flames->working[i] = other[i];
        else
            flames->working[i] = blend_rgb(other[i], base[i], (uint32_t)(256 - fade));
    }
}

/** The shear applied to row `y`, tapering to nothing at the top of the grid. */
static int
row_shear(const struct TitleFlames* flames, int y)
{
    return flames->line[y] * (GRID_H - y) / GRID_H;
}

static void
composite(uint32_t* dst, uint32_t rgb, uint32_t intensity)
{
    /* Intensity is both the palette index and the blend weight: a dim pixel is
     * both a darker colour and more transparent, which is what keeps the top of
     * the flame from ending on a hard edge. */
    *dst = (*dst & 0xFF000000u) | blend_rgb(*dst & 0xFFFFFFu, rgb, intensity);
}

void
TitleFlames_RenderLeft(const struct TitleFlames* flames, uint32_t* panel)
{
    int src = 0;
    int dst = PANEL_TOP_ROW << GRID_SHIFT;

    assert(flames);
    assert(panel);

    for( int y = 1; y < GRID_H - 1; y++ )
    {
        int step = row_shear(flames, y) + 22;

        if( step < 0 )
            step = 0;
        /* Skipping `step` source cells and `step` destination cells at opposite
         * ends of the row is the shear: the row is drawn short and pushed right. */
        src += step;
        for( int x = step; x < GRID_W; x++ )
        {
            uint32_t intensity = (uint32_t)flames->heat[src++];

            if( intensity == 0 )
                dst++;
            else
                composite(&panel[dst++], flames->working[intensity], intensity);
        }
        dst += step;
    }
}

void
TitleFlames_RenderRight(const struct TitleFlames* flames, uint32_t* panel)
{
    int src = 0;
    int dst = (PANEL_TOP_ROW << GRID_SHIFT) + PANEL_RIGHT_INSET;

    assert(flames);
    assert(panel);

    for( int y = 1; y < GRID_H - 1; y++ )
    {
        int shear = row_shear(flames, y);
        int step = 103 - shear;

        /* Mirrored: the right panel pads the destination first and truncates the
         * row's tail, so the same heat buffer leans the other way. */
        dst += shear;
        for( int x = 0; x < step; x++ )
        {
            uint32_t intensity = (uint32_t)flames->heat[src++];

            if( intensity == 0 )
                dst++;
            else
                composite(&panel[dst++], flames->working[intensity], intensity);
        }
        src += GRID_W - step;
        dst += GRID_W - step - shear;
    }
}

void
TitleFlames_Frame(struct TitleFlames* flames)
{
    assert(flames);

    /* Once per frame, not once per tick: the reference samples its sines off the
     * client's own loop counter, so both ticks in a frame see the same shear. */
    flames->phase_14 += PHASE_STEP_14;
    flames->phase_15 += PHASE_STEP_15;
    flames->phase_16 += PHASE_STEP_16;
    step_once(flames);
    step_once(flames);
    resolve_palette(flames);
}

void
TitleFlames_SetMasks(
    struct TitleFlames* flames,
    const struct TitleFlames_Mask* masks,
    int mask_count)
{
    assert(flames);
    assert(mask_count >= 0);
    if( mask_count > 0 )
        assert(masks);

    flames->masks = masks;
    flames->mask_count = mask_count;
}

const uint32_t*
TitleFlames_Palette(const struct TitleFlames* flames)
{
    assert(flames);
    return flames->working;
}

struct TitleFlames*
TitleFlames_New(
    const int ramps[TITLE_FLAMES_RAMP_ROWS][TITLE_FLAMES_RAMP_STOPS],
    uint32_t seed)
{
    struct TitleFlames* flames;

    assert(ramps);

    flames = (struct TitleFlames*)calloc(1, sizeof(*flames));
    assert(flames);
    flames->heat = (int32_t*)calloc(GRID_CELLS, sizeof(*flames->heat));
    assert(flames->heat);
    flames->heat_next = (int32_t*)calloc(GRID_CELLS, sizeof(*flames->heat_next));
    assert(flames->heat_next);
    flames->cool = (int32_t*)calloc(GRID_CELLS, sizeof(*flames->cool));
    assert(flames->cool);
    flames->cool_next = (int32_t*)calloc(GRID_CELLS, sizeof(*flames->cool_next));
    assert(flames->cool_next);

    /* 0 is xorshift32's fixed point — it would shift to 0 forever and the fire
     * would be identical every frame. Any other constant would do. */
    flames->rng = seed ? seed : 0x9E3779B9u;

    for( int row = 0; row < TITLE_FLAMES_RAMP_ROWS; row++ )
        expand_ramp(ramps[row], flames->palette[row]);
    memcpy(flames->working, flames->palette[0], sizeof(flames->working));

    generate_cooling_map(flames);
    return flames;
}

void
TitleFlames_Free(struct TitleFlames* flames)
{
    if( !flames )
        return;
    free(flames->heat);
    free(flames->heat_next);
    free(flames->cool);
    free(flames->cool_next);
    free(flames);
}
