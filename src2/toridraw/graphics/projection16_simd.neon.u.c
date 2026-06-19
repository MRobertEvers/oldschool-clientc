#ifndef PROJECTION16_SIMD_NEON_U_C
#define PROJECTION16_SIMD_NEON_U_C

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>
#include "projection_zdiv_simd.neon.u.c"

static inline void
project_vertices_array_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // load 4 verts
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        int32x4_t c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t s_my = vdupq_n_s32(sin_model_yaw);

        // x' = (x*c + z*s)>>16
        int32x4_t x_tmp = vaddq_s32(vmulq_s32(xv4, c_my), vmulq_s32(zv4, s_my));
        x_rotated = vshrq_n_s32(x_tmp, 16);

        // z' = (z*c - x*s)>>16
        int32x4_t z_tmp = vsubq_s32(vmulq_s32(zv4, c_my), vmulq_s32(xv4, s_my));
        z_rotated = vshrq_n_s32(z_tmp, 16);

        // translate
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        // yaw
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        // x_scene = (x'*c + z'*s)>>16
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        // z_scene = (z'*c - x'*s)>>16
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        // pitch
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        // y_scene = (y'*c - z_scene*s)>>16
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        // z_final = (y'*s + z_scene*c)>>16
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        // store orthographic
        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        // perspective scale (keeps your SSE path semantics: >>6 after mul with ish15)
        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    // scalar tail (matches your original)
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 6;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        // load 4 verts
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        // translate
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        // yaw
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        // x_scene = (x'*c + z'*s)>>16
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        // z_scene = (z'*c - x'*s)>>16
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        // pitch
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        // y_scene = (y'*c - z_scene*s)>>16
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        // z_final = (y'*s + z_scene*c)>>16
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        // store orthographic
        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        // perspective scale (keeps your SSE path semantics: >>6 after mul with ish15)
        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    // scalar tail (matches your original)
    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 6;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
    if( model_yaw != 0 )
    {
        project_vertices_array_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_noyaw_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    int i = 0;

    // Arm Neon Float Div is faster.
#define ARM_NEON_FLOAT_RECIP_DIV
#ifdef ARM_NEON_FLOAT_RECIP_DIV
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        projection_neon_zdiv_tex_4_at(
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            i,
            model_mid_z,
            near_plane_z);
    }
    if( i < num_vertices )
    {
        projection_neon_zdiv_tex_tail(
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            i,
            num_vertices - i,
            model_mid_z,
            near_plane_z);
    }
#else
    for( ; i < num_vertices; i++ )
    {
        int z = orthographic_vertices_z[i];
        bool clipped = (z < near_plane_z);

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] /= z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] /= z;
        }
    }
#endif // ARM_NEON_FLOAT_DIV
}

static inline void
project_vertices_array_neon_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        int32x4_t c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t s_my = vdupq_n_s32(sin_model_yaw);

        int32x4_t x_tmp = vaddq_s32(vmulq_s32(xv4, c_my), vmulq_s32(zv4, s_my));
        x_rotated = vshrq_n_s32(x_tmp, 16);

        int32x4_t z_tmp = vsubq_s32(vmulq_s32(zv4, c_my), vmulq_s32(xv4, s_my));
        z_rotated = vshrq_n_s32(z_tmp, 16);

        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        // store z_final to screen_vertices_z as temporary (wrapper second pass reads it)
        vst1q_s32(&screen_vertices_z[i], z_final);

        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        screen_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 6;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_noyaw_neon_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;

    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;

    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));

        int32x4_t x_rotated = xv4;
        int32x4_t z_rotated = zv4;

        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));

        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);

        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);

        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);

        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);

        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);

        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        vst1q_s32(&screen_vertices_z[i], z_final);

        int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);
        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);

        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        vst1q_s32(&screen_vertices_x[i], x_scaled);
        vst1q_s32(&screen_vertices_y[i], y_scaled);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];

        int x_rotated = x;
        int z_rotated = z;

        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;

        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        screen_vertices_z[i] = z_final_scene;

        int screen_x = (x_scene * cot_fov_half_ish15) >> 6;
        int screen_y = (y_scene * cot_fov_half_ish15) >> 6;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
    }
}

static inline void
project_vertices_array_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
    if( model_yaw != 0 )
    {
        project_vertices_array_neon_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_noyaw_neon_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }

    // Second pass: z-division. screen_vertices_z currently holds raw z_final from the inner kernel.
    int i = 0;
#ifdef ARM_NEON_FLOAT_RECIP_DIV
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        projection_neon_zdiv_notex_4_at(
            screen_vertices_x, screen_vertices_y, screen_vertices_z, i, model_mid_z, near_plane_z);
    }
    if( i < num_vertices )
    {
        projection_neon_zdiv_notex_tail(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            i,
            num_vertices - i,
            model_mid_z,
            near_plane_z);
    }
#else
    for( ; i < num_vertices; i++ )
    {
        int z = screen_vertices_z[i];
        bool clipped = (z < near_plane_z);

        screen_vertices_z[i] = z - model_mid_z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] /= z;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] /= z;
        }
    }
#endif
}

/* Fused: ortho + FOV scale + z-division in one pass (keeps x/y/z in registers in the SIMD loop). */
static inline void
project_vertices_array_fused_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];
    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));
        int32x4_t c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t x_tmp = vaddq_s32(vmulq_s32(xv4, c_my), vmulq_s32(zv4, s_my));
        int32x4_t x_rotated = vshrq_n_s32(x_tmp, 16);
        int32x4_t z_tmp = vsubq_s32(vmulq_s32(zv4, c_my), vmulq_s32(xv4, s_my));
        int32x4_t z_rotated = vshrq_n_s32(z_tmp, 16);
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);
        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        vst1q_s32(&screen_vertices_z[i], vscreen_z);
        vst1q_s32(&screen_vertices_x[i], final_x);
        vst1q_s32(&screen_vertices_y[i], final_y);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        int x_rotated = x;
        int z_rotated = z;
        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;
        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;
        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_noyaw_neon(
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_mid_z,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));
        int32x4_t x_rotated = vaddq_s32(xv4, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        int32x4_t z_rotated = vaddq_s32(zv4, vdupq_n_s32(scene_z));
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t x_scene = vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw));
        x_scene = vshrq_n_s32(x_scene, 16);
        int32x4_t z_scene = vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw));
        z_scene = vshrq_n_s32(z_scene, 16);
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t y_scene = vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch));
        y_scene = vshrq_n_s32(y_scene, 16);
        int32x4_t z_final = vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch));
        z_final = vshrq_n_s32(z_final, 16);

        vst1q_s32(&orthographic_vertices_x[i], x_scene);
        vst1q_s32(&orthographic_vertices_y[i], y_scene);
        vst1q_s32(&orthographic_vertices_z[i], z_final);

        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);
        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        vst1q_s32(&screen_vertices_z[i], vscreen_z);
        vst1q_s32(&screen_vertices_x[i], final_x);
        vst1q_s32(&screen_vertices_y[i], final_y);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        int x_rotated = x;
        int z_rotated = z;
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;
        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;
        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        orthographic_vertices_x[i] = x_scene;
        orthographic_vertices_y[i] = y_scene;
        orthographic_vertices_z[i] = z_final_scene;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
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
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
    if( model_yaw != 0 )
    {
        project_vertices_array_fused_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_fused_noyaw_neon(
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
}

static inline void
project_vertices_array_fused_neon_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];
    int sin_model_yaw = g_sin_table[model_yaw];
    int cos_model_yaw = g_cos_table[model_yaw];

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));
        int32x4_t c_my = vdupq_n_s32(cos_model_yaw);
        int32x4_t s_my = vdupq_n_s32(sin_model_yaw);
        int32x4_t x_rotated = vshrq_n_s32(
            vaddq_s32(vmulq_s32(xv4, c_my), vmulq_s32(zv4, s_my)), 16);
        int32x4_t z_rotated = vshrq_n_s32(
            vsubq_s32(vmulq_s32(zv4, c_my), vmulq_s32(xv4, s_my)), 16);
        x_rotated = vaddq_s32(x_rotated, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        z_rotated = vaddq_s32(z_rotated, vdupq_n_s32(scene_z));
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t x_scene = vshrq_n_s32(
            vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw)), 16);
        int32x4_t z_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw)), 16);
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t y_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch)), 16);
        int32x4_t z_final = vshrq_n_s32(
            vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch)), 16);

        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);
        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        vst1q_s32(&screen_vertices_z[i], vscreen_z);
        vst1q_s32(&screen_vertices_x[i], final_x);
        vst1q_s32(&screen_vertices_y[i], final_y);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        int x_rotated = x;
        int z_rotated = z;
        x_rotated = (x * cos_model_yaw + z * sin_model_yaw) >> 16;
        z_rotated = (z * cos_model_yaw - x * sin_model_yaw) >> 16;
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;
        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;
        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_noyaw_neon_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
    int num_vertices,
    int model_mid_z,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_fov,
    int camera_pitch,
    int camera_yaw)
{
    int fov_half = camera_fov >> 1;
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];
    int cot_fov_half_ish15 = cot_fov_half_ish16 >> 1;
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];

    int32x4_t v_near = vdupq_n_s32(near_plane_z);
    int32x4_t v_mid = vdupq_n_s32(model_mid_z);
    int32x4_t v_neg5000 = vdupq_n_s32(-5000);
    int32x4_t v_neg5001 = vdupq_n_s32(-5001);
    int32x4_t cot_v = vdupq_n_s32(cot_fov_half_ish15);

    int i = 0;
    for( ; i + 4 <= num_vertices; i += 4 )
    {
        int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));
        int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));
        int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));
        int32x4_t x_rotated = vaddq_s32(xv4, vdupq_n_s32(scene_x));
        int32x4_t y_rotated = vaddq_s32(yv4, vdupq_n_s32(scene_y));
        int32x4_t z_rotated = vaddq_s32(zv4, vdupq_n_s32(scene_z));
        int32x4_t c_yaw = vdupq_n_s32(cos_camera_yaw);
        int32x4_t s_yaw = vdupq_n_s32(sin_camera_yaw);
        int32x4_t x_scene = vshrq_n_s32(
            vaddq_s32(vmulq_s32(x_rotated, c_yaw), vmulq_s32(z_rotated, s_yaw)), 16);
        int32x4_t z_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(z_rotated, c_yaw), vmulq_s32(x_rotated, s_yaw)), 16);
        int32x4_t c_pitch = vdupq_n_s32(cos_camera_pitch);
        int32x4_t s_pitch = vdupq_n_s32(sin_camera_pitch);
        int32x4_t y_scene = vshrq_n_s32(
            vsubq_s32(vmulq_s32(y_rotated, c_pitch), vmulq_s32(z_scene, s_pitch)), 16);
        int32x4_t z_final = vshrq_n_s32(
            vaddq_s32(vmulq_s32(y_rotated, s_pitch), vmulq_s32(z_scene, c_pitch)), 16);

        int32x4_t xfov = vmulq_s32(x_scene, cot_v);
        int32x4_t yfov = vmulq_s32(y_scene, cot_v);
        int32x4_t x_scaled = vshrq_n_s32(xfov, 6);
        int32x4_t y_scaled = vshrq_n_s32(yfov, 6);

        int32x4_t vscreen_z;
        int32x4_t final_x;
        int32x4_t final_y;
        projection_zdiv_neon_apply(
            z_final,
            x_scaled,
            y_scaled,
            v_near,
            v_mid,
            v_neg5000,
            v_neg5001,
            &vscreen_z,
            &final_x,
            &final_y);
        vst1q_s32(&screen_vertices_z[i], vscreen_z);
        vst1q_s32(&screen_vertices_x[i], final_x);
        vst1q_s32(&screen_vertices_y[i], final_y);
    }

    for( ; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        int x_rotated = x;
        int z_rotated = z;
        x_rotated += scene_x;
        int y_rotated = y + scene_y;
        z_rotated += scene_z;
        int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
        int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;
        int y_scene = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        int z_final_scene = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

        int screen_x = x_scene * cot_fov_half_ish15;
        int screen_y = y_scene * cot_fov_half_ish15;
        screen_x >>= 6;
        screen_y >>= 6;

        screen_vertices_z[i] = z_final_scene - model_mid_z;
        if( z_final_scene < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = screen_y;
        }
        else
        {
            screen_vertices_x[i] = screen_x / z_final_scene;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_y / z_final_scene;
        }
    }
}

static inline void
project_vertices_array_fused_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z,
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
    if( model_yaw != 0 )
    {
        project_vertices_array_fused_neon_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_yaw,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
    else
    {
        project_vertices_array_fused_noyaw_neon_notex(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            vertex_x,
            vertex_y,
            vertex_z,
            num_vertices,
            model_mid_z,
            near_plane_z,
            scene_x,
            scene_y,
            scene_z,
            camera_fov,
            camera_pitch,
            camera_yaw);
    }
}

#endif

#endif /* PROJECTION16_SIMD_NEON_U_C */
