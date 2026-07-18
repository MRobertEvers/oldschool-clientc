#include "toridraw_sprite_from_torirs.h"

#include "engine/torirs_types.h"

#include "toridraw_sprite.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_Sprite*
ToriDraw_SpriteFromToriRSFrame(struct ToriRS_SpriteFrame const* frame)
{
    uint32_t* argb;
    struct ToriDraw_Sprite* spr;
    size_t n;

    assert(frame);
    if( !frame->pixels_argb || frame->width <= 0 || frame->height <= 0 )
        return NULL;

    n = (size_t)frame->width * (size_t)frame->height;
    argb = malloc(n * sizeof(uint32_t));
    if( !argb )
        return NULL;
    memcpy(argb, frame->pixels_argb, n * sizeof(uint32_t));

    spr = ToriDraw_SpriteNewFromArgbOwned(argb, frame->width, frame->height);
    if( !spr )
        return NULL;

    spr->crop_x = frame->crop_x;
    spr->crop_y = frame->crop_y;
    spr->crop_width = frame->crop_width;
    spr->crop_height = frame->crop_height;
    return spr;
}

struct ToriDraw_Sprite**
ToriDraw_SpritesFromToriRS(
    struct ToriRS_Sprite const* src,
    int* out_count)
{
    struct ToriDraw_Sprite** sprites;
    int count;
    int i;

    assert(src);
    if( out_count )
        *out_count = 0;
    if( !src->frames || src->frame_count <= 0 )
        return NULL;

    count = src->frame_count;
    sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
        return NULL;

    for( i = 0; i < count; i++ )
    {
        sprites[i] = ToriDraw_SpriteFromToriRSFrame(&src->frames[i]);
        if( !sprites[i] )
        {
            for( int j = 0; j < i; j++ )
                ToriDraw_SpriteFree(sprites[j]);
            free(sprites);
            return NULL;
        }
    }

    if( out_count )
        *out_count = count;
    return sprites;
}
