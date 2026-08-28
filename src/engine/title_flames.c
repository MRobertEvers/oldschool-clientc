#include "engine/title_flames.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Reference constants (Client-TS updateFlames/drawFlames, deob class77).
 *
 * Every one of these is the simulation's own shape rather than anything a
 * revision restates, which is why they are here and not in revconfig: the
 * profile chooses the COLOURS and where the columns go; the physics is the
 * physics.
 */
/** The fire is fed from this row, two above the bottom. */
#define FLAME_SOURCE_ROW (TORIRS_FLAME_H - 2)
/** Feeding stops short of the edges so the column has a dark margin. */
#define FLAME_SOURCE_X0 10
#define FLAME_SOURCE_X1 117
/** Loose embers per step, thrown into the lower half. */
#define FLAME_SPARKS 100
#define FLAME_SPARK_HEAT 192
/** Seeds scattered into a fresh cooling map, then smoothed this many times. */
#define FLAME_COOLING_SEEDS 5000
#define FLAME_COOLING_PASSES 20
/** How far the cooling map scrolls per step, and the divisor on its bite. */
#define FLAME_COOLING_SCROLL TORIRS_FLAME_W
#define FLAME_COOLING_DIVISOR 5
/** A cross-fade runs from here to zero, four counts per step. */
#define FLAME_FADE_TICKS 1024
#define FLAME_FADE_STEP 4
/** Odds per step of starting a cross-fade once both are idle. */
#define FLAME_FADE_ODDS 2000
/** The step the reference's 35 ms timer takes, twice per tick. */
#define FLAME_STEP_MS 35
#define FLAME_UPDATES_PER_STEP 2
/** A stall must not be paid back all at once. */
#define FLAME_MAX_STEPS_PER_ADVANCE 3
/** Rune glyphs are stamped this far into the map, away from its edges. */
#define FLAME_RUNE_INSET 16

static uint32_t
flame_rand(struct TitleFlames* flames)
{
    /* Numerical Recipes' LCG. Private and seeded rather than rand(), because a
     * fire that differs run to run cannot be golden-tested. */
    flames->rng = flames->rng * 1664525u + 1013904223u;
    return flames->rng;
}

static int
flame_rand_below(
    struct TitleFlames* flames,
    int bound)
{
    assert(bound > 0);
    return (int)((flame_rand(flames) >> 8) % (uint32_t)bound);
}

/* One palette: five stops spread across four bands of 64 (deob method7884). */
static void
build_palette(
    uint32_t* out,
    int const stops[TORIRS_FLAME_PALETTE_STOPS])
{
    assert(out);
    assert(stops);

    for( int band = 0; band < TORIRS_FLAME_PALETTE_STOPS - 1; band++ )
    {
        int from = stops[band];
        int to = stops[band + 1];
        for( int i = 0; i < 64; i++ )
        {
            int r = (((from >> 16) & 0xFF) * (64 - i) + ((to >> 16) & 0xFF) * i) / 64;
            int g = (((from >> 8) & 0xFF) * (64 - i) + ((to >> 8) & 0xFF) * i) / 64;
            int b = ((from & 0xFF) * (64 - i) + (to & 0xFF) * i) / 64;
            out[band * 64 + i] = (uint32_t)((r << 16) | (g << 8) | b);
        }
    }
}

void
TitleFlames_SetPalettes(
    struct TitleFlames* flames,
    int const stops[TORIRS_FLAME_PALETTES][TORIRS_FLAME_PALETTE_STOPS])
{
    assert(flames);
    assert(stops);
    for( int p = 0; p < TORIRS_FLAME_PALETTES; p++ )
        build_palette(flames->palette[p], stops[p]);
    memcpy(flames->active_palette, flames->palette[0], sizeof(flames->active_palette));
}

/*
 * A fresh cooling map: noise, smoothed, with one rune glyph punched through it
 * as zeroes.
 *
 * The zeroes are the point. Cooling subtracts this map from the heat field, so
 * a region of zero cools nothing and the glyph rises through the fire as a
 * bright silhouette. Nothing draws the rune; it is a hole in the cooling.
 */
static void
generate_cooling_map(
    struct TitleFlames* flames,
    struct TitleFlameRune const* rune)
{
    assert(flames);

    memset(flames->cooling, 0, sizeof(flames->cooling));
    for( int i = 0; i < FLAME_COOLING_SEEDS; i++ )
        flames->cooling[flame_rand_below(flames, TORIRS_FLAME_CELLS)] =
            flame_rand_below(flames, 256);

    for( int pass = 0; pass < FLAME_COOLING_PASSES; pass++ )
    {
        for( int y = 1; y < TORIRS_FLAME_H - 1; y++ )
        {
            for( int x = 1; x < TORIRS_FLAME_W - 1; x++ )
            {
                int at = y * TORIRS_FLAME_W + x;
                flames->cooling_scratch[at] =
                    (flames->cooling[at - 1] + flames->cooling[at + 1] +
                     flames->cooling[at - TORIRS_FLAME_W] + flames->cooling[at + TORIRS_FLAME_W]) /
                    4;
            }
        }
        memcpy(flames->cooling, flames->cooling_scratch, sizeof(flames->cooling));
    }

    if( !rune || !rune->mask )
        return;

    for( int y = 0; y < rune->height; y++ )
    {
        for( int x = 0; x < rune->width; x++ )
        {
            int out_x;
            int out_y;

            if( !rune->mask[y * rune->width + x] )
                continue;
            out_x = x + rune->offset_x + FLAME_RUNE_INSET;
            out_y = y + rune->offset_y + FLAME_RUNE_INSET;
            if( out_x < 0 || out_x >= TORIRS_FLAME_W || out_y < 0 || out_y >= TORIRS_FLAME_H )
                continue;
            flames->cooling[out_y * TORIRS_FLAME_W + out_x] = 0;
        }
    }
}

/*
 * The deob's smoothing: a separable running-sum box blur (class77).
 *
 * Two passes, horizontal then vertical, each a sliding window carried as a
 * running total -- which is why the reference can afford a window at all on
 * 32768 cells. The window INCLUDES the centre, and that is the whole
 * difference from Client-TS's four-neighbour average: it mixes the two
 * checkerboard sublattices instead of letting them evolve apart, so the
 * fire has no dither.
 *
 * The radius alternates 0 and 1 with the step counter, exactly as
 * `((clientCycle & 1) + ticks) / 2` does at one tick per step. A radius of
 * zero is a copy, so half the steps smooth and half do not.
 */
static void
flame_blur_box(struct TitleFlames* flames)
{
    int radius = ((flames->update_index & 1) + 1) / 2;
    int span = radius * 2 + 1;

    assert(flames);
    if( radius <= 0 )
        return;

    for( int y = 0; y < TORIRS_FLAME_H; y++ )
    {
        int row = y * TORIRS_FLAME_W;
        int sum = 0;

        for( int x = -radius; x < TORIRS_FLAME_W; x++ )
        {
            if( x + radius < TORIRS_FLAME_W )
                sum += flames->heat[row + x + radius];
            if( x - (radius + 1) >= 0 )
                sum -= flames->heat[row + x - (radius + 1)];
            if( x >= 0 )
                flames->blur[row + x] = sum / span;
        }
    }

    for( int x = 0; x < TORIRS_FLAME_W; x++ )
    {
        int sum = 0;

        for( int y = -radius; y < TORIRS_FLAME_H; y++ )
        {
            if( y + radius < TORIRS_FLAME_H )
                sum += flames->blur[(y + radius) * TORIRS_FLAME_W + x];
            if( y - (radius + 1) >= 0 )
                sum -= flames->blur[(y - (radius + 1)) * TORIRS_FLAME_W + x];
            if( y >= 0 )
                flames->heat[y * TORIRS_FLAME_W + x] = sum / span;
        }
    }
}

/* One simulation step: feed, spark, blur, cool, wobble, and age the fades. */
static void
flame_update(struct TitleFlames* flames)
{
    assert(flames);

    /* Feed the source row. Half the columns each step, so the base flickers
     * rather than burning as a solid bar. */
    for( int x = FLAME_SOURCE_X0; x < FLAME_SOURCE_X1; x++ )
    {
        if( flame_rand_below(flames, 100) < 50 )
            flames->heat[FLAME_SOURCE_ROW * TORIRS_FLAME_W + x] = 255;
    }

    /* Embers in the lower half, which is what gives the fire its detached
     * flecks rather than one smooth plume. */
    for( int i = 0; i < FLAME_SPARKS; i++ )
    {
        int x = flame_rand_below(flames, TORIRS_FLAME_W - 4) + 2;
        int y = flame_rand_below(flames, TORIRS_FLAME_H / 2) + TORIRS_FLAME_H / 2;
        flames->heat[y * TORIRS_FLAME_W + x] = FLAME_SPARK_HEAT;
    }

    /*
     * Four-neighbour blur, for the lane whose reference does it here.
     *
     * The row below is included, which is what carries heat upward: each
     * cell inherits from the hotter row beneath it. The deob has no pass in
     * this position at all -- it smooths once, at the end, over the field it
     * is about to draw -- so running both would smooth twice and the fire
     * would come out dim and short.
     */
    if( flames->blur_kind != TORIRS_FLAME_BLUR_BOX )
    {
        for( int y = 1; y < TORIRS_FLAME_H - 1; y++ )
        {
            for( int x = 1; x < TORIRS_FLAME_W - 1; x++ )
            {
                int at = y * TORIRS_FLAME_W + x;
                flames->blur[at] = (flames->heat[at - 1] + flames->heat[at + 1] +
                                    flames->heat[at - TORIRS_FLAME_W] +
                                    flames->heat[at + TORIRS_FLAME_W]) /
                                   4;
            }
        }
    }

    /* Scroll the cooling map; on a wrap, punch a new rune through it. */
    flames->cooling_scroll += FLAME_COOLING_SCROLL;
    if( flames->cooling_scroll >= TORIRS_FLAME_CELLS )
    {
        flames->cooling_scroll -= TORIRS_FLAME_CELLS;
        generate_cooling_map(
            flames,
            flames->rune_count > 0 ? &flames->runes[flame_rand_below(flames, flames->rune_count)]
                                   : NULL);
    }

    /* Lift the blurred field by one row and cool it. The row shift is the
     * upward motion; the subtraction is what shapes the flame. */
    {
        /* The deob cools straight off the heat field; Client-TS cools off the
         * blurred copy it just made. Reading `at + W` ahead of the write at
         * `at` is safe in both cases -- the row below has not been reached. */
        int32_t const* from =
            flames->blur_kind == TORIRS_FLAME_BLUR_BOX ? flames->heat : flames->blur;

        for( int at = 0; at < TORIRS_FLAME_CELLS - TORIRS_FLAME_W; at++ )
        {
            int cooled =
                from[at + TORIRS_FLAME_W] -
                flames->cooling[(at + flames->cooling_scroll) & (TORIRS_FLAME_CELLS - 1)] /
                    FLAME_COOLING_DIVISOR;
            flames->heat[at] = cooled < 0 ? 0 : cooled;
        }
    }

    /* The per-row sway, shifted up a row per step so a wobble travels with the
     * flame instead of the whole column swinging at once. Three sines with no
     * common period, which is why it never visibly repeats. */
    for( int y = 0; y < TORIRS_FLAME_H - 1; y++ )
        flames->line_offset[y] = flames->line_offset[y + 1];
    flames->line_offset[TORIRS_FLAME_H - 1] =
        (int)(sin((double)flames->wobble_phase / 14.0) * 16.0 +
              sin((double)flames->wobble_phase / 15.0) * 14.0 +
              sin((double)flames->wobble_phase / 16.0) * 12.0);
    flames->wobble_phase++;

    /* The deob smooths at the END of its update, over the field it is about
     * to draw; Client-TS's blur has already happened above, feeding the cool.
     * A profile that asks for neither gets the older one. */
    if( flames->blur_kind == TORIRS_FLAME_BLUR_BOX )
        flame_blur_box(flames);
    flames->update_index++;

    for( int i = 0; i < TORIRS_FLAME_PALETTES - 1; i++ )
    {
        if( flames->fade[i] > 0 )
            flames->fade[i] -= FLAME_FADE_STEP;
        if( flames->fade[i] < 0 )
            flames->fade[i] = 0;
    }
    if( flames->fade[0] == 0 && flames->fade[1] == 0 )
    {
        int roll = flame_rand_below(flames, FLAME_FADE_ODDS);
        if( roll == 0 )
            flames->fade[0] = FLAME_FADE_TICKS;
        else if( roll == 1 )
            flames->fade[1] = FLAME_FADE_TICKS;
    }
}

static uint32_t
lerp_rgb(
    uint32_t from,
    uint32_t to,
    int numerator,
    int denominator)
{
    int inv = denominator - numerator;
    int r = (int)(((from >> 16) & 0xFF) * inv + ((to >> 16) & 0xFF) * numerator) / denominator;
    int g = (int)(((from >> 8) & 0xFF) * inv + ((to >> 8) & 0xFF) * numerator) / denominator;
    int b = (int)((from & 0xFF) * inv + (to & 0xFF) * numerator) / denominator;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

/* The palette in force: the base one, or partway to an alternate. The fade
 * runs out and back -- over to the alternate, a while there, then home -- which
 * is the reference's three-branch shape. */
static void
resolve_palette(struct TitleFlames* flames)
{
    int which = flames->fade[0] > 0 ? 0 : (flames->fade[1] > 0 ? 1 : -1);
    int ticks;

    if( which < 0 )
    {
        memcpy(flames->active_palette, flames->palette[0], sizeof(flames->active_palette));
        return;
    }

    ticks = flames->fade[which];
    for( int i = 0; i < TORIRS_FLAME_PALETTE_ENTRIES; i++ )
    {
        uint32_t base = flames->palette[0][i];
        uint32_t alt = flames->palette[which + 1][i];

        if( ticks > 768 )
            flames->active_palette[i] = lerp_rgb(base, alt, FLAME_FADE_TICKS - ticks, 256);
        else if( ticks > 256 )
            flames->active_palette[i] = alt;
        else
            flames->active_palette[i] = lerp_rgb(alt, base, 256 - ticks, 256);
    }
}

/* Blend the heat field over the restored backdrop, one column per side. */
static void
flame_composite(struct TitleFlames* flames)
{
    assert(flames);
    resolve_palette(flames);

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        size_t bytes = (size_t)flames->width * flames->height * sizeof(uint32_t);
        uint32_t* out = flames->out[side];
        struct TitleFlameGeometry const* geom = &flames->geometry[side];
        int run;

        /* Restore first: the fire is translucent, and compositing onto the
         * previous frame's result would smear it into a solid smear. */
        memcpy(out, flames->background[side], bytes);

        /* A side the profile never placed burns nowhere. Drawing it at 0,0
         * instead would put a fire in the middle of a wall. */
        if( !flames->has_geometry[side] )
            continue;

        run = geom->run > 0 && geom->run < TORIRS_FLAME_W ? geom->run : TORIRS_FLAME_W;

        for( int y = 1; y < TORIRS_FLAME_H - 1; y++ )
        {
            /* The sway fades out toward the top, so the flame leans at its
             * base and stands straight where it thins. */
            int offset = flames->line_offset[y] * (TORIRS_FLAME_H - y) / TORIRS_FLAME_H;
            int shift = geom->bias + geom->sway * offset;
            int out_y = y + geom->row;

            if( out_y < 0 || out_y >= flames->height )
                continue;

            for( int x = 0; x < run; x++ )
            {
                int heat = flames->heat[y * TORIRS_FLAME_W + x];
                int out_x = x + shift;
                uint32_t colour;
                uint32_t under;
                int at;
                int inv;

                if( heat <= 0 )
                    continue;
                if( heat > 255 )
                    heat = 255;
                if( out_x < 0 || out_x >= flames->width )
                    continue;

                at = out_y * flames->width + out_x;
                colour = flames->active_palette[heat];
                under = out[at];
                inv = 256 - heat;
                out[at] = 0xFF000000u |
                          ((((((colour >> 16) & 0xFF) * heat + ((under >> 16) & 0xFF) * inv) >> 8)
                            & 0xFF)
                           << 16) |
                          ((((((colour >> 8) & 0xFF) * heat + ((under >> 8) & 0xFF) * inv) >> 8)
                            & 0xFF)
                           << 8) |
                          ((((colour & 0xFF) * heat + (under & 0xFF) * inv) >> 8) & 0xFF);
            }
        }
    }
}

void
TitleFlames_Init(
    struct TitleFlames* flames,
    uint32_t const* const background[TORIRS_FLAME_SIDES],
    int width,
    int height,
    struct ToriRS_Sprite const* runes)
{
    /* The reference's own three gradients: red-to-yellow-to-white, and the
     * green and blue variants it drifts to. A profile that states its own
     * replaces these through TitleFlames_SetPalettes. */
    static int const k_default_stops[TORIRS_FLAME_PALETTES][TORIRS_FLAME_PALETTE_STOPS] = {
        { 0x000000, 0xFF0000, 0xFFFF00, 0xFFFFFF, 0xFFFFFF },
        { 0x000000, 0x00FF00, 0x00FFFF, 0xFFFFFF, 0xFFFFFF },
        { 0x000000, 0x0000FF, 0xFF00FF, 0xFFFFFF, 0xFFFFFF },
    };
    size_t bytes;

    assert(flames);
    assert(background);
    assert(background[0]);
    assert(background[1]);
    assert(width > 0);
    assert(height > 0);

    memset(flames, 0, sizeof(*flames));
    flames->width = width;
    flames->height = height;
    flames->rng = 0x1BADB002u;

    bytes = (size_t)width * height * sizeof(uint32_t);
    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        flames->background[side] = malloc(bytes);
        assert(flames->background[side]);
        memcpy(flames->background[side], background[side], bytes);
        flames->out[side] = malloc(bytes);
        assert(flames->out[side]);
        memcpy(flames->out[side], background[side], bytes);
    }

    if( runes )
    {
        for( int i = 0; i < runes->frame_count && i < TORIRS_FLAME_RUNES; i++ )
        {
            struct ToriRS_SpriteFrame const* frame = &runes->frames[i];
            struct TitleFlameRune* rune = &flames->runes[flames->rune_count];
            size_t cells;

            if( !frame->pixels_argb || frame->width <= 0 || frame->height <= 0 )
                continue;

            cells = (size_t)frame->width * frame->height;
            rune->mask = malloc(cells);
            assert(rune->mask);
            /* Coverage only: the glyph's colour never reaches the screen, its
             * SHAPE does -- as a hole in the cooling map. */
            for( size_t p = 0; p < cells; p++ )
                rune->mask[p] = (frame->pixels_argb[p] & 0x00FFFFFFu) != 0u ? 1u : 0u;
            rune->width = frame->width;
            rune->height = frame->height;
            rune->offset_x = frame->crop_x;
            rune->offset_y = frame->crop_y;
            flames->rune_count++;
        }
    }

    TitleFlames_SetPalettes(flames, k_default_stops);
    generate_cooling_map(
        flames, flames->rune_count > 0 ? &flames->runes[0] : NULL);
    flames->inited = 1;
}

void
TitleFlames_SetGeometry(
    struct TitleFlames* flames,
    enum TitleFlameSide side,
    struct TitleFlameGeometry const* geometry)
{
    assert(flames);
    assert(geometry);
    assert(side >= 0);
    assert(side < TORIRS_FLAME_SIDES);

    flames->geometry[side] = *geometry;
    flames->has_geometry[side] = 1;
}

void
TitleFlames_SetBlur(
    struct TitleFlames* flames,
    int blur_kind)
{
    assert(flames);
    flames->blur_kind = blur_kind;
}

void
TitleFlames_Free(struct TitleFlames* flames)
{
    if( !flames )
        return;
    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        free(flames->background[side]);
        free(flames->out[side]);
    }
    for( int i = 0; i < flames->rune_count; i++ )
        free(flames->runes[i].mask);
    memset(flames, 0, sizeof(*flames));
}

int
TitleFlames_Advance(
    struct TitleFlames* flames,
    int elapsed_ms)
{
    int steps = 0;

    assert(flames);
    if( !flames->inited )
        return 0;
    if( elapsed_ms > 0 )
        flames->accum_ms += elapsed_ms;

    while( flames->accum_ms >= FLAME_STEP_MS && steps < FLAME_MAX_STEPS_PER_ADVANCE )
    {
        flames->accum_ms -= FLAME_STEP_MS;
        for( int i = 0; i < FLAME_UPDATES_PER_STEP; i++ )
            flame_update(flames);
        steps++;
    }
    /* A stall is dropped rather than paid back: catching up would run the fire
     * fast for a second, which reads as a glitch. */
    if( flames->accum_ms > FLAME_STEP_MS * FLAME_MAX_STEPS_PER_ADVANCE )
        flames->accum_ms = 0;

    if( steps == 0 )
        return 0;
    flame_composite(flames);
    return 1;
}

uint32_t const*
TitleFlames_Pixels(
    struct TitleFlames const* flames,
    enum TitleFlameSide side)
{
    assert(flames);
    assert(side >= 0);
    assert(side < TORIRS_FLAME_SIDES);
    return flames->out[side];
}
