#ifndef RSCACHE_RSCACHEDAT1A_CONFIGTEXTURES_H
#define RSCACHE_RSCACHEDAT1A_CONFIGTEXTURES_H

#include "../dat1disk/dat1disk.h"
#include "../shared/shared_file_list.h"
#include "dat1a_configs_dat.h"

#include <stdbool.h>

// Pix3D.unpackTextures in LostCity JavaClient
struct RSCacheDat1A_ConfigTexture
{
    int xof;
    int yof;
    int wi;
    int hi;
    int* pixels;
    int pixel_count;

    int* palette;
    int palette_count;
};

struct RSCacheDat1A_ConfigTexture*
RSCacheDat1A_ConfigTextureNewFromFilelistDat(
    struct RSCacheShared_FileListDat* textures_filelist,
    int texture_id,
    int sprite_count);

void
RSCacheDat1A_ConfigTextureFree(struct RSCacheDat1A_ConfigTexture* texture);
#endif