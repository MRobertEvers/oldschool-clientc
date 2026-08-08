#ifndef PROJECTION_ZDIV_SIMD_SCALAR_U_C
#define PROJECTION_ZDIV_SIMD_SCALAR_U_C

#include <stdbool.h>

static inline void
projection_zdiv_tex_scalar_range_clip(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i0,
    int i1_exclusive,
    int model_mid_z,
    int near_plane_z)
{
    for( int zi = i0; zi < i1_exclusive; zi++ )
    {
        int z = orthographic_vertices_z[zi];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[zi] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[zi] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        }
        else
        {
            screen_vertices_x[zi] = screen_vertices_x[zi] / z;
            if( screen_vertices_x[zi] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[zi] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[zi] = screen_vertices_y[zi] / z;
        }
    }
}

static inline void
projection_zdiv_tex_scalar_range_noclip(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i0,
    int i1_exclusive,
    int model_mid_z,
    int near_plane_z)
{
    for( int zi = i0; zi < i1_exclusive; zi++ )
    {
        int z = orthographic_vertices_z[zi];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[zi] = z - model_mid_z;

        screen_vertices_x[zi] = screen_vertices_x[zi] / z;
        screen_vertices_y[zi] = screen_vertices_y[zi] / z;
    }
}

static inline void
projection_zdiv_notex_scalar_range_clip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i0,
    int i1_exclusive,
    int model_mid_z,
    int near_plane_z)
{
    for( int zi = i0; zi < i1_exclusive; zi++ )
    {
        int z = screen_vertices_z[zi];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[zi] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[zi] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        }
        else
        {
            screen_vertices_x[zi] = screen_vertices_x[zi] / z;
            if( screen_vertices_x[zi] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                screen_vertices_x[zi] = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            screen_vertices_y[zi] = screen_vertices_y[zi] / z;
        }
    }
}

static inline void
projection_zdiv_notex_scalar_range_noclip(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int i0,
    int i1_exclusive,
    int model_mid_z,
    int near_plane_z)
{
    for( int zi = i0; zi < i1_exclusive; zi++ )
    {
        int z = screen_vertices_z[zi];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[zi] = z - model_mid_z;

        screen_vertices_x[zi] = screen_vertices_x[zi] / z;
        screen_vertices_y[zi] = screen_vertices_y[zi] / z;
    }
}

#endif /* PROJECTION_ZDIV_SIMD_SCALAR_U_C */
