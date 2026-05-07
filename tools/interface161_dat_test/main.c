/*
 * Legacy dat cache (main_file_cache.dat + idx*): load CONFIG_DAT_INTERFACES jagfile,
 * decode the "data" blob into CacheDatConfigComponent[], draw RECT / GRAPHIC widgets
 * under a root component id to a BMP.
 *
 * Unlike tools/interface161_test (dat2 + CACHE_INTERFACES archives), --iface selects a
 * root *component id* in the decoded component table (Lost City-style flat ids),
 * not an OSRS interface archive index.
 *
 * --mount is not implemented for dat caches (no per-interface file indices).
 *
 * Usage: interface161_dat_test <cache_directory> [--iface N] [--sprites] [out.bmp]
 */

#include "bmp.h"
#include "osrs/rscache/archive.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    CANVAS_W = 1024,
    CANVAS_H = 768
};

static void
fill_rect(
    int* px,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb)
{
    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > CANVAS_W )
        x1 = CANVAS_W;
    if( y1 > CANVAS_H )
        y1 = CANVAS_H;
    for( int y = y0; y < y1; y++ )
        for( int x = x0; x < x1; x++ )
            px[y * stride + x] = argb;
}

static void
draw_rect_dat(
    struct CacheDatConfigComponent* c,
    int abs_x,
    int abs_y,
    int* pixels)
{
    if( c->alpha != 0 )
        return;
    if( !c->fill )
        return;
    int colour = c->colour & 0xFFFFFF;
    int argb = 0xFF000000 | colour;
    fill_rect(pixels, CANVAS_W, abs_x, abs_y, abs_x + c->width, abs_y + c->height, argb);
}

/** Pack RGB from pix8 (palette 0 = transparent). */
static int
pix8_to_argb(int rgb)
{
    if( rgb == 0 )
        return 0;
    return (int)(0xFF000000u | (unsigned)rgb);
}

static void
blit_pix32_sprite(
    struct CacheDatPix32* p,
    int dest_x,
    int dest_y,
    int* pixels)
{
    if( !p || !p->pixels )
        return;
    int sw = p->draw_width;
    int sh = p->draw_height;
    int stride_w = p->stride_x;
    for( int y = 0; y < sh; y++ )
    {
        int sy = dest_y + y;
        if( sy < 0 || sy >= CANVAS_H )
            continue;
        for( int x = 0; x < sw; x++ )
        {
            int sx = dest_x + x;
            if( sx < 0 || sx >= CANVAS_W )
                continue;
            int src_x = p->crop_x + x;
            int src_y = p->crop_y + y;
            if( src_x < 0 || src_y < 0 || src_x >= stride_w || src_y >= p->stride_y )
                continue;
            int rgb = p->pixels[src_y * stride_w + src_x];
            if( rgb == 0 )
                continue;
            pixels[sy * CANVAS_W + sx] = (int)(0xFF000000u | (unsigned)rgb);
        }
    }
}

static void
blit_pix8_sprite(
    struct CacheDatPix8Palette* p,
    int dest_x,
    int dest_y,
    int* pixels)
{
    if( !p || !p->pixels || !p->palette )
        return;
    int w = p->width;
    int h = p->height;
    for( int y = 0; y < h; y++ )
    {
        int sy = dest_y + y;
        if( sy < 0 || sy >= CANVAS_H )
            continue;
        for( int x = 0; x < w; x++ )
        {
            int sx = dest_x + x;
            if( sx < 0 || sx >= CANVAS_W )
                continue;
            uint8_t ix = p->pixels[y * w + x];
            int rgb = p->palette[ix];
            int argb = pix8_to_argb(rgb);
            if( (argb >> 24) == 0 )
                continue;
            pixels[sy * CANVAS_W + sx] = argb;
        }
    }
}

static void
draw_graphic_dat(
    struct FileListDat* media,
    int index_dat_idx,
    struct CacheDatConfigComponent* c,
    int abs_x,
    int abs_y,
    int* pixels)
{
    const char* graphic_name = c->graphic;
    if( !graphic_name || !graphic_name[0] )
        return;

    char filename_buf[256];
    int sprite_idx = 0;
    if( sscanf(graphic_name, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        snprintf(filename_buf, sizeof(filename_buf), "%s", graphic_name);
        sprite_idx = 0;
    }
    size_t len = strlen(filename_buf);
    if( len + 5 > sizeof(filename_buf) )
        return;
    if( len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0 )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }

    int data_idx = filelist_dat_find_file_by_name(media, filename_buf);
    if( data_idx < 0 )
        return;

    struct CacheDatPix32* p32 = cache_dat_pix32_new(
        media->files[data_idx],
        media->file_sizes[data_idx],
        media->files[index_dat_idx],
        media->file_sizes[index_dat_idx],
        sprite_idx);
    if( p32 )
    {
        blit_pix32_sprite(p32, abs_x, abs_y, pixels);
        cache_dat_pix32_free(p32);
        return;
    }

    struct CacheDatPix8Palette* p8 = cache_dat_pix8_palette_new(
        media->files[data_idx],
        media->file_sizes[data_idx],
        media->files[index_dat_idx],
        media->file_sizes[index_dat_idx],
        sprite_idx);
    if( p8 )
    {
        blit_pix8_sprite(p8, abs_x, abs_y, pixels);
        cache_dat_pix8_palette_free(p8);
    }
}

static void
draw_dat_component(
    struct CacheDatConfigComponentList* list,
    struct FileListDat* media,
    int index_dat_idx,
    struct CacheDatConfigComponent* c,
    int abs_x,
    int abs_y,
    int scroll_y,
    int* pixels,
    int want_sprites)
{
    if( !c || c->hide )
        return;

    if( c->type != COMPONENT_TYPE_LAYER )
    {
        if( c->type == COMPONENT_TYPE_RECT )
            draw_rect_dat(c, abs_x, abs_y, pixels);
        else if( want_sprites && c->type == COMPONENT_TYPE_GRAPHIC && media && index_dat_idx >= 0 )
            draw_graphic_dat(media, index_dat_idx, c, abs_x, abs_y, pixels);
        return;
    }

    if( !c->children || !c->childX || !c->childY )
        return;

    for( int i = 0; i < c->children_count; i++ )
    {
        int child_id = c->children[i];
        if( child_id < 0 || child_id >= list->components_count )
            continue;
        struct CacheDatConfigComponent* ch = list->components[child_id];
        if( !ch )
            continue;

        int cx = abs_x + c->childX[i] + ch->x;
        int cy = abs_y + c->childY[i] - scroll_y;

        draw_dat_component(list, media, index_dat_idx, ch, cx, cy, 0, pixels, want_sprites);
    }
}

static void
free_component_list(struct CacheDatConfigComponentList* list)
{
    if( !list )
        return;
    if( list->components )
    {
        for( int i = 0; i < list->components_count; i++ )
        {
            if( list->components[i] )
                cache_dat_config_component_free(list->components[i]);
        }
        free(list->components);
    }
    free(list);
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: interface161_dat_test <cache_directory> [--iface N] [--sprites] [out.bmp]\n"
        "  Opens main_file_cache.dat (not .dat2). --iface is a *component id* in the\n"
        "  decoded interface table, not a CACHE_INTERFACES archive index.\n"
        "  --mount is unsupported.\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    const char* out_path = "interface161_dat_out.bmp";
    int root_id = 0;
    int want_sprites = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iface") == 0 && i + 1 < argc )
        {
            root_id = atoi(argv[++i]);
        }
        else if( strcmp(argv[i], "--sprites") == 0 )
        {
            want_sprites = 1;
        }
        else if( strcmp(argv[i], "--mount") == 0 )
        {
            fprintf(stderr, "interface161_dat_test: --mount is not supported for dat caches\n");
            usage();
            return 1;
        }
        else if( !cache_dir )
        {
            cache_dir = argv[i];
        }
        else
        {
            out_path = argv[i];
        }
    }

    if( !cache_dir )
    {
        usage();
        return 1;
    }

    struct CacheDat* cd = cache_dat_new_from_directory(cache_dir);
    if( !cd )
    {
        fprintf(stderr, "failed to open dat cache: %s\n", cache_dir);
        return 1;
    }

    struct CacheDatArchive* iface_arch =
        cache_dat_archive_new_load(cd, CACHE_DAT_CONFIGS, CONFIG_DAT_INTERFACES);
    if( !iface_arch )
    {
        fprintf(stderr, "failed to load CONFIG_DAT_INTERFACES archive\n");
        cache_dat_free(cd);
        return 1;
    }

    struct FileListDat* iface_fl = filelist_dat_new_from_cache_dat_archive(iface_arch);
    cache_dat_archive_free(iface_arch);
    iface_arch = NULL;

    if( !iface_fl )
    {
        fprintf(stderr, "failed to unpack interface jagfile\n");
        cache_dat_free(cd);
        return 1;
    }

    int data_idx = -1;
    int name_hash = archive_name_hash_dat("data");
    for( int i = 0; i < iface_fl->file_count; i++ )
    {
        if( iface_fl->file_name_hashes[i] == name_hash )
        {
            data_idx = i;
            break;
        }
    }

    if( data_idx < 0 )
    {
        fprintf(stderr, "interface jagfile: missing 'data' entry\n");
        filelist_dat_free(iface_fl);
        cache_dat_free(cd);
        return 1;
    }

    struct CacheDatConfigComponentList* list = cache_dat_config_component_list_new_decode(
        iface_fl->files[data_idx], iface_fl->file_sizes[data_idx]);
    filelist_dat_free(iface_fl);

    if( !list )
    {
        fprintf(stderr, "failed to decode component list\n");
        cache_dat_free(cd);
        return 1;
    }

    if( root_id < 0 || root_id >= list->components_count || !list->components[root_id] )
    {
        fprintf(
            stderr,
            "invalid or missing root component id %d (table size %d)\n",
            root_id,
            list->components_count);
        free_component_list(list);
        cache_dat_free(cd);
        return 1;
    }

    struct FileListDat* media_fl = NULL;
    int media_index_dat = -1;
    if( want_sprites )
    {
        struct CacheDatArchive* med_arch =
            cache_dat_archive_new_load(cd, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);
        if( med_arch )
        {
            media_fl = filelist_dat_new_from_cache_dat_archive(med_arch);
            cache_dat_archive_free(med_arch);
        }
        if( media_fl )
            media_index_dat = filelist_dat_find_file_by_name(media_fl, "index.dat");
        if( media_index_dat < 0 )
        {
            fprintf(stderr, "warning: --sprites set but media jagfile or index.dat missing\n");
            if( media_fl )
            {
                filelist_dat_free(media_fl);
                media_fl = NULL;
            }
        }
    }

    int* pixels = calloc((size_t)(CANVAS_W * CANVAS_H), sizeof(int));
    if( !pixels )
    {
        if( media_fl )
            filelist_dat_free(media_fl);
        free_component_list(list);
        cache_dat_free(cd);
        return 1;
    }
    fill_rect(pixels, CANVAS_W, 0, 0, CANVAS_W, CANVAS_H, 0xFF202428);

    draw_dat_component(
        list,
        media_fl,
        media_index_dat,
        list->components[root_id],
        0,
        0,
        0,
        pixels,
        want_sprites);

    printf("root component %d -> %s\n", root_id, out_path);
    bmp_write_file(out_path, pixels, CANVAS_W, CANVAS_H);

    free(pixels);
    if( media_fl )
        filelist_dat_free(media_fl);
    free_component_list(list);
    cache_dat_free(cd);
    return 0;
}
