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
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api,
    char const* name,
    int* handle,
    uint32_t** px,
    int* w,
    int* h)
{
    assert(ctx);
    assert(api);
    assert(name);
    assert(handle);
    assert(px);
    assert(w);
    assert(h);

    if( *px )
        return 1;
    if( *handle < 0 )
        *handle = api->image_load(ctx, name);
    if( *handle < 0 )
        return 0;
    if( !api->image_size(ctx, *handle, w, h) || *w <= 0 || *h <= 0 )
        return 0;
    *px = malloc((size_t)*w * (size_t)*h * sizeof(**px));
    assert(*px);
    if( !api->image_pixels(ctx, *handle, *px, *w * *h) )
    {
        free(*px);
        *px = NULL;
        return 0;
    }
    return 1;
}

void
PluginDraw_ImageFree(uint32_t** px, int* handle)
{
    assert(px);
    assert(handle);
    free(*px);
    *px = NULL;
    /* The HANDLE is the host's -- it drops every image a plugin owns at
     * teardown -- so this only forgets it, which is what stops a restart from
     * drawing with a slot the host has already reclaimed. */
    *handle = -1;
}

/**
 * Read `<name>.ini` into `atlas`.
 *
 * A glyph line is one whose SECOND byte is '=', which is what lets the space
 * glyph -- a line beginning with a space -- be read by the same rule as every
 * other one and keeps it apart from the header keys and the comments.
 */
static int
plugin_draw_read_ini(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api,
    struct PluginDraw_Atlas* atlas,
    char const* name)
{
    char file[TORIRS_PLUGIN_ASSET_NAME_MAX];
    char const* at;
    int size = 0;

    snprintf(file, sizeof(file), "%s.ini", name);
    if( !api->asset_load(ctx, file) )
        return 0;
    at = (char const*)api->asset_data(ctx, file, &size);
    if( !at || size <= 0 )
        return 0;

    for( char const* end = at + size; at < end; )
    {
        /* The asset is a byte RANGE and not a C string, so every line is
         * copied out before it is parsed: sscanf runs to a NUL, and on the
         * last line that NUL is past the end of the allocation. */
        char line[128];
        char const* start = at;
        char const* stop = start;
        size_t len;

        while( stop < end && *stop != '\n' )
            stop++;
        at = stop < end ? stop + 1 : end;
        if( stop > start && stop[-1] == '\r' )
            stop--;
        len = (size_t)(stop - start);
        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';

        if( len > 12 && strncmp(line, "line_height=", 12) == 0 )
        {
            atlas->line_h = atoi(line + 12);
            continue;
        }
        if( len < 3 || line[1] != '=' )
            continue;
        {
            int const index = (unsigned char)line[0] - PLUGIN_DRAW_GLYPH_FIRST;
            struct PluginDraw_Glyph* g;

            if( index < 0 || index >= PLUGIN_DRAW_GLYPH_COUNT )
                continue;
            g = &atlas->glyph[index];
            if( sscanf(
                    line + 2, "%d %d %d %d %d %d %d", &g->x, &g->y, &g->w, &g->h,
                    &g->off_x, &g->off_y, &g->advance) == 7 )
                atlas->ready = 1;
        }
    }
    return atlas->ready;
}

int
PluginDraw_AtlasLoad(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api,
    struct PluginDraw_Atlas* atlas,
    char const* name)
{
    char file[TORIRS_PLUGIN_ASSET_NAME_MAX];

    assert(ctx);
    assert(api);
    assert(atlas);
    assert(name);

    if( atlas->ready && atlas->px )
        return 1;
    if( !atlas->ready && !plugin_draw_read_ini(ctx, api, atlas, name) )
        return 0;
    snprintf(file, sizeof(file), "%s.png", name);
    return PluginDraw_ImageLoad(
        ctx, api, file, &atlas->image, &atlas->px, &atlas->w, &atlas->h);
}

void
PluginDraw_AtlasFree(struct PluginDraw_Atlas* atlas)
{
    assert(atlas);
    PluginDraw_ImageFree(&atlas->px, &atlas->image);
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
    if( !atlas->ready || !atlas->px )
        return;

    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - PLUGIN_DRAW_GLYPH_FIRST;
        struct PluginDraw_Glyph const* g;

        if( index < 0 || index >= PLUGIN_DRAW_GLYPH_COUNT )
            continue;
        g = &atlas->glyph[index];
        if( g->w > 0 && g->h > 0 )
            PluginDraw_Blit(
                buf, w, h, pen + g->off_x, top + g->off_y, atlas->px, atlas->w,
                atlas->h, g->x, g->y, g->w, g->h, tint);
        pen += g->advance;
    }
}

void
PluginDraw_TextRight(
    uint32_t* buf,
    int w,
    int h,
    int right,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint)
{
    PluginDraw_Text(
        buf, w, h, right - PluginDraw_TextWidth(atlas, text), top, atlas, text, tint);
}

void
PluginDraw_TextCenter(
    uint32_t* buf,
    int w,
    int h,
    int x,
    int width,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint)
{
    PluginDraw_Text(
        buf, w, h, x + (width - PluginDraw_TextWidth(atlas, text)) / 2, top, atlas,
        text, tint);
}
