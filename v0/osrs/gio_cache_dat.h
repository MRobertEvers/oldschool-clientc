#ifndef GIO_RSCACHE_DAT1DISK_H
#define GIO_RSCACHE_DAT1DISK_H

#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat2a/dat2a_model.h"

struct FileBuffer
{
    char* data;
    int data_size;
};

struct FileBuffer*
filebuffer_new(
    char* data,
    int data_size);

void
filebuffer_free(struct FileBuffer* filebuffer);

struct RSCacheDat1Disk*
gioqb_cache_dat_new(void);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_map_scenery_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int chunk_x,
    int chunk_y);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_map_terrain_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int chunk_x,
    int chunk_y);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_models_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int model_id);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_animbaseframes_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int animbaseframes_id);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_sound_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int sound_id);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_texture_sprites_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_media_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_title_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_configs_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_interfaces_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_media_2d_graphics_new_load(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_version_list_new_load(struct RSCacheDat1Disk* cache_dat);

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct RSCacheDat1Disk* cache_dat,
    struct GIOMessage* message);

#endif