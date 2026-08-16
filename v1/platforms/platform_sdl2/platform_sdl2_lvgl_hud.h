#ifndef PLATFORM_SDL2_LVGL_HUD_H
#define PLATFORM_SDL2_LVGL_HUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct LibToriRS_Instance;
struct LibToriHud;

#define LIBTORI_HUD_PANEL_X 8
#define LIBTORI_HUD_PANEL_Y 8
#define LIBTORI_HUD_PANEL_W 220
#define LIBTORI_HUD_PANEL_H 56

#if defined(TORIRS_ENABLE_LVGL_HUD)

struct LibToriHud*
LibToriHud_New(void);

void
LibToriHud_Update(
    struct LibToriHud* hud,
    struct LibToriRS_Instance* instance);

uint8_t const*
LibToriHud_PixelsBGRA(
    struct LibToriHud* hud,
    int* out_width,
    int* out_height,
    int* out_pitch_bytes);

void
LibToriHud_CompositeOverARGB8888(
    struct LibToriHud* hud,
    uint32_t* dest,
    int dest_stride,
    int dest_w,
    int dest_h);

void
LibToriHud_Free(struct LibToriHud* hud);

#else

static inline struct LibToriHud*
LibToriHud_New(void)
{
    return NULL;
}

static inline void
LibToriHud_Update(
    struct LibToriHud* hud,
    struct LibToriRS_Instance* instance)
{
    (void)hud;
    (void)instance;
}

static inline uint8_t const*
LibToriHud_PixelsBGRA(
    struct LibToriHud* hud,
    int* out_width,
    int* out_height,
    int* out_pitch_bytes)
{
    (void)hud;
    (void)out_width;
    (void)out_height;
    (void)out_pitch_bytes;
    return NULL;
}

static inline void
LibToriHud_CompositeOverARGB8888(
    struct LibToriHud* hud,
    uint32_t* dest,
    int dest_stride,
    int dest_w,
    int dest_h)
{
    (void)hud;
    (void)dest;
    (void)dest_stride;
    (void)dest_w;
    (void)dest_h;
}

static inline void
LibToriHud_Free(struct LibToriHud* hud)
{
    (void)hud;
}

#endif

#endif
