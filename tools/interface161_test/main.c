/*
 * Load interface archive N from dat2 cache (table CACHE_INTERFACES), decode each
 * file as IF1/IF3 Component, optionally blit type-3/type-5 widgets to a BMP.
 *
 * IF3 widgets use parent-relative coordinates and size/position modes (RuneLite
 * WidgetSizeMode / WidgetPositionMode, 16384ths scaling). IF1 uses absolute
 * baseX/baseY as in the cache. Hidden components are skipped.
 *
 * Usage: interface161_test <cache_directory> [--iface N] [--sprites] [out.bmp]
 */

#include "bmp.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/component.h"
#include "osrs/rscache/tables/sprites.h"

#include <stdint.h>
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

/**
 * Repeat spr (sw x sh) to fill [rect_x, rect_y) + size. Sample coordinates use
 * the same origin as a single-sprite blit: (origin_x, origin_y) maps to spr[0,0].
 */
static void
blit_rgba_sprite_tiled(
    int* dest,
    int dstride,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    const int* spr,
    int sw,
    int sh,
    int origin_x,
    int origin_y)
{
    if( sw <= 0 || sh <= 0 || rect_w <= 0 || rect_h <= 0 )
        return;
    int x0 = rect_x;
    int y0 = rect_y;
    int x1 = rect_x + rect_w;
    int y1 = rect_y + rect_h;
    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > CANVAS_W )
        x1 = CANVAS_W;
    if( y1 > CANVAS_H )
        y1 = CANVAS_H;
    for( int y = y0; y < y1; y++ )
    {
        int sy = y - origin_y;
        sy = ((sy % sh) + sh) % sh;
        for( int x = x0; x < x1; x++ )
        {
            int sx = x - origin_x;
            sx = ((sx % sw) + sw) % sw;
            int p = spr[sy * sw + sx];
            int a = (p >> 24) & 0xFF;
            if( a == 0 )
                continue;
            if( a == 255 )
                dest[y * dstride + x] = p;
            else
            {
                int d = dest[y * dstride + x];
                int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
                int sr = (p >> 16) & 0xFF, sg = (p >> 8) & 0xFF, sb = p & 0xFF;
                int rr = (sr * a + dr * (255 - a)) / 255;
                int rg = (sg * a + dg * (255 - a)) / 255;
                int rb = (sb * a + db * (255 - a)) / 255;
                dest[y * dstride + x] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
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

/** Matches net.runelite.api.widgets.WidgetSizeMode / WidgetPositionMode (16384ths). */
enum
{
    RS_LAYOUT_UNITS = 16384
};

static int
dim_from_parent_mode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return (int)((int64_t)parent_dim * (int64_t)orig / RS_LAYOUT_UNITS);
    default:
        return orig;
    }
}

static int
axis_from_position_mode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + (parent_dim - self_dim) / 2 + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS);
    case 4:
        return parent_origin + (parent_dim - self_dim) / 2 +
               (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS);
    case 5:
        return parent_origin + parent_dim -
               (int)((int64_t)parent_dim * (int64_t)base / RS_LAYOUT_UNITS) - self_dim;
    default:
        return parent_origin + base;
    }
}

/**
 * IF3 components store coordinates relative to their parent; IF1 uses absolute
 * coordinates. Resolve bounds into *out_* (parallel to comps[], length n).
 */
static void
resolve_interface_layout(
    Component* comps,
    int n,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* depth = calloc((size_t)n, sizeof(int));
    int* order = calloc((size_t)n, sizeof(int));
    if( !parent_idx || !depth || !order )
    {
        free(parent_idx);
        free(depth);
        free(order);
        for( int i = 0; i < n; i++ )
        {
            out_x[i] = comps[i].baseX;
            out_y[i] = comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
        }
        return;
    }

    for( int i = 0; i < n; i++ )
    {
        parent_idx[i] = -1;
        if( comps[i].layer < 0 )
            continue;
        for( int j = 0; j < n; j++ )
        {
            if( comps[j].id == comps[i].layer )
            {
                parent_idx[i] = j;
                break;
            }
        }
    }

    for( int i = 0; i < n; i++ )
    {
        int d = 0;
        int cur = i;
        while( parent_idx[cur] >= 0 )
        {
            cur = parent_idx[cur];
            d++;
            if( d > n )
                break;
        }
        depth[i] = d;
    }

    for( int i = 0; i < n; i++ )
        order[i] = i;
    for( int a = 0; a < n; a++ )
    {
        for( int b = a + 1; b < n; b++ )
        {
            if( depth[order[b]] < depth[order[a]] )
            {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }

    for( int k = 0; k < n; k++ )
    {
        int i = order[k];
        if( !comps[i].if3 )
        {
            out_x[i] = comps[i].baseX;
            out_y[i] = comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            continue;
        }

        int px = 0;
        int py = 0;
        int pw = CANVAS_W;
        int ph = CANVAS_H;
        if( parent_idx[i] >= 0 )
        {
            int p = parent_idx[i];
            px = out_x[p];
            py = out_y[p];
            pw = out_w[p];
            ph = out_h[p];
        }

        int w = dim_from_parent_mode(comps[i].widthMode, comps[i].baseWidth, pw);
        int h = dim_from_parent_mode(comps[i].heightMode, comps[i].baseHeight, ph);
        if( w < 0 )
            w = 0;
        if( h < 0 )
            h = 0;

        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = axis_from_position_mode(
            comps[i].xMode, comps[i].baseX, px, pw, w);
        out_y[i] = axis_from_position_mode(
            comps[i].yMode, comps[i].baseY, py, ph, h);
    }

    free(parent_idx);
    free(depth);
    free(order);
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

    int n = fl->file_count;
    Component* comps = calloc((size_t)n, sizeof(Component));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    if( !comps || !lay_x || !lay_y || !lay_w || !lay_h )
    {
        free(comps);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        free(pixels);
        filelist_free(fl);
        cache_archive_free(arch);
        cache_free(cache);
        return 1;
    }

    int decoded = 0;
    int in_group = 0;

    for( int fi = 0; fi < n; fi++ )
    {
        if( decode_component_from_bytes(
                &comps[fi],
                fl->files[fi],
                fl->file_sizes[fi],
                iface,
                fi) != 0 )
        {
            Component_init(&comps[fi]);
            continue;
        }
        decoded++;
        if( (comps[fi].id >> 16) == iface )
            in_group++;
    }

    resolve_interface_layout(comps, n, lay_x, lay_y, lay_w, lay_h);

    for( int fi = 0; fi < n; fi++ )
    {
        Component* comp = &comps[fi];
        if( comp->type < 0 )
            continue;

        int px = lay_x[fi];
        int py = lay_y[fi];
        int lw = lay_w[fi];
        int lh = lay_h[fi];

        if( comp->hidden )
            continue;

        if( comp->type == 3 && comp->fill )
        {
            int argb = 0xFF000000 | (comp->color & 0xFFFFFF);
            fill_rect(
                pixels,
                CANVAS_W,
                px,
                py,
                px + lw,
                py + lh,
                argb);
        }

        if( want_sprites && comp->type == 5 && comp->graphic >= 0 )
        {
            struct CacheSpritePack* pack =
                sprite_pack_new_from_cache(cache, comp->graphic);
            if( pack && pack->count > 0 && pack->palette )
            {
                int* spr_px = sprite_get_pixels(&pack->sprites[0], pack->palette, 0);
                if( spr_px )
                {
                    int ox = px + pack->sprites[0].offset_x;
                    int oy = py + pack->sprites[0].offset_y;
                    int sw = pack->sprites[0].width;
                    int sh = pack->sprites[0].height;
                    if( comp->tiled )
                        blit_rgba_sprite_tiled(
                            pixels,
                            CANVAS_W,
                            px,
                            py,
                            lw,
                            lh,
                            spr_px,
                            sw,
                            sh,
                            ox,
                            oy);
                    else
                        blit_rgba_sprite(
                            pixels, CANVAS_W, ox, oy, spr_px, sw, sh);
                    free(spr_px);
                }
                sprite_pack_free(pack);
            }
            else if( pack )
                sprite_pack_free(pack);
        }
    }

    for( int fi = 0; fi < n; fi++ )
        Component_free(&comps[fi]);
    free(comps);
    free(lay_x);
    free(lay_y);
    free(lay_w);
    free(lay_h);

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
