#include "platform_sdl2_lvgl_hud.h"

#if defined(TORIRS_ENABLE_LVGL_HUD)

#include "../../libtorirs.h"
#include "../../toriauxlib/cache/toriauxlibcache.h"
#include "../../toriauxlib/toriauxlib.h"
#include "lvgl.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LibToriHud
{
    lv_display_t* disp;
    uint8_t* fb_raw;
    uint8_t* fb;
    size_t fb_size;
    uint32_t stride;
    int width;
    int height;
    lv_obj_t* label_datx;
    lv_obj_t* label_cache;
    size_t last_l1_bytes;
    size_t last_total_bytes;
    bool has_mem_values;
};

static bool g_lvgl_inited;

static uint32_t
hud_sdl_tick(void)
{
    return (uint32_t)SDL_GetTicks64();
}

static void
hud_flush_cb(
    lv_display_t* disp,
    const lv_area_t* area,
    uint8_t* color_p)
{
    (void)area;
    (void)color_p;
    lv_display_flush_ready(disp);
}

struct LibToriHud*
LibToriHud_New(void)
{
    int const width = LIBTORI_HUD_PANEL_W;
    int const height = LIBTORI_HUD_PANEL_H;

    if( !g_lvgl_inited )
    {
        lv_init();
        lv_tick_set_cb(hud_sdl_tick);
        g_lvgl_inited = true;
    }

    struct LibToriHud* hud = (struct LibToriHud*)calloc(1, sizeof(*hud));
    if( !hud )
        return NULL;

    hud->width = width;
    hud->height = height;
    hud->stride = lv_draw_buf_width_to_stride((uint32_t)width, LV_COLOR_FORMAT_ARGB8888);
    hud->fb_size = (size_t)hud->stride * (size_t)height;

    hud->fb_raw = (uint8_t*)malloc(hud->fb_size + LV_DRAW_BUF_ALIGN);
    if( !hud->fb_raw )
    {
        free(hud);
        return NULL;
    }

    hud->fb = lv_draw_buf_align(hud->fb_raw, LV_COLOR_FORMAT_ARGB8888);
    memset(hud->fb, 0, hud->fb_size);

    hud->disp = lv_display_create(width, height);
    if( !hud->disp )
    {
        free(hud->fb_raw);
        free(hud);
        return NULL;
    }

    lv_display_set_color_format(hud->disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(
        hud->disp, hud->fb, NULL, (uint32_t)hud->fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(hud->disp, hud_flush_cb);

    lv_theme_t* theme = lv_theme_default_init(
        hud->disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_CYAN),
        true,
        LV_FONT_DEFAULT);
    lv_display_set_theme(hud->disp, theme);

    lv_obj_t* scr = lv_display_get_screen_active(hud->disp);
    lv_obj_set_size(scr, width, height);
    lv_obj_set_style_bg_opa(scr, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x202428), 0);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 6, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    hud->label_datx = lv_label_create(scr);
    lv_label_set_text(hud->label_datx, "DatX: -- MB");
    lv_obj_set_pos(hud->label_datx, 0, 0);

    hud->label_cache = lv_label_create(scr);
    lv_label_set_text(hud->label_cache, "Cache: -- MB");
    lv_obj_set_pos(hud->label_cache, 0, 20);

    return hud;
}

void
LibToriHud_Update(
    struct LibToriHud* hud,
    struct LibToriRS_Instance* instance)
{
    assert(hud);
    assert(instance);

    struct ToriAuxLib* tal = LibToriRS_GetToriAuxLib(instance);
    if( tal )
    {
        struct ToriAuxLibCache_MemReport report;
        ToriAuxLibCache_MemReport(ToriAuxLib_C(tal), &report);

        if( !hud->has_mem_values || report.l1_bytes != hud->last_l1_bytes )
        {
            char buf[64];
            snprintf(
                buf, sizeof(buf), "DatX: %.1f MB", (double)report.l1_bytes / (1024.0 * 1024.0));
            lv_label_set_text(hud->label_datx, buf);
            hud->last_l1_bytes = report.l1_bytes;
        }

        if( !hud->has_mem_values || report.total_bytes != hud->last_total_bytes )
        {
            char buf[64];
            snprintf(
                buf, sizeof(buf), "Cache: %.1f MB", (double)report.total_bytes / (1024.0 * 1024.0));
            lv_label_set_text(hud->label_cache, buf);
            hud->last_total_bytes = report.total_bytes;
        }

        hud->has_mem_values = true;
    }

    lv_obj_invalidate(lv_display_get_screen_active(hud->disp));
    lv_refr_now(hud->disp);
    lv_timer_handler();
}

void
LibToriHud_CompositeOverARGB8888(
    struct LibToriHud* hud,
    uint32_t* dest,
    int dest_stride,
    int dest_w,
    int dest_h)
{
    assert(hud);
    assert(dest);
    if( dest_stride <= 0 || dest_w <= 0 || dest_h <= 0 )
        return;

    int hud_w = 0;
    int hud_h = 0;
    int hud_pitch = 0;
    uint8_t const* src = LibToriHud_PixelsBGRA(hud, &hud_w, &hud_h, &hud_pitch);
    if( !src || hud_w <= 0 || hud_h <= 0 )
        return;

    int const dst_x0 = LIBTORI_HUD_PANEL_X;
    int const dst_y0 = LIBTORI_HUD_PANEL_Y;
    int const copy_w = hud_w;
    int const copy_h = hud_h;

    if( dst_x0 >= dest_w || dst_y0 >= dest_h )
        return;

    int const max_w = dest_w - dst_x0;
    int const max_h = dest_h - dst_y0;
    int const blend_w = copy_w < max_w ? copy_w : max_w;
    int const blend_h = copy_h < max_h ? copy_h : max_h;

    for( int y = 0; y < blend_h; y++ )
    {
        uint8_t const* src_row = src + (size_t)y * (size_t)hud_pitch;
        uint32_t* dst_row = dest + (size_t)(dst_y0 + y) * (size_t)dest_stride + (size_t)dst_x0;

        for( int x = 0; x < blend_w; x++ )
        {
            uint8_t const b = src_row[x * 4 + 0];
            uint8_t const g = src_row[x * 4 + 1];
            uint8_t const r = src_row[x * 4 + 2];
            uint8_t const a = src_row[x * 4 + 3];
            if( a == 0 )
                continue;

            if( a == 255 )
            {
                dst_row[x] =
                    ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                continue;
            }

            uint32_t const dst_px = dst_row[x];
            uint8_t const dst_a = (uint8_t)((dst_px >> 24) & 0xFFu);
            uint8_t const dst_r = (uint8_t)((dst_px >> 16) & 0xFFu);
            uint8_t const dst_g = (uint8_t)((dst_px >> 8) & 0xFFu);
            uint8_t const dst_b = (uint8_t)(dst_px & 0xFFu);

            uint16_t const inv_a = (uint16_t)(255u - (uint16_t)a);
            uint8_t const out_r =
                (uint8_t)(((uint16_t)r * (uint16_t)a + (uint16_t)dst_r * inv_a) / 255u);
            uint8_t const out_g =
                (uint8_t)(((uint16_t)g * (uint16_t)a + (uint16_t)dst_g * inv_a) / 255u);
            uint8_t const out_b =
                (uint8_t)(((uint16_t)b * (uint16_t)a + (uint16_t)dst_b * inv_a) / 255u);
            uint8_t const out_a = (uint8_t)((uint16_t)a + ((uint16_t)dst_a * inv_a) / 255u);

            dst_row[x] = ((uint32_t)out_a << 24) | ((uint32_t)out_r << 16) |
                         ((uint32_t)out_g << 8) | (uint32_t)out_b;
        }
    }
}

uint8_t const*
LibToriHud_PixelsBGRA(
    struct LibToriHud* hud,
    int* out_width,
    int* out_height,
    int* out_pitch_bytes)
{
    assert(hud);

    if( out_width )
        *out_width = hud->width;
    if( out_height )
        *out_height = hud->height;
    if( out_pitch_bytes )
        *out_pitch_bytes = (int)hud->stride;
    return hud->fb;
}

void
LibToriHud_Free(struct LibToriHud* hud)
{
    if( !hud )
        return;

    if( hud->disp )
    {
        lv_display_delete(hud->disp);
        hud->disp = NULL;
    }

    free(hud->fb_raw);
    hud->fb_raw = NULL;
    hud->fb = NULL;
    free(hud);

    if( g_lvgl_inited )
    {
        lv_deinit();
        g_lvgl_inited = false;
    }
}

#endif
