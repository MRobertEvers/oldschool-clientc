#ifndef TORIRS_PNG_DECODE_H
#define TORIRS_PNG_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Decode a PNG into 0x00RRGGBB pixels (alpha dropped; the images this reads are
 * opaque). Supports exactly what the cache ships: 8-bit, colour type 2 or 6, no
 * interlace. Returns false and touches no output otherwise.
 *
 * The caller owns `*out_pixels` (free()).
 */
bool
PngDecode_Rgb(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels);

/**
 * The same decode, into 0xAARRGGBB, KEEPING the alpha channel: 0xFF for a
 * colour-type-2 image, the file's own byte for colour type 6.
 *
 * A second entry point rather than an argument on the first, because the two
 * answer different questions and every existing caller means the first one.
 * The world map's ground images are opaque photographs of terrain and are
 * blitted as such; a plugin's art is a CUT-OUT -- an orb is a circle in a
 * square file, and dropping its alpha fills the corners with whatever colour
 * the authoring tool left in the transparent pixels (black, usually, so the
 * orb arrives as a black square with a disc drawn on it).
 *
 * The caller owns `*out_pixels` (free()).
 */
bool
PngDecode_Argb(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels);

#endif /* TORIRS_PNG_DECODE_H */
