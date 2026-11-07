#ifndef SRC_EDITOR_EDITOR_FLOORCOLOR_H
#define SRC_EDITOR_EDITOR_FLOORCOLOR_H

/**
 * The floor colours as the client shows them, headless.
 *
 * An underlay record is one 0xRRGGBB (configs/all.underlay `colour=`), but
 * the ground is never that colour: the world builder averages the HSL24 of
 * every underlay within five tiles (engine/world_builder/blendmap.c, an
 * 11x11 window with the hue as a chroma/luminance ratio), packs the average
 * to HSL16 (3rd/toridraw/osrs/palette.c, 64 hues, 8 saturations, 128
 * lightnesses) and draws through the palette table (3rd/toridraw/graphics/
 * shared_tables.c pix3d_init_palette, brightness 0.8). Two underlays blend
 * well when that average walks a short path between them; this module says
 * what the path looks like, so the Palette panel's picker, blend strip and
 * swatch readouts can show it without a scene.
 *
 * The maths is the client's, copied rather than linked so the editor's
 * headless tests need no palette table; editor_panels_test checks it
 * against the originals.
 */

#include <stdint.h>

/** The averaging space of blendmap.c: hue, sat and light 0..255; luminance
 *  (the hue's weight) and chroma (hue * luminance). */
struct Editor_FloorHsl
{
    int hue;
    int sat;
    int light;
    int luminance;
    int chroma;
};

/** The client's HSL24 of a record colour (palette_rgb_to_hsl24). */
void
Editor_FloorHslOfRgb(
    uint32_t rgb,
    struct Editor_FloorHsl* out);

/** HSL24 packed as the client packs it (palette_hsl24_to_hsl16): the
 *  saturation halves per lightness band past 179, then hue/4, sat/32,
 *  light/2. */
int
Editor_FloorHsl16(
    int hue,
    int sat,
    int light);

/** What the ground shows for an HSL16: the palette table's colour
 *  (pix3d_init_palette with the client's 0.8 brightness), 0xRRGGBB. */
uint32_t
Editor_FloorRgbOfHsl16(int hsl16);

/** A record colour as the ground shows it on its own, surrounded by itself:
 *  through HSL24, HSL16 and the palette. */
uint32_t
Editor_FloorShown(uint32_t rgb);

/** An unquantised 0xRRGGBB from HSL24 components (the picker's sliders),
 *  the inverse of Editor_FloorHslOfRgb within rounding. */
uint32_t
Editor_FloorRgbOfHsl(
    int hue,
    int sat,
    int light);

/** How far apart two record colours are to the ground: lightness counts
 *  double, hue counts with the lesser saturation (two greys of any hue are
 *  alike). 0 for the same colour; a step of one HSL16 lightness is about 4. */
int
Editor_FloorDistance(
    uint32_t a,
    uint32_t b);

/** The window radius of the client's blend, in tiles. */
#define EDITOR_FLOOR_BLEND_RADIUS 5

/** The ground across a straight seam between a plane of `from` (the left
 *  half of the strip) and a plane of `to` (the right half): `out[i]` for
 *  i in 0..count-1 is the colour the client draws on the tile i - count/2
 *  tiles from the seam, 0xRRGGBB. Beyond EDITOR_FLOOR_BLEND_RADIUS tiles
 *  from the seam a tile is the plane's own colour (Editor_FloorShown). */
void
Editor_FloorBlendStrip(
    uint32_t from,
    uint32_t to,
    uint32_t* out,
    int count);

#endif
