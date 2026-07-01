#include "toridraw_font.h"

#include "osrs/colors.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_FONT_ADVANCE_ONLY_GLYPH TORIDRAW_FONT_GLYPH_COUNT

const uint16_t TORIDRAW_FONT_CHARSET[] = {
    'A', 'B',  'C', 'D', 'E',  'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O',  'P', 'Q', 'R',  'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b',  'c', 'd', 'e',  'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o',  'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1',  '2', '3', '4',  '5', '6', '7', '8', '9', '!', '"', 0x00A3,
    '$', '%',  '^', '&', '*',  '(', ')', '-', '_', '=', '+', '[', '{',
    ']', '}',  ';', ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/',
    '?', '\\', ' '
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
    if( !data || !index_data || data_size <= 0 || index_data_size <= 0 )
        return NULL;

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
        return NULL;

    font_init_charcodeset(font);

    struct RSCacheShared_RSBuffer databuf = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size) };
    struct RSCacheShared_RSBuffer indexbuf = {
        .data = (uint8_t*)(index_data), .size = (uint32_t)(index_data_size)
    };

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
    if( !font )
        return false;

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
    if( gi < 0 || gi >= TORIDRAW_FONT_GLYPH_COUNT )
        return false;

    int const gw = font->glyph_width[gi];
    int const gh = font->glyph_height[gi];
    if( gw <= 0 || gh <= 0 )
        return false;
    if( !font->glyph_alpha[gi] )
        return false;
    return true;
}

int
ToriDraw2D_MeasureString(
    struct ToriDraw_Font* font,
    char const* text)
{
    if( !font || !text )
        return 0;

    int width = 0;
    int const len = (int)strlen(text);
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
        {
            i += 4;
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
ToriDraw_FontVisitGlyphs(
    struct ToriDraw_Font* font,
    char const* text,
    int x,
    int y,
    int default_color_rgb,
    ToriDraw_FontGlyphCallback callback,
    void* ctx)
{
    if( !font || !text || !callback )
        return;

    int const len = (int)strlen(text);
    int color = default_color_rgb;
    int const space_adv = font_space_advance(font);

    for( int i = 0; i < len; i++ )
    {
        if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
        {
            int const tagged = ToriDraw_FontEvaluateColorTag(&text[i + 1]);
            if( tagged >= 0 )
                color = tagged;
            i += 4;
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
    if( !font || !view_port || !text || !pixel_buffer )
        return 0;

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

    if( center )
        x -= ToriDraw2D_MeasureString(font, text) / 2;

    y -= font->line_height;

    int const shadow_color = (int)0xFF000000u;
    int const space_adv = font_space_advance(font);
    int len = (int)strlen(text);

    if( shadowed )
    {
        int sx = x;
        int sy = y;
        for( int i = 0; i < len; i++ )
        {
            if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
            {
                i += 4;
                continue;
            }
            if( font_is_rs_space_char((unsigned char)text[i]) )
            {
                sx += space_adv;
                continue;
            }
            int const gi = font_glyph_index(font, (unsigned char)text[i]);
            pixels_written += font_draw_glyph_pixels(
                font,
                gi,
                sx + font->offset_x[gi] + 1,
                sy + font->offset_y[gi] + 1,
                shadow_color,
                cl,
                ct,
                cr,
                cb,
                stride,
                pixel_buffer);
            sx += font_glyph_advance(font, gi);
        }
    }

    int current_color = color;
    for( int i = 0; i < len; i++ )
    {
        if( text[i] == '@' && i + 4 < len && text[i + 4] == '@' )
        {
            int const tagged = ToriDraw_FontEvaluateColorTag(&text[i + 1]);
            if( tagged >= 0 )
                current_color = tagged;
            i += 4;
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
