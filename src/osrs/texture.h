#ifndef TEXTURE_H
#define TEXTURE_H

#include "datastruct/hmap.h"
#include "graphics/dash.h"
#include "tables/textures.h"

#include <stdbool.h>

struct SpritePackEntry
{
    int id;
    struct CacheSpritePack* spritepack;
};

struct Texture*
texture_new_from_definition(struct CacheTexture* texture_definition, struct HMap* sprites_hmap);

void texture_free(struct Texture* texture);
#endif