#include "engine/png_decode.h"

#include "miniz.h"

#include <stdlib.h>
#include <string.h>

/*
 * Minimal PNG reader, for the world map's ground images (cache table 20): the
 * only PNGs in any cache this client reads, all 64x64 8-bit truecolour.
 *
 * Deliberately narrow — bit depth 8, colour type 2 or 6, no interlace, no
 * palette, no 16-bit. Anything else is refused rather than half-read. miniz is
 * already linked for the cache's gzip/zlib containers, so IDAT costs no new
 * dependency; what is left is the chunk walk and the five row filters.
 */

static uint32_t
png_read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static int
png_paeth(int left, int up, int up_left)
{
    int estimate = left + up - up_left;
    int distance_left = abs(estimate - left);
    int distance_up = abs(estimate - up);
    int distance_up_left = abs(estimate - up_left);

    if( distance_left <= distance_up && distance_left <= distance_up_left )
        return left;
    if( distance_up <= distance_up_left )
        return up;
    return up_left;
}

/* Undo the per-row filter in place. `raw` is the inflated stream: one filter
 * byte then `stride` bytes, per row. */
static bool
png_unfilter(
    uint8_t* raw,
    int width,
    int height,
    int bytes_per_pixel)
{
    int stride = width * bytes_per_pixel;

    for( int row = 0; row < height; row++ )
    {
        uint8_t* line = raw + (size_t)row * (stride + 1);
        int filter = line[0];
        uint8_t* pixels = line + 1;
        uint8_t* previous = row > 0 ? raw + (size_t)(row - 1) * (stride + 1) + 1 : NULL;

        for( int i = 0; i < stride; i++ )
        {
            int left = i >= bytes_per_pixel ? pixels[i - bytes_per_pixel] : 0;
            int up = previous ? previous[i] : 0;
            int up_left = (previous && i >= bytes_per_pixel) ? previous[i - bytes_per_pixel] : 0;

            switch( filter )
            {
            case 0:
                break;
            case 1:
                pixels[i] = (uint8_t)(pixels[i] + left);
                break;
            case 2:
                pixels[i] = (uint8_t)(pixels[i] + up);
                break;
            case 3:
                pixels[i] = (uint8_t)(pixels[i] + ((left + up) >> 1));
                break;
            case 4:
                pixels[i] = (uint8_t)(pixels[i] + png_paeth(left, up, up_left));
                break;
            default:
                return false;
            }
        }
    }
    return true;
}

bool
PngDecode_Rgb(
    const void* data,
    int data_size,
    int* out_width,
    int* out_height,
    uint32_t** out_pixels)
{
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    const uint8_t* bytes = (const uint8_t*)data;
    uint8_t* idat = NULL;
    size_t idat_size = 0;
    uint8_t* raw = NULL;
    uint32_t* pixels = NULL;
    int width = 0;
    int height = 0;
    int bytes_per_pixel = 0;
    int offset = 8;
    bool ok = false;

    if( !data || data_size < 8 || memcmp(bytes, signature, sizeof(signature)) != 0 )
        return false;

    while( offset + 8 <= data_size )
    {
        uint32_t length = png_read_u32(bytes + offset);
        const uint8_t* type = bytes + offset + 4;
        const uint8_t* payload = bytes + offset + 8;

        if( (uint64_t)offset + 12 + length > (uint64_t)data_size )
            break;

        if( memcmp(type, "IHDR", 4) == 0 && length >= 13 )
        {
            width = (int)png_read_u32(payload);
            height = (int)png_read_u32(payload + 4);
            if( payload[8] != 8 || payload[12] != 0 )
                goto done; /* bit depth / interlace outside the narrow support */
            if( payload[9] == 2 )
                bytes_per_pixel = 3;
            else if( payload[9] == 6 )
                bytes_per_pixel = 4;
            else
                goto done;
            if( width <= 0 || height <= 0 || width > 4096 || height > 4096 )
                goto done;
        }
        else if( memcmp(type, "IDAT", 4) == 0 )
        {
            uint8_t* grown = (uint8_t*)realloc(idat, idat_size + length);
            if( !grown )
                goto done;
            idat = grown;
            memcpy(idat + idat_size, payload, length);
            idat_size += length;
        }
        else if( memcmp(type, "IEND", 4) == 0 )
        {
            break;
        }

        offset += 12 + (int)length;
    }

    if( !idat || bytes_per_pixel == 0 )
        goto done;

    {
        mz_ulong raw_size = (mz_ulong)((size_t)height * ((size_t)width * bytes_per_pixel + 1));
        raw = (uint8_t*)malloc(raw_size);
        if( !raw )
            goto done;
        if( mz_uncompress(raw, &raw_size, idat, (mz_ulong)idat_size) != MZ_OK )
            goto done;
        if( raw_size != (mz_ulong)((size_t)height * ((size_t)width * bytes_per_pixel + 1)) )
            goto done;
    }

    if( !png_unfilter(raw, width, height, bytes_per_pixel) )
        goto done;

    pixels = (uint32_t*)malloc((size_t)width * height * sizeof(*pixels));
    if( !pixels )
        goto done;

    for( int row = 0; row < height; row++ )
    {
        const uint8_t* line = raw + (size_t)row * ((size_t)width * bytes_per_pixel + 1) + 1;
        for( int col = 0; col < width; col++ )
        {
            const uint8_t* pixel = line + (size_t)col * bytes_per_pixel;
            pixels[(size_t)row * width + col] =
                ((uint32_t)pixel[0] << 16) | ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[2];
        }
    }

    *out_width = width;
    *out_height = height;
    *out_pixels = pixels;
    pixels = NULL;
    ok = true;

done:
    free(pixels);
    free(raw);
    free(idat);
    return ok;
}
