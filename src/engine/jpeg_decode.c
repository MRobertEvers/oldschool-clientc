#include "engine/jpeg_decode.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Declarations only. The implementation is 3rd/stb/stb_image_impl.c, compiled
 * on its own with -w; see the note there. */
#define STBI_NO_STDIO
#include "stb_image.h"

/* Shared tail: stb hands back tightly packed RGB triplets; the client wants
 * 0xFFRRGGBB words. */
static bool
jpeg_decode_rgb_to_argb(
    const unsigned char* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels)
{
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    unsigned char* rgb;
    uint32_t* pixels;

    assert(data);
    assert(out_width);
    assert(out_height);
    assert(out_pixels);

    /* Forced to 3 channels, so greyscale and YCbCr arrive in the same shape. */
    rgb = stbi_load_from_memory(data, data_size, &width, &height, &channels_in_file, 3);
    if( !rgb )
        return false;
    if( width <= 0 || height <= 0 )
    {
        stbi_image_free(rgb);
        return false;
    }

    pixels = malloc((size_t)width * (size_t)height * sizeof(*pixels));
    assert(pixels);

    for( size_t i = 0, n = (size_t)width * (size_t)height; i < n; i++ )
    {
        pixels[i] = 0xFF000000u | ((uint32_t)rgb[i * 3 + 0] << 16) |
                    ((uint32_t)rgb[i * 3 + 1] << 8) | (uint32_t)rgb[i * 3 + 2];
    }

    stbi_image_free(rgb);

    *out_width = width;
    *out_height = height;
    *out_pixels = pixels;
    return true;
}

bool
JpegDecode_Argb(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels)
{
    assert(data);
    assert(out_width);
    assert(out_height);
    assert(out_pixels);
    if( data_size <= 0 )
        return false;

    return jpeg_decode_rgb_to_argb(
        (const unsigned char*)data, data_size, out_width, out_height, out_pixels);
}

bool
JpegDecode_ArgbRsCache(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels)
{
    unsigned char* patched;
    bool ok;

    assert(data);
    assert(out_width);
    assert(out_height);
    assert(out_pixels);
    if( data_size <= 0 )
        return false;

    /* A copy, so the jagfile member the caller still holds is not rewritten
     * under it -- these bytes are cached and decoded more than once. */
    patched = malloc((size_t)data_size);
    assert(patched);
    memcpy(patched, data, (size_t)data_size);

    /* The whole repair: restore the SOI marker's first byte. A no-op on a
     * cache whose title image is well-formed, which lc254's is. */
    patched[0] = 0xFF;

    ok = jpeg_decode_rgb_to_argb(patched, data_size, out_width, out_height, out_pixels);
    free(patched);
    return ok;
}
