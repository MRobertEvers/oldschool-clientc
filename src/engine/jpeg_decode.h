#ifndef TORIRS_JPEG_DECODE_H
#define TORIRS_JPEG_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Decode a JPEG into 0xFFRRGGBB pixels.
 *
 * JPEG carries no alpha, so every pixel comes back fully opaque -- written as
 * 0xFF rather than 0x00 so a decoded background takes the sprite blit's
 * all-opaque path instead of being composited away.
 *
 * Baseline and progressive, greyscale and YCbCr. Returns false and touches no
 * output on anything else, on a truncated file, or on an allocation the
 * decoder itself refused.
 *
 * The caller owns `*out_pixels` (free()).
 */
bool
JpegDecode_Argb(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels);

/**
 * The same decode for a JPEG that came out of an RS cache.
 *
 * Some cached title images do not begin with a valid SOI -- the first byte is
 * not 0xFF -- and the reference clients repair it before decoding rather than
 * treat the image as broken (Client-TS decodeJpeg: "fix invalid JPEG header").
 * lc254's own title.dat is well-formed, so this is insurance against the caches
 * that are not, and the cost of being wrong about which is a blank title screen.
 *
 * The repair is the cache's quirk and not the decoder's, so it lives in its own
 * entry point: a caller reading a well-formed JPEG from anywhere else should
 * not silently get its header rewritten.
 *
 * `data` is not modified; the patch is applied to an internal copy.
 *
 * The caller owns `*out_pixels` (free()).
 */
bool
JpegDecode_ArgbRsCache(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels);

#endif /* TORIRS_JPEG_DECODE_H */
