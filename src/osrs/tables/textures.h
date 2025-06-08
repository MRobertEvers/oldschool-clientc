#ifndef TEXTURES_H
#define TEXTURES_H

#include "buffer.h"

#include <stdbool.h>

struct TextureDefinition
{
    int average_hsl;
    bool opaque;
    int* file_ids;
    int file_ids_count;

    int* sprite_types;
    int* transforms;
    int animation_direction;
    int animation_speed;
};

struct Cache;
struct TextureDefinition* texture_definition_new_from_cache(struct Cache* cache, int id);

struct TextureDefinition*
texture_definition_new_decode(const unsigned char* inputData, int inputLength);

#endif