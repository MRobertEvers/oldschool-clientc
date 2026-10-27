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

struct IconVisit
{
    int count;
    int x;
    int y;
};

static void
icon_visit_glyph(
    void* ctx,
    struct ToriDraw_Font* font,
    int glyph_index,
    int x,
    int y,
    int color_rgb)
{
    (void)ctx;
    (void)font;
    (void)glyph_index;
    (void)x;
    (void)y;
    (void)color_rgb;
}

static void
icon_visit_icon(
    void* ctx,
    struct ToriDraw_FontIcon const* icon,
    int x,
    int y)
{
    struct IconVisit* visit = (struct IconVisit*)ctx;
    (void)icon;
    visit->count++;
    visit->x = x;
    visit->y = y;
}

int
main(void)
{
    struct ToriDraw_Font* font = ToriRSChromeFont_Small();
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

    /* Every `<` opens a tag and none of it is drawn, whatever it says (deob
     * class671's draw loop). A tag nothing knows, or a value that does not
     * parse, changes nothing but is still swallowed; an unclosed `<` swallows
     * the rest of the line. Content that means a literal `<` writes `<lt>`. */
    check(ToriDraw2D_MeasureString(font, "ab<str") == ToriDraw2D_MeasureString(font, "ab"),
          "an unterminated <str swallows the rest of the line");
    check(ToriDraw2D_MeasureString(font, "<strong>abcd") == plain_w, "an unknown <strong> tag is swallowed");
    check(ToriDraw2D_MeasureString(font, "<str=zz>abcd") == plain_w, "a <str=> with bad hex is swallowed");
    check(ToriDraw2D_MeasureString(font, "<col=0xff0000>abcd") == plain_w,
          "<col=0xff0000> (a value the reference's parser refuses) is swallowed");
    check(ToriDraw2D_MeasureString(font, "<img=22>abcd") == plain_w, "<img=22> draws no glyphs");

    printf("tag values parse as the reference parses them\n");
    canvas_draw(canvas, font, "<col=ff>abcd");
    check(canvas_count(canvas, (int)0xFF0000FF) > 0, "<col=ff> is blue: any number of hex digits");
    canvas_draw(canvas, font, "<col=0xff0000>abcd");
    check(canvas_count(canvas, (int)0xFFFF0000) == 0, "<col=0xff0000> does not turn the text red");
    canvas_draw(canvas, font, "<str=zz>abcd");
    check(canvas_count(canvas, STRIKE_DEFAULT_ARGB) == 0, "<str=zz> strikes nothing");

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

    struct Canvas* plain=calloc(1,sizeof(*plain));
    assert(plain);
    canvas_draw(plain,font,"<col=ff0000>A</col>");
    canvas_init(canvas);
    ToriDraw2D_DrawString(font,&canvas->view_port,TEXT_X,TEXT_BASELINE,
        "<col=ff0000>A</col>",TEXT_RGB,false,2,canvas->pixels);
    bool outline_ok=true,color_ok=true;int outline_edges=0;
    int const neighbors[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
    for( int y=1;y<CANVAS_H-1;++y ) for( int x=1;x<CANVAS_W-1;++x )
    {
        int pixel=canvas_at(plain,x,y);
        if( !pixel ) continue;
        color_ok &= canvas_at(canvas,x,y)==pixel;
        for( int n=0;n<4;++n )
        {
            int nx=x+neighbors[n][0],ny=y+neighbors[n][1];
            if( canvas_at(plain,nx,ny) ) continue;
            ++outline_edges;outline_ok &= (uint32_t)canvas_at(canvas,nx,ny)==UINT32_C(0xff000000);
        }
    }
    check(color_ok,"outline preserves markup foreground colors");
    check(outline_ok && outline_edges>0,"outline draws black glyph edges on all four sides");
    free(plain);

    /* The chat filter buttons: "Game<br> " over "<br>On" in one centred box.
     * The blank second line draws nothing but still takes a line of the
     * block, or "Game" centres onto "On". The GL3 renderer's own copy of this
     * layout dropped it; every renderer now places box text through here. */
    printf("a blank line still counts for box alignment\n");
    {
        struct ToriDraw_FontBoxLine top[TORIDRAW_FONT_BOX_MAX_LINES];
        struct ToriDraw_FontBoxLine bottom[TORIDRAW_FONT_BOX_MAX_LINES];
        struct ToriDraw_FontBoxLine alone[TORIDRAW_FONT_BOX_MAX_LINES];
        int const lh = font->line_height > 0 ? font->line_height : 1;
        int const box_h = lh * 2 + 4;
        int const top_n =
            ToriDraw2D_LayoutStringBox(font, 0, 0, 56, box_h, "Game<br> ", 1, 1, 0, top);
        int const bottom_n =
            ToriDraw2D_LayoutStringBox(font, 0, 0, 56, box_h, "<br>On", 1, 1, 0, bottom);
        int const alone_n =
            ToriDraw2D_LayoutStringBox(font, 0, 0, 56, box_h, "Game", 1, 1, 0, alone);
        check(top_n == 1 && bottom_n == 1 && alone_n == 1, "blank lines are not handed out to draw");
        check(
            top_n == 1 && bottom_n == 1 && bottom[0].y - top[0].y == lh,
            "\"Game<br> \" and \"<br>On\" stack one line apart");
        check(
            top_n == 1 && alone_n == 1 && top[0].y < alone[0].y,
            "\"Game<br> \" sits above a lone \"Game\" in the same box");
    }
    /* `<img=N>` draws mod icon N with its bottom on the line's baseline and
     * advances by its width (deob class671 method14459 and the draw loop); an
     * N past the table draws nothing and takes no width. */
    printf("<img=N> draws its icon\n");
    {
        static uint32_t const red[3 * 5] = {
            0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u,
            0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u,
            0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u,
        };
        struct ToriDraw_FontIcon const icon = { 3, 5, red, 0, 0 };
        struct IconVisit visit = { 0 };
        ToriDraw_FontSetIcons(&icon, 1);
        check(
            ToriDraw2D_MeasureString(font, "<img=0>abcd") == plain_w + 3,
            "<img=0> measures as its icon's width");
        check(
            ToriDraw2D_MeasureString(font, "<img=1>abcd") == plain_w,
            "<img=1> past the table takes no width");
        check(
            ToriDraw_FontMarkupIconAdvance("a<img=0>", 8, 1) == 3,
            "ToriDraw_FontMarkupIconAdvance gives the icon's width");
        check(ToriDraw_FontMarkupIconAdvance("a<img=0>", 8, 0) == 0, "a glyph is no icon");
        ToriDraw_FontVisitGlyphsStyledIcons(
            font, "ab<img=0>", 0, 0, TEXT_RGB, false, icon_visit_glyph, icon_visit_icon, &visit);
        check(visit.count == 1, "the walk reaches the icon once");
        check(visit.x == ToriDraw2D_MeasureString(font, "ab"), "the icon follows the text before it");
        check(visit.y == ascent - 5, "the icon's bottom sits on the baseline");
        canvas_draw(canvas, font, "<img=0>");
        check(canvas_count(canvas, (int)0xFFFF0000u) == 3 * 5, "the software draw paints every icon pixel");
        check(
            canvas_at(canvas, TEXT_X, TEXT_BASELINE - 5) == (int)0xFFFF0000u &&
                canvas_at(canvas, TEXT_X + 2, TEXT_BASELINE - 1) == (int)0xFFFF0000u,
            "the icon is drawn from the pen with its bottom on the baseline");
        canvas_draw(canvas, font, "<img=1>");
        check(canvas_count(canvas, (int)0xFFFF0000u) == 0, "<img=1> past the table draws nothing");
        ToriDraw_FontSetIcons(NULL, 0);
    }
    /* `@key@` is CS2's translations_set table (deob class202.field2713): the
     * server sends "you've completed a quest: @q_mes@Imp Catcher" and the
     * draw puts the key's value, `<col=0ab0ff>`, in its place. The token takes
     * no width (class11 method187 skips it); an unknown key draws nothing. */
    printf("@key@ expands through the translations table\n");
    {
        ToriDraw_FontClearTranslations();
        check(
            ToriDraw2D_MeasureString(font, "@q_mes@abcd") > plain_w,
            "with no table, @q_mes@ is text, as the 2004 clients drew it");
        check(ToriDraw2D_MeasureString(font, "@red@abcd") == plain_w, "with no table, @red@ is still a colour code");
        canvas_draw(canvas, font, "@red@abcd");
        check(canvas_count(canvas, (int)0xFFFF0000) > 0, "and paints red");

        ToriDraw_FontSetTranslation("Q_MES", "<col=0ab0ff>");
        ToriDraw_FontSetTranslation("red", "<col=00ff00>");
        check(ToriDraw2D_MeasureString(font, "@q_mes@abcd") == plain_w, "@q_mes@ measures as nothing");
        check(
            visit_glyph_count(font, "@q_mes@abcd</col>") == visit_glyph_count(font, "abcd"),
            "the glyph walk emits no glyphs for @q_mes@");
        canvas_draw(canvas, font, "@q_mes@abcd</col>");
        check(canvas_count(canvas, (int)0xFF0AB0FF) > 0, "@q_mes@ paints its value's colour");
        canvas_draw(canvas, font, "@Q_MES@abcd");
        check(canvas_count(canvas, (int)0xFF0AB0FF) > 0, "keys match in any case");
        canvas_draw(canvas, font, "@red@abcd");
        check(
            canvas_count(canvas, (int)0xFF00FF00) > 0 && canvas_count(canvas, (int)0xFFFF0000) == 0,
            "a table entry outranks the 2004 colour of the same name");
        check(ToriDraw2D_MeasureString(font, "@no_such@abcd") == plain_w, "an unknown key draws nothing");
        check(
            ToriDraw2D_MeasureString(font, "@a b@abcd") > plain_w,
            "a key with a space is no key: the text is drawn");
        check(
            ToriDraw2D_MeasureString(font, "x@abcd") > ToriDraw2D_MeasureString(font, "xabcd"),
            "an unclosed @ is drawn");
        unsigned char emitted = 0;
        check(
            ToriDraw_FontMarkupTokenLength("<at>", 4, 0, &emitted) == 4 && emitted == '@',
            "<at> renders as @");
        ToriDraw_FontClearTranslations();
    }
    free(canvas);

    if( Failures > 0 )
    {
        printf("FAILED: %d\n", Failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
