/**
 * The hardware depth-test world path for the D3D9 renderer.
 *
 * Where the painter path leans on submission order for correctness, this one
 * lets the depth buffer resolve occlusion and only sorts what genuinely needs
 * sorting:
 *
 *   1. Each pose is classified once into a material table -- per face, is it
 *      opaque, a binary cutout, or truly blended?  Opaque and cutout faces are
 *      depth-order independent, so they go out in natural face order with
 *      depth writes on and a front-face test standing in for the ordering that
 *      RenderModel2SortFaces used to fuse in.
 *   2. Only models that actually carry blended faces pay for the legacy
 *      priority sort, and their faces are queued into a separate chain drawn
 *      back-to-front afterwards with depth writes off.
 *
 * That split is why this path keeps state the painter has none of: the material
 * tables, and the deferred blended-submission queue.  Both live in struct
 * D3D9ZBufferWorld, which is private to this file -- the core only ever sees
 * the pointer, and its being non-NULL is what selects this implementation.
 *
 * platform_win32_renderer_d3d9_painter.c is the order-dependent alternative.
 * The two are peers and neither calls the other.
 */

#include "platform/platform_win32_renderer_d3d9_core.h"

#include "perf/torirs_perf.h"

#include <stdlib.h>
#include <string.h>

enum D3D9WorldFacePass
{
    D3D9_WORLD_FACE_SKIP = 0,
    D3D9_WORLD_FACE_OPAQUE = 1,
    D3D9_WORLD_FACE_CUTOUT = 2,
    D3D9_WORLD_FACE_BLENDED = 3,
};

struct D3D9MaterialPose
{
    uint8_t* face_passes;
    uint32_t face_count;
    uint32_t opaque_count;
    uint32_t cutout_count;
    uint32_t blended_count;
};

struct D3D9MaterialTrack
{
    struct D3D9MaterialPose* poses;
    uint32_t pose_count;
    uint32_t pose_capacity;
};

struct D3D9MaterialElement
{
    struct D3D9MaterialTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct D3D9MaterialTable
{
    struct D3D9MaterialElement* elements;
    uint32_t element_count;
    uint32_t element_capacity;
};

struct D3D9AlphaSubmission
{
    uint32_t binding;
    uint32_t page_base;
    uint32_t index_offset;
    uint32_t index_count;
    int depth;
    uint32_t ordinal;
};

/** Everything depth mode owns that painter mode has no use for. */
struct D3D9ZBufferWorld
{
    /* Face classification, mirroring the two retained pose tables the core
     * keeps, plus a scratch entry for geometry that is not retained at all. */
    struct D3D9MaterialTable materials;
    struct D3D9MaterialTable batch_materials;
    struct D3D9MaterialPose dynamic_material;

    /* Blended model submissions are the only world objects that still get
     * sorted.  The flat index arena and its records retain capacity across
     * frames. */
    struct TRSPK_IBOChain* alpha_ibo_chain;
    uint16_t* alpha_indices;
    uint32_t alpha_index_count;
    uint32_t alpha_index_capacity;
    struct D3D9AlphaSubmission* alpha_submissions;
    uint32_t alpha_submission_count;
    uint32_t alpha_submission_capacity;
};

static struct D3D9ZBufferWorld*
d3d9_zbuffer_state(struct ToriRS_D3D9* renderer)
{
    return renderer->zbuffer;
}

static void
d3d9_set_projection_zbuffer(
    float* projection,
    float near_plane,
    float far_plane)
{
    float range;
    if( near_plane < 1.0f )
        near_plane = D3D9_WIDGET_MODEL_NEAR;
    if( far_plane <= near_plane )
        far_plane = D3D9_WORLD_FAR;
    range = far_plane - near_plane;

    /* Preserve the legacy X/Y projection scale while replacing its
     * painter-only constant clip-Z with the conventional D3D [0,w] mapping.
     * TRSPK stores column-major matrices; the identical bytes are the
     * transposed D3D row-vector matrix consumed by SetTransform. */
    projection[10] = far_plane / range;
    projection[11] = 1.0f;
    projection[14] = -(near_plane * far_plane) / range;
    projection[15] = 0.0f;
}

static enum D3D9WorldFacePass
d3d9_world_face_pass(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_ModelHandle handle,
    uint32_t face)
{
    if( handle.kind == TORIDRAWMK_MODEL )
    {
        struct ToriDraw_Model* model = handle.u.model.model;
        int raw_type;
        int tex_id;
        uint8_t alpha;
        if( !model || face >= (uint32_t)model->face_count )
            return D3D9_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return D3D9_WORLD_FACE_SKIP;
        /* Animation baking has already written the pose's final face alpha.
         * Apply it before texture classification: textured faces can still be
         * truly translucent, and fully faded faces must not write depth. */
        alpha = model->face_alphas
            ? (uint8_t)(0xffu - model->face_alphas[face])
            : 0xffu;
        if( alpha <= 1u )
            return D3D9_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return D3D9_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
        {
            struct ToriDraw_Texture* texture = renderer && renderer->scene
                ? ToriDraw_TextureMapGet(
                      &ToriDraw_SceneTexState(renderer->scene)->texture_map,
                      tex_id)
                : NULL;
            return texture && texture->opaque
                ? D3D9_WORLD_FACE_OPAQUE
                : D3D9_WORLD_FACE_CUTOUT;
        }
        return D3D9_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return D3D9_WORLD_FACE_SKIP;
        return D3D9_WORLD_FACE_OPAQUE;
    }
    return D3D9_WORLD_FACE_SKIP;
}

static void
d3d9_material_pose_clear(struct D3D9MaterialPose* pose)
{
    if( !pose )
        return;
    free(pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

static bool
d3d9_material_pose_set(
    struct D3D9MaterialPose* pose,
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_ModelHandle handle)
{
    int face_count;
    uint8_t* passes;
    if( !pose )
        return false;
    face_count = trspk_toridraw_face_count(handle);
    if( face_count <= 0 )
        return false;
    passes = pose->face_count == (uint32_t)face_count
        ? pose->face_passes
        : (uint8_t*)realloc(pose->face_passes, (size_t)face_count);
    if( !passes )
        return false;
    pose->face_passes = passes;
    pose->face_count = (uint32_t)face_count;
    pose->opaque_count = 0u;
    pose->cutout_count = 0u;
    pose->blended_count = 0u;
    for( uint32_t face = 0u; face < pose->face_count; face++ )
    {
        enum D3D9WorldFacePass pass =
            d3d9_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == D3D9_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == D3D9_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == D3D9_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    return true;
}

static bool
d3d9_material_table_set(
    struct D3D9MaterialTable* table,
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct D3D9MaterialTrack* track;
    uint32_t needed;
    if( !table || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return false;
    needed = (uint32_t)element_id + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct D3D9MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct D3D9MaterialElement*)realloc(
            table->elements,
            (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        memset(
            grown + table->element_capacity,
            0,
            (size_t)(capacity - table->element_capacity) * sizeof(*grown));
        table->elements = grown;
        table->element_capacity = capacity;
    }
    if( table->element_count < needed )
        table->element_count = needed;
    track = &table->elements[element_id].tracks[anim_index];
    needed = (uint32_t)pose_id + 1u;
    if( needed > track->pose_capacity )
    {
        uint32_t capacity = track->pose_capacity ? track->pose_capacity : 8u;
        struct D3D9MaterialPose* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct D3D9MaterialPose*)realloc(
            track->poses,
            (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        memset(
            grown + track->pose_capacity,
            0,
            (size_t)(capacity - track->pose_capacity) * sizeof(*grown));
        track->poses = grown;
        track->pose_capacity = capacity;
    }
    if( track->pose_count < needed )
        track->pose_count = needed;
    return d3d9_material_pose_set(
        &track->poses[pose_id], renderer, handle);
}

static const struct D3D9MaterialPose*
d3d9_material_table_get(
    const struct D3D9MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct D3D9MaterialTrack* track;
    if( !table || !table->elements || element_id < 0 ||
        (uint32_t)element_id >= table->element_count || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return NULL;
    track = &table->elements[element_id].tracks[anim_index];
    if( !track->poses || (uint32_t)pose_id >= track->pose_count ||
        !track->poses[pose_id].face_passes )
        return NULL;
    return &track->poses[pose_id];
}

static void
d3d9_material_table_remove_track(
    struct D3D9MaterialTable* table,
    int element_id,
    int anim_index)
{
    struct D3D9MaterialTrack* track;
    if( !table || !table->elements || element_id < 0 ||
        (uint32_t)element_id >= table->element_count || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[element_id].tracks[anim_index];
    for( uint32_t pose = 0u; pose < track->pose_count; pose++ )
        d3d9_material_pose_clear(&track->poses[pose]);
    track->pose_count = 0u;
}

static void
d3d9_material_table_remove_element(
    struct D3D9MaterialTable* table,
    int element_id)
{
    for( int track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        d3d9_material_table_remove_track(table, element_id, track);
}

static void
d3d9_material_table_clear(struct D3D9MaterialTable* table)
{
    if( !table )
        return;
    for( uint32_t element = 0u; element < table->element_count; element++ )
        d3d9_material_table_remove_element(table, (int)element);
    table->element_count = 0u;
}

static void
d3d9_material_table_free(struct D3D9MaterialTable* table)
{
    if( !table )
        return;
    d3d9_material_table_clear(table);
    for( uint32_t element = 0u; element < table->element_capacity; element++ )
        for( int track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
            free(table->elements[element].tracks[track].poses);
    free(table->elements);
    memset(table, 0, sizeof(*table));
}

static bool
d3d9_world_face_front_facing(
    struct ToriDraw_ModelHandle handle,
    const struct ToriDraw_Scene* scene,
    uint32_t face)
{
    const faceint_t* face_a;
    const faceint_t* face_b;
    const faceint_t* face_c;
    uint32_t vertex_count;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    int64_t dx1;
    int64_t dy1;
    int64_t dx2;
    int64_t dy2;
    if( !scene || !scene->screen_vertices_x || !scene->screen_vertices_y )
        return false;
    if( handle.kind == TORIDRAWMK_MODEL && handle.u.model.model )
    {
        struct ToriDraw_Model* model = handle.u.model.model;
        if( face >= (uint32_t)model->face_count )
            return false;
        face_a = model->face_indices_a;
        face_b = model->face_indices_b;
        face_c = model->face_indices_c;
        vertex_count = (uint32_t)model->vertex_count;
    }
    else if( handle.kind == TORIDRAWMK_GROUND && handle.u.model.ground )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        if( face >= (uint32_t)ground->face_count )
            return false;
        face_a = ground->face_indices_a;
        face_b = ground->face_indices_b;
        face_c = ground->face_indices_c;
        vertex_count = (uint32_t)ground->vertex_count;
    }
    else
        return false;
    a = (uint32_t)face_a[face];
    b = (uint32_t)face_b[face];
    c = (uint32_t)face_c[face];
    if( a >= vertex_count || b >= vertex_count || c >= vertex_count )
        return false;
    dx1 = (int64_t)scene->screen_vertices_x[a] - scene->screen_vertices_x[b];
    dy1 = (int64_t)scene->screen_vertices_y[a] - scene->screen_vertices_y[b];
    dx2 = (int64_t)scene->screen_vertices_x[c] - scene->screen_vertices_x[b];
    dy2 = (int64_t)scene->screen_vertices_y[c] - scene->screen_vertices_y[b];
    return dx1 * dy2 - dy1 * dx2 > 0;
}

static bool
d3d9_queue_alpha_submission(
    struct D3D9ZBufferWorld* world,
    uint32_t binding,
    uint32_t page_base,
    int depth,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct D3D9AlphaSubmission* submission;
    uint32_t needed_indices;
    if( !world || !indices || index_count == 0u ||
        world->alpha_index_count > UINT32_MAX - index_count )
        return false;
    needed_indices = world->alpha_index_count + index_count;
    if( needed_indices > world->alpha_index_capacity )
    {
        uint32_t capacity = world->alpha_index_capacity
            ? world->alpha_index_capacity
            : 1024u;
        uint16_t* grown;
        while( capacity < needed_indices )
        {
            if( capacity > UINT32_MAX / 2u )
            {
                capacity = needed_indices;
                break;
            }
            capacity *= 2u;
        }
        grown = (uint16_t*)realloc(
            world->alpha_indices,
            (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        world->alpha_indices = grown;
        world->alpha_index_capacity = capacity;
    }
    if( world->alpha_submission_count >= world->alpha_submission_capacity )
    {
        uint32_t capacity = world->alpha_submission_capacity
            ? world->alpha_submission_capacity * 2u
            : 128u;
        struct D3D9AlphaSubmission* grown =
            (struct D3D9AlphaSubmission*)realloc(
                world->alpha_submissions,
                (size_t)capacity * sizeof(*grown));
        if( !grown )
            return false;
        world->alpha_submissions = grown;
        world->alpha_submission_capacity = capacity;
    }
    memcpy(
        world->alpha_indices + world->alpha_index_count,
        indices,
        (size_t)index_count * sizeof(*indices));
    submission = &world->alpha_submissions[world->alpha_submission_count];
    submission->binding = binding;
    submission->page_base = page_base;
    submission->index_offset = world->alpha_index_count;
    submission->index_count = index_count;
    submission->depth = depth;
    submission->ordinal = world->alpha_submission_count;
    world->alpha_index_count = needed_indices;
    world->alpha_submission_count++;
    return true;
}

static int
d3d9_compare_alpha_submission(const void* lhs, const void* rhs)
{
    const struct D3D9AlphaSubmission* a =
        (const struct D3D9AlphaSubmission*)lhs;
    const struct D3D9AlphaSubmission* b =
        (const struct D3D9AlphaSubmission*)rhs;
    if( a->depth > b->depth )
        return -1;
    if( a->depth < b->depth )
        return 1;
    if( a->ordinal < b->ordinal )
        return -1;
    if( a->ordinal > b->ordinal )
        return 1;
    return 0;
}

static void
d3d9_build_alpha_chain(struct D3D9ZBufferWorld* world)
{
    uint32_t i;
    if( !world || !world->alpha_ibo_chain || world->alpha_submission_count == 0u )
        return;
    qsort(
        world->alpha_submissions,
        world->alpha_submission_count,
        sizeof(world->alpha_submissions[0]),
        d3d9_compare_alpha_submission);
    for( i = 0u; i < world->alpha_submission_count; i++ )
    {
        const struct D3D9AlphaSubmission* submission =
            &world->alpha_submissions[i];
        trspk_ibochain_push16(
            world->alpha_ibo_chain,
            submission->binding,
            submission->page_base,
            world->alpha_indices + submission->index_offset,
            submission->index_count);
    }
}

bool
d3d9_zbuffer_create(struct ToriRS_D3D9* renderer)
{
    struct D3D9ZBufferWorld* world =
        (struct D3D9ZBufferWorld*)calloc(1u, sizeof(struct D3D9ZBufferWorld));
    if( !world )
        return false;
    world->alpha_ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    if( !world->alpha_ibo_chain )
    {
        free(world);
        return false;
    }
    renderer->zbuffer = world;
    return true;
}

void
d3d9_zbuffer_destroy(struct ToriRS_D3D9* renderer)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    if( world->alpha_ibo_chain )
        trspk_ibochain_free(world->alpha_ibo_chain);
    d3d9_material_table_free(&world->materials);
    d3d9_material_table_free(&world->batch_materials);
    d3d9_material_pose_clear(&world->dynamic_material);
    free(world->alpha_indices);
    free(world->alpha_submissions);
    free(world);
    renderer->zbuffer = NULL;
}

void
d3d9_zbuffer_reset_pass(struct ToriRS_D3D9* renderer)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    if( world->alpha_ibo_chain )
        trspk_ibochain_reset(world->alpha_ibo_chain);
    world->alpha_index_count = 0u;
    world->alpha_submission_count = 0u;
}

void
d3d9_zbuffer_begin_pass(struct ToriRS_D3D9* renderer)
{
    d3d9_zbuffer_reset_pass(renderer);
    IDirect3DDevice9_Clear(
        renderer->device,
        0u,
        NULL,
        D3DCLEAR_ZBUFFER,
        0u,
        1.0f,
        0u);
}

void
d3d9_zbuffer_setup_projection(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    d3d9_set_projection_zbuffer(
        renderer->proj,
        (float)command->camera.near_plane_z,
        D3D9_WORLD_FAR);
}

void
d3d9_zbuffer_end_pass(struct ToriRS_D3D9* renderer)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    d3d9_build_alpha_chain(world);
    if( world->alpha_ibo_chain && world->alpha_ibo_chain->head )
        d3d9_draw_retained(renderer, world->alpha_ibo_chain, true);
}

void
d3d9_zbuffer_apply_world_states(struct ToriRS_D3D9* renderer)
{
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZWRITEENABLE, TRUE);
}

void
d3d9_zbuffer_apply_pass_states(struct ToriRS_D3D9* renderer, bool blended_pass)
{
    /* The blended chain is already sorted back-to-front, so it blends against
     * the opaque result without contributing depth of its own. */
    IDirect3DDevice9_SetRenderState(
        renderer->device,
        D3DRS_ZWRITEENABLE,
        blended_pass ? FALSE : TRUE);
    IDirect3DDevice9_SetRenderState(
        renderer->device,
        D3DRS_ALPHABLENDENABLE,
        blended_pass ? TRUE : FALSE);
}

void
d3d9_zbuffer_emit_model(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    const struct D3D9MaterialPose* material = NULL;
    const uint32_t local_base = placement->local_base;
    uint32_t written = 0u;
    int sorted_face_count;
    int* face_order;
    int i;

    if( !world )
        return;
    if( placement->dynamic )
    {
        if( d3d9_material_pose_set(
                &world->dynamic_material, renderer, command->model) )
            material = &world->dynamic_material;
    }
    else
        material = d3d9_material_table_get(
            placement->binding >= D3D9_STATIC_PAGE_BINDING_BASE
                ? &world->batch_materials
                : &world->materials,
            command->element_id,
            placement->anim_index,
            placement->pose_id);
    if( !material && d3d9_material_pose_set(
            &world->dynamic_material, renderer, command->model) )
        material = &world->dynamic_material;
    if( !material || material->face_count != (uint32_t)placement->face_count )
        return;

    /* Opaque and binary-cutout faces are depth-order independent. Preserve
     * natural face order and perform only the projected front-face test that
     * used to be fused into RenderModel2SortFaces. */
    for( i = 0; i < placement->face_count; i++ )
    {
        uint32_t face = (uint32_t)i;
        uint32_t base;
        if( (material->face_passes[face] != D3D9_WORLD_FACE_OPAQUE &&
             material->face_passes[face] != D3D9_WORLD_FACE_CUTOUT) ||
            !d3d9_world_face_front_facing(command->model, renderer->scene, face) ||
            face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        renderer->model_indices[written++] = (uint16_t)base;
        renderer->model_indices[written++] = (uint16_t)(base + 1u);
        renderer->model_indices[written++] = (uint16_t)(base + 2u);
    }
    trspk_ibochain_push16(
        renderer->ibo_chain,
        placement->binding,
        placement->page_base,
        renderer->model_indices,
        written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_OPAQUE_TRIANGLES, written / 3u);

    /* Only models with true blended faces pay the legacy depth/priority sort.
     * Filter the sorted result so opaque faces never enter the blend pass. */
    written = 0u;
    if( material->blended_count == 0u )
        return;
    sorted_face_count = ToriDraw_RenderModel2SortFaces(command->model, renderer->scene);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_SORTED_MODELS, 1);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    for( i = 0; i < sorted_face_count; i++ )
    {
        uint32_t face;
        uint32_t base;
        if( face_order[i] < 0 )
            continue;
        face = (uint32_t)face_order[i];
        if( material->face_passes[face] != D3D9_WORLD_FACE_BLENDED ||
            face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        renderer->model_indices[written++] = (uint16_t)base;
        renderer->model_indices[written++] = (uint16_t)(base + 1u);
        renderer->model_indices[written++] = (uint16_t)(base + 2u);
    }
    (void)d3d9_queue_alpha_submission(
        world,
        placement->binding,
        placement->page_base,
        renderer->scene->projected_vertex.z,
        renderer->model_indices,
        written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_Z_BLENDED_TRIANGLES, written / 3u);
}

void
d3d9_zbuffer_pose_baked(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    (void)d3d9_material_table_set(
        &world->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
d3d9_zbuffer_element_dropped(struct ToriRS_D3D9* renderer, int element_id)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    d3d9_material_table_remove_element(&world->materials, element_id);
}

void
d3d9_zbuffer_track_dropped(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    d3d9_material_table_remove_track(&world->materials, element_id, anim_index);
}

void
d3d9_zbuffer_batch_pose_baked(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    if( !world )
        return;
    (void)d3d9_material_table_set(
        &world->batch_materials,
        renderer,
        element_id,
        anim_index,
        pose_id,
        handle);
}

void
d3d9_zbuffer_batch_dropped(struct ToriRS_D3D9* renderer, struct TRSPK_Batch16* cpu)
{
    struct D3D9ZBufferWorld* world = d3d9_zbuffer_state(renderer);
    uint32_t entry_count;
    uint32_t entry_index;
    if( !world )
        return;
    entry_count = trspk_batch16_entry_count(cpu);
    for( entry_index = 0u; entry_index < entry_count; entry_index++ )
    {
        const struct TRSPK_Batch16Entry* entry =
            trspk_batch16_get_entry(cpu, entry_index);
        if( entry )
            d3d9_material_table_remove_element(
                &world->batch_materials, entry->element_id);
    }
}
