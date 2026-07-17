#include "engine/torirs_sprite_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ToriRS_SpriteFrame
sprite_frame_from_dat2_sprite(
    struct RSCache_Dat2SpritePack const* pack,
    int frame_index)
{
    struct ToriRS_SpriteFrame frame;
    struct RSCache_Dat2Sprite* def;
    int* px;

    memset(&frame, 0, sizeof(frame));
    assert(pack);
    assert(frame_index >= 0 && frame_index < pack->count);

    def = &pack->sprites[frame_index];
    px = RSCache_Dat2SpriteGetPixels(def, pack->palette, 0);
    if( !px )
        return frame;

    frame.pixels_argb = (uint32_t*)px;
    frame.width = def->crop_width > 0 ? def->crop_width : def->width;
    frame.height = def->crop_height > 0 ? def->crop_height : def->height;
    frame.crop_x = def->offset_x;
    frame.crop_y = def->offset_y;
    frame.crop_width = frame.width;
    frame.crop_height = frame.height;
    return frame;
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Pack(
    struct RSCache_Dat2SpritePack* pack,
    int sprite_id)
{
    struct ToriRS_Sprite* sprite;
    int i;
    int loaded;

    assert(pack);
    if( pack->count <= 0 )
        return NULL;

    sprite = calloc(1, sizeof(*sprite));
    if( !sprite )
        return NULL;

    sprite->frames = calloc((size_t)pack->count, sizeof(*sprite->frames));
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }
    sprite->frame_count = pack->count;
    snprintf(sprite->name, sizeof(sprite->name), "spr:%d", sprite_id);

    loaded = 0;
    for( i = 0; i < pack->count; i++ )
    {
        sprite->frames[i] = sprite_frame_from_dat2_sprite(pack, i);
        if( sprite->frames[i].pixels_argb )
            loaded++;
    }

    if( loaded <= 0 )
    {
        ToriRS_SpriteFree(sprite);
        return NULL;
    }
    return sprite;
}

struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Archive(
    struct RSCache_Dat2DiskArchive* archive,
    int sprite_id)
{
    struct RSCache_Dat2SpritePack* pack;
    struct ToriRS_Sprite* sprite;

    if( !archive || sprite_id < 0 )
    {
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    pack = RSCache_Dat2SpritePackNewDecode(
        (const unsigned char*)archive->data,
        archive->data_size,
        RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
        return NULL;
    }

    sprite = ToriRS_SpriteFromDat2Pack(pack, sprite_id);
    RSCache_Dat2SpritePackFree(pack);
    return sprite;
}
