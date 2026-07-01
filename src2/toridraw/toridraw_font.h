#ifndef TORIDRAW_FONT_H
#define TORIDRAW_FONT_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

#define TORIDRAW_FONT_GLYPH_COUNT 94

uint16_t const*
ToriDraw_FontCharsetTable(void);

void
ToriDraw_FontInitCharcodeset(struct ToriDraw_Font* font);

struct ToriDraw_Font
{
    uint8_t* glyph_alpha[TORIDRAW_FONT_GLYPH_COUNT];
    int glyph_width[TORIDRAW_FONT_GLYPH_COUNT];
    int glyph_height[TORIDRAW_FONT_GLYPH_COUNT];
    int offset_x[TORIDRAW_FONT_GLYPH_COUNT];
    int offset_y[TORIDRAW_FONT_GLYPH_COUNT];
    int advance[TORIDRAW_FONT_GLYPH_COUNT + 1];
    int draw_width[256];
    int line_height;
    char charcodeset[256];
};

struct ToriDraw_Font*
ToriDraw_FontNewFromRSBytes(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size);

void
ToriDraw_FontFree(struct ToriDraw_Font* font);

bool
ToriDraw_FontValidate(struct ToriDraw_Font* font);

int
ToriDraw2D_MeasureString(
    struct ToriDraw_Font* font,
    char const* text);

/** Returns the number of opaque glyph pixels written (0 if nothing drawn). */
int
ToriDraw2D_DrawString(
    struct ToriDraw_Font* font,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    char const* text,
    int color,
    bool center,
    bool shadowed,
    int* pixel_buffer);

#endif
