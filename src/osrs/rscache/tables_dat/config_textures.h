#ifndef CONFIG_TEXTURES_H
#define CONFIG_TEXTURES_H

#include "../cache_dat.h"
#include "../filelist.h"
#include "configs_dat.h"

#include <stdbool.h>

// Pix3D.unpackTextures in LostCity JavaClient
struct CacheDatTexture
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

struct CacheDatTexture*
cache_dat_texture_new_from_filelist_dat(
    struct FileListDat* textures_filelist,
    int texture_id,
    int sprite_count);

void
cache_dat_texture_free(struct CacheDatTexture* texture);
#endif