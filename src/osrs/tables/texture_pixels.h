#ifndef TEXTURE_PIXELS_H
#define TEXTURE_PIXELS_H

#include "sprites.h"
#include "textures.h"

int* texture_pixels_new_from_definition(
    struct TextureDefinition* def,
    int size,
    struct SpritePack* sprite_packs,
    int* pack_ids,
    int pack_count,
    int brightness);
void texture_pixels_free(struct TexturePixels* pixels);

#endif