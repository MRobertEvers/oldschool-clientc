#include "pg_math.h"

#include <math.h>
#include <string.h>

struct PG_Vec3
pg_vec3(float x, float y, float z)
{
    struct PG_Vec3 v = { x, y, z };
    return v;
}

struct PG_Vec3
pg_vec3_add(struct PG_Vec3 a, struct PG_Vec3 b)
{
    return pg_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

struct PG_Vec3
pg_vec3_sub(struct PG_Vec3 a, struct PG_Vec3 b)
{
    return pg_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

struct PG_Vec3
pg_vec3_mul(struct PG_Vec3 a, float s)
{
    return pg_vec3(a.x * s, a.y * s, a.z * s);
}

struct PG_Vec3
pg_vec3_div(struct PG_Vec3 a, float s)
{
    if( s == 0.0f )
        return a;
    return pg_vec3(a.x / s, a.y / s, a.z / s);
}

struct PG_Vec3
pg_vec3_cross(struct PG_Vec3 a, struct PG_Vec3 b)
{
    return pg_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

float
pg_vec3_dot(struct PG_Vec3 a, struct PG_Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float
pg_vec3_length(struct PG_Vec3 a)
{
    return sqrtf(pg_vec3_dot(a, a));
}

struct PG_Vec3
pg_vec3_normalize(struct PG_Vec3 a)
{
    float len = pg_vec3_length(a);
    if( len == 0.0f )
        return a;
    return pg_vec3_div(a, len);
}

struct PG_Vec3
pg_vec3_lerp(struct PG_Vec3 a, struct PG_Vec3 b, float t)
{
    return pg_vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

bool
pg_vec3_equals(struct PG_Vec3 a, struct PG_Vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool
pg_vec3_equals_eps(struct PG_Vec3 a, struct PG_Vec3 b, float eps)
{
    return fabsf(a.x - b.x) <= eps && fabsf(a.y - b.y) <= eps && fabsf(a.z - b.z) <= eps;
}

float
pg_vec3_get(struct PG_Vec3 a, int index)
{
    switch( index )
    {
    case 0:
        return a.x;
    case 1:
        return a.y;
    case 2:
        return a.z;
    default:
        return 0.0f;
    }
}

void
pg_vec3_set(struct PG_Vec3* a, int index, float value)
{
    switch( index )
    {
    case 0:
        a->x = value;
        break;
    case 1:
        a->y = value;
        break;
    case 2:
        a->z = value;
        break;
    default:
        break;
    }
}

struct PG_Mat4
pg_mat4_identity(void)
{
    struct PG_Mat4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

struct PG_Mat4
pg_mat4_mul(struct PG_Mat4 a, struct PG_Mat4 b)
{
    struct PG_Mat4 r;
    for( int c = 0; c < 4; c++ )
    {
        for( int row = 0; row < 4; row++ )
        {
            float sum = 0.0f;
            for( int k = 0; k < 4; k++ )
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

void
pg_mat4_translate(struct PG_Mat4* m, struct PG_Vec3 t)
{
    struct PG_Mat4 tr = pg_mat4_identity();
    tr.m[12] = t.x;
    tr.m[13] = t.y;
    tr.m[14] = t.z;
    *m = pg_mat4_mul(*m, tr);
}

void
pg_mat4_rotate_xyz(struct PG_Mat4* m, float rx, float ry, float rz)
{
    struct PG_Mat4 r = pg_mat4_identity();
    float sx = sinf(rx), cx = cosf(rx);
    float sy = sinf(ry), cy = cosf(ry);
    float sz = sinf(rz), cz = cosf(rz);

    /* Rx * Ry * Rz, expanded. */
    r.m[0] = cy * cz;
    r.m[1] = cx * sz + sx * sy * cz;
    r.m[2] = sx * sz - cx * sy * cz;
    r.m[4] = -cy * sz;
    r.m[5] = cx * cz - sx * sy * sz;
    r.m[6] = sx * cz + cx * sy * sz;
    r.m[8] = sy;
    r.m[9] = -sx * cy;
    r.m[10] = cx * cy;
    *m = pg_mat4_mul(*m, r);
}

void
pg_mat4_scale(struct PG_Mat4* m, float s)
{
    struct PG_Mat4 sc = pg_mat4_identity();
    sc.m[0] = sc.m[5] = sc.m[10] = s;
    *m = pg_mat4_mul(*m, sc);
}

struct PG_Vec4
pg_mat4_transform(struct PG_Mat4 m, struct PG_Vec4 v)
{
    struct PG_Vec4 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    r.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return r;
}

bool
pg_mat4_invert(struct PG_Mat4 m, struct PG_Mat4* out)
{
    const float* a = m.m;
    float inv[16];
    float det;

    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
             a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
             a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
              a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
              a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if( det == 0.0f )
        return false;
    det = 1.0f / det;
    for( int i = 0; i < 16; i++ )
        out->m[i] = inv[i] * det;
    return true;
}

bool
pg_unproject_ray(
    struct PG_Mat4 proj_view,
    float win_x,
    float win_y,
    const int viewport[4],
    struct PG_Ray* out)
{
    struct PG_Mat4 inv;
    struct PG_Vec4 near_point, far_point;
    float ndc_x, ndc_y;

    if( viewport[2] == 0 || viewport[3] == 0 )
        return false;
    if( !pg_mat4_invert(proj_view, &inv) )
        return false;

    ndc_x = (win_x - (float)viewport[0]) / (float)viewport[2] * 2.0f - 1.0f;
    ndc_y = (win_y - (float)viewport[1]) / (float)viewport[3] * 2.0f - 1.0f;

    {
        struct PG_Vec4 a = { ndc_x, ndc_y, -1.0f, 1.0f };
        struct PG_Vec4 b = { ndc_x, ndc_y, 1.0f, 1.0f };
        near_point = pg_mat4_transform(inv, a);
        far_point = pg_mat4_transform(inv, b);
    }
    if( near_point.w == 0.0f || far_point.w == 0.0f )
        return false;

    near_point.x /= near_point.w;
    near_point.y /= near_point.w;
    near_point.z /= near_point.w;
    far_point.x /= far_point.w;
    far_point.y /= far_point.w;
    far_point.z /= far_point.w;

    out->origin = pg_vec3(near_point.x, near_point.y, near_point.z);
    out->dir = pg_vec3(
        far_point.x - near_point.x, far_point.y - near_point.y, far_point.z - near_point.z);
    return true;
}

bool
pg_intersect_ray_aabb(
    struct PG_Ray ray,
    struct PG_Vec3 min,
    struct PG_Vec3 max,
    float* out_near,
    float* out_far)
{
    float tmin = -INFINITY;
    float tmax = INFINITY;
    const float o[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
    const float d[3] = { ray.dir.x, ray.dir.y, ray.dir.z };
    const float lo[3] = { min.x, min.y, min.z };
    const float hi[3] = { max.x, max.y, max.z };

    for( int i = 0; i < 3; i++ )
    {
        if( d[i] == 0.0f )
        {
            if( o[i] < lo[i] || o[i] > hi[i] )
                return false;
            continue;
        }
        {
            float inv = 1.0f / d[i];
            float t1 = (lo[i] - o[i]) * inv;
            float t2 = (hi[i] - o[i]) * inv;
            if( t1 > t2 )
            {
                float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if( t1 > tmin )
                tmin = t1;
            if( t2 < tmax )
                tmax = t2;
            if( tmin > tmax )
                return false;
        }
    }
    if( tmax < 0.0f )
        return false;
    if( out_near )
        *out_near = tmin;
    if( out_far )
        *out_far = tmax;
    return true;
}

float
pg_intersect_ray_plane(
    struct PG_Ray ray,
    struct PG_Vec3 point,
    struct PG_Vec3 normal)
{
    float denom = pg_vec3_dot(ray.dir, normal);
    if( denom == 0.0f )
        return -1.0f;
    return pg_vec3_dot(pg_vec3_sub(point, ray.origin), normal) / denom;
}

float
pg_radians(float degrees)
{
    return degrees * 0.017453292519943295f;
}

float
pg_degrees(float radians)
{
    return radians * 57.29577951308232f;
}

int
pg_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float
pg_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int
pg_floor_mod(int a, int b)
{
    int r;
    if( b == 0 )
        return 0;
    r = a % b;
    if( r != 0 && ((r < 0) != (b < 0)) )
        r += b;
    return r;
}
