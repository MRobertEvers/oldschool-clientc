#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_types.h"

#include <stdint.h>

struct ToriDraw_Animation;
struct GameCache_Sequence;
struct GameCache_Flotype;
struct GameCache_SceneryConfig;

struct GameCache
{
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* flotype_hmap;
    struct ToriDraw_Map* scenery_config_hmap;
    struct ToriDraw_Map* animations_hmap;
};

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gamecache);

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct ToriDraw_ModelHandle model);

struct ToriDraw_ModelHandle
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id);

struct ToriDraw_ModelHandle
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id);

void
gamecache_models_clear_all(struct GameCache* gamecache);

void
gamecache_sequence_add(
    struct GameCache* gamecache,
    int sequence_id,
    struct GameCache_Sequence* sequence);

struct GameCache_Sequence*
gamecache_sequence_get(
    struct GameCache* gamecache,
    int sequence_id);

void
gamecache_flotype_add(
    struct GameCache* gamecache,
    int flotype_id,
    struct GameCache_Flotype* flotype);

struct GameCache_Flotype*
gamecache_flotype_get(
    struct GameCache* gamecache,
    int flotype_id);

void
gamecache_scenery_config_add(
    struct GameCache* gamecache,
    int loc_id,
    struct GameCache_SceneryConfig* config);

struct GameCache_SceneryConfig*
gamecache_scenery_config_get(
    struct GameCache* gamecache,
    int loc_id);

void
gamecache_animation_add(
    struct GameCache* gamecache,
    int animbaseframes_id,
    struct ToriDraw_Animation* animation);

struct ToriDraw_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int animbaseframes_id);

void
gamecache_sequences_clear_all(struct GameCache* gamecache);

void
gamecache_floortypes_clear_all(struct GameCache* gamecache);

void
gamecache_scenery_configs_clear_all(struct GameCache* gamecache);

void
gamecache_animations_clear_all(struct GameCache* gamecache);

#endif
