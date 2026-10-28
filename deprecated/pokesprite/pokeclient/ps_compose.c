#include "ps_compose.h"

#include "ps_color.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define EYE_RGB 0x1C1424u /* (28, 20, 36) */

static int
lum(uint32_t rgb)
{
    return (int)((rgb >> 16) & 0xFF) + (int)((rgb >> 8) & 0xFF) + (int)(rgb & 0xFF);
}

/*
 * Eyes where the FACE is, drawn over whatever head kit is there: rows from
 * the skin (where it is at least 60% of its widest), columns from the head,
 * so a fringe over one eye does not take it away. Never on anything but a head
 * kit: a helm's visor stays a visor. layers.py paint_eyes().
 */
static void
paint_eyes(
    uint32_t* img,
    const uint8_t* skin,
    const uint8_t* head,
    int w,
    int h,
    int facing)
{
    if( facing == PS_FACING_UP )
        return;
    int widths[512] = { 0 };
    int ymin = -1, ymax = -1, best = 0;
    assert(h <= 512);
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < w; x++ )
            if( skin[y * w + x] )
            {
                widths[y]++;
                if( ymin < 0 )
                    ymin = y;
                ymax = y;
            }
    if( ymin < 0 )
        return;
    for( int y = ymin; y <= ymax; y++ )
        if( widths[y] > best )
            best = widths[y];
    int top = -1, bot = -1;
    for( int y = ymin; y <= ymax; y++ )
        if( widths[y] * 10 >= best * 6 )
        {
            if( top < 0 )
                top = y;
            bot = y;
        }
    if( bot - top < 2 )
        return;
    int ey = (int)(top + 0.38f * (float)(bot - top) + 0.5f);
    int x0 = -1, x1 = -1, n = 0;
    for( int x = 0; x < w; x++ )
        if( head[ey * w + x] )
        {
            if( x0 < 0 )
                x0 = x;
            x1 = x;
            n++;
        }
    if( n < 3 )
        return;
    int span = x1 - x0 + 1;
    int cols[2], ncol = 0;
    if( facing == PS_FACING_DOWN )
    {
        int off = (int)(span * 0.18f + 0.5f);
        if( off < 1 )
            off = 1;
        cols[ncol++] = (x0 + x1) / 2 - off;
        cols[ncol++] = (x0 + x1 + 1) / 2 + off;
    }
    else
    {
        int in = (int)(span * 0.25f + 0.5f);
        if( in < 1 )
            in = 1;
        cols[ncol++] = facing == PS_FACING_LEFT ? x0 + in : x1 - in;
    }
    for( int c = 0; c < ncol; c++ )
        for( int y = ey; y <= ey + 1 && y < h; y++ )
            if( cols[c] >= 0 && cols[c] < w && head[y * w + cols[c]] )
                img[y * w + cols[c]] = 0xFF000000u | EYE_RGB;
}

void
PS_Compose(
    const struct PS_ComposeInput* in,
    uint32_t* out,
    int w,
    int h)
{
    assert(in);
    assert(out);
    assert(in->layer_count <= PS_COMPOSE_MAX_LAYERS);
    size_t n = (size_t)w * (size_t)h;
    int* depth = (int*)malloc(n * sizeof(int));
    int* lid = (int*)malloc(n * sizeof(int));
    uint32_t* rgb = (uint32_t*)malloc(n * sizeof(uint32_t));
    uint32_t* shadow = (uint32_t*)malloc(n * sizeof(uint32_t));
    uint8_t* head = (uint8_t*)calloc(n, 1);
    uint8_t* skin = (uint8_t*)calloc(n, 1);
    assert(depth);
    assert(lid);
    assert(rgb);
    assert(shadow);
    assert(head);
    assert(skin);
    for( size_t i = 0; i < n; i++ )
    {
        depth[i] = 999;
        lid[i] = -1;
    }

    for( int li = 0; li < in->layer_count; li++ )
    {
        const struct PS_Sheet* s = in->layers[li];
        assert(s);
        assert(s->head.kind == POKESPRITE_KIND_LAYER);
        assert(s->head.cell_w == w);
        assert(s->head.cell_h == h);
        uint32_t ramps[POKESPRITE_MAX_MATERIALS][3];
        for( int m = 0; m < s->head.material_count; m++ )
        {
            const struct PokeSpriteMaterial* mat = &s->head.materials[m];
            uint32_t base = ((uint32_t)mat->base_rgb[0] << 16) | ((uint32_t)mat->base_rgb[1] << 8) |
                            mat->base_rgb[2];
            for( int k = 0; k < mat->hsl16_count; k++ )
                for( int r = 0; r < in->recolour_count; r++ )
                    if( mat->hsl16[k] == in->recolours[r].from_hsl16 )
                        base = PS_Hsl16ToRgb(in->recolours[r].to_hsl16);
            PS_Ramp(base, mat->steps, ramps[m]);
        }
        int frame = in->frame < s->head.frames ? in->frame : s->head.frames - 1;
        const uint32_t* cell = PS_SheetCell(s, in->facing, frame);
        for( int y = 0; y < h; y++ )
            for( int x = 0; x < w; x++ )
            {
                uint32_t p = cell[(size_t)y * (size_t)s->width + (size_t)x];
                if( (p >> 24) == 0 )
                    continue;
                int d = (int)(p & 0xFF);
                size_t i = (size_t)y * (size_t)w + (size_t)x;
                if( d >= depth[i] )
                    continue;
                int ms = (int)((p >> 16) & 0xFF);
                int band = (int)((p >> 8) & 0xFF);
                assert(ms < s->head.material_count);
                const struct PokeSpriteMaterial* mat = &s->head.materials[ms];
                if( band >= mat->steps )
                    band = mat->steps - 1;
                depth[i] = d;
                lid[i] = li;
                rgb[i] = ramps[ms][band];
                shadow[i] = ramps[ms][0];
                head[i] = (mat->flags & POKESPRITE_MAT_HEAD) != 0;
                skin[i] = (mat->flags & POKESPRITE_MAT_SKIN) != 0;
            }
    }

    static const int dx[4] = { 0, 0, -1, 1 };
    static const int dy[4] = { -1, 1, 0, 0 };
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < w; x++ )
        {
            size_t i = (size_t)y * (size_t)w + (size_t)x;
            if( lid[i] >= 0 )
            {
                /* Overlap line: on the far side of a real depth step to another layer. */
                int behind = 0;
                for( int k = 0; k < 4 && !behind; k++ )
                {
                    int nx = x + dx[k], ny = y + dy[k];
                    if( nx < 0 || ny < 0 || nx >= w || ny >= h )
                        continue;
                    size_t j = (size_t)ny * (size_t)w + (size_t)nx;
                    behind = lid[j] >= 0 && lid[j] != lid[i] && depth[i] - depth[j] > 6;
                }
                out[i] = 0xFF000000u | (behind ? PS_Darken(shadow[i], 0.62f) : rgb[i]);
                continue;
            }
            /* Outline: a dark tint of the darkest material it touches. */
            int pick = -1, best = 1 << 30;
            for( int k = 0; k < 4; k++ )
            {
                int nx = x + dx[k], ny = y + dy[k];
                if( nx < 0 || ny < 0 || nx >= w || ny >= h )
                    continue;
                size_t j = (size_t)ny * (size_t)w + (size_t)nx;
                if( lid[j] >= 0 && lum(shadow[j]) < best )
                {
                    best = lum(shadow[j]);
                    pick = (int)j;
                }
            }
            out[i] = pick < 0 ? 0u : 0xFF000000u | PS_Darken(shadow[pick], 0.40f);
        }

    for( size_t i = 0; i < n; i++ )
    {
        head[i] = head[i] && lid[i] >= 0;
        skin[i] = skin[i] && head[i];
    }
    paint_eyes(out, skin, head, w, h, in->facing);

    free(depth);
    free(lid);
    free(rgb);
    free(shadow);
    free(head);
    free(skin);
}
