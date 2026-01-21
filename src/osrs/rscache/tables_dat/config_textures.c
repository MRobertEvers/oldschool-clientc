#include "config_textures.h"

#include "../rsbuf.h"

#include <stdio.h>
#include <string.h>

struct CacheDatTexture*
cache_dat_texture_new_from_filelist_dat(
    struct FileListDat* textures_filelist,
    int texture_id,
    int sprite_count)
{
    char name[16] = { 0 };
    snprintf(name, sizeof(name), "%d.dat", texture_id);
    int file_data_idx = filelist_dat_find_file_by_name(textures_filelist, name);
    if( file_data_idx == -1 )
        return NULL;

    int file_index_idx = filelist_dat_find_file_by_name(textures_filelist, "index.dat");
    if( file_index_idx == -1 )
        return NULL;

    struct RSBuffer databuf = { .data = textures_filelist->files[file_data_idx],
                                .size = textures_filelist->file_sizes[file_data_idx] };

    struct RSBuffer indexbuf = { .data = textures_filelist->files[file_index_idx],
                                 .size = textures_filelist->file_sizes[file_index_idx] };

    //  index.pos = data.g2();
    //  this.owi = index.g2();
    //  this.ohi = index.g2();

    //  int palCount = index.g1();
    //  this.bpal = new int[palCount];
    //  for (int i = 0; i < palCount - 1; i++) {
    //      this.bpal[i + 1] = index.g3();
    //  }

    //  for (int i = 0; i < sprite; i++) {
    //      index.pos += 2;
    //      data.pos += index.g2() * index.g2();
    //      index.pos++;
    //  }

    //  this.xof = index.g1();
    //  this.yof = index.g1();
    //  this.wi = index.g2();
    //  this.hi = index.g2();

    //  int pixelOrder = index.g1();
    //  int len = this.wi * this.hi;
    //  this.pixels = new byte[len];

    //  if (pixelOrder == 0) {
    //      for (int i = 0; i < len; i++) {
    //          this.pixels[i] = data.g1b();
    //      }
    //  } else if (pixelOrder == 1) {
    //      for (int x = 0; x < this.wi; x++) {
    //          for (int y = 0; y < this.hi; y++) {
    //              this.pixels[x + y * this.wi] = data.g1b();
    //          }
    //      }
    //  }

    struct CacheDatTexture* texture = malloc(sizeof(struct CacheDatTexture));
    if( !texture )
        return NULL;

    indexbuf.position = g2(&databuf);
    int owi = g2(&indexbuf);
    int ohi = g2(&indexbuf);
    int pal_count = g1(&indexbuf);
    int* bpal = malloc(pal_count * sizeof(int));
    if( !bpal )
        return NULL;
    memset(bpal, 0, pal_count * sizeof(int));
    for( int i = 0; i < pal_count - 1; i++ )
        bpal[i + 1] = g3(&indexbuf);

    for( int i = 0; i < sprite_count; i++ )
    {
        indexbuf.position += 2;
        databuf.position += g2(&indexbuf) * g2(&indexbuf);
        indexbuf.position++;
    }

    int xof = g1(&indexbuf);
    int yof = g1(&indexbuf);
    int wi = g2(&indexbuf);
    int hi = g2(&indexbuf);
    int pixel_order = g1(&indexbuf);
    int len = wi * hi;
    int* pixels = malloc(len * sizeof(int));
    if( !pixels )
        return NULL;
    if( pixel_order == 0 )
    {
        for( int i = 0; i < len; i++ )
            pixels[i] = g1b(&databuf);
    }
    else if( pixel_order == 1 )
    {
        for( int x = 0; x < wi; x++ )
            for( int y = 0; y < hi; y++ )
                pixels[x + y * wi] = g1b(&databuf);
    }

    texture->xof = xof;
    texture->yof = yof;
    texture->wi = wi;
    texture->hi = hi;
    texture->pixels = pixels;
    texture->pixel_count = len;
    texture->palette = bpal;
    texture->palette_count = pal_count;

    return texture;
}

void
cache_dat_texture_free(struct CacheDatTexture* texture)
{
    if( !texture )
        return;
    free(texture->pixels);
    free(texture);
}