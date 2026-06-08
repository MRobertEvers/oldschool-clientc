#ifndef GAMECACHE_SUBMIT_H
#define GAMECACHE_SUBMIT_H

struct Dat1BuildCache;
struct GameCache;
struct GameCacheL;

void
gamecache_submit_map_terrain_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int map_id);

void
gamecache_submit_map_scenery_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int map_id);

void
gamecache_submit_animation_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int anim_id);

void
gamecache_submit_model_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1,
    int model_id);

void
gamecache_submit_all_sequences_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1);

void
gamecache_submit_all_flotypes_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1);

void
gamecache_submit_all_locations_from_dat1(
    struct GameCache* gamecache,
    struct Dat1BuildCache* dat1);

#endif
