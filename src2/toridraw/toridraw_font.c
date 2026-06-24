#include "toridraw_font.h"

#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const uint16_t TORIDRAW_FONT_CHARSET[] = {
    'A', 'B',  'C', 'D', 'E',  'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O',  'P', 'Q', 'R',  'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b',  'c', 'd', 'e',  'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o',  'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1',  '2', '3', '4',  '5', '6', '7', '8', '9', '!', '"', 0x00A3,
    '$', '%',  '^', '&', '*',  '(', ')', '-', '_', '=', '+', '[', '{',
    ']', '}',  ';', ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/',
    '?', '\\', '|', ' '
};

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
        assert(c < TORIDRAW_FONT_GLYPH_COUNT);
        font->charcodeset[i] = (char)c;
    }
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

        font->offset_x[i] = 1;
        font->advance[i] = w + 2;

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

    if( font->advance[93] < 4 )
        font->advance[93] = font->advance[8];
    font->advance[94] = font->advance[8];
    for( int i = 0; i < 256; i++ )
        font->draw_width[i] = font->advance[(unsigned char)font->charcodeset[i]];

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

int
ToriDraw2D_MeasureString(
    struct ToriDraw_Font* font,
    char const* text)
{
    if( !font || !text )
        return 0;

    int width = 0;
    for( char const* p = text; *p; ++p )
        width += font->draw_width[(unsigned char)*p];
    return width;
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

void
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
        return;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;

    if( center )
        x -= ToriDraw2D_MeasureString(font, text) / 2;

    if( shadowed )
    {
        int sx = x;
        int sy = y;
        for( char const* p = text; *p; ++p )
        {
            int gi = (unsigned char)font->charcodeset[(unsigned char)*p];
            int gw = font->glyph_width[gi];
            int gh = font->glyph_height[gi];
            int off = sx + font->offset_x[gi] + (sy + font->offset_y[gi] + 1) * stride;
            font_draw_mask(gw, gh, font->glyph_alpha[gi], 0, 0, pixel_buffer, off, stride, 0);
            sx += font->advance[gi];
        }
    }

    for( char const* p = text; *p; ++p )
    {
        int gi = (unsigned char)font->charcodeset[(unsigned char)*p];
        int gw = font->glyph_width[gi];
        int gh = font->glyph_height[gi];
        int gx = x + font->offset_x[gi];
        int gy = y + font->offset_y[gi];

        for( int row = 0; row < gh; row++ )
        {
            int dst_y = gy + row;
            if( dst_y < ct || dst_y >= cb )
                continue;
            for( int col = 0; col < gw; col++ )
            {
                int dst_x = gx + col;
                if( dst_x < cl || dst_x >= cr )
                    continue;
                uint8_t a = font->glyph_alpha[gi][col + row * gw];
                if( a == 0 )
                    continue;
                pixel_buffer[dst_y * stride + dst_x] = color;
            }
        }
        x += font->advance[gi];
    }
}
