#include "plugin/plugins/plugin_draw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- pixels */

void
PluginDraw_Pixel(uint32_t* buf, int w, int h, int x, int y, uint32_t rgb, int alpha)
{
    uint32_t* p;
    uint32_t old;
    int r;
    int g;
    int b;

    assert(buf);
    if( x < 0 || y < 0 || x >= w || y >= h || alpha <= 0 )
        return;
    p = &buf[(size_t)y * (size_t)w + (size_t)x];
    if( alpha >= 255 )
    {
        *p = 0xFF000000u | (rgb & 0x00FFFFFFu);
        return;
    }
    old = *p;
    r = (int)((old >> 16) & 0xFF);
    g = (int)((old >> 8) & 0xFF);
    b = (int)(old & 0xFF);
    r += (((int)((rgb >> 16) & 0xFF) - r) * alpha) / 255;
    g += (((int)((rgb >> 8) & 0xFF) - g) * alpha) / 255;
    b += (((int)(rgb & 0xFF) - b) * alpha) / 255;
    *p = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void
PluginDraw_Fill(
    uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb, int alpha)
{
    assert(buf);
    for( int j = 0; j < rh; j++ )
        for( int i = 0; i < rw; i++ )
            PluginDraw_Pixel(buf, w, h, x + i, y + j, rgb, alpha);
}

void
PluginDraw_Frame(
    uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb)
{
    assert(buf);
    if( rw <= 0 || rh <= 0 )
        return;
    PluginDraw_Fill(buf, w, h, x, y, rw, 1, rgb, 255);
    PluginDraw_Fill(buf, w, h, x, y + rh - 1, rw, 1, rgb, 255);
    PluginDraw_Fill(buf, w, h, x, y, 1, rh, rgb, 255);
    PluginDraw_Fill(buf, w, h, x + rw - 1, y, 1, rh, rgb, 255);
}

void
PluginDraw_Blit(
    uint32_t* dst,
    int dw,
    int dh,
    int dx,
    int dy,
    uint32_t const* src,
    int sw,
    int sh,
    int sx,
    int sy,
    int cw,
    int ch,
    uint32_t tint)
{
    assert(dst);
    if( !src )
        return;
    for( int j = 0; j < ch; j++ )
    {
        int const py = sy + j;
        if( py < 0 || py >= sh )
            continue;
        for( int i = 0; i < cw; i++ )
        {
            int const px = sx + i;
            uint32_t texel;
            int alpha;
            uint32_t rgb;

            if( px < 0 || px >= sw )
                continue;
            texel = src[(size_t)py * (size_t)sw + (size_t)px];
            alpha = (int)((texel >> 24) & 0xFF);
            if( alpha <= 0 )
                continue;
            rgb = texel & 0x00FFFFFFu;
            if( tint )
            {
                uint32_t const r = (((rgb >> 16) & 0xFF) * ((tint >> 16) & 0xFF)) / 255u;
                uint32_t const g = (((rgb >> 8) & 0xFF) * ((tint >> 8) & 0xFF)) / 255u;
                uint32_t const b = ((rgb & 0xFF) * (tint & 0xFF)) / 255u;
                rgb = (r << 16) | (g << 8) | b;
            }
            PluginDraw_Pixel(dst, dw, dh, dx + i, dy + j, rgb, alpha);
        }
    }
}

void
PluginDraw_Tile(
    uint32_t* dst,
    int dw,
    int dh,
    int dx,
    int dy,
    int rw,
    int rh,
    uint32_t const* src,
    int sw,
    int sh,
    uint32_t tint)
{
    assert(dst);
    if( !src || sw <= 0 || sh <= 0 )
        return;
    for( int y = 0; y < rh; y += sh )
    {
        int const ch = rh - y < sh ? rh - y : sh;
        for( int x = 0; x < rw; x += sw )
        {
            int const cw = rw - x < sw ? rw - x : sw;
            PluginDraw_Blit(
                dst, dw, dh, dx + x, dy + y, src, sw, sh, 0, 0, cw, ch, tint);
        }
    }
}

/* ---------------------------------------------------------------- assets */

int
PluginDraw_ImageLoad(
    struct ToriRS_ApiV2* api,
    char const* name,
    struct ToriRS_ImageRef* handle,
    uint32_t** px,
    int* w,
    int* h)
{
    size_t count = 0;

    assert(api);
    assert(name);
    assert(handle);
    assert(px);
    assert(w);
    assert(h);
    if( *px ) return 1;
    if( handle->value == 0 )
    {
        enum ToriRS_AssetState const state = api->assets.image(api, name, handle);
        if( state != TORIRS_ASSET_PENDING && state != TORIRS_ASSET_READY )
            return 0;
    }
    if( !api->assets.image_size(api, *handle, w, h) || *w <= 0 || *h <= 0 ||
        (size_t)*w > SIZE_MAX / (size_t)*h ||
        (size_t)*w * (size_t)*h > SIZE_MAX / sizeof(**px) )
        return 0;
    count = (size_t)*w * (size_t)*h;
    *px = malloc(count * sizeof(**px));
    if( !*px ) return 0;
    {
        size_t written = 0;
        if( !api->assets.image_pixels(api, *handle, *px, count, &written) ||
            written != count )
        {
            free(*px);
            *px = NULL;
            return 0;
        }
    }
    return 1;
}

void
PluginDraw_ImageFree(
    struct ToriRS_ApiV2* api,
    uint32_t** px,
    struct ToriRS_ImageRef* handle)
{
    assert(api);
    assert(px);
    assert(handle);
    free(*px);
    *px = NULL;
    if( handle->value ) api->assets.image_release(api, *handle);
    handle->value = 0;
}

static int
plugin_draw_read_ini(
    struct ToriRS_ApiV2* api,
    struct PluginDraw_Atlas* atlas,
    char const* name)
{
    char file[TORIRS_PLUGIN_ASSET_NAME_MAX];
    void const* bytes = NULL;
    char const* at;
    size_t size = 0;

    snprintf(file, sizeof(file), "%s.ini", name);
    if( api->assets.request(api, file) != TORIRS_ASSET_READY ||
        !api->assets.bytes(api, file, &bytes, &size) || !bytes || size == 0 )
        return 0;
    at = bytes;
    for( char const* end = at + size; at < end; )
    {
        char line[128];
        char const* start = at;
        char const* stop = start;
        size_t len;
        while( stop < end && *stop != '\n' ) stop++;
        at = stop < end ? stop + 1 : end;
        if( stop > start && stop[-1] == '\r' ) stop--;
        len = (size_t)(stop - start);
        if( len >= sizeof(line) ) len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        if( len > 12 && strncmp(line, "line_height=", 12) == 0 )
        {
            atlas->line_h = atoi(line + 12);
            continue;
        }
        if( len >= 3 && line[1] == '=' )
        {
            int const index = (unsigned char)line[0] - PLUGIN_DRAW_GLYPH_FIRST;
            struct PluginDraw_Glyph* glyph;
            if( index < 0 || index >= PLUGIN_DRAW_GLYPH_COUNT ) continue;
            glyph = &atlas->glyph[index];
            if( sscanf(line + 2, "%d %d %d %d %d %d %d",
                    &glyph->x, &glyph->y, &glyph->w, &glyph->h,
                    &glyph->off_x, &glyph->off_y, &glyph->advance) == 7 )
                atlas->ready = 1;
        }
    }
    return atlas->ready;
}

int
PluginDraw_AtlasLoad(
    struct ToriRS_ApiV2* api,
    struct PluginDraw_Atlas* atlas,
    char const* name)
{
    char file[TORIRS_PLUGIN_ASSET_NAME_MAX];
    assert(api);
    assert(atlas);
    assert(name);
    if( atlas->ready && atlas->px ) return 1;
    if( !atlas->ready && !plugin_draw_read_ini(api, atlas, name) ) return 0;
    snprintf(file, sizeof(file), "%s.png", name);
    return PluginDraw_ImageLoad(
        api, file, &atlas->image, &atlas->px, &atlas->w, &atlas->h);
}

void
PluginDraw_AtlasFree(
    struct ToriRS_ApiV2* api,
    struct PluginDraw_Atlas* atlas)
{
    assert(api);
    assert(atlas);
    PluginDraw_ImageFree(api, &atlas->px, &atlas->image);
    atlas->ready = 0;
    atlas->w = 0;
    atlas->h = 0;
}

/* ------------------------------------------------------------------ text */

int
PluginDraw_TextWidth(struct PluginDraw_Atlas const* atlas, char const* text)
{
    int width = 0;
    assert(atlas);
    assert(text);
    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - PLUGIN_DRAW_GLYPH_FIRST;
        if( index >= 0 && index < PLUGIN_DRAW_GLYPH_COUNT )
            width += atlas->glyph[index].advance;
    }
    return width;
}

void
PluginDraw_Text(
    uint32_t* buf,
    int w,
    int h,
    int x,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint)
{
    int pen = x;
    assert(buf);
    assert(atlas);
    assert(text);
    if( !atlas->ready || !atlas->px ) return;
    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - PLUGIN_DRAW_GLYPH_FIRST;
        struct PluginDraw_Glyph const* glyph;
        if( index < 0 || index >= PLUGIN_DRAW_GLYPH_COUNT ) continue;
        glyph = &atlas->glyph[index];
        if( glyph->w > 0 && glyph->h > 0 )
            PluginDraw_Blit(buf, w, h, pen + glyph->off_x, top + glyph->off_y,
                atlas->px, atlas->w, atlas->h, glyph->x, glyph->y,
                glyph->w, glyph->h, tint);
        pen += glyph->advance;
    }
}

void
PluginDraw_TextRight(
    uint32_t* buf, int w, int h, int right, int top,
    struct PluginDraw_Atlas const* atlas, char const* text, uint32_t tint)
{
    PluginDraw_Text(buf, w, h,
        right - PluginDraw_TextWidth(atlas, text), top, atlas, text, tint);
}

void
PluginDraw_TextCenter(
    uint32_t* buf, int w, int h, int x, int width, int top,
    struct PluginDraw_Atlas const* atlas, char const* text, uint32_t tint)
{
    PluginDraw_Text(buf, w, h,
        x + (width - PluginDraw_TextWidth(atlas, text)) / 2,
        top, atlas, text, tint);
}
