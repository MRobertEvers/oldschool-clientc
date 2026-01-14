#ifndef TEXTURE_H
#define TEXTURE_H

#include "graphics/dash.h"
#include "tables/textures.h"

#include <stdbool.h>

struct SpritePackEntry
{
    int id;
    struct CacheSpritePack* spritepack;
};

struct DashTexture*
texture_new_from_definition(
    struct CacheTexture* texture_definition,
    struct DashMap* sprites_hmap);

void
texture_free(struct DashTexture* texture);
#endif