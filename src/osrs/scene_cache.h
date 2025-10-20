#ifndef SCENE_CACHE_C
#define SCENE_CACHE_C

#include "tables/model.h"
#include "tables/sprites.h"
#include "tables/textures.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ModelCache;

struct ModelCache* model_cache_new();
void model_cache_free(struct ModelCache* model_cache);

struct CacheModel*
model_cache_checkout(struct ModelCache* model_cache, struct Cache* cache, int model_id);

void model_cache_checkin(struct ModelCache* model_cache, struct CacheModel* model);

struct Texture
{
    int* texels;
    int width;
    int height;

    int animation_direction;
    int animation_speed;

    bool opaque;
};

void texture_animate(struct Texture* texture, int time_delta);

struct TexturesCache* textures_cache_new(struct Cache* cache);
void textures_cache_free(struct TexturesCache* textures_cache);

struct TexWalk
{
    void* item;
    struct Texture* texture;
};

struct Texture* textures_cache_checkout(
    struct TexturesCache* textures_cache,
    struct Cache* cache,
    int texture_id,
    int size,
    double brightness);
void textures_cache_checkin(struct TexturesCache* textures_cache, struct Texture* texture);

struct TexWalk* textures_cache_walk_new(struct TexturesCache* textures_cache);
void textures_cache_walk_free(struct TexWalk* walk);

struct Texture* textures_cache_walk_next(struct TexWalk* walk);

#ifdef __cplusplus
}
#endif

#endif