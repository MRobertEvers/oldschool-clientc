/*
 * The rev-241 client's world pass on D3D9, as runs (see the header). Shared by
 * both Windows lanes; how a run is drawn is each lane's.
 */

#include "platform/platform_win32_renderer_d3d9_rev241.h"

#include "mem/torirs_mem.h"
#include "perf/torirs_perf.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A blended face and its depth bucket, for the client's sorted order. */
struct D3D9Rev241FaceKey
{
    int bucket;
    int face;
};

/* Distinct bias values one model's runs can hold; faces past them draw
 * unbiased, in the buckets. */
#define D3D9_REV241_BIAS_GROUPS 8u
/* Distinct projectors one model's runs can hold. */
#define D3D9_REV241_PROJECTOR_GROUPS 16u

static struct D3D9Rev241World*
d3d9_rev241_state(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    assert(renderer->rev241);
    return renderer->rev241;
}

bool
d3d9_rev241_create(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    struct D3D9Rev241World* world;
    assert(renderer);
    assert(!renderer->rev241);
    d3d9_zbuffer_destroy(renderer);
    if( !d3d9_zbuffer_create(renderer) )
        return false;
    world = (struct D3D9Rev241World*)ToriMem_Calloc(ToriMem_Renderer(), 1u, sizeof(*world));
    assert(world);
    world->run_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    assert(world->run_chain);
    renderer->rev241 = world;
    return true;
}

void
d3d9_rev241_destroy(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    struct D3D9Rev241World* world;
    if( !renderer || !renderer->rev241 )
        return;
    world = renderer->rev241;
    if( world->projector_textures )
    {
        for( int i = 0; i < TORIDRAW_TEXTURE_ID_CAPACITY; i++ )
            if( world->projector_textures[i] )
                IDirect3DTexture9_Release(world->projector_textures[i]);
        ToriMem_Free(ToriMem_Renderer(), world->projector_textures);
    }
    for( uint32_t i = 0u; i < D3D9_REV241_LANE_OBJECTS; i++ )
        if( world->lane_objects[i] )
            IUnknown_Release(world->lane_objects[i]);
    if( world->run_chain )
        trspk_ibochain_free(world->run_chain);
    ToriMem_Free(ToriMem_Renderer(), world->indices);
    ToriMem_Free(ToriMem_Renderer(), world->runs);
    ToriMem_Free(ToriMem_Renderer(), world->face_keys);
    ToriMem_Free(ToriMem_Renderer(), world->marks);
    ToriMem_Free(ToriMem_Renderer(), world);
    renderer->rev241 = NULL;
}

void
d3d9_rev241_reset(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    world->index_count = 0u;
    world->run_count = 0u;
}

/* ---- the queues ------------------------------------------------------------- */

static struct D3D9Rev241Run*
d3d9_rev241_queue_run(
    struct D3D9Rev241World* world,
    const struct D3D9ModelPlacement* placement,
    enum D3D9Rev241RunKind kind,
    bool translucent,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct D3D9Rev241Run* run;
    if( index_count == 0u )
        return NULL;
    if( world->index_count + index_count > world->index_capacity )
    {
        uint32_t capacity = world->index_capacity ? world->index_capacity : 4096u;
        uint16_t* grown;
        while( capacity < world->index_count + index_count )
            capacity *= 2u;
        grown = (uint16_t*)ToriMem_Realloc(
            ToriMem_Renderer(), world->indices, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->indices = grown;
        world->index_capacity = capacity;
    }
    if( world->run_count >= world->run_capacity )
    {
        uint32_t capacity = world->run_capacity ? world->run_capacity * 2u : 128u;
        struct D3D9Rev241Run* grown = (struct D3D9Rev241Run*)ToriMem_Realloc(
            ToriMem_Renderer(), world->runs, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->runs = grown;
        world->run_capacity = capacity;
    }
    run = &world->runs[world->run_count++];
    memset(run, 0, sizeof(*run));
    run->kind = (uint8_t)kind;
    run->translucent = translucent;
    run->binding = placement->binding;
    run->page_base = placement->page_base;
    run->index_offset = world->index_count;
    run->index_count = index_count;
    memcpy(world->indices + world->index_count, indices, (size_t)index_count * sizeof(*indices));
    world->index_count += index_count;
    return run;
}

/* A face's three indices into `destination`; false when its triplet does not
 * fit the 16-bit page. */
static inline bool
d3d9_rev241_write_face(
    uint16_t* destination,
    uint32_t* written,
    uint32_t local_base,
    uint32_t face)
{
    uint32_t base;
    if( face > (UINT32_MAX - local_base - 2u) / 3u )
        return false;
    base = local_base + face * 3u;
    if( base + 2u > UINT16_MAX )
        return false;
    destination[(*written)++] = (uint16_t)base;
    destination[(*written)++] = (uint16_t)(base + 1u);
    destination[(*written)++] = (uint16_t)(base + 2u);
    return true;
}

static int
d3d9_rev241_compare_face_key(const void* lhs, const void* rhs)
{
    const struct D3D9Rev241FaceKey* a = (const struct D3D9Rev241FaceKey*)lhs;
    const struct D3D9Rev241FaceKey* b = (const struct D3D9Rev241FaceKey*)rhs;
    /* Far buckets first, face index order within one [class174.method5697]. */
    if( a->bucket != b->bucket )
        return a->bucket > b->bucket ? -1 : 1;
    return a->face < b->face ? -1 : (a->face > b->face ? 1 : 0);
}

/* The scene's bench holds this model's projection, made now when the cull did
 * not. False: not visible after all. */
static bool
d3d9_rev241_ensure_projected(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement)
{
    struct ToriDraw_Position position;
    if( placement->projected_in_scene )
        return true;
    position = command->position;
    return ToriDraw_RenderModel1ProjectWithTable(
               command->model,
               renderer->scene,
               &position,
               &renderer->cur_3d.view_port,
               &renderer->cur_3d.camera,
               renderer->kernel) == TORIDRAW_CULL_VISIBLE;
}

/* The camera depth of the model's origin, for a per-draw bias. */
static float
d3d9_rev241_view_depth(
    const struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    /* renderer->view is column-major; row 2 gives camera z. */
    const float* v = renderer->view;
    float const x = (float)command->world_position.x;
    float const y = (float)command->world_position.y;
    float const z = (float)command->world_position.z;
    return v[2] * x + v[6] * y + v[10] * z + v[14];
}

/* ---- projectors --------------------------------------------------------------- */

static void
d3d9_rev241_cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float
d3d9_rev241_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Whether a textured face's texel is NOT its baked texcoord's: its projector
 * names its own P/M/N, and a corner of the face is off the projector's
 * plane. A face in its projector's plane maps every eye ray to the texel its
 * corners' texcoords interpolate to. */
static bool
d3d9_rev241_face_is_projected(
    const struct ToriDraw_Model* model,
    uint32_t face,
    const struct TRSPK_WorldPlacement* placement)
{
    float p[3];
    float u[3];
    float v[3];
    float n[3];
    float length;
    const faceint_t* corners[3];
    if( !model->face_textures || model->face_textures[face] < 0 || !model->face_texture_coords ||
        model->face_texture_coords[face] == -1 )
        return false;
    trspk_toridraw_face_projector(model, face, placement, p, u, v);
    d3d9_rev241_cross(u, v, n);
    length = sqrtf(d3d9_rev241_dot(n, n));
    if( length <= 0.0f )
        return false;
    corners[0] = model->face_indices_a;
    corners[1] = model->face_indices_b;
    corners[2] = model->face_indices_c;
    for( int i = 0; i < 3; i++ )
    {
        int const vertex = corners[i][face];
        float c[3];
        float d[3];
        trspk_toridraw_world_vertex(placement, model->vertices_x[vertex], model->vertices_y[vertex],
            model->vertices_z[vertex], &c[0], &c[1], &c[2]);
        d[0] = c[0] - p[0];
        d[1] = c[1] - p[1];
        d[2] = c[2] - p[2];
        if( fabsf(d3d9_rev241_dot(d, n)) / length > 1.0f )
            return true;
    }
    return false;
}

/*
 * A projector's texcoords as affine functions of world position. The client's
 * lookup: r = point - eye, Pe = P - eye, then
 *   u = -dot(r, Pe x V) / dot(r, U x V),  v = -dot(r, U x Pe) / dot(r, U x V)
 * so s = -dot(r, Pe x V), t = -dot(r, U x Pe), q = dot(r, U x V), and each is
 * dot(point, n) - dot(eye, n). The texture's scroll is added to u and v: s
 * and t gain scroll * q.
 */
static void
d3d9_rev241_projector_rows(
    const float eye[3],
    const float p[3],
    const float u[3],
    const float v[3],
    const float scroll[2],
    float rows[4][3])
{
    float pe[3];
    float ns[3];
    float nt[3];
    float nq[3];
    float cs[3];
    float ct[3];
    for( int i = 0; i < 3; i++ )
        pe[i] = p[i] - eye[i];
    d3d9_rev241_cross(pe, v, cs);
    d3d9_rev241_cross(u, pe, ct);
    d3d9_rev241_cross(u, v, nq);
    for( int i = 0; i < 3; i++ )
    {
        ns[i] = -cs[i] + scroll[0] * nq[i];
        nt[i] = -ct[i] + scroll[1] * nq[i];
    }
    for( int i = 0; i < 3; i++ )
    {
        rows[i][0] = ns[i];
        rows[i][1] = nt[i];
        rows[i][2] = nq[i];
    }
    rows[3][0] = -d3d9_rev241_dot(eye, ns);
    rows[3][1] = -d3d9_rev241_dot(eye, nt);
    rows[3][2] = -d3d9_rev241_dot(eye, nq);
}

/* ---- emission ------------------------------------------------------------------- */

/* A two-pass model: every face in render-priority order, its opaque faces one
 * run and its blended faces another. */
static void
d3d9_rev241_emit_two_pass(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct D3D9Rev241World* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement,
    const uint8_t* passes,
    uint32_t blended_count)
{
    int sorted_face_count;
    const int* face_order;
    uint32_t written = 0u;
    if( !d3d9_rev241_ensure_projected(renderer, command, placement) )
        return;
    sorted_face_count =
        ToriDraw_RenderModel2SortFacesWithTable(command->model, renderer->scene, renderer->kernel);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    for( int i = 0; i < sorted_face_count; i++ )
    {
        uint8_t pass;
        if( face_order[i] < 0 || face_order[i] >= placement->face_count )
            continue;
        pass = passes[face_order[i]];
        if( pass == D3D9_WORLD_FACE_OPAQUE || pass == D3D9_WORLD_FACE_CUTOUT )
            (void)d3d9_rev241_write_face(
                renderer->model_indices, &written, placement->local_base, (uint32_t)face_order[i]);
    }
    (void)d3d9_rev241_queue_run(
        world, placement, D3D9_REV241_RUN_TWO_PASS, false, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_OPAQUE_TRIANGLES, written / 3u);
    if( blended_count == 0u )
        return;
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        if( face_order[i] < 0 || face_order[i] >= placement->face_count )
            continue;
        if( passes[face_order[i]] == D3D9_WORLD_FACE_BLENDED )
            (void)d3d9_rev241_write_face(
                renderer->model_indices, &written, placement->local_base, (uint32_t)face_order[i]);
    }
    (void)d3d9_rev241_queue_run(
        world, placement, D3D9_REV241_RUN_TWO_PASS, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_BLENDED_TRIANGLES, written / 3u);
}

/* A model's blended faces not taken by `taken`, far to near by depth bucket. */
static void
d3d9_rev241_emit_translucent(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct D3D9Rev241World* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement,
    const uint8_t* passes,
    uint32_t blended_count,
    const uint8_t* taken)
{
    const struct ToriDraw_Model* model;
    struct D3D9Rev241FaceKey* keys;
    const int* depth;
    uint32_t key_count = 0u;
    uint32_t written = 0u;
    if( blended_count == 0u || !ToriDraw_ModelKindIsFull(command->model.kind) )
        return;
    if( !d3d9_rev241_ensure_projected(renderer, command, placement) )
        return;
    model = ToriDraw_ModelRead(command->model);
    if( blended_count > world->face_key_capacity )
    {
        uint32_t capacity = world->face_key_capacity ? world->face_key_capacity : 256u;
        void* grown;
        while( capacity < blended_count )
            capacity *= 2u;
        grown = ToriMem_Realloc(
            ToriMem_Renderer(), world->face_keys, (size_t)capacity * sizeof(struct D3D9Rev241FaceKey));
        assert(grown);
        world->face_keys = grown;
        world->face_key_capacity = capacity;
    }
    keys = (struct D3D9Rev241FaceKey*)world->face_keys;
    depth = renderer->scene->screen_vertices_z;
    for( int face = 0; face < placement->face_count; face++ )
    {
        if( passes[face] != D3D9_WORLD_FACE_BLENDED || taken[face] )
            continue;
        assert(key_count < blended_count);
        keys[key_count].face = face;
        keys[key_count].bucket = (depth[model->face_indices_a[face]] + depth[model->face_indices_b[face]] +
                                  depth[model->face_indices_c[face]]) /
                                 3;
        key_count++;
    }
    qsort(keys, key_count, sizeof(*keys), d3d9_rev241_compare_face_key);
    for( uint32_t i = 0u; i < key_count; i++ )
        (void)d3d9_rev241_write_face(
            renderer->model_indices, &written, placement->local_base, (uint32_t)keys[i].face);
    (void)d3d9_rev241_queue_run(
        world, placement, D3D9_REV241_RUN_TRANSLUCENT, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_SORTED_MODELS, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_BLENDED_TRIANGLES, written / 3u);
}

/* A model's opaque faces with one bias value, `bias` (twice the byte). */
static void
d3d9_rev241_emit_biased(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct D3D9Rev241World* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement,
    const uint8_t* passes,
    const uint8_t* taken,
    int bias_byte)
{
    const struct ToriDraw_Model* model = ToriDraw_ModelRead(command->model);
    uint32_t written = 0u;
    struct D3D9Rev241Run* run;
    for( int face = 0; face < placement->face_count; face++ )
    {
        uint8_t const pass = passes[face];
        if( taken[face] || (pass != D3D9_WORLD_FACE_OPAQUE && pass != D3D9_WORLD_FACE_CUTOUT) ||
            model->face_z_offsets[face] != bias_byte )
            continue;
        (void)d3d9_rev241_write_face(renderer->model_indices, &written, placement->local_base, (uint32_t)face);
    }
    run = d3d9_rev241_queue_run(
        world, placement, D3D9_REV241_RUN_BIASED, false, renderer->model_indices, written);
    if( run )
    {
        run->depth_bias = 2.0f * (float)bias_byte;
        run->view_depth = d3d9_rev241_view_depth(renderer, command);
    }
}

/* A model's faces under one projector, as one run in the phase their pass
 * says. `group_face` is any face of the group. */
static void
d3d9_rev241_emit_projected(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    struct D3D9Rev241World* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement,
    const uint8_t* passes,
    const uint8_t* projected,
    const struct TRSPK_WorldPlacement* world_placement,
    int group_face,
    bool translucent)
{
    const struct ToriDraw_Model* model = ToriDraw_ModelRead(command->model);
    int const coords = model->face_texture_coords[group_face];
    int const tex_id = model->face_textures[group_face];
    uint32_t written = 0u;
    struct D3D9Rev241Run* run;
    for( int face = 0; face < placement->face_count; face++ )
    {
        uint8_t const pass = passes[face];
        if( !projected[face] || model->face_texture_coords[face] != coords ||
            model->face_textures[face] != tex_id ||
            (pass == D3D9_WORLD_FACE_BLENDED) != translucent || pass == D3D9_WORLD_FACE_SKIP )
            continue;
        (void)d3d9_rev241_write_face(renderer->model_indices, &written, placement->local_base, (uint32_t)face);
    }
    run = d3d9_rev241_queue_run(
        world, placement, D3D9_REV241_RUN_PROJECTED, translucent, renderer->model_indices, written);
    if( run )
    {
        float p[3];
        float u[3];
        float v[3];
        float scroll[2];
        float const eye[3] = {
            (float)renderer->cur_3d.camera_position.x,
            (float)renderer->cur_3d.camera_position.y,
            (float)renderer->cur_3d.camera_position.z,
        };
        trspk_toridraw_face_projector(model, (uint32_t)group_face, world_placement, p, u, v);
        d3d9_texture_scroll_of(renderer, tex_id, scroll);
        run->tex_id = tex_id;
        d3d9_rev241_projector_rows(eye, p, u, v, scroll, run->projector);
    }
}

static void
d3d9_rev241_emit(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement,
    uint8_t* projected)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    const struct ToriDraw_Model* model =
        ToriDraw_ModelKindIsFull(command->model.kind) ? ToriDraw_ModelRead(command->model) : NULL;
    uint32_t blended_count = 0u;
    const uint8_t* passes =
        d3d9_zbuffer_face_passes(renderer, command, placement, &blended_count);
    uint8_t* taken;
    uint32_t written = 0u;
    int bias_values[D3D9_REV241_BIAS_GROUPS];
    uint32_t bias_value_count = 0u;
    if( !passes )
        return;
    if( model && (model->flags & TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS) )
    {
        d3d9_rev241_emit_two_pass(renderer, world, command, placement, passes, blended_count);
        return;
    }

    /* `taken`: faces a run owns, so the buckets and the translucent run skip
     * them. The projected marks double as taken. */
    taken = projected;

    /* The biased opaque faces' values (rare: floor glows, shadows). */
    if( model && model->face_z_offsets )
    {
        for( int face = 0; face < placement->face_count; face++ )
        {
            int const bias = model->face_z_offsets[face];
            uint8_t const pass = passes[face];
            uint32_t k;
            if( bias == 0 || taken[face] ||
                (pass != D3D9_WORLD_FACE_OPAQUE && pass != D3D9_WORLD_FACE_CUTOUT) )
                continue;
            for( k = 0u; k < bias_value_count; k++ )
                if( bias_values[k] == bias )
                    break;
            if( k == bias_value_count && bias_value_count < D3D9_REV241_BIAS_GROUPS )
                bias_values[bias_value_count++] = bias;
        }
    }

    for( int face = 0; face < placement->face_count; face++ )
    {
        uint8_t const pass = passes[face];
        bool in_bias_run = false;
        if( taken[face] || (pass != D3D9_WORLD_FACE_OPAQUE && pass != D3D9_WORLD_FACE_CUTOUT) )
            continue;
        if( bias_value_count > 0u && model->face_z_offsets[face] != 0 )
            for( uint32_t k = 0u; k < bias_value_count; k++ )
                in_bias_run = in_bias_run || bias_values[k] == model->face_z_offsets[face];
        if( in_bias_run )
            continue;
        (void)d3d9_rev241_write_face(renderer->model_indices, &written, placement->local_base, (uint32_t)face);
    }
    if( written > 0u )
        d3d9_zbuffer_queue_opaque(renderer, placement, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_OPAQUE_TRIANGLES, written / 3u);

    for( uint32_t k = 0u; k < bias_value_count; k++ )
        d3d9_rev241_emit_biased(renderer, world, command, placement, passes, taken, bias_values[k]);
    d3d9_rev241_emit_translucent(renderer, world, command, placement, passes, blended_count, taken);
}

/* One mark per face of the model being emitted, all clear. */
static uint8_t*
d3d9_rev241_marks(struct ToriPlatformWin32_Renderer_D3D9* renderer, int face_count)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    if( (uint32_t)face_count > world->mark_capacity )
    {
        uint32_t grown = world->mark_capacity ? world->mark_capacity : 1024u;
        while( grown < (uint32_t)face_count )
            grown *= 2u;
        world->marks = (uint8_t*)ToriMem_Realloc(ToriMem_Renderer(), world->marks, grown);
        assert(world->marks);
        world->mark_capacity = grown;
    }
    memset(world->marks, 0, (size_t)face_count);
    return world->marks;
}

void
d3d9_rev241_emit_model(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement)
{
    d3d9_rev241_emit(renderer, command, placement, d3d9_rev241_marks(renderer, placement->face_count));
}

void
d3d9_rev241_emit_model_projected(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    uint8_t* projected = d3d9_rev241_marks(renderer, placement->face_count);
    const struct ToriDraw_Model* model;
    struct TRSPK_WorldPlacement world_placement;
    uint32_t blended_count = 0u;
    const uint8_t* passes;
    int groups[D3D9_REV241_PROJECTOR_GROUPS];
    uint32_t group_count = 0u;
    bool any = false;

    if( !ToriDraw_ModelKindIsFull(command->model.kind) )
    {
        d3d9_rev241_emit(renderer, command, placement, projected);
        return;
    }
    model = ToriDraw_ModelRead(command->model);
    if( (model->flags & TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS) || !model->face_texture_coords ||
        !model->face_textures )
    {
        d3d9_rev241_emit(renderer, command, placement, projected);
        return;
    }
    passes = d3d9_zbuffer_face_passes(renderer, command, placement, &blended_count);
    if( !passes )
        return;
    trspk_toridraw_placement_init(&world_placement, &command->world_position);
    for( int face = 0; face < placement->face_count; face++ )
    {
        uint32_t k;
        if( passes[face] == D3D9_WORLD_FACE_SKIP ||
            !d3d9_rev241_face_is_projected(model, (uint32_t)face, &world_placement) )
            continue;
        for( k = 0u; k < group_count; k++ )
            if( model->face_texture_coords[groups[k]] == model->face_texture_coords[face] &&
                model->face_textures[groups[k]] == model->face_textures[face] )
                break;
        if( k == group_count )
        {
            if( group_count == D3D9_REV241_PROJECTOR_GROUPS )
                continue;
            groups[group_count++] = face;
        }
        projected[face] = 1u;
        any = true;
    }
    /* The rest of the model as Sailing draws it; its projected faces are
     * already marked taken. */
    d3d9_rev241_emit(renderer, command, placement, projected);
    if( !any )
        return;
    for( uint32_t k = 0u; k < group_count; k++ )
    {
        d3d9_rev241_emit_projected(
            renderer, world, command, placement, passes, projected, &world_placement, groups[k], false);
        d3d9_rev241_emit_projected(
            renderer, world, command, placement, passes, projected, &world_placement, groups[k], true);
    }
}

bool
d3d9_rev241_place(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    struct D3D9ModelPlacement* placement)
{
    bool projected = false;
    int face_count;
    if( !d3d9_model_begin(renderer, command) )
        return false;
    if( !d3d9_model_cull_depth(renderer, command, &projected) )
        return false;
    if( command->pick_only )
        return false;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 )
        return false;
    return d3d9_model_place(renderer, command, face_count, 0, projected, placement);
}

/* ---- drawing support -------------------------------------------------------------- */

bool
d3d9_rev241_prepare_run(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    trspk_ibochain_reset(world->run_chain);
    trspk_ibochain_push16(
        world->run_chain, run->binding, run->page_base, world->indices + run->index_offset,
        run->index_count);
    return d3d9_retained_prepare(renderer, world->run_chain);
}

bool
d3d9_rev241_prepare_opaque(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    d3d9_zbuffer_flush_opaque(renderer);
    if( !renderer->ibo_chain || !renderer->ibo_chain->head )
        return false;
    return d3d9_retained_prepare(renderer, renderer->ibo_chain);
}

bool
d3d9_rev241_bind_projector_texture(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    struct D3D9Rev241World* world = d3d9_rev241_state(renderer);
    IDirect3DDevice9* device = renderer->device;
    if( run->tex_id < 0 || run->tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return false;
    if( !world->projector_textures )
    {
        world->projector_textures = (IDirect3DTexture9**)ToriMem_Calloc(
            ToriMem_Renderer(), TORIDRAW_TEXTURE_ID_CAPACITY, sizeof(*world->projector_textures));
        assert(world->projector_textures);
    }
    if( !d3d9_texture_standalone(renderer, run->tex_id, &world->projector_textures[run->tex_id]) )
        return false;
    IDirect3DDevice9_SetTexture(device, 0, (IDirect3DBaseTexture9*)world->projector_textures[run->tex_id]);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    /* The client clamps u and wraps v [class171.method5476]. */
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    return true;
}

void
d3d9_rev241_depth_mapping(
    const struct ToriPlatformWin32_Renderer_D3D9* renderer,
    float* out_a,
    float* out_b)
{
    /* d3d9_zbuffer_setup_projection's depth row: z_clip = depth * A + B,
     * w = depth (proj[10] = A, proj[14] = B, column-major). */
    *out_a = renderer->proj[10];
    *out_b = renderer->proj[14];
}

/* ---- matrices ----------------------------------------------------------------- */

void
d3d9_rev241_multiply(const D3DMATRIX* a, const D3DMATRIX* b, D3DMATRIX* out)
{
    D3DMATRIX r;
    for( int i = 0; i < 4; i++ )
        for( int j = 0; j < 4; j++ )
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] +
                        a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    *out = r;
}

bool
d3d9_rev241_invert_affine(const D3DMATRIX* m, D3DMATRIX* out)
{
    /* Gauss-Jordan on the whole 4x4: the view need not be rigid. */
    double a[4][8];
    for( int i = 0; i < 4; i++ )
        for( int j = 0; j < 4; j++ )
        {
            a[i][j] = m->m[i][j];
            a[i][j + 4] = i == j ? 1.0 : 0.0;
        }
    for( int c = 0; c < 4; c++ )
    {
        int pivot = c;
        for( int r = c + 1; r < 4; r++ )
            if( fabs(a[r][c]) > fabs(a[pivot][c]) )
                pivot = r;
        if( fabs(a[pivot][c]) < 1e-12 )
            return false;
        if( pivot != c )
            for( int j = 0; j < 8; j++ )
            {
                double t = a[c][j];
                a[c][j] = a[pivot][j];
                a[pivot][j] = t;
            }
        {
            double const inv = 1.0 / a[c][c];
            for( int j = 0; j < 8; j++ )
                a[c][j] *= inv;
        }
        for( int r = 0; r < 4; r++ )
        {
            double f;
            if( r == c )
                continue;
            f = a[r][c];
            for( int j = 0; j < 8; j++ )
                a[r][j] -= f * a[c][j];
        }
    }
    for( int i = 0; i < 4; i++ )
        for( int j = 0; j < 4; j++ )
            out->m[i][j] = (float)a[i][j + 4];
    return true;
}
