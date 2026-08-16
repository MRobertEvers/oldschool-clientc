/*
 * Font markup: what the tag grammar swallows and what it draws.
 *
 * The failure this guards against is not subtle in the picture but is invisible
 * to every model-level test: a tag the grammar does not know is not skipped, so
 * the renderer draws its characters. That is how a quest journal ends up with
 * "<str>" typed across the start of each wrapped line — the server wrapper had
 * been re-emitting the strikethrough tag per line all along, and only the
 * renderer's grammar was short.
 *
 * So the assertions come in pairs: a *width* check (the tag must cost nothing
 * to measure, which is exactly what a literal-rendered tag would break) and a
 * *pixel* check (the rule is actually stroked, in the right colour, on the
 * right row, and stops where the closing tag says).
 *
 * Uses the baked debug font, so it needs no cache. Run:
 *   make -C src test-font-markup
 */

#include "engine/torirs_debug_font_baked.h"

#include "toridraw_font.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS_W 320
#define CANVAS_H 64

/* Drawn at this baseline; DrawString subtracts the ascent to get the line top. */
#define TEXT_X 8
#define TEXT_BASELINE 40

#define TEXT_RGB 0xFFFFFF
#define STRIKE_DEFAULT_ARGB ((int)(0xFF000000u | TORIDRAW_FONT_STRIKE_DEFAULT_RGB))

static int Failures = 0;

static void
check(
    bool ok,
    char const* what)
{
    if( !ok )
    {
        printf("  FAIL: %s\n", what);
        Failures++;
    }
    else
    {
        printf("  ok:   %s\n", what);
    }
}

struct Canvas
{
    int pixels[CANVAS_W * CANVAS_H];
    struct ToriDraw_ViewPort view_port;
};

static void
canvas_init(struct Canvas* canvas)
{
    memset(canvas->pixels, 0, sizeof(canvas->pixels));
    memset(&canvas->view_port, 0, sizeof(canvas->view_port));
    canvas->view_port.width = CANVAS_W;
    canvas->view_port.height = CANVAS_H;
    canvas->view_port.stride = CANVAS_W;
    canvas->view_port.clip_left = 0;
    canvas->view_port.clip_top = 0;
    canvas->view_port.clip_right = CANVAS_W;
    canvas->view_port.clip_bottom = CANVAS_H;
}

static void
canvas_draw(
    struct Canvas* canvas,
    struct ToriDraw_Font* font,
    char const* text)
{
    canvas_init(canvas);
    ToriDraw2D_DrawString(
        font,
        &canvas->view_port,
        TEXT_X,
        TEXT_BASELINE,
        text,
        TEXT_RGB,
        false,
        false,
        canvas->pixels);
}

static int
canvas_at(
    struct Canvas const* canvas,
    int x,
    int y)
{
    assert(x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H);
    return canvas->pixels[y * CANVAS_W + x];
}

/** How many pixels of exactly `argb` the whole canvas holds. */
static int
canvas_count(
    struct Canvas const* canvas,
    int argb)
{
    int count = 0;
    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
    {
        if( canvas->pixels[i] == argb )
            count++;
    }
    return count;
}

/** Rightmost x on row `y` painted exactly `argb`, or -1 if the row has none. */
static int
canvas_row_last(
    struct Canvas const* canvas,
    int y,
    int argb)
{
    for( int x = CANVAS_W - 1; x >= 0; x-- )
    {
        if( canvas_at(canvas, x, y) == argb )
            return x;
    }
    return -1;
}

/** The single row holding pixels of `argb`, or -1 if none / more than one. */
static int
canvas_find_row(
    struct Canvas const* canvas,
    int argb)
{
    int found = -1;
    for( int y = 0; y < CANVAS_H; y++ )
    {
        for( int x = 0; x < CANVAS_W; x++ )
        {
            if( canvas_at(canvas, x, y) != argb )
                continue;
            if( found >= 0 && found != y )
                return -1;
            found = y;
            break;
        }
    }
    return found;
}

static int
canvas_row_count(
    struct Canvas const* canvas,
    int y,
    int argb)
{
    int count = 0;
    for( int x = 0; x < CANVAS_W; x++ )
    {
        if( canvas_at(canvas, x, y) == argb )
            count++;
    }
    return count;
}

struct GlyphCount
{
    int count;
};

static void
count_glyph(
    void* ctx,
    struct ToriDraw_Font* font,
    int gi,
    int x,
    int y,
    int color_rgb)
{
    (void)font;
    (void)gi;
    (void)x;
    (void)y;
    (void)color_rgb;
    ((struct GlyphCount*)ctx)->count++;
}

static int
visit_glyph_count(
    struct ToriDraw_Font* font,
    char const* text)
{
    struct GlyphCount ctx = { 0 };
    ToriDraw_FontVisitGlyphs(font, text, TEXT_X, TEXT_BASELINE, TEXT_RGB, count_glyph, &ctx);
    return ctx.count;
}

int
main(void)
{
    struct ToriDraw_Font* font = ToriDbgFont_Small();
    struct Canvas* canvas = malloc(sizeof(*canvas));
    assert(font && canvas);

    int const ascent = font->line_height > 0 ? font->line_height : 1;
    int const line_top = TEXT_BASELINE - ascent;
    int const strike_y = line_top + (ascent * 7) / 10;
    int const underline_y = line_top + ascent + 1;

    printf("font: ascent %d, strike row %d, underline row %d\n", ascent, strike_y, underline_y);

    /* The tag costs nothing to measure — the whole point of a grammar entry. */
    printf("markup is not text\n");
    int const plain_w = ToriDraw2D_MeasureString(font, "abcd");
    check(
        ToriDraw2D_MeasureString(font, "<str>abcd</str>") == plain_w,
        "<str>abcd</str> measures as wide as abcd");
    check(
        ToriDraw2D_MeasureString(font, "<str=00ff00>abcd</str>") == plain_w,
        "<str=rrggbb> measures as wide as its text");
    check(
        visit_glyph_count(font, "<str>abcd</str>") == visit_glyph_count(font, "abcd"),
        "the glyph walk emits no glyphs for the tag");

    /* An unclosed or malformed tag still has to render as text, or a stray '<'
     * in content would silently eat the rest of the line. There is no kerning,
     * so a literal run measures as the sum of its pieces. */
    int const lt_w = ToriDraw2D_MeasureString(font, "<");
    check(
        ToriDraw2D_MeasureString(font, "<str") == lt_w + ToriDraw2D_MeasureString(font, "str"),
        "an unterminated <str is left as literal text");
    check(
        ToriDraw2D_MeasureString(font, "<strong>") ==
            lt_w + ToriDraw2D_MeasureString(font, "strong") + ToriDraw2D_MeasureString(font, ">"),
        "an unknown <strong> tag is left as literal text");
    check(
        ToriDraw2D_MeasureString(font, "<str=zz>") ==
            lt_w + ToriDraw2D_MeasureString(font, "str=zz") + ToriDraw2D_MeasureString(font, ">"),
        "a <str=> with bad hex is left as literal text");

    printf("the rule is drawn\n");
    canvas_draw(canvas, font, "abcd");
    check(canvas_count(canvas, STRIKE_DEFAULT_ARGB) == 0, "untagged text strikes nothing");

    canvas_draw(canvas, font, "<str>abcd</str>");
    int const struck = canvas_count(canvas, STRIKE_DEFAULT_ARGB);
    check(struck > 0, "<str> strikes in the reference's default red");
    check(
        canvas_row_last(canvas, strike_y, STRIKE_DEFAULT_ARGB) >= TEXT_X + plain_w - 2,
        "the strike reaches the end of the struck run");
    check(
        canvas_row_count(canvas, strike_y, STRIKE_DEFAULT_ARGB) == struck,
        "every struck pixel is on the one strike row");
    check(
        canvas_find_row(canvas, STRIKE_DEFAULT_ARGB) == strike_y,
        "the strike lands 0.7 of the ascent below the line top, as the reference does");

    canvas_draw(canvas, font, "<str=00ff00>abcd</str>");
    check(
        canvas_count(canvas, (int)0xFF00FF00) > 0 &&
            canvas_count(canvas, STRIKE_DEFAULT_ARGB) == 0,
        "<str=00ff00> strikes green, not the default");

    printf("the closing tag stops it\n");
    int const ab_w = ToriDraw2D_MeasureString(font, "ab");
    canvas_draw(canvas, font, "<str>ab</str>cd");
    int const partial_last = canvas_row_last(canvas, strike_y, STRIKE_DEFAULT_ARGB);
    check(partial_last >= 0, "the struck prefix is struck");
    check(
        partial_last < TEXT_X + ab_w + 1,
        "</str> stops the rule at the tag, not at the end of the line");
    check(
        canvas_at(canvas, TEXT_X + plain_w - 1, strike_y) != STRIKE_DEFAULT_ARGB,
        "text after </str> is not struck");

    printf("it composes with the underline\n");
    canvas_draw(canvas, font, "<u><str>abcd</str></u>");
    int const struck_row = canvas_find_row(canvas, STRIKE_DEFAULT_ARGB);
    int const underlined_row = canvas_row_last(canvas, underline_y, (int)0xFF000000) >= 0
                                   ? underline_y
                                   : -1;
    check(struck_row == strike_y, "the strike survives an enclosing <u>");
    check(underlined_row == underline_y, "the underline is drawn too");
    check(
        struck_row > line_top && struck_row < underlined_row,
        "the strike crosses the glyphs, the underline sits below them");

    canvas_draw(canvas, font, "<str><col=00ff00>abcd</col></str>");
    check(
        canvas_count(canvas, STRIKE_DEFAULT_ARGB) == struck,
        "a colour change inside the struck run does not recolour the rule");

    free(canvas);

    if( Failures > 0 )
    {
        printf("FAILED: %d\n", Failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
