/*
 * Load interface archive N from dat2 cache (table RSCacheDat2Disk_Table_Interfaces), decode each
 * file as IF1/IF3 Component, optionally blit widgets to a BMP.
 *
 * Draws type 2 (inventory slot backgrounds), type 3 (rect fill/outline), type 5 (sprites).
 * Use --fixture for sample worn items on equipment slot widgets (type 0 file indices).
 *
 * Usage: interface161_test <cache_directory> [--iface N] [--sprites]
 *          [--fixture path.json] [--panel] [--root-w W] [--root-h H]
 *          [--mount childFileIndex:ifaceId] ... [out.bmp]
 */

#include "bmp.h"
#include "fixture.h"
#include "ui/ui_if3_layout.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "toridraw/toridraw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

enum
{
    CANVAS_W = 1024,
    CANVAS_H = 768,
    FIXED_MODE_ROOT_W = 765,
    FIXED_MODE_ROOT_H = 503,
    MAX_MOUNTS = 32,
    INV_SLOT_PITCH = 32
};

struct MountSpec
{
    int child_file_index;
    int iface_id;
};

struct DrawContext
{
    struct RSCacheDat2Disk* cache;
    struct ToriDraw_Scene* scene;
    struct Interface161Fixture const* fixture;
    struct Interface161ObjIconCache** obj_icon_cache;
    int* pixels;
    int want_sprites;
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
draw_rect_outline(
    int* px,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb)
{
    if( x1 <= x0 || y1 <= y0 )
        return;
    fill_rect(px, stride, x0, y0, x1, y0 + 1, argb);
    fill_rect(px, stride, x0, y1 - 1, x1, y1, argb);
    fill_rect(px, stride, x0, y0, x0 + 1, y1, argb);
    fill_rect(px, stride, x1 - 1, y0, x1, y1, argb);
}

static void
blit_rgba_pixel(
    int* dest,
    int dstride,
    int sx,
    int sy,
    int p,
    int trans_scale)
{
    if( sx < 0 || sy < 0 || sx >= CANVAS_W || sy >= CANVAS_H )
        return;

    int a = (p >> 24) & 0xFF;
    if( trans_scale < 256 )
        a = (a * trans_scale) / 256;
    if( a == 0 )
        return;

    if( a == 255 )
    {
        dest[sy * dstride + sx] = (p & 0x00FFFFFF) | 0xFF000000;
        return;
    }

    int d = dest[sy * dstride + sx];
    int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    int sr = (p >> 16) & 0xFF, sg = (p >> 8) & 0xFF, sb = p & 0xFF;
    int rr = (sr * a + dr * (255 - a)) / 255;
    int rg = (sg * a + dg * (255 - a)) / 255;
    int rb = (sb * a + db * (255 - a)) / 255;
    dest[sy * dstride + sx] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

static void
blit_rgba_sprite(
    int* dest,
    int dstride,
    int dx,
    int dy,
    const int* spr,
    int sw,
    int sh,
    int trans_scale)
{
    for( int y = 0; y < sh; y++ )
    {
        int sy = dy + y;
        for( int x = 0; x < sw; x++ )
            blit_rgba_pixel(dest, dstride, dx + x, sy, spr[y * sw + x], trans_scale);
    }
}

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
    int origin_y,
    int trans_scale)
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
            blit_rgba_pixel(dest, dstride, x, y, spr[sy * sw + sx], trans_scale);
        }
    }
}

static void
blit_sprite_from_cache(
    struct DrawContext* ctx,
    int graphic_id,
    int dx,
    int dy,
    int lw,
    int lh,
    bool tiled,
    int trans_scale)
{
    if( !ctx->want_sprites || graphic_id < 0 )
        return;

    struct RSCacheDat2A_SpritePack* pack =
        RSCacheDat2A_SpritePackNewFromCache(ctx->cache, graphic_id);
    if( !pack || pack->count <= 0 || !pack->palette )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return;
    }

    int* spr_px = RSCacheDat2A_SpriteGetPixels(&pack->sprites[0], pack->palette, 0);
    if( spr_px )
    {
        int ox = dx + pack->sprites[0].offset_x;
        int oy = dy + pack->sprites[0].offset_y;
        int sw = pack->sprites[0].width;
        int sh = pack->sprites[0].height;
        if( tiled )
        {
            blit_rgba_sprite_tiled(
                ctx->pixels, CANVAS_W, dx, dy, lw, lh, spr_px, sw, sh, ox, oy, trans_scale);
        }
        else
        {
            blit_rgba_sprite(ctx->pixels, CANVAS_W, ox, oy, spr_px, sw, sh, trans_scale);
        }
        free(spr_px);
    }
    RSCacheDat2A_SpritePackFree(pack);
}

static void
blit_obj_icon_centered(
    struct DrawContext* ctx,
    int obj_id,
    int cx,
    int cy,
    int box_w,
    int box_h)
{
    if( obj_id < 0 || !ctx->scene || !ctx->obj_icon_cache )
        return;

    const int* icon =
        interface161_obj_icon_get(ctx->cache, ctx->scene, ctx->obj_icon_cache, obj_id);
    if( !icon )
        return;

    int dx = cx + (box_w - INV_SLOT_PITCH) / 2;
    int dy = cy + (box_h - INV_SLOT_PITCH) / 2;
    blit_rgba_sprite(ctx->pixels, CANVAS_W, dx, dy, icon, INV_SLOT_PITCH, INV_SLOT_PITCH, 256);
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
    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (int8_t*)data, size);
    Component_init(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

static void
resolve_interface_layout(
    Component* comps,
    int n,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h,
    int* out_order)
{
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* depth = calloc((size_t)n, sizeof(int));
    if( !parent_idx || !depth )
    {
        free(parent_idx);
        free(depth);
        for( int i = 0; i < n; i++ )
        {
            out_x[i] = root_x + comps[i].baseX;
            out_y[i] = root_y + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            if( out_order )
                out_order[i] = i;
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

    if( out_order )
    {
        for( int i = 0; i < n; i++ )
            out_order[i] = i;
        for( int a = 0; a < n; a++ )
        {
            for( int b = a + 1; b < n; b++ )
            {
                if( depth[out_order[b]] < depth[out_order[a]] )
                {
                    int t = out_order[a];
                    out_order[a] = out_order[b];
                    out_order[b] = t;
                }
            }
        }
    }

    for( int k = 0; k < n; k++ )
    {
        int i = out_order ? out_order[k] : k;

        int px = root_x;
        int py = root_y;
        int pw = root_w;
        int ph = root_h;
        if( parent_idx[i] >= 0 )
        {
            int p = parent_idx[i];
            px = out_x[p];
            py = out_y[p];
            pw = out_w[p];
            ph = out_h[p];
        }

        if( !comps[i].if3 )
        {
            out_x[i] = px + comps[i].baseX;
            out_y[i] = py + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            continue;
        }

        int rel_x = 0;
        int rel_y = 0;
        int w = 0;
        int h = 0;
        ui_if3_dat2_component_parent_relative_layout(
            &comps[i], pw, ph, &rel_x, &rel_y, &w, &h);
        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = px + rel_x;
        out_y[i] = py + rel_y;
    }

    free(parent_idx);
    free(depth);
}

static void
draw_component_inv(
    struct DrawContext* ctx,
    Component* comp,
    int fi,
    int px,
    int py,
    int lw,
    int lh)
{
    (void)lh;
    int cols = comp->baseWidth > 0 ? comp->baseWidth : lw;
    int rows = comp->baseHeight > 0 ? comp->baseHeight : 1;
    int margin_x = comp->marginX;
    int margin_y = comp->marginY;
    int fixture_obj = ctx->fixture ? interface161_fixture_obj_for_file(ctx->fixture, fi) : -1;

    int slot = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++ )
        {
            int slot_x = px + col * (margin_x + INV_SLOT_PITCH);
            int slot_y = py + row * (margin_y + INV_SLOT_PITCH);
            if( slot < 20 && comp->invSlotOffsetX && comp->invSlotOffsetY )
            {
                slot_x += comp->invSlotOffsetX[slot];
                slot_y += comp->invSlotOffsetY[slot];
            }

            if( fixture_obj >= 0 )
            {
                blit_obj_icon_centered(
                    ctx, fixture_obj, slot_x, slot_y, INV_SLOT_PITCH, INV_SLOT_PITCH);
            }
            else if( comp->invSlotGraphicId && slot < 20 && comp->invSlotGraphicId[slot] >= 0 )
            {
                blit_sprite_from_cache(
                    ctx, comp->invSlotGraphicId[slot], slot_x, slot_y, 0, 0, false, 256);
            }

            slot++;
        }
    }
}

static void
draw_one_component(
    struct DrawContext* ctx,
    Component* comp,
    int fi,
    int px,
    int py,
    int lw,
    int lh)
{
    int trans_scale = 256 - (comp->transparency & 0xFF);
    if( trans_scale < 0 )
        trans_scale = 0;

    if( comp->type == 3 )
    {
        int argb = 0xFF000000 | (comp->color & 0xFFFFFF);
        if( comp->fill )
            fill_rect(ctx->pixels, CANVAS_W, px, py, px + lw, py + lh, argb);
        else
            draw_rect_outline(ctx->pixels, CANVAS_W, px, py, px + lw, py + lh, argb);
    }

    if( comp->type == 2 )
        draw_component_inv(ctx, comp, fi, px, py, lw, lh);

    if( comp->type == 5 && comp->graphic >= 0 )
    {
        blit_sprite_from_cache(
            ctx, comp->graphic, px, py, lw, lh, comp->tiled, trans_scale);
    }

    if( ctx->fixture )
    {
        int fixture_obj = interface161_fixture_obj_for_file(ctx->fixture, fi);
        if( fixture_obj >= 0 && comp->type == 0 )
            blit_obj_icon_centered(ctx, fixture_obj, px, py, lw, lh);
    }
}

static void
draw_interface_components(
    struct DrawContext* ctx,
    Component* comps,
    int n,
    const int* lay_x,
    const int* lay_y,
    const int* lay_w,
    const int* lay_h,
    const int* draw_order)
{
    for( int k = 0; k < n; k++ )
    {
        int fi = draw_order ? draw_order[k] : k;
        Component* comp = &comps[fi];
        if( comp->type < 0 || comp->hidden )
            continue;

        draw_one_component(ctx, comp, fi, lay_x[fi], lay_y[fi], lay_w[fi], lay_h[fi]);
    }
}

static int
render_mounted_interface(
    struct RSCacheDat2Disk* cache,
    struct ToriDraw_Scene* scene,
    struct Interface161Fixture const* fixture,
    struct Interface161ObjIconCache** obj_icon_cache,
    int iface_id,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* pixels,
    int want_sprites)
{
    if( root_w <= 0 || root_h <= 0 )
        return 0;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, iface_id);
    if( !arch )
    {
        fprintf(stderr, "mount: failed to load interface archive %d\n", iface_id);
        return -1;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    int n = fl->file_count;
    Component* comps = calloc((size_t)n, sizeof(Component));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    int* draw_order = calloc((size_t)n, sizeof(int));
    if( !comps || !lay_x || !lay_y || !lay_w || !lay_h || !draw_order )
    {
        free(comps);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        free(draw_order);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    for( int fi = 0; fi < n; fi++ )
    {
        if( decode_component_from_bytes(
                &comps[fi], fl->files[fi], fl->file_sizes[fi], iface_id, fi) != 0 )
            Component_init(&comps[fi]);
    }

    resolve_interface_layout(
        comps, n, root_x, root_y, root_w, root_h, lay_x, lay_y, lay_w, lay_h, draw_order);

    struct DrawContext dctx = {
        .cache = cache,
        .scene = scene,
        .fixture = fixture,
        .obj_icon_cache = obj_icon_cache,
        .pixels = pixels,
        .want_sprites = want_sprites,
    };
    draw_interface_components(&dctx, comps, n, lay_x, lay_y, lay_w, lay_h, draw_order);

    for( int fi = 0; fi < n; fi++ )
        Component_free(&comps[fi]);
    free(comps);
    free(lay_x);
    free(lay_y);
    free(lay_w);
    free(lay_h);
    free(draw_order);
    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return 0;
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: interface161_test <cache_directory> [--iface N] [--sprites]\n"
        "          [--fixture path.json] [--panel] [--root-w W] [--root-h H]\n"
        "          [--mount childFileIndex:ifaceId] ... [out.bmp]\n"
        "  --panel       use sidebar panel root size (%dx%d)\n"
        "  --root-w/h    IF3 virtual parent size (default %dx%d)\n",
        UITREE_SIDEBAR_PANEL_W,
        UITREE_SIDEBAR_PANEL_H,
        FIXED_MODE_ROOT_W,
        FIXED_MODE_ROOT_H);
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    const char* out_path = "interface161_out.bmp";
    const char* fixture_path = NULL;
    int iface = 161;
    int want_sprites = 0;
    int root_w = FIXED_MODE_ROOT_W;
    int root_h = FIXED_MODE_ROOT_H;
    int mount_count = 0;
    struct MountSpec mounts[MAX_MOUNTS];

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iface") == 0 && i + 1 < argc )
            iface = atoi(argv[++i]);
        else if( strcmp(argv[i], "--sprites") == 0 )
            want_sprites = 1;
        else if( strcmp(argv[i], "--fixture") == 0 && i + 1 < argc )
            fixture_path = argv[++i];
        else if( strcmp(argv[i], "--panel") == 0 )
        {
            root_w = UITREE_SIDEBAR_PANEL_W;
            root_h = UITREE_SIDEBAR_PANEL_H;
        }
        else if( strcmp(argv[i], "--root-w") == 0 && i + 1 < argc )
            root_w = atoi(argv[++i]);
        else if( strcmp(argv[i], "--root-h") == 0 && i + 1 < argc )
            root_h = atoi(argv[++i]);
        else if( strcmp(argv[i], "--mount") == 0 && i + 1 < argc )
        {
            const char* s = argv[++i];
            char* colon = strchr(s, ':');
            if( colon && mount_count < MAX_MOUNTS )
            {
                mounts[mount_count].child_file_index = atoi(s);
                mounts[mount_count].iface_id = atoi(colon + 1);
                mount_count++;
            }
        }
        else if( !cache_dir )
            cache_dir = argv[i];
        else
            out_path = argv[i];
    }

    if( !cache_dir )
    {
        usage();
        return 1;
    }

    struct Interface161Fixture fixture;
    interface161_fixture_init(&fixture);
    if( fixture_path && interface161_fixture_load(&fixture, fixture_path) != 0 )
        fprintf(stderr, "warning: could not load fixture %s\n", fixture_path);

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", cache_dir);
        interface161_fixture_free(&fixture);
        return 1;
    }

    struct ToriDraw_Scene* scene = NULL;
    struct Interface161ObjIconCache* obj_icon_cache = NULL;
    if( fixture.slot_count > 0 )
    {
        ToriDraw_Init();
        scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL);
        if( !scene )
            fprintf(stderr, "warning: ToriDraw_SceneNew failed; fixture objs skipped\n");
    }

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, iface);
    if( !arch )
    {
        fprintf(stderr, "failed to load interface archive %d\n", iface);
        interface161_obj_icon_cache_free(obj_icon_cache);
        if( scene )
            ToriDraw_SceneFree(scene);
        RSCacheDat2Disk_Free(cache);
        interface161_fixture_free(&fixture);
        return 1;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        interface161_obj_icon_cache_free(obj_icon_cache);
        if( scene )
            ToriDraw_SceneFree(scene);
        RSCacheDat2Disk_Free(cache);
        interface161_fixture_free(&fixture);
        return 1;
    }

    int* pixels = calloc((size_t)(CANVAS_W * CANVAS_H), sizeof(int));
    if( !pixels )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        interface161_obj_icon_cache_free(obj_icon_cache);
        if( scene )
            ToriDraw_SceneFree(scene);
        RSCacheDat2Disk_Free(cache);
        interface161_fixture_free(&fixture);
        return 1;
    }
    fill_rect(pixels, CANVAS_W, 0, 0, CANVAS_W, CANVAS_H, 0xFF202428);

    int n = fl->file_count;
    Component* comps = calloc((size_t)n, sizeof(Component));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    int* draw_order = calloc((size_t)n, sizeof(int));
    if( !comps || !lay_x || !lay_y || !lay_w || !lay_h || !draw_order )
    {
        free(comps);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        free(draw_order);
        free(pixels);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        interface161_obj_icon_cache_free(obj_icon_cache);
        if( scene )
            ToriDraw_SceneFree(scene);
        RSCacheDat2Disk_Free(cache);
        interface161_fixture_free(&fixture);
        return 1;
    }

    int decoded = 0;
    int in_group = 0;
    for( int fi = 0; fi < n; fi++ )
    {
        if( decode_component_from_bytes(&comps[fi], fl->files[fi], fl->file_sizes[fi], iface, fi) ==
            0 )
        {
            decoded++;
            if( (comps[fi].id >> 16) == iface )
                in_group++;
        }
        else
            Component_init(&comps[fi]);
    }

    resolve_interface_layout(
        comps,
        n,
        0,
        0,
        root_w,
        root_h,
        lay_x,
        lay_y,
        lay_w,
        lay_h,
        draw_order);

    printf(
        "interface %d: root=%dx%d files=%d decoded_ok=%d id_group_match=%d -> %s\n",
        iface,
        root_w,
        root_h,
        fl->file_count,
        decoded,
        in_group,
        out_path);

    struct DrawContext dctx = {
        .cache = cache,
        .scene = scene,
        .fixture = fixture.slot_count > 0 ? &fixture : NULL,
        .obj_icon_cache = &obj_icon_cache,
        .pixels = pixels,
        .want_sprites = want_sprites,
    };
    draw_interface_components(&dctx, comps, n, lay_x, lay_y, lay_w, lay_h, draw_order);

    for( int mi = 0; mi < mount_count; mi++ )
    {
        int cf = mounts[mi].child_file_index;
        if( cf < 0 || cf >= n )
            continue;
        render_mounted_interface(
            cache,
            scene,
            fixture.slot_count > 0 ? &fixture : NULL,
            &obj_icon_cache,
            mounts[mi].iface_id,
            lay_x[cf],
            lay_y[cf],
            lay_w[cf],
            lay_h[cf],
            pixels,
            want_sprites);
    }

    bmp_write_file(out_path, pixels, CANVAS_W, CANVAS_H);

    for( int fi = 0; fi < n; fi++ )
        Component_free(&comps[fi]);
    free(comps);
    free(lay_x);
    free(lay_y);
    free(lay_w);
    free(lay_h);
    free(draw_order);
    free(pixels);
    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    if( scene )
        ToriDraw_SceneFree(scene);
    interface161_obj_icon_cache_free(obj_icon_cache);
    RSCacheDat2Disk_Free(cache);
    interface161_fixture_free(&fixture);
    return 0;
}
