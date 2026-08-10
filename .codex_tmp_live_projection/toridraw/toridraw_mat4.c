#include "toridraw_mat4.h"

#include <math.h>
#include <string.h>

/* ---- vec3 ---- */

void
toridraw_vec3_set(ToriVec3 out, float x, float y, float z)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* ---- mat4 ---- */

void
toridraw_mat4_identity(ToriMat4 out)
{
    memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void
toridraw_mat4_copy(ToriMat4 out, const ToriMat4 m)
{
    memcpy(out, m, 16 * sizeof(float));
}

void
toridraw_mat4_mul(ToriMat4 out, const ToriMat4 a, const ToriMat4 b)
{
    ToriMat4 tmp;
    for( int col = 0; col < 4; col++ )
    {
        for( int row = 0; row < 4; row++ )
        {
            float sum = 0.0f;
            for( int k = 0; k < 4; k++ )
                sum += a[k * 4 + row] * b[col * 4 + k];
            tmp[col * 4 + row] = sum;
        }
    }
    memcpy(out, tmp, 16 * sizeof(float));
}

int
toridraw_mat4_invert(ToriMat4 out, const ToriMat4 m)
{
    /* Cofactor / adjugate expansion — matches gl-matrix mat4.invert */
    float a00 = m[0],  a01 = m[1],  a02 = m[2],  a03 = m[3];
    float a10 = m[4],  a11 = m[5],  a12 = m[6],  a13 = m[7];
    float a20 = m[8],  a21 = m[9],  a22 = m[10], a23 = m[11];
    float a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if( det == 0.0f )
        return 1;

    float inv = 1.0f / det;
    out[0]  = (a11 * b11 - a12 * b10 + a13 * b09) * inv;
    out[1]  = (a02 * b10 - a01 * b11 - a03 * b09) * inv;
    out[2]  = (a31 * b05 - a32 * b04 + a33 * b03) * inv;
    out[3]  = (a22 * b04 - a21 * b05 - a23 * b03) * inv;
    out[4]  = (a12 * b08 - a10 * b11 - a13 * b07) * inv;
    out[5]  = (a00 * b11 - a02 * b08 + a03 * b07) * inv;
    out[6]  = (a32 * b02 - a30 * b05 - a33 * b01) * inv;
    out[7]  = (a20 * b05 - a22 * b02 + a23 * b01) * inv;
    out[8]  = (a10 * b10 - a11 * b08 + a13 * b06) * inv;
    out[9]  = (a01 * b08 - a00 * b10 - a03 * b06) * inv;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * inv;
    out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * inv;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * inv;
    out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * inv;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * inv;
    out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * inv;
    return 0;
}

void
toridraw_mat4_from_quat(ToriMat4 out, const ToriQuat q)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    out[0]  = 1.0f - (yy + zz);
    out[1]  = xy + wz;
    out[2]  = xz - wy;
    out[3]  = 0.0f;
    out[4]  = xy - wz;
    out[5]  = 1.0f - (xx + zz);
    out[6]  = yz + wx;
    out[7]  = 0.0f;
    out[8]  = xz + wy;
    out[9]  = yz - wx;
    out[10] = 1.0f - (xx + yy);
    out[11] = 0.0f;
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

void
toridraw_mat4_from_scaling(ToriMat4 out, float sx, float sy, float sz)
{
    memset(out, 0, 16 * sizeof(float));
    out[0]  = sx;
    out[5]  = sy;
    out[10] = sz;
    out[15] = 1.0f;
}

void
toridraw_mat4_get_translation(ToriVec3 out, const ToriMat4 m)
{
    out[0] = m[12];
    out[1] = m[13];
    out[2] = m[14];
}

void
toridraw_mat4_get_scaling(ToriVec3 out, const ToriMat4 m)
{
    /* gl-matrix: length of each column basis vector */
    float sx = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    float sy = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    float sz = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
    out[0] = sx;
    out[1] = sy;
    out[2] = sz;
}

/* ---- quat ---- */

void
toridraw_quat_identity(ToriQuat q)
{
    q[0] = q[1] = q[2] = 0.0f;
    q[3] = 1.0f;
}

void
toridraw_quat_set_axis_angle(ToriQuat out, float ax, float ay, float az, float angle)
{
    float half = angle * 0.5f;
    float s = sinf(half);
    out[0] = ax * s;
    out[1] = ay * s;
    out[2] = az * s;
    out[3] = cosf(half);
}

void
toridraw_quat_mul(ToriQuat out, const ToriQuat a, const ToriQuat b)
{
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    /* Matches gl-matrix quat.mul */
    out[0] = ax * bw + aw * bx + ay * bz - az * by;
    out[1] = ay * bw + aw * by + az * bx - ax * bz;
    out[2] = az * bw + aw * bz + ax * by - ay * bx;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}
