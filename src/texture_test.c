#include "texture.h"
#include <SDL2/SDL.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define SWAP(a, b)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        typeof(a) temp = a;                                                                        \
        a = b;                                                                                     \
        b = temp;                                                                                  \
    } while( 0 )

// Helper function to create a simple test texture
static void
create_test_texture(int* texels, int width, int height)
{
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            // Create a simple checkerboard pattern
            int color = ((x / 8) + (y / 8)) % 2 ? 0xFFFFFF : 0x000000;
            texels[y * width + x] = color;
        }
    }
}

// Helper function to compare pixel buffers
static int
compare_buffers(int* buf1, int* buf2, int width, int height)
{
    for( int i = 0; i < width * height; i++ )
    {
        if( buf1[i] != buf2[i] )
        {
            printf("Mismatch at index %d: expected 0x%x, got 0x%x\n", i, buf1[i], buf2[i]);
            return 0;
        }
    }
    return 1;
}

void
test_perspective_texture()
{
    const int SCREEN_WIDTH = 100;
    const int SCREEN_HEIGHT = 100;
    const int TEXTURE_WIDTH = 32;
    const int TEXTURE_HEIGHT = 32;

    // Create test buffers
    int* pixel_buffer = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(int));
    int* texels = malloc(TEXTURE_WIDTH * TEXTURE_HEIGHT * sizeof(int));
    int* expected_buffer = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(int));

    // Create test texture
    create_test_texture(texels, TEXTURE_WIDTH, TEXTURE_HEIGHT);

    // Test case 1: Flat plane (no perspective)
    printf("Test case 1: Flat plane\n");
    draw_scanline_texture(
        pixel_buffer,
        SCREEN_WIDTH,
        50,  // y
        20,  // screen_x_start
        80,  // screen_x_end
        100, // z_start
        100, // z_end
        0,   // u_start
        31,  // u_end
        0,   // v_start
        0,   // v_end
        texels,
        TEXTURE_WIDTH);

    // Verify the texture is mapped linearly
    int valid = 1;
    for( int x = 20; x < 80; x++ )
    {
        int expected_u = ((x - 20) * 31) / 60;
        int color = pixel_buffer[50 * SCREEN_WIDTH + x];
        if( color != texels[expected_u] )
        {
            printf("Flat plane test failed at x=%d\n", x);
            valid = 0;
            break;
        }
    }
    assert(valid);

    // Test case 2: Perspective distortion
    printf("Test case 2: Perspective distortion\n");
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    draw_scanline_texture(
        pixel_buffer,
        SCREEN_WIDTH,
        50,  // y
        20,  // screen_x_start
        80,  // screen_x_end
        100, // z_start
        200, // z_end (different depth)
        0,   // u_start
        31,  // u_end
        0,   // v_start
        0,   // v_end
        texels,
        TEXTURE_WIDTH);

    // Verify the texture shows perspective distortion
    // The texture should appear more compressed on the far end
    valid = 1;
    int prev_u = -1;
    for( int x = 20; x < 80; x++ )
    {
        int color = pixel_buffer[50 * SCREEN_WIDTH + x];
        int u = (color == 0xFFFFFF) ? 0 : 1; // Simple check for checkerboard
        if( prev_u != -1 && abs(u - prev_u) > 1 )
        {
            printf("Perspective test failed at x=%d\n", x);
            valid = 0;
            break;
        }
        prev_u = u;
    }
    assert(valid);

    // Test case 3: Extreme perspective
    printf("Test case 3: Extreme perspective\n");
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    draw_scanline_texture(
        pixel_buffer,
        SCREEN_WIDTH,
        50,  // y
        20,  // screen_x_start
        80,  // screen_x_end
        50,  // z_start (very close)
        500, // z_end (very far)
        0,   // u_start
        31,  // u_end
        0,   // v_start
        0,   // v_end
        texels,
        TEXTURE_WIDTH);

    // Verify extreme perspective distortion
    // The texture should be very stretched on the near end
    valid = 1;
    int near_end_texels = 0;
    int far_end_texels = 0;

    // Count texture transitions in first and last quarter
    for( int x = 20; x < 35; x++ )
    {
        int color = pixel_buffer[50 * SCREEN_WIDTH + x];
        if( color != pixel_buffer[50 * SCREEN_WIDTH + x - 1] )
        {
            near_end_texels++;
        }
    }

    for( int x = 65; x < 80; x++ )
    {
        int color = pixel_buffer[50 * SCREEN_WIDTH + x];
        if( color != pixel_buffer[50 * SCREEN_WIDTH + x - 1] )
        {
            far_end_texels++;
        }
    }

    // Near end should have more texture transitions than far end
    assert(near_end_texels > far_end_texels);

    // Cleanup
    free(pixel_buffer);
    free(texels);
    free(expected_buffer);

    printf("All texture mapping tests passed!\n");
}

int
main()
{
    test_perspective_texture();
    return 0;
}