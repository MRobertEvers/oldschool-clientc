#include "ev_wire.h"
#include <assert.h>

#include "toridraw.h"

/* The full ToriDraw_TexMapping; toridraw_types.h only forward-declares it, so
 * the HD tail below cannot be written or read without this. */
#include "graphics/raster/texture/texmap_common.h"

#include <stdlib.h>
#include <string.h>

/* ---- writing ------------------------------------------------------------ */

static int
buf_reserve(struct EV_WireBuf* b, size_t extra)
{
    if( b->len + extra <= b->cap )
        return 1;
    size_t next = b->cap ? b->cap : 4096;
    while( next < b->len + extra )
        next *= 2;
    uint8_t* grown = realloc(b->data, next);
    assert(grown);
    b->data = grown;
    b->cap = next;
    return 1;
}

static int
put_i32(struct EV_WireBuf* b, int32_t v)
{
    if( !buf_reserve(b, 4) )
        return 0;
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFF);
    return 1;
}

/* A float as its raw bits. The skinning palette is 16 floats per bone per
 * frame and quantising it would show up as a pose that drifts. */
static int
put_f32(struct EV_WireBuf* b, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return put_i32(b, (int32_t)bits);
}

/*
 * An optional array: a presence flag then the elements.
 *
 * The flag is not redundant with a zero count. A model with face_alphas == NULL
 * is fully opaque and the raster takes a different path than one with an array
 * of zeroes, so "absent" and "empty" have to survive the trip.
 */
#define PUT_OPT(buf, ptr, count, expr)                                                             \
    do                                                                                             \
    {                                                                                              \
        if( !put_i32((buf), (ptr) ? 1 : 0) )                                                       \
            return 0;                                                                              \
        if( ptr )                                                                                  \
            for( int i_ = 0; i_ < (count); i_++ )                                                  \
                if( !put_i32((buf), (int32_t)(expr)) )                                             \
                    return 0;                                                                      \
    } while( 0 )

void
ev_wire_free(struct EV_WireBuf* buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int
write_bones(struct EV_WireBuf* out, const struct ToriDraw_Bones* bones)
{
    if( !put_i32(out, bones ? 1 : 0) )
        return 0;
    if( !bones )
        return 1;
    if( !put_i32(out, bones->bones_count) )
        return 0;
    for( int i = 0; i < bones->bones_count; i++ )
    {
        int size = bones->bones_sizes ? (int)bones->bones_sizes[i] : 0;
        if( !put_i32(out, size) )
            return 0;
        for( int j = 0; j < size; j++ )
            if( !put_i32(out, (int32_t)bones->bones[i][j]) )
                return 0;
    }
    return 1;
}

int
ev_wire_write_model(
    struct EV_WireBuf* out,
    const struct ToriDraw_Model* m)
{
    assert(m);

    if( !put_i32(out, (int32_t)EV_WIRE_MODEL_MAGIC) )
        return 0;
    if( !put_i32(out, m->flags) || !put_i32(out, m->vertex_count) ||
        !put_i32(out, m->face_count) || !put_i32(out, m->model_priority) )
        return 0;

    for( int i = 0; i < m->vertex_count; i++ )
        if( !put_i32(out, m->vertices_x[i]) || !put_i32(out, m->vertices_y[i]) ||
            !put_i32(out, m->vertices_z[i]) )
            return 0;

    for( int i = 0; i < m->face_count; i++ )
        if( !put_i32(out, m->face_indices_a[i]) || !put_i32(out, m->face_indices_b[i]) ||
            !put_i32(out, m->face_indices_c[i]) )
            return 0;

    PUT_OPT(out, m->face_colors, m->face_count, m->face_colors[i_]);
    PUT_OPT(out, m->face_alphas, m->face_count, m->face_alphas[i_]);
    PUT_OPT(out, m->face_infos, m->face_count, m->face_infos[i_]);
    PUT_OPT(out, m->face_textures, m->face_count, m->face_textures[i_]);
    /* Two 4-bit priorities per byte, so the array is half as long as the rest. */
    PUT_OPT(out, m->face_priorities, (m->face_count + 1) / 2, m->face_priorities[i_]);

    /* Lit colours. A model that has been through ToriDraw_LightModelScene has
     * per-corner colours and no longer needs normals or the light model in the
     * browser, which is why lighting happens on the server. */
    PUT_OPT(out, m->face_colors_a, m->face_count, m->face_colors_a[i_]);
    PUT_OPT(out, m->face_colors_b, m->face_count, m->face_colors_b[i_]);
    PUT_OPT(out, m->face_colors_c, m->face_count, m->face_colors_c[i_]);

    if( !put_i32(out, m->textured_face_count) )
        return 0;
    PUT_OPT(out, m->textured_p_coordinate, m->textured_face_count, m->textured_p_coordinate[i_]);
    PUT_OPT(out, m->textured_m_coordinate, m->textured_face_count, m->textured_m_coordinate[i_]);
    PUT_OPT(out, m->textured_n_coordinate, m->textured_face_count, m->textured_n_coordinate[i_]);
    /* Which projection each textured face group uses. Without it every face
     * reads as render type 0 and the HD path draws a cube map as a plane. */
    PUT_OPT(out, m->texture_render_types, m->textured_face_count, m->texture_render_types[i_]);
    PUT_OPT(out, m->face_texture_coords, m->face_count, m->face_texture_coords[i_]);

    /* The rig binding. Without it the model has no response to a frame at all. */
    if( !write_bones(out, m->vertex_bones) )
        return 0;
    if( !write_bones(out, m->face_bones) )
        return 0;

    /*
     * The Animaya skin: per-vertex bone influences and their weights.
     *
     * Classic frame animation does not use it, which is why it was missing here
     * — and why skeletal sequences posed nothing in the browser. A model
     * without it is not mis-animated by ToriDraw_ModelAnimateSkeletal, it is
     * simply left in its bind pose, so the failure looked like a stuck
     * animation rather than a dropped field.
     */
    if( !put_i32(out, m->animaya_vertex_count) )
        return 0;
    for( int i = 0; i < m->animaya_vertex_count; i++ )
    {
        int cnt = m->animaya_group_counts ? m->animaya_group_counts[i] : 0;
        if( !put_i32(out, cnt) )
            return 0;
        for( int j = 0; j < cnt; j++ )
            if( !put_i32(out, m->animaya_groups[i][j]) ||
                !put_i32(out, m->animaya_scales[i][j]) )
                return 0;
    }

    return 1;
}

/*
 * An HD model: the HD magic, a complete base blob, then the mapping tail.
 *
 * Nesting a whole EVM1 blob rather than interleaving the fields means the base
 * writer stays the single definition of what a model is — an HD blob read by
 * the base reader (past the first 4 bytes) is byte-for-byte an ordinary model.
 */
int
ev_wire_write_model_hd(
    struct EV_WireBuf* out,
    const struct ToriDraw_ModelHD* model)
{
    assert(model);

    if( !put_i32(out, (int32_t)EV_WIRE_MODEL_HD_MAGIC) )
        return 0;

    /*
     * The tail's byte length, written BEFORE the base blob so the reader can
     * find the tail without knowing how long the base is.
     *
     * The obvious alternative — count the tail's fields and index back from the
     * end — is what this replaces, and it was wrong on the first attempt: the
     * per-mapping record is 20 words and the count said 19, so the presence
     * flag was read from the middle of a matrix and every mapping was silently
     * dropped. A length the writer computes cannot disagree with itself.
     */
    int count = model->base.textured_face_count;
    int has = model->texture_mappings && count > 0;
    int32_t tail_bytes = 4 + (has ? count * (int32_t)EV_WIRE_HD_MAPPING_WORDS * 4 : 0);
    if( !put_i32(out, tail_bytes) )
        return 0;

    if( !ev_wire_write_model(out, &model->base) )
        return 0;

    if( !put_i32(out, has ? 1 : 0) )
        return 0;
    if( !has )
        return 1;

    for( int i = 0; i < count; i++ )
    {
        const struct ToriDraw_TexMapping* m = &model->texture_mappings[i];
        if( !put_i32(out, m->centre_x) || !put_i32(out, m->centre_y) ||
            !put_i32(out, m->centre_z) )
            return 0;
        for( int k = 0; k < 9; k++ )
            if( !put_f32(out, m->matrix[k]) )
                return 0;
        if( !put_i32(out, m->direction) )
            return 0;
        if( !put_f32(out, m->speed) || !put_f32(out, m->u_offset) ||
            !put_f32(out, m->v_offset) || !put_f32(out, m->scale_z) ||
            !put_f32(out, m->axis_scale_x) || !put_f32(out, m->axis_scale_y) ||
            !put_f32(out, m->axis_scale_z) )
            return 0;
    }
    return 1;
}

int
ev_wire_write_anim(
    struct EV_WireBuf* out,
    const struct ToriDraw_Animation* a)
{
    if( !a || (!a->base && !a->skeletal) )
        return 0;

    if( !put_i32(out, (int32_t)EV_WIRE_ANIM_MAGIC) )
        return 0;
    if( !put_i32(out, a->frame_count) || !put_i32(out, a->frame_step) )
        return 0;

    /*
     * Which kind of animation this is. The two are exclusive in a
     * ToriDraw_Animation — a skeletal one has no base and no frames — so the
     * flag chooses which of the two bodies follows.
     */
    if( !put_i32(out, a->skeletal ? 1 : 0) )
        return 0;
    if( a->skeletal )
    {
        const struct ToriDraw_SkeletalAnim* sk = a->skeletal;
        if( !put_i32(out, sk->id) || !put_i32(out, sk->bone_count) ||
            !put_i32(out, sk->frame_count) )
            return 0;
        size_t floats = (size_t)sk->frame_count * (size_t)sk->bone_count * 16;
        for( size_t i = 0; i < floats; i++ )
            if( !put_f32(out, sk->matrices[i]) )
                return 0;
        return 1;
    }

    const struct ToriDraw_AnimBase* base = a->base;
    if( !put_i32(out, base->length) )
        return 0;
    for( int i = 0; i < base->length; i++ )
    {
        int len = base->bone_group_lengths ? base->bone_group_lengths[i] : 0;
        if( !put_i32(out, base->types ? base->types[i] : 0) || !put_i32(out, len) )
            return 0;
        for( int j = 0; j < len; j++ )
            if( !put_i32(out, base->bone_groups[i][j]) )
                return 0;
    }

    for( int f = 0; f < a->frame_count; f++ )
    {
        const struct ToriDraw_AnimFrame* fr = &a->frames[f];
        if( !put_i32(out, fr->id) || !put_i32(out, fr->length) || !put_i32(out, fr->delay) )
            return 0;
        for( int i = 0; i < fr->length; i++ )
            if( !put_i32(out, fr->groups[i]) || !put_i32(out, fr->x[i]) ||
                !put_i32(out, fr->y[i]) || !put_i32(out, fr->z[i]) )
                return 0;
    }

    return 1;
}

/* ---- reading ------------------------------------------------------------ */

/*
 * A bounds-checked cursor.
 *
 * Every read goes through it and sets `bad` on overrun rather than returning a
 * sentinel, so a truncated blob fails once at the end instead of quietly
 * producing a model with plausible-looking garbage in its later arrays.
 */
struct Cur
{
    const uint8_t* p;
    size_t left;
    int bad;
};

static float
get_f32(struct Cur* c);

static int32_t
get_i32(struct Cur* c)
{
    if( c->left < 4 )
    {
        c->bad = 1;
        return 0;
    }
    int32_t v = (int32_t)((uint32_t)c->p[0] | ((uint32_t)c->p[1] << 8) |
                          ((uint32_t)c->p[2] << 16) | ((uint32_t)c->p[3] << 24));
    c->p += 4;
    c->left -= 4;
    return v;
}

static float
get_f32(struct Cur* c)
{
    uint32_t bits = (uint32_t)get_i32(c);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

#define READ_OPT(cur, field, count, type)                                                          \
    do                                                                                             \
    {                                                                                              \
        int present_ = get_i32(cur);                                                               \
        if( (cur)->bad )                                                                           \
            goto fail;                                                                             \
        if( present_ )                                                                             \
        {                                                                                          \
            int n_ = (count);                                                                      \
            field = calloc((size_t)(n_ > 0 ? n_ : 1), sizeof(type));                               \
            if( !field )                                                                           \
                goto fail;                                                                         \
            for( int i_ = 0; i_ < n_; i_++ )                                                       \
                field[i_] = (type)get_i32(cur);                                                    \
            if( (cur)->bad )                                                                       \
                goto fail;                                                                         \
        }                                                                                          \
    } while( 0 )

static struct ToriDraw_Bones*
read_bones(struct Cur* c)
{
    int present = get_i32(c);
    if( c->bad || !present )
        return NULL;

    struct ToriDraw_Bones* bones = calloc(1, sizeof(*bones));
    assert(bones);

    bones->bones_count = get_i32(c);
    if( c->bad || bones->bones_count < 0 )
    {
        free(bones);
        return NULL;
    }

    int n = bones->bones_count > 0 ? bones->bones_count : 1;
    bones->bones = calloc((size_t)n, sizeof(boneint_t*));
    bones->bones_sizes = calloc((size_t)n, sizeof(boneint_t));
    assert(bones->bones);
    assert(bones->bones_sizes);

    for( int i = 0; i < bones->bones_count; i++ )
    {
        int size = get_i32(c);
        if( c->bad || size < 0 )
        {
            ToriDraw_BonesFree(bones);
            return NULL;
        }
        bones->bones_sizes[i] = (boneint_t)size;
        bones->bones[i] = calloc((size_t)(size > 0 ? size : 1), sizeof(boneint_t));
        assert(bones->bones[i]);
        for( int j = 0; j < size; j++ )
            bones->bones[i][j] = (boneint_t)get_i32(c);
    }

    if( c->bad )
    {
        ToriDraw_BonesFree(bones);
        return NULL;
    }
    return bones;
}

struct ToriDraw_Model*
ev_wire_read_model(
    const uint8_t* data,
    size_t len)
{
    struct Cur cur = { data, len, 0 };
    struct ToriDraw_Model* m = NULL;

    if( (uint32_t)get_i32(&cur) != EV_WIRE_MODEL_MAGIC || cur.bad )
        return NULL;

    int flags = get_i32(&cur);
    int vertex_count = get_i32(&cur);
    int face_count = get_i32(&cur);
    int model_priority = get_i32(&cur);
    if( cur.bad || vertex_count < 0 || face_count < 0 )
        return NULL;

    m = ToriDraw_ModelNew(vertex_count, face_count, (uint8_t)flags);
    if( !m )
        return NULL;
    m->model_priority = (uint8_t)model_priority;

    m->vertices_x = calloc((size_t)(vertex_count > 0 ? vertex_count : 1), sizeof(vertexint_t));
    m->vertices_y = calloc((size_t)(vertex_count > 0 ? vertex_count : 1), sizeof(vertexint_t));
    m->vertices_z = calloc((size_t)(vertex_count > 0 ? vertex_count : 1), sizeof(vertexint_t));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    for( int i = 0; i < vertex_count; i++ )
    {
        m->vertices_x[i] = (vertexint_t)get_i32(&cur);
        m->vertices_y[i] = (vertexint_t)get_i32(&cur);
        m->vertices_z[i] = (vertexint_t)get_i32(&cur);
    }
    if( cur.bad )
        goto fail;

    m->face_indices_a = calloc((size_t)(face_count > 0 ? face_count : 1), sizeof(faceint_t));
    m->face_indices_b = calloc((size_t)(face_count > 0 ? face_count : 1), sizeof(faceint_t));
    m->face_indices_c = calloc((size_t)(face_count > 0 ? face_count : 1), sizeof(faceint_t));
    assert(m->face_indices_a);
    assert(m->face_indices_b);
    assert(m->face_indices_c);
    for( int i = 0; i < face_count; i++ )
    {
        m->face_indices_a[i] = (faceint_t)get_i32(&cur);
        m->face_indices_b[i] = (faceint_t)get_i32(&cur);
        m->face_indices_c[i] = (faceint_t)get_i32(&cur);
    }
    if( cur.bad )
        goto fail;

    READ_OPT(&cur, m->face_colors, face_count, hsl16_t);
    READ_OPT(&cur, m->face_alphas, face_count, alphaint_t);
    READ_OPT(&cur, m->face_infos, face_count, int);
    READ_OPT(&cur, m->face_textures, face_count, faceint_t);
    READ_OPT(&cur, m->face_priorities, (face_count + 1) / 2, uint8_t);
    READ_OPT(&cur, m->face_colors_a, face_count, hsl16_t);
    READ_OPT(&cur, m->face_colors_b, face_count, hsl16_t);
    READ_OPT(&cur, m->face_colors_c, face_count, hsl16_t);

    m->textured_face_count = get_i32(&cur);
    if( cur.bad || m->textured_face_count < 0 )
        goto fail;
    READ_OPT(&cur, m->textured_p_coordinate, m->textured_face_count, faceint_t);
    READ_OPT(&cur, m->textured_m_coordinate, m->textured_face_count, faceint_t);
    READ_OPT(&cur, m->textured_n_coordinate, m->textured_face_count, faceint_t);
    READ_OPT(&cur, m->texture_render_types, m->textured_face_count, uint8_t);
    READ_OPT(&cur, m->face_texture_coords, face_count, faceint_t);

    m->vertex_bones = read_bones(&cur);
    m->face_bones = read_bones(&cur);
    if( cur.bad )
        goto fail;

    m->animaya_vertex_count = get_i32(&cur);
    if( cur.bad || m->animaya_vertex_count < 0 )
        goto fail;
    if( m->animaya_vertex_count > 0 )
    {
        int n = m->animaya_vertex_count;
        m->animaya_group_counts = calloc((size_t)n, 1);
        m->animaya_groups = calloc((size_t)n, sizeof(uint8_t*));
        m->animaya_scales = calloc((size_t)n, sizeof(uint8_t*));
        assert(m->animaya_group_counts);
        assert(m->animaya_groups);
        assert(m->animaya_scales);
        for( int i = 0; i < n; i++ )
        {
            int cnt = get_i32(&cur);
            if( cur.bad || cnt < 0 )
                goto fail;
            m->animaya_group_counts[i] = (uint8_t)cnt;
            if( cnt == 0 )
                continue;
            m->animaya_groups[i] = malloc((size_t)cnt);
            m->animaya_scales[i] = malloc((size_t)cnt);
            assert(m->animaya_groups[i]);
            assert(m->animaya_scales[i]);
            for( int j = 0; j < cnt; j++ )
            {
                m->animaya_groups[i][j] = (uint8_t)get_i32(&cur);
                m->animaya_scales[i][j] = (uint8_t)get_i32(&cur);
            }
        }
        if( cur.bad )
            goto fail;
    }

    /* The animator transforms `vertices_*` away from a pristine copy, so it
     * needs that copy taken before the first frame is applied. */
    ToriDraw_ModelCaptureOriginalVertices(m);

    /* And the projection needs a bounds cylinder: without one it returns
     * TORIDRAW_CULL_ERROR for every model, which reads as a blank canvas with
     * no error anywhere. Computed here rather than sent, because it is derived
     * from vertices the reader already has. */
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;

fail:
    ToriDraw_ModelFree(m);
    return NULL;
}

struct ToriDraw_Animation*
ev_wire_read_anim(
    const uint8_t* data,
    size_t len)
{
    struct Cur cur = { data, len, 0 };

    if( (uint32_t)get_i32(&cur) != EV_WIRE_ANIM_MAGIC || cur.bad )
        return NULL;

    struct ToriDraw_Animation* a = calloc(1, sizeof(*a));
    assert(a);

    a->frame_count = get_i32(&cur);
    a->frame_step = get_i32(&cur);
    if( cur.bad || a->frame_count < 0 )
        goto bad;

    a->replaceheldleft = -1;
    a->replaceheldright = -1;

    if( get_i32(&cur) )
    {
        struct ToriDraw_SkeletalAnim* sk = calloc(1, sizeof(*sk));
        assert(sk);
        a->skeletal = sk;
        sk->id = get_i32(&cur);
        sk->bone_count = get_i32(&cur);
        sk->frame_count = get_i32(&cur);
        if( cur.bad || sk->bone_count < 0 || sk->frame_count < 0 )
            goto bad;
        size_t floats = (size_t)sk->frame_count * (size_t)sk->bone_count * 16;
        if( floats > 0 )
        {
            sk->matrices = malloc(floats * sizeof(float));
            assert(sk->matrices);
            for( size_t i = 0; i < floats; i++ )
                sk->matrices[i] = get_f32(&cur);
            if( cur.bad )
                goto bad;
        }
        return a;
    }

    a->base = calloc(1, sizeof(*a->base));
    assert(a->base);
    a->base->length = get_i32(&cur);
    if( cur.bad || a->base->length < 0 )
        goto bad;

    {
        int n = a->base->length > 0 ? a->base->length : 1;
        a->base->types = calloc((size_t)n, sizeof(uint8_t));
        a->base->bone_group_lengths = calloc((size_t)n, sizeof(uint16_t));
        a->base->bone_groups = calloc((size_t)n, sizeof(uint8_t*));
        assert(a->base->types);
        assert(a->base->bone_group_lengths);
        assert(a->base->bone_groups);

        for( int i = 0; i < a->base->length; i++ )
        {
            a->base->types[i] = (uint8_t)get_i32(&cur);
            int glen = get_i32(&cur);
            if( cur.bad || glen < 0 )
                goto bad;
            a->base->bone_group_lengths[i] = (uint16_t)glen;
            a->base->bone_groups[i] = calloc((size_t)(glen > 0 ? glen : 1), sizeof(uint8_t));
            assert(a->base->bone_groups[i]);
            for( int j = 0; j < glen; j++ )
                a->base->bone_groups[i][j] = (uint8_t)get_i32(&cur);
        }
    }

    a->frames = calloc((size_t)(a->frame_count > 0 ? a->frame_count : 1), sizeof(*a->frames));
    assert(a->frames);

    for( int f = 0; f < a->frame_count; f++ )
    {
        struct ToriDraw_AnimFrame* fr = &a->frames[f];
        fr->id = get_i32(&cur);
        fr->length = get_i32(&cur);
        fr->delay = get_i32(&cur);
        if( cur.bad || fr->length < 0 )
            goto bad;

        int n = fr->length > 0 ? fr->length : 1;
        fr->groups = calloc((size_t)n, sizeof(int16_t));
        fr->x = calloc((size_t)n, sizeof(int16_t));
        fr->y = calloc((size_t)n, sizeof(int16_t));
        fr->z = calloc((size_t)n, sizeof(int16_t));
        assert(fr->groups);
        assert(fr->x);
        assert(fr->y);
        assert(fr->z);
        for( int i = 0; i < fr->length; i++ )
        {
            fr->groups[i] = (int16_t)get_i32(&cur);
            fr->x[i] = (int16_t)get_i32(&cur);
            fr->y[i] = (int16_t)get_i32(&cur);
            fr->z[i] = (int16_t)get_i32(&cur);
        }
    }

    if( cur.bad )
        goto bad;
    return a;

bad:
    ToriDraw_AnimationFree(a);
    return NULL;
}

/* ------------------------------------------------------------------- HD */

int
ev_wire_is_model_hd(const uint8_t* data, size_t len)
{
    if( len < 4 )
        return 0;
    assert(data);
    uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return magic == EV_WIRE_MODEL_HD_MAGIC;
}

/* ---- texture sets --------------------------------------------------------- */

static uint32_t
wire_u32_at(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int
ev_wire_texture_count(const uint8_t* data, size_t len)
{
    assert(data);
    if( len < 8 || wire_u32_at(data) != EV_WIRE_TEXTURES_MAGIC )
        return 0;
    return (int)wire_u32_at(data + 4);
}

/*
 * Walked from the start each time rather than indexed, because the entries are
 * variable-length (a 64x64 texture is a quarter the size of a 128x128) and the
 * format carries no offset table. Callers read the set once at load, so the
 * quadratic walk is over tens of items, not thousands.
 */
int
ev_wire_read_texture(const uint8_t* data, size_t len, int index, struct EV_WireTexture* out)
{
    int count = ev_wire_texture_count(data, len);
    size_t at = 8;

    if( index < 0 || index >= count )
        return 0;
    assert(out);

    for( int i = 0; i < count; i++ )
    {
        int id, size, opaque;
        size_t texel_bytes;

        if( at + 8 > len )
            return 0;
        id = (int)wire_u32_at(data + at);
        size = (int)data[at + 4] | ((int)data[at + 5] << 8);
        opaque = data[at + 6];
        at += 8;

        if( size <= 0 )
            return 0;
        texel_bytes = (size_t)size * (size_t)size * 4u;
        if( at + texel_bytes > len )
            return 0;

        if( i == index )
        {
            out->id = id;
            out->size = size;
            out->opaque = opaque;
            out->texels = (const uint32_t*)(const void*)(data + at);
            return 1;
        }
        at += texel_bytes;
    }
    return 0;
}

struct ToriDraw_ModelHD*
ev_wire_read_model_hd(const uint8_t* data, size_t len)
{
    if( !ev_wire_is_model_hd(data, len) )
        return NULL;

    /*
     * The base blob starts 4 bytes in and the base reader validates its own
     * magic, so a truncated or mismatched payload fails there rather than here.
     * It returns a heap ToriDraw_Model; the HD wrapper embeds its base by
     * value, so the contents move across and the original shell is released
     * without touching the arrays it handed over.
     */
    /* magic, then the tail length, then the base blob. */
    struct Cur head;
    head.p = data + 4;
    head.left = len - 4;
    head.bad = 0;
    int32_t tail_bytes = get_i32(&head);
    if( head.bad || tail_bytes < 4 || (size_t)tail_bytes > len )
        return NULL;

    struct ToriDraw_Model* base = ev_wire_read_model(data + 8, len - 8 - (size_t)tail_bytes);
    if( !base )
        return NULL;

    struct ToriDraw_ModelHD* hd =
        (struct ToriDraw_ModelHD*)calloc(1, sizeof(struct ToriDraw_ModelHD));
    if( !hd )
    {
        ToriDraw_ModelFree(base);
        return NULL;
    }
    hd->base = *base;
    free(base); /* the shell only; every array is now owned by hd->base */

    int count = hd->base.textured_face_count;
    if( len < (size_t)tail_bytes || tail_bytes < 4 )
    {
        ToriDraw_ModelHDFree(hd);
        return NULL;
    }

    struct Cur cur;
    cur.p = data + (len - (size_t)tail_bytes);
    cur.left = (size_t)tail_bytes;
    cur.bad = 0;

    int present = get_i32(&cur);
    if( cur.bad || !present || count <= 0 )
        return hd; /* a valid HD model that simply carries no mappings */

    hd->texture_mappings = (struct ToriDraw_TexMapping*)calloc(
        (size_t)count, sizeof(struct ToriDraw_TexMapping));
    if( !hd->texture_mappings )
    {
        ToriDraw_ModelHDFree(hd);
        return NULL;
    }

    for( int i = 0; i < count; i++ )
    {
        struct ToriDraw_TexMapping* m = &hd->texture_mappings[i];
        m->centre_x = get_i32(&cur);
        m->centre_y = get_i32(&cur);
        m->centre_z = get_i32(&cur);
        for( int k = 0; k < 9; k++ )
            m->matrix[k] = get_f32(&cur);
        m->direction = get_i32(&cur);
        m->speed = get_f32(&cur);
        m->u_offset = get_f32(&cur);
        m->v_offset = get_f32(&cur);
        m->scale_z = get_f32(&cur);
        m->axis_scale_x = get_f32(&cur);
        m->axis_scale_y = get_f32(&cur);
        m->axis_scale_z = get_f32(&cur);
    }

    if( cur.bad )
    {
        ToriDraw_ModelHDFree(hd);
        return NULL;
    }
    return hd;
}
