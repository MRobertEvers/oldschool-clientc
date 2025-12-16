#ifndef TEXTURE_H
#define TEXTURE_H

#include "datastruct/hmap.h"
#include "tables/textures.h"

#include <stdbool.h>

struct Texture
{
    int id;

    int* texels;
    int width;
    int height;

    int animation_direction;
    int animation_speed;

    bool opaque;
};

struct SpritePackEntry
{
    int id;
    struct CacheSpritePack* spritepack;
};

struct Texture*
texture_new_from_definition(struct CacheTexture* texture_definition, struct HMap* sprites_hmap);

void texture_free(struct Texture* texture);
#endif