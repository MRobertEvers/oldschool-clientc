#include "dat2_skeletalbase.h"

#include "dat2_animaya.h"
#include "dat2_framemap.h"

#include "../rsbuffer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- column-major mat4 helpers ------------------------------------------
 * Local statics (skb_ prefixed for the unity build). */

static void
skb_mat4_identity(RSCache_SkeletalMat4 m)
{
    memset(m, 0, sizeof(RSCache_SkeletalMat4));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void
skb_mat4_copy(
    RSCache_SkeletalMat4 dst,
    const RSCache_SkeletalMat4 src)
{
    memcpy(dst, src, sizeof(RSCache_SkeletalMat4));
}

static void
skb_mat4_mul(
    RSCache_SkeletalMat4 out,
    const RSCache_SkeletalMat4 a,
    const RSCache_SkeletalMat4 b)
{
    RSCache_SkeletalMat4 tmp;
    for( int col = 0; col < 4; col++ )
        for( int row = 0; row < 4; row++ )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; k++ )
                s += a[k * 4 + row] * b[col * 4 + k];
            tmp[col * 4 + row] = s;
        }
    memcpy(out, tmp, sizeof(RSCache_SkeletalMat4));
}

/* Singular input yields identity — a degenerate bone must not poison the whole
 * palette with NaN (the renderer's finite check would drop those vertices). */
static void
skb_mat4_invert(
    RSCache_SkeletalMat4 out,
    const RSCache_SkeletalMat4 m)
{
    float a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3];
    float a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
    float a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
    float a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
    float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    float inv;

    if( det == 0.0f )
    {
        skb_mat4_identity(out);
        return;
    }
    inv = 1.0f / det;
    out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * inv;
    out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * inv;
    out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * inv;
    out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * inv;
    out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * inv;
    out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * inv;
    out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * inv;
    out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * inv;
    out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * inv;
    out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * inv;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * inv;
    out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * inv;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * inv;
    out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * inv;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * inv;
    out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * inv;
}

/* Euler XYZ off an inverted local matrix (SkeletalBone.getRotation). */
static void
skb_mat4_rotation_xyz(
    RSCache_SkeletalVec3 out,
    const RSCache_SkeletalMat4 m)
{
    float cos_x;

    out[0] = -asinf(fmaxf(-1.0f, fminf(1.0f, m[6])));
    cos_x = cosf(out[0]);
    if( fabsf(cos_x) > 0.005f )
    {
        out[1] = atan2f(m[2], m[10]);
        out[2] = atan2f(m[4], m[5]);
    }
    else
    {
        out[1] = (m[6] < 0.0f) ? atan2f(m[1], m[0]) : -atan2f(m[1], m[0]);
        out[2] = 0.0f;
    }
}

static void
skb_mat4_translation(
    RSCache_SkeletalVec3 out,
    const RSCache_SkeletalMat4 m)
{
    out[0] = m[12];
    out[1] = m[13];
    out[2] = m[14];
}

static void
skb_mat4_scaling(
    RSCache_SkeletalVec3 out,
    const RSCache_SkeletalMat4 m)
{
    out[0] = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    out[1] = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    out[2] = sqrtf(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
}

static int
skb_bone_extract_transformations(struct RSCache_Dat2SkeletalBone* bone)
{
    int pc = bone->pose_count;
    RSCache_SkeletalMat4 inv;

    bone->rotations = malloc((size_t)pc * sizeof(RSCache_SkeletalVec3));
    bone->translations = malloc((size_t)pc * sizeof(RSCache_SkeletalVec3));
    bone->scalings = malloc((size_t)pc * sizeof(RSCache_SkeletalVec3));
    if( !bone->rotations || !bone->translations || !bone->scalings )
        return 0;

    for( int i = 0; i < pc; i++ )
    {
        skb_mat4_invert(inv, bone->local_matrices[i]);
        skb_mat4_rotation_xyz(bone->rotations[i], inv);
        skb_mat4_translation(bone->translations[i], bone->local_matrices[i]);
        skb_mat4_scaling(bone->scalings[i], bone->local_matrices[i]);
    }
    return 1;
}

/* ---- lazy model matrices ------------------------------------------------ */

const RSCache_SkeletalMat4*
RSCache_Dat2SkeletalBoneModelMatrix(
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int pose_id)
{
    struct RSCache_Dat2SkeletalBone* bone = &base->bones[bone_index];
    RSCache_SkeletalMat4* mm;

    if( pose_id < 0 || pose_id >= bone->pose_count )
        pose_id = 0;
    mm = &bone->model_matrices[pose_id];

    /* Uncomputed == all-zero (calloc'd): no valid transform has m[15] == 0, so
     * that element doubles as the computed flag. */
    if( (*mm)[15] == 0.0f )
    {
        if( bone->parent == NULL )
        {
            skb_mat4_copy(*mm, bone->local_matrices[pose_id]);
        }
        else
        {
            int pi = (int)(bone->parent - base->bones);
            const RSCache_SkeletalMat4* parent_mm =
                RSCache_Dat2SkeletalBoneModelMatrix(base, pi, pose_id);
            skb_mat4_mul(*mm, *parent_mm, bone->local_matrices[pose_id]);
        }
    }
    return (const RSCache_SkeletalMat4*)mm;
}

const RSCache_SkeletalMat4*
RSCache_Dat2SkeletalBoneInvertedModelMatrix(
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int pose_id)
{
    struct RSCache_Dat2SkeletalBone* bone = &base->bones[bone_index];
    RSCache_SkeletalMat4* imm;

    if( pose_id < 0 || pose_id >= bone->pose_count )
        pose_id = 0;
    imm = &bone->inverted_model_matrices[pose_id];

    if( (*imm)[15] == 0.0f )
    {
        const RSCache_SkeletalMat4* mm =
            RSCache_Dat2SkeletalBoneModelMatrix(base, bone_index, pose_id);
        skb_mat4_invert(*imm, *mm);
    }
    return (const RSCache_SkeletalMat4*)imm;
}

void
RSCache_Dat2SkeletalBaseFree(struct RSCache_Dat2SkeletalBase* base)
{
    if( !base )
        return;
    if( base->bones )
    {
        for( int i = 0; i < base->bone_count; i++ )
        {
            struct RSCache_Dat2SkeletalBone* b = &base->bones[i];
            free(b->local_matrices);
            free(b->model_matrices);
            free(b->inverted_model_matrices);
            free(b->rotations);
            free(b->translations);
            free(b->scalings);
        }
        free(base->bones);
    }
    free(base);
}

/* ---- decode ------------------------------------------------------------- */

struct RSCache_Dat2SkeletalBase*
RSCache_Dat2SkeletalBaseNewDecodeTail(
    int base_id,
    const uint8_t* tail,
    int tail_size)
{
    struct RSCache_Buffer buffer;
    struct RSCache_Dat2SkeletalBase* base;
    int bone_count;
    int pose_count;

    /* u16 boneCount + u8 poseCount is the smallest a rig can be. */
    if( !tail || tail_size < 3 )
        return NULL;

    buffer.data = (uint8_t*)tail;
    buffer.size = (uint32_t)tail_size;
    buffer.position = 0;

    bone_count = g2(&buffer);
    if( bone_count <= 0 )
        return NULL;
    pose_count = g1(&buffer);
    if( pose_count <= 0 )
        return NULL;
    /* s16 parent + poseCount * 19 floats per bone. A tail too short for that is
     * an unread pad that happens to start with plausible counts, not a rig. */
    if( (int64_t)tail_size - 3 <
        (int64_t)bone_count * (2 + (int64_t)pose_count * 19 * 4) )
        return NULL;

    base = calloc(1, sizeof(struct RSCache_Dat2SkeletalBase));
    if( !base )
        return NULL;
    base->id = base_id;
    base->bone_count = bone_count;
    base->pose_count = pose_count;
    base->bones = calloc((size_t)bone_count, sizeof(struct RSCache_Dat2SkeletalBone));
    if( !base->bones )
    {
        RSCache_Dat2SkeletalBaseFree(base);
        return NULL;
    }

    for( int bi = 0; bi < bone_count; bi++ )
    {
        struct RSCache_Dat2SkeletalBone* bone = &base->bones[bi];

        bone->pose_count = pose_count;
        bone->parent_id = g2b(&buffer);

        bone->local_matrices = calloc((size_t)pose_count, sizeof(RSCache_SkeletalMat4));
        bone->model_matrices = calloc((size_t)pose_count, sizeof(RSCache_SkeletalMat4));
        bone->inverted_model_matrices = calloc((size_t)pose_count, sizeof(RSCache_SkeletalMat4));
        if( !bone->local_matrices || !bone->model_matrices || !bone->inverted_model_matrices )
        {
            RSCache_Dat2SkeletalBaseFree(base);
            return NULL;
        }

        for( int p = 0; p < pose_count; p++ )
        {
            float* lm = bone->local_matrices[p];
            for( int e = 0; e < 16; e++ )
                lm[e] = gf(&buffer);
            /* 3 trailing floats per pose the client never reads. */
            gf(&buffer);
            gf(&buffer);
            gf(&buffer);
        }
        if( !skb_bone_extract_transformations(bone) )
        {
            RSCache_Dat2SkeletalBaseFree(base);
            return NULL;
        }
    }

    for( int bi = 0; bi < bone_count; bi++ )
    {
        int pid = base->bones[bi].parent_id;
        if( pid >= 0 && pid < bone_count && pid != bi )
            base->bones[bi].parent = &base->bones[pid];
        else
            base->bones[bi].parent = NULL;
    }

    return base;
}

struct RSCache_Dat2SkeletalBase*
RSCache_Dat2SkeletalBaseNewFromFramemap(const struct RSCache_Dat2Framemap* framemap)
{
    if( !framemap )
        return NULL;
    return RSCache_Dat2SkeletalBaseNewDecodeTail(
        framemap->id, framemap->tail, framemap->tail_size);
}

/* ---- palette bake ------------------------------------------------------- */

static void
skb_quat_axis_angle(
    float q[4],
    float ax,
    float ay,
    float az,
    float angle)
{
    float h = angle * 0.5f;
    float s = sinf(h);
    q[0] = ax * s;
    q[1] = ay * s;
    q[2] = az * s;
    q[3] = cosf(h);
}

static void
skb_quat_mul(
    float out[4],
    const float a[4],
    const float b[4])
{
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = ax * bw + aw * bx + ay * bz - az * by;
    out[1] = ay * bw + aw * by + az * bx - ax * bz;
    out[2] = az * bw + aw * bz + ax * by - ay * bx;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

static void
skb_mat4_from_quat(
    RSCache_SkeletalMat4 out,
    const float q[4])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    memset(out, 0, sizeof(RSCache_SkeletalMat4));
    out[0] = 1.0f - (yy + zz);
    out[1] = xy + wz;
    out[2] = xz - wy;
    out[4] = xy - wz;
    out[5] = 1.0f - (xx + zz);
    out[6] = yz + wx;
    out[8] = xz + wy;
    out[9] = yz - wx;
    out[10] = 1.0f - (xx + yy);
    out[15] = 1.0f;
}

/* animMatrix for (bone, tick): SkeletalSeq.updateAnimMatrix — every channel the
 * curve set does not drive falls back to the bind pose. */
static void
skb_build_anim_matrix(
    RSCache_SkeletalMat4 out,
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int t)
{
    int pose_id = maya->pose_id;
    struct RSCache_Dat2SkeletalBone* bone = &base->bones[bone_index];
    const struct RSCache_Dat2BoneCurves* bc = NULL;
    float rx, ry, rz, sx, sy, sz, tx, ty, tz;

    if( maya->bone_curves && bone_index < maya->bone_curve_count )
        bc = &maya->bone_curves[bone_index];

    if( pose_id < 0 || pose_id >= bone->pose_count )
        pose_id = 0;

    rx = bone->rotations[pose_id][0];
    ry = bone->rotations[pose_id][1];
    rz = bone->rotations[pose_id][2];
    sx = bone->scalings[pose_id][0];
    sy = bone->scalings[pose_id][1];
    sz = bone->scalings[pose_id][2];
    tx = bone->translations[pose_id][0];
    ty = bone->translations[pose_id][1];
    tz = bone->translations[pose_id][2];

    if( bc )
    {
        if( bc->curves[0] )
            rx = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[0], t);
        if( bc->curves[1] )
            ry = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[1], t);
        if( bc->curves[2] )
            rz = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[2], t);
        if( bc->curves[3] )
            tx = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[3], t);
        if( bc->curves[4] )
            ty = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[4], t);
        if( bc->curves[5] )
            tz = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[5], t);
        if( bc->curves[6] )
            sx = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[6], t);
        if( bc->curves[7] )
            sy = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[7], t);
        if( bc->curves[8] )
            sz = RSCache_Dat2AnimMayaCurveGetValue(bc->curves[8], t);
    }

    skb_mat4_identity(out);

    /* Rotation — SkeletalSeq.applyRotation composes qZ, then qX, then qY. */
    {
        float qi[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float qx[4], qy[4], qz[4], q[4], qtmp[4];
        RSCache_SkeletalMat4 rotm;

        skb_quat_axis_angle(qx, 1.0f, 0.0f, 0.0f, rx);
        skb_quat_axis_angle(qy, 0.0f, 1.0f, 0.0f, ry);
        skb_quat_axis_angle(qz, 0.0f, 0.0f, 1.0f, rz);
        skb_quat_mul(q, qz, qi);
        skb_quat_mul(qtmp, qx, q);
        skb_quat_mul(q, qy, qtmp);
        skb_mat4_from_quat(rotm, q);
        skb_mat4_mul(out, rotm, out);
    }

    /* Scale */
    {
        RSCache_SkeletalMat4 sm;
        memset(sm, 0, sizeof(sm));
        sm[0] = sx;
        sm[5] = sy;
        sm[10] = sz;
        sm[15] = 1.0f;
        skb_mat4_mul(out, sm, out);
    }

    /* Translation is set, not composed (SkeletalSeq.applyTranslation). */
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
}

/* animModelMatrix = parent.animModelMatrix * animMatrix, memoised per tick. */
static void
skb_compute_anim_model_matrix(
    RSCache_SkeletalMat4* anim_model_matrices,
    uint8_t* done,
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int t)
{
    struct RSCache_Dat2SkeletalBone* bone;
    RSCache_SkeletalMat4 anim_matrix;

    if( done[bone_index] )
        return;
    /* Mark before recursing: a cyclic parent chain in a corrupt rig would
     * otherwise recurse forever. */
    done[bone_index] = 1;

    bone = &base->bones[bone_index];
    skb_build_anim_matrix(anim_matrix, maya, base, bone_index, t);

    if( bone->parent == NULL )
    {
        skb_mat4_copy(anim_model_matrices[bone_index], anim_matrix);
    }
    else
    {
        int pi = (int)(bone->parent - base->bones);
        skb_compute_anim_model_matrix(anim_model_matrices, done, maya, base, pi, t);
        skb_mat4_mul(anim_model_matrices[bone_index], anim_model_matrices[pi], anim_matrix);
    }
}

float*
RSCache_Dat2SkeletalBaseBakePalette(
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int* frame_count_out,
    int* bone_count_out)
{
    int max_tick = 0;
    int found = 0;
    int frame_count;
    int bone_count;
    float* palette;
    RSCache_SkeletalMat4* anim_model_matrices;
    uint8_t* done;

    if( !maya || !base || base->bone_count <= 0 || !frame_count_out || !bone_count_out )
        return NULL;

    /* Frame f samples curve tick f (SkeletalSeq.updateAnimMatrix), so the
     * animation runs as long as its longest channel. */
    for( int b = 0; b < maya->bone_curve_count && maya->bone_curves; b++ )
    {
        const struct RSCache_Dat2BoneCurves* bc = &maya->bone_curves[b];
        for( int s = 0; s < 9; s++ )
        {
            const struct RSCache_Dat2Curve* c = bc->curves[s];
            if( !c || !c->values )
                continue;
            if( !found || c->end_tick > max_tick )
                max_tick = c->end_tick;
            found = 1;
        }
    }

    frame_count = found ? (max_tick + 1) : 1;
    bone_count = base->bone_count;

    palette = malloc((size_t)frame_count * (size_t)bone_count * 16 * sizeof(float));
    anim_model_matrices = malloc((size_t)bone_count * sizeof(RSCache_SkeletalMat4));
    done = malloc((size_t)bone_count);
    if( !palette || !anim_model_matrices || !done )
    {
        free(palette);
        free(anim_model_matrices);
        free(done);
        return NULL;
    }

    for( int f = 0; f < frame_count; f++ )
    {
        memset(done, 0, (size_t)bone_count);
        for( int b = 0; b < bone_count; b++ )
            skb_compute_anim_model_matrix(anim_model_matrices, done, maya, base, b, f);

        /* Skinning matrix = animModelMatrix * invertedModelMatrix(poseId). */
        for( int b = 0; b < bone_count; b++ )
        {
            const RSCache_SkeletalMat4* inv_mm =
                RSCache_Dat2SkeletalBoneInvertedModelMatrix(base, b, maya->pose_id);
            RSCache_SkeletalMat4 final_m;
            skb_mat4_mul(final_m, anim_model_matrices[b], *inv_mm);
            memcpy(&palette[(f * bone_count + b) * 16], final_m, 16 * sizeof(float));
        }
    }

    free(done);
    free(anim_model_matrices);

    *frame_count_out = frame_count;
    *bone_count_out = bone_count;
    return palette;
}
