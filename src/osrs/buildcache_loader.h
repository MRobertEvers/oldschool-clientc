#ifndef BUILD_CACHE_LOADER_H
#define BUILD_CACHE_LOADER_H

#include "game.h"
#include "osrs/buildcache.h"

void
buildcache_loader_add_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    int data_size,
    void* data);

void
buildcache_loader_add_config_scenery(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size);

void
buildcache_loader_add_model(
    struct BuildCache* buildcache,
    int model_id,
    int data_size,
    void* data);

void
buildcache_loader_add_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    int data_size,
    void* data);

void
buildcache_loader_add_config_underlay(
    struct BuildCache* buildcache,
    int data_size,
    void* data);

void
buildcache_loader_add_config_overlay(
    struct BuildCache* buildcache,
    int data_size,
    void* data);

void
buildcache_loader_add_texture_definitions(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size);

void
buildcache_loader_add_spritepack(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data);

void
buildcache_loader_add_config_sequences(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size);

void
buildcache_loader_add_frame_blob(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data);

void
buildcache_loader_add_framemap(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data);

#endif
