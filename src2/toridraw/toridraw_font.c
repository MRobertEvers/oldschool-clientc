#include "toridraw_font.h"

#include "osrs/colors.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

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
    ';',    ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/', '?', '\\', ' '
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
        if( c == -1 )
            c = font_index_of_char(' ');
        if( c < 0 || c >= TORIDRAW_FONT_GLYPH_COUNT )
            c = TORIDRAW_FONT_GLYPH_COUNT - 1;
        font->charcodeset[i] = (char)c;
    }
    font->charcodeset[(unsigned char)' '] = (char)TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;
    font->charcodeset[(unsigned char)'|'] = (char)TORIDRAW_FONT_ADVANCE_ONLY_GLYPH;
}

static void
font_finish_draw_widths(struct ToriDraw_Font* font)
{
    int const fallback = font->advance[8] > 0 ? font->advance[8] : 4;

    if( font->advance[93] < 4 )
        font->advance[93] = fallback;
    if( font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH] <= 0 )
        font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH] = fallback;

    for( int i = 0; i < 256; i++ )
        font->draw_width[i] = font->advance[(unsigned char)font->charcodeset[i]];

    font->draw_width[(unsigned char)' '] = font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH];
    font->draw_width[(unsigned char)'|'] = font->advance[TORIDRAW_FONT_ADVANCE_ONLY_GLYPH];
}

void
ToriDraw_FontFinishDrawWidths(struct ToriDraw_Font* font)
{
    font_finish_draw_widths(font);
}

int
ToriDraw_FontEvaluateColorTag(char const tag[3])
{
    if( !tag )
        return -1;
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

static int
font_try_consume_markup(
    char const* text,
    int len,
    int i,
    int default_color,
    int* color_out,
    bool apply_color,
    unsigned char* emit_char_out)
{
    if( !text || i < 0 || i >= len )
        return 0;

    if( emit_char_out )
        *emit_char_out = 0;

    if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
    {
        if( apply_color && color_out )
        {
            int const tagged = ToriDraw_FontEvaluateColorTag(&text[i + 1]);
            if( tagged >= 0 )
                *color_out = tagged;
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
        if( apply_color && color_out )
            *color_out = default_color;
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
            if( apply_color && color_out )
            {
                int const parsed = font_parse_hex_rgb(&text[i + 5], hex_len);
                if( parsed >= 0 )
                    *color_out = parsed;
            }
            return j - i + 1;
        }
    }

    return 0;
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
    return ch == ' ' || ch == '|';
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

struct ToriDraw_Font*
ToriDraw_FontNewFromRSBytes(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size)
{
    assert(data && index_data && data_size > 0 && index_data_size > 0);

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    assert(font);

    font_init_charcodeset(font);

    struct RSCacheShared_RSBuffer databuf = { .data = (uint8_t*)(data),
                                              .size = (uint32_t)(data_size) };
    struct RSCacheShared_RSBuffer indexbuf = { .data = (uint8_t*)(index_data),
                                               .size = (uint32_t)(index_data_size) };

    indexbuf.position = g2(&databuf) + 4;
    int off = g1(&indexbuf);
    if( off > 0 )
        indexbuf.position += (off - 1) * 3;

    font->line_height = 0;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        font->offset_x[i] = g1(&indexbuf);
        font->offset_y[i] = g1(&indexbuf);

        int w = g2(&indexbuf);
        int h = g2(&indexbuf);
        font->glyph_width[i] = w;
        font->glyph_height[i] = h;
        if( h > font->line_height )
            font->line_height = h;

        int type = g1(&indexbuf);
        int len = w * h;

        if( len > 0 )
        {
            font->glyph_alpha[i] = malloc((size_t)len);
            if( !font->glyph_alpha[i] )
                goto fail;
            memset(font->glyph_alpha[i], 0, (size_t)len);
            if( type == 0 )
            {
                for( int j = 0; j < len; j++ )
                    font->glyph_alpha[i][j] = (uint8_t)g1b(&databuf);
            }
            else if( type == 1 )
            {
                for( int x = 0; x < w; x++ )
                {
                    for( int y = 0; y < h; y++ )
                        font->glyph_alpha[i][x + y * w] = (uint8_t)g1b(&databuf);
                }
            }
        }

        font->offset_x[i] = 1;
        font->advance[i] = w + 2;

        if( w > 0 && h > 0 && font->glyph_alpha[i] )
        {
            int space = 0;
            for( int y = (h / 7) | 0; y < h; y++ )
                space += font->glyph_alpha[i][y * w];

            if( space <= ((h / 7) | 0) )
            {
                font->advance[i]--;
                font->offset_x[i] = 0;
            }

            space = 0;
            for( int y = (h / 7) | 0; y < h; y++ )
                space += font->glyph_alpha[i][w + y * w - 1];

            if( space <= ((h / 7) | 0) )
                font->advance[i]--;
        }
    }

    if( font->line_height <= 0 )
    {
        for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
        {
            if( font->glyph_height[i] > font->line_height )
                font->line_height = font->glyph_height[i];
        }
    }

    if( font->advance[93] < 4 )
        font->advance[93] = font->advance[8];
    ToriDraw_FontFinishDrawWidths(font);

    if( !ToriDraw_FontValidate(font) )
        goto fail;
    return font;

fail:
    ToriDraw_FontFree(font);
    return NULL;
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
    assert(font && gi >= 0 && gi < TORIDRAW_FONT_GLYPH_COUNT);

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
    if( !p || p[0] == '\0' )
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
            font_try_consume_markup(text, len, i, 0, NULL, false, &emit_char);
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
    if( !font || !text || len <= 0 )
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
    if( !font || !text || len <= 0 )
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
    int* pixel_buffer)
{
    if( !font_glyph_drawable(font, gi) )
        return 0;

    int pixels_written = 0;
    int const gw = font->glyph_width[gi];
    int const gh = font->glyph_height[gi];

    for( int row = 0; row < gh; row++ )
    {
        int const dst_y = gy + row;
        if( dst_y < ct || dst_y >= cb )
            continue;
        for( int col = 0; col < gw; col++ )
        {
            int const dst_x = gx + col;
            if( dst_x < cl || dst_x >= cr )
                continue;
            uint8_t const a = font->glyph_alpha[gi][col + row * gw];
            if( a == 0 )
                continue;
            pixel_buffer[dst_y * stride + dst_x] = color;
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

    int color = default_color_rgb;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed = font_try_consume_markup(
            text, len, i, default_color_rgb, &color, true, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                if( font_glyph_drawable(font, gi) )
                {
                    int const gx = x + font->offset_x[gi];
                    int const gy = y + font->offset_y[gi];
                    callback(ctx, font, gi, gx, gy, color);
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
            callback(ctx, font, gi, gx, gy, color);
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
    int* dst,
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
    int* pixel_buffer)
{
    assert(font && text && len > 0 && pixel_buffer);

    int pixels_written = 0;
    int current_color = color;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, color, &current_color, true, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char )
            {
                int const gi = font_glyph_index(font, emit_char);
                int const opaque_color = (int)(0xFF000000u | (uint32_t)current_color);
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
        int const opaque_color = (int)(0xFF000000u | (uint32_t)current_color);
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
        x += font_glyph_advance(font, gi);
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
    int* pixel_buffer)
{
    assert(font && text && len > 0 && pixel_buffer);

    int pixels_written = 0;
    int const shadow_color = (int)0xFF000000u;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, false, &emit_char);
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
    int* pixel_buffer)
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
            font_try_consume_markup(text, len, i, 0, NULL, false, &emit_char);
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
    if( !text || len <= 0 )
        return false;

    for( int i = 0; i < len; i++ )
    {
        if( font_is_rs_space_char((unsigned char)text[i]) )
            continue;

        unsigned char emit_char = 0;
        int const consumed =
            font_try_consume_markup(text, len, i, 0, NULL, false, &emit_char);
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
    if( !text || text[0] == '\0' )
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
    int* pixel_buffer)
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
