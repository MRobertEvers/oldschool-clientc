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

#endif /* TORIRS_PNG_DECODE_H */
