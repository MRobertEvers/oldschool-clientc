#ifndef PROJECTION_SIMD_SCALAR_U_C
#define PROJECTION_SIMD_SCALAR_U_C


static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    // Checked on 09/15/2025, this does get vectorized on Mac with clang, using arm neon. It also
    // gets vectorized on Windows and Linux with GCC. Tested on 09/18/2025, that the hand-rolled
    // Simd above is faster than the compilers
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish15;
        y *= cot_fov_half_ish15;
        x >>= 15;
        y >>= 15;

        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }

    for( int i = 0; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        // screen_vertices_z[i] = projected_vertex.z - mid_z;
        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            // TODO: The actual renderer from the deob marks that a face was clipped.
            // so it doesn't have to worry about a value actually being -5000.
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

static inline void
project_vertices_array_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish15;
        y *= cot_fov_half_ish15;
        x >>= 15;
        y >>= 15;

        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);

        // store raw z temporarily for the second-pass z-division
        screen_vertices_z[i] = z;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }

    for( int i = 0; i < num_vertices; i++ )
    {
        int z = screen_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }
}

static inline void
project_vertices_array_fused(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish15;
        y *= cot_fov_half_ish15;
        x >>= 15;
        y >>= 15;

        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_z[i] = z - model_mid_z;
        if( z < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z;
        }
    }
}

static inline void
project_vertices_array_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex projected_vertex;
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish15;
        y *= cot_fov_half_ish15;
        x >>= 15;
        y >>= 15;

        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);

        screen_vertices_z[i] = z - model_mid_z;
        if( z < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z;
        }
    }
}

#endif /* PROJECTION_SIMD_SCALAR_U_C */
