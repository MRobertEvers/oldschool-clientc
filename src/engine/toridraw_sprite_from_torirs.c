#include "toridraw_sprite_from_torirs.h"

#include "engine/torirs_types.h"

#include "toridraw_sprite.h"

#include <assert.h>
#include <stdlib.h>

struct ToriDraw_Sprite*
ToriDraw_SpriteFromToriRSFrame(struct ToriRS_SpriteFrame* frame)
{
    uint32_t* argb;
    struct ToriDraw_Sprite* spr;

    assert(frame);
    if( !frame->pixels_argb || frame->width <= 0 || frame->height <= 0 )
        return NULL;

    /*
     * A move, not a copy.
     *
     * These pixels were decoded once into the ToriRS sprite the provider
     * holds, and the only thing that ever reads them is this conversion:
     * UITreeSceneBridge_EnsureSprite memoises the scene id per graphic id and
     * never clears the map, and every other reader of a provider sprite looks
     * at frame_count alone. Copying therefore kept a second full set of ARGB
     * resident for the life of the scene — 2.5 MB across ~875 frames — to
     * back a buffer nobody would look at again.
     *
     * The guard above is what makes the handover safe: with width and height
     * already known positive, ToriDraw_SpriteNewFromArgbOwned cannot return
     * NULL, so the buffer cannot be stranded between the two owners.
     */
    argb = frame->pixels_argb;
    frame->pixels_argb = NULL;

    spr = ToriDraw_SpriteNewFromArgbOwned(argb, frame->width, frame->height);
    assert(spr);

    spr->crop_x = frame->crop_x;
    spr->crop_y = frame->crop_y;
    spr->crop_width = frame->crop_width;
    spr->crop_height = frame->crop_height;
    /* The decoder's statement about its own pixels travels with them. */
    spr->alpha_channel = frame->alpha_channel;
    return spr;
}

struct ToriDraw_Sprite**
ToriDraw_SpritesFromToriRS(
    struct ToriRS_Sprite* src,
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
    assert(sprites);

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
