#include "../../src/graphics/projection.u.c"
#include "../../src/graphics/projection_simd.u.c"
#include "../../src/shared_tables.c"

#include <stdio.h>

static int vertices_x[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int vertices_y[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int vertices_z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static int orthographic_vertices_x[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int orthographic_vertices_y[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int orthographic_vertices_z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_x[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_y[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static int orthographic_vertices_x_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int orthographic_vertices_y_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int orthographic_vertices_z_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_x_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_y_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int screen_vertices_z_scalar[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

int
main(void)
{
    init_sin_table();
    init_cos_table();
    init_tan_table();

    // Populate the vertices with a simple cube centered at origin, size 512 (UNIT_SCALE)
    // Vertices in right-handed system: +X right, +Y up, +Z into screen
    vertices_x[0] = -256;
    vertices_y[0] = -256;
    vertices_z[0] = -256;
    vertices_x[1] = 256;
    vertices_y[1] = -256;
    vertices_z[1] = -256;
    vertices_x[2] = 256;
    vertices_y[2] = 256;
    vertices_z[2] = -256;
    vertices_x[3] = -256;
    vertices_y[3] = 256;
    vertices_z[3] = -256;
    vertices_x[4] = -256;
    vertices_y[4] = -256;
    vertices_z[4] = 256;
    vertices_x[5] = 256;
    vertices_y[5] = -256;
    vertices_z[5] = 256;
    vertices_x[6] = 256;
    vertices_y[6] = 256;
    vertices_z[6] = 256;
    vertices_x[7] = -256;
    vertices_y[7] = 256;
    vertices_z[7] = 256;

    project_vertices_array_simd(
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertices_x,
        vertices_y,
        vertices_z,
        8,
        0,
        20,
        20,
        50,
        512,
        0,
        0);

    for( int i = 0; i < 8; i++ )
    {
        struct ProjectedVertex projected_vertex;

        project_orthographic_fast(
            &projected_vertex,
            vertices_x[i],
            vertices_y[i],
            vertices_z[i], //
            0,
            20,
            20,
            50,
            0,
            0);

        int fov_half = 512 >> 1; // fov/2

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        orthographic_vertices_x_scalar[i] = projected_vertex.x;
        orthographic_vertices_y_scalar[i] = projected_vertex.y;
        orthographic_vertices_z_scalar[i] = projected_vertex.z;

        // fov_scale = 1/tan(fov/2)
        // cot(x) = 1/tan(x)
        // cot(x) = tan(pi/2 - x)
        // cot(x + pi) = cot(x)
        // assert(fov_half < 1536);
        int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

        static const int scale_angle = 1;
        int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

        // Apply FOV scaling to x and y coordinates

        // unit scale 9, angle scale 16
        // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

        // if valid x is -Screen_width/2 to Screen_width/2
        // And the max resolution we allow is 1600 (either dimension)
        // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

        // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
        // i.e. x/z < 2^6 or x/z < 64

        // Suppose we allow z > 16, so z_clip_bits = 4
        // then x_bits_max < 10, so 2^9, which is 512

        x *= cot_fov_half_ish_scaled;
        y *= cot_fov_half_ish_scaled;
        x >>= 16 - scale_angle;
        y >>= 16 - scale_angle;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        int screen_x = (x << 9);
        int screen_y = (y << 9);

        screen_vertices_x_scalar[i] = screen_x;
        screen_vertices_y_scalar[i] = screen_y;
        screen_vertices_z_scalar[i] = projected_vertex.z;
    }
    for( int i = 0; i < 8; i++ )
    {
        printf(
            "orthographic_vertices_x[%d] = %d, %d\n",
            i,
            orthographic_vertices_x[i],
            orthographic_vertices_x_scalar[i]);
        printf(
            "orthographic_vertices_y[%d] = %d, %d\n",
            i,
            orthographic_vertices_y[i],
            orthographic_vertices_y_scalar[i]);
        printf(
            "orthographic_vertices_z[%d] = %d, %d\n",
            i,
            orthographic_vertices_z[i],
            orthographic_vertices_z_scalar[i]);
        printf(
            "screen_vertices_x[%d] = %d, %d\n",
            i,
            screen_vertices_x[i],
            screen_vertices_x_scalar[i]);
        printf(
            "screen_vertices_y[%d] = %d, %d\n",
            i,
            screen_vertices_y[i],
            screen_vertices_y_scalar[i]);
        printf(
            "screen_vertices_z[%d] = %d, %d\n",
            i,
            screen_vertices_z[i],
            screen_vertices_z_scalar[i]);
    }
}