#include "sprites.h"

#include "../rsbuf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheSpriteDefinition*
sprite_definition_new_from_cache(struct Cache* cache, int id)
{
    return NULL;
}

// InputStream is = new InputStream(b);

// 		is.setOffset(is.getLength() - 2);
// 		int spriteCount = is.readUnsignedShort();

// 		SpriteDefinition[] sprites = new SpriteDefinition[spriteCount];

// 		// 2 for size
// 		// 5 for width, height, palette length
// 		// + 8 bytes per sprite for offset x/y, width, and height
// 		is.setOffset(is.getLength() - 7 - spriteCount * 8);

// 		// max width and height
// 		int width = is.readUnsignedShort();
// 		int height = is.readUnsignedShort();
// 		int paletteLength = is.readUnsignedByte() + 1;

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			sprites[i] = new SpriteDefinition();
// 			sprites[i].setId(id);
// 			sprites[i].setFrame(i);
// 			sprites[i].setMaxWidth(width);
// 			sprites[i].setMaxHeight(height);
// 		}

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			sprites[i].setOffsetX(is.readUnsignedShort());
// 		}

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			sprites[i].setOffsetY(is.readUnsignedShort());
// 		}

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			sprites[i].setWidth(is.readUnsignedShort());
// 		}

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			sprites[i].setHeight(is.readUnsignedShort());
// 		}

// 		// same as above + 3 bytes for each palette entry, except for the first one (which is
// transparent) 		is.setOffset(is.getLength() - 7 - spriteCount * 8 - (paletteLength - 1) *
// 3); 		int[] palette = new int[paletteLength];

// 		for (int i = 1; i < paletteLength; ++i)
// 		{
// 			palette[i] = is.read24BitInt();

// 			if (palette[i] == 0)
// 			{
// 				palette[i] = 1;
// 			}
// 		}

// 		is.setOffset(0);

// 		for (int i = 0; i < spriteCount; ++i)
// 		{
// 			SpriteDefinition def = sprites[i];
// 			int spriteWidth = def.getWidth();
// 			int spriteHeight = def.getHeight();
// 			int dimension = spriteWidth * spriteHeight;
// 			byte[] pixelPaletteIndicies = new byte[dimension];
// 			byte[] pixelAlphas = new byte[dimension];
// 			def.pixelIdx = pixelPaletteIndicies;
// 			def.palette = palette;

// 			int flags = is.readUnsignedByte();

// 			if ((flags & FLAG_VERTICAL) == 0)
// 			{
// 				// read horizontally
// 				for (int j = 0; j < dimension; ++j)
// 				{
// 					pixelPaletteIndicies[j] = is.readByte();
// 				}
// 			}
// 			else
// 			{
// 				// read vertically
// 				for (int j = 0; j < spriteWidth; ++j)
// 				{
// 					for (int k = 0; k < spriteHeight; ++k)
// 					{
// 						pixelPaletteIndicies[spriteWidth * k + j] = is.readByte();
// 					}
// 				}
// 			}

// 			// read alphas
// 			if ((flags & FLAG_ALPHA) != 0)
// 			{
// 				if ((flags & FLAG_VERTICAL) == 0)
// 				{
// 					// read horizontally
// 					for (int j = 0; j < dimension; ++j)
// 					{
// 						pixelAlphas[j] = is.readByte();
// 					}
// 				}
// 				else
// 				{
// 					// read vertically
// 					for (int j = 0; j < spriteWidth; ++j)
// 					{
// 						for (int k = 0; k < spriteHeight; ++k)
// 						{
// 							pixelAlphas[spriteWidth * k + j] = is.readByte();
// 						}
// 					}
// 				}
// 			}
// 			else
// 			{
// 				// everything non-zero is opaque
// 				for (int j = 0; j < dimension; ++j)
// 				{
// 					int index = pixelPaletteIndicies[j];

// 					if (index != 0)
// 						pixelAlphas[j] = (byte) 0xFF;
// 				}
// 			}

// 			int[] pixels = new int[dimension];

// 			// build argb pixels from palette/alphas
// 			for (int j = 0; j < dimension; ++j)
// 			{
// 				int index = pixelPaletteIndicies[j] & 0xFF;

// 				pixels[j] = palette[index] | (pixelAlphas[j] << 24);
// 			}

// 			def.setPixels(pixels);
// 		}

// 		return sprites;

struct CacheSpritePack*
sprite_pack_new_from_cache(struct Cache* cache, int id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_SPRITES, id);
    if( !archive )
        return NULL;

    struct CacheSpritePack* pack =
        sprite_pack_new_decode(archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
    cache_archive_free(archive);

    return pack;
}

struct CacheSpritePack*
sprite_pack_new_decode(const unsigned char* data, int length, enum SpiteLoaderFlags decode_flags)
{
    if( !data || length < 7 )
        return NULL;

    struct CacheSpritePack* pack = (struct CacheSpritePack*)malloc(sizeof(struct CacheSpritePack));
    if( !pack )
        return NULL;

    memset(pack, 0, sizeof(struct CacheSpritePack));

    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, length);

    buffer.position = length - 2;
    // Read sprite count from end of data
    int sprite_count = rsbuf_g2(&buffer);
    pack->count = sprite_count;

    buffer.position = length - 7 - sprite_count * 8;

    int memory_width = rsbuf_g2(&buffer);
    int memory_height = rsbuf_g2(&buffer);
    int palette_length = rsbuf_g1(&buffer) + 1;

    struct CacheSprite* sprites =
        (struct CacheSprite*)malloc(sprite_count * sizeof(struct CacheSprite));
    if( !sprites )
        return NULL;
    pack->sprites = sprites;

    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        memset(sprite, 0, sizeof(struct CacheSprite));
        sprite->frame = i;
        sprite->width = memory_width;
        sprite->height = memory_height;
    }

    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        int offset_x = rsbuf_g2(&buffer);
        sprite->offset_x = offset_x;
    }
    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        int offset_y = rsbuf_g2(&buffer);
        sprite->offset_y = offset_y;
    }

    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        int width = rsbuf_g2(&buffer);
        sprite->crop_width = width;
    }

    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        int height = rsbuf_g2(&buffer);
        sprite->crop_height = height;
    }

    // same as above + 3 bytes for each palette entry, except for the first one (which is
    // transparent) is.setOffset(is.getLength() - 7 - spriteCount * 8 - (paletteLength - 1) * 3);
    buffer.position = length - 7 - sprite_count * 8 - (palette_length - 1) * 3;

    uint32_t* palette = (uint32_t*)malloc(palette_length * sizeof(uint32_t));
    if( !palette )
        return NULL;
    memset(palette, 0, palette_length * sizeof(uint32_t));
    pack->palette_length = palette_length;

    for( int i = 1; i < palette_length; i++ )
    {
        int palette_entry = rsbuf_g3(&buffer);
        if( palette_entry == 0 )
            palette_entry = 1;
        palette[i] = palette_entry;
    }

    pack->palette = palette;

    buffer.position = 0;

    for( int i = 0; i < sprite_count; i++ )
    {
        struct CacheSprite* sprite = &sprites[i];
        // for sprites that are not normal size (128x128),
        // rs-map-viewer calls IndexedSprite.normalize,
        // which fills the sprite out to 128x128
        int dimension = sprite->crop_width * sprite->crop_height;
        uint8_t* pixel_idx = (uint8_t*)malloc(dimension * sizeof(uint8_t));
        if( !pixel_idx )
            return NULL;
        memset(pixel_idx, 0, dimension * sizeof(uint8_t));

        uint8_t* pixel_alpha = (uint8_t*)malloc(dimension * sizeof(uint8_t));
        if( !pixel_alpha )
            return NULL;
        memset(pixel_alpha, 0, dimension * sizeof(uint8_t));

        int flags = rsbuf_g1(&buffer);
        // rs-map-viewer calls this readPixelsDimension
        //        const readPixelsDimension = buffer.readUnsignedByte();
        if( (flags & FLAG_VERTICAL) == 0 )
        {
            for( int j = 0; j < dimension; j++ )
            {
                pixel_idx[j] = rsbuf_g1(&buffer);
            }
        }
        else
        {
            for( int j = 0; j < sprite->crop_width; j++ )
            {
                for( int k = 0; k < sprite->crop_height; k++ )
                {
                    pixel_idx[sprite->crop_width * k + j] = rsbuf_g1(&buffer);
                }
            }
        }

        if( (flags & FLAG_ALPHA) != 0 )
        {
            if( (flags & FLAG_VERTICAL) == 0 )
            {
                for( int j = 0; j < dimension; j++ )
                {
                    pixel_alpha[j] = rsbuf_g1(&buffer);
                }
            }
            else
            {
                for( int j = 0; j < sprite->width; j++ )
                {
                    for( int k = 0; k < sprite->height; k++ )
                    {
                        pixel_alpha[sprite->width * k + j] = rsbuf_g1(&buffer);
                    }
                }
            }
        }
        else
        {
            for( int j = 0; j < dimension; j++ )
            {
                int index = pixel_idx[j] & 0xFF;
                pixel_alpha[j] = (uint8_t)(index != 0 ? 0xFF : 0);
            }
        }

        sprite->pixel_alphas = pixel_alpha;
        sprite->palette_pixels = pixel_idx;

        // int* pixels = (int*)malloc(dimension * sizeof(int));
        // if( !pixels )
        //     return NULL;
        // memset(pixels, 0, dimension * sizeof(int));

        // for( int j = 0; j < dimension; j++ )
        // {
        //     int index = pixel_idx[j] & 0xFF;
        //     int pixel = palette[index] | (pixel_alpha[j] << 24);
        //     pixels[j] = pixel;
        // }

        // sprite->pixels = pixels;
    }

    // See rs-map-viewer IndexedSprite.normalize
    if( decode_flags & SPRITELOAD_FLAG_NORMALIZE )
    {
        for( int i = 0; i < sprite_count; i++ )
        {
            struct CacheSprite* sprite = &sprites[i];
            if( sprite->width != sprite->crop_width || sprite->height != sprite->crop_height )
            {
                int dimension = sprite->width * sprite->height;
                uint8_t* pixel_idx = (uint8_t*)malloc(dimension * sizeof(uint8_t));
                if( !pixel_idx )
                    return NULL;
                uint8_t* pixel_alphas = (uint8_t*)malloc(dimension * sizeof(uint8_t));
                if( !pixel_alphas )
                    return NULL;
                memset(pixel_idx, 0, dimension * sizeof(uint8_t));
                memset(pixel_alphas, 0, dimension * sizeof(uint8_t));

                int index = 0;
                for( int y = 0; y < sprite->crop_height; y++ )
                {
                    for( int x = 0; x < sprite->crop_width; x++ )
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

static void
sprite_free(struct CacheSprite* sprite)
{
    free(sprite->palette_pixels);
    free(sprite->pixel_alphas);
}

void
sprite_pack_free(struct CacheSpritePack* pack)
{
    if( !pack )
    {
        return;
    }

    free(pack->palette);
    for( int i = 0; i < pack->count; i++ )
    {
        struct CacheSprite* sprite = &pack->sprites[i];
        sprite_free(sprite);
    }
    free(pack->sprites);
    free(pack);
}

// export function brightenRgb(rgb: number, brightness: number) {
//     let r = (rgb >> 16) / 256.0;
//     let g = ((rgb >> 8) & 255) / 256.0;
//     let b = (rgb & 255) / 256.0;
//     r = Math.pow(r, brightness);
//     g = Math.pow(g, brightness);
//     b = Math.pow(b, brightness);
//     const newR = (r * 256.0) | 0;
//     const newG = (g * 256.0) | 0;
//     const newB = (b * 256.0) | 0;
//     return (newR << 16) | (newG << 8) | newB;
// }

static int
brighten_rgb(int rgb, double brightness)
{
    double r = (rgb >> 16) / 256.0;
    double g = ((rgb >> 8) & 255) / 256.0;
    double b = (rgb & 255) / 256.0;
    r = pow(r, brightness);
    g = pow(g, brightness);
    b = pow(b, brightness);
    int new_r = (int)(r * 256.0);
    int new_g = (int)(g * 256.0);
    int new_b = (int)(b * 256.0);
    return (new_r << 16) | (new_g << 8) | new_b;
}

int*
sprite_get_pixels(struct CacheSprite* sprite, int* palette, int brightness)
{
    int* pixels = (int*)malloc(sprite->width * sprite->height * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, sprite->width * sprite->height * sizeof(int));

    uint8_t* palette_pixels = sprite->palette_pixels;

    // for( int pi = 0; pi < sprite->width * sprite->height; pi++ )
    // {
    //     int alpha = 0xff;
    //     if( palette[pi] == 0 )
    //         alpha = 0;
    //     palette[pi] = (alpha << 24) | brighten_rgb(palette[pi], brightness);
    // }

    for( int pixel_index = 0; pixel_index < sprite->width * sprite->height; pixel_index++ )
    {
        int palette_index = palette_pixels[pixel_index];
        uint8_t alpha = sprite->pixel_alphas[pixel_index];
        pixels[pixel_index] = (alpha << 24) | palette[palette_index];
    }

    return pixels;
}
