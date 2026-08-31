#include "toridraw_font.h"

#include "osrs/colors.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_FONT_ADVANCE_ONLY_GLYPH TORIDRAW_FONT_GLYPH_COUNT

const uint16_t TORIDRAW_FONT_CHARSET[] = {
    'A',    'B', 'C',  'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',  'N', 'O', 'P',
    'Q',    'R', 'S',  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c',  'd', 'e', 'f',
    'g',    'h', 'i',  'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',  't', 'u', 'v',
    'w',    'x', 'y',  'z', '0', '1', '2', '3', '4', '5', '6', '7', '8',  '9', '!', '"',
    0x00A3, '$', '%',  '^', '&', '*', '(', ')', '-', '_', '=', '+', '[',  '{', ']', '}',
    ';',    ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/', '?', '\\', '|'
};

_Static_assert(
    sizeof(TORIDRAW_FONT_CHARSET) / sizeof(TORIDRAW_FONT_CHARSET[0]) == TORIDRAW_FONT_GLYPH_COUNT,
    "TORIDRAW_FONT_CHARSET must have exactly TORIDRAW_FONT_GLYPH_COUNT entries");

uint16_t const*
ToriDraw_FontCharsetTable(void)
{
    return TORIDRAW_FONT_CHARSET;
}

static int
font_index_of_char(uint8_t c)
{
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( (TORIDRAW_FONT_CHARSET[i] & 0xFF) == c )
            return i;
    }
    return -1;
}

static void
font_init_charcodeset(struct ToriDraw_Font* font)
{
    for( int i = 0; i < 256; i++ )
    {
        int c = font_index_of_char((uint8_t)i);
        /* A character this font has no record for draws nothing and advances
         * like a space. It must NOT fall back to the last glyph record --
         * that slot is '|', and every unknown byte would draw a bar. */
        if( c < 0 || c >= TORIDRAW_FONT_GLYPH_COUNT )
            c = TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;
        font->charcodeset[i] = (char)c;
    }
    /* The space is the one character with no glyph record: it is the
     * advance-only slot past the end of the 94 records. @see the CHARSET
     * note in 3rd/rscache dat1_pix_font.c, which this table mirrors. */
    font->charcodeset[(unsigned char)' '] = (char)TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;
}

static void
font_finish_draw_widths(struct ToriDraw_Font* font)
{
    int const fallback = font->advance[8] > 0 ? font->advance[8] : 4;

    if( font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH] <= 0 )
        font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH] = fallback;

    for( int i = 0; i < 256; i++ )
        font->draw_width[i] = font->advance[(unsigned char)font->charcodeset[i]];

    font->draw_width[(unsigned char)' '] = font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH];
}

void
ToriDraw_FontFinishDrawWidths(struct ToriDraw_Font* font)
{
    font_finish_draw_widths(font);
}

int
ToriDraw_FontEvaluateColorTag(char const tag[3])
{
    assert(tag);
    if( tag[0] == 'c' && tag[1] == 'y' && tag[2] == 'a' )
        return CYAN;
    if( tag[0] == 'w' && tag[1] == 'h' && tag[2] == 'i' )
        return WHITE;
    if( tag[0] == 'y' && tag[1] == 'e' && tag[2] == 'l' )
        return YELLOW;
    if( tag[0] == 'r' && tag[1] == 'e' && tag[2] == 'd' )
        return RED;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == 'e' )
        return GREEN;
    if( tag[0] == 'b' && tag[1] == 'l' && tag[2] == 'u' )
        return BLUE;
    if( tag[0] == 'm' && tag[1] == 'a' && tag[2] == 'g' )
        return MAGENTA;
    if( tag[0] == 'b' && tag[1] == 'l' && tag[2] == 'a' )
        return BLACK;
    if( tag[0] == 'l' && tag[1] == 'r' && tag[2] == 'e' )
        return LIGHTRED;
    if( tag[0] == 'd' && tag[1] == 'r' && tag[2] == 'e' )
        return DARKRED;
    if( tag[0] == 'd' && tag[1] == 'b' && tag[2] == 'l' )
        return DARKBLUE;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '1' )
        return ORANGE1;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '2' )
        return ORANGE2;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '3' )
        return ORANGE3;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '1' )
        return GREEN1;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '2' )
        return GREEN2;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '3' )
        return GREEN3;
    return -1;
}

static bool
font_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int
font_parse_hex_rgb(char const* hex, int len)
{
    if( !hex || (len != 6 && len != 8) )
        return -1;

    int rgb = 0;
    for( int i = 0; i < 6; i++ )
    {
        if( !font_is_hex_digit(hex[i]) )
            return -1;
        int v;
        char const c = hex[i];
        if( c >= '0' && c <= '9' )
            v = c - '0';
        else if( c >= 'a' && c <= 'f' )
            v = c - 'a' + 10;
        else
            v = c - 'A' + 10;
        rgb = (rgb << 4) | v;
    }

    if( len == 8 )
    {
        for( int i = 6; i < 8; i++ )
        {
            if( !font_is_hex_digit(hex[i]) )
                return -1;
        }
    }

    return rgb;
}

int
ToriDraw_FontParseHexColor(char const* hex, int len)
{
    return font_parse_hex_rgb(hex, len);
}

static bool
font_char_eq_icase(
    char a,
    char b)
{
    if( a >= 'A' && a <= 'Z' )
        a = (char)(a + ('a' - 'A'));
    if( b >= 'A' && b <= 'Z' )
        b = (char)(b + ('a' - 'A'));
    return a == b;
}

static bool
font_match_icase_literal(
    char const* p,
    char const* lit,
    int lit_len)
{
    for( int k = 0; k < lit_len; k++ )
    {
        if( !font_char_eq_icase(p[k], lit[k]) )
            return false;
    }
    return true;
}

/**
 * The style the markup tags mutate as a string is walked: glyph colour plus the
 * two rule colours, where -1 means the rule is off. One struct because the
 * tokens that set them share a grammar, so every walk that needs one of them
 * has to skip all of them — a walk that knows `<col=…>` but not `<str>` prints
 * the tag it does not know as literal text.
 */
struct FontMarkupStyle
{
    int color;
    int underline;
    int strike;
};

static struct FontMarkupStyle
font_markup_style_init(int default_color)
{
    struct FontMarkupStyle style;
    style.color = default_color;
    style.underline = -1;
    style.strike = -1;
    return style;
}

/**
 * `style` is optional: pass NULL to only measure the token grammar (how many
 * bytes the token spans and what character it renders), which is what the
 * measuring and wrapping walks want.
 */
static int
font_try_consume_markup(
    char const* text,
    int len,
    int i,
    int default_color,
    struct FontMarkupStyle* style,
    unsigned char* emit_char_out)
{
    if( i < 0 || i >= len )
        return 0;
    assert(text);

    if( emit_char_out )
        *emit_char_out = 0;

    if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
    {
        if( style )
        {
            int const tagged = ToriDraw_FontEvaluateColorTag(&text[i + 1]);
            if( tagged >= 0 )
                style->color = tagged;
        }
        return 5;
    }

    if( text[i] == '<' && len - i >= 4 &&
        font_match_icase_literal(&text[i + 1], "gt>", 3) )
    {
        if( emit_char_out )
            *emit_char_out = '>';
        return 4;
    }

    if( text[i] == '<' && len - i >= 4 &&
        font_match_icase_literal(&text[i + 1], "lt>", 3) )
    {
        if( emit_char_out )
            *emit_char_out = '<';
        return 4;
    }

    if( text[i] == '<' && len - i >= 6 && strncmp(&text[i], "</col>", 6) == 0 )
    {
        if( style )
            style->color = default_color;
        return 6;
    }

    if( text[i] == '<' && len - i >= 4 && strncmp(&text[i], "</u>", 4) == 0 )
    {
        if( style )
            style->underline = -1;
        return 4;
    }

    if( text[i] == '<' && len - i >= 6 && strncmp(&text[i], "</str>", 6) == 0 )
    {
        if( style )
            style->strike = -1;
        return 6;
    }

    if( text[i] == '<' && len - i >= 10 && strncmp(&text[i], "<col=", 5) == 0 )
    {
        int j = i + 5;
        int hex_len = 0;
        while( j < len && hex_len < 8 && font_is_hex_digit(text[j]) )
        {
            j++;
            hex_len++;
        }
        if( (hex_len == 6 || hex_len == 8) && j < len && text[j] == '>' )
        {
            if( style )
            {
                int const parsed = font_parse_hex_rgb(&text[i + 5], hex_len);
                if( parsed >= 0 )
                    style->color = parsed;
            }
            return j - i + 1;
        }
    }

    /* Colored underline only — does not recolor glyphs (OSRS AbstractFont). */
    if( text[i] == '<' && len - i >= 3 && text[i + 1] == 'u' )
    {
        if( text[i + 2] == '>' )
        {
            if( style )
                style->underline = 0;
            return 3;
        }
        if( text[i + 2] == '=' && len - i >= 10 )
        {
            int j = i + 3;
            int hex_len = 0;
            while( j < len && hex_len < 8 && font_is_hex_digit(text[j]) )
            {
                j++;
                hex_len++;
            }
            if( (hex_len == 6 || hex_len == 8) && j < len && text[j] == '>' )
            {
                if( style )
                {
                    int const parsed = font_parse_hex_rgb(&text[i + 3], hex_len);
                    if( parsed >= 0 )
                        style->underline = parsed;
                }
                return j - i + 1;
            }
        }
    }

    /* Strikethrough, same shape as underline: bare `<str>` takes the reference's
     * fixed dark red (deob class671 sets 0x800000), `<str=rrggbb>` overrides it,
     * and neither recolors glyphs. */
    if( text[i] == '<' && len - i >= 5 && strncmp(&text[i], "<str", 4) == 0 )
    {
        if( text[i + 4] == '>' )
        {
            if( style )
                style->strike = TORIDRAW_FONT_STRIKE_DEFAULT_RGB;
            return 5;
        }
        if( text[i + 4] == '=' && len - i >= 12 )
        {
            int j = i + 5;
            int hex_len = 0;
            while( j < len && hex_len < 8 && font_is_hex_digit(text[j]) )
            {
                j++;
                hex_len++;
            }
            if( (hex_len == 6 || hex_len == 8) && j < len && text[j] == '>' )
            {
                if( style )
                {
                    int const parsed = font_parse_hex_rgb(&text[i + 5], hex_len);
                    if( parsed >= 0 )
                        style->strike = parsed;
                }
                return j - i + 1;
            }
        }
    }

    return 0;
}

int
ToriDraw_FontMarkupTokenLength(
    char const* text,
    int len,
    int index,
    unsigned char* emit_char_out)
{
    return font_try_consume_markup(text, len, index, 0, NULL, emit_char_out);
}

static int
font_space_advance(struct ToriDraw_Font const* font)
{
    int const adv = font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH];
    return adv > 0 ? adv : 4;
}

static bool
font_is_rs_space_char(unsigned char ch)
{
    /* The space alone. '|' is a printable glyph in this font family -- it is
     * the caret both references draw on the login screen ("@yel@|") -- and
     * calling it a space made that caret invisible. */
    return ch == ' ';
}

static int
font_glyph_advance(
    struct ToriDraw_Font const* font,
    int gi)
{
    int adv = font->advance[gi];
    return adv > 0 ? adv : 4;
}

void
ToriDraw_FontInitCharcodeset(struct ToriDraw_Font* font)
{
    font_init_charcodeset(font);
}

void
ToriDraw_FontFree(struct ToriDraw_Font* font)
{
    if( !font )
        return;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        free(font->glyph_alpha[i]);
        font->glyph_alpha[i] = NULL;
    }
    free(font);
}

bool
ToriDraw_FontValidate(struct ToriDraw_Font* font)
{
    assert(font);

    for( int i = 0; i < 256; i++ )
    {
        int const gi = (unsigned char)font->charcodeset[i];
        if( gi > TORIDRAW_FONT_ADVANCE_ONLY_GLYPH )
            return false;
    }

    bool has_drawable_glyph = false;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int const gw = font->glyph_width[i];
        int const gh = font->glyph_height[i];
        if( gw < 0 || gh < 0 )
            return false;
        if( gw > 0 && gh > 0 )
        {
            if( !font->glyph_alpha[i] )
                return false;
            has_drawable_glyph = true;
        }
    }
    return has_drawable_glyph;
}

static int
font_glyph_index(
    struct ToriDraw_Font const* font,
    unsigned char ch)
{
    if( font_is_rs_space_char(ch) )
        return TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;

    int gi = (unsigned char)font->charcodeset[ch];
    if( gi >= 0 && gi <= TORIDRAW_FONT_ADVANCE_ONLY_GLYPH )
        return gi;

    gi = (unsigned char)font->charcodeset[(unsigned char)' '];
    if( gi >= 0 && gi <= TORIDRAW_FONT_ADVANCE_ONLY_GLYPH )
        return gi;

    assert(!"invalid font charcodeset");
    return TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;
}

static bool
font_glyph_drawable(
    struct ToriDraw_Font const* font,
    int gi)
{
    assert(font);
    assert(gi >= 0);
    assert(gi <= TORIDRAW_FONT_ADVANCE_ONLY_GLYPH);

    /* The advance-only glyph is a legitimate answer from font_glyph_index --
     * a space, or any character this font's charcodeset does not map (an
     * NBSP out of a chat line) -- and it indexes one past every glyph array.
     * It moves the pen and draws nothing. */
    if( gi == TORIDRAW_FONT_ADVANCE_ONLY_GLYPH )
        return false;

    int const gw = font->glyph_width[gi];
    int const gh = font->glyph_height[gi];
    if( gw <= 0 || gh <= 0 )
        return false;
    if( !font->glyph_alpha[gi] )
        return false;
    return true;
}

static bool
font_line_break_at(
    char const* p,
    int* advance_out)
{
    assert(p);
    if( p[0] == '\0' )
        return false;

    if( p[0] == '\\' && p[1] == 'n' )
    {
        *advance_out = 2;
        return true;
    }

    if( p[0] == '\r' && p[1] == '\n' )
    {
        *advance_out = 2;
        return true;
    }

    if( p[0] == '\n' || p[0] == '\r' )
    {
        *advance_out = 1;
        return true;
    }

    if( p[0] == '<' && font_char_eq_icase(p[1], 'b') && font_char_eq_icase(p[2], 'r') &&
        p[3] == '/' && font_char_eq_icase(p[4], '>') )
    {
        *advance_out = 5;
        return true;
    }

    if( p[0] == '<' && font_char_eq_icase(p[1], 'b') && font_char_eq_icase(p[2], 'r') &&
        p[3] == '>' )
    {
        *advance_out = 4;
        return true;
    }

    return false;
}

static char const*
font_next_line(
    char const* rest,
    int* line_len_out,
    int* break_advance_out)
{
    char const* p = rest;
    while( p[0] != '\0' )
    {
        if( font_line_break_at(p, break_advance_out) )
        {
            *line_len_out = (int)(p - rest);
            return p;
        }
        p++;
    }

    *line_len_out = (int)(p - rest);
    *break_advance_out = 0;
    return p;
}

static int
font_measure_range(
    struct ToriDraw_Font* font,
    char const* text,
    int len)
{
    assert(font && text && len > 0);

    int width = 0;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                width += font_glyph_advance(font, gi);
            }
            i += consumed - 1;
            continue;
        }
        if( font_is_rs_space_char((unsigned char)text[i]) )
        {
            width += space_adv;
            continue;
        }
        int const gi = font_glyph_index(font, (unsigned char)text[i]);
        width += font_glyph_advance(font, gi);
    }
    return width;
}

int
ToriDraw2D_MeasureString(
    struct ToriDraw_Font* font,
    char const* text)
{
    assert(font && text);

    int max_width = 0;
    char const* rest = text;

    for( ;; )
    {
        int line_len = 0;
        int break_advance = 0;
        font_next_line(rest, &line_len, &break_advance);

        int const line_width = font_measure_range(font, rest, line_len);
        if( line_width > max_width )
            max_width = line_width;

        if( break_advance == 0 )
            break;

        rest += line_len + break_advance;
    }

    return max_width;
}

static int
font_wrap_segment_max_width(
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int max_width)
{
    assert(font);
    assert(text);
    if( len <= 0 )
        return 0;
    if( max_width <= 0 )
        return font_measure_range(font, text, len);

    int max_line = 0;
    int cur_w = 0;
    int word_start = 0;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i <= len; i++ )
    {
        if( i < len && text[i] != ' ' )
            continue;

        int const word_len = i - word_start;
        int const word_w =
            word_len > 0 ? font_measure_range(font, text + word_start, word_len) : 0;

        if( word_len == 0 )
        {
            word_start = i + 1;
            continue;
        }

        int const candidate = cur_w == 0 ? word_w : (cur_w + space_adv + word_w);
        if( cur_w > 0 && candidate > max_width )
        {
            if( cur_w > max_line )
                max_line = cur_w;
            cur_w = word_w;
        }
        else
            cur_w = candidate;

        word_start = i + 1;
    }

    if( cur_w > max_line )
        max_line = cur_w;
    return max_line;
}

int
ToriDraw2D_WrapMaxLineWidth(
    struct ToriDraw_Font* font,
    char const* text,
    int max_width)
{
    assert(font && text);

    if( text[0] == '\0' )
        return 0;

    int max_line = 0;
    char const* rest = text;

    for( ;; )
    {
        int line_len = 0;
        int break_advance = 0;
        font_next_line(rest, &line_len, &break_advance);

        if( line_len > 0 )
        {
            int const w = font_wrap_segment_max_width(font, rest, line_len, max_width);
            if( w > max_line )
                max_line = w;
        }

        if( break_advance == 0 )
            break;

        rest += line_len + break_advance;
    }

    return max_line;
}

static int
font_wrap_segment_line_count(
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int max_width)
{
    assert(font);
    assert(text);
    if( len <= 0 )
        return 0;

    if( max_width <= 0 )
        return 1;

    int lines = 1;
    int cur_w = 0;
    int word_start = 0;

    for( int i = 0; i <= len; i++ )
    {
        bool const at_end = i == len;
        bool const is_space = !at_end && text[i] == ' ';
        if( !at_end && !is_space )
            continue;

        int const word_len = i - word_start;
        if( word_len <= 0 )
        {
            word_start = i + 1;
            continue;
        }

        int word_w = font_measure_range(font, text + word_start, word_len);
        int space_adv = 0;
        if( !at_end && is_space )
            space_adv = font->glyph_width[(unsigned char)' '];

        int const candidate = cur_w == 0 ? word_w : (cur_w + space_adv + word_w);
        if( cur_w > 0 && candidate > max_width )
        {
            lines++;
            cur_w = word_w;
        }
        else
            cur_w = candidate;

        word_start = i + 1;
    }

    return lines > 0 ? lines : 1;
}

int
ToriDraw2D_WrapLineCount(
    struct ToriDraw_Font* font,
    char const* text,
    int max_width)
{
    assert(font && text);

    if( text[0] == '\0' )
        return 0;

    int total = 0;
    char const* rest = text;

    for( ;; )
    {
        int line_len = 0;
        int break_advance = 0;
        font_next_line(rest, &line_len, &break_advance);

        if( line_len > 0 )
            total += font_wrap_segment_line_count(font, rest, line_len, max_width);
        else
            total += 1;

        if( break_advance == 0 )
            break;

        rest += line_len + break_advance;
    }

    return total > 0 ? total : 1;
}

static int
font_draw_glyph_pixels(
    struct ToriDraw_Font const* font,
    int gi,
    int gx,
    int gy,
    int color,
    int cl,
    int ct,
    int cr,
    int cb,
    int stride,
    toripixel_t* pixel_buffer)
{
    if( !font_glyph_drawable(font, gi) )
        return 0;

    int pixels_written = 0;
    int const gw = font->glyph_width[gi];
    int const gh = font->glyph_height[gi];

    /* Clamp the glyph's row/column range once. Glyphs are 5-12 px wide and
     * text is drawn thousands of times a frame, so the two clip compares this
     * removes from every pixel were a large share of the per-glyph work. */
    int col_begin = 0;
    int col_stop = gw;
    if( gx < cl )
        col_begin = cl - gx;
    if( gx + col_stop > cr )
        col_stop = cr - gx;

    int row_begin = 0;
    int row_stop = gh;
    if( gy < ct )
        row_begin = ct - gy;
    if( gy + row_stop > cb )
        row_stop = cb - gy;

    if( col_begin >= col_stop || row_begin >= row_stop )
        return 0;

    for( int row = row_begin; row < row_stop; row++ )
    {
        uint8_t const* arow = font->glyph_alpha[gi] + (size_t)row * gw;
        toripixel_t* drow = pixel_buffer + (size_t)(gy + row) * stride + gx;

        for( int col = col_begin; col < col_stop; col++ )
        {
            if( arow[col] == 0 )
                continue;
            drow[col] = toripixel_pack_argb8888((uint32_t)color);
            pixels_written++;
        }
    }
    return pixels_written;
}

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

void
ToriDraw_FontVisitGlyphs(
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    ToriDraw_FontGlyphCallback callback,
    void* ctx)
{
    ToriDraw_FontVisitGlyphsStyled(font, text, x, y, default_color_rgb, false, callback, ctx);
}

static void
font_visit_glyphs_range(
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int x,
    int y,
    int default_color_rgb,
    ToriDraw_FontGlyphCallback callback,
    void* ctx)
{
    assert(font && text && len > 0 && callback);

    /* Glyph callbacks draw glyphs only, so the rule colours this collects are
     * unused here — the two renderers that want rules stroke them separately. */
    struct FontMarkupStyle style = font_markup_style_init(default_color_rgb);
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, default_color_rgb, &style, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                if( font_glyph_drawable(font, gi) )
                {
                    int const gx = x + font->offset_x[gi];
                    int const gy = y + font->offset_y[gi];
                    callback(ctx, font, gi, gx, gy, style.color);
                }
                x += font_glyph_advance(font, gi);
            }
            i += consumed - 1;
            continue;
        }
        if( font_is_rs_space_char((unsigned char)text[i]) )
        {
            x += space_adv;
            continue;
        }

        int const gi = font_glyph_index(font, (unsigned char)text[i]);
        if( font_glyph_drawable(font, gi) )
        {
            int const gx = x + font->offset_x[gi];
            int const gy = y + font->offset_y[gi];
            callback(ctx, font, gi, gx, gy, style.color);
        }
        x += font_glyph_advance(font, gi);
    }
}

void
ToriDraw_FontVisitGlyphsStyled(
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    bool center,
    ToriDraw_FontGlyphCallback callback,
    void* ctx)
{
    assert(font && text && callback);

    int const line_step = font->line_height > 0 ? font->line_height : 1;
    char const* rest = text;

    for( ;; )
    {
        int line_len = 0;
        int break_advance = 0;
        char const* break_at = font_next_line(rest, &line_len, &break_advance);

        int line_x = x;
        if( center && line_len > 0 )
            line_x -= font_measure_range(font, rest, line_len) / 2;

        if( line_len > 0 )
            font_visit_glyphs_range(
                font, rest, line_len, line_x, y, default_color_rgb, callback, ctx);

        if( break_advance == 0 )
            break;

        y += line_step;
        rest = break_at + break_advance;
    }
}

static void
font_draw_mask(
    int w,
    int h,
    uint8_t const* src,
    int src_off,
    int src_step,
    toripixel_t* dst,
    int dst_off,
    int dst_step,
    int rgb)
{
    int hw = -(w >> 2);
    w = -(w & 0x3);
    for( int y = -h; y < 0; y++ )
    {
        for( int x = hw; x < 0; x++ )
        {
            if( src[src_off++] == 0 )
                dst_off++;
            else
                dst[dst_off++] = rgb;

            if( src[src_off++] == 0 )
                dst_off++;
            else
                dst[dst_off++] = rgb;

            if( src[src_off++] == 0 )
                dst_off++;
            else
                dst[dst_off++] = rgb;

            if( src[src_off++] == 0 )
                dst_off++;
            else
                dst[dst_off++] = rgb;
        }
        for( int x = w; x < 0; x++ )
        {
            if( src[src_off++] == 0 )
                dst_off++;
            else
                dst[dst_off++] = rgb;
        }
        src_off += src_step;
        dst_off += dst_step;
    }
}

/** One-pixel horizontal rule under (underline) or through (strike) one advance. */
static void
font_draw_rule_span(
    int x,
    int y,
    int width,
    int rgb,
    int cl,
    int ct,
    int cr,
    int cb,
    int stride,
    toripixel_t* pixel_buffer)
{
    if( !pixel_buffer || width <= 0 || y < ct || y >= cb )
        return;
    int x0 = x < cl ? cl : x;
    int x1 = x + width;
    if( x1 > cr )
        x1 = cr;
    if( x0 >= x1 )
        return;
    int const argb = (int)(0xFF000000u | (uint32_t)(rgb & 0xFFFFFF));
    toripixel_t* row = pixel_buffer + y * stride;
    for( int px = x0; px < x1; px++ )
        row[px] = toripixel_pack_argb8888((uint32_t)argb);
}

/** Both rules for one advance — they are independent and can be on together. */
static void
font_draw_style_rules(
    struct FontMarkupStyle const* style,
    int x,
    int advance,
    int underline_y,
    int strike_y,
    int cl,
    int ct,
    int cr,
    int cb,
    int stride,
    toripixel_t* pixel_buffer)
{
    if( style->strike >= 0 )
        font_draw_rule_span(
            x, strike_y, advance, style->strike, cl, ct, cr, cb, stride, pixel_buffer);
    if( style->underline >= 0 )
        font_draw_rule_span(
            x, underline_y, advance, style->underline, cl, ct, cr, cb, stride, pixel_buffer);
}

static int
font_draw_string_range(
    struct ToriDraw_Font const* font,
    char const* text,
    int len,
    int x,
    int y,
    int color,
    int cl,
    int ct,
    int cr,
    int cb,
    int stride,
    toripixel_t* pixel_buffer)
{
    assert(font && text && len > 0 && pixel_buffer);

    int pixels_written = 0;
    struct FontMarkupStyle style = font_markup_style_init(color);
    int const space_adv = font_space_advance(font);
    int const ascent = font->line_height > 0 ? font->line_height : 1;
    int const underline_y = y + ascent + 1;
    /* Reference puts the strike 0.7 of the ascent down from the line top
     * (deob class671: `(int)(ascent * 0.7) + top`), which lands it across the
     * x-height rather than on the baseline. */
    int const strike_y = y + (ascent * 7) / 10;

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, color, &style, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                int const opaque_color = (int)(0xFF000000u | (uint32_t)style.color);
                int const adv = font_glyph_advance(font, gi);
                pixels_written += font_draw_glyph_pixels(
                    font,
                    gi,
                    x + font->offset_x[gi],
                    y + font->offset_y[gi],
                    opaque_color,
                    cl,
                    ct,
                    cr,
                    cb,
                    stride,
                    pixel_buffer);
                font_draw_style_rules(
                    &style, x, adv, underline_y, strike_y, cl, ct, cr, cb, stride, pixel_buffer);
                x += adv;
            }
            i += consumed - 1;
            continue;
        }
        if( font_is_rs_space_char((unsigned char)text[i]) )
        {
            font_draw_style_rules(
                &style,
                x,
                space_adv,
                underline_y,
                strike_y,
                cl,
                ct,
                cr,
                cb,
                stride,
                pixel_buffer);
            x += space_adv;
            continue;
        }
        int const gi = font_glyph_index(font, (unsigned char)text[i]);
        int const opaque_color = (int)(0xFF000000u | (uint32_t)style.color);
        int const adv = font_glyph_advance(font, gi);
        pixels_written += font_draw_glyph_pixels(
            font,
            gi,
            x + font->offset_x[gi],
            y + font->offset_y[gi],
            opaque_color,
            cl,
            ct,
            cr,
            cb,
            stride,
            pixel_buffer);
        font_draw_style_rules(
            &style, x, adv, underline_y, strike_y, cl, ct, cr, cb, stride, pixel_buffer);
        x += adv;
    }
    return pixels_written;
}

static int
font_draw_string_shadow_range(
    struct ToriDraw_Font const* font,
    char const* text,
    int len,
    int x,
    int y,
    int cl,
    int ct,
    int cr,
    int cb,
    int stride,
    toripixel_t* pixel_buffer)
{
    assert(font && text && len > 0 && pixel_buffer);

    int pixels_written = 0;
    int const shadow_color = (int)0xFF000000u;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                pixels_written += font_draw_glyph_pixels(
                    font,
                    gi,
                    x + font->offset_x[gi] + 1,
                    y + font->offset_y[gi] + 1,
                    shadow_color,
                    cl,
                    ct,
                    cr,
                    cb,
                    stride,
                    pixel_buffer);
                x += font_glyph_advance(font, gi);
            }
            i += consumed - 1;
            continue;
        }
        if( font_is_rs_space_char((unsigned char)text[i]) )
        {
            x += space_adv;
            continue;
        }
        int const gi = font_glyph_index(font, (unsigned char)text[i]);
        pixels_written += font_draw_glyph_pixels(
            font,
            gi,
            x + font->offset_x[gi] + 1,
            y + font->offset_y[gi] + 1,
            shadow_color,
            cl,
            ct,
            cr,
            cb,
            stride,
            pixel_buffer);
        x += font_glyph_advance(font, gi);
    }
    return pixels_written;
}

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
    toripixel_t* pixel_buffer)
{
    assert(font && view_port && text && pixel_buffer);

    if( !ToriDraw_FontValidate(font) )
    {
        assert(!"ToriDraw2D_DrawString: invalid font");
        return 0;
    }

    int pixels_written = 0;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;

    y -= font->line_height;

    int const line_step = font->line_height > 0 ? font->line_height : 1;
    char const* rest = text;

    for( ;; )
    {
        int line_len = 0;
        int break_advance = 0;
        char const* break_at = font_next_line(rest, &line_len, &break_advance);

        int line_x = x;
        if( center && line_len > 0 )
            line_x -= font_measure_range(font, rest, line_len) / 2;

        if( line_len > 0 )
        {
            if( shadowed )
            {
                pixels_written += font_draw_string_shadow_range(
                    font, rest, line_len, line_x, y, cl, ct, cr, cb, stride, pixel_buffer);
            }
            pixels_written += font_draw_string_range(
                font, rest, line_len, line_x, y, color, cl, ct, cr, cb, stride, pixel_buffer);
        }

        if( break_advance == 0 )
            break;

        y += line_step;
        rest = break_at + break_advance;
    }

    return pixels_written;
}

static void
font_line_vertical_extents(
    struct ToriDraw_Font const* font,
    char const* text,
    int len,
    int* min_oy_out,
    int* visual_h_out)
{
    assert(font && min_oy_out && visual_h_out);

    int const fallback_h = font->line_height > 0 ? font->line_height : 1;
    int min_oy = 0;
    int max_bottom = 0;
    bool any = false;

    if( !text || len <= 0 )
    {
        *min_oy_out = 0;
        *visual_h_out = fallback_h;
        return;
    }

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                if( font_glyph_drawable(font, gi) )
                {
                    int const oy = font->offset_y[gi];
                    int const bottom = oy + font->glyph_height[gi];
                    if( !any || oy < min_oy )
                        min_oy = oy;
                    if( !any || bottom > max_bottom )
                        max_bottom = bottom;
                    any = true;
                }
            }
            i += consumed - 1;
            continue;
        }
        if( font_is_rs_space_char((unsigned char)text[i]) )
            continue;

        int const gi = font_glyph_index(font, (unsigned char)text[i]);
        if( !font_glyph_drawable(font, gi) )
            continue;

        int const oy = font->offset_y[gi];
        int const bottom = oy + font->glyph_height[gi];
        if( !any || oy < min_oy )
            min_oy = oy;
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }

    if( !any )
    {
        *min_oy_out = 0;
        *visual_h_out = fallback_h;
        return;
    }

    *min_oy_out = min_oy;
    *visual_h_out = max_bottom - min_oy;
    if( *visual_h_out <= 0 )
        *visual_h_out = fallback_h;
}

enum
{
    FONT_DRAW_BOX_MAX_LINES = 64,
};

/** Glyph line box: max(offset_y + glyph_height) over drawable glyphs.
 *  Returns 0 when the font has no drawable glyphs. */
static int
font_line_box_height(struct ToriDraw_Font const* font)
{
    assert(font);

    int max_bottom = 0;
    bool any = false;

    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( !font_glyph_drawable(font, i) )
            continue;

        int const bottom = font->offset_y[i] + font->glyph_height[i];
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }
    return any ? max_bottom : 0;
}

int
ToriDraw_FontLineBoxHeight(struct ToriDraw_Font const* font)
{
    assert(font);
    int const box = font_line_box_height(font);
    if( box > 0 )
        return box;
    return font->line_height > 0 ? font->line_height : 1;
}

static void
font_get_vertical_metrics(
    struct ToriDraw_Font const* font,
    int* max_ascent_out,
    int* max_descent_out)
{
    assert(font && max_ascent_out && max_descent_out);

    int const fallback_lh = font->line_height > 0 ? font->line_height : 1;
    int min_oy = 0;
    int max_bottom = 0;
    bool any = false;

    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( !font_glyph_drawable(font, i) )
            continue;

        int const oy = font->offset_y[i];
        int const bottom = oy + font->glyph_height[i];
        if( !any || oy < min_oy )
            min_oy = oy;
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }

    if( !any )
    {
        *max_ascent_out = fallback_lh;
        *max_descent_out = 0;
        return;
    }

    int const ascent = fallback_lh;
    int max_ascent = ascent - min_oy;
    int max_descent = max_bottom - ascent;
    if( max_ascent <= 0 )
        max_ascent = fallback_lh;
    if( max_descent < 0 )
        max_descent = 0;
    *max_ascent_out = max_ascent;
    *max_descent_out = max_descent;
}

static bool
font_should_auto_wrap_text(
    int widget_height,
    int line_height,
    int max_ascent,
    int max_descent)
{
    int const resolved_lh = line_height > 0 ? line_height : 1;
    int const ascent = max_ascent > 0 ? max_ascent : 0;
    int const descent = max_descent > 0 ? max_descent : 0;
    int const height = widget_height > 0 ? widget_height : 0;
    return !(height < resolved_lh + ascent + descent && height < resolved_lh * 2);
}

static bool
font_segment_has_visible_content(
    char const* text,
    int len)
{
    if( len <= 0 )
        return false;
    assert(text);

    for( int i = 0; i < len; i++ )
    {
        if( font_is_rs_space_char((unsigned char)text[i]) )
            continue;

        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
                return true;
            i += consumed - 1;
            continue;
        }
        return true;
    }
    return false;
}

static bool
font_append_draw_line(
    char const* lines[],
    int line_lens[],
    int* line_count,
    int max_lines,
    char const* start,
    int len)
{
    if( *line_count >= max_lines )
        return false;

    lines[*line_count] = start;
    line_lens[*line_count] = len;
    (*line_count)++;
    return true;
}

static bool
font_wrap_segment_emit(
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int max_width,
    char const* lines[],
    int line_lens[],
    int* line_count,
    int max_lines)
{
    if( len <= 0 )
        return font_append_draw_line(lines, line_lens, line_count, max_lines, text, 0);

    if( !font_segment_has_visible_content(text, len) )
        return font_append_draw_line(lines, line_lens, line_count, max_lines, text, 0);

    int const space_adv = font_space_advance(font);
    int cur_start = -1;
    int cur_len = 0;
    int cur_w = 0;
    int word_start = 0;

    for( int i = 0; i <= len; i++ )
    {
        bool const at_end = i == len;
        bool const is_space = !at_end && font_is_rs_space_char((unsigned char)text[i]);
        if( !at_end && !is_space )
            continue;

        int const word_len = i - word_start;
        if( word_len <= 0 )
        {
            word_start = at_end ? i : i + 1;
            continue;
        }

        int const word_w = font_measure_range(font, text + word_start, word_len);
        if( cur_len <= 0 )
        {
            cur_start = word_start;
            cur_len = word_len;
            cur_w = word_w;
        }
        else
        {
            int const candidate = cur_w + space_adv + word_w;
            if( candidate > max_width )
            {
                if( !font_append_draw_line(
                        lines, line_lens, line_count, max_lines, text + cur_start, cur_len) )
                    return false;
                cur_start = word_start;
                cur_len = word_len;
                cur_w = word_w;
            }
            else
            {
                cur_len = i - cur_start;
                cur_w = candidate;
            }
        }

        word_start = at_end ? i : i + 1;
    }

    if( cur_len > 0 )
        return font_append_draw_line(
            lines, line_lens, line_count, max_lines, text + cur_start, cur_len);

    return true;
}

static int
font_collect_draw_lines(
    struct ToriDraw_Font* font,
    char const* text,
    int w,
    int h,
    int line_height,
    char const* lines[],
    int line_lens[])
{
    int line_count = 0;
    assert(text);
    if( text[0] == '\0' )
        return 0;

    int const resolved_lh =
        line_height > 0 ? line_height : (font->line_height > 0 ? font->line_height : 1);
    int const logical_w = w > 0 ? w : 1;

    int max_ascent = resolved_lh;
    int max_descent = 0;
    font_get_vertical_metrics(font, &max_ascent, &max_descent);

    bool const auto_wrap =
        w > 0 && h > 0 &&
        font_should_auto_wrap_text(h, resolved_lh, max_ascent, max_descent);

    char const* rest = text;
    while( rest && rest[0] != '\0' && line_count < FONT_DRAW_BOX_MAX_LINES )
    {
        int segment_len = 0;
        int break_advance = 0;
        char const* break_at = font_next_line(rest, &segment_len, &break_advance);

        if( auto_wrap )
        {
            if( !font_wrap_segment_emit(
                    font,
                    rest,
                    segment_len,
                    logical_w,
                    lines,
                    line_lens,
                    &line_count,
                    FONT_DRAW_BOX_MAX_LINES) )
                break;
        }
        else
        {
            if( !font_append_draw_line(
                    lines, line_lens, &line_count, FONT_DRAW_BOX_MAX_LINES, rest, segment_len) )
                break;
        }

        if( break_advance == 0 )
            break;

        rest = break_at + break_advance;
    }

    return line_count;
}

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
    toripixel_t* pixel_buffer)
{
    assert(font && view_port && text && pixel_buffer);

    if( !ToriDraw_FontValidate(font) )
    {
        assert(!"ToriDraw2D_DrawStringBox: invalid font");
        return 0;
    }

    char const* lines[FONT_DRAW_BOX_MAX_LINES];
    int line_lens[FONT_DRAW_BOX_MAX_LINES];
    int const line_count =
        font_collect_draw_lines(font, text, w, h, line_height, lines, line_lens);

    if( line_count <= 0 )
        return 0;

    int const resolved_lh =
        line_height > 0 ? line_height : (font->line_height > 0 ? font->line_height : 1);
    int const logical_w = w > 0 ? w : 1;
    int const font_ascent = font->line_height > 0 ? font->line_height : resolved_lh;

    int max_ascent = resolved_lh;
    int max_descent = 0;
    font_get_vertical_metrics(font, &max_ascent, &max_descent);

    int const block_h =
        line_count > 0 ? resolved_lh * (line_count - 1) + max_ascent + max_descent : max_ascent + max_descent;
    int const logical_h = h > 0 ? h : block_h;

    /* AbstractFont.drawLines vertical alignment (matches reference TextRenderer). */
    int base_y0 = max_ascent;
    if( y_align == 1 )
    {
        int const space =
            logical_h - max_ascent - max_descent - resolved_lh * (line_count - 1);
        base_y0 = max_ascent + space / 2;
    }
    else if( y_align == 2 )
        base_y0 = logical_h - max_descent - resolved_lh * (line_count - 1);

    int const cl = view_port->clip_left;
    int const ct = view_port->clip_top;
    int const cr = view_port->clip_right;
    int const cb = view_port->clip_bottom;
    int const stride = view_port->stride;

    int pixels_written = 0;
    for( int i = 0; i < line_count; i++ )
    {
        int line_x = x;
        if( line_lens[i] > 0 )
        {
            int const tw = font_measure_range(font, lines[i], line_lens[i]);
            if( x_align == 1 )
                line_x = x + (logical_w - tw) / 2;
            else if( x_align == 2 )
                line_x = x + logical_w - tw;
        }

        int const draw_y = y + base_y0 + i * resolved_lh - font_ascent;
        if( line_lens[i] <= 0 )
            continue;

        if( shadowed )
        {
            pixels_written += font_draw_string_shadow_range(
                font, lines[i], line_lens[i], line_x, draw_y, cl, ct, cr, cb, stride, pixel_buffer);
        }
        pixels_written += font_draw_string_range(
            font,
            lines[i],
            line_lens[i],
            line_x,
            draw_y,
            color,
            cl,
            ct,
            cr,
            cb,
            stride,
            pixel_buffer);
    }

    return pixels_written;
}
