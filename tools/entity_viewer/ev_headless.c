/*
 * Headless rendering helpers shared by the entity viewer's command-line tools
 * (ev_sheet, ev_icons): the cache's textures installed into ev_render's scene,
 * and a texture's flat average colour for a face that cannot draw it.
 * Moved out of ev_sheet.c unchanged.
 */
#include "ev_headless.h"

#include "ev_build.h"
#include "ev_render.h"
#include "ev_textures.h"
#include "ev_wire.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- texture flattening */

/*
 * A sprite render has no texture raster, so a textured face is drawn in its
 * texture's average colour -- the reference client's low-detail look. The
 * average is taken over the opaque texels, gamma undone (the HSL table bakes
 * brightness 0.8 in), and put back into HSL16 so the face is lit and grouped
 * like any other. Cached per texture id.
 */
static struct EV_TextureSet g_textures;
static int g_textures_loaded = 0;
static int g_flat_hsl[8192];

static int
rgb_to_hsl16(
    double r,
    double g,
    double b)
{
    r = pow(r, 1.0 / 0.8);
    g = pow(g, 1.0 / 0.8);
    b = pow(b, 1.0 / 0.8);
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    double l = (mx + mn) / 2.0, h = 0.0, s = 0.0;
    if( mx > mn )
    {
        double d = mx - mn;
        s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
        if( mx == r )
            h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if( mx == g )
            h = (b - r) / d + 2.0;
        else
            h = (r - g) / d + 4.0;
        h /= 6.0;
    }
    int hh = (int)(h * 64.0) & 63;
    int ss = (int)(s * 8.0);
    int ll = (int)(l * 128.0);
    ss = ss > 7 ? 7 : ss;
    ll = ll > 127 ? 127 : ll;
    return (hh << 10) | (ss << 7) | ll;
}

int
ev_headless_flatten_texture(
    int id,
    void* user)
{
    struct Tool_Dat2Cache* cache = (struct Tool_Dat2Cache*)user;
    if( id < 0 || id >= (int)(sizeof(g_flat_hsl) / sizeof(g_flat_hsl[0])) )
        return -1;
    if( !g_textures_loaded )
    {
        /* The first call loads the set, and only it needs the cache. */
        assert(cache);
        g_textures_loaded = ev_textures_load(cache, &g_textures) ? 1 : -1;
        for( int i = 0; i < (int)(sizeof(g_flat_hsl) / sizeof(g_flat_hsl[0])); i++ )
            g_flat_hsl[i] = -2;
    }
    if( g_textures_loaded < 0 )
        return -1;
    if( g_flat_hsl[id] != -2 )
        return g_flat_hsl[id];
    const struct EV_Texture* t = ev_textures_get(&g_textures, id);
    int result = -1;
    if( t && t->texels && t->size > 0 )
    {
        double sr = 0, sg = 0, sb = 0;
        long n = 0;
        for( int i = 0; i < t->size * t->size; i++ )
        {
            uint32_t p = (uint32_t)t->texels[i];
            if( !t->opaque && (p >> 24) < 128 )
                continue;
            sr += (p >> 16) & 0xFF;
            sg += (p >> 8) & 0xFF;
            sb += p & 0xFF;
            n++;
        }
        if( n > 0 )
            result = rgb_to_hsl16(sr / n / 255.0, sg / n / 255.0, sb / n / 255.0);
    }
    g_flat_hsl[id] = result;
    return result;
}

/*
 * Textures drawn for real.
 *
 * The lit frame is the shading every sprite is built from, so a textured face
 * has to be drawn TEXTURED there: wood grain, stonework, thatch and leaves are
 * light and dark detail, which the Pokemon stage turns into its shade bands.
 * The cache's textures are installed into the renderer as the viewer's own
 * EVT1 blob (the format ev_server sends the page), and the build is told they
 * are available so it keeps them on the faces. The flatten hook above remains
 * the fallback for a texture that did not bake, and the average it computes is
 * a textured face's base colour in the face table (write_faces).
 */
static int
texture_available(
    int id,
    void* user)
{
    (void)user;
    return g_textures_loaded > 0 && ev_textures_get(&g_textures, id) != NULL;
}

static void
put_u32(
    uint8_t** p,
    uint32_t v)
{
    (*p)[0] = (uint8_t)v;
    (*p)[1] = (uint8_t)(v >> 8);
    (*p)[2] = (uint8_t)(v >> 16);
    (*p)[3] = (uint8_t)(v >> 24);
    *p += 4;
}

void
ev_headless_install_textures(struct Tool_Dat2Cache* cache)
{
    assert(cache);
    (void)ev_headless_flatten_texture(0, cache); /* loads the set */
    if( g_textures_loaded <= 0 )
    {
        fprintf(stderr, "warning: textures did not load; textured faces fall back to flat\n");
        return;
    }
    size_t bytes = 8;
    int count = 0;
    for( int i = 0; i < g_textures.count; i++ )
        if( g_textures.items[i].texels )
        {
            bytes += 4 + 8 + 4u * (size_t)g_textures.items[i].size * (size_t)g_textures.items[i].size;
            count++;
        }
    uint8_t* blob = (uint8_t*)malloc(bytes);
    assert(blob);
    uint8_t* p = blob;
    put_u32(&p, EV_WIRE_TEXTURES_MAGIC);
    put_u32(&p, (uint32_t)count);
    for( int i = 0; i < g_textures.count; i++ )
    {
        const struct EV_Texture* t = &g_textures.items[i];
        if( !t->texels )
            continue;
        put_u32(&p, (uint32_t)t->id);
        /* The header ev_server's handle_textures_bin writes. */
        p[0] = (uint8_t)(t->size & 0xFF);
        p[1] = (uint8_t)((t->size >> 8) & 0xFF);
        p[2] = (uint8_t)(t->alpha_mode >= 0 && t->alpha_mode <= 2 ? t->alpha_mode : 0);
        p[3] = (uint8_t)((t->repeat_s ? 0 : 1) | (t->repeat_t ? 0 : 2) | (t->modulate ? 4 : 0));
        p[4] = (uint8_t)(t->mean_luma < 1 ? 1 : t->mean_luma);
        p[5] = p[6] = p[7] = 0;
        p += 8;
        for( int k = 0; k < t->size * t->size; k++ )
            put_u32(&p, (uint32_t)t->texels[k]);
    }
    assert((size_t)(p - blob) == bytes);
    int installed = ev_set_textures(blob, (int)bytes);
    free(blob);
    if( installed > 0 )
        ev_build_set_texture_available(texture_available, NULL);
}

