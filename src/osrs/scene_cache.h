#ifndef SCENE_CACHE_C
#define SCENE_CACHE_C

#include "tables/model.h"
#include "tables/sprites.h"
#include "tables/textures.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

void
textures_cache_add(struct TexturesCache* textures_cache, int texture_id, struct Texture* texture);
bool textures_cache_contains(struct TexturesCache* textures_cache, int texture_id);
struct Texture* textures_cache_get(struct TexturesCache* textures_cache, int texture_id);
struct Texture* textures_cache_remove(struct TexturesCache* textures_cache, int texture_id);

struct TexWalk* textures_cache_walk_new(struct TexturesCache* textures_cache);
void textures_cache_walk_free(struct TexWalk* walk);

struct Texture* textures_cache_walk_next(struct TexWalk* walk);

#ifdef __cplusplus
}
#endif

#endif