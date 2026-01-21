#ifndef GIO_CACHE_DAT_H
#define GIO_CACHE_DAT_H

#include "osrs/gio.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"

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

struct CacheDat*
gioqb_cache_dat_new(void);

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
gioqb_cache_dat_models_new_load(
    struct CacheDat* cache_dat,
    int model_id);

struct FileBuffer*
gioqb_cache_dat_config_flotype_file_load(struct CacheDat* cache_dat);

struct CacheDatArchive* //
gioqb_cache_dat_config_texture_sprites_new_load(struct CacheDat* cache_dat);

struct FileListDatIndexed*
gioqb_cache_dat_config_scenery_fileidx_new_load(struct CacheDat* cache_dat);

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct CacheDat* cache_dat,
    struct GIOMessage* message);

#endif