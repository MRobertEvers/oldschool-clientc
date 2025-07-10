#ifndef TEXTURE_PIXELS_H
#define TEXTURE_PIXELS_H

#include "sprites.h"
#include "textures.h"

int* texture_pixels_new_from_definition(
    struct CacheTexture* def,
    int size,
    struct CacheSpritePack* sprite_packs,
    int* pack_ids,
    int pack_count,
    double brightness);
void texture_pixels_free(struct TexturePixels* pixels);

#endif