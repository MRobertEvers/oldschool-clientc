#ifndef PROJECTION_ORTHO_U_C
#define PROJECTION_ORTHO_U_C

#include "projection_ortho.h"

#include "dash_restrict.h"
#include "dash_vertexint.h"
#include "projection.h"

#include <stdbool.h>

/*
 * Parallel (orthographic) projection, for the map editor.
 *
 * The perspective kernels spend their money on one thing: the per-vertex
 * divide. `screen = coord * scale / z` needs a real division per axis, and no
 * target here has an integer SIMD divide, so the vector paths detour through
 * float -- convert z to float, reciprocal-estimate, one Newton-Raphson step,
 * two multiplies, convert back with truncation. That round trip, not the
 * transform, is the expensive part of projecting a vertex.
 *
 * A parallel projection has no eye point, so there is nothing to divide by:
 *
 *     screen_x = (x_camera * zoom16) >> 16
 *     screen_y = (y_camera * zoom16) >> 16
 *
 * One multiply and one shift per axis, in integer lanes that vectorize as wide
 * as the machine allows. The transform, and the depth value the painter sorts
 * on, are unchanged -- so these drop into the existing pipeline as an
 * alternative kernel choice, leaving the perspective kernels untouched.
 *
 * ZOOM. `camera_zoom16` is 16.16 fixed point: 65536 draws one world unit as one
 * pixel, 131072 is 2x, 32768 is half. Continuous rather than stepped, because a
 * multiply costs the same at any value -- there is no reason to restrict a map
 * editor to power-of-two zoom.
 *
 * FOUR KERNELS, WRITTEN OUT. tex/notex and clip/noclip are separate functions
 * rather than one kernel taking flags, matching the perspective families: the
 * choice is a property of the model and the camera, made once by the caller, so
 * nothing below should be re-testing it per vertex. A `bool want_camera_space`
 * or `bool may_clip` parameter would sit in the innermost loop and, worse, stop
 * being a constant at lower optimization levels.
 *
 * WHAT CLIPPING MEANS HERE. With no divide there is no singularity -- a vertex
 * behind the camera projects to a perfectly ordinary coordinate, which is why
 * the *_noclip kernels are the natural default and are strictly cheaper. The
 * *_clip kernels exist for the case a map editor actually hits: a camera placed
 * inside or beneath geometry, where everything behind the view plane would
 * otherwise still be drawn. They mark such vertices with
 * TORIDRAW_SCREEN_X_NEAR_CLIPPED so the existing per-face test rejects the face.
 *
 * CRITICAL: mark-to-DROP, not mark-to-rebuild. The perspective near-clip
 * polygon builder reconstructs a clipped triangle through
 * ToriDraw_TriangleLerpPlaneProjecti, which ends in `SCALE_UNIT(p) /
 * near_plane_z` -- a perspective divide. That is wrong for a parallel
 * projection and would place the rebuilt vertices incorrectly. A caller using
 * the *_clip kernels must therefore leave allow_near_clip false so marked faces
 * are skipped outright. A parallel-correct rebuild would only need a lerp with
 * no divide at all, but that is a triangle-builder change, not a kernel one.
 *
 * SENTINEL Y. The perspective kernels leave a clipped vertex's screen y
 * *undivided*, because dividing by that z is the unsafe operation. Nothing is
 * unsafe here, so these store the correctly projected y and only screen x
 * carries the marker. Consumers reject the whole face on x, so they never read
 * the pair either way.
 *
 * ROUNDING. `>> 16` floors; the perspective path's `/ z` truncates toward zero.
 * Flooring is the better behaviour here -- truncation is discontinuous across
 * x == 0, which makes geometry twitch by a pixel as it crosses the origin under
 * a slow pan. Not bug-compatible with the perspective kernels, deliberately.
 *
 * DOMAIN. The multiply is 32x32->32, so |x_camera * zoom16| must fit in an
 * int32: at 1:1 that is |x_camera| < 32768 world units, and the bound tightens
 * as you zoom in. Anything the AABB cull admits is on screen and comfortably
 * inside it. This is the same assumption the perspective kernels already make
 * for `x * cot15`.
 */

/* TORIDRAW_ORTHO_ZOOM_SHIFT / _UNIT are in projection.h: callers set
 * ToriDraw_Camera.parallel_zoom16, so the unit is public API. */

/*
 * ONE LANE. The four kernels below hold the scalar work and the tail loop; the
 * vector body of each is in the lane file, behind the hooks projection_ortho.h
 * names. `#elif` and not four `#if`s: a build has exactly one of these, and
 * the stacked form this replaced -- three vector blocks inside each of four
 * functions -- read as though a build could have several.
 */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "projection_ortho.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "projection_ortho.avx.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "projection_ortho.sse2.u.c"
#else
#include "projection_ortho.none.u.c"
#endif

/** One vertex to camera space. Shared by every scalar tail and the no-SIMD build. */
static inline void
toridraw_ortho_to_camera_space(
    int vx,
    int vy,
    int vz,
    int cos_model_yaw,
    int sin_model_yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    int cos_camera_yaw,
    int sin_camera_yaw,
    int cos_camera_pitch,
    int sin_camera_pitch,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int x_rotated = (vx * cos_model_yaw + vz * sin_model_yaw) >> 16;
    int z_rotated = (vz * cos_model_yaw - vx * sin_model_yaw) >> 16;

    x_rotated += scene_x;
    int y_rotated = vy + scene_y;
    z_rotated += scene_z;

    int x_scene = (x_rotated * cos_camera_yaw + z_rotated * sin_camera_yaw) >> 16;
    int z_scene = (z_rotated * cos_camera_yaw - x_rotated * sin_camera_yaw) >> 16;

    *out_x = x_scene;
    *out_y = (y_rotated * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
    *out_z = (y_rotated * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;
}

/** With camera-space output; no near-plane test at all -- the cheapest kernel. */
static inline void
project_vertices_array_ortho_fused_noclip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
    int const sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
    int const cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
    int const sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
    int const cos_model_yaw = ToriDraw_ReadCosTable(model_yaw);
    int const sin_model_yaw = ToriDraw_ReadSinTable(model_yaw);

    struct ToriDraw_OrthoFusedCamera const cam = {
        .cos_model_yaw = cos_model_yaw,
        .sin_model_yaw = sin_model_yaw,
        .cos_camera_yaw = cos_camera_yaw,
        .sin_camera_yaw = sin_camera_yaw,
        .cos_camera_pitch = cos_camera_pitch,
        .sin_camera_pitch = sin_camera_pitch,
        .scene_x = scene_x,
        .scene_y = scene_y,
        .scene_z = scene_z,
        .camera_zoom16 = camera_zoom16,
        .model_mid_z = model_mid_z,
        /* The noclip lane never reads it. */
        .near_plane_z = 0,
    };

    /* Whatever the build's lane took off the front; 0 if it has no kernel. */
    int i = toridraw_ortho_lane_fused(
        &cam,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            cos_model_yaw,
            sin_model_yaw,
            scene_x,
            scene_y,
            scene_z,
            cos_camera_yaw,
            sin_camera_yaw,
            cos_camera_pitch,
            sin_camera_pitch,
            &x_cam,
            &y_cam,
            &z_fin);

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** With camera-space output; marks vertices behind near_plane_z for the face test to DROP (see
 * header). */
static inline void
project_vertices_array_ortho_fused_clip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
    int const sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
    int const cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
    int const sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
    int const cos_model_yaw = ToriDraw_ReadCosTable(model_yaw);
    int const sin_model_yaw = ToriDraw_ReadSinTable(model_yaw);

    struct ToriDraw_OrthoFusedCamera const cam = {
        .cos_model_yaw = cos_model_yaw,
        .sin_model_yaw = sin_model_yaw,
        .cos_camera_yaw = cos_camera_yaw,
        .sin_camera_yaw = sin_camera_yaw,
        .cos_camera_pitch = cos_camera_pitch,
        .sin_camera_pitch = sin_camera_pitch,
        .scene_x = scene_x,
        .scene_y = scene_y,
        .scene_z = scene_z,
        .camera_zoom16 = camera_zoom16,
        .model_mid_z = model_mid_z,
        .near_plane_z = near_plane_z,
    };

    /* Whatever the build's lane took off the front; 0 if it has no kernel. */
    int i = toridraw_ortho_lane_fused_clip(
        &cam,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            cos_model_yaw,
            sin_model_yaw,
            scene_x,
            scene_y,
            scene_z,
            cos_camera_yaw,
            sin_camera_yaw,
            cos_camera_pitch,
            sin_camera_pitch,
            &x_cam,
            &y_cam,
            &z_fin);

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** No camera-space output; no near-plane test at all -- the cheapest kernel. */
static inline void
project_vertices_array_ortho_fused_notex_noclip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
    int const sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
    int const cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
    int const sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
    int const cos_model_yaw = ToriDraw_ReadCosTable(model_yaw);
    int const sin_model_yaw = ToriDraw_ReadSinTable(model_yaw);

    struct ToriDraw_OrthoFusedCamera const cam = {
        .cos_model_yaw = cos_model_yaw,
        .sin_model_yaw = sin_model_yaw,
        .cos_camera_yaw = cos_camera_yaw,
        .sin_camera_yaw = sin_camera_yaw,
        .cos_camera_pitch = cos_camera_pitch,
        .sin_camera_pitch = sin_camera_pitch,
        .scene_x = scene_x,
        .scene_y = scene_y,
        .scene_z = scene_z,
        .camera_zoom16 = camera_zoom16,
        .model_mid_z = model_mid_z,
        /* The noclip lane never reads it. */
        .near_plane_z = 0,
    };

    /* Whatever the build's lane took off the front; 0 if it has no kernel. */
    int i = toridraw_ortho_lane_fused_notex(
        &cam,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            cos_model_yaw,
            sin_model_yaw,
            scene_x,
            scene_y,
            scene_z,
            cos_camera_yaw,
            sin_camera_yaw,
            cos_camera_pitch,
            sin_camera_pitch,
            &x_cam,
            &y_cam,
            &z_fin);

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** No camera-space output; marks vertices behind near_plane_z for the face test to DROP (see
 * header). */
static inline void
project_vertices_array_ortho_fused_notex_clip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw)
{
    int const cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
    int const sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
    int const cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
    int const sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
    int const cos_model_yaw = ToriDraw_ReadCosTable(model_yaw);
    int const sin_model_yaw = ToriDraw_ReadSinTable(model_yaw);

    struct ToriDraw_OrthoFusedCamera const cam = {
        .cos_model_yaw = cos_model_yaw,
        .sin_model_yaw = sin_model_yaw,
        .cos_camera_yaw = cos_camera_yaw,
        .sin_camera_yaw = sin_camera_yaw,
        .cos_camera_pitch = cos_camera_pitch,
        .sin_camera_pitch = sin_camera_pitch,
        .scene_x = scene_x,
        .scene_y = scene_y,
        .scene_z = scene_z,
        .camera_zoom16 = camera_zoom16,
        .model_mid_z = model_mid_z,
        .near_plane_z = near_plane_z,
    };

    /* Whatever the build's lane took off the front; 0 if it has no kernel. */
    int i = toridraw_ortho_lane_fused_notex_clip(
        &cam,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices);

    for( ; i < num_vertices; i++ )
    {
        int x_cam;
        int y_cam;
        int z_fin;
        toridraw_ortho_to_camera_space(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            cos_model_yaw,
            sin_model_yaw,
            scene_x,
            scene_y,
            scene_z,
            cos_camera_yaw,
            sin_camera_yaw,
            cos_camera_pitch,
            sin_camera_pitch,
            &x_cam,
            &y_cam,
            &z_fin);

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (with camera-space output).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_noclip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (with camera-space output, marks behind near_plane_z).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_clip(
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        orthographic_vertices_x[i] = x_cam;
        orthographic_vertices_y[i] = y_cam;
        orthographic_vertices_z[i] = z_fin;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (no camera-space output).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_notex_noclip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        screen_vertices_x[i] = (x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

/** Full 6DOF parallel projection (no camera-space output, marks behind near_plane_z).
 *  Scalar: model pitch/roll is the icon and spotanim path, not world geometry,
 *  so it is not worth a vector transform of its own. */
static inline void
project_vertices_array_ortho6_fused_notex_clip(
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    vertexint_t* RESTRICT vertex_x,
    vertexint_t* RESTRICT vertex_y,
    vertexint_t* RESTRICT vertex_z,
    int num_vertices,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_zoom16,
    int camera_pitch,
    int camera_yaw,
    int camera_roll)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedVertex pv = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll);

        int x_cam = pv.x;
        int y_cam = pv.y;
        int z_fin = pv.z;

        screen_vertices_x[i] = (z_fin < near_plane_z)
                                   ? TORIDRAW_SCREEN_X_NEAR_CLIPPED
                                   : ((x_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT);
        screen_vertices_y[i] = (y_cam * camera_zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_vertices_z[i] = z_fin - model_mid_z;
    }
}

#endif /* PROJECTION_ORTHO_U_C */
