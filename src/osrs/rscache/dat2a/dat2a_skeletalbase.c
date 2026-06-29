#include "dat2a_skeletalbase.h"
#include "dat2a_animaya.h"

#include "../dat2disk/dat2disk.h"
#include "../shared/shared_rs_buffer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small mat4 helpers (column-major) ---- */

static void
mat4_identity(SkeletalMat4 m)
{
    memset(m, 0, sizeof(SkeletalMat4));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void
mat4_copy(SkeletalMat4 dst, const SkeletalMat4 src)
{
    memcpy(dst, src, sizeof(SkeletalMat4));
}

static void
mat4_mul(SkeletalMat4 out, const SkeletalMat4 a, const SkeletalMat4 b)
{
    SkeletalMat4 tmp;
    for( int col = 0; col < 4; col++ )
        for( int row = 0; row < 4; row++ )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; k++ )
                s += a[k * 4 + row] * b[col * 4 + k];
            tmp[col * 4 + row] = s;
        }
    memcpy(out, tmp, sizeof(SkeletalMat4));
}

static int
mat4_invert(SkeletalMat4 out, const SkeletalMat4 m)
{
    float a00=m[0],a01=m[1],a02=m[2],a03=m[3];
    float a10=m[4],a11=m[5],a12=m[6],a13=m[7];
    float a20=m[8],a21=m[9],a22=m[10],a23=m[11];
    float a30=m[12],a31=m[13],a32=m[14],a33=m[15];
    float b00=a00*a11-a01*a10, b01=a00*a12-a02*a10, b02=a00*a13-a03*a10;
    float b03=a01*a12-a02*a11, b04=a01*a13-a03*a11, b05=a02*a13-a03*a12;
    float b06=a20*a31-a21*a30, b07=a20*a32-a22*a30, b08=a20*a33-a23*a30;
    float b09=a21*a32-a22*a31, b10=a21*a33-a23*a31, b11=a22*a33-a23*a32;
    float det=b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
    if( det == 0.0f ) { mat4_identity(out); return 1; }
    float inv=1.0f/det;
    out[0] =(a11*b11-a12*b10+a13*b09)*inv;
    out[1] =(a02*b10-a01*b11-a03*b09)*inv;
    out[2] =(a31*b05-a32*b04+a33*b03)*inv;
    out[3] =(a22*b04-a21*b05-a23*b03)*inv;
    out[4] =(a12*b08-a10*b11-a13*b07)*inv;
    out[5] =(a00*b11-a02*b08+a03*b07)*inv;
    out[6] =(a32*b02-a30*b05-a33*b01)*inv;
    out[7] =(a20*b05-a22*b02+a23*b01)*inv;
    out[8] =(a10*b10-a11*b08+a13*b06)*inv;
    out[9] =(a01*b08-a00*b10-a03*b06)*inv;
    out[10]=(a30*b04-a31*b02+a33*b00)*inv;
    out[11]=(a21*b02-a20*b04-a23*b00)*inv;
    out[12]=(a11*b07-a10*b09-a12*b06)*inv;
    out[13]=(a00*b09-a01*b07+a02*b06)*inv;
    out[14]=(a31*b01-a30*b03-a32*b00)*inv;
    out[15]=(a20*b03-a21*b01+a22*b00)*inv;
    return 0;
}

/* Extract euler XYZ rotation from an inverted localMatrix (from SkeletalBone.getRotation) */
static void
mat4_get_rotation_xyz(SkeletalVec3 out, const SkeletalMat4 m)
{
    /* SkeletalBone.ts getRotation:
     *   out[0] = -asin(m[6])
     *   cosX = cos(out[0])
     *   if |cosX| > 0.005:
     *     out[1] = atan2(m[2], m[10])
     *     out[2] = atan2(m[4], m[5])
     *   else:
     *     if m[6] < 0: out[1] = atan2(m[1], m[0])
     *     else:        out[1] = -atan2(m[1], m[0])
     *     out[2] = 0
     */
    out[0] = -asinf(fmaxf(-1.0f, fminf(1.0f, m[6])));
    float cosX = cosf(out[0]);
    if( fabsf(cosX) > 0.005f )
    {
        out[1] = atan2f(m[2], m[10]);
        out[2] = atan2f(m[4], m[5]);
    }
    else
    {
        float sinY = m[1], cosY = m[0];
        out[1] = (m[6] < 0.0f) ? atan2f(sinY, cosY) : -atan2f(sinY, cosY);
        out[2] = 0.0f;
    }
}

static void
mat4_get_translation(SkeletalVec3 out, const SkeletalMat4 m)
{
    out[0] = m[12]; out[1] = m[13]; out[2] = m[14];
}

static void
mat4_get_scaling(SkeletalVec3 out, const SkeletalMat4 m)
{
    out[0] = sqrtf(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);
    out[1] = sqrtf(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]);
    out[2] = sqrtf(m[8]*m[8]+m[9]*m[9]+m[10]*m[10]);
}

/* ---- extract_transformations ---- */

static void
bone_extract_transformations(struct RSCacheDat2A_SkeletalBone* bone)
{
    int pc = bone->pose_count;
    bone->rotations    = malloc((size_t)pc * sizeof(SkeletalVec3));
    bone->translations = malloc((size_t)pc * sizeof(SkeletalVec3));
    bone->scalings     = malloc((size_t)pc * sizeof(SkeletalVec3));

    SkeletalMat4 inv;
    for( int i = 0; i < pc; i++ )
    {
        mat4_invert(inv, bone->local_matrices[i]);
        mat4_get_rotation_xyz(bone->rotations[i], inv);
        mat4_get_translation(bone->translations[i], bone->local_matrices[i]);
        mat4_get_scaling(bone->scalings[i], bone->local_matrices[i]);
    }
}

/* ---- model matrix (lazy, recursive) ---- */

const SkeletalMat4*
RSCacheDat2A_SkeletalBoneGetModelMatrix(
    struct RSCacheDat2A_SkeletalBase* base,
    int bone_index,
    int pose_id)
{
    struct RSCacheDat2A_SkeletalBone* bone = &base->bones[bone_index];
    SkeletalMat4* mm = &bone->model_matrices[pose_id];
    /* Check if already computed (non-zero determinant marker: mm[15] != 0) */
    /* We use a separate computed flag array to avoid fragile checks. */
    /* Since we zero the arrays at alloc and identity has mm[15]=1, we check
     * the convention: uncomputed = all zero (not a valid transform).
     * Better: track a flag per bone per pose.  Here we use the fact that a
     * zero matrix is never a valid model matrix: we set mm[15] after compute. */
    if( (*mm)[15] == 0.0f )
    {
        if( bone->parent == NULL )
        {
            mat4_copy(*mm, bone->local_matrices[pose_id]);
        }
        else
        {
            int pi = (int)(bone->parent - base->bones);
            const SkeletalMat4* parent_mm =
                RSCacheDat2A_SkeletalBoneGetModelMatrix(base, pi, pose_id);
            mat4_mul(*mm, *parent_mm, bone->local_matrices[pose_id]);
        }
    }
    return (const SkeletalMat4*)mm;
}

const SkeletalMat4*
RSCacheDat2A_SkeletalBoneGetInvertedModelMatrix(
    struct RSCacheDat2A_SkeletalBase* base,
    int bone_index,
    int pose_id)
{
    struct RSCacheDat2A_SkeletalBone* bone = &base->bones[bone_index];
    SkeletalMat4* imm = &bone->inverted_model_matrices[pose_id];
    if( (*imm)[15] == 0.0f )
    {
        const SkeletalMat4* mm =
            RSCacheDat2A_SkeletalBoneGetModelMatrix(base, bone_index, pose_id);
        mat4_invert(*imm, *mm);
    }
    return (const SkeletalMat4*)imm;
}

/* ---- free ---- */

void
RSCacheDat2A_SkeletalBaseFree(struct RSCacheDat2A_SkeletalBase* base)
{
    if( !base ) return;
    if( base->bones )
    {
        for( int i = 0; i < base->bone_count; i++ )
        {
            struct RSCacheDat2A_SkeletalBone* b = &base->bones[i];
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

/* ---- decode ---- */

struct RSCacheDat2A_SkeletalBase*
RSCacheDat2A_SkeletalBaseNewFromCache(
    struct RSCacheDat2Disk* cache,
    int base_id)
{
    /* idx1 stores one file per SeqBase (archive = base_id, file 0) */
    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Skeletons, base_id);
    if( !archive )
        return NULL;

    /* idx1 archives are single-file (no multi-file splitting in the base Skeletons table).
     * archive->data is the raw decompressed SeqBase bytes. */
    const char* file_data = archive->data;
    int         file_size = archive->data_size;

    if( !file_data || file_size < 1 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    struct RSCacheShared_RSBuffer buf = {
        .data     = (uint8_t*)file_data,
        .size     = (uint32_t)file_size,
        .position = 0,
    };

    /* --- Dat2SeqBase classic fields (Dat2SeqBase.load in docs/seq/SeqBase.ts) ---
     *   u8  count
     *   count x u8  type
     *   count x u8  labelLen        (all lengths before data in dat2 format)
     *   sum(labelLen) x u8 label data
     *
     * Note: transformActor (u8, rev >= 481) and mask (u16, rev >= 530) fields only
     * exist in RS3 caches (game === "runescape"), not in OSRS caches.
     */
    int count = g1(&buf);

    /* types */
    for( int i = 0; i < count; i++ ) g1(&buf);

    /* label lengths: all lengths come first in dat2 format */
    int label_total = 0;
    for( int i = 0; i < count; i++ )
        label_total += g1(&buf);

    /* label data */
    for( int i = 0; i < label_total; i++ )
        g1(&buf);

    /* --- SkeletalBase tail --- */
    if( buf.position >= buf.size )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    int bone_count = g2(&buf);
    if( bone_count <= 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    int pose_count = g1(&buf);
    if( pose_count <= 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    struct RSCacheDat2A_SkeletalBase* base =
        calloc(1, sizeof(struct RSCacheDat2A_SkeletalBase));
    if( !base ) { RSCacheDat2Disk_ArchiveFree(archive); return NULL; }

    base->bone_count = bone_count;
    base->pose_count = pose_count;
    base->bones = calloc((size_t)bone_count, sizeof(struct RSCacheDat2A_SkeletalBone));
    if( !base->bones ) { RSCacheDat2Disk_ArchiveFree(archive); RSCacheDat2A_SkeletalBaseFree(base); return NULL; }

    for( int bi = 0; bi < bone_count; bi++ )
    {
        struct RSCacheDat2A_SkeletalBone* bone = &base->bones[bi];
        bone->pose_count = pose_count;
        bone->parent_id  = g2b(&buf); /* signed short parentId */

        bone->local_matrices           = calloc((size_t)pose_count, sizeof(SkeletalMat4));
        bone->model_matrices           = calloc((size_t)pose_count, sizeof(SkeletalMat4));
        bone->inverted_model_matrices  = calloc((size_t)pose_count, sizeof(SkeletalMat4));

        for( int p = 0; p < pose_count; p++ )
        {
            float* lm = bone->local_matrices[p];
            for( int e = 0; e < 16; e++ )
                lm[e] = gf(&buf);
            /* 3 unused floats */
            gf(&buf); gf(&buf); gf(&buf);
        }
        bone_extract_transformations(bone);
    }

    /* Link parent pointers */
    for( int bi = 0; bi < bone_count; bi++ )
    {
        int pid = base->bones[bi].parent_id;
        if( pid >= 0 && pid < bone_count && pid != bi )
            base->bones[bi].parent = &base->bones[pid];
        else
            base->bones[bi].parent = NULL;
    }

    RSCacheDat2Disk_ArchiveFree(archive);
    return base;
}

/* ---- Bake palette ---- */

/* Quaternion helpers (local copies; no dependency on toridraw_mat4) */
static void
quat_set_axis_angle(float q[4], float ax, float ay, float az, float angle)
{
    float h = angle * 0.5f, s = sinf(h);
    q[0] = ax*s; q[1] = ay*s; q[2] = az*s; q[3] = cosf(h);
}

static void
quat_mul(float out[4], const float a[4], const float b[4])
{
    float ax=a[0],ay=a[1],az=a[2],aw=a[3];
    float bx=b[0],by=b[1],bz=b[2],bw=b[3];
    out[0]=ax*bw+aw*bx+ay*bz-az*by;
    out[1]=ay*bw+aw*by+az*bx-ax*bz;
    out[2]=az*bw+aw*bz+ax*by-ay*bx;
    out[3]=aw*bw-ax*bx-ay*by-az*bz;
}

static void
mat4_from_quat(SkeletalMat4 out, const float q[4])
{
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float x2=x+x,y2=y+y,z2=z+z;
    float xx=x*x2,xy=x*y2,xz=x*z2,yy=y*y2,yz=y*z2,zz=z*z2;
    float wx=w*x2,wy=w*y2,wz=w*z2;
    memset(out, 0, sizeof(SkeletalMat4));
    out[0] =1.0f-(yy+zz); out[1] =xy+wz;  out[2] =xz-wy;
    out[4] =xy-wz;         out[5] =1.0f-(xx+zz); out[6] =yz+wx;
    out[8] =xz+wy;         out[9] =yz-wx;  out[10]=1.0f-(xx+yy);
    out[15]=1.0f;
}

static void
mat4_from_scaling_local(SkeletalMat4 out, float sx, float sy, float sz)
{
    memset(out,0,sizeof(SkeletalMat4));
    out[0]=sx; out[5]=sy; out[10]=sz; out[15]=1.0f;
}

static void
mat4_set_translation(SkeletalMat4 out, float tx, float ty, float tz)
{
    out[12]=tx; out[13]=ty; out[14]=tz;
}

/**
 * Build animMatrix for bone b at tick t, using bone_curves + bind-pose defaults.
 * Port of SkeletalSeq.updateAnimMatrix / applyRotation / applyScaling / applyTranslation.
 */
static void
build_anim_matrix(
    SkeletalMat4 out,
    const struct RSCacheDat2A_AnimMaya* maya,
    struct RSCacheDat2A_SkeletalBase*   base,
    int bone_index,
    int t)
{
    int pose_id    = maya->pose_id;
    struct RSCacheDat2A_SkeletalBone* bone = &base->bones[bone_index];
    const struct RSCacheDat2A_BoneCurves* bc = NULL;
    if( maya->bone_curves && bone_index < maya->bone_curve_count )
        bc = &maya->bone_curves[bone_index];

    /* Rotation defaults from bind pose */
    float rx = bone->rotations[pose_id][0];
    float ry = bone->rotations[pose_id][1];
    float rz = bone->rotations[pose_id][2];
    if( bc )
    {
        if( bc->curves[0] ) rx = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[0], t);
        if( bc->curves[1] ) ry = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[1], t);
        if( bc->curves[2] ) rz = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[2], t);
    }

    /* Scale defaults */
    float sx = bone->scalings[pose_id][0];
    float sy = bone->scalings[pose_id][1];
    float sz = bone->scalings[pose_id][2];
    if( bc )
    {
        if( bc->curves[6] ) sx = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[6], t);
        if( bc->curves[7] ) sy = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[7], t);
        if( bc->curves[8] ) sz = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[8], t);
    }

    /* Translation defaults */
    float tx = bone->translations[pose_id][0];
    float ty = bone->translations[pose_id][1];
    float tz = bone->translations[pose_id][2];
    if( bc )
    {
        if( bc->curves[3] ) tx = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[3], t);
        if( bc->curves[4] ) ty = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[4], t);
        if( bc->curves[5] ) tz = RSCacheDat2A_AnimMayaCurveGetValue(bc->curves[5], t);
    }

    /* Build: rot(z)*rot(x)*rot(y) * scale * translate, as in SkeletalSeq.ts */
    SkeletalMat4 anim;
    mat4_identity(anim);

    /* Rotation — SkeletalSeq.applyRotation: qZ * qX * qY order */
    {
        float qx[4], qy[4], qz[4], q[4], qtmp[4], rotm[16];
        quat_set_axis_angle(qx, 1,0,0, rx);
        quat_set_axis_angle(qy, 0,1,0, ry);
        quat_set_axis_angle(qz, 0,0,1, rz);
        /* quaternion = identity, then mul(qZ), mul(qX), mul(qY) */
        float qi[4] = {0,0,0,1};
        quat_mul(q, qz, qi);
        quat_mul(qtmp, qx, q);
        quat_mul(q, qy, qtmp);
        mat4_from_quat(rotm, q);
        mat4_mul(anim, rotm, anim);
    }

    /* Scale */
    {
        SkeletalMat4 sm;
        mat4_from_scaling_local(sm, sx, sy, sz);
        mat4_mul(anim, sm, anim);
    }

    /* Translation — set columns 12-14 directly (like SkeletalSeq.applyTranslation) */
    mat4_set_translation(anim, tx, ty, tz);

    mat4_copy(out, anim);
}

/* Recursive animModelMatrix: parent.animModelMatrix * animMatrix */
static void
compute_anim_model_matrix(
    SkeletalMat4* anim_model_matrices,
    uint8_t* done,
    const struct RSCacheDat2A_AnimMaya* maya,
    struct RSCacheDat2A_SkeletalBase* base,
    int bone_index,
    int t)
{
    if( done[bone_index] )
        return;

    struct RSCacheDat2A_SkeletalBone* bone = &base->bones[bone_index];
    SkeletalMat4 anim_matrix;
    build_anim_matrix(anim_matrix, maya, base, bone_index, t);

    if( bone->parent == NULL )
    {
        mat4_copy(anim_model_matrices[bone_index], anim_matrix);
    }
    else
    {
        int pi = (int)(bone->parent - base->bones);
        compute_anim_model_matrix(anim_model_matrices, done, maya, base, pi, t);
        mat4_mul(anim_model_matrices[bone_index], anim_model_matrices[pi], anim_matrix);
    }

    done[bone_index] = 1;
}

float*
RSCacheDat2A_SkeletalBaseBakePalette(
    const struct RSCacheDat2A_AnimMaya* maya,
    struct RSCacheDat2A_SkeletalBase*   base,
    int*                                frame_count_out,
    int*                                bone_count_out)
{
    if( !maya || !base || base->bone_count <= 0 ) return NULL;

    /* Animation frame f maps to curve tick f (rs-map-viewer SkeletalSeq.updateAnimMatrix). */
    int max_tick = 0;
    int found = 0;
    for( int b = 0; b < maya->bone_curve_count && b < base->bone_count; b++ )
    {
        if( !maya->bone_curves ) break;
        const struct RSCacheDat2A_BoneCurves* bc = &maya->bone_curves[b];
        for( int s = 0; s < 9; s++ )
        {
            const struct RSCacheDat2A_Curve* c = bc->curves[s];
            if( !c || !c->values ) continue;
            if( !found ) { max_tick = c->end_tick; found = 1; }
            else if( c->end_tick > max_tick )
                max_tick = c->end_tick;
        }
    }

    int frame_count = found ? (max_tick + 1) : 1;
    int bone_count  = base->bone_count;

    float* palette = malloc((size_t)frame_count * (size_t)bone_count * 16 * sizeof(float));
    if( !palette ) return NULL;

    SkeletalMat4* anim_model_matrices =
        malloc((size_t)bone_count * sizeof(SkeletalMat4));
    if( !anim_model_matrices ) { free(palette); return NULL; }

    uint8_t* done = malloc((size_t)bone_count);
    if( !done ) { free(anim_model_matrices); free(palette); return NULL; }

    for( int f = 0; f < frame_count; f++ )
    {
        int t = f;

        memset(done, 0, (size_t)bone_count);

        for( int b = 0; b < bone_count; b++ )
            compute_anim_model_matrix(anim_model_matrices, done, maya, base, b, t);

        /* finalMatrix = animModelMatrix * invertedModelMatrix(poseId) */
        for( int b = 0; b < bone_count; b++ )
        {
            const SkeletalMat4* inv_mm =
                RSCacheDat2A_SkeletalBoneGetInvertedModelMatrix(base, b, maya->pose_id);
            SkeletalMat4 final_m;
            mat4_mul(final_m, anim_model_matrices[b], *inv_mm);
            float* dst = &palette[(f * bone_count + b) * 16];
            memcpy(dst, final_m, 16 * sizeof(float));
        }
    }

    free(done);
    free(anim_model_matrices);

    *frame_count_out = frame_count;
    *bone_count_out  = bone_count;
    return palette;
}
