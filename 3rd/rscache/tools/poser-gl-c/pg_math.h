#ifndef POSER_GL_C_PG_MATH_H
#define POSER_GL_C_PG_MATH_H

/*
 * The slice of JOML the editor actually uses.
 *
 * Matrices are column-major with JOML's `mCR` naming (column then row), which is
 * also OpenGL's memory order, so a PG_Mat4 can be handed to glUniformMatrix4fv
 * untransposed. Getting that convention wrong is silent: the model still draws,
 * just rotated into somewhere off-screen.
 */

#include <stdbool.h>

struct PG_Vec2
{
    float x, y;
};

struct PG_Vec3
{
    float x, y, z;
};

struct PG_Vec4
{
    float x, y, z, w;
};

struct PG_Mat4
{
    float m[16]; /* m[c * 4 + r] */
};

/** A ray: origin plus direction, the shape every pick in the editor takes. */
struct PG_Ray
{
    struct PG_Vec3 origin;
    struct PG_Vec3 dir;
};

struct PG_Vec3
pg_vec3(float x, float y, float z);
struct PG_Vec3
pg_vec3_add(struct PG_Vec3 a, struct PG_Vec3 b);
struct PG_Vec3
pg_vec3_sub(struct PG_Vec3 a, struct PG_Vec3 b);
struct PG_Vec3
pg_vec3_mul(struct PG_Vec3 a, float s);
struct PG_Vec3
pg_vec3_div(struct PG_Vec3 a, float s);
struct PG_Vec3
pg_vec3_cross(struct PG_Vec3 a, struct PG_Vec3 b);
float
pg_vec3_dot(struct PG_Vec3 a, struct PG_Vec3 b);
float
pg_vec3_length(struct PG_Vec3 a);
struct PG_Vec3
pg_vec3_normalize(struct PG_Vec3 a);
struct PG_Vec3
pg_vec3_lerp(struct PG_Vec3 a, struct PG_Vec3 b, float t);
bool
pg_vec3_equals(struct PG_Vec3 a, struct PG_Vec3 b);
/** Componentwise |a-b| <= eps, JOML's Vector3f.equals(v, delta). */
bool
pg_vec3_equals_eps(struct PG_Vec3 a, struct PG_Vec3 b, float eps);
/** Component by index: 0=x, 1=y, 2=z. Out-of-range reads 0. */
float
pg_vec3_get(struct PG_Vec3 a, int index);
void
pg_vec3_set(struct PG_Vec3* a, int index, float value);

struct PG_Mat4
pg_mat4_identity(void);
struct PG_Mat4
pg_mat4_mul(struct PG_Mat4 a, struct PG_Mat4 b);
void
pg_mat4_translate(struct PG_Mat4* m, struct PG_Vec3 t);
/** JOML rotateXYZ: rotateX then rotateY then rotateZ, angles in radians. */
void
pg_mat4_rotate_xyz(struct PG_Mat4* m, float rx, float ry, float rz);
void
pg_mat4_scale(struct PG_Mat4* m, float s);
struct PG_Vec4
pg_mat4_transform(struct PG_Mat4 m, struct PG_Vec4 v);
/** Returns false for a singular matrix, leaving `out` untouched. */
bool
pg_mat4_invert(struct PG_Mat4 m, struct PG_Mat4* out);

/**
 * JOML Matrix4f.unprojectRay against a combined projection*view matrix.
 *
 * `viewport` is {x, y, width, height} and win_y follows OpenGL's bottom-up
 * convention, so a top-down cursor position has to be flipped by the caller.
 */
bool
pg_unproject_ray(
    struct PG_Mat4 proj_view,
    float win_x,
    float win_y,
    const int viewport[4],
    struct PG_Ray* out);

/**
 * Ray/AABB slab test. On a hit, `out_near`/`out_far` receive the ray parameters
 * at entry and exit; JOML's intersectRayAab reports a hit when far >= 0, i.e. an
 * origin inside the box counts.
 */
bool
pg_intersect_ray_aabb(
    struct PG_Ray ray,
    struct PG_Vec3 min,
    struct PG_Vec3 max,
    float* out_near,
    float* out_far);

/**
 * Ray/plane, plane given as a point and a normal. Returns the ray parameter or
 * a negative value when the ray is parallel to the plane.
 */
float
pg_intersect_ray_plane(
    struct PG_Ray ray,
    struct PG_Vec3 point,
    struct PG_Vec3 normal);

float
pg_radians(float degrees);
float
pg_degrees(float radians);
int
pg_clampi(int v, int lo, int hi);
float
pg_clampf(float v, float lo, float hi);
/** Java's Math.floorMod: the result carries the sign of the divisor. */
int
pg_floor_mod(int a, int b);

#endif
