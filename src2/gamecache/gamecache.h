#ifndef GAMECACHE_H
#define GAMECACHE_H

#include "gamecache_types.h"

#include <stdbool.h>
#include <stdint.h>

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gamecache);

void
gamecache_model_add(
    struct GameCache* gamecache,
    int model_id,
    struct GameCache_Model* model);

struct GameCache_Model*
gamecache_model_get(
    struct GameCache* gamecache,
    int model_id);

bool
gamecache_model_has(
    struct GameCache* gamecache,
    int model_id);

struct GameCache_Model*
gamecache_model_remove(
    struct GameCache* gamecache,
    int model_id);

void
gamecache_models_clear_all(struct GameCache* gamecache);

void
gamecache_texture_add(
    struct GameCache* gamecache,
    int texture_id,
    struct GameCache_Texture* texture);

struct GameCache_Texture*
gamecache_texture_get(
    struct GameCache* gamecache,
    int texture_id);

bool
gamecache_texture_has(
    struct GameCache* gamecache,
    int texture_id);

void
gamecache_textures_clear_all(struct GameCache* gamecache);

void
gamecache_map_terrain_add(
    struct GameCache* gamecache,
    int map_id,
    struct GameCache_MapTerrain* terrain);

struct GameCache_MapTerrain*
gamecache_map_terrain_get(
    struct GameCache* gamecache,
    int map_id);

bool
gamecache_map_terrain_has(
    struct GameCache* gamecache,
    int map_id);

void
gamecache_map_scenery_add(
    struct GameCache* gamecache,
    int map_id,
    struct GameCache_MapLocs* locs);

struct GameCache_MapLocs*
gamecache_map_scenery_get(
    struct GameCache* gamecache,
    int map_id);

bool
gamecache_map_scenery_has(
    struct GameCache* gamecache,
    int map_id);

void
gamecache_flotype_add(
    struct GameCache* gamecache,
    int flo_id,
    struct GameCache_Flotype* flotype);

struct GameCache_Flotype*
gamecache_flotype_get(
    struct GameCache* gamecache,
    int flo_id);

bool
gamecache_flotype_has(
    struct GameCache* gamecache,
    int flo_id);

void
gamecache_location_add(
    struct GameCache* gamecache,
    int loc_id,
    struct GameCache_Location* loc);

struct GameCache_Location*
gamecache_location_get(
    struct GameCache* gamecache,
    int loc_id);

bool
gamecache_location_has(
    struct GameCache* gamecache,
    int loc_id);

void
gamecache_sequence_add(
    struct GameCache* gamecache,
    int seq_id,
    struct GameCache_Sequence* seq);

struct GameCache_Sequence*
gamecache_sequence_get(
    struct GameCache* gamecache,
    int seq_id);

bool
gamecache_sequence_has(
    struct GameCache* gamecache,
    int seq_id);

struct GameCache_Animation*
gamecache_sequence_primary_animation(
    struct GameCache* gamecache,
    int seq_id);

struct GameCache_Animation*
gamecache_sequence_resolved_animation(
    struct GameCache* gamecache,
    int seq_id);

void
gamecache_animation_add(
    struct GameCache* gamecache,
    int anim_id,
    struct GameCache_Animation* animation);

struct GameCache_Animation*
gamecache_animation_get(
    struct GameCache* gamecache,
    int anim_id);

bool
gamecache_animation_has(
    struct GameCache* gamecache,
    int anim_id);

bool
gamecache_sequence_resolve_frame(
    struct GameCache* gamecache,
    int seq_id,
    int frame_index,
    const struct GameCache_AnimFrame** out_frame,
    const struct GameCache_AnimBase** out_base,
    int* out_delay);

#endif
