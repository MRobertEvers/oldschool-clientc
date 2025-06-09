#ifndef TEXTURES_H
#define TEXTURES_H

#include "buffer.h"

#include <stdbool.h>

struct TextureDefinition
{
    int average_hsl;
    bool opaque;
    int* sprite_ids;
    int sprite_ids_count;

    int* sprite_types;
    int* transforms;
    int animation_direction;
    int animation_speed;
};

struct Cache;
struct TextureDefinition* texture_definition_new_from_cache(struct Cache* cache, int id);

struct TextureDefinition*
texture_definition_new_decode(const unsigned char* inputData, int inputLength);
struct TextureDefinition* texture_definition_decode_inplace(
    struct TextureDefinition*, const unsigned char* inputData, int inputLength);

#endif