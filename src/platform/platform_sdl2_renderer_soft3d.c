#include "platform/platform_sdl2_renderer_soft3d.h"

#include "perf/torirs_perf.h"
#include "render/torirs_frame.h"

#include "graphics/fb_clear.h"
#include "toridraw.h"
#include "toridraw_2d.h"
#include "toridraw_frame_ab.h"
#include "toridraw_font.h"
#include "toridraw_model_sprite.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Soft3D is stack-allocated and re-Init'd every App_Render, so persistent
 * working buffers live here rather than on the struct. The outline cache is
 * what stops SpriteNewGraphicOutline from calloc/freeing the same chrome
 * icons every frame (idle flamegraphs put it at ~2.5% of samples). Dense
 * SETOBJECT grids with cc_setoutline(1) prefer a pre-baked bordered icon;
 * this cache covers remaining draw-time outline/shadow chrome.
 */
static uint32_t* g_soft3d_scratch;
static size_t g_soft3d_scratch_cap;

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

static struct Soft3DOutlineCacheEntry g_soft3d_outline_cache[256];
static uint64_t g_soft3d_outline_clock;

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
    int right = scissor_x + scissor_w;
    int bottom = scissor_y + scissor_h;

    memset(&vp, 0, sizeof(vp));
    vp.stride = soft->stride;
    vp.clip_left = scissor_x < 0 ? 0 : scissor_x;
    vp.clip_top = scissor_y < 0 ? 0 : scissor_y;
    vp.clip_right = right > soft->width ? soft->width : right;
    vp.clip_bottom = bottom > soft->height ? soft->height : bottom;
    /*
     * Damage clipping rides the same choke point, which is the whole reason it
     * is cheap to add and impossible to forget: a draw kind added later cannot
     * escape it without also escaping the canvas bound above.
     *
     * It only ever SHRINKS a clip, so the worst a wrong damage rect can do is
     * fail to draw -- it can never let a kernel write outside the buffer. A
     * command entirely outside the damage collapses to an empty viewport, which
     * every kernel already treats as a no-op.
     */
    if( soft->damage_valid )
    {
        if( vp.clip_left < soft->damage_x0 )
            vp.clip_left = soft->damage_x0;
        if( vp.clip_top < soft->damage_y0 )
            vp.clip_top = soft->damage_y0;
        if( vp.clip_right > soft->damage_x1 )
            vp.clip_right = soft->damage_x1;
        if( vp.clip_bottom > soft->damage_y1 )
            vp.clip_bottom = soft->damage_y1;
    }
    /* An entirely off-canvas box collapses to empty rather than inverting. */
    if( vp.clip_right < vp.clip_left )
        vp.clip_right = vp.clip_left;
    if( vp.clip_bottom < vp.clip_top )
        vp.clip_bottom = vp.clip_top;
    return vp;
}

static uint32_t*
soft3d_scratch(size_t pixels)
{
    if( pixels == 0 )
        return NULL;
    if( pixels > g_soft3d_scratch_cap )
    {
        uint32_t* grown =
            (uint32_t*)realloc(g_soft3d_scratch, pixels * sizeof(uint32_t));
        if( !grown )
            return NULL;
        g_soft3d_scratch = grown;
        g_soft3d_scratch_cap = pixels;
    }
    return g_soft3d_scratch;
}

static uint32_t*
soft3d_outline_cache_get(
    uint32_t const* src,
    int sw,
    int sh,
    int outline,
    int graphic_shadow,
    int* out_w,
    int* out_h)
{
    int i;
    int victim = 0;
    uint64_t victim_used = UINT64_MAX;
    uint32_t const* outlined;
    uint32_t* final_px;
    int ow = 0;
    int oh = 0;
    int fw = 0;
    int fh = 0;

    assert(out_w);
    assert(out_h);
    g_soft3d_outline_clock++;

    for( i = 0; i < (int)(sizeof(g_soft3d_outline_cache) / sizeof(g_soft3d_outline_cache[0]));
         i++ )
    {
        if( g_soft3d_outline_cache[i].src == src && g_soft3d_outline_cache[i].sw == sw &&
            g_soft3d_outline_cache[i].sh == sh &&
            g_soft3d_outline_cache[i].outline == outline &&
            g_soft3d_outline_cache[i].graphic_shadow == graphic_shadow &&
            g_soft3d_outline_cache[i].pixels )
        {
            g_soft3d_outline_cache[i].last_used = g_soft3d_outline_clock;
            *out_w = g_soft3d_outline_cache[i].w;
            *out_h = g_soft3d_outline_cache[i].h;
            return g_soft3d_outline_cache[i].pixels;
        }
        if( g_soft3d_outline_cache[i].last_used < victim_used )
        {
            victim_used = g_soft3d_outline_cache[i].last_used;
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
        uint32_t* shadowed = ToriDraw_SpriteNewGraphicShadow(
            outlined, ow, oh, graphic_shadow, &fw, &fh);
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

    free(g_soft3d_outline_cache[victim].pixels);
    g_soft3d_outline_cache[victim].src = src;
    g_soft3d_outline_cache[victim].sw = sw;
    g_soft3d_outline_cache[victim].sh = sh;
    g_soft3d_outline_cache[victim].outline = outline;
    g_soft3d_outline_cache[victim].graphic_shadow = graphic_shadow;
    g_soft3d_outline_cache[victim].pixels = final_px;
    g_soft3d_outline_cache[victim].w = ow;
    g_soft3d_outline_cache[victim].h = oh;
    g_soft3d_outline_cache[victim].last_used = g_soft3d_outline_clock;
    *out_w = ow;
    *out_h = oh;
    return final_px;
}

static uint32_t*
soft3d_clamp_to_nominal(
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
    dst = soft3d_scratch(n);
    if( !dst )
        return NULL;
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

/* --- sprite opacity census (TORIRS_SPRITE_CENSUS=1) ---------------------- */
static double g_spr_opaque_n, g_spr_opaque_px, g_spr_mixed_n, g_spr_mixed_px;

static void
soft3d_sprite_census_dump(void)
{
    double n = g_spr_opaque_n + g_spr_mixed_n;
    double px = g_spr_opaque_px + g_spr_mixed_px;
    if( n <= 0.0 )
        return;
    TORIRS_REPORT(
        "\n=== sprite opacity census ===\n"
        "all-opaque : %10.0f blits (%5.1f%%)  %12.0f px (%5.1f%%)\n"
        "mixed      : %10.0f blits (%5.1f%%)  %12.0f px (%5.1f%%)\n",
        g_spr_opaque_n, 100.0 * g_spr_opaque_n / n, g_spr_opaque_px,
        px > 0 ? 100.0 * g_spr_opaque_px / px : 0.0,
        g_spr_mixed_n, 100.0 * g_spr_mixed_n / n, g_spr_mixed_px,
        px > 0 ? 100.0 * g_spr_mixed_px / px : 0.0);
}

static int
soft3d_sprite_census_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        armed = getenv("TORIRS_SPRITE_CENSUS") ? 1 : 0;
        if( armed )
            atexit(soft3d_sprite_census_dump);
    }
    return armed;
}

static void
soft3d_sprite_census_note(int opaque, int px)
{
    if( opaque )
    {
        g_spr_opaque_n += 1.0;
        g_spr_opaque_px += (double)px;
    }
    else
    {
        g_spr_mixed_n += 1.0;
        g_spr_mixed_px += (double)px;
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

    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);

    /* Chrome rotated by camera yaw (compass, minimap, scrollbar arrows): inverse-map
     * the destination box through the anchor pair instead of growing a pixel buffer.
     * Units here are 0..2047, not the IF3 spriteAngle scale used further down. */
    if( cmd->rotated )
    {
        struct ToriDraw_Sprite* mask_spr = NULL;
        if( cmd->mask_scene_id > 0 )
        {
            int mask_count = 0;
            struct ToriDraw_Sprite** mask_sprites =
                ToriDraw_SceneSpriteGet(soft->scene, cmd->mask_scene_id, &mask_count);
            if( mask_sprites && cmd->mask_atlas_index >= 0 &&
                cmd->mask_atlas_index < mask_count )
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
        return;
    }

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
            ToriDraw2D_BlitArgbTiledAlpha(
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
                alpha,
                soft->pixels);
            return;
        }
        if( !cmd->if3 )
        {
            /* TORIRS_SPRITE_CENSUS=1: how often does the opaque precondition
             * actually hold, and over how much area? A fast path is worth only
             * what its precondition is worth, and that has to be counted rather
             * than assumed -- this one measured as no change at all. */
            if( soft3d_sprite_census_armed() )
                soft3d_sprite_census_note(
                    ToriDraw_SpriteAlphaClass(spr) == TORIDRAW_SPRITE_ALPHA_ALL_OPAQUE,
                    sw * sh);
            ToriDraw2D_BlitArgbAlpha(
                &vp,
                cmd->x + ox,
                cmd->y + oy,
                spr->pixels_argb,
                sw,
                sh,
                alpha,
                soft->pixels);
            return;
        }
        /* if3 scales the *nominal* box, so a crop offset is only skippable when
         * the sprite already sits at that box's origin — otherwise the offset
         * would have to scale with it and the general path has to run. */
        if( ox == 0 && oy == 0 )
        {
            int draw_w = cmd->w > 0 ? cmd->w : sw;
            int draw_h = cmd->h > 0 ? cmd->h : sh;
            ToriDraw2D_BlitArgbScaledAlpha(
                &vp,
                cmd->x,
                cmd->y,
                draw_w,
                draw_h,
                spr->pixels_argb,
                sw,
                sh,
                alpha,
                soft->pixels);
            return;
        }
    }

    /*
     * Outlined/shadowed icons with no further pixel mutation: serve from the
     * process-lifetime LRU so idle chrome stops calloc/freeing every frame.
     */
    if( (cmd->outline > 0 || cmd->graphic_shadow != 0) && cmd->trans <= 0 &&
        !cmd->flip_h && !cmd->flip_v && cmd->sprite_angle_r2pi65536 == 0 && !cmd->tiled )
    {
        int cw = 0;
        int ch = 0;
        uint32_t* cached = soft3d_outline_cache_get(
            spr->pixels_argb,
            sw,
            sh,
            cmd->outline,
            cmd->graphic_shadow,
            &cw,
            &ch);
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
                    ToriDraw2D_BlitArgbScaled(
                        &vp, cmd->x, cmd->y, draw_w, draw_h, cached, cw, ch, soft->pixels);
                    return;
                }
            }
            else
            {
                ToriDraw2D_BlitArgb(
                    &vp, cmd->x + cox, cmd->y + coy, cached, cw, ch, soft->pixels);
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
                soft3d_clamp_to_nominal(spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
            if( clamped )
            {
                /* clamp writes into the process scratch — copy out so the
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
            ToriDraw2D_BlitArgbScaled(
                &vp, draw_x, draw_y, draw_w, draw_h, spr_px, sw, sh, soft->pixels);
        }
    }
    else
    {
        ToriDraw_SpriteTransformPixels(
            &spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, angle_2d);

        if( cmd->tiled )
        {
            ToriDraw2D_BlitArgbTiled(
                &vp,
                cmd->x,
                cmd->y,
                cmd->w,
                cmd->h,
                spr_px,
                sw,
                sh,
                cmd->x + ox,
                cmd->y + oy,
                soft->pixels);
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
            ToriDraw2D_BlitArgb(&vp, draw_x, draw_y, spr_px, sw, sh, soft->pixels);
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

    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
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
            cmd->shadowed != 0,
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
        cmd->shadowed != 0,
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
    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    x0 = cmd->x;
    y0 = cmd->y;
    x1 = cmd->x + cmd->w;
    y1 = cmd->y + cmd->h;
    if( cmd->filled )
        ToriDraw2D_FillRect(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
    else
        ToriDraw2D_DrawRectOutline(&vp, x0, y0, x1, y1, cmd->argb, soft->pixels);
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
    vp = viewport_from_scissor(soft, 0, 0, soft->width, soft->height);
    x0 = cmd->x;
    y0 = cmd->y;
    x1 = cmd->x + cmd->w;
    y1 = cmd->y + cmd->h;
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

    cx = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_x : 0;
    cy = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_y : 0;
    cw = soft->polygon.scissor_w > 0 ? soft->polygon.scissor_w : soft->width;
    ch = soft->polygon.scissor_h > 0 ? soft->polygon.scissor_h : soft->height;

    ToriRS_PolygonFillConvex(
        soft->polygon_x, soft->polygon_y, soft->polygon_count, cx, cy, cw, ch,
        soft3d_polygon_span, &ctx);
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
    vp = viewport_from_scissor(
        soft, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
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
    if( cmd->model.kind != TORIDRAWMK_MODEL || !cmd->model.u.model.model )
        return;

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
}

/* TORIRS_DRAW_TRACE=<min_vertex_count>, 0/unset = off. Vertex count rather than
 * an element id because element ids change every time the entity respawns and
 * the model that matters here is the largest thing in the scene. */
/* Edge-triggered: remember the last verdict so the trace can be left on for a
 * whole session and still only speak when something changes. Per-frame logging
 * is what made the first version unusable -- the volume buried the one frame
 * that mattered. */
static int g_draw_trace_last_cull = -999;
static int g_draw_trace_last_sorted = -999;
static int g_draw_trace_drawn_frames = 0;

static int
draw_trace_min_vertices(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* v = getenv("TORIRS_DRAW_TRACE");
        cached = (v && *v) ? atoi(v) : 0;
    }
    return cached;
}

/* See the two arms in soft3d_draw_model. Read once; off is a predicted branch. */
static int
soft3d_abl_noraster(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NORASTER") ? 1 : 0;
    return armed;
}

static int
soft3d_abl_nomodels(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOMODELS") ? 1 : 0;
    return armed;
}

static void
soft3d_draw_model(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand_Model const* cmd)
{
    struct ToriDraw_Position position;
    int cull;
    int trace_min;
    int trace_this;

    assert(soft);
    assert(cmd);
    if( !soft->has_3d )
        return;
    if( cmd->model.kind == TORIDRAWMK_NONE )
        return;

    /* ABLATION (TORIRS_ABL_NOMODELS=1, measurement only): drop the whole 3D
     * model pass -- projection, hittest, face sort and raster alike.
     *
     * With TORIRS_ABL_NORASTER below, this decomposes `render` by deletion
     * rather than by instrumentation. TORIRS_PERF is ~69% of the frame on this
     * box, so its own split of r_project / r_sort / r_raster cannot be read as
     * absolute time; three runs of a build that simply does less can. */
    if( soft3d_abl_nomodels() )
        return;

    if( cmd->animation && cmd->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            soft->scene, cmd->element_id, cmd->anim_index == 0, cmd->anim_frame);

    /*
     * TORIRS_DRAW_TRACE=<min_vertex_count>: per-frame, unsampled, why a big
     * model did or did not rasterize.
     *
     * Built for "I can still mouse over and click the Queen but nothing is
     * drawn". That symptom localises here and nowhere else, because the pick
     * below runs BEFORE the face sort: a model that projects VISIBLE and then
     * sorts to zero faces stays fully clickable and paints nothing. The
     * TORIDRAW_SORT_DEBUG/NDJSON counters answer the same question but are
     * gated and sampled, so they miss the transition that causes it.
     *
     * cull   the projection verdict (0 = TORIDRAW_CULL_VISIBLE).
     * sorted faces surviving the depth/priority sort; 0 here with cull=0 is
     *        exactly the invisible-but-clickable state.
     */
    trace_min = draw_trace_min_vertices();
    trace_this = trace_min > 0 && cmd->model.kind == TORIDRAWMK_MODEL &&
                 cmd->model.u.model.model &&
                 cmd->model.u.model.model->vertex_count >= trace_min;

    position = cmd->position;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_PROJECT)
    {
        cull = ToriDraw_RenderModel1Project(
            cmd->model, soft->scene, &position, &soft->view_port_3d, &soft->camera_3d);
    }
    if( trace_this && cull != g_draw_trace_last_cull )
    {
        struct ToriDraw_Model const* m = cmd->model.u.model.model;
        struct ToriDraw_BoundsCylinder const* bc = m->bounds_cylinder;
        TORIRS_LOG("draw_trace: element=%d vc=%d faces=%d CULL %d -> %d (0=visible) pos=(%d,%d,%d) "
            "radius=%d min_y=%d max_y=%d bias=%d after %d drawn frames\n",
            cmd->element_id, m->vertex_count, m->face_count, g_draw_trace_last_cull, (int)cull,
            position.x, position.y, position.z, bc ? bc->radius : -1, bc ? bc->min_y : 0,
            bc ? bc->max_y : 0, bc ? bc->min_z_depth_any_rotation : -1,
            g_draw_trace_drawn_frames);
        g_draw_trace_last_cull = (int)cull;
        g_draw_trace_drawn_frames = 0;
    }
    if( trace_this && cull != TORIDRAW_CULL_VISIBLE )
        g_draw_trace_drawn_frames++;
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_MODEL_CULLED, 1);
        return;
    }

    /* Hittest before the face sort: the scene scratch holds this model's
     * projection only until the next model projects, and a model whose faces
     * all sort away must still pick. */
    if( soft->pick_enabled && cmd->pickable && cmd->element_id >= 0 &&
        (cmd->pick_aabb   ? ToriDraw_ProjectedModelContainsAabb(
                              soft->scene, soft->pick_mouse_x, soft->pick_mouse_y)
         : cmd->pick_terrain ? ToriDraw_ProjectedTileMouseHitTest(
                                   soft->scene, cmd->model, &soft->view_port_3d,
                                   soft->pick_mouse_x, soft->pick_mouse_y)
                             : ToriDraw_ProjectedModelMouseHitTest(
                                   soft->scene, cmd->model, &soft->view_port_3d,
                                   soft->pick_mouse_x, soft->pick_mouse_y)) )
        ToriRS_PickHitsAdd(
            &soft->pick_hits,
            cmd->element_id,
            cmd->pick_terrain,
            cmd->pick_tile_x,
            cmd->pick_tile_z,
            cmd->pick_tile_level);

    if( cmd->pick_only )
        return;
    {
        int sorted = 0;
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_SORT)
        {
            sorted = ToriDraw_RenderModel2SortFaces(cmd->model, soft->scene);
        }
        /* Only the transitions matter: went-to-zero is the invisible-but-
         * clickable state, came-back is the recovery. A drifting face count on
         * a model that keeps drawing is noise. */
        if( trace_this && ((sorted <= 0) != (g_draw_trace_last_sorted <= 0)) )
        {
            TORIRS_LOG("draw_trace: element=%d SORTED %d -> %d %s after %d frames\n",
                cmd->element_id, g_draw_trace_last_sorted, sorted,
                sorted <= 0 ? "(RASTERIZES NOTHING - invisible but still clickable)"
                            : "(drawing again)",
                g_draw_trace_drawn_frames);
            g_draw_trace_drawn_frames = 0;
        }
        g_draw_trace_last_sorted = sorted;
        g_draw_trace_drawn_frames++;
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
    }
    /* ABLATION (TORIRS_ABL_NORASTER=1, measurement only): keep the projection,
     * the hittest and the face sort; write no pixels. The difference against
     * the baseline is what rasterisation actually costs, and the difference
     * against TORIRS_ABL_NOMODELS is what deciding-what-to-draw costs. */
    if( soft3d_abl_noraster() )
        return;

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_RASTER)
    {
        ToriDraw_RenderModel3Raster(
            soft->scene, &soft->view_port_3d, &soft->camera_3d, soft->pixels, false);
    }
}

void
ToriRS_Soft3D_Init(
    struct ToriRS_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int width,
    int height)
{
    assert(soft);
    assert(scene);
    assert(pixels);
    assert(width > 0 && height > 0);
    memset(soft, 0, sizeof(*soft));
    soft->scene = scene;
    soft->pixels = pixels;
    soft->width = width;
    soft->height = height;
    soft->stride = width;
}

/* A/B for the damage clear's store kind; see the call site. Read once. */
static int
soft3d_damage_clear_nt(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE_CLEAR_PLAIN") ? 0 : 1;
    return armed;
}

void
ToriRS_Soft3D_SetDamage(
    struct ToriRS_Soft3D* soft,
    int x,
    int y,
    int w,
    int h)
{
    assert(soft);
    assert(w > 0);
    assert(h > 0);

    soft->damage_x0 = x < 0 ? 0 : x;
    soft->damage_y0 = y < 0 ? 0 : y;
    soft->damage_x1 = x + w > soft->width ? soft->width : x + w;
    soft->damage_y1 = y + h > soft->height ? soft->height : y + h;
    soft->damage_valid =
        (soft->damage_x1 > soft->damage_x0 && soft->damage_y1 > soft->damage_y0);
}

void
ToriRS_Soft3D_SetPick(
    struct ToriRS_Soft3D* soft,
    int mouse_x,
    int mouse_y)
{
    assert(soft);
    soft->pick_enabled = true;
    soft->pick_mouse_x = mouse_x;
    soft->pick_mouse_y = mouse_y;
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
        ToriDraw_ScenePrepareProjectionCamera(soft->scene, &soft->camera_3d);
        if( soft->view_port_3d.stride <= 0 )
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
            soft->polygon_x[soft->polygon_count] = cmd->u.polygon_point.x;
            soft->polygon_y[soft->polygon_count] = cmd->u.polygon_point.y;
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

/*
 * Pixel ownership: which draw command last wrote each pixel of a rect.
 *
 * TORIRS_PIXOWNER=x0,x1,y0,y1[,RRGGBB] snapshots the rect after every command
 * and attributes the pixels that changed to that command. Filtering by a final
 * colour answers the question a screenshot cannot — "what is painting THIS" —
 * naming the loc id or terrain tile rather than leaving the reader to guess
 * from a draw-order dump. Written to TORIRS_PIXOWNER_OUT (default stderr) at
 * frame TORIRS_PIXOWNER_AT (default: the last frame rendered).
 *
 * O(commands x rect), so keep the rect small; it is inert unless armed and the
 * unarmed render loop is untouched.
 */
struct PixOwnerRec
{
    int cmd_index;
    int kind;      /* enum ToriRS_RenderCommandKind */
    int element_id;
    int loc_id;
    int terrain;   /* 1 = terrain tile */
    int tile_x, tile_z, tile_level;
    int world_x, world_y, world_z;
};

static int g_pixowner_armed = -1;
static int g_pixowner_rect[4];
static int g_pixowner_want_colour = -1;
static long g_pixowner_at = -1;
static long g_pixowner_frame;
static uint32_t* g_pixowner_prev;
static struct PixOwnerRec* g_pixowner_owner; /* one per rect pixel */
static int g_pixowner_cmd_index;
static int g_pixowner_active; /* this frame is the one being recorded */

static int
soft3d_pixowner_armed(void)
{
    if( g_pixowner_armed < 0 )
    {
        char const* env = getenv("TORIRS_PIXOWNER");
        char const* at = getenv("TORIRS_PIXOWNER_AT");
        char colour[16] = { 0 };
        g_pixowner_armed = 0;
        if( env && env[0] )
        {
            int got = sscanf(env, "%d,%d,%d,%d,%15s", &g_pixowner_rect[0], &g_pixowner_rect[1],
                             &g_pixowner_rect[2], &g_pixowner_rect[3], colour);
            if( got >= 4 )
            {
                g_pixowner_armed = 1;
                if( got >= 5 && colour[0] )
                    g_pixowner_want_colour = (int)strtol(colour, NULL, 16);
            }
        }
        g_pixowner_at = (at && at[0]) ? strtol(at, NULL, 0) : -1;
    }
    return g_pixowner_armed;
}

static void
soft3d_pixowner_begin(struct ToriRS_Soft3D* soft)
{
    int rect_w = g_pixowner_rect[1] - g_pixowner_rect[0] + 1;
    int rect_h = g_pixowner_rect[3] - g_pixowner_rect[2] + 1;
    size_t count;

    g_pixowner_frame++;
    /* -1 = "the last frame": record every frame and let the final one win. */
    g_pixowner_active = (g_pixowner_at < 0 || g_pixowner_frame == g_pixowner_at);
    g_pixowner_cmd_index = 0;
    if( !g_pixowner_active || rect_w <= 0 || rect_h <= 0 )
        return;
    count = (size_t)rect_w * (size_t)rect_h;
    if( !g_pixowner_prev )
    {
        g_pixowner_prev = calloc(count, sizeof(*g_pixowner_prev));
        g_pixowner_owner = calloc(count, sizeof(*g_pixowner_owner));
    }
    if( !g_pixowner_prev || !g_pixowner_owner )
        return;
    for( size_t i = 0; i < count; i++ )
    {
        g_pixowner_owner[i].cmd_index = -1;
        g_pixowner_owner[i].element_id = -1;
        g_pixowner_owner[i].loc_id = -1;
    }
    (void)soft;
}

static void
soft3d_pixowner_after_command(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd)
{
    int x0 = g_pixowner_rect[0], x1 = g_pixowner_rect[1];
    int y0 = g_pixowner_rect[2], y1 = g_pixowner_rect[3];
    int rect_w = x1 - x0 + 1;
    int index = g_pixowner_cmd_index++;

    if( !g_pixowner_active || !g_pixowner_prev || !g_pixowner_owner )
        return;
    if( x1 >= soft->width )
        x1 = soft->width - 1;
    if( y1 >= soft->height )
        y1 = soft->height - 1;

    for( int y = y0; y <= y1; y++ )
    {
        for( int x = x0; x <= x1; x++ )
        {
            size_t slot = (size_t)(y - y0) * (size_t)rect_w + (size_t)(x - x0);
            uint32_t now = (uint32_t)soft->pixels[y * soft->stride + x] & 0xFFFFFFu;
            if( now == g_pixowner_prev[slot] )
                continue;
            g_pixowner_prev[slot] = now;
            g_pixowner_owner[slot].cmd_index = index;
            g_pixowner_owner[slot].kind = (int)cmd->kind;
            if( cmd->kind == TORIRSRC_DRAW_MODEL )
            {
                g_pixowner_owner[slot].element_id = cmd->u.model.element_id;
                g_pixowner_owner[slot].terrain = cmd->u.model.pick_terrain ? 1 : 0;
                g_pixowner_owner[slot].tile_x = cmd->u.model.pick_tile_x;
                g_pixowner_owner[slot].tile_z = cmd->u.model.pick_tile_z;
                g_pixowner_owner[slot].tile_level = cmd->u.model.pick_tile_level;
                g_pixowner_owner[slot].world_x = cmd->u.model.world_position.x;
                g_pixowner_owner[slot].world_y = cmd->u.model.world_position.y;
                g_pixowner_owner[slot].world_z = cmd->u.model.world_position.z;
                /* No loc id here: the renderer has no World to resolve an
                 * element through. `cmd=` indexes the same stream
                 * TORIRS_DRAW_ORDER prints, which carries the loc id — that is
                 * the join, and it keeps this probe free of a world lookup. */
                g_pixowner_owner[slot].loc_id = -1;
            }
            else
            {
                g_pixowner_owner[slot].element_id = -1;
                g_pixowner_owner[slot].loc_id = -1;
                g_pixowner_owner[slot].terrain = 0;
            }
        }
    }
}

static void
soft3d_pixowner_end(void)
{
    int x0 = g_pixowner_rect[0], x1 = g_pixowner_rect[1];
    int y0 = g_pixowner_rect[2], y1 = g_pixowner_rect[3];
    int rect_w = x1 - x0 + 1;
    int rect_h = y1 - y0 + 1;
    char const* out_path;
    FILE* out;

    if( !g_pixowner_active || !g_pixowner_prev || !g_pixowner_owner )
        return;

    /* With no TORIRS_PIXOWNER_AT every frame is recorded and every frame
     * prints; the file is opened "w" so the last frame is what survives, which
     * is the usual want. Point _OUT at a file rather than reading stderr. */
    out_path = getenv("TORIRS_PIXOWNER_OUT");
    out = (out_path && out_path[0]) ? fopen(out_path, "w") : stderr;
    if( !out )
        out = stderr;

    fprintf(out, "# pixel owners, rect x%d..%d y%d..%d, frame %ld\n", x0, x1, y0, y1,
            g_pixowner_frame);
    if( g_pixowner_want_colour >= 0 )
        fprintf(out, "# filtered to colour %06x\n", (unsigned)g_pixowner_want_colour);
    fprintf(out, "# colour cmd kind elem loc terrain tile pixels\n");
    {
        /* Aggregate: one row per (owner, colour), sorted by pixel count. */
        struct Agg
        {
            uint32_t colour;
            struct PixOwnerRec rec;
            int pixels;
        };
        struct Agg* agg = calloc((size_t)rect_w * (size_t)rect_h, sizeof(*agg));
        int agg_count = 0;
        assert(agg);
        for( int i = 0; i < rect_w * rect_h; i++ )
        {
            uint32_t colour = g_pixowner_prev[i];
            struct PixOwnerRec* rec = &g_pixowner_owner[i];
            int found = -1;
            if( rec->cmd_index < 0 )
                continue;
            if( g_pixowner_want_colour >= 0 && colour != (uint32_t)g_pixowner_want_colour )
                continue;
            for( int a = 0; a < agg_count && found < 0; a++ )
                if( agg[a].colour == colour && agg[a].rec.cmd_index == rec->cmd_index )
                    found = a;
            if( found < 0 )
            {
                found = agg_count++;
                agg[found].colour = colour;
                agg[found].rec = *rec;
            }
            agg[found].pixels++;
        }
        for( int a = 0; a < agg_count; a++ )
        {
            int best = a;
            for( int b = a + 1; b < agg_count; b++ )
                if( agg[b].pixels > agg[best].pixels )
                    best = b;
            if( best != a )
            {
                struct Agg tmp = agg[a];
                agg[a] = agg[best];
                agg[best] = tmp;
            }
            fprintf(out, "%06x cmd=%d kind=%d elem=%d loc=%d %s", (unsigned)agg[a].colour,
                    agg[a].rec.cmd_index, agg[a].rec.kind, agg[a].rec.element_id,
                    agg[a].rec.loc_id, agg[a].rec.terrain ? "TERRAIN" : "loc");
            if( agg[a].rec.terrain )
                fprintf(out, " tile=%d,%d L%d", agg[a].rec.tile_x, agg[a].rec.tile_z,
                        agg[a].rec.tile_level);
            else
                fprintf(out, " wpos=%d,%d,%d", agg[a].rec.world_x, agg[a].rec.world_y,
                        agg[a].rec.world_z);
            fprintf(out, " pixels=%d\n", agg[a].pixels);
        }
        free(agg);
    }
    if( out != stderr )
        fclose(out);
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
/* ABLATION SUPPORT (measurement only) -- see the TORIRS_ABL_NOCHROME arm in
 * ToriRS_Soft3D_RenderFrame. Read once; off is one predicted branch. */
static int
soft3d_abl_nochrome(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOCHROME") ? 1 : 0;
    return armed;
}


/* True for the command kinds that put pixels in the framebuffer, as opposed to
 * state transitions and resource loads. Kept beside the dispatcher's switch so
 * the two cannot drift apart. */
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
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_MODEL) { ToriRS_Soft3D_Execute(soft, cmd); }
        return;
    case TORIRSRC_SPRITE:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_SPRITE, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_SPRITE) { ToriRS_Soft3D_Execute(soft, cmd); }
        return;
    case TORIRSRC_FONT:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_FONT, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_FONT) { ToriRS_Soft3D_Execute(soft, cmd); }
        return;
    case TORIRSRC_CLEAR_RECT:
    case TORIRSRC_FILL_RECT:
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_R_CMDS_RECT, 1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_RECT) { ToriRS_Soft3D_Execute(soft, cmd); }
        return;
    default:
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_OTHER) { ToriRS_Soft3D_Execute(soft, cmd); }
        return;
    }
}

#if defined(TORIDRAW_FB_POISON) && TORIDRAW_FB_POISON
/*
 * Survivor census for the frame clear. Poison is a colour the palette cannot
 * produce, so any pixel still carrying it at end of frame was written by the
 * clear and by nothing else.
 */
#define FB_POISON_VALUE 0xFFDEADBEu

static unsigned long long g_fb_poison_frames;
static unsigned long long g_fb_poison_survivors;
static unsigned long long g_fb_poison_total;
static int g_fb_poison_min_x = 1 << 30;
static int g_fb_poison_max_x = -1;
static int g_fb_poison_min_y = 1 << 30;
static int g_fb_poison_max_y = -1;
static unsigned g_fb_poison_worst;
static unsigned g_fb_poison_best = 0xFFFFFFFFu;
static unsigned g_fb_poison_last;
static unsigned long long g_fb_poison_blank;
static unsigned long long g_fb_poison_counted;
static int g_fb_poison_atexit;

/* Frames to let the scene come up before anything is believed. */
#define FB_POISON_WARMUP 12
static long g_fb_poison_warmup;

static void
fb_poison_dump(void)
{
    const char* path = getenv("TORIDRAW_FB_POISON_FILE");
    FILE* f = path ? fopen(path, "w") : stderr;
    double frames = (double)(g_fb_poison_frames ? g_fb_poison_frames : 1);
    (void)frames;

    assert(f);
    fprintf(f, "fb poison census over %llu frames\n", g_fb_poison_frames);
    fprintf(f, "  pixels cleared per frame: %.0f\n",
            (double)g_fb_poison_total / frames);
    fprintf(f, "  frames counted after %d warmup: %llu (%llu of them drew nothing)\n",
            FB_POISON_WARMUP, g_fb_poison_counted, g_fb_poison_blank);
    fprintf(f, "  survivors/frame: min %u, mean %.0f, max %u; last frame %u\n",
            g_fb_poison_best == 0xFFFFFFFFu ? 0u : g_fb_poison_best,
            (double)g_fb_poison_survivors
                / (double)(g_fb_poison_counted ? g_fb_poison_counted : 1),
            g_fb_poison_worst, g_fb_poison_last);
    if( g_fb_poison_max_x >= 0 )
        fprintf(f, "  survivor bbox: x %d..%d, y %d..%d (%dx%d)\n",
                g_fb_poison_min_x, g_fb_poison_max_x,
                g_fb_poison_min_y, g_fb_poison_max_y,
                g_fb_poison_max_x - g_fb_poison_min_x + 1,
                g_fb_poison_max_y - g_fb_poison_min_y + 1);
    else
        fprintf(f, "  survivor bbox: none -- every pixel was overdrawn\n");
    if( path )
        fclose(f);
}

static void
fb_poison_scan(const struct ToriRS_Soft3D* soft)
{
    const uint32_t* p = (const uint32_t*)soft->pixels;
    unsigned live = 0;
    int y;

    if( !g_fb_poison_atexit )
    {
        g_fb_poison_atexit = 1;
        atexit(fb_poison_dump);
    }
    g_fb_poison_frames++;
    g_fb_poison_total += (unsigned)soft->width * (unsigned)soft->height;

    for( y = 0; y < soft->height; y++ )
    {
        const uint32_t* row = p + (size_t)y * (size_t)soft->width;
        int x;
        for( x = 0; x < soft->width; x++ )
        {
            if( row[x] != FB_POISON_VALUE )
                continue;
            live++;
            if( g_fb_poison_warmup < FB_POISON_WARMUP )
                continue;
            if( x < g_fb_poison_min_x ) g_fb_poison_min_x = x;
            if( x > g_fb_poison_max_x ) g_fb_poison_max_x = x;
            if( y < g_fb_poison_min_y ) g_fb_poison_min_y = y;
            if( y > g_fb_poison_max_y ) g_fb_poison_max_y = y;
        }
    }
    g_fb_poison_last = live;
    if( g_fb_poison_warmup < FB_POISON_WARMUP )
    {
        g_fb_poison_warmup++;
        return;
    }
    g_fb_poison_counted++;
    g_fb_poison_survivors += live;
    if( live > g_fb_poison_worst )
        g_fb_poison_worst = live;
    if( live < g_fb_poison_best )
        g_fb_poison_best = live;
    if( live * 2 >= (unsigned)soft->width * (unsigned)soft->height )
        g_fb_poison_blank++;
}
#endif

void
ToriRS_Soft3D_RenderFrame(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand cmd;
    size_t n;

    assert(soft);
    assert(frame);
    assert(soft->pixels);
    assert(soft->width > 0 && soft->height > 0);

    n = (size_t)soft->width * (size_t)soft->height;

    /*
     * The A/B brackets the clear AND the rasterization that follows it,
     * because the two are coupled through the cache. Arm selection is read
     * once, inside the region, so the clear and the accounting cannot
     * disagree about which arm this frame was.
     */
    ToriDraw_FrameAbBegin();

    /*
     * Clear every frame. A census on the pinned bench found only 503 of
     * 384,795 pixels still holding the clear colour at end of frame, which
     * argued for clearing once -- but that bench has no skybox, and a skybox
     * that does not cover every pixel shows whatever the clear would have
     * removed. The bench could not falsify the premise, so the saving was
     * withdrawn. What stays is the non-temporal clear below, which makes the
     * clear cheaper without skipping it.
     */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_R_CLEAR)
    {
#if defined(__APPLE__)
        uint32_t bg = (uint32_t)TORIRS_SOFT3D_BG;
        memset_pattern4(soft->pixels, &bg, n * sizeof(int));
#else
        /*
         * 765x503x4 = 1.54 MB written every frame and never read back in this
         * pass -- long, contiguous, aligned, write-only. That is the one shape
         * in this renderer where a non-temporal store pays: measured on the
         * Pentium 4 target, 1.296 GB/s normal against 3.060 GB/s
         * non-temporal, so 1.19 ms of clear against 0.50 ms.
         *
         * It is emphatically NOT the shape of a rasterizer span. The same
         * probe measured the same two sequences at the census's real span
         * length of 7.24 pixels and found the non-temporal version NINE TIMES
         * slower, because a write-combine buffer evicted before it fills goes
         * out as several partial-line transactions. The kernels keep their
         * ordinary stores; see graphics/fb_clear_i686.S.
         */
        uint32_t* p = (uint32_t*)soft->pixels;
#if defined(TORIDRAW_FB_POISON) && TORIDRAW_FB_POISON
        uint32_t bg = FB_POISON_VALUE;
#else
        uint32_t bg = (uint32_t)TORIRS_SOFT3D_BG;
#endif
        if( soft->damage_valid )
        {
            /*
             * Row-at-a-time: the damage box is a sub-rectangle, so the one long
             * contiguous run the non-temporal clear wants does not exist.
             *
             * The non-temporal clear keeps its job here, which is not what was
             * expected: a 717-pixel row is 2.8 KB, and the guess was that
             * filling and tearing down the write-combine buffer 335 times a
             * frame would cost more than the NT store saves, since the measured
             * 2.4x for this clear was taken on one long run. Both arms,
             * measured, same binary:
             *
             *              fps   CPU ms/frame
             *   plain     40.9          13.18
             *   NT        43.9          12.43
             *
             * The guess was wrong by 0.75 ms/frame. A row is still write-only
             * and never read back, so the plain stores pay read-for-ownership
             * on every line they touch and the NT stores do not -- and that
             * costs more than the per-row buffer teardown saves.
             * TORIRS_DAMAGE_CLEAR_PLAIN=1 selects the losing arm.
             */
            int rw = soft->damage_x1 - soft->damage_x0;
            int nt = soft3d_damage_clear_nt();
            for( int y = soft->damage_y0; y < soft->damage_y1; y++ )
            {
                uint32_t* row = p + (size_t)y * soft->stride + soft->damage_x0;
                if( nt )
                {
                    TORIDRAW_FB_CLEAR32(row, (size_t)rw, bg);
                }
                else
                {
                    int i = 0;
                    for( ; i + 4 <= rw; i += 4 )
                    {
                        row[i] = bg;
                        row[i + 1] = bg;
                        row[i + 2] = bg;
                        row[i + 3] = bg;
                    }
                    for( ; i < rw; i++ )
                        row[i] = bg;
                }
            }
        }
        else if( ToriDraw_FrameAbArm() )
        {
            TORIDRAW_FB_CLEAR32(p, n, bg);
        }
        else
        {
            size_t i = 0;
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
#endif
    }

    soft->has_3d = false;
    ToriRS_FrameBegin(frame);
    if( soft3d_pixowner_armed() )
    {
        soft3d_pixowner_begin(soft);
        while( ToriRS_FrameNextCommand(frame, &cmd) )
        {
            soft3d_execute_measured(soft, &cmd);
            soft3d_pixowner_after_command(soft, &cmd);
        }
        soft3d_pixowner_end();
    }
    else if( soft3d_abl_nochrome() )
    {
        /* ABLATION (TORIRS_ABL_NOCHROME=1, measurement only): execute the 3D
         * pass and every state/resource command, and drop the 2D *drawing*
         * outside it -- the sidebar, chatback, minimap, compass and every
         * sprite and glyph composing them.
         *
         * This deliberately renders a wrong image. Its only purpose is to put
         * an upper bound on what damage-gated chrome rasterisation could
         * recover, by deleting all of it: a damage system that never redrew a
         * single chrome pixel could not beat this number. Loads/unloads and
         * BEGIN/END still run, or the scene state diverges from the command
         * stream and the 3D pass stops being comparable. */
        int depth_3d = 0;
        while( ToriRS_FrameNextCommand(frame, &cmd) )
        {
            if( cmd.kind == TORIRSRC_BEGIN_3D )
                depth_3d++;
            else if( cmd.kind == TORIRSRC_END_3D && depth_3d > 0 )
                depth_3d--;
            else if( depth_3d == 0 && soft3d_cmd_is_draw(cmd.kind) )
                continue;
            soft3d_execute_measured(soft, &cmd);
        }
    }
    else
    {
        while( ToriRS_FrameNextCommand(frame, &cmd) )
            soft3d_execute_measured(soft, &cmd);
    }
    ToriRS_FrameEnd(frame);
#if defined(TORIDRAW_FB_POISON) && TORIDRAW_FB_POISON
    fb_poison_scan(soft);
#endif

    ToriDraw_FrameAbEnd();
}
