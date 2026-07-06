/*
 * Load interface archive N from dat2 cache (table RSCacheDat2Disk_Table_Interfaces), decode each
 * file as IF1/IF3 Component, optionally blit widgets to a BMP.
 *
 * Draws type 2 (inventory slot backgrounds), type 3 (rect fill/outline), type 5 (sprites).
 * Use --fixture for sample worn items on equipment slot widgets (type 0 file indices).
 *
 * Usage: interface161_test <cache_directory> [--iface N] [--sprites]
 *          [--fixture path.json] [--panel] [--root-w W] [--root-h H]
 *          [--mount childFileIndex:ifaceId] ... [--verbose-layout] [out.bmp]
 */

#include "bmp.h"
#include "cs2_runner.h"
#include "fixture.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_model_sprite.h"
#include "toridraw_cachemodel.h"
#include "ui/ui_if3_layout.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    CANVAS_W = 1024,
    CANVAS_H = 768,
    FIXED_MODE_ROOT_W = 765,
    FIXED_MODE_ROOT_H = 503,
    MAX_MOUNTS = 32,
    INV_SLOT_PITCH = 32,
    INTERFACE161_FONT_CACHE_MAX = 32,
    DAT2_FONT_METRICS_SIZE = 257
};

static int verbose_layout = 0;

struct MountSpec
{
    int child_file_index;
    int iface_id;
};

/** Fixed-mode gameframe (archive 165) sidebar mount slots are 1×1 logical layers.
 *  rev_kronos_ui.ini places all sidebar_tab_* panels at this rect on fixed_shell. */
enum
{
    GAMEFRAME_165_SIDEBAR_X = 547,
    GAMEFRAME_165_SIDEBAR_Y = 205,
    GAMEFRAME_165_SIDEBAR_CHILD_MIN = 8,
    GAMEFRAME_165_SIDEBAR_CHILD_MAX = 21
};

static void
resolve_mount_rect(
    int parent_iface,
    int child_file,
    int lay_x,
    int lay_y,
    int lay_w,
    int lay_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    *out_x = lay_x;
    *out_y = lay_y;
    *out_w = lay_w;
    *out_h = lay_h;

    if( parent_iface == 165 && child_file >= GAMEFRAME_165_SIDEBAR_CHILD_MIN &&
        child_file <= GAMEFRAME_165_SIDEBAR_CHILD_MAX )
    {
        *out_x = GAMEFRAME_165_SIDEBAR_X;
        *out_y = GAMEFRAME_165_SIDEBAR_Y;
        *out_w = UITREE_SIDEBAR_PANEL_W;
        *out_h = UITREE_SIDEBAR_PANEL_H;
        return;
    }

    if( lay_w <= 1 )
        *out_w = UITREE_SIDEBAR_PANEL_W;
    if( lay_h <= 1 )
        *out_h = UITREE_SIDEBAR_PANEL_H;
}

struct DrawContext
{
    struct RSCacheDat2Disk* cache;
    struct ToriDraw_Scene* scene;
    struct Interface161Fixture const* fixture;
    struct Interface161ObjIconCache** obj_icon_cache;
    struct Interface161ModelSpriteCache** model_sprite_cache;
    struct Interface161FontCache* font_cache;
    int* pixels;
    int want_sprites;
};

struct Interface161ModelSpriteCache
{
    int model_id;
    int zoom;
    int xan;
    int yan;
    int width;
    int height;
    int* pixels;
    struct Interface161ModelSpriteCache* next;
};

struct Interface161FontCache
{
    int font_ids[INTERFACE161_FONT_CACHE_MAX];
    struct ToriDraw_Font* fonts[INTERFACE161_FONT_CACHE_MAX];
    int count;
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

struct SpriteChromeOpts
{
    int outline;
    int graphic_shadow;
    bool flip_h;
    bool flip_v;
};

static bool
sprite_pixel_opaque(
    int p)
{
    return ((p >> 24) & 0xFF) != 0;
}

static void
flip_sprite_h(
    int* px,
    int sw,
    int sh)
{
    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw / 2; x++ )
        {
            int l = y * sw + x;
            int r = y * sw + (sw - 1 - x);
            int t = px[l];
            px[l] = px[r];
            px[r] = t;
        }
    }
}

static void
flip_sprite_v(
    int* px,
    int sw,
    int sh)
{
    for( int y = 0; y < sh / 2; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            int t = y * sw + x;
            int b = (sh - 1 - y) * sw + x;
            int tmp = px[t];
            px[t] = px[b];
            px[b] = tmp;
        }
    }
}

static int*
apply_sprite_outline(
    int const* src,
    int sw,
    int sh,
    int outline,
    int* out_w,
    int* out_h)
{
    if( outline <= 0 || !src )
    {
        *out_w = sw;
        *out_h = sh;
        return NULL;
    }

    int pad = outline;
    int dw = sw + pad * 2;
    int dh = sh + pad * 2;
    int* dst = calloc((size_t)dw * (size_t)dh, sizeof(int));
    if( !dst )
        return NULL;

    for( int y = 0; y < dh; y++ )
    {
        for( int x = 0; x < dw; x++ )
        {
            int sx = x - pad;
            int sy = y - pad;
            if( sx >= 0 && sx < sw && sy >= 0 && sy < sh && sprite_pixel_opaque(src[sy * sw + sx]) )
                dst[y * dw + x] = src[sy * sw + sx];
        }
    }

    for( int pass = 0; pass < (outline >= 2 ? 2 : 1); pass++ )
    {
        int outline_rgb = pass == 0 ? 0xFF000000 : 0xFFFFFFFF;
        int* next = calloc((size_t)dw * (size_t)dh, sizeof(int));
        if( !next )
            break;
        memcpy(next, dst, (size_t)dw * (size_t)dh * sizeof(int));

        for( int y = 0; y < dh; y++ )
        {
            for( int x = 0; x < dw; x++ )
            {
                if( sprite_pixel_opaque(dst[y * dw + x]) )
                    continue;
                bool neighbor = false;
                for( int oy = -1; oy <= 1 && !neighbor; oy++ )
                {
                    for( int ox = -1; ox <= 1; ox++ )
                    {
                        if( ox == 0 && oy == 0 )
                            continue;
                        int nx = x + ox;
                        int ny = y + oy;
                        if( nx < 0 || ny < 0 || nx >= dw || ny >= dh )
                            continue;
                        if( sprite_pixel_opaque(dst[ny * dw + nx]) )
                        {
                            neighbor = true;
                            break;
                        }
                    }
                }
                if( neighbor )
                    next[y * dw + x] = outline_rgb;
            }
        }
        free(dst);
        dst = next;
    }

    *out_w = dw;
    *out_h = dh;
    return dst;
}

static int*
apply_sprite_shadow(
    int const* src,
    int sw,
    int sh,
    int shadow_colour,
    int* out_w,
    int* out_h,
    int* out_ox,
    int* out_oy)
{
    if( shadow_colour == 0 || !src )
        return NULL;

    int pad = 2;
    int dw = sw + pad;
    int dh = sh + pad;
    int* dst = calloc((size_t)dw * (size_t)dh, sizeof(int));
    if( !dst )
        return NULL;

    int sr = (shadow_colour >> 16) & 0xFF;
    int sg = (shadow_colour >> 8) & 0xFF;
    int sb = shadow_colour & 0xFF;
    int shadow_px = 0xFF000000 | (sr << 16) | (sg << 8) | sb;

    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            if( !sprite_pixel_opaque(src[y * sw + x]) )
                continue;
            int dx = x + 1;
            int dy = y + 1;
            if( dx < dw && dy < dh )
                dst[dy * dw + dx] = shadow_px;
        }
    }

    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            int p = src[y * sw + x];
            if( sprite_pixel_opaque(p) )
                dst[y * dw + x] = p;
        }
    }

    *out_w = dw;
    *out_h = dh;
    *out_ox = 0;
    *out_oy = 0;
    return dst;
}

static int*
load_sprite_rgba(
    struct DrawContext* ctx,
    int graphic_id,
    struct SpriteChromeOpts const* chrome,
    int* out_w,
    int* out_h,
    int* out_ox,
    int* out_oy)
{
    *out_w = 0;
    *out_h = 0;
    *out_ox = 0;
    *out_oy = 0;
    if( !ctx->want_sprites || graphic_id < 0 )
        return NULL;

    struct RSCacheDat2A_SpritePack* pack =
        RSCacheDat2A_SpritePackNewFromCache(ctx->cache, graphic_id);
    if( !pack || pack->count <= 0 || !pack->palette )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    int* spr_px = RSCacheDat2A_SpriteGetPixels(&pack->sprites[0], pack->palette, 0);
    int sw = pack->sprites[0].width;
    int sh = pack->sprites[0].height;
    int ox = pack->sprites[0].offset_x;
    int oy = pack->sprites[0].offset_y;
    RSCacheDat2A_SpritePackFree(pack);
    if( !spr_px )
        return NULL;

    struct SpriteChromeOpts empty = { 0 };
    if( !chrome )
        chrome = &empty;

    if( chrome->flip_h )
        flip_sprite_h(spr_px, sw, sh);
    if( chrome->flip_v )
        flip_sprite_v(spr_px, sw, sh);

    int* working = spr_px;
    if( chrome->outline > 0 )
    {
        int ow = 0, oh = 0;
        int* outlined = apply_sprite_outline(working, sw, sh, chrome->outline, &ow, &oh);
        if( outlined )
        {
            if( working != spr_px )
                free(working);
            working = outlined;
            ox -= chrome->outline;
            oy -= chrome->outline;
            sw = ow;
            sh = oh;
        }
    }

    if( chrome->graphic_shadow != 0 )
    {
        int sw2 = 0, sh2 = 0, ox2 = 0, oy2 = 0;
        int* shadowed =
            apply_sprite_shadow(working, sw, sh, chrome->graphic_shadow, &sw2, &sh2, &ox2, &oy2);
        if( shadowed )
        {
            if( working != spr_px )
                free(working);
            working = shadowed;
            sw = sw2;
            sh = sh2;
        }
    }

    *out_w = sw;
    *out_h = sh;
    *out_ox = ox;
    *out_oy = oy;
    if( working != spr_px )
        free(spr_px);
    return working;
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
    int trans_scale,
    struct SpriteChromeOpts const* chrome)
{
    if( !ctx->want_sprites || graphic_id < 0 )
        return;

    int sw = 0, sh = 0, ox = 0, oy = 0;
    int* spr_px = load_sprite_rgba(ctx, graphic_id, chrome, &sw, &sh, &ox, &oy);
    if( !spr_px )
    {
        if( verbose_layout && graphic_id >= 0 )
            fprintf(stderr, "sprite %d: load failed\n", graphic_id);
        return;
    }

    ox = dx + ox;
    oy = dy + oy;
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

static void
interface161_model_sprite_cache_free(struct Interface161ModelSpriteCache* cache)
{
    while( cache )
    {
        struct Interface161ModelSpriteCache* next = cache->next;
        free(cache->pixels);
        free(cache);
        cache = next;
    }
}

static struct ToriDraw_Font*
interface161_font_new_from_dat2(
    struct RSCacheDat2Disk* cache,
    int font_id)
{
    if( !cache || font_id < 0 )
        return NULL;

    struct RSCacheDat2Disk_Archive* font_archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Fonts, font_id);
    if( !font_archive )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, font_archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(font_archive);
    RSCacheDat2Disk_ArchiveFree(font_archive);
    if( !fl || fl->file_count <= 0 || fl->file_sizes[0] < DAT2_FONT_METRICS_SIZE )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    uint8_t metrics[DAT2_FONT_METRICS_SIZE];
    memcpy(metrics, fl->files[0], DAT2_FONT_METRICS_SIZE);
    RSCacheShared_FileListFree(fl);

    struct RSCacheDat2Disk_Archive* sprite_archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Sprites, font_id);
    if( !sprite_archive )
        return NULL;

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewDecode(
        (unsigned char const*)sprite_archive->data,
        sprite_archive->data_size,
        SPRITELOAD_FLAG_NONE);
    RSCacheDat2Disk_ArchiveFree(sprite_archive);
    if( !pack || pack->count <= 0 )
    {
        RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
    {
        RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    ToriDraw_FontInitCharcodeset(font);
    font->line_height = metrics[256] & 0xFF;

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        uint16_t const codepoint = ToriDraw_FontCharsetTable()[gi];
        int const cp = (int)codepoint;
        if( cp < 0 || cp >= pack->count )
            continue;

        struct RSCacheDat2A_Sprite const* sprite = &pack->sprites[cp];
        int const gw = sprite->crop_width;
        int const gh = sprite->crop_height;
        if( gw <= 0 || gh <= 0 || !sprite->pixel_alphas )
            continue;

        size_t const len = (size_t)gw * (size_t)gh;
        font->glyph_alpha[gi] = malloc(len);
        if( !font->glyph_alpha[gi] )
            goto font_fail;

        for( size_t j = 0; j < len; j++ )
            font->glyph_alpha[gi][j] = sprite->pixel_alphas[j] != 0 ? (uint8_t)255 : (uint8_t)0;

        font->glyph_width[gi] = gw;
        font->glyph_height[gi] = gh;
        font->offset_x[gi] = sprite->offset_x;
        font->offset_y[gi] = sprite->offset_y;
        font->advance[gi] = metrics[cp] & 0xFF;
        if( font->advance[gi] <= 0 )
            font->advance[gi] = gw > 0 ? gw + 2 : 4;
    }

    if( font->line_height <= 0 )
    {
        for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
        {
            if( font->glyph_height[i] > font->line_height )
                font->line_height = font->glyph_height[i];
        }
    }

    ToriDraw_FontFinishDrawWidths(font);
    RSCacheDat2A_SpritePackFree(pack);

    if( !ToriDraw_FontValidate(font) )
        goto font_fail;
    return font;

font_fail:
    ToriDraw_FontFree(font);
    return NULL;
}

static struct ToriDraw_Font*
interface161_font_get(
    struct DrawContext* ctx,
    struct Interface161FontCache* font_cache,
    int font_id)
{
    if( !ctx || !ctx->cache || !font_cache || font_id < 0 )
        return NULL;

    for( int i = 0; i < font_cache->count; i++ )
    {
        if( font_cache->font_ids[i] == font_id )
            return font_cache->fonts[i];
    }

    if( font_cache->count >= INTERFACE161_FONT_CACHE_MAX )
        return NULL;

    struct ToriDraw_Font* font = interface161_font_new_from_dat2(ctx->cache, font_id);
    if( !font )
        return NULL;

    int idx = font_cache->count++;
    font_cache->font_ids[idx] = font_id;
    font_cache->fonts[idx] = font;
    return font;
}

static void
interface161_font_cache_free(struct Interface161FontCache* font_cache)
{
    if( !font_cache )
        return;
    for( int i = 0; i < font_cache->count; i++ )
        ToriDraw_FontFree(font_cache->fonts[i]);
    memset(font_cache, 0, sizeof(*font_cache));
}

static void
draw_text_component(
    struct DrawContext* ctx,
    struct Interface161FontCache* font_cache,
    int font_id,
    char const* text,
    int color,
    int center,
    int shadowed,
    int px,
    int py,
    int lw,
    int lh)
{
    if( !ctx || !text || !text[0] || !ctx->want_sprites )
        return;

    if( font_id < 0 )
        font_id = 495;

    struct ToriDraw_Font* font = interface161_font_get(ctx, font_cache, font_id);
    if( !font )
        return;

    struct ToriDraw_ViewPort view_port = {
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = CANVAS_W,
        .clip_bottom = CANVAS_H,
        .stride = CANVAS_W,
    };

    int tx = px;
    int ty = py + lh;
    if( center && lw > 0 )
    {
        int tw = ToriDraw2D_MeasureString(font, text);
        tx = px + (lw - tw) / 2;
    }

    int rgb = color & 0xFFFFFF;
    (void)ToriDraw2D_DrawString(
        font, &view_port, tx, ty, text, rgb, center != 0, shadowed != 0, ctx->pixels);
}

static int*
interface161_model_sprite_get(
    struct DrawContext* ctx,
    int model_id,
    int zoom,
    int xan,
    int yan,
    int width,
    int height)
{
    if( !ctx || !ctx->scene || model_id <= 0 || width <= 0 || height <= 0 )
        return NULL;

    struct Interface161ModelSpriteCache** head = ctx->model_sprite_cache;
    if( !head )
        return NULL;

    for( struct Interface161ModelSpriteCache* it = *head; it; it = it->next )
    {
        if( it->model_id == model_id && it->zoom == zoom && it->xan == xan && it->yan == yan &&
            it->width == width && it->height == height )
            return it->pixels;
    }

    struct RSCacheDat2A_Model* raw = RSCacheDat2A_ModelNewFromCache(ctx->cache, model_id);
    if( !raw )
        return NULL;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    RSCacheDat2A_ModelFree(raw);
    if( !copy )
        return NULL;

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
        return NULL;

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, 0, 0);

    if( zoom <= 0 )
        zoom = 100;

    struct ToriDraw_Sprite* spr =
        ToriDraw_SpriteNewFromModelRaster(ctx->scene, hnd, zoom, xan, yan, width, height, false);
    if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
    {
        ToriDraw_SceneFrameEnd(ctx->scene);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }

    int pixel_count = spr->width * spr->height;
    int* pixels = malloc((size_t)pixel_count * sizeof(int));
    if( !pixels )
    {
        ToriDraw_SceneFrameEnd(ctx->scene);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }

    for( int i = 0; i < pixel_count; i++ )
        pixels[i] = (int)spr->pixels_argb[i];

    ToriDraw_SpriteFree(spr);
    ToriDraw_SceneFrameEnd(ctx->scene);

    struct Interface161ModelSpriteCache* entry = calloc(1, sizeof(*entry));
    if( !entry )
    {
        free(pixels);
        return NULL;
    }

    entry->model_id = model_id;
    entry->zoom = zoom;
    entry->xan = xan;
    entry->yan = yan;
    entry->width = width;
    entry->height = height;
    entry->pixels = pixels;
    entry->next = *head;
    *head = entry;
    return pixels;
}

static void
draw_line_component(
    int* pixels,
    int color,
    int line_width,
    bool horizontal,
    int px,
    int py,
    int lw,
    int lh)
{
    if( !pixels || color == 0 )
        return;

    int argb = 0xFF000000 | (color & 0xFFFFFF);
    if( horizontal )
    {
        int lh_draw = line_width > 0 ? line_width : 1;
        fill_rect(pixels, CANVAS_W, px, py + (lh - lh_draw) / 2, px + lw, py + (lh - lh_draw) / 2 + lh_draw, argb);
    }
    else
    {
        int lw_draw = line_width > 0 ? line_width : 1;
        fill_rect(pixels, CANVAS_W, px + (lw - lw_draw) / 2, py, px + (lw - lw_draw) / 2 + lw_draw, py + lh, argb);
    }
}

static int
node_item_obj_id(struct StaticUIComponent const* node)
{
    if( node->item_id > 0 )
        return node->item_id;
    if( node->type == UIELEM_CC_OBJ && node->u.cc_obj.obj_id > 0 )
        return node->u.cc_obj.obj_id;
    return 0;
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
layout_one_component(
    Component* comp,
    int px,
    int py,
    int pw,
    int ph,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    if( !comp->if3 )
    {
        *out_x = px + comp->baseX;
        *out_y = py + comp->baseY;
        *out_w = comp->baseWidth;
        *out_h = comp->baseHeight;
        return;
    }

    int rel_x = 0;
    int rel_y = 0;
    int w = 0;
    int h = 0;
    ui_if3_dat2_component_parent_relative_layout(comp, pw, ph, &rel_x, &rel_y, &w, &h);
    *out_w = w;
    *out_h = h;
    *out_x = px + rel_x;
    *out_y = py + rel_y;
}

static int
find_layout_root_index(int n)
{
    if( n <= 0 )
        return -1;
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
    int* out_order,
    int* out_order_count)
{
    int root_idx = find_layout_root_index(n);
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* depth = calloc((size_t)n, sizeof(int));
    int* in_tree = calloc((size_t)n, sizeof(int));
    if( !parent_idx || !depth || !in_tree )
    {
        free(parent_idx);
        free(depth);
        free(in_tree);
        for( int i = 0; i < n; i++ )
        {
            out_x[i] = root_x + comps[i].baseX;
            out_y[i] = root_y + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            if( out_order )
                out_order[i] = i;
        }
        if( out_order_count )
            *out_order_count = n;
        return;
    }

    for( int i = 0; i < n; i++ )
        parent_idx[i] = -1;

    for( int i = 0; i < n; i++ )
    {
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
        if( parent_idx[i] < 0 && root_idx >= 0 )
        {
            fprintf(
                stderr,
                "layout: file %d layer=0x%08x parent missing in archive; attach to root\n",
                i,
                comps[i].layer);
            parent_idx[i] = root_idx;
        }
    }

    if( root_idx >= 0 )
        in_tree[root_idx] = 1;
    int changed = 1;
    while( changed )
    {
        changed = 0;
        for( int i = 0; i < n; i++ )
        {
            if( in_tree[i] || comps[i].layer < 0 )
                continue;
            int p = parent_idx[i];
            if( p >= 0 && in_tree[p] )
            {
                in_tree[i] = 1;
                changed = 1;
            }
        }
    }

    for( int i = 0; i < n; i++ )
    {
        if( !in_tree[i] )
        {
            depth[i] = n + 1;
            continue;
        }
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

    int order_count = 0;
    if( out_order )
    {
        for( int i = 0; i < n; i++ )
        {
            if( !in_tree[i] )
                continue;
            out_order[order_count++] = i;
        }
        for( int a = 0; a < order_count; a++ )
        {
            for( int b = a + 1; b < order_count; b++ )
            {
                if( depth[out_order[b]] < depth[out_order[a]] )
                {
                    int t = out_order[a];
                    out_order[a] = out_order[b];
                    out_order[b] = t;
                }
            }
        }
        for( int i = 0; i < n; i++ )
        {
            if( in_tree[i] || i == root_idx )
                continue;
            if( comps[i].layer >= 0 )
                continue;
            out_order[order_count++] = i;
        }
    }
    if( out_order_count )
        *out_order_count = out_order ? order_count : n;

    for( int k = 0; k < (out_order ? order_count : n); k++ )
    {
        int i = out_order ? out_order[k] : k;
        if( out_order && !in_tree[i] )
            continue;

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

        layout_one_component(&comps[i], px, py, pw, ph, &out_x[i], &out_y[i], &out_w[i], &out_h[i]);
    }

    if( root_idx >= 0 )
    {
        int root_rw = out_w[root_idx];
        int root_rh = out_h[root_idx];
        int root_rx = out_x[root_idx];
        int root_ry = out_y[root_idx];

        for( int i = 0; i < n; i++ )
        {
            if( i == root_idx || comps[i].layer >= 0 || in_tree[i] )
                continue;

            layout_one_component(
                &comps[i],
                root_rx,
                root_ry,
                root_rw,
                root_rh,
                &out_x[i],
                &out_y[i],
                &out_w[i],
                &out_h[i]);
        }
    }

    if( verbose_layout )
    {
        for( int i = 0; i < n; i++ )
        {
            Component* c = &comps[i];
            fprintf(
                stderr,
                "layout[%02d] type=%d lay=%d,%d %dx%d graphic=%d tiled=%d layer=0x%08x tree=%d\n",
                i,
                c->type,
                out_x[i],
                out_y[i],
                out_w[i],
                out_h[i],
                c->graphic,
                c->tiled ? 1 : 0,
                c->layer,
                in_tree[i] ? 1 : 0);
        }
    }

    free(parent_idx);
    free(depth);
    free(in_tree);
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
                    ctx,
                    comp->invSlotGraphicId[slot],
                    slot_x,
                    slot_y,
                    0,
                    0,
                    false,
                    256,
                    NULL);
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

    if( comp->type == 4 && comp->text && comp->text[0] )
    {
        draw_text_component(
            ctx,
            ctx->font_cache,
            comp->textFont,
            comp->text,
            comp->color,
            comp->textHorizontalAlignment,
            comp->textShadow ? 1 : 0,
            px,
            py,
            lw,
            lh);
    }

    if( comp->type == 6 && comp->modelType == 1 && comp->modelId > 0 )
    {
        int rw = lw > 0 ? lw : (comp->baseWidth > 0 ? comp->baseWidth : 32);
        int rh = lh > 0 ? lh : (comp->baseHeight > 0 ? comp->baseHeight : 32);
        int* model_px = interface161_model_sprite_get(
            ctx,
            comp->modelId,
            comp->modelZoom,
            comp->modelXAngle,
            comp->modelYAngle,
            rw,
            rh);
        if( model_px )
            blit_rgba_sprite(ctx->pixels, CANVAS_W, px, py, model_px, rw, rh, trans_scale);
    }

    if( comp->type == 9 )
    {
        draw_line_component(
            ctx->pixels,
            comp->color,
            comp->lineWidth,
            comp->lineDirection,
            px,
            py,
            lw,
            lh);
    }

    if( comp->type == 5 && comp->graphic >= 0 )
    {
        int draw_w = lw;
        int draw_h = lh;
        if( comp->tiled )
        {
            if( draw_w <= 0 )
                draw_w = comp->baseWidth;
            if( draw_h <= 0 )
                draw_h = comp->baseHeight;
        }
        struct SpriteChromeOpts chrome = {
            .outline = comp->outline,
            .graphic_shadow = comp->graphicShadow,
            .flip_h = comp->horizontalFlip,
            .flip_v = comp->verticalFlip,
        };
        blit_sprite_from_cache(
            ctx, comp->graphic, px, py, draw_w, draw_h, comp->tiled, trans_scale, &chrome);
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
    const int* draw_order,
    int draw_count)
{
    if( draw_count <= 0 )
        draw_count = n;
    for( int k = 0; k < draw_count; k++ )
    {
        int fi = draw_order ? draw_order[k] : k;
        Component* comp = &comps[fi];
        if( comp->type < 0 || comp->hidden )
            continue;

        draw_one_component(ctx, comp, fi, lay_x[fi], lay_y[fi], lay_w[fi], lay_h[fi]);
    }
}

static void
draw_cs2_tree_overlays(
    struct DrawContext* ctx,
    struct UITree const* tree,
    Component* comps,
    int n,
    const int* lay_x,
    const int* lay_y,
    const int* lay_w,
    const int* lay_h);

static int
render_interface_archive(
    struct RSCacheDat2Disk* cache,
    struct ToriDraw_Scene* scene,
    struct Interface161Fixture const* fixture,
    struct Interface161ObjIconCache** obj_icon_cache,
    struct Interface161ModelSpriteCache** model_sprite_cache,
    struct Interface161FontCache* font_cache,
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
        fprintf(stderr, "failed to load interface archive %d\n", iface_id);
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

    int draw_count = 0;
    resolve_interface_layout(
        comps,
        n,
        root_x,
        root_y,
        root_w,
        root_h,
        lay_x,
        lay_y,
        lay_w,
        lay_h,
        draw_order,
        &draw_count);

    struct DrawContext dctx = {
        .cache = cache,
        .scene = scene,
        .fixture = fixture,
        .obj_icon_cache = obj_icon_cache,
        .model_sprite_cache = model_sprite_cache,
        .font_cache = font_cache,
        .pixels = pixels,
        .want_sprites = want_sprites,
    };
    draw_interface_components(
        &dctx, comps, n, lay_x, lay_y, lay_w, lay_h, draw_order, draw_count);

    struct Interface161Cs2Context cs2;
    interface161_cs2_context_init(&cs2);
    cs2.cache = cache;
    cs2.scene = scene;
    cs2.obj_icon_cache = &obj_icon_cache;
    cs2.root_w = root_w;
    cs2.root_h = root_h;
    if( interface161_cs2_run_interface(
            &cs2, comps, n, lay_x, lay_y, lay_w, lay_h, iface_id, fixture) == 0 )
    {
        draw_cs2_tree_overlays(&dctx, cs2.tree, comps, n, lay_x, lay_y, lay_w, lay_h);
    }
    interface161_cs2_context_free(&cs2);

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

static bool
is_orphan_graphic(
    Component const* comp)
{
    return comp && comp->type == 5 && comp->layer < 0 && comp->graphic >= 0 && !comp->hidden;
}

static void
draw_orphan_graphics(
    struct DrawContext* ctx,
    Component* comps,
    int n,
    const int* lay_x,
    const int* lay_y,
    const int* lay_w,
    const int* lay_h)
{
    for( int fi = 0; fi < n; fi++ )
    {
        if( !is_orphan_graphic(&comps[fi]) )
            continue;
        int trans_scale = 256 - (comps[fi].transparency & 0xFF);
        if( trans_scale < 0 )
            trans_scale = 0;
        struct SpriteChromeOpts chrome = {
            .outline = comps[fi].outline,
            .graphic_shadow = comps[fi].graphicShadow,
            .flip_h = comps[fi].horizontalFlip,
            .flip_v = comps[fi].verticalFlip,
        };
        blit_sprite_from_cache(
            ctx,
            comps[fi].graphic,
            lay_x[fi],
            lay_y[fi],
            lay_w[fi],
            lay_h[fi],
            comps[fi].tiled,
            trans_scale,
            &chrome);
    }
}

static void
draw_cs2_tree_graphics(
    struct DrawContext* ctx,
    struct UITree const* tree)
{
    if( !ctx || !tree )
        return;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &tree->components[i];
        if( node->type != UIELEM_RS_GRAPHIC )
            continue;
        if( node->behavior.hide )
            continue;
        if( node->item_id > 0 )
            continue;
        if( node->u.rs_graphic.graphic_hitbox_only )
            continue;
        if( node->u.rs_graphic.scene_id < 0 )
            continue;

        int px = node->position.abs_x;
        int py = node->position.abs_y;
        int pw = node->position.abs_w > 0 ? node->position.abs_w : 36;
        int ph = node->position.abs_h > 0 ? node->position.abs_h : 36;
        struct SpriteChromeOpts chrome = {
            .outline = node->u.rs_graphic.outline,
            .graphic_shadow = node->u.rs_graphic.graphic_shadow,
            .flip_h = node->u.rs_graphic.flip_h != 0,
            .flip_v = node->u.rs_graphic.flip_v != 0,
        };
        blit_sprite_from_cache(
            ctx,
            node->u.rs_graphic.scene_id,
            px,
            py,
            pw,
            ph,
            node->u.rs_graphic.tiled != 0,
            256,
            &chrome);
    }
}

static void
draw_cs2_tree_text(
    struct DrawContext* ctx,
    struct UITree const* tree)
{
    if( !ctx || !tree )
        return;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &tree->components[i];
        if( node->type != UIELEM_RS_TEXT )
            continue;
        if( node->behavior.hide )
            continue;
        if( !node->u.rs_text.text || !node->u.rs_text.text[0] )
            continue;

        int px = node->position.abs_x;
        int py = node->position.abs_y;
        int pw = node->position.abs_w > 0 ? node->position.abs_w : 0;
        int ph = node->position.abs_h > 0 ? node->position.abs_h : 0;
        draw_text_component(
            ctx,
            ctx->font_cache,
            node->u.rs_text.font_id,
            node->u.rs_text.text,
            node->u.rs_text.color,
            node->u.rs_text.center,
            node->u.rs_text.shadowed,
            px,
            py,
            pw,
            ph);
    }
}

static void
draw_cs2_tree_lines(
    struct DrawContext* ctx,
    struct UITree const* tree)
{
    if( !ctx || !tree )
        return;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &tree->components[i];
        if( node->type != UIELEM_RS_LINE )
            continue;
        if( node->behavior.hide )
            continue;

        int px = node->position.abs_x;
        int py = node->position.abs_y;
        int pw = node->position.abs_w > 0 ? node->position.abs_w : 1;
        int ph = node->position.abs_h > 0 ? node->position.abs_h : 1;
        draw_line_component(
            ctx->pixels,
            node->u.rs_line.color,
            node->u.rs_line.line_width,
            node->u.rs_line.horizontal != 0,
            px,
            py,
            pw,
            ph);
    }
}

static void
draw_cs2_tree_objs(
    struct DrawContext* ctx,
    struct UITree const* tree)
{
    if( !ctx || !tree )
        return;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &tree->components[i];
        if( node->behavior.hide )
            continue;

        int obj_id = node_item_obj_id(node);
        if( obj_id <= 0 )
            continue;

        int px = node->position.abs_x;
        int py = node->position.abs_y;
        int pw = node->position.abs_w > 0 ? node->position.abs_w : 36;
        int ph = node->position.abs_h > 0 ? node->position.abs_h : 36;
        blit_obj_icon_centered(ctx, obj_id, px, py, pw, ph);
    }
}

static void
draw_cs2_tree_overlays(
    struct DrawContext* ctx,
    struct UITree const* tree,
    Component* comps,
    int n,
    const int* lay_x,
    const int* lay_y,
    const int* lay_w,
    const int* lay_h)
{
    draw_cs2_tree_graphics(ctx, tree);
    draw_cs2_tree_text(ctx, tree);
    draw_cs2_tree_lines(ctx, tree);
    if( comps && n > 0 )
        draw_orphan_graphics(ctx, comps, n, lay_x, lay_y, lay_w, lay_h);
    draw_cs2_tree_objs(ctx, tree);
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: interface161_test <cache_directory> [--iface N] [--sprites]\n"
        "          [--fixture path.json] [--panel] [--root-w W] [--root-h H]\n"
        "          [--mount childFileIndex:ifaceId] ... [--verbose-layout] [out.bmp]\n"
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
        else if( strcmp(argv[i], "--verbose-layout") == 0 )
            verbose_layout = 1;
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
    struct Interface161ModelSpriteCache* model_sprite_cache = NULL;
    struct Interface161FontCache font_cache;
    memset(&font_cache, 0, sizeof(font_cache));
    if( want_sprites )
    {
        ToriDraw_Init();
        scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL);
        if( !scene )
            fprintf(stderr, "warning: ToriDraw_SceneNew failed; sprite/model/obj draw skipped\n");
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

    int draw_count = 0;
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
        draw_order,
        &draw_count);

    if( verbose_layout )
        fprintf(stderr, "draw_count=%d want_sprites=%d\n", draw_count, want_sprites);

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
        .model_sprite_cache = &model_sprite_cache,
        .font_cache = &font_cache,
        .pixels = pixels,
        .want_sprites = want_sprites,
    };
    draw_interface_components(
        &dctx, comps, n, lay_x, lay_y, lay_w, lay_h, draw_order, draw_count);

    struct Interface161Cs2Context cs2;
    interface161_cs2_context_init(&cs2);
    cs2.cache = cache;
    cs2.scene = scene;
    cs2.obj_icon_cache = &obj_icon_cache;
    cs2.root_w = root_w;
    cs2.root_h = root_h;
    if( interface161_cs2_run_interface(
            &cs2, comps, n, lay_x, lay_y, lay_w, lay_h, iface, &fixture) == 0 )
    {
        draw_cs2_tree_overlays(&dctx, cs2.tree, comps, n, lay_x, lay_y, lay_w, lay_h);
    }
    interface161_cs2_context_free(&cs2);

    for( int mi = 0; mi < mount_count; mi++ )
    {
        int cf = mounts[mi].child_file_index;
        if( cf < 0 || cf >= n )
            continue;
        int mx = lay_x[cf];
        int my = lay_y[cf];
        int mw = lay_w[cf];
        int mh = lay_h[cf];
        resolve_mount_rect(iface, cf, mx, my, mw, mh, &mx, &my, &mw, &mh);
        render_interface_archive(
            cache,
            scene,
            fixture.slot_count > 0 ? &fixture : NULL,
            &obj_icon_cache,
            &model_sprite_cache,
            &font_cache,
            mounts[mi].iface_id,
            mx,
            my,
            mw,
            mh,
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
    interface161_font_cache_free(&font_cache);
    interface161_model_sprite_cache_free(model_sprite_cache);
    interface161_obj_icon_cache_free(obj_icon_cache);
    RSCacheDat2Disk_Free(cache);
    interface161_fixture_free(&fixture);
    return 0;
}
