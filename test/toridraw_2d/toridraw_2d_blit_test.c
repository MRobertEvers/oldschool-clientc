#include "toridraw_2d.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    BUFFER_WIDTH = 53,
    BUFFER_HEIGHT = 41,
    BUFFER_STRIDE = 59,
    SOURCE_WIDTH_MAX = 17,
    SOURCE_HEIGHT_MAX = 15,
    ITERATIONS = 4000
};

static uint32_t random_state = 0xC001D00Du;

static uint32_t
next_random(void)
{
    uint32_t x = random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_state = x;
    return x;
}

static int
random_below(int limit)
{
    return (int)(next_random() % (uint32_t)limit);
}

static uint32_t
random_argb(void)
{
    uint32_t pixel = next_random();
    switch( next_random() & 7u )
    {
        case 0:
            pixel &= 0x00FFFFFFu;
            break;
        case 1:
            pixel |= 0xFF000000u;
            break;
        default:
            break;
    }
    return pixel;
}

static uint32_t
apply_alpha(uint32_t pixel, int alpha)
{
    int source_alpha;
    if( alpha <= 0 )
        return pixel & 0x00FFFFFFu;
    if( alpha > 255 )
        alpha = 255;
    source_alpha = (int)(pixel >> 24) & 0xFF;
    source_alpha = (source_alpha * alpha) / 255;
    return (pixel & 0x00FFFFFFu) | ((uint32_t)source_alpha << 24);
}

static void
reference_blit(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    uint32_t const* source,
    int source_width,
    int source_height,
    int alpha,
    int* pixels)
{
    for( int y = 0; y < source_height; y++ )
    {
        for( int x = 0; x < source_width; x++ )
        {
            uint32_t pixel = apply_alpha(source[(size_t)y * source_width + x], alpha);
            ToriDraw2D_BlendArgbPixel(view_port, dst_x + x, dst_y + y, (int)pixel, pixels);
        }
    }
}

static void
reference_blit_scaled(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    uint32_t const* source,
    int source_width,
    int source_height,
    int alpha,
    int* pixels)
{
    for( int y = 0; y < dst_height; y++ )
    {
        int source_y = (int)(((int64_t)y * source_height) / dst_height);
        for( int x = 0; x < dst_width; x++ )
        {
            int source_x = (int)(((int64_t)x * source_width) / dst_width);
            uint32_t pixel =
                apply_alpha(source[(size_t)source_y * source_width + source_x], alpha);
            ToriDraw2D_BlendArgbPixel(view_port, dst_x + x, dst_y + y, (int)pixel, pixels);
        }
    }
}

static int
positive_modulo(int value, int modulus)
{
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

static void
reference_blit_tiled(
    struct ToriDraw_ViewPort* view_port,
    int rect_x,
    int rect_y,
    int rect_width,
    int rect_height,
    uint32_t const* source,
    int source_width,
    int source_height,
    int origin_x,
    int origin_y,
    int alpha,
    int* pixels)
{
    for( int y = 0; y < rect_height; y++ )
    {
        int dst_y = rect_y + y;
        int source_y = positive_modulo(dst_y - origin_y, source_height);
        for( int x = 0; x < rect_width; x++ )
        {
            int dst_x = rect_x + x;
            int source_x = positive_modulo(dst_x - origin_x, source_width);
            uint32_t pixel =
                apply_alpha(source[(size_t)source_y * source_width + source_x], alpha);
            ToriDraw2D_BlendArgbPixel(view_port, dst_x, dst_y, (int)pixel, pixels);
        }
    }
}

static void
initialize_pixels(int* pixels)
{
    for( size_t i = 0; i < (size_t)BUFFER_STRIDE * BUFFER_HEIGHT; i++ )
        pixels[i] = (int)(next_random() | 0xFF000000u);
}

static int
compare_pixels(
    char const* operation,
    int iteration,
    int alpha,
    int const* expected,
    int const* actual)
{
    for( size_t i = 0; i < (size_t)BUFFER_STRIDE * BUFFER_HEIGHT; i++ )
    {
        if( expected[i] != actual[i] )
        {
            fprintf(
                stderr,
                "%s mismatch at iteration %d, alpha %d, pixel %zu: %08x != %08x\n",
                operation,
                iteration,
                alpha,
                i,
                (unsigned int)expected[i],
                (unsigned int)actual[i]);
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    static int expected[BUFFER_STRIDE * BUFFER_HEIGHT];
    static int actual[BUFFER_STRIDE * BUFFER_HEIGHT];
    static uint32_t source[SOURCE_WIDTH_MAX * SOURCE_HEIGHT_MAX];
    static int const alpha_cases[] = { -9, 0, 1, 2, 63, 127, 128, 254, 255, 300 };

    for( int iteration = 0; iteration < ITERATIONS; iteration++ )
    {
        struct ToriDraw_ViewPort view_port = { 0 };
        int source_width = 1 + random_below(SOURCE_WIDTH_MAX);
        int source_height = 1 + random_below(SOURCE_HEIGHT_MAX);
        int dst_x = random_below(BUFFER_WIDTH + 24) - 12;
        int dst_y = random_below(BUFFER_HEIGHT + 24) - 12;
        int dst_width = 1 + random_below(BUFFER_WIDTH + 12);
        int dst_height = 1 + random_below(BUFFER_HEIGHT + 12);
        int origin_x = random_below(BUFFER_WIDTH + 30) - 15;
        int origin_y = random_below(BUFFER_HEIGHT + 30) - 15;
        int alpha = alpha_cases[random_below((int)(sizeof(alpha_cases) / sizeof(alpha_cases[0])))];

        view_port.stride = BUFFER_STRIDE;
        view_port.clip_left = random_below(BUFFER_WIDTH + 1);
        view_port.clip_right = view_port.clip_left + random_below(BUFFER_WIDTH - view_port.clip_left + 1);
        view_port.clip_top = random_below(BUFFER_HEIGHT + 1);
        view_port.clip_bottom =
            view_port.clip_top + random_below(BUFFER_HEIGHT - view_port.clip_top + 1);

        for( int i = 0; i < source_width * source_height; i++ )
            source[i] = random_argb();

        initialize_pixels(expected);
        memcpy(actual, expected, sizeof(actual));
        reference_blit(
            &view_port,
            dst_x,
            dst_y,
            source,
            source_width,
            source_height,
            alpha,
            expected);
        ToriDraw2D_BlitArgbAlpha(
            &view_port,
            dst_x,
            dst_y,
            source,
            source_width,
            source_height,
            alpha,
            actual);
        if( compare_pixels("plain-alpha", iteration, alpha, expected, actual) )
            return 1;

        initialize_pixels(expected);
        memcpy(actual, expected, sizeof(actual));
        reference_blit_scaled(
            &view_port,
            dst_x,
            dst_y,
            dst_width,
            dst_height,
            source,
            source_width,
            source_height,
            alpha,
            expected);
        ToriDraw2D_BlitArgbScaledAlpha(
            &view_port,
            dst_x,
            dst_y,
            dst_width,
            dst_height,
            source,
            source_width,
            source_height,
            alpha,
            actual);
        if( compare_pixels("scaled-alpha", iteration, alpha, expected, actual) )
            return 1;

        initialize_pixels(expected);
        memcpy(actual, expected, sizeof(actual));
        reference_blit_tiled(
            &view_port,
            dst_x,
            dst_y,
            dst_width,
            dst_height,
            source,
            source_width,
            source_height,
            origin_x,
            origin_y,
            alpha,
            expected);
        ToriDraw2D_BlitArgbTiledAlpha(
            &view_port,
            dst_x,
            dst_y,
            dst_width,
            dst_height,
            source,
            source_width,
            source_height,
            origin_x,
            origin_y,
            alpha,
            actual);
        if( compare_pixels("tiled-alpha", iteration, alpha, expected, actual) )
            return 1;

        if( alpha == 255 )
        {
            initialize_pixels(expected);
            memcpy(actual, expected, sizeof(actual));
            reference_blit(
                &view_port,
                dst_x,
                dst_y,
                source,
                source_width,
                source_height,
                255,
                expected);
            ToriDraw2D_BlitArgb(
                &view_port,
                dst_x,
                dst_y,
                source,
                source_width,
                source_height,
                actual);
            if( compare_pixels("plain", iteration, alpha, expected, actual) )
                return 1;

            initialize_pixels(expected);
            memcpy(actual, expected, sizeof(actual));
            reference_blit_scaled(
                &view_port,
                dst_x,
                dst_y,
                dst_width,
                dst_height,
                source,
                source_width,
                source_height,
                255,
                expected);
            ToriDraw2D_BlitArgbScaled(
                &view_port,
                dst_x,
                dst_y,
                dst_width,
                dst_height,
                source,
                source_width,
                source_height,
                actual);
            if( compare_pixels("scaled", iteration, alpha, expected, actual) )
                return 1;

            initialize_pixels(expected);
            memcpy(actual, expected, sizeof(actual));
            reference_blit_tiled(
                &view_port,
                dst_x,
                dst_y,
                dst_width,
                dst_height,
                source,
                source_width,
                source_height,
                origin_x,
                origin_y,
                255,
                expected);
            ToriDraw2D_BlitArgbTiled(
                &view_port,
                dst_x,
                dst_y,
                dst_width,
                dst_height,
                source,
                source_width,
                source_height,
                origin_x,
                origin_y,
                actual);
            if( compare_pixels("tiled", iteration, alpha, expected, actual) )
                return 1;
        }
    }

    printf("toridraw_2d_blit_test: PASS (%d randomized cases)\n", ITERATIONS);
    return 0;
}
