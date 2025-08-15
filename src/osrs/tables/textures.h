#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdbool.h>

struct CacheTexture
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
struct CacheTexture* texture_definition_new_from_cache(struct Cache* cache, int id);
void texture_definition_free(struct CacheTexture* texture_definition);

struct CacheTexture* texture_definition_new_decode(const unsigned char* inputData, int inputLength);
struct CacheTexture* texture_definition_decode_inplace(
    struct CacheTexture*, const unsigned char* inputData, int inputLength);

#endif