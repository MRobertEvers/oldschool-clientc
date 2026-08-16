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

void
ToriDraw_FontFinishDrawWidths(struct ToriDraw_Font* font);

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

void
ToriDraw_FontFree(struct ToriDraw_Font* font);

bool
ToriDraw_FontValidate(struct ToriDraw_Font* font);

int
ToriDraw_FontEvaluateColorTag(char const tag[3]);

int
ToriDraw_FontParseHexColor(
    char const* hex,
    int len);

/**
 * Length in bytes of the markup token starting at text[i], or 0 if there is no
 * token there. `*emit_char_out` receives the character the token renders as
 * (`<lt>` -> '<', `<gt>` -> '>') or 0 when it renders nothing.
 *
 * Exposed because measuring text and drawing it must agree on what is markup:
 * anything that walks a string to work out how wide it will be has to skip
 * exactly the tokens the glyph walk skips. The grammar needs no font, so this
 * takes none.
 */
int
ToriDraw_FontMarkupTokenLength(
    char const* text,
    int len,
    int index,
    unsigned char* emit_char_out);

typedef void (*ToriDraw_FontGlyphCallback)(
    void* ctx,
    struct ToriDraw_Font* font,
    int gi,
    int x,
    int y,
    int color_rgb);

void
ToriDraw_FontVisitGlyphs(
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    ToriDraw_FontGlyphCallback callback,
    void* ctx);

void
ToriDraw_FontVisitGlyphsStyled(
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    bool center,
    ToriDraw_FontGlyphCallback callback,
    void* ctx);

int
ToriDraw2D_MeasureString(
    struct ToriDraw_Font* font,
    char const* text);

/** Glyph line box height: max(offset_y + glyph_height) over drawable glyphs.
 *  Falls back to line_height (ascent) when the font has no drawable glyphs.
 *  Used by the minimenu to size rows independently of the metrics ascent. */
int
ToriDraw_FontLineBoxHeight(struct ToriDraw_Font const* font);

/** Word-wrap at max_width pixels; return widest resulting line width. */
int
ToriDraw2D_WrapMaxLineWidth(
    struct ToriDraw_Font* font,
    char const* text,
    int max_width);

/** Word-wrap at max_width pixels; return resulting line count (0 for empty text). */
int
ToriDraw2D_WrapLineCount(
    struct ToriDraw_Font* font,
    char const* text,
    int max_width);

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

/** Multi-line widget text with box alignment (OSRS drawLines semantics). */
int
ToriDraw2D_DrawStringBox(
    struct ToriDraw_Font* font,
    struct ToriDraw_ViewPort* view_port,
    int x,
    int y,
    int w,
    int h,
    char const* text,
    int color,
    int x_align,
    int y_align,
    int line_height,
    bool shadowed,
    int* pixel_buffer);

#endif
