#include "platform/platform_sdl2_renderer_soft3d.h"

#include "graphics/fb_clear.h"
#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "render/torirs_frame.h"
#include "toridraw.h"
#include "toridraw_2d.h"
#include "toridraw_font.h"
#include "toridraw_frame_ab.h"
#include "toridraw_model_sprite.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every probe, census, ablation arm and environment knob this renderer has.
 * Each site below is a single call into it, and a default build takes one
 * predicted branch per frame or per model for the lot. */
#include "platform_sdl2_renderer_soft3d_debug.u.c"

struct Soft3DOutlineCacheEntry
{
    uint32_t const* src;
    int sw, sh;
    int outline;
    int graphic_shadow;
    uint32_t* pixels;
    int w, h;
    uint64_t last_used;
};

#define SOFT3D_OUTLINE_CACHE_SLOTS 256

/*
 * The renderer's frame-crossing working set (declared opaque in the header).
 * Init resets everything else on the struct every frame; this hangs off it and
 * lives from New to Free, because the outline cache is only worth anything if
 * it outlives a frame. Dense SETOBJECT grids with cc_setoutline(1) prefer a
 * pre-baked bordered icon; this cache covers the remaining draw-time
 * outline/shadow chrome.
 */
struct ToriRS_Soft3DScratch
{
    uint32_t* blit;
    size_t blit_cap;
    /* Layout-sized canvas for the pieces drawn at layout resolution and then
     * scaled into the buffer. @see soft3d_layer_begin. */
    int* layer;
    size_t layer_cap;
    /* The region a begun layer covers, and the buffer it swapped out. */
    int layer_open;
    int layer_x0, layer_y0, layer_x1, layer_y1;
    int* layer_saved_pixels;
    int layer_saved_width, layer_saved_height, layer_saved_stride;
    struct Soft3DOutlineCacheEntry outline_cache[SOFT3D_OUTLINE_CACHE_SLOTS];
    uint64_t outline_clock;
};

/* floor(v * num / den) for a positive den, negative v included. */
static inline int
soft3d_floor_scale(long long v, int num, int den)
{
    long long const n = v * num;
    long long q = n / den;
    if( n % den != 0 && n < 0 )
        q--;
    return (int)q;
}

/* The layout pixel a (non-negative) buffer pixel belongs to: the largest v
 * whose edge soft3d_mx(v) is at or before p. The inverse that keeps every
 * layout pixel's own buffer pixels -- floor(p * layout / buf) gives the first
 * of them to the neighbour and loses one-pixel-wide ones. */
static inline int
soft3d_owner(long long p, int buf, int layout)
{
    return (int)(((p + 1) * layout + buf - 1) / buf) - 1;
}

/* A layout coordinate, in buffer pixels. Edges map with floor, so two
 * layout rects that share an edge share it in the buffer too. */
static inline int
soft3d_mx(struct ToriRS_Soft3D const* soft, long long x)
{
    return soft->scaled ? soft3d_floor_scale(x, soft->width, soft->layout_w) : (int)x;
}

static inline int
soft3d_my(struct ToriRS_Soft3D const* soft, long long y)
{
    return soft->scaled ? soft3d_floor_scale(y, soft->height, soft->layout_h) : (int)y;
}

/*
 * Emitted scissor -> raster clip rect, intersected with the pixel buffer.
 *
 * The clip an emitter produces is whatever the layout says (a RevConfig node
 * box, an IF3 parent's box); nothing guarantees it lies inside the canvas —
 * chrome anchored to a screen edge routinely extends past it. Every draw kind
 * funnels through here, so this is the one place that can promise a command
 * cannot write outside the buffer.
 */
static struct ToriDraw_ViewPort
viewport_from_scissor(
    struct ToriRS_Soft3D const* soft,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h)
{
    struct ToriDraw_ViewPort vp;
    /* The scissor is a layout rect; the clip is the buffer's. */
    int const left = soft3d_mx(soft, scissor_x);
    int const top = soft3d_my(soft, scissor_y);
    int const right = soft3d_mx(soft, (long long)scissor_x + scissor_w);
    int const bottom = soft3d_my(soft, (long long)scissor_y + scissor_h);

    memset(&vp, 0, sizeof(vp));
    vp.stride = soft->stride;
    vp.clip_left = left < 0 ? 0 : left;
    vp.clip_top = top < 0 ? 0 : top;
    vp.clip_right = right > soft->width ? soft->width : right;
    vp.clip_bottom = bottom > soft->height ? soft->height : bottom;
    /* An entirely off-canvas box collapses to empty rather than inverting. */
    if( vp.clip_right < vp.clip_left )
        vp.clip_right = vp.clip_left;
    if( vp.clip_bottom < vp.clip_top )
        vp.clip_bottom = vp.clip_top;
    return vp;
}

static uint32_t*
soft3d_scratch(
    struct ToriRS_Soft3D* soft,
    size_t pixels)
{
    assert(soft);
    assert(soft->scratch);

    if( pixels == 0 )
        return NULL;
    if( pixels > soft->scratch->blit_cap )
    {
        uint32_t* grown = (uint32_t*)realloc(soft->scratch->blit, pixels * sizeof(uint32_t));
        assert(grown);
        soft->scratch->blit = grown;
        soft->scratch->blit_cap = pixels;
    }
    return soft->scratch->blit;
}

static uint32_t*
soft3d_outline_cache_get(
    struct ToriRS_Soft3D* soft,
    uint32_t const* src,
    int sw,
    int sh,
    int outline,
    int graphic_shadow,
    int* out_w,
    int* out_h)
{
    struct Soft3DOutlineCacheEntry* cache;
    uint64_t stamp;
    int i;
    int victim = 0;
    uint64_t victim_used = UINT64_MAX;
    uint32_t const* outlined;
    uint32_t* final_px;
    int ow = 0;
    int oh = 0;
    int fw = 0;
    int fh = 0;

    assert(soft);
    assert(soft->scratch);
    assert(out_w);
    assert(out_h);

    cache = soft->scratch->outline_cache;
    stamp = ++soft->scratch->outline_clock;

    for( i = 0; i < SOFT3D_OUTLINE_CACHE_SLOTS; i++ )
    {
        if( cache[i].src == src && cache[i].sw == sw && cache[i].sh == sh &&
            cache[i].outline == outline && cache[i].graphic_shadow == graphic_shadow &&
            cache[i].pixels )
        {
            cache[i].last_used = stamp;
            *out_w = cache[i].w;
            *out_h = cache[i].h;
            return cache[i].pixels;
        }
        if( cache[i].last_used < victim_used )
        {
            victim_used = cache[i].last_used;
            victim = i;
        }
    }

    outlined = src;
    ow = sw;
    oh = sh;
    final_px = NULL;

    if( outline > 0 )
    {
        outlined = ToriDraw_SpriteNewGraphicOutline(src, sw, sh, outline, &ow, &oh);
        if( !outlined )
            return NULL;
        final_px = (uint32_t*)outlined;
    }

    if( graphic_shadow != 0 )
    {
        uint32_t* shadowed =
            ToriDraw_SpriteNewGraphicShadow(outlined, ow, oh, graphic_shadow, &fw, &fh);
        if( final_px && final_px != src )
            free(final_px);
        if( !shadowed )
            return NULL;
        final_px = shadowed;
        ow = fw;
        oh = fh;
    }
    else if( !final_px )
    {
        return NULL;
    }

    free(cache[victim].pixels);
    cache[victim].src = src;
    cache[victim].sw = sw;
    cache[victim].sh = sh;
    cache[victim].outline = outline;
    cache[victim].graphic_shadow = graphic_shadow;
    cache[victim].pixels = final_px;
    cache[victim].w = ow;
    cache[victim].h = oh;
    cache[victim].last_used = stamp;
    *out_w = ow;
    *out_h = oh;
    return final_px;
}

static uint32_t*
soft3d_clamp_to_nominal(
    struct ToriRS_Soft3D* soft,
    uint32_t const* src,
    int src_w,
    int src_h,
    int src_ox,
    int src_oy,
    int nominal_w,
    int nominal_h)
{
    uint32_t* dst;
    int y;
    int x;
    size_t n;

    if( nominal_w <= 0 || nominal_h <= 0 || src_w <= 0 || src_h <= 0 )
        return NULL;
    assert(src);

    n = (size_t)nominal_w * (size_t)nominal_h;
    dst = soft3d_scratch(soft, n);
    assert(dst);
    memset(dst, 0, n * sizeof(uint32_t));

    for( y = 0; y < src_h; y++ )
    {
        int dst_y = y + src_oy;
        if( dst_y < 0 || dst_y >= nominal_h )
            continue;
        for( x = 0; x < src_w; x++ )
        {
            int dst_x = x + src_ox;
            if( dst_x < 0 || dst_x >= nominal_w )
                continue;
            dst[dst_y * nominal_w + dst_x] = src[y * src_w + x];
        }
    }
    return dst;
}

static void
soft3d_scale_pixel_alpha(
    uint32_t* buf,
    size_t count,
    int alpha)
{
    size_t i;

    if( alpha >= 255 )
        return;
    assert(buf);
    if( alpha < 0 )
        alpha = 0;

    for( i = 0; i < count; i++ )
    {
        uint32_t p = buf[i];
        int a = (int)((p >> 24) & 0xFF);
        a = (a * alpha) / 255;
        buf[i] = (p & 0x00FFFFFFu) | ((uint32_t)a << 24);
    }
}

/*
 * The sprite blits, in layout pixels. Unscaled they are the ToriDraw calls
 * themselves; scaled, the layout box maps to its buffer box and the image is
 * stretched into it, so an image drawn 1:1 in layout lands at the interface
 * scale.
 */
static void
soft3d_blit_scaled_alpha(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_ViewPort* vp,
    int x,
    int y,
    int w,
    int h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha)
{
    int const x0 = soft3d_mx(soft, x);
    int const y0 = soft3d_my(soft, y);
    ToriDraw2D_BlitArgbScaledAlpha(
        vp,
        x0,
        y0,
        soft3d_mx(soft, (long long)x + w) - x0,
        soft3d_my(soft, (long long)y + h) - y0,
        src,
        src_w,
        src_h,
        alpha,
        soft->pixels);
}

static void
soft3d_blit_alpha(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_ViewPort* vp,
    int x,
    int y,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha)
{
    if( !soft->scaled )
    {
        ToriDraw2D_BlitArgbAlpha(vp, x, y, src, src_w, src_h, alpha, soft->pixels);
        return;
    }
    soft3d_blit_scaled_alpha(soft, vp, x, y, src_w, src_h, src, src_w, src_h, alpha);
}

static void
soft3d_blit_tiled_alpha(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_ViewPort* vp,
    int x,
    int y,
    int w,
    int h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int origin_x,
    int origin_y,
    int alpha)
{
    if( !soft->scaled )
    {
        ToriDraw2D_BlitArgbTiledAlpha(
            vp, x, y, w, h, src, src_w, src_h, origin_x, origin_y, alpha, soft->pixels);
        return;
    }
    ToriDraw2D_BlitArgbTiledScaledAlpha(
        vp,
        x,
        y,
        w,
        h,
        src,
        src_w,
        src_h,
        origin_x,
        origin_y,
        soft->width,
        soft->layout_w,
        soft->height,
        soft->layout_h,
        alpha,
        soft->pixels);
}

/*
 * The layer: a piece drawn at layout resolution by code that only knows how
 * to write 1:1 -- a rotated sprite, a model widget -- then scaled into the
 * buffer. Only opaque writers go through it: the region starts as a sentinel
 * no writer produces, and every other pixel is copied across as it was
 * written. A writer that BLENDS would blend against the sentinel, so those
 * are scaled natively instead.
 */
#define SOFT3D_LAYER_EMPTY ((int)0x00FE01FD)

static int*
soft3d_layer_begin(struct ToriRS_Soft3D* soft, int x0, int y0, int x1, int y1)
{
    struct ToriRS_Soft3DScratch* scratch;
    size_t n;

    assert(soft);
    assert(soft->scratch);
    scratch = soft->scratch;
    assert(!scratch->layer_open);
    if( !soft->scaled )
        return soft->pixels;

    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > soft->layout_w )
        x1 = soft->layout_w;
    if( y1 > soft->layout_h )
        y1 = soft->layout_h;
    if( x1 < x0 )
        x1 = x0;
    if( y1 < y0 )
        y1 = y0;

    n = (size_t)soft->layout_w * (size_t)soft->layout_h;
    if( n > scratch->layer_cap )
    {
        int* grown = (int*)realloc(scratch->layer, n * sizeof(int));
        assert(grown);
        scratch->layer = grown;
        scratch->layer_cap = n;
    }
    for( int y = y0; y < y1; y++ )
    {
        int* row = scratch->layer + (size_t)y * (size_t)soft->layout_w;
        for( int x = x0; x < x1; x++ )
            row[x] = SOFT3D_LAYER_EMPTY;
    }

    scratch->layer_open = 1;
    scratch->layer_x0 = x0;
    scratch->layer_y0 = y0;
    scratch->layer_x1 = x1;
    scratch->layer_y1 = y1;
    scratch->layer_saved_pixels = soft->pixels;
    scratch->layer_saved_width = soft->width;
    scratch->layer_saved_height = soft->height;
    scratch->layer_saved_stride = soft->stride;
    /* Unscaled while it is open: the writer sees a layout-sized buffer. */
    soft->pixels = scratch->layer;
    soft->width = soft->layout_w;
    soft->height = soft->layout_h;
    soft->stride = soft->layout_w;
    soft->scaled = false;
    ToriDraw2D_FontSetOutputScale(1, 1, 1, 1);
    return scratch->layer;
}

static void
soft3d_layer_end(struct ToriRS_Soft3D* soft)
{
    struct ToriRS_Soft3DScratch* scratch;
    int bx0, by0, bx1, by1;

    assert(soft);
    assert(soft->scratch);
    scratch = soft->scratch;
    if( !scratch->layer_open )
        return;
    scratch->layer_open = 0;
    soft->pixels = scratch->layer_saved_pixels;
    soft->width = scratch->layer_saved_width;
    soft->height = scratch->layer_saved_height;
    soft->stride = scratch->layer_saved_stride;
    soft->scaled = true;
    ToriDraw2D_FontSetOutputScale(soft->width, soft->layout_w, soft->height, soft->layout_h);

    bx0 = soft3d_mx(soft, scratch->layer_x0);
    by0 = soft3d_my(soft, scratch->layer_y0);
    bx1 = soft3d_mx(soft, scratch->layer_x1);
    by1 = soft3d_my(soft, scratch->layer_y1);
    if( bx1 > soft->width )
        bx1 = soft->width;
    if( by1 > soft->height )
        by1 = soft->height;
    if( bx0 >= bx1 || by0 >= by1 )
        return;

    {
        int const columns = bx1 - bx0;
        int* lx = (int*)malloc((size_t)columns * sizeof(int));
        assert(lx);
        for( int x = 0; x < columns; x++ )
            lx[x] = soft3d_owner(bx0 + x, soft->width, soft->layout_w);
        for( int y = by0; y < by1; y++ )
        {
            int const ly = soft3d_owner(y, soft->height, soft->layout_h);
            int const* srow = scratch->layer + (size_t)ly * (size_t)soft->layout_w;
            int* drow = soft->pixels + (size_t)y * (size_t)soft->stride;
            for( int x = 0; x < columns; x++ )
            {
                int const v = srow[lx[x]];
                if( v != SOFT3D_LAYER_EMPTY )
                    drow[bx0 + x] = v;
            }
        }
        free(lx);
    }
}

static void
soft3d_draw_sprite(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Sprite const* cmd)
{
    struct ToriDraw_Sprite** sprites;
    int count = 0;
    struct ToriDraw_Sprite* spr;
    struct ToriDraw_ViewPort vp;
    int atlas;
    int nominal_w;
    int nominal_h;
    int sw;
    int sh;
    int ox;
    int oy;
    size_t pixel_count;
    uint32_t* spr_px;
    int alpha;
    int angle_2d;
    int pre_rot_sw;
    int pre_rot_sh;
    int pre_rot_ox;
    int pre_rot_oy;

    assert(soft);
    assert(cmd);
    assert(soft->scene);

    if( cmd->scene_id <= 0 )
        return;
    sprites = ToriDraw_SceneSpriteGet(soft->scene, cmd->scene_id, &count);
    if( !sprites || count <= 0 )
        return;
    atlas = cmd->atlas_index;
    if( atlas < 0 || atlas >= count )
        atlas = 0;
    spr = sprites[atlas];
    if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
        return;

    /* Chrome rotated by camera yaw (compass, minimap, scrollbar arrows): inverse-map
     * the destination box through the anchor pair instead of growing a pixel buffer.
     * Units here are 0..2047, not the IF3 spriteAngle scale used further down.
     * The rotator writes opaque pixels 1:1, so a scaled frame draws it into the
     * layer over its own box and scales that. */
    if( cmd->rotated )
    {
        struct ToriDraw_Sprite* mask_spr = NULL;
        int const box_w = cmd->w > 0 ? cmd->w : spr->width;
        int const box_h = cmd->h > 0 ? cmd->h : spr->height;
        bool const layered = soft->scaled;
        if( layered )
            (void)soft3d_layer_begin(soft, cmd->x, cmd->y, cmd->x + box_w, cmd->y + box_h);
        vp = viewport_from_scissor(
            soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
        if( cmd->mask_scene_id > 0 )
        {
            int mask_count = 0;
            struct ToriDraw_Sprite** mask_sprites =
                ToriDraw_SceneSpriteGet(soft->scene, cmd->mask_scene_id, &mask_count);
            if( mask_sprites && cmd->mask_atlas_index >= 0 && cmd->mask_atlas_index < mask_count )
                mask_spr = mask_sprites[cmd->mask_atlas_index];
        }
        if( mask_spr && mask_spr->pixels_argb )
            ToriDraw2D_BlitSpriteRotatedMaskedEx(
                spr,
                mask_spr,
                cmd->mask_keep_opaque,
                &vp,
                cmd->x,
                cmd->y,
                cmd->w > 0 ? cmd->w : spr->width,
                cmd->h > 0 ? cmd->h : spr->height,
                cmd->dst_anchor_x,
                cmd->dst_anchor_y,
                cmd->src_anchor_x,
                cmd->src_anchor_y,
                cmd->rotation_r2pi2048,
                soft->pixels);
        else
            ToriDraw2D_BlitSpriteRotatedEx(
                spr,
                &vp,
                cmd->x,
                cmd->y,
                cmd->w > 0 ? cmd->w : spr->width,
                cmd->h > 0 ? cmd->h : spr->height,
                cmd->dst_anchor_x,
                cmd->dst_anchor_y,
                cmd->src_anchor_x,
                cmd->src_anchor_y,
                cmd->rotation_r2pi2048,
                soft->pixels);
        /* The writer clipped to the scissor, so what it did not write stays
         * empty and is not copied. */
        if( layered )
            soft3d_layer_end(soft);
        return;
    }

    vp =
        viewport_from_scissor(soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);

    nominal_w = spr->width;
    nominal_h = spr->height;
    sw = nominal_w;
    sh = nominal_h;
    ox = spr->crop_x;
    oy = spr->crop_y;
    pixel_count = (size_t)sw * (size_t)sh;

    /*
     * Fast path: no command below mutates pixels, so blit the scene's cached
     * sprite directly instead of cloning it.
     *
     * The general path allocates and copies the whole image up to four times
     * per sprite per frame (clone, flip, clamp-to-nominal, rotate). At rev230
     * gameframe sprite counts that clone traffic — and the calloc inside
     * soft3d_clamp_to_nominal in particular — was a top frame-time cost, and
     * every one of those copies is the identity when the sprite is drawn
     * plain, which is the overwhelmingly common case.
     */
    if( cmd->outline <= 0 && cmd->graphic_shadow == 0 && !cmd->flip_h && !cmd->flip_v &&
        cmd->sprite_angle_r2pi65536 == 0 )
    {
        alpha = 255 - cmd->trans;
        if( alpha < 0 )
            alpha = 0;
        else if( alpha > 255 )
            alpha = 255;

        if( cmd->tiled )
        {
            soft3d_blit_tiled_alpha(
                soft,
                &vp,
                cmd->x,
                cmd->y,
                cmd->w,
                cmd->h,
                spr->pixels_argb,
                sw,
                sh,
                cmd->x + ox,
                cmd->y + oy,
                alpha);
            return;
        }
        if( !cmd->if3 )
        {
            soft3d_dbg_sprite_census_note(spr, sw * sh);
            soft3d_blit_alpha(soft, &vp, cmd->x + ox, cmd->y + oy, spr->pixels_argb, sw, sh, alpha);
            return;
        }
        /* if3 scales the *nominal* box, so a crop offset is only skippable when
         * the sprite already sits at that box's origin — otherwise the offset
         * would have to scale with it and the general path has to run. */
        if( ox == 0 && oy == 0 )
        {
            int draw_w = cmd->w > 0 ? cmd->w : sw;
            int draw_h = cmd->h > 0 ? cmd->h : sh;
            soft3d_blit_scaled_alpha(
                soft, &vp, cmd->x, cmd->y, draw_w, draw_h, spr->pixels_argb, sw, sh, alpha);
            return;
        }
    }

    /*
     * Outlined/shadowed icons with no further pixel mutation: serve from the
     * renderer-lifetime LRU so idle chrome stops calloc/freeing every frame.
     */
    if( (cmd->outline > 0 || cmd->graphic_shadow != 0) && cmd->trans <= 0 && !cmd->flip_h &&
        !cmd->flip_v && cmd->sprite_angle_r2pi65536 == 0 && !cmd->tiled )
    {
        int cw = 0;
        int ch = 0;
        uint32_t* cached = soft3d_outline_cache_get(
            soft, spr->pixels_argb, sw, sh, cmd->outline, cmd->graphic_shadow, &cw, &ch);
        if( cached )
        {
            int cox = ox;
            int coy = oy;
            if( cmd->if3 )
            {
                int draw_w = cmd->w > 0 ? cmd->w : nominal_w;
                int draw_h = cmd->h > 0 ? cmd->h : nominal_h;
                /* Outline is now same-size as the source (deob method9420);
                 * no pad offset to compensate. */
                if( cox == 0 && coy == 0 && cw == nominal_w && ch == nominal_h )
                {
                    soft3d_blit_scaled_alpha(
                        soft, &vp, cmd->x, cmd->y, draw_w, draw_h, cached, cw, ch, 255);
                    return;
                }
            }
            else
            {
                soft3d_blit_alpha(soft, &vp, cmd->x + cox, cmd->y + coy, cached, cw, ch, 255);
                return;
            }
        }
    }

    spr_px = malloc(pixel_count * sizeof(uint32_t));
    assert(spr_px);
    memcpy(spr_px, spr->pixels_argb, pixel_count * sizeof(uint32_t));

    if( cmd->outline > 0 || cmd->graphic_shadow != 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* cached = soft3d_outline_cache_get(
            soft,
            spr->pixels_argb,
            nominal_w,
            nominal_h,
            cmd->outline,
            cmd->graphic_shadow,
            &sw2,
            &sh2);
        if( cached )
        {
            size_t n = (size_t)sw2 * (size_t)sh2;
            uint32_t* copy = malloc(n * sizeof(uint32_t));
            assert(copy);
            memcpy(copy, cached, n * sizeof(uint32_t));
            free(spr_px);
            spr_px = copy;
            sw = sw2;
            sh = sh2;
        }
    }

    alpha = 255 - cmd->trans;
    if( alpha < 0 )
        alpha = 0;
    else if( alpha > 255 )
        alpha = 255;
    soft3d_scale_pixel_alpha(spr_px, (size_t)sw * (size_t)sh, alpha);

    angle_2d = cmd->sprite_angle_r2pi65536;
    pre_rot_sw = sw;
    pre_rot_sh = sh;
    pre_rot_ox = ox;
    pre_rot_oy = oy;

    if( cmd->if3 && !cmd->tiled )
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, 0);

        /* Identity when the sprite already fills its nominal box at the origin;
         * skipping it drops a full-image calloc+copy from the general path too
         * (a flipped or translucent sprite still usually needs no clamp). */
        if( ox != 0 || oy != 0 || sw != nominal_w || sh != nominal_h )
        {
            uint32_t* clamped =
                soft3d_clamp_to_nominal(soft, spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
            if( clamped )
            {
                /* clamp writes into the renderer scratch — copy out so the
                 * later TransformPixels free stays well-defined. */
                size_t n = (size_t)nominal_w * (size_t)nominal_h;
                uint32_t* owned = malloc(n * sizeof(uint32_t));
                assert(owned);
                memcpy(owned, clamped, n * sizeof(uint32_t));
                free(spr_px);
                spr_px = owned;
                sw = nominal_w;
                sh = nominal_h;
                ox = 0;
                oy = 0;
            }
        }

        {
            int const unrot_w = sw;
            int const unrot_h = sh;
            int const box_w = cmd->w > 0 ? cmd->w : unrot_w;
            int const box_h = cmd->h > 0 ? cmd->h : unrot_h;
            int draw_x = cmd->x;
            int draw_y = cmd->y;
            int draw_w = box_w;
            int draw_h = box_h;

            ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, 0, 0, angle_2d);

            /* Rotation grows the buffer past the nominal box (a square turned
             * 45 degrees needs sqrt(2) times the room). Scale by the *box*
             * ratio, not the rotated one, and keep the result centred on the
             * box: scale-then-rotate, so a spinning icon holds its size
             * instead of pumping smaller as it turns. */
            if( angle_2d != 0 && unrot_w > 0 && unrot_h > 0 )
            {
                draw_w = sw * box_w / unrot_w;
                draw_h = sh * box_h / unrot_h;
                draw_x = cmd->x + (box_w - draw_w) / 2;
                draw_y = cmd->y + (box_h - draw_h) / 2;
            }
            soft3d_blit_scaled_alpha(soft, &vp, draw_x, draw_y, draw_w, draw_h, spr_px, sw, sh, 255);
        }
    }
    else
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, angle_2d);

        if( cmd->tiled )
        {
            soft3d_blit_tiled_alpha(
                soft, &vp, cmd->x, cmd->y, cmd->w, cmd->h, spr_px, sw, sh, cmd->x + ox, cmd->y + oy,
                255);
        }
        else
        {
            int draw_x = cmd->x + ox;
            int draw_y = cmd->y + oy;
            if( angle_2d != 0 )
            {
                int center_x = cmd->x + pre_rot_ox + pre_rot_sw / 2;
                int center_y = cmd->y + pre_rot_oy + pre_rot_sh / 2;
                draw_x = center_x - sw / 2;
                draw_y = center_y - sh / 2;
            }
            soft3d_blit_alpha(soft, &vp, draw_x, draw_y, spr_px, sw, sh, 255);
        }
    }

    free(spr_px);
}

static void
soft3d_draw_font(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Font const* cmd)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;

    assert(soft);
    assert(cmd);
    if( cmd->font_id < 0 || !cmd->text )
        return;
    font = ToriDraw_SceneFontGet(soft->scene, cmd->font_id);
    if( !font )
        return;

    vp =
        viewport_from_scissor(soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    if( cmd->baseline )
    {
        /* Baseline text (world overlays like hitsplats): y is the text bottom,
         * matching reference PixFont.drawString/centreString. Box alignment
         * (y_align, w/h) does not apply. */
        (void)ToriDraw2D_DrawString(
            font,
            &vp,
            cmd->x,
            cmd->y,
            cmd->text,
            cmd->color,
            cmd->center != 0,
            cmd->shadowed,
            soft->pixels);
        return;
    }
    (void)ToriDraw2D_DrawStringBox(
        font,
        &vp,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->text,
        cmd->color,
        cmd->center,
        cmd->y_align,
        cmd->line_height,
        cmd->shadowed,
        soft->pixels);
}

static void
soft3d_draw_fill_rect(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_FillRect const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int x0;
    int y0;
    int x1;
    int y1;

    assert(soft);
    assert(cmd);
    vp =
        viewport_from_scissor(soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    x0 = soft3d_mx(soft, cmd->x);
    y0 = soft3d_my(soft, cmd->y);
    x1 = soft3d_mx(soft, (long long)cmd->x + cmd->w);
    y1 = soft3d_my(soft, (long long)cmd->y + cmd->h);
    if( cmd->filled )
        ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
    else if( !soft->scaled )
        ToriDraw2D_DrawRectOutline(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
    else if( cmd->w > 0 && cmd->h > 0 )
    {
        /* The outline is one LAYOUT pixel: each edge is that pixel's strip. */
        int const inner_x0 = soft3d_mx(soft, (long long)cmd->x + 1);
        int const inner_y0 = soft3d_my(soft, (long long)cmd->y + 1);
        int const inner_x1 = soft3d_mx(soft, (long long)cmd->x + cmd->w - 1);
        int const inner_y1 = soft3d_my(soft, (long long)cmd->y + cmd->h - 1);
        ToriDraw2D_FillRect(&vp, x0, y0, x1, inner_y0, cmd->argb, soft->pixels);
        if( inner_y1 > inner_y0 )
        {
            ToriDraw2D_FillRect(&vp, x0, inner_y1, x1, y1, cmd->argb, soft->pixels);
            ToriDraw2D_FillRect(&vp, x0, inner_y0, inner_x0, inner_y1, cmd->argb, soft->pixels);
            ToriDraw2D_FillRect(&vp, inner_x1, inner_y0, x1, inner_y1, cmd->argb, soft->pixels);
        }
    }
}

static void
soft3d_draw_clear_rect(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_ClearRect const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int x0;
    int y0;
    int x1;
    int y1;

    assert(soft);
    assert(cmd);
    vp = viewport_from_scissor(
        soft, 0, 0, soft->scaled ? soft->layout_w : soft->width, soft->scaled ? soft->layout_h : soft->height);
    x0 = soft3d_mx(soft, cmd->x);
    y0 = soft3d_my(soft, cmd->y);
    x1 = soft3d_mx(soft, (long long)cmd->x + cmd->w);
    y1 = soft3d_my(soft, (long long)cmd->y + cmd->h);
    ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, TORIRS_SOFT3D_BG, soft->pixels);
}

/* ---- convex polygon ------------------------------------------------------ *
 *
 * The shared decomposition (render/torirs_polygon.c) turns the polygon into
 * horizontal runs; this writes them. Blending is the same alpha the sprite path
 * uses, because a highlight is a wash over what is already drawn -- an opaque
 * fill would hide the very model it is marking.
 */

struct soft3d_span_ctx
{
    struct ToriRS_Soft3D* soft;
    uint32_t argb;
    int trans;
};

static void
soft3d_polygon_span(
    void* user_data,
    int x,
    int y,
    int count)
{
    struct soft3d_span_ctx* ctx = user_data;
    struct ToriRS_Soft3D* soft = ctx->soft;
    int* row;
    int alpha;

    if( y < 0 || y >= soft->height || count <= 0 )
        return;
    if( x < 0 )
    {
        count += x;
        x = 0;
    }
    if( x + count > soft->width )
        count = soft->width - x;
    if( count <= 0 )
        return;

    row = soft->pixels + (size_t)y * (size_t)soft->stride + (size_t)x;
    alpha = 255 - (ctx->trans & 0xFF);
    if( alpha >= 255 )
    {
        for( int i = 0; i < count; i++ )
            row[i] = (int)ctx->argb;
        return;
    }
    if( alpha <= 0 )
        return;

    {
        int const sr = (int)((ctx->argb >> 16) & 0xFF);
        int const sg = (int)((ctx->argb >> 8) & 0xFF);
        int const sb = (int)(ctx->argb & 0xFF);
        int const inv = 255 - alpha;
        for( int i = 0; i < count; i++ )
        {
            uint32_t const d = (uint32_t)row[i];
            int const dr = (int)((d >> 16) & 0xFF);
            int const dg = (int)((d >> 8) & 0xFF);
            int const db = (int)(d & 0xFF);
            row[i] = (int)(0xFF000000u | (uint32_t)(((sr * alpha + dr * inv) / 255) << 16) |
                           (uint32_t)(((sg * alpha + dg * inv) / 255) << 8) |
                           (uint32_t)((sb * alpha + db * inv) / 255));
        }
    }
}

static void
soft3d_polygon_end(struct ToriRS_Soft3D* soft)
{
    struct soft3d_span_ctx ctx;
    int cx;
    int cy;
    int cw;
    int ch;

    assert(soft);
    if( !soft->polygon_open )
        return;
    soft->polygon_open = 0;

    ctx.soft = soft;
    ctx.argb = (uint32_t)soft->polygon.argb;
    ctx.trans = soft->polygon.trans;

    /* In layout pixels, as the command states it; the points were scaled as
     * they arrived and the scissor is scaled here. */
    {
        int const lx = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_x : 0;
        int const ly = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_y : 0;
        int const lw = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_w
                                                   : (soft->scaled ? soft->layout_w : soft->width);
        int const lh = soft->polygon.scissor_h > 0 ? soft->polygon.scissor_h
                                                   : (soft->scaled ? soft->layout_h : soft->height);
        cx = soft3d_mx(soft, lx);
        cy = soft3d_my(soft, ly);
        cw = soft3d_mx(soft, (long long)lx + lw) - cx;
        ch = soft3d_my(soft, (long long)ly + lh) - cy;
    }

    ToriRS_PolygonFillConvex(
        soft->polygon_x,
        soft->polygon_y,
        soft->polygon_count,
        cx,
        cy,
        cw,
        ch,
        soft3d_polygon_span,
        &ctx);
}

static void
soft3d_draw_line(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Line const* cmd)
{
    struct ToriDraw_ViewPort vp;
    int thickness;
    int x1;
    int y1;
    int x2;
    int y2;

    assert(soft);
    assert(cmd);
    vp =
        viewport_from_scissor(soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    thickness = cmd->line_width > 0 ? cmd->line_width : 1;

    if( cmd->line_direction )
    {
        x1 = cmd->x;
        y1 = cmd->y + cmd->h;
        x2 = cmd->x + cmd->w;
        y2 = cmd->y;
    }
    else
    {
        x1 = cmd->x;
        y1 = cmd->y;
        x2 = cmd->x + cmd->w;
        y2 = cmd->y + cmd->h;
    }

    if( soft->scaled )
    {
        x1 = soft3d_mx(soft, x1);
        y1 = soft3d_my(soft, y1);
        x2 = soft3d_mx(soft, x2);
        y2 = soft3d_my(soft, y2);
        thickness = (int)((long long)thickness * soft->height / soft->layout_h);
        if( thickness < 1 )
            thickness = 1;
    }
    ToriDraw2D_DrawLine(&vp, x1, y1, x2, y2, thickness, cmd->argb, soft->pixels);
}

static void
soft3d_draw_model_widget(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_ModelWidget const* cmd)
{
    int draw_x = 0;
    int draw_y = 0;
    int out_w = 0;
    int out_h = 0;

    assert(soft);
    assert(cmd);
    if( !ToriDraw_ModelKindIsFull(cmd->model.kind) || !cmd->model.u.model.model )
        return;

    /* The widget raster writes opaque pixels 1:1 at layout resolution; a
     * scaled frame draws it into the layer over its clipped box. */
    bool const layered = soft->scaled;
    if( layered )
    {
        int bx0 = cmd->x > cmd->scissor_x ? cmd->x : cmd->scissor_x;
        int by0 = cmd->y > cmd->scissor_y ? cmd->y : cmd->scissor_y;
        int bx1 = cmd->x + cmd->w < cmd->scissor_x + cmd->scissor_w ? cmd->x + cmd->w
                                                                    : cmd->scissor_x + cmd->scissor_w;
        int by1 = cmd->y + cmd->h < cmd->scissor_y + cmd->scissor_h ? cmd->y + cmd->h
                                                                    : cmd->scissor_y + cmd->scissor_h;
        (void)soft3d_layer_begin(soft, bx0, by0, bx1, by1);
    }

    (void)ToriDraw_RenderModelExtentsAtWidget(
        soft->scene,
        cmd->model,
        cmd->model_zoom > 0 ? cmd->model_zoom : 2000,
        cmd->model_xan,
        cmd->model_yan,
        cmd->model_zan,
        cmd->model_x_offset,
        cmd->model_y_offset,
        cmd->model_center_y,
        cmd->model_orthog != 0,
        cmd->model_fixed_zoom != 0,
        (toripixel_t*)soft->pixels,
        soft->stride,
        soft->width,
        soft->height,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        cmd->scissor_x,
        cmd->scissor_y,
        cmd->scissor_x + cmd->scissor_w,
        cmd->scissor_y + cmd->scissor_h,
        &draw_x,
        &draw_y,
        &out_w,
        &out_h);
    if( layered )
        soft3d_layer_end(soft);
}

static void
soft3d_draw_model(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Model const* cmd)
{
    struct ToriDraw_Position position;
    int cull;
    const struct ToriDraw_Kernel* kernel;

    assert(soft);
    assert(cmd);
    if( !soft->has_3d )
        return;
    kernel = ToriDraw_FrameAbEnabled() ? &soft->kernel_ab[ToriDraw_FrameAbArm()] : soft->kernel;
    if( cmd->model.kind == TORIDRAWMK_NONE )
        return;

    /* ABLATION (TORIRS_ABL_NOMODELS): the whole 3D pass, deleted. */
    if( soft3d_dbg_abl_nomodels() )
        return;

    if( cmd->animation && cmd->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            soft->scene, cmd->element_id, cmd->anim_index == 0, cmd->anim_frame);

    position = cmd->position;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_PROJECT)
    {
        cull = ToriDraw_RenderModel1ProjectWithTable(
            cmd->model, soft->scene, &position, &soft->view_port_3d, &soft->camera_3d, kernel);
    }
    soft3d_dbg_draw_trace_cull(cmd, &position, (int)cull);
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_MODEL_CULLED, 1);
        return;
    }

    /* Hittest before the face sort: the scene scratch holds this model's
     * projection only until the next model projects, and a model whose faces
     * all sort away must still pick. */
    if( soft->pick_enabled && cmd->pickable && cmd->element_id >= 0 )
    {
        bool hit;
        if( cmd->pick_aabb )
            hit = ToriDraw_ProjectedModelContainsAabb(
                soft->scene, soft->pick_mouse_x, soft->pick_mouse_y);
        else if( cmd->pick_terrain )
            hit = ToriDraw_ProjectedTileMouseHitTest(
                soft->scene,
                cmd->model,
                &soft->view_port_3d,
                soft->pick_mouse_x,
                soft->pick_mouse_y);
        else
            hit = ToriDraw_ProjectedModelMouseHitTest(
                soft->scene,
                cmd->model,
                &soft->view_port_3d,
                soft->pick_mouse_x,
                soft->pick_mouse_y);

        if( hit )
            ToriRS_PickHitsAdd(
                &soft->pick_hits,
                cmd->element_id,
                cmd->pick_terrain,
                cmd->pick_tile_x,
                cmd->pick_tile_z,
                cmd->pick_tile_level,
                cmd->pick_view);
    }

    if( cmd->pick_only )
        return;

    int sorted = 0;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_SORT)
    {
        /* Whether this leaves the batched walk's y-ordered stash behind is the
         * TABLE's answer, not ours -- and it has to be, because the arm we
         * hold changes under TORIDRAW_RASTER_SCANLINE and the frame A/B, and
         * only the branching kernel has a door that reads the stash. */
        sorted = ToriDraw_RenderModel2SortFacesWithTable(cmd->model, soft->scene, kernel);
    }
    soft3d_dbg_draw_trace_sorted(cmd, sorted);
    /* Counted after the sort, not before: a model that survives both culls
     * has already paid its whole per-vertex projection by this point, so
     * `sorted <= 0` is work spent for no pixels and wants its own name. */
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_MODEL_DRAWN, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_MODEL_FACES, sorted > 0 ? sorted : 0);
    if( sorted <= 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_MODEL_SORT_EMPTY, 1);
        return;
    }
    /* ABLATION (TORIRS_ABL_NORASTER): everything decided, no pixels written. */
    if( soft3d_dbg_abl_noraster() )
        return;

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_RASTER)
    {
        ToriDraw_RenderModel3RasterWithTable(
            soft->scene, &soft->view_port_3d, &soft->camera_3d, soft->pixels, kernel);
    }
}

/*
 * The world at the buffer's resolution: the layout viewport mapped into the
 * buffer, and the projection scaled with it so the same scene fills the
 * larger rectangle. Interface scaling sizes the interface; this is what keeps
 * it from sizing the world.
 */
static void
soft3d_scale_world(struct ToriRS_Soft3D* soft)
{
    struct ToriDraw_ViewPort* vp = &soft->view_port_3d;
    struct ToriDraw_Camera* camera = &soft->camera_3d;
    int const left = vp->x_center - vp->width / 2;
    int const top = vp->y_center - vp->height / 2;
    int const bleft = soft3d_mx(soft, left);
    int const btop = soft3d_my(soft, top);
    int const bw = soft3d_mx(soft, (long long)left + vp->width) - bleft;
    int const bh = soft3d_my(soft, (long long)top + vp->height) - btop;

    assert(soft->scaled);
    vp->width = bw;
    vp->height = bh;
    vp->x_center = bleft + bw / 2;
    vp->y_center = btop + bh / 2;
    vp->clip_left = soft3d_mx(soft, vp->clip_left);
    vp->clip_top = soft3d_my(soft, vp->clip_top);
    vp->clip_right = soft3d_mx(soft, vp->clip_right);
    vp->clip_bottom = soft3d_my(soft, vp->clip_bottom);
    if( vp->clip_right > soft->width )
        vp->clip_right = soft->width;
    if( vp->clip_bottom > soft->height )
        vp->clip_bottom = soft->height;

    /* One factor for both axes -- the projection has one -- taken from the
     * height, which is what the viewport's scale was derived from. */
    switch( camera->projection_mode )
    {
    case TORIDRAW_PROJECTION_MODE_PARALLEL:
        camera->parallel_zoom16 =
            (int)((long long)camera->parallel_zoom16 * soft->height / soft->layout_h);
        break;
    case TORIDRAW_PROJECTION_MODE_FOV:
    {
        /* An angle does not grow with the viewport; the equivalent linear
         * scale does. */
        int const cot16 = toridraw_projection_cot16_from_fov(camera->fov_rpi2048);
        camera->projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
        camera->projection_scale = (int)(((long long)cot16 * soft->height / soft->layout_h) >>
                                         TORIDRAW_PROJECTION_COT16_SHIFT);
        break;
    }
    default:
    {
        int const scale = camera->projection_scale > 0 ? camera->projection_scale
                                                       : TORIDRAW_PROJECTION_SCALE_DEFAULT;
        camera->projection_scale = (int)((long long)scale * soft->height / soft->layout_h);
        break;
    }
    }
}

struct ToriRS_Soft3D*
ToriRS_Soft3D_New(void)
{
    struct ToriRS_Soft3D* soft = calloc(1, sizeof(*soft));

    assert(soft);
    soft->scratch = calloc(1, sizeof(*soft->scratch));
    assert(soft->scratch);
    return soft;
}

void
ToriRS_Soft3D_Free(struct ToriRS_Soft3D* soft)
{
    int i;

    if( !soft )
        return;
    if( soft->scratch )
    {
        for( i = 0; i < SOFT3D_OUTLINE_CACHE_SLOTS; i++ )
            free(soft->scratch->outline_cache[i].pixels);
        free(soft->scratch->blit);
        free(soft->scratch->layer);
        free(soft->scratch);
    }
    free(soft);
}

void
ToriRS_Soft3D_Init(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Soft3DScratch* scratch;

    assert(soft);
    assert(scene);
    assert(pixels);
    assert(width > 0 && height > 0);
    /* New's, and the only thing that survives the reset -- an Init that threw
     * the outline cache away every frame would be the cache never existing. */
    assert(soft->scratch);

    scratch = soft->scratch;
    memset(soft, 0, sizeof(*soft));
    soft->scratch = scratch;
    soft->scene = scene;
    /* Taking it is also checking it: ToriDraw_KernelTake validates the table
     * against this scene and provisions the scratch its three stages need, so
     * "I selected the presorting painter and did not get it" is a line on
     * stderr here rather than a shape in a profile later. */
    soft->kernel = ToriDraw_KernelTake(scene, ToriDraw_KernelGetStock());
    soft3d_dbg_frame_ab_kernels_init(soft);
    soft->pixels = pixels;
    soft->width = width;
    soft->height = height;
    soft->stride = width;
    soft->layout_w = width;
    soft->layout_h = height;
    soft->scaled = false;
}

void
ToriRS_Soft3D_SetLayout(
    struct ToriRS_Soft3D* soft,
    int layout_w,
    int layout_h)
{
    assert(soft);
    assert(layout_w > 0);
    assert(layout_h > 0);
    soft->layout_w = layout_w;
    soft->layout_h = layout_h;
    soft->scaled = layout_w != soft->width || layout_h != soft->height;
}

int*
ToriRS_Soft3D_LayerBegin(
    struct ToriRS_Soft3D* soft,
    int x0,
    int y0,
    int x1,
    int y1)
{
    assert(soft);
    return soft3d_layer_begin(soft, x0, y0, x1, y1);
}

void
ToriRS_Soft3D_LayerEnd(struct ToriRS_Soft3D* soft)
{
    assert(soft);
    soft3d_layer_end(soft);
    ToriDraw2D_FontSetOutputScale(1, 1, 1, 1);
}

void
ToriRS_Soft3D_SetPick(
    struct ToriRS_Soft3D* soft,
    int mouse_x,
    int mouse_y)
{
    assert(soft);
    soft->pick_enabled = true;
    /* A layout point, tested against a world drawn at the buffer's pixels. */
    soft->pick_mouse_x = soft3d_mx(soft, mouse_x);
    soft->pick_mouse_y = soft3d_my(soft, mouse_y);
    ToriRS_PickHitsReset(&soft->pick_hits);
}

void
ToriRS_Soft3D_Execute(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd)
{
    assert(soft);
    assert(cmd);

    switch( cmd->kind )
    {
    case TORIRSRC_BEGIN_3D:
        soft->has_3d = true;
        soft->view_port_3d = cmd->u.begin_3d.view_port;
        soft->camera_3d = cmd->u.begin_3d.camera;
        if( soft->scaled )
            soft3d_scale_world(soft);
        ToriDraw_ScenePrepareProjectionCamera(soft->scene, &soft->camera_3d);
        if( soft->view_port_3d.stride <= 0 || soft->scaled )
            soft->view_port_3d.stride = soft->stride;
        break;

    case TORIRSRC_END_3D:
        ToriDraw_SceneClearProjectionCamera(soft->scene);
        soft->has_3d = false;
        break;

    case TORIRSRC_BEGIN_2D:
    case TORIRSRC_END_2D:
        break;

    case TORIRSRC_CLEAR_RECT:
        soft3d_draw_clear_rect(soft, &cmd->u.clear_rect);
        break;

    case TORIRSRC_FILL_RECT:
        soft3d_draw_fill_rect(soft, &cmd->u.fill_rect);
        break;

    case TORIRSRC_DRAW_MODEL:
        soft3d_draw_model(soft, &cmd->u.model);
        break;

    case TORIRSRC_DRAW_MODEL_WIDGET:
        soft3d_draw_model_widget(soft, &cmd->u.model_widget);
        break;

    case TORIRSRC_SPRITE:
        soft3d_draw_sprite(soft, &cmd->u.sprite);
        break;

    case TORIRSRC_FONT:
        soft3d_draw_font(soft, &cmd->u.font);
        break;

    case TORIRSRC_LINE:
        soft3d_draw_line(soft, &cmd->u.line);
        break;

    case TORIRSRC_POLYGON_BEGIN:
        soft->polygon = cmd->u.polygon_begin;
        soft->polygon_count = 0;
        soft->polygon_open = 1;
        break;

    case TORIRSRC_POLYGON_POINT:
        /* Points past the cap are dropped rather than growing the run: the
         * cap is far above any highlight, so hitting it means something is
         * wrong upstream, and a dropped tail distorts the shape less than a
         * wrapped write would destroy memory. */
        if( soft->polygon_open && soft->polygon_count < TORIRS_POLYGON_MAX_POINTS )
        {
            soft->polygon_x[soft->polygon_count] = soft3d_mx(soft, cmd->u.polygon_point.x);
            soft->polygon_y[soft->polygon_count] = soft3d_my(soft, cmd->u.polygon_point.y);
            soft->polygon_count++;
        }
        break;

    case TORIRSRC_POLYGON_END:
        soft3d_polygon_end(soft);
        break;

    case TORIRSRC_MODEL_LOAD:
    case TORIRSRC_MODEL_UNLOAD:
    case TORIRSRC_ANIM_LOAD:
    case TORIRSRC_ANIM_UNLOAD:
    case TORIRSRC_TEX_LOAD:
    case TORIRSRC_TEX_UNLOAD:
    case TORIRSRC_SPRITE_LOAD:
    case TORIRSRC_SPRITE_UNLOAD:
    case TORIRSRC_FONT_LOAD:
    case TORIRSRC_FONT_UNLOAD:
    case TORIRSRC_BATCH3D_BEGIN:
    case TORIRSRC_BATCH3D_MODEL_ADD:
    case TORIRSRC_BATCH3D_ANIM_ADD:
    case TORIRSRC_BATCH3D_END:
    case TORIRSRC_BATCH3D_CLEAR:
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
    case TORIRSRC_NONE:
        break;
    }
}

/* True for the command kinds that put pixels in the framebuffer, as opposed to
 * state transitions and resource loads. Kept beside the dispatcher's switch so
 * the two cannot drift apart; the NOCHROME ablation is what reads it. */
static int
soft3d_cmd_is_draw(enum ToriRS_RenderCommandKind kind)
{
    switch( kind )
    {
    case TORIRSRC_DRAW_MODEL:
    case TORIRSRC_DRAW_MODEL_WIDGET:
    case TORIRSRC_SPRITE:
    case TORIRSRC_FONT:
    case TORIRSRC_LINE:
    case TORIRSRC_CLEAR_RECT:
    case TORIRSRC_FILL_RECT:
    case TORIRSRC_POLYGON_BEGIN:
    case TORIRSRC_POLYGON_POINT:
    case TORIRSRC_POLYGON_END:
        return 1;
    default:
        return 0;
    }
}

/* `ToriRS_Soft3D_Execute` under a per-class timer, so the one opaque `render`
 * bracket splits into world models, sprites, glyphs and rectangles. The classes
 * are disjoint and exhaustive.
 *
 * This is how `render` was attributed: 85.5% of it is `r_model`, i.e. 64% of
 * the whole i686 frame, against 4.4% for sprite blitting. It is the gate for
 * the R1-R4 targets in docs/CS2_OPTIMIZATION_TARGETS.md.
 *
 * Two clock reads per command is not free when perf is enabled; `r_cmds` is
 * the divisor that says how much was added. Read the split as a ratio between
 * the classes. With perf off this costs one predicted branch per command. */
static void
soft3d_execute_measured(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd)
{
    assert(soft);
    assert(cmd);

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS, 1);
    switch( cmd->kind )
    {
    case TORIRSRC_DRAW_MODEL:
    case TORIRSRC_DRAW_MODEL_WIDGET:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_MODEL, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_MODEL)
        {
            ToriRS_Soft3D_Execute(soft, cmd);
        }
        return;
    case TORIRSRC_SPRITE:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_SPRITE, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_SPRITE)
        {
            ToriRS_Soft3D_Execute(soft, cmd);
        }
        return;
    case TORIRSRC_FONT:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_FONT, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_FONT)
        {
            ToriRS_Soft3D_Execute(soft, cmd);
        }
        return;
    case TORIRSRC_CLEAR_RECT:
    case TORIRSRC_FILL_RECT:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_RECT, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_RECT)
        {
            ToriRS_Soft3D_Execute(soft, cmd);
        }
        return;
    default:
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_OTHER)
        {
            ToriRS_Soft3D_Execute(soft, cmd);
        }
        return;
    }
}

/*
 * Clear every frame. A census on the pinned bench found only 503 of 384,795
 * pixels still holding the clear colour at end of frame, which argued for
 * clearing once -- but that bench has no skybox, and a skybox that does not
 * cover every pixel shows whatever the clear would have removed. The bench
 * could not falsify the premise, so the saving was withdrawn. What stays is
 * the non-temporal clear below, which makes the clear cheaper without skipping
 * it.
 */

#if defined(__APPLE__)

static void
soft3d_clear_framebuffer(struct ToriRS_Soft3D* soft)
{
    uint32_t bg = SOFT3D_DBG_CLEAR_COLOUR;

    assert(soft);
    assert(soft->pixels);

    memset_pattern4(soft->pixels, &bg, (size_t)soft->width * (size_t)soft->height * sizeof(int));
}

#else

/* Ordinary stores, four to the iteration -- the alternative both clears below
 * are weighed against. TORIDRAW_FB_CLEAR32 is the non-temporal one. */
static void
soft3d_clear_run_plain(
    uint32_t* p,
    size_t n,
    uint32_t bg)
{
    size_t i = 0;

    assert(p);

    for( ; i + 4 <= n; i += 4 )
    {
        p[i] = bg;
        p[i + 1] = bg;
        p[i + 2] = bg;
        p[i + 3] = bg;
    }
    for( ; i < n; i++ )
        p[i] = bg;
}

/*
 * 765x503x4 = 1.54 MB written every frame and never read back in this pass --
 * long, contiguous, aligned, write-only. That is the one shape in this
 * renderer where a non-temporal store pays: measured on the Pentium 4 target,
 * 1.296 GB/s normal against 3.060 GB/s non-temporal, so 1.19 ms of clear
 * against 0.50 ms.
 *
 * It is emphatically NOT the shape of a rasterizer span. The same probe
 * measured the same two sequences at the census's real span length of 7.24
 * pixels and found the non-temporal version NINE TIMES slower, because a
 * write-combine buffer evicted before it fills goes out as several
 * partial-line transactions. The kernels keep their ordinary stores; see
 * graphics/fb_clear_i686.S.
 */
static void
soft3d_clear_framebuffer(struct ToriRS_Soft3D* soft)
{
    uint32_t* p;
    uint32_t bg = SOFT3D_DBG_CLEAR_COLOUR;
    size_t n;

    assert(soft);
    assert(soft->pixels);

    p = (uint32_t*)soft->pixels;
    n = (size_t)soft->width * (size_t)soft->height;

    if( soft3d_dbg_full_clear_nt() )
        TORIDRAW_FB_CLEAR32(p, n, bg);
    else
        soft3d_clear_run_plain(p, n, bg);
}

#endif

static void
soft3d_run_commands(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand cmd;

    assert(soft);
    assert(frame);

    /* A probe that has to watch or edit the stream drives it itself, so this
     * loop stays the shape it has when nothing is armed: one branch a frame,
     * none per command. */
    if( soft3d_dbg_frame_walk_armed() )
    {
        soft3d_dbg_frame_walk(soft, frame);
        return;
    }
    while( ToriRS_FrameNextCommand(frame, &cmd) )
        soft3d_execute_measured(soft, &cmd);
}

void
ToriRS_Soft3D_RenderFrame(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame)
{
    assert(soft);
    assert(frame);
    assert(soft->pixels);
    assert(soft->width > 0 && soft->height > 0);

    soft3d_dbg_frame_ab_begin(soft);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_CLEAR)
    {
        soft3d_clear_framebuffer(soft);
    }

    soft->has_3d = false;
    /* Text is the one writer that scales inside ToriDraw; its scale is this
     * frame's and ends with it. */
    ToriDraw2D_FontSetOutputScale(soft->width, soft->layout_w, soft->height, soft->layout_h);
    ToriRS_FrameBegin(frame);
    soft3d_run_commands(soft, frame);
    ToriRS_FrameEnd(frame);
    ToriDraw2D_FontSetOutputScale(1, 1, 1, 1);
    SOFT3D_DBG_FB_POISON_SCAN(soft);

    soft3d_dbg_frame_ab_end();
}
