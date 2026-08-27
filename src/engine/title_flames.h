#ifndef TITLE_FLAMES_H
#define TITLE_FLAMES_H

/*
 * The title screen's fire — the two burning panels either side of the login box.
 *
 * This is a port of the effect the client has always had, and it is deliberately
 * standalone: the only includes are <stdint.h> here and <assert.h>, <stdlib.h>,
 * <string.h> in the .c. No project header, no renderer, no sprite type, no cache
 * handle — and no <math.h> either, so it adds no -lm to any link that takes it.
 * You hand it the five-stop colour ramps and a pixel buffer you own; it hands
 * back composited pixels.
 *
 * ## Where the ramps come from
 *
 * The three `int[5]` ramps are the graphic-defaults record's opcode 3 —
 * OldSchool cache index 17, group 3. The 317 client built these same three
 * gradients in its own source (black through red/yellow, green/cyan and
 * blue/magenta, each ending white); OldSchool ships byte for byte those values
 * as content instead. See docs/CACHE_INDEX_16_17.md.
 *
 * Passing the 317 constants and passing the osrs239 record produce identical
 * fire, because they are the same numbers. `TitleFlames_ClassicRamps` below is
 * those constants, so a caller with no cache can still light the thing.
 *
 * ## What the ramps are not
 *
 * They are not the palette. Each five-stop ramp is expanded into a 256-entry
 * palette at construction — four segments of 64, each step a per-channel linear
 * blend — and 256 is not arbitrary: it is the range of the fire simulation's own
 * output. Every pixel of the heat buffer is an intensity 0..255 and drawing it
 * is one lookup. The three palettes are cross-faded into a working palette as
 * the fire burns, which is what makes it occasionally turn green or blue.
 *
 * ## Using it
 *
 *     struct TitleFlames* fire = TitleFlames_New(TitleFlames_ClassicRamps, 12345);
 *
 *     // once per drawn frame
 *     TitleFlames_Frame(fire);
 *     memcpy(left,  left_background,  sizeof(left));
 *     memcpy(right, right_background, sizeof(right));
 *     TitleFlames_RenderLeft(fire, left);
 *     TitleFlames_RenderRight(fire, right);
 *
 * The render calls *composite over* what is already in the buffer, so the
 * background copy is the caller's job and is not smuggled in here — that is the
 * one place a dependency on an image type would otherwise creep in.
 *
 * Nothing in here is thread safe, and nothing in here is global: two instances
 * burn independently with independent RNG streams.
 */

#include <stdint.h>

/** Ramps are 3 rows of 5 stops, each stop 0x00RRGGBB. */
#define TITLE_FLAMES_RAMP_ROWS 3
#define TITLE_FLAMES_RAMP_STOPS 5

/** The simulation grid. 128x256 is the client's, and the strides below assume it. */
#define TITLE_FLAMES_WIDTH 128
#define TITLE_FLAMES_HEIGHT 256

/** The destination panel. 128x265, i.e. eight rows taller than the fire. */
#define TITLE_FLAMES_PANEL_WIDTH 128
#define TITLE_FLAMES_PANEL_HEIGHT 265
/** Pixels in a panel buffer — what RenderLeft/RenderRight expect. */
#define TITLE_FLAMES_PANEL_PIXELS (TITLE_FLAMES_PANEL_WIDTH * TITLE_FLAMES_PANEL_HEIGHT)

/**
 * The 317 client's three gradients, as ramps.
 *
 * Identical to what osrs239 stores in index 17 group 3. Use these when there is
 * no cache to read, or as the thing to diff a decoded record against.
 */
extern const int TitleFlames_ClassicRamps[TITLE_FLAMES_RAMP_ROWS][TITLE_FLAMES_RAMP_STOPS];

/**
 * One rune silhouette, punched into the cooling map so the fire draws its shape.
 *
 * `pixels` is one byte per pixel and only zero/non-zero is read — it is a mask,
 * not an image, so an indexed sprite's palette indices work unchanged. The
 * caller keeps ownership and must outlive the TitleFlames using it.
 *
 * `origin_x`/`origin_y` are where the mask's top-left lands on the 128x256 grid.
 * The reference client passes the sprite's own offsets plus 16.
 */
struct TitleFlames_Mask
{
    const uint8_t* pixels;
    int width;
    int height;
    int origin_x;
    int origin_y;
};

struct TitleFlames;

/**
 * Build the fire. `ramps` is copied; nothing is retained.
 *
 * `seed` seeds this instance's own generator — the effect is entirely
 * random-driven, and taking the seed rather than reaching for rand() is what
 * lets two instances differ, a test pin a frame, and a replay reproduce one.
 * Any value works; 0 is seeded away from the xorshift fixed point internally.
 */
struct TitleFlames*
TitleFlames_New(
    const int ramps[TITLE_FLAMES_RAMP_ROWS][TITLE_FLAMES_RAMP_STOPS],
    uint32_t seed);

void
TitleFlames_Free(struct TitleFlames* flames);

/**
 * Register the rune masks the cooling map carves itself from, or none.
 *
 * The fire picks one at random every time it regenerates its cooling map. With
 * no masks registered it still burns — it just burns plain, which is what the
 * client looks like before the title archive has loaded.
 *
 * `masks` and every `pixels` they point at stay owned by the caller.
 */
void
TitleFlames_SetMasks(
    struct TitleFlames* flames,
    const struct TitleFlames_Mask* masks,
    int mask_count);

/**
 * Advance one drawn frame.
 *
 * This is two simulation ticks, not one. The client's fire runs on a ~40ms
 * timer and steps twice per draw to compensate; that ratio is part of how the
 * flames look, so it lives here rather than in every caller.
 */
void
TitleFlames_Frame(struct TitleFlames* flames);

/**
 * Composite the left and right panels over `panel`.
 *
 * `panel` is TITLE_FLAMES_PANEL_PIXELS of 0xAARRGGBB and must already hold the
 * background. Each destination pixel keeps its own alpha byte; only RGB is
 * blended, weighted by the flame's intensity at that point.
 *
 * The two differ by more than a mirror: they take opposite sides of the same
 * heat buffer and shear opposite ways, which is why one call cannot serve both.
 */
void
TitleFlames_RenderLeft(const struct TitleFlames* flames, uint32_t* panel);

void
TitleFlames_RenderRight(const struct TitleFlames* flames, uint32_t* panel);

/**
 * The working palette — 256 entries of 0x00RRGGBB, indexed by flame intensity.
 *
 * Exposed because it is the interesting output: it is what the ramps became,
 * and it is what a test can compare against a reference expansion without
 * rendering anything.
 */
const uint32_t*
TitleFlames_Palette(const struct TitleFlames* flames);

#endif
