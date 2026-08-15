#include "toridraw_font_from_torirs.h"

#include "engine/torirs_types.h"

#include "toridraw_font.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_Font*
ToriDraw_FontFromToriRS(struct ToriRS_Font const* src)
{
    struct ToriDraw_Font* dst;
    int i;

    assert(src);
    assert(TORIRS_FONT_GLYPH_COUNT == TORIDRAW_FONT_GLYPH_COUNT);

    dst = calloc(1, sizeof(*dst));
    assert(dst);

    for( i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int w = src->glyph_width[i];
        int h = src->glyph_height[i];
        size_t nbytes;

        dst->glyph_width[i] = w;
        dst->glyph_height[i] = h;
        dst->offset_x[i] = src->offset_x[i];
        dst->offset_y[i] = src->offset_y[i];
        dst->advance[i] = src->advance[i];

        if( !src->glyph_alpha[i] || w <= 0 || h <= 0 )
            continue;

        nbytes = (size_t)w * (size_t)h;
        dst->glyph_alpha[i] = malloc(nbytes);
        assert(dst->glyph_alpha[i]);
        memcpy(dst->glyph_alpha[i], src->glyph_alpha[i], nbytes);
    }
    dst->advance[TORIDRAW_FONT_GLYPH_COUNT] = src->advance[TORIRS_FONT_GLYPH_COUNT];
    memcpy(dst->draw_width, src->draw_width, sizeof(dst->draw_width));
    dst->line_height = src->line_height;
    memcpy(dst->charcodeset, src->charcodeset, sizeof(dst->charcodeset));
    return dst;
}
