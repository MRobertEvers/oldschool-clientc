#ifndef TORIDRAW_PROJECTION_PERSPECTIVE_RSZBUF_U_C
#define TORIDRAW_PROJECTION_PERSPECTIVE_RSZBUF_U_C

/*
 * The rev-241 world projection [class174.method5690 with method5692, the
 * bounds from method5784, tiles per class137.method5020 / method5026].
 *
 * This is not the integer projection every other table uses. The 241 client
 * rotates in float -- the model's yaw through a 2048-step float table, the
 * camera through 16384-step ones -- and divides by depth in float, so a vertex
 * lands at a FRACTIONAL screen position and the rasterizer rounds it there. The
 * rszbuf rasterizer needs exactly those positions, and this writes them into
 * the scene's rszbuf group. It also fills the integer arrays every other
 * consumer of a projection reads (the pick test, the screen bound), from the
 * same float positions.
 *
 * The frustum cull before it is the client's integer cull on the model's
 * bounding cylinder, and the cylinder is the client's (rszbuf_model_bounds),
 * which is not toridraw's own: 241 takes ceilings of float extents where
 * toridraw adds one to a truncated root.
 *
 * Not mirrored: the client's draw-distance cull (callers emit only what is in
 * range) and the world-view matrix path (class147). A model with pitch or
 * roll takes them in integer, as toridraw always has, before the 241 yaw.
 */

#include "graphics/raster/rszbuf/rszbuf_model.h"
#include "graphics/raster/rszbuf/rszbuf_raster.h"
#include "graphics/shared_tables.h"
#include "impl/projection/projection.scalar_reference.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <math.h>

/* The client's angle tables, entry by entry: each is one Math.sin / Math.cos
 * narrowed the way the table narrows it, so computing the one entry needed is
 * the table. Camera angles are 14-bit [class199.field2680..2683]; toridraw's
 * are 11-bit, eight 14-bit steps apart. The model's yaw is 11-bit
 * [class169.field2306 / field2316]. */
static inline int
rszbuf_camera_sin(int angle14)
{
    return (int)(sin((double)angle14 * 3.834951969714103E-4) * 65536.0);
}

static inline int
rszbuf_camera_cos(int angle14)
{
    return (int)(cos((double)angle14 * 3.834951969714103E-4) * 65536.0);
}

static inline float
rszbuf_camera_sinf(int angle14)
{
    return (float)sin((double)angle14 * 3.834951969714103E-4);
}

static inline float
rszbuf_camera_cosf(int angle14)
{
    return (float)cos((double)angle14 * 3.834951969714103E-4);
}

static inline float
rszbuf_model_sinf(int angle11)
{
    return (float)sin((double)angle11 * 0.0030679615);
}

static inline float
rszbuf_model_cosf(int angle11)
{
    return (float)cos((double)angle11 * 0.0030679615);
}

/** The bounding cylinder the cull and the bucket sort use [method5784]. */
static inline void
rszbuf_model_bounds(
    struct ToriDraw_RsZbufModel* rz,
    const struct ToriDraw_Model* model)
{
    RSZBUF_NO_FMA
    float above = 0.0f;
    float below = 0.0f;
    float radius_squared = 0.0f;

    for( int v = 0; v < model->vertex_count; v++ )
    {
        float const x = (float)model->vertices_x[v];
        float const y = (float)model->vertices_y[v];
        float const z = (float)model->vertices_z[v];
        float const r2 = x * x + z * z;

        if( -y > above )
            above = -y;
        if( y > below )
            below = y;
        if( r2 > radius_squared )
            radius_squared = r2;
    }

    rz->bottom = (int)ceil((double)below);
    rz->height = (int)ceil((double)above);
    rz->radius = (int)ceil(sqrt((double)radius_squared));
    rz->depth_offset = (int)ceil(sqrt((double)toridraw_wrap_add(
        toridraw_wrap_mul(rz->height, rz->height), toridraw_wrap_mul(rz->radius, rz->radius))));
    rz->depth_range =
        rz->depth_offset + (int)ceil(sqrt((double)toridraw_wrap_add(
                               toridraw_wrap_mul(rz->radius, rz->radius),
                               toridraw_wrap_mul(rz->bottom, rz->bottom))));
}

/**
 * A model's own pitch and roll, in toridraw's integer maths and order (roll,
 * then pitch), ahead of the 241 yaw. The client has no per-draw pitch or roll
 * on this path; a model that carries one was rotated before it got here.
 */
static inline void
rszbuf_rotate_roll_pitch(int* x, int* y, int* z, int pitch, int roll)
{
    if( roll != 0 )
    {
        int const sin_roll = ToriDraw_ReadSinTable(roll);
        int const cos_roll = ToriDraw_ReadCosTable(roll);
        int const turned = (*y * sin_roll + *x * cos_roll) >> 16;
        *y = (*y * cos_roll - *x * sin_roll) >> 16;
        *x = turned;
    }
    if( pitch != 0 )
    {
        int const sin_pitch = ToriDraw_ReadSinTable(pitch);
        int const cos_pitch = ToriDraw_ReadCosTable(pitch);
        int const turned = (*y * cos_pitch - *z * sin_pitch) >> 16;
        *z = (*y * sin_pitch + *z * cos_pitch) >> 16;
        *y = turned;
    }
}

/** The clip box a raster of this viewport draws into, as the raster derives it. */
static inline void
rszbuf_viewport_size(const struct ToriDraw_ViewPort* view_port, int* width, int* height)
{
    int clip_left = view_port->clip_left < 0 ? 0 : view_port->clip_left;
    int clip_top = view_port->clip_top < 0 ? 0 : view_port->clip_top;
    int clip_right = view_port->clip_right > 0 ? view_port->clip_right : view_port->width;
    int clip_bottom = view_port->clip_bottom > 0 ? view_port->clip_bottom : view_port->height;

    if( clip_right < clip_left )
        clip_right = clip_left;
    if( clip_bottom < clip_top )
        clip_bottom = clip_top;
    *width = clip_right - clip_left;
    *height = clip_bottom - clip_top;
}

/**
 * Project one model the way the 241 client does. Returns a TORIDRAW_CULL_*
 * code; on VISIBLE the scene's rszbuf group and its integer vertex arrays
 * describe this model.
 */
static int
toridraw_project_rszbuf(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    RSZBUF_NO_FMA
    const struct ToriDraw_Model* const model = ToriDraw_ModelRead(hnd);
    struct ToriDraw_RsZbufModel* const rz = scene->rszbuf;

    assert(model);
    assert(rz);
    /* The client has no orthographic world camera. */
    assert(!toridraw_projection_is_parallel(camera->projection_mode));

    scene->projection_near_plane_z = RSZBUF_NEAR_PLANE;
    if( model->vertex_count > rz->vertex_capacity || model->face_count > rz->face_capacity )
        return TORIDRAW_CULL_ERROR;
    /* Past the client's static arrays the draw throws, and is caught. */
    if( model->vertex_count > RSZBUF_MODEL_CAPACITY || model->face_count > RSZBUF_MODEL_CAPACITY )
        return TORIDRAW_CULL_ERROR;

    int clip_width;
    int clip_height;
    rszbuf_viewport_size(view_port, &clip_width, &clip_height);
    int const center_x = clip_width >> 1;
    int const center_y = clip_height >> 1;
    /* The client's integer zoom [class175.field2452]. */
    int const scale = toridraw_projection_scale_from_cot16(toridraw_projection_cot16(
        camera->projection_mode, camera->projection_scale, camera->fov_rpi2048));

    int const pitch14 = ToriDraw_NormalizeAngle(camera->pitch) << 3;
    int const yaw14 = ToriDraw_NormalizeAngle(camera->yaw) << 3;
    int const sin_pitch = rszbuf_camera_sin(pitch14);
    int const cos_pitch = rszbuf_camera_cos(pitch14);
    int const sin_yaw = rszbuf_camera_sin(yaw14);
    int const cos_yaw = rszbuf_camera_cos(yaw14);

    int const px = position->x;
    int const py = position->y;
    int const pz = position->z;

    rszbuf_model_bounds(rz, model);

    /* The cylinder against the frustum, in the client's integer maths. */
    int const yawed_z = toridraw_wrap_sub(toridraw_wrap_mul(pz, cos_yaw), toridraw_wrap_mul(px, sin_yaw)) >> 16;
    int const center_depth =
        toridraw_wrap_add(toridraw_wrap_mul(py, sin_pitch), toridraw_wrap_mul(cos_pitch, yawed_z)) >> 16;
    int const radius_depth = toridraw_wrap_mul(rz->radius, cos_pitch) >> 16;
    int const far_depth = center_depth + radius_depth;
    if( far_depth <= RSZBUF_NEAR_PLANE )
        return TORIDRAW_CULL_FAST;

    int const yawed_x = toridraw_wrap_add(toridraw_wrap_mul(px, cos_yaw), toridraw_wrap_mul(pz, sin_yaw)) >> 16;
    if( toridraw_wrap_mul(yawed_x - rz->radius, scale) / far_depth >= clip_width - center_x )
        return TORIDRAW_CULL_FAST;
    if( toridraw_wrap_mul(rz->radius + yawed_x, scale) / far_depth <= -center_x )
        return TORIDRAW_CULL_FAST;

    int const pitched_y =
        toridraw_wrap_sub(toridraw_wrap_mul(py, cos_pitch), toridraw_wrap_mul(sin_pitch, yawed_z)) >> 16;
    int const radius_rise = toridraw_wrap_mul(rz->radius, sin_pitch) >> 16;
    int const bottom_extent = (toridraw_wrap_mul(rz->bottom, cos_pitch) >> 16) + radius_rise;
    if( toridraw_wrap_mul(pitched_y + bottom_extent, scale) / far_depth <= -center_y )
        return TORIDRAW_CULL_FAST;
    int const top_extent = (toridraw_wrap_mul(rz->height, cos_pitch) >> 16) + radius_rise;
    if( toridraw_wrap_mul(pitched_y - top_extent, scale) / far_depth >= clip_height - center_y )
        return TORIDRAW_CULL_FAST;

    int const height_depth = (toridraw_wrap_mul(rz->height, sin_pitch) >> 16) + radius_depth;
    bool const may_clip = center_depth - height_depth <= RSZBUF_NEAR_PLANE;
    rz->center_depth = center_depth;
    rz->center_x = center_x;
    rz->center_y = center_y;
    rz->projection_scale = scale;
    rz->camera_stored = may_clip || model->face_textures != NULL;
    rz->any_behind_near = false;

    /* Every vertex, in float. */
    float const sin_pitch_f = rszbuf_camera_sinf(pitch14);
    float const cos_pitch_f = rszbuf_camera_cosf(pitch14);
    float const sin_yaw_f = rszbuf_camera_sinf(yaw14);
    float const cos_yaw_f = rszbuf_camera_cosf(yaw14);
    int const model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    float const model_sin = rszbuf_model_sinf(model_yaw);
    float const model_cos = rszbuf_model_cosf(model_yaw);
    int const model_pitch = ToriDraw_NormalizeAngle(position->pitch);
    int const model_roll = ToriDraw_NormalizeAngle(position->roll);

    for( int v = 0; v < model->vertex_count; v++ )
    {
        int vx = model->vertices_x[v];
        int vy = model->vertices_y[v];
        int vz = model->vertices_z[v];

        if( model_roll != 0 || model_pitch != 0 )
            rszbuf_rotate_roll_pitch(&vx, &vy, &vz, model_pitch, model_roll);

        float x = (float)vx;
        float y = (float)vy;
        float z = (float)vz;

        if( model_yaw != 0 )
        {
            float const turned_x = model_sin * z + model_cos * x;
            z = model_cos * z - model_sin * x;
            x = turned_x;
        }

        float const world_x = (float)px + x;
        float const world_y = (float)py + y;
        float const world_z = (float)pz + z;
        float const camera_x = sin_yaw_f * world_z + cos_yaw_f * world_x;
        float const yawed = cos_yaw_f * world_z - sin_yaw_f * world_x;
        float const camera_y = cos_pitch_f * world_y - sin_pitch_f * yawed;
        float const camera_z = sin_pitch_f * world_y + cos_pitch_f * yawed;

        rz->bucket_depth[v] = rszbuf_java_int(camera_z) - center_depth;
        if( rz->camera_stored )
        {
            rz->camera_x[v] = rszbuf_java_int(camera_x);
            rz->camera_y[v] = rszbuf_java_int(camera_y);
            rz->camera_z[v] = rszbuf_java_int(camera_z);
        }

        if( camera_z >= (float)RSZBUF_NEAR_PLANE )
        {
            rz->screen_x[v] = (float)center_x + camera_x * (float)scale / camera_z;
            rz->screen_y[v] = (float)center_y + camera_y * (float)scale / camera_z;
            rz->depth[v] = camera_z;
            scene->screen_vertices_x[v] = rszbuf_java_int(rz->screen_x[v]) - center_x;
            scene->screen_vertices_y[v] = rszbuf_java_int(rz->screen_y[v]) - center_y;
        }
        else
        {
            rz->screen_x[v] = RSZBUF_BEHIND_NEAR_PLANE;
            rz->any_behind_near = true;
            scene->screen_vertices_x[v] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            scene->screen_vertices_y[v] = 0;
        }
        scene->screen_vertices_z[v] = rszbuf_java_int(camera_z) - center_depth;
        scene->orthographic_vertices_x[v] = rszbuf_java_int(camera_x);
        scene->orthographic_vertices_y[v] = rszbuf_java_int(camera_y);
        scene->orthographic_vertices_z[v] = rszbuf_java_int(camera_z);
    }

    /* A tile with a corner behind the near plane is not drawn at all; the
     * client never clips terrain. */
    if( (model->flags & TORIDRAW_MODEL_FLAG_TERRAIN) && rz->any_behind_near )
        return TORIDRAW_CULL_FAST;

    scene->projected_vertex.x = 0;
    scene->projected_vertex.y = 0;
    scene->projected_vertex.z = center_depth;
    scene->near_clipped = rz->any_behind_near;
    scene->projection_bound_vertices = 0;
    toridraw_projected_bound(scene, view_port, model->vertex_count);

    return ToriDraw_AabbCull(&scene->aabb, view_port, camera);
}

#endif
