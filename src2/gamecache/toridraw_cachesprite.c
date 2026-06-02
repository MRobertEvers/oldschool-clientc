#include "toridraw_cachesprite.h"

#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"

#include <stdlib.h>
#include <string.h>

static struct ToriDraw_Pix8*
toridraw_pix8_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    if( !pix8_palette )
        return NULL;
    struct ToriDraw_Pix8* pix8 = malloc(sizeof(struct ToriDraw_Pix8));
    if( !pix8 )
        return NULL;
    memset(pix8, 0, sizeof(struct ToriDraw_Pix8));
    uint8_t* pixels = malloc((size_t)pix8_palette->width * (size_t)pix8_palette->height);
    if( !pixels )
    {
        free(pix8);
        return NULL;
    }
    memcpy(
        pixels,
        pix8_palette->pixels,
        (size_t)pix8_palette->width * (size_t)pix8_palette->height);

    pix8->pixels = pixels;
    pix8->width = pix8_palette->width;
    pix8->height = pix8_palette->height;
    return pix8;
}

static struct ToriDraw_PixPalette*
toridraw_pixpalette_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    if( !pix8_palette )
        return NULL;
    struct ToriDraw_PixPalette* palette = malloc(sizeof(struct ToriDraw_PixPalette));
    if( !palette )
        return NULL;
    memset(palette, 0, sizeof(struct ToriDraw_PixPalette));
    void* pal = malloc((size_t)pix8_palette->palette_count * sizeof(int));
    if( !pal )
    {
        free(palette);
        return NULL;
    }
    memcpy(pal, pix8_palette->palette, (size_t)pix8_palette->palette_count * sizeof(int));

    palette->palette = (int*)pal;
    palette->palette_count = pix8_palette->palette_count;
    return palette;
}

struct ToriDraw_Sprite*
toridraw_sprite_new_from_cache_pix32(struct CacheDatPix32* pix32)
{
    if( !pix32 )
        return NULL;
    struct ToriDraw_Pix32 td_pix32 = { 0 };
    td_pix32.pixels = pix32->pixels;
    td_pix32.draw_width = pix32->draw_width;
    td_pix32.draw_height = pix32->draw_height;
    td_pix32.crop_x = pix32->crop_x;
    td_pix32.crop_y = pix32->crop_y;
    td_pix32.stride_x = pix32->stride_x;
    td_pix32.stride_y = pix32->stride_y;
    return toridraw_sprite_new_from_pix32(&td_pix32);
}

struct ToriDraw_Sprite*
toridraw_sprite_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    if( !pix8_palette )
        return NULL;
    struct ToriDraw_Pix8* pix8 = toridraw_pix8_new_from_cache_pix8_palette(pix8_palette);
    struct ToriDraw_PixPalette* palette =
        toridraw_pixpalette_new_from_cache_pix8_palette(pix8_palette);
    if( !pix8 || !palette )
    {
        toridraw_pix8_free(pix8);
        toridraw_pixpalette_free(palette);
        return NULL;
    }
    struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_pix8(pix8, palette);
    toridraw_pix8_free(pix8);
    toridraw_pixpalette_free(palette);
    if( sprite )
    {
        sprite->crop_x = pix8_palette->crop_x;
        sprite->crop_y = pix8_palette->crop_y;
    }
    return sprite;
}
