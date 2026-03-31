/*
 * Load interface archive N from dat2 cache (table CACHE_INTERFACES), decode each
 * file as IF1/IF3 Component, optionally blit type-3/type-5 widgets to a BMP.
 *
 * Usage: interface161_test <cache_directory> [--iface N] [--sprites] [out.bmp]
 */

#include "bmp.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/component.h"
#include "osrs/rscache/tables/sprites.h"

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
blit_rgba_sprite(
    int* dest,
    int dstride,
    int dx,
    int dy,
    const int* spr,
    int sw,
    int sh)
{
    for( int y = 0; y < sh; y++ )
    {
        int sy = dy + y;
        if( sy < 0 || sy >= CANVAS_H )
            continue;
        for( int x = 0; x < sw; x++ )
        {
            int sx = dx + x;
            if( sx < 0 || sx >= CANVAS_W )
                continue;
            int p = spr[y * sw + x];
            int a = (p >> 24) & 0xFF;
            if( a == 0 )
                continue;
            if( a == 255 )
                dest[sy * dstride + sx] = p;
            else
            {
                int d = dest[sy * dstride + sx];
                int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
                int sr = (p >> 16) & 0xFF, sg = (p >> 8) & 0xFF, sb = p & 0xFF;
                int rr = (sr * a + dr * (255 - a)) / 255;
                int rg = (sg * a + dg * (255 - a)) / 255;
                int rb = (sb * a + db * (255 - a)) / 255;
                dest[sy * dstride + sx] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
            }
        }
    }
}

static int
decode_component_from_bytes(
    Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index)
{
    if( !data || size <= 0 )
        return -1;
    struct RSBuffer buf;
    rsbuf_init(&buf, (int8_t*)data, size);
    Component_init(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: interface161_test <cache_directory> [--iface N] [--sprites] [out.bmp]\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    const char* out_path = "interface161_out.bmp";
    int iface = 161;
    int want_sprites = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iface") == 0 && i + 1 < argc )
        {
            iface = atoi(argv[++i]);
        }
        else if( strcmp(argv[i], "--sprites") == 0 )
        {
            want_sprites = 1;
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

    struct Cache* cache = cache_new_from_directory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", cache_dir);
        return 1;
    }

    struct CacheArchive* arch = cache_archive_new_load(cache, CACHE_INTERFACES, iface);
    if( !arch )
    {
        fprintf(stderr, "failed to load CACHE_INTERFACES archive %d\n", iface);
        cache_free(cache);
        return 1;
    }

    cache_archive_init_metadata(cache, arch);
    struct FileList* fl = filelist_new_from_cache_archive(arch);
    if( !fl )
    {
        fprintf(stderr, "failed to unpack interface archive file list\n");
        cache_archive_free(arch);
        cache_free(cache);
        return 1;
    }

    int* pixels = calloc((size_t)(CANVAS_W * CANVAS_H), sizeof(int));
    if( !pixels )
    {
        filelist_free(fl);
        cache_archive_free(arch);
        cache_free(cache);
        return 1;
    }
    fill_rect(pixels, CANVAS_W, 0, 0, CANVAS_W, CANVAS_H, 0xFF202428);

    int decoded = 0;
    int in_group = 0;

    for( int fi = 0; fi < fl->file_count; fi++ )
    {
        Component comp;
        if( decode_component_from_bytes(
                &comp, fl->files[fi], fl->file_sizes[fi], iface, fi) != 0 )
        {
            continue;
        }
        decoded++;
        if( (comp.id >> 16) == iface )
            in_group++;

        int px = comp.baseX;
        int py = comp.baseY;

        if( comp.type == 3 && comp.fill )
        {
            int argb = 0xFF000000 | (comp.color & 0xFFFFFF);
            fill_rect(
                pixels,
                CANVAS_W,
                px,
                py,
                px + comp.baseWidth,
                py + comp.baseHeight,
                argb);
        }

        if( want_sprites && comp.type == 5 && comp.graphic >= 0 )
        {
            struct CacheSpritePack* pack =
                sprite_pack_new_from_cache(cache, comp.graphic);
            if( pack && pack->count > 0 && pack->palette )
            {
                int* spr_px = sprite_get_pixels(&pack->sprites[0], pack->palette, 0);
                if( spr_px )
                {
                    blit_rgba_sprite(
                        pixels,
                        CANVAS_W,
                        px + pack->sprites[0].offset_x,
                        py + pack->sprites[0].offset_y,
                        spr_px,
                        pack->sprites[0].width,
                        pack->sprites[0].height);
                    free(spr_px);
                }
                sprite_pack_free(pack);
            }
            else if( pack )
                sprite_pack_free(pack);
        }

        Component_free(&comp);
    }

    printf(
        "interface %d: files=%d decoded_ok=%d id_group_match=%d -> %s\n",
        iface,
        fl->file_count,
        decoded,
        in_group,
        out_path);

    bmp_write_file(out_path, pixels, CANVAS_W, CANVAS_H);

    free(pixels);
    filelist_free(fl);
    cache_archive_free(arch);
    cache_free(cache);
    return 0;
}
