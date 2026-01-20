#ifndef GIO_CACHE_DAT_H
#define GIO_CACHE_DAT_H

#include "osrs/gio.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"

struct CacheDat*
gioqb_cache_dat_new(void);

struct CacheDatArchive* //
gioqb_cache_dat_model_new_load(
    struct CacheDat* cache_dat,
    int model_id);

struct CacheDatArchive* //
gioqb_cache_dat_texture_new_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_spritepack_new_load(
    struct CacheDat* cache_dat,
    int spritepack_id);

struct CacheDatArchive* //
gioqb_cache_dat_map_scenery_new_load(
    struct CacheDat* cache_dat,
    int chunk_x,
    int chunk_y);

struct CacheDatArchive* //
gioqb_cache_dat_map_terrain_new_load(
    struct CacheDat* cache_dat,
    int chunk_x,
    int chunk_y);

struct CacheDatArchive* //
gioqb_cache_dat_config_scenery_new_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_config_underlay_new_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_config_overlay_new_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_config_sequences_new_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_animation_new_load(
    struct CacheDat* cache_dat,
    int animation_aka_archive_id);

struct CacheDatArchive* //
gioqb_cache_dat_framemap_new_load(
    struct CacheDat* cache_dat,
    int framemap_id);

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct CacheDat* cache_dat,
    struct GIOMessage* message);

#endif