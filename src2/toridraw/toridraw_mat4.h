#ifndef TORIDRAW_MAT4_H
#define TORIDRAW_MAT4_H

/**
 * Minimal column-major 4x4 float matrix and quaternion math for skeletal
 * animation baking.  Matches the semantics of gl-matrix used in the RuneLite
 * reference code (docs/skeletal/).
 *
 * mat4 layout (column-major, indices 0-15):
 *   [ 0  4  8 12 ]
 *   [ 1  5  9 13 ]
 *   [ 2  6 10 14 ]
 *   [ 3  7 11 15 ]
 *
 * quat layout: [x, y, z, w]
 */

typedef float ToriMat4[16];
typedef float ToriQuat[4]; /* x y z w */
typedef float ToriVec3[3];

/* mat4 */

void toridraw_mat4_identity(ToriMat4 out);

/** out = a * b  (column-major) */
void toridraw_mat4_mul(ToriMat4 out, const ToriMat4 a, const ToriMat4 b);

/** out = inverse(m); returns 0 on success, non-zero if not invertible */
int toridraw_mat4_invert(ToriMat4 out, const ToriMat4 m);

void toridraw_mat4_copy(ToriMat4 out, const ToriMat4 m);

/** Build a rotation matrix from a unit quaternion */
void toridraw_mat4_from_quat(ToriMat4 out, const ToriQuat q);

/** Build a scaling matrix */
void toridraw_mat4_from_scaling(ToriMat4 out, float sx, float sy, float sz);

/** Extract translation from a mat4 */
void toridraw_mat4_get_translation(ToriVec3 out, const ToriMat4 m);

/** Extract per-axis scale from a mat4 */
void toridraw_mat4_get_scaling(ToriVec3 out, const ToriMat4 m);

/* quat */

void toridraw_quat_identity(ToriQuat q);

/** q = rotation by angle (radians) around unit axis */
void toridraw_quat_set_axis_angle(ToriQuat out, float ax, float ay, float az, float angle);

/** out = a * b */
void toridraw_quat_mul(ToriQuat out, const ToriQuat a, const ToriQuat b);

/* vec3 helpers */

void toridraw_vec3_set(ToriVec3 out, float x, float y, float z);

#endif /* TORIDRAW_MAT4_H */
