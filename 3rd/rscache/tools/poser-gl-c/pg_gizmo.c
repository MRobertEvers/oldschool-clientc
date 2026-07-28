#include "pg_gizmo.h"

#include <math.h>
#include <string.h>

#define PG_ROTATION_SPEED 3
/* Below this the cursor has not really moved, and asin of the residual cross
 * product would jitter the value every frame the mouse is held still. */
#define PG_MOVE_THRESHOLD 0.01f

static struct PG_Vec4
rgba(int r, int g, int b)
{
    struct PG_Vec4 c = { (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f };
    return c;
}

void
pg_gizmo_set(struct PG_Gizmo* gizmo, int kind, struct PG_Vec3 position)
{
    memset(gizmo, 0, sizeof(*gizmo));
    gizmo->kind = kind;
    gizmo->position = position;
    gizmo->selected_axis = -1;

    for( int i = 0; i < 3; i++ )
        gizmo->axes[i].axis = i;
    gizmo->axes[0].colour = rgba(220, 14, 44);
    gizmo->axes[1].colour = rgba(14, 220, 44);
    gizmo->axes[2].colour = rgba(14, 44, 220);

    if( kind == PG_GIZMO_ROTATION )
    {
        gizmo->scale = 25.0f;
        /* The ring model lies in the XZ plane, so the Y axis needs no rotation
         * and the other two are turned onto their planes. */
        gizmo->axes[0].rotation = pg_vec3(0, 0, 90);
        gizmo->axes[1].rotation = pg_vec3(0, 0, 0);
        gizmo->axes[2].rotation = pg_vec3(0, 90, 90);
    }
    else
    {
        gizmo->scale = 40.0f;
        /* The arrow model points along +X, and each rotation turns it onto the
         * negative half of its axis — which is where the pick volumes are. */
        gizmo->axes[0].rotation = pg_vec3(0, 180, 0);
        gizmo->axes[1].rotation = pg_vec3(0, 0, -90);
        gizmo->axes[2].rotation = pg_vec3(0, 90, 0);
    }
}

static float
relative_scale(const struct PG_Gizmo* gizmo, int entity_size)
{
    return gizmo->scale * sqrtf((float)(entity_size > 0 ? entity_size : 1));
}

/*
 * Where the ray meets the plane through the gizmo facing the camera.
 *
 * Using a camera-facing plane rather than the axis itself is what lets a drag
 * continue after the cursor leaves the thin arrow: the axis component of the
 * movement is taken from a surface the cursor cannot miss.
 */
static struct PG_Vec3
plane_intersection(const struct PG_Gizmo* gizmo, struct PG_Ray ray)
{
    struct PG_Vec3 normal = pg_vec3(-ray.dir.x, -ray.dir.y, -ray.dir.z);
    float t = pg_intersect_ray_plane(ray, gizmo->position, normal);
    return pg_vec3_add(ray.origin, pg_vec3_mul(ray.dir, t));
}

static struct PG_Vec3
circle_intersection(const struct PG_Gizmo* gizmo, struct PG_Ray ray, int entity_size)
{
    struct PG_Vec3 on_plane = plane_intersection(gizmo, ray);
    struct PG_Vec3 direction =
        pg_vec3_mul(
            pg_vec3_normalize(pg_vec3_sub(on_plane, gizmo->position)),
            relative_scale(gizmo, entity_size));
    struct PG_Vec3 point = pg_vec3_add(gizmo->position, direction);
    return pg_vec3_normalize(pg_vec3_sub(on_plane, point));
}

static int
closest_axis(const struct PG_Gizmo* gizmo, struct PG_Ray ray, int entity_size)
{
    float min_distance = 3.402823466e+38F;
    int closest = -1;
    float scale = relative_scale(gizmo, entity_size);

    for( int i = 0; i < 3; i++ )
    {
        struct PG_Vec3 min;
        struct PG_Vec3 max;
        float near_t = 0.0f;
        float far_t = 0.0f;

        if( gizmo->kind == PG_GIZMO_ROTATION )
        {
            /* A slab: full radius across the ring's plane, two units thick. */
            struct PG_Vec3 offset = pg_vec3(scale, scale, scale);
            pg_vec3_set(&offset, i, 2.0f);
            min = pg_vec3_sub(gizmo->position, offset);
            max = pg_vec3_add(gizmo->position, offset);
        }
        else
        {
            /* A shaft, extending along the negative half of the axis. */
            struct PG_Vec3 lower = pg_vec3(2.0f, 2.0f, 2.0f);
            struct PG_Vec3 upper = pg_vec3(2.0f, 2.0f, 2.0f);
            pg_vec3_set(&lower, i, scale);
            pg_vec3_set(&upper, i, 0.0f);
            min = pg_vec3_sub(gizmo->position, lower);
            max = pg_vec3_add(gizmo->position, upper);
        }

        if( pg_intersect_ray_aabb(ray, min, max, &near_t, &far_t) && near_t < min_distance )
        {
            min_distance = near_t;
            closest = i;
        }
    }
    return closest;
}

int
pg_gizmo_update(
    struct PG_Gizmo* gizmo,
    struct PG_Ray ray,
    bool lmb_down,
    int entity_size,
    int* out_delta,
    bool* out_cyclic)
{
    struct PG_GizmoAxis* axis;
    struct PG_Vec3 zero = pg_vec3(0, 0, 0);

    *out_delta = 0;
    *out_cyclic = false;

    if( !lmb_down )
    {
        int next = closest_axis(gizmo, ray, entity_size);
        gizmo->active = false;
        if( next != gizmo->selected_axis )
        {
            gizmo->selected_axis = next;
            if( next >= 0 )
                gizmo->axes[next].previous_intersection = zero;
        }
        return -1;
    }

    gizmo->active = true;
    if( gizmo->selected_axis < 0 )
        return -1;
    axis = &gizmo->axes[gizmo->selected_axis];

    if( gizmo->kind == PG_GIZMO_ROTATION )
    {
        struct PG_Vec3 intersection = circle_intersection(gizmo, ray, entity_size);
        int result = -1;

        if( !pg_vec3_equals(axis->previous_intersection, zero) &&
            !pg_vec3_equals_eps(intersection, axis->previous_intersection, PG_MOVE_THRESHOLD) )
        {
            struct PG_Vec3 cross = pg_vec3_cross(intersection, axis->previous_intersection);
            float sine = pg_vec3_length(cross);
            float theta = asinf(pg_clampf(sine, -1.0f, 1.0f));
            float degrees = pg_degrees(theta);
            int value = (int)ceilf(degrees);

            if( pg_vec3_get(cross, axis->axis) > 0.0f )
                value = -value;
            if( axis->axis == 1 )
                value = -value;
            /* One step per frame in whichever direction, scaled: the raw angle is
             * noisy at this sample rate and a proportional value would spin. */
            value = pg_clampi(value, -1, 1) * PG_ROTATION_SPEED;
            *out_delta = value;
            *out_cyclic = true;
            result = axis->axis;
        }
        axis->previous_intersection = intersection;
        return result;
    }
    else
    {
        struct PG_Vec3 intersection = plane_intersection(gizmo, ray);
        int result = -1;

        if( !pg_vec3_equals(axis->previous_intersection, zero) &&
            !pg_vec3_equals(intersection, axis->previous_intersection) )
        {
            float delta = pg_vec3_get(
                pg_vec3_sub(intersection, axis->previous_intersection), axis->axis);
            float value = delta > 0.0f ? ceilf(delta) : floorf(delta);
            /* The entity shader flips x, so a rightward drag on the x arrow is a
             * decrease in model space. */
            if( axis->axis == 0 )
                value = -value;
            *out_delta = (int)value;
            *out_cyclic = false;
            result = axis->axis;
        }
        axis->previous_intersection = intersection;
        return result;
    }
}

void
pg_gizmo_render(
    struct PG_Gizmo* gizmo,
    struct PG_Renderer* renderer,
    struct PG_Mat4 view,
    int entity_size)
{
    const struct PG_GlModel* model;
    float scale = relative_scale(gizmo, entity_size);

    switch( gizmo->kind )
    {
    case PG_GIZMO_ROTATION:
        model = &renderer->rotation_gizmo;
        break;
    case PG_GIZMO_SCALE:
        model = &renderer->scale_gizmo;
        break;
    default:
        model = &renderer->translation_gizmo;
        break;
    }
    if( !model->vao )
        return;

    pg_glUseProgram(renderer->gizmo_shader.program);
    pg_glDisable(GL_DEPTH_TEST);
    pg_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    pg_glBindVertexArray(model->vao);
    pg_glUniformMatrix4fv(
        pg_glGetUniformLocation(renderer->gizmo_shader.program, "viewMatrix"), 1, GL_FALSE,
        view.m);
    pg_glUniformMatrix4fv(
        pg_glGetUniformLocation(renderer->gizmo_shader.program, "projectionMatrix"), 1, GL_FALSE,
        renderer->projection.m);

    /* Reverse order so the x axis, drawn last, sits on top — and while a rotation
     * drag is in progress only the ring being turned is drawn, so the other two
     * do not read as part of the motion. */
    for( int i = 2; i >= 0; i-- )
    {
        struct PG_GizmoAxis* axis = &gizmo->axes[i];
        struct PG_Mat4 transformation;
        struct PG_Vec4 colour = axis->colour;

        if( gizmo->active && gizmo->kind == PG_GIZMO_ROTATION && i != gizmo->selected_axis )
            continue;

        transformation = pg_create_transformation_matrix(gizmo->position, axis->rotation, scale);
        pg_glUniformMatrix4fv(
            pg_glGetUniformLocation(renderer->gizmo_shader.program, "transformationMatrix"), 1,
            GL_FALSE, transformation.m);
        colour.w = i == gizmo->selected_axis ? 0.6f : 1.0f;
        pg_glUniform4f(
            pg_glGetUniformLocation(renderer->gizmo_shader.program, "colour"),
            colour.x,
            colour.y,
            colour.z,
            colour.w);
        pg_glDrawArrays(GL_TRIANGLES, 0, model->vertex_count);
    }

    pg_glEnable(GL_DEPTH_TEST);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
}
