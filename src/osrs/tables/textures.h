#ifndef TEXTURES_H
#define TEXTURES_H

#include "../scene_cache.h"

#include <stdbool.h>

enum TextureDirection
{
    TEXTURE_DIRECTION_NONE,
    TEXTURE_DIRECTION_V_DOWN = 1,
    TEXTURE_DIRECTION_U_DOWN = 2,
    TEXTURE_DIRECTION_V_UP = 3,
    TEXTURE_DIRECTION_U_UP = 4,
};

static const int TEXTURE_U_DIRECTION[] = { 0, 0, -1, 0, 1 };

static const int TEXTURE_V_DIRECTION[] = { 0, -1, 0, 1, 0 };

struct CacheTexture
{
    int average_hsl;
    bool opaque;
    int* sprite_ids;
    int sprite_ids_count;

    int* sprite_types;
    int* transforms;

    // See direction above.
    int animation_direction;

    // Pixels per time unit
    int animation_speed;
};

struct Cache;
struct CacheTexture* texture_definition_new_from_cache(struct Cache* cache, int id);

void texture_definition_free(struct CacheTexture* texture_definition);

struct CacheTexture* texture_definition_new_decode(const unsigned char* inputData, int inputLength);
struct CacheTexture* texture_definition_decode_inplace(
    struct CacheTexture*, const unsigned char* inputData, int inputLength);

struct CacheTextureMapLoader;
struct CacheTextureMapLoader*
texture_definition_map_loader_new_from_archive(struct CacheArchive* archive);

void texture_definition_map_loader_load(struct CacheTextureMapLoader* loader, int id);

void texture_definition_map_loader_free(struct CacheTextureMapLoader* loader);

struct CacheTextureMap;
struct CacheTextureMap*
texture_definition_map_new_from_loader(struct CacheTextureMapLoader* loader);

struct CacheTexture* texture_definition_map_get(struct CacheTextureMap* loader, int id);
void texture_definition_map_free(struct CacheTextureMap* map);

#endif