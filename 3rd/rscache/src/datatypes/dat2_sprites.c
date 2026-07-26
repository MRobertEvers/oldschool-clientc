#include "dat2_sprites.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
sprite_free_contents(struct RSCache_Dat2Sprite* sprite)
{
    free(sprite->palette_pixels);
    free(sprite->pixel_alphas);
    sprite->palette_pixels = NULL;
    sprite->pixel_alphas = NULL;
}

struct RSCache_Dat2SpritePack*
RSCache_Dat2SpritePackNewDecode(
    const unsigned char* data,
    int length,
    enum RSCache_SpriteLoadFlags decode_flags)
{
    struct RSCache_Dat2SpritePack* pack;
    struct RSCache_Buffer buffer;
    struct RSCache_Dat2Sprite* sprites;
    int* palette;
    int sprite_count;
    int memory_width;
    int memory_height;
    int palette_length;
    int i;

    if( !data || length < 7 )
        return NULL;

    pack = calloc(1, sizeof(*pack));
    if( !pack )
        return NULL;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)length);

    buffer.position = (uint32_t)length - 2;
    sprite_count = g2(&buffer);
    pack->count = sprite_count;

    buffer.position = (uint32_t)(length - 7 - sprite_count * 8);

    memory_width = g2(&buffer);
    memory_height = g2(&buffer);
    palette_length = g1(&buffer) + 1;

    sprites = calloc((size_t)sprite_count, sizeof(*sprites));
    if( !sprites )
    {
        free(pack);
        return NULL;
    }
    pack->sprites = sprites;

    for( i = 0; i < sprite_count; i++ )
    {
        sprites[i].frame = i;
        sprites[i].width = memory_width;
        sprites[i].height = memory_height;
    }

    for( i = 0; i < sprite_count; i++ )
        sprites[i].offset_x = g2(&buffer);
    for( i = 0; i < sprite_count; i++ )
        sprites[i].offset_y = g2(&buffer);
    for( i = 0; i < sprite_count; i++ )
        sprites[i].crop_width = g2(&buffer);
    for( i = 0; i < sprite_count; i++ )
        sprites[i].crop_height = g2(&buffer);

    buffer.position =
        (uint32_t)(length - 7 - sprite_count * 8 - (palette_length - 1) * 3);

    palette = calloc((size_t)palette_length, sizeof(int));
    if( !palette )
    {
        RSCache_Dat2SpritePackFree(pack);
        return NULL;
    }
    pack->palette_length = palette_length;
    pack->palette = palette;

    for( i = 1; i < palette_length; i++ )
    {
        int palette_entry = g3(&buffer);
        if( palette_entry == 0 )
            palette_entry = 1;
        palette[i] = palette_entry;
    }

    buffer.position = 0;

    for( i = 0; i < sprite_count; i++ )
    {
        struct RSCache_Dat2Sprite* sprite = &sprites[i];
        int dimension = sprite->crop_width * sprite->crop_height;
        uint8_t* pixel_idx;
        uint8_t* pixel_alpha;
        int flags;
        int j;
        int k;

        pixel_idx = calloc((size_t)dimension, sizeof(uint8_t));
        pixel_alpha = calloc((size_t)dimension, sizeof(uint8_t));
        if( !pixel_idx || !pixel_alpha )
        {
            free(pixel_idx);
            free(pixel_alpha);
            RSCache_Dat2SpritePackFree(pack);
            return NULL;
        }

        flags = g1(&buffer);
        if( (flags & FLAG_VERTICAL) == 0 )
        {
            for( j = 0; j < dimension; j++ )
                pixel_idx[j] = (uint8_t)g1(&buffer);
        }
        else
        {
            for( j = 0; j < sprite->crop_width; j++ )
            {
                for( k = 0; k < sprite->crop_height; k++ )
                    pixel_idx[sprite->crop_width * k + j] = (uint8_t)g1(&buffer);
            }
        }

        if( (flags & FLAG_ALPHA) != 0 )
        {
            if( (flags & FLAG_VERTICAL) == 0 )
            {
                for( j = 0; j < dimension; j++ )
                    pixel_alpha[j] = (uint8_t)g1(&buffer);
            }
            else
            {
                /* Same crop-sized layout as palette indices — memory width/height
                 * would OOB-write the crop buffer and corrupt the heap. */
                for( j = 0; j < sprite->crop_width; j++ )
                {
                    for( k = 0; k < sprite->crop_height; k++ )
                        pixel_alpha[sprite->crop_width * k + j] = (uint8_t)g1(&buffer);
                }
            }
        }

        /* OSRS 232+: every non-zero palette index is opaque, even when FLAG_ALPHA
         * supplied per-pixel alphas. RuneLite removed the `else` so this always
         * runs after any alpha payload. */
        {
            for( j = 0; j < dimension; j++ )
            {
                int index = pixel_idx[j] & 0xFF;
                if( index != 0 )
                    pixel_alpha[j] = (uint8_t)0xFF;
            }
        }

        sprite->pixel_alphas = pixel_alpha;
        sprite->palette_pixels = pixel_idx;
    }

    if( decode_flags & RSCACHE_SPRITELOAD_FLAG_NORMALIZE )
    {
        for( i = 0; i < sprite_count; i++ )
        {
            struct RSCache_Dat2Sprite* sprite = &sprites[i];
            if( sprite->width != sprite->crop_width || sprite->height != sprite->crop_height )
            {
                int dimension = sprite->width * sprite->height;
                uint8_t* pixel_idx = calloc((size_t)dimension, sizeof(uint8_t));
                uint8_t* pixel_alphas = calloc((size_t)dimension, sizeof(uint8_t));
                int index = 0;
                int y;
                int x;

                if( !pixel_idx || !pixel_alphas )
                {
                    free(pixel_idx);
                    free(pixel_alphas);
                    RSCache_Dat2SpritePackFree(pack);
                    return NULL;
                }

                for( y = 0; y < sprite->crop_height; y++ )
                {
                    for( x = 0; x < sprite->crop_width; x++ )
                    {
                        int y_idx = sprite->width * (y + sprite->offset_y);
                        int x_idx = x + sprite->offset_x;
                        pixel_idx[y_idx + x_idx] = sprite->palette_pixels[index];
                        pixel_alphas[y_idx + x_idx] = sprite->pixel_alphas[index];
                        index++;
                    }
                }
                free(sprite->palette_pixels);
                free(sprite->pixel_alphas);
                sprite->palette_pixels = pixel_idx;
                sprite->pixel_alphas = pixel_alphas;
                sprite->crop_width = sprite->width;
                sprite->crop_height = sprite->height;
                sprite->offset_x = 0;
                sprite->offset_y = 0;
            }
        }
    }

    return pack;
}

uint32_t
RSCache_Dat2SpritePackEncodeBound(const struct RSCache_Dat2SpritePack* pack)
{
    if( !pack )
        return 0;

    uint32_t total = 0;
    for( int i = 0; i < pack->count; i++ )
    {
        const struct RSCache_Dat2Sprite* sprite = &pack->sprites[i];
        uint32_t dimension = (uint32_t)sprite->crop_width * (uint32_t)sprite->crop_height;
        /* flags byte, palette indices, and alphas in the worst case. */
        total += 1u + dimension * 2u;
    }

    /* Palette, then the trailer: memory width/height, palette length, four u16
     * columns per sprite, and the sprite count. */
    total += (uint32_t)(pack->palette_length > 0 ? pack->palette_length - 1 : 0) * 3u;
    total += 5u + (uint32_t)pack->count * 8u + 2u;
    return total;
}

uint32_t
RSCache_Dat2SpritePackEncode(
    const struct RSCache_Dat2SpritePack* pack,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !pack || !out || pack->count <= 0 )
        return 0;
    if( out_capacity < RSCache_Dat2SpritePackEncodeBound(pack) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Pixel data first — the decoder reads it from offset 0 forward, and everything
     * else from the end backwards. */
    for( int i = 0; i < pack->count; i++ )
    {
        const struct RSCache_Dat2Sprite* sprite = &pack->sprites[i];
        int dimension = sprite->crop_width * sprite->crop_height;

        /*
         * The flags byte is not retained by the decoder, so it has to be chosen
         * rather than reproduced.
         *
         * FLAG_VERTICAL is never written: the column-major variant carries the same
         * pixels in a different order, and writing row-major is always valid. A
         * sprite that was stored vertically therefore round-trips semantically but
         * not byte-exactly.
         *
         * FLAG_ALPHA is written only when the alphas cannot be re-derived. With the
         * flag clear the decoder synthesises alpha as (index != 0 ? 0xFF : 0), so
         * whenever the stored alphas already match that rule the channel is pure
         * redundancy and omitting it is both smaller and byte-exact against a source
         * that omitted it too.
         */
        bool alpha_derivable = true;
        for( int j = 0; j < dimension; j++ )
        {
            uint8_t expected = sprite->palette_pixels[j] != 0 ? 0xFF : 0x00;
            if( sprite->pixel_alphas[j] != expected )
            {
                alpha_derivable = false;
                break;
            }
        }

        int flags = alpha_derivable ? 0 : FLAG_ALPHA;
        p1(&buffer, flags);

        for( int j = 0; j < dimension; j++ )
            p1(&buffer, sprite->palette_pixels[j]);

        if( !alpha_derivable )
        {
            for( int j = 0; j < dimension; j++ )
                p1(&buffer, sprite->pixel_alphas[j]);
        }
    }

    /* Palette. Index 0 is implicit and never stored. The decoder rewrites a stored 0
     * to 1, so a palette that legitimately held 0 cannot be reproduced — that
     * information is lost at decode. */
    for( int i = 1; i < pack->palette_length; i++ )
        p3(&buffer, pack->palette[i]);

    /* Trailer. Memory width/height are per-pack but stored on every sprite by the
     * decoder, so take them from the first. */
    p2(&buffer, pack->sprites[0].width);
    p2(&buffer, pack->sprites[0].height);
    p1(&buffer, pack->palette_length - 1);

    for( int i = 0; i < pack->count; i++ )
        p2(&buffer, pack->sprites[i].offset_x);
    for( int i = 0; i < pack->count; i++ )
        p2(&buffer, pack->sprites[i].offset_y);
    for( int i = 0; i < pack->count; i++ )
        p2(&buffer, pack->sprites[i].crop_width);
    for( int i = 0; i < pack->count; i++ )
        p2(&buffer, pack->sprites[i].crop_height);

    p2(&buffer, pack->count);

    return buffer.position;
}

void
RSCache_Dat2SpritePackFree(struct RSCache_Dat2SpritePack* pack)
{
    int i;

    if( !pack )
        return;

    free(pack->palette);
    if( pack->sprites )
    {
        for( i = 0; i < pack->count; i++ )
            sprite_free_contents(&pack->sprites[i]);
        free(pack->sprites);
    }
    free(pack);
}

int*
RSCache_Dat2SpriteGetPixels(
    struct RSCache_Dat2Sprite* sprite,
    int* palette,
    int brightness)
{
    int* pixels;
    int pixel_count;
    int pixel_index;

    assert(sprite);
    assert(palette);
    (void)brightness;

    /* Pixel buffers are always crop-sized (normalize expands crop to memory size). */
    pixel_count = sprite->crop_width * sprite->crop_height;
    if( pixel_count <= 0 )
        return NULL;

    pixels = calloc((size_t)pixel_count, sizeof(int));
    if( !pixels )
        return NULL;

    for( pixel_index = 0; pixel_index < pixel_count; pixel_index++ )
    {
        int palette_index = sprite->palette_pixels[pixel_index] & 0xFF;
        uint8_t alpha;

        /* Palette index 0 is always transparent. */
        if( palette_index == 0 )
            continue;

        alpha = sprite->pixel_alphas[pixel_index];
        if( alpha == 0 )
            continue;

        pixels[pixel_index] = ((int)alpha << 24) | palette[palette_index];
    }

    return pixels;
}
