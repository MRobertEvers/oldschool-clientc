#ifndef GIO_CACHE_H
#define GIO_CACHE_H

#include "osrs/gio.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/tables/model.h"

struct Cache*
gioqb_cache_new(void);

struct CacheArchive* //
gioqb_cache_model_new_load(
    struct Cache* cache,
    int model_id);

struct CacheArchive* //
gioqb_cache_texture_new_load(struct Cache* cache);

struct CacheArchive* //
gioqb_cache_spritepack_new_load(
    struct Cache* cache,
    int spritepack_id);

struct CacheArchive* //
gioqb_cache_map_scenery_new_load(
    struct Cache* cache,
    int chunk_x,
    int chunk_y);

struct CacheArchive* //
gioqb_cache_map_terrain_new_load(
    struct Cache* cache,
    int chunk_x,
    int chunk_y);

struct CacheArchive* //
gioqb_cache_config_scenery_new_load(struct Cache* cache);

struct CacheArchive* //
gioqb_cache_config_underlay_new_load(struct Cache* cache);

struct CacheArchive* //
gioqb_cache_config_overlay_new_load(struct Cache* cache);

struct CacheArchive* //
gioqb_cache_config_sequences_new_load(struct Cache* cache);

struct CacheArchive* //
gioqb_cache_animation_new_load(
    struct Cache* cache,
    int animation_aka_archive_id);

struct CacheArchive* //
gioqb_cache_framemap_new_load(
    struct Cache* cache,
    int framemap_id);

void
gioqb_cache_fullfill(
    struct GIOQueue* io,
    struct Cache* cache,
    struct GIOMessage* message);

#endif