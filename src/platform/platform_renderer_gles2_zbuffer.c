/**
 * The hardware depth-test world path for the GLES2 renderer.
 *
 * Where the painter path leans on submission order for correctness, this one
 * lets the depth buffer resolve occlusion and only sorts what genuinely needs
 * sorting:
 *
 *   1. Each pose is classified once into a material table -- per face, is it
 *      opaque, a binary cutout, or truly blended? Opaque and cutout faces are
 *      depth-order independent.
 *   2. A pose whose faces are ALL opaque/cutout is a contiguous run of
 *      triangles in its vertex buffer (a bake writes faces as sequential
 *      triplets), so it needs no indices at all: it is queued as an ARRAY
 *      RANGE and drawn with glDrawArrays. Ranges are sorted by buffer and
 *      coalesced when contiguous, which is what the static world -- baked in
 *      scene order into Batch16 pages -- overwhelmingly is. That is the
 *      "static VBO, no per-frame index rebuild" the D3D9 lane could never
 *      quite reach: D3D9 rebuilds its U16 stream every frame.
 *   3. A pose with a mix of face kinds sends its opaque and cutout faces
 *      through per-page index buckets (one chain node per page and program,
 *      like D3D9's D3D9OpaqueBucket minus the vertex-span clustering that only
 *      software vertex processing needed).
 *   4. Only models that actually carry blended faces pay for the legacy
 *      priority sort, and their faces are queued into a separate chain drawn
 *      back-to-front afterwards with depth writes off.
 *
 * The split between the plain and the cutout fragment program is kept all the
 * way through: opaque faces never run a shader that can discard, so the GPU
 * keeps early depth rejection for them.
 *
 * All of it lives in struct GLES2ZBufferWorld, private to this file -- the
 * core only ever sees the pointer, and its being non-NULL is what selects this
 * implementation. platform_renderer_gles2_painter.c is the
 * order-dependent alternative. The two are peers and neither calls the other.
 */

#include "platform/platform_renderer_gles2_core.h"

#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "toridraw.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum GLES2WorldFacePass
{
    GLES2_WORLD_FACE_SKIP = 0,
    GLES2_WORLD_FACE_OPAQUE = 1,
    GLES2_WORLD_FACE_CUTOUT = 2,
    GLES2_WORLD_FACE_BLENDED = 3,
};

struct GLES2MaterialPose
{
    uint8_t* face_passes;
    uint32_t face_count;
    uint32_t opaque_count;
    uint32_t cutout_count;
    uint32_t blended_count;
    /** Every face is opaque or cutout: the pose is one array range. */
    bool uniform;
};

struct GLES2MaterialTrack
{
    struct GLES2MaterialPose* poses;
    uint32_t pose_count;
    uint32_t pose_capacity;
};

struct GLES2MaterialElement
{
    struct GLES2MaterialTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct GLES2MaterialTable
{
    struct GLES2MaterialElement* elements;
    uint32_t element_count;
    uint32_t element_capacity;
};

struct GLES2AlphaSubmission
{
    uint32_t binding;
    uint32_t page_base;
    uint32_t index_offset;
    uint32_t index_count;
    int depth;
    uint32_t ordinal;
};

/**
 * One frame's indexed opaque-or-cutout faces for a single
 * (binding, page_base, program) triple. Models arrive in scene-traversal
 * order, which ping-pongs across the pages; pushed straight onto the chain
 * that order becomes one node per model. Depth testing makes opaque submission
 * order irrelevant, so the faces are gathered per page here and flushed as one
 * node each. Slots keep their storage across frames; only the counts reset.
 */
struct GLES2OpaqueBucket
{
    uint32_t binding;
    uint32_t page_base;
    bool cutout;
    uint16_t* indices;
    uint32_t index_count;
    uint32_t index_capacity;
};

/** One uniform pose, or after coalescing a run of them: `first` is the
 *  absolute vertex index in the binding's buffer. */
struct GLES2ArrayRange
{
    uint32_t binding;
    uint32_t first;
    uint32_t count;
    bool cutout;
};

/** Everything depth mode owns that painter mode has no use for. */
struct GLES2ZBufferWorld
{
    struct GLES2MaterialTable materials;
    struct GLES2MaterialTable batch_materials;
    struct GLES2MaterialPose dynamic_material;

    uint16_t* alpha_indices;
    uint32_t alpha_index_count;
    uint32_t alpha_index_capacity;
    struct GLES2AlphaSubmission* alpha_submissions;
    uint32_t alpha_submission_count;
    uint32_t alpha_submission_capacity;

    struct GLES2OpaqueBucket* opaque_buckets;
    uint32_t opaque_bucket_count;
    uint32_t opaque_bucket_capacity;
    /* Scratch for one model's cutout indices while its opaque ones are in
     * the core's model_indices. */
    uint16_t* cutout_scratch;
    uint32_t cutout_scratch_capacity;

    struct GLES2ArrayRange* array_ranges;
    uint32_t array_range_count;
    uint32_t array_range_capacity;
    /* After coalescing: the ranges actually drawn this frame. */
    uint32_t array_draw_count;
};

static struct GLES2ZBufferWorld*
gles2_zbuffer_state(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    return renderer->zbuffer;
}

/*
 * Which pass a textured face belongs to.
 *
 * OPAQUE only when the texture is in the scene map, decoded as opaque, AND
 * has an atlas slot. Everything else is CUTOUT -- including "not loaded yet",
 * because the plain fragment program never discards, and a face reading a
 * reserved-but-empty tile through it would paint black where the software
 * rasteriser paints nothing. The cutout program alpha-tests those texels away,
 * which is what the D3D9 alpha test did for every face.
 */
static enum GLES2WorldFacePass
gles2_textured_face_pass(struct ToriRS_GLES2* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return GLES2_WORLD_FACE_CUTOUT;
    texture = renderer->scene
        ? ToriDraw_TextureMapGet(
              &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id)
        : NULL;
    if( !texture || !texture->opaque || renderer->tex_slot_of_id[tex_id] < 0 )
        return GLES2_WORLD_FACE_CUTOUT;
    return GLES2_WORLD_FACE_OPAQUE;
}

static enum GLES2WorldFacePass
gles2_world_face_pass(
    struct ToriRS_GLES2* renderer,
    struct ToriDraw_ModelHandle handle,
    uint32_t face)
{
    if( ToriDraw_ModelKindIsFull(handle.kind) )
    {
        struct ToriDraw_Model* model = handle.u.model.model;
        int raw_type;
        int tex_id;
        uint8_t alpha;
        if( !model || face >= (uint32_t)model->face_count )
            return GLES2_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return GLES2_WORLD_FACE_SKIP;
        /* Animation baking has already written the pose's final face alpha.
         * Apply it before texture classification: textured faces can still be
         * truly translucent, and fully faded faces must not write depth. */
        alpha = model->face_alphas ? (uint8_t)(0xffu - model->face_alphas[face]) : 0xffu;
        if( alpha <= 1u )
            return GLES2_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return GLES2_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
            return gles2_textured_face_pass(renderer, tex_id);
        return GLES2_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        int tex_id;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return GLES2_WORLD_FACE_SKIP;
        tex_id = ground->face_textures ? (int)ground->face_textures[face] : -1;
        if( tex_id >= 0 )
            return gles2_textured_face_pass(renderer, tex_id);
        return GLES2_WORLD_FACE_OPAQUE;
    }
    return GLES2_WORLD_FACE_SKIP;
}

/* --- the material tables --------------------------------------------------- */

static void
gles2_material_pose_clear(struct GLES2MaterialPose* pose)
{
    assert(pose);
    free(pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

static bool
gles2_material_pose_set(
    struct GLES2MaterialPose* pose,
    struct ToriRS_GLES2* renderer,
    struct ToriDraw_ModelHandle handle)
{
    int face_count;
    uint8_t* passes;
    uint32_t face;

    assert(pose);
    assert(renderer);
    face_count = trspk_toridraw_face_count(handle);
    if( face_count <= 0 )
        return false;
    if( pose->face_count == (uint32_t)face_count && pose->face_passes )
        passes = pose->face_passes;
    else
    {
        passes = (uint8_t*)realloc(pose->face_passes, (size_t)face_count);
        assert(passes);
    }
    pose->face_passes = passes;
    pose->face_count = (uint32_t)face_count;
    pose->opaque_count = 0u;
    pose->cutout_count = 0u;
    pose->blended_count = 0u;
    for( face = 0u; face < pose->face_count; face++ )
    {
        enum GLES2WorldFacePass pass = gles2_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == GLES2_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == GLES2_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == GLES2_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    pose->uniform = pose->opaque_count + pose->cutout_count == pose->face_count;
    return true;
}

static bool
gles2_material_table_set(
    struct GLES2MaterialTable* table,
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct GLES2MaterialTrack* track;
    uint32_t needed;

    assert(table);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        pose_id < 0 )
        return false;
    needed = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct GLES2MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct GLES2MaterialElement*)realloc(
            table->elements, (size_t)capacity * sizeof(*grown));
        assert(grown);
        memset(
            grown + table->element_capacity,
            0,
            (size_t)(capacity - table->element_capacity) * sizeof(*grown));
        table->elements = grown;
        table->element_capacity = capacity;
    }
    if( table->element_count < needed )
        table->element_count = needed;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    needed = (uint32_t)pose_id + 1u;
    if( needed > track->pose_capacity )
    {
        uint32_t capacity = track->pose_capacity ? track->pose_capacity : 8u;
        struct GLES2MaterialPose* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct GLES2MaterialPose*)realloc(
            track->poses, (size_t)capacity * sizeof(*grown));
        assert(grown);
        memset(
            grown + track->pose_capacity,
            0,
            (size_t)(capacity - track->pose_capacity) * sizeof(*grown));
        track->poses = grown;
        track->pose_capacity = capacity;
    }
    if( track->pose_count < needed )
        track->pose_count = needed;
    return gles2_material_pose_set(&track->poses[pose_id], renderer, handle);
}

static const struct GLES2MaterialPose*
gles2_material_table_get(
    const struct GLES2MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct GLES2MaterialTrack* track;
    assert(table);
    if( !table->elements || element_id < 0 ||
        (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return NULL;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    if( !track->poses || (uint32_t)pose_id >= track->pose_count ||
        !track->poses[pose_id].face_passes )
        return NULL;
    return &track->poses[pose_id];
}

static void
gles2_material_table_remove_track(
    struct GLES2MaterialTable* table,
    int element_id,
    int anim_index)
{
    struct GLES2MaterialTrack* track;
    uint32_t pose;
    assert(table);
    if( !table->elements || element_id < 0 ||
        (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    for( pose = 0u; pose < track->pose_count; pose++ )
        gles2_material_pose_clear(&track->poses[pose]);
    track->pose_count = 0u;
}

static void
gles2_material_table_remove_element(struct GLES2MaterialTable* table, int element_id)
{
    int track;
    for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        gles2_material_table_remove_track(table, element_id, track);
}

static void
gles2_material_table_free(struct GLES2MaterialTable* table)
{
    uint32_t element;
    int track;
    if( !table )
        return;
    for( element = 0u; element < table->element_count; element++ )
        gles2_material_table_remove_element(table, (int)element);
    for( element = 0u; element < table->element_capacity; element++ )
        for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
            free(table->elements[element].tracks[track].poses);
    free(table->elements);
    memset(table, 0, sizeof(*table));
}

static uint64_t
gles2_material_table_bytes(const struct GLES2MaterialTable* table)
{
    uint64_t bytes = (uint64_t)table->element_capacity * sizeof(struct GLES2MaterialElement);
    uint32_t element_index;
    uint32_t track;
    uint32_t pose;
    for( element_index = 0u; element_index < table->element_count; element_index++ )
        for( track = 0u; track < TRSPK_POSE_TRACK_COUNT; track++ )
        {
            const struct GLES2MaterialTrack* material_track =
                &table->elements[element_index].tracks[track];
            bytes += (uint64_t)material_track->pose_capacity * sizeof(struct GLES2MaterialPose);
            for( pose = 0u; pose < material_track->pose_count; pose++ )
                bytes += material_track->poses[pose].face_count;
        }
    return bytes;
}

/* --- per-frame queues ------------------------------------------------------ */

static void
gles2_queue_alpha_submission(
    struct GLES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t page_base,
    int depth,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct GLES2AlphaSubmission* submission;
    uint32_t needed_indices;

    assert(world);
    assert(indices);
    if( index_count == 0u )
        return;
    needed_indices = world->alpha_index_count + index_count;
    if( needed_indices > world->alpha_index_capacity )
    {
        uint32_t capacity = world->alpha_index_capacity ? world->alpha_index_capacity : 1024u;
        uint16_t* grown;
        while( capacity < needed_indices )
            capacity *= 2u;
        grown = (uint16_t*)realloc(world->alpha_indices, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->alpha_indices = grown;
        world->alpha_index_capacity = capacity;
    }
    if( world->alpha_submission_count >= world->alpha_submission_capacity )
    {
        uint32_t capacity =
            world->alpha_submission_capacity ? world->alpha_submission_capacity * 2u : 128u;
        struct GLES2AlphaSubmission* grown = (struct GLES2AlphaSubmission*)realloc(
            world->alpha_submissions, (size_t)capacity * sizeof(*grown));
        assert(grown);
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
}

static void
gles2_queue_opaque_indices(
    struct GLES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct GLES2OpaqueBucket* bucket = NULL;
    uint32_t bucket_index;

    assert(world);
    assert(indices);
    if( index_count == 0u )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct GLES2OpaqueBucket* candidate = &world->opaque_buckets[bucket_index];
        if( candidate->binding == binding && candidate->page_base == page_base &&
            candidate->cutout == cutout )
        {
            bucket = candidate;
            break;
        }
    }
    if( !bucket )
    {
        if( world->opaque_bucket_count >= world->opaque_bucket_capacity )
        {
            uint32_t capacity =
                world->opaque_bucket_capacity ? world->opaque_bucket_capacity * 2u : 64u;
            struct GLES2OpaqueBucket* grown = (struct GLES2OpaqueBucket*)realloc(
                world->opaque_buckets, (size_t)capacity * sizeof(*grown));
            assert(grown);
            memset(
                grown + world->opaque_bucket_capacity,
                0,
                (size_t)(capacity - world->opaque_bucket_capacity) * sizeof(*grown));
            world->opaque_buckets = grown;
            world->opaque_bucket_capacity = capacity;
        }
        bucket = &world->opaque_buckets[world->opaque_bucket_count++];
        bucket->binding = binding;
        bucket->page_base = page_base;
        bucket->cutout = cutout;
        bucket->index_count = 0u;
    }
    if( bucket->index_count + index_count > bucket->index_capacity )
    {
        uint32_t capacity = bucket->index_capacity ? bucket->index_capacity : 1024u;
        uint16_t* grown;
        while( capacity < bucket->index_count + index_count )
            capacity *= 2u;
        grown = (uint16_t*)realloc(bucket->indices, (size_t)capacity * sizeof(*grown));
        assert(grown);
        bucket->indices = grown;
        bucket->index_capacity = capacity;
    }
    memcpy(
        bucket->indices + bucket->index_count,
        indices,
        (size_t)index_count * sizeof(*indices));
    bucket->index_count += index_count;
}

static void
gles2_queue_array_range(
    struct GLES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout)
{
    struct GLES2ArrayRange* range;
    assert(world);
    if( count == 0u )
        return;
    if( world->array_range_count >= world->array_range_capacity )
    {
        uint32_t capacity =
            world->array_range_capacity ? world->array_range_capacity * 2u : 1024u;
        struct GLES2ArrayRange* grown = (struct GLES2ArrayRange*)realloc(
            world->array_ranges, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->array_ranges = grown;
        world->array_range_capacity = capacity;
    }
    range = &world->array_ranges[world->array_range_count++];
    range->binding = binding;
    range->first = first;
    range->count = count;
    range->cutout = cutout;
}

static int
gles2_compare_opaque_bucket(const void* lhs, const void* rhs)
{
    const struct GLES2OpaqueBucket* a = (const struct GLES2OpaqueBucket*)lhs;
    const struct GLES2OpaqueBucket* b = (const struct GLES2OpaqueBucket*)rhs;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->page_base != b->page_base )
        return a->page_base < b->page_base ? -1 : 1;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    return 0;
}

static int
gles2_compare_array_range(const void* lhs, const void* rhs)
{
    const struct GLES2ArrayRange* a = (const struct GLES2ArrayRange*)lhs;
    const struct GLES2ArrayRange* b = (const struct GLES2ArrayRange*)rhs;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->first != b->first )
        return a->first < b->first ? -1 : 1;
    return 0;
}

static int
gles2_compare_alpha_submission(const void* lhs, const void* rhs)
{
    const struct GLES2AlphaSubmission* a = (const struct GLES2AlphaSubmission*)lhs;
    const struct GLES2AlphaSubmission* b = (const struct GLES2AlphaSubmission*)rhs;
    /* Back to front: larger depth first; ties keep submission order. */
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
gles2_push_alpha_submissions(struct ToriRS_GLES2* renderer, struct GLES2ZBufferWorld* world)
{
    uint32_t submission_index;
    assert(renderer);
    assert(world);
    if( world->alpha_submission_count == 0u )
        return;
    qsort(
        world->alpha_submissions,
        world->alpha_submission_count,
        sizeof(world->alpha_submissions[0]),
        gles2_compare_alpha_submission);
    for( submission_index = 0u; submission_index < world->alpha_submission_count;
         submission_index++ )
    {
        const struct GLES2AlphaSubmission* submission =
            &world->alpha_submissions[submission_index];
        gles2_sequence_push_indexed(
            renderer,
            submission->binding,
            submission->page_base,
            true,
            true,
            world->alpha_indices + submission->index_offset,
            submission->index_count);
    }
}

/* --- lifetime ----------------------------------------------------------------- */

bool
gles2_zbuffer_create(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world;
    assert(renderer);
    world = (struct GLES2ZBufferWorld*)calloc(1u, sizeof(struct GLES2ZBufferWorld));
    assert(world);
    renderer->zbuffer = world;
    return true;
}

void
gles2_zbuffer_destroy(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world;
    uint32_t bucket_index;
    if( !renderer || !renderer->zbuffer )
        return;
    world = renderer->zbuffer;
    gles2_material_table_free(&world->materials);
    gles2_material_table_free(&world->batch_materials);
    gles2_material_pose_clear(&world->dynamic_material);
    free(world->alpha_indices);
    free(world->alpha_submissions);
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_capacity; bucket_index++ )
        free(world->opaque_buckets[bucket_index].indices);
    free(world->opaque_buckets);
    free(world->cutout_scratch);
    free(world->array_ranges);
    free(world);
    renderer->zbuffer = NULL;
}

void
gles2_zbuffer_report_memory(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    uint64_t bucket_bytes = 0u;
    uint64_t material_bytes;
    uint64_t batch_material_bytes;
    uint32_t bucket_index;
    if( !world )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_capacity; bucket_index++ )
        bucket_bytes += sizeof(world->opaque_buckets[bucket_index]) +
            (uint64_t)world->opaque_buckets[bucket_index].index_capacity * sizeof(uint16_t);
    material_bytes = gles2_material_table_bytes(&world->materials);
    batch_material_bytes = gles2_material_table_bytes(&world->batch_materials);
    /* TORIRS_LOG compiles out of a release build; computed regardless so the
     * function producing the figure is not dead code there. */
    (void)material_bytes;
    (void)batch_material_bytes;
    TORIRS_LOG("gles2_mem: zb_materials          %10.2f MB\n"
               "gles2_mem: zb_batch_materials    %10.2f MB\n"
               "gles2_mem: zb_alpha_arena        %10.2f MB\n"
               "gles2_mem: zb_opaque_buckets     %10.2f MB\n"
               "gles2_mem: zb_array_ranges       %10.2f MB\n",
        (double)material_bytes / 1048576.0,
        (double)batch_material_bytes / 1048576.0,
        ((double)world->alpha_index_capacity * sizeof(uint16_t) +
            (double)world->alpha_submission_capacity * sizeof(struct GLES2AlphaSubmission)) /
            1048576.0,
        (double)bucket_bytes / 1048576.0,
        (double)world->array_range_capacity * sizeof(struct GLES2ArrayRange) / 1048576.0);
}

void
gles2_zbuffer_reset_pass(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    uint32_t bucket_index;
    if( !world )
        return;
    world->alpha_index_count = 0u;
    world->alpha_submission_count = 0u;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
        world->opaque_buckets[bucket_index].index_count = 0u;
    world->opaque_bucket_count = 0u;
    world->array_range_count = 0u;
    world->array_draw_count = 0u;
}

/* --- the pass ------------------------------------------------------------------ */

void
gles2_zbuffer_begin_pass(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    gles2_zbuffer_reset_pass(renderer);
    /*
     * Once per world pass, scissored to the world viewport so the UI drawn
     * around it is untouched. The depth mask is forced on for the clear:
     * glClear obeys it, and a clear issued while the mask happens to be false
     * silently does nothing -- which leaves last frame's depth to reject this
     * frame's geometry, and looks like random missing models.
     */
    gles2_set_scissor(renderer, &renderer->world_viewport);
    gles2_set_depth(renderer, true, true);
    glClear(GL_DEPTH_BUFFER_BIT);
    gles2_set_scissor(renderer, NULL);
}

void
gles2_zbuffer_setup_projection(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    float near_z;
    float far_z = GLES2_WORLD_FAR;
    float range;

    assert(renderer);
    assert(command);
    /*
     * Give the matrix a real depth row. The painter projection leaves clip.z
     * constant because nothing reads it; under a depth buffer that would put
     * every fragment at the same depth. GL's clip volume is z in [-w, w], so
     * this is the GL mapping rather than a copy of the D3D one.
     */
    near_z = (float)command->camera.near_plane_z;
    if( near_z < 1.0f )
        near_z = GLES2_WORLD_NEAR;
    if( far_z <= near_z )
        far_z = near_z + 1.0f;
    range = far_z - near_z;
    renderer->projection[10] = (far_z + near_z) / range;
    renderer->projection[11] = 1.0f;
    renderer->projection[14] = -(2.0f * far_z * near_z) / range;
    renderer->projection[15] = 0.0f;
}

/* Which way round the GPU culls. GL_CCW front + cull GL_BACK is the GL
 * spelling of D3DCULL_CW, the handedness measured on the D3D9 lane.
 * TORIRS_GLES2_CULL overrides it -- `ccw`, `cw`, or `none` to draw both
 * sides, which is the useful one when a model looks inside out. */
static int
gles2_zbuffer_cull_mode(void)
{
    static int mode = -1;
    if( mode < 0 )
    {
        const char* value = getenv("TORIRS_GLES2_CULL");
        if( value && (value[0] == 'n' || value[0] == 'N') )
            mode = 0;
        else if( value && (value[0] == 'c' || value[0] == 'C') && (value[1] == 'w' || value[1] == 'W') )
            mode = 2;
        else
            mode = 1;
    }
    return mode;
}

void
gles2_zbuffer_apply_world_states(struct ToriRS_GLES2* renderer)
{
    int mode = gles2_zbuffer_cull_mode();
    assert(renderer);
    /* LEQUAL, not LESS: coplanar geometry submitted twice (a decor plane on
     * its floor tile) must keep the later one, which is what painter order did
     * and what the content is authored against. */
    gles2_set_depth(renderer, true, true);
    glDepthFunc(GL_LEQUAL);
    if( mode == 0 )
        gles2_set_cull(renderer, false);
    else
    {
        glFrontFace(mode == 1 ? GL_CCW : GL_CW);
        glCullFace(GL_BACK);
        gles2_set_cull(renderer, true);
    }
}

void
gles2_zbuffer_apply_pass_states(struct ToriRS_GLES2* renderer, bool blended_pass)
{
    assert(renderer);
    /* The blended chain is already sorted back-to-front, so it blends against
     * the opaque result without contributing depth of its own. */
    gles2_set_depth(renderer, true, !blended_pass);
    gles2_set_blend(renderer, blended_pass);
}

void
gles2_zbuffer_flush_opaque(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    uint32_t bucket_index;
    uint32_t range_index;
    uint32_t write_index = 0u;
    if( !world )
        return;

    /* Array ranges first: sorted so that contiguous poses in one buffer
     * become one draw, and so the plain program runs before the cutout one.
     * They are the bulk of the static world. */
    if( world->array_range_count > 0u )
    {
        qsort(
            world->array_ranges,
            world->array_range_count,
            sizeof(world->array_ranges[0]),
            gles2_compare_array_range);
        for( range_index = 1u; range_index < world->array_range_count; range_index++ )
        {
            struct GLES2ArrayRange* previous = &world->array_ranges[write_index];
            const struct GLES2ArrayRange* current = &world->array_ranges[range_index];
            if( current->binding == previous->binding && current->cutout == previous->cutout &&
                previous->first + previous->count == current->first )
            {
                previous->count += current->count;
                continue;
            }
            write_index++;
            if( write_index != range_index )
                world->array_ranges[write_index] = *current;
        }
        world->array_draw_count = write_index + 1u;
        for( range_index = 0u; range_index < world->array_draw_count; range_index++ )
        {
            const struct GLES2ArrayRange* range = &world->array_ranges[range_index];
            gles2_sequence_push_array(
                renderer, range->binding, range->first, range->count, range->cutout, false);
        }
        world->array_range_count = 0u;
    }

    /* Then the per-page index buckets, in page order so consecutive items
     * stay on one buffer binding. */
    if( world->opaque_bucket_count > 1u )
        qsort(
            world->opaque_buckets,
            world->opaque_bucket_count,
            sizeof(world->opaque_buckets[0]),
            gles2_compare_opaque_bucket);
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct GLES2OpaqueBucket* bucket = &world->opaque_buckets[bucket_index];
        if( bucket->index_count == 0u )
            continue;
        gles2_sequence_push_indexed(
            renderer,
            bucket->binding,
            bucket->page_base,
            bucket->cutout,
            false,
            bucket->indices,
            bucket->index_count);
        bucket->index_count = 0u;
    }
    world->opaque_bucket_count = 0u;
}

void
gles2_zbuffer_end_pass(struct ToriRS_GLES2* renderer)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    if( !world )
        return;
    gles2_push_alpha_submissions(renderer, world);
}

static void
gles2_zbuffer_reserve_cutout_scratch(struct GLES2ZBufferWorld* world, uint32_t needed)
{
    uint16_t* grown;
    uint32_t capacity;
    if( needed <= world->cutout_scratch_capacity )
        return;
    capacity = world->cutout_scratch_capacity ? world->cutout_scratch_capacity : 256u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (uint16_t*)realloc(world->cutout_scratch, (size_t)capacity * sizeof(*grown));
    assert(grown);
    world->cutout_scratch = grown;
    world->cutout_scratch_capacity = capacity;
}

void
gles2_zbuffer_emit_model(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct GLES2ModelPlacement* placement)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    const struct GLES2MaterialPose* material = NULL;
    const uint32_t local_base = placement->local_base;
    uint32_t opaque_written = 0u;
    uint32_t cutout_written = 0u;
    int sorted_face_count;
    int* face_order;
    int face_index;

    assert(command);
    assert(placement);
    if( !world )
        return;
    if( placement->dynamic )
    {
        if( gles2_material_pose_set(&world->dynamic_material, renderer, command->model) )
            material = &world->dynamic_material;
    }
    else
        material = gles2_material_table_get(
            placement->binding >= GLES2_STATIC_PAGE_BINDING ? &world->batch_materials
                                                            : &world->materials,
            command->element_id,
            placement->anim_index,
            placement->pose_id);
    if( !material &&
        gles2_material_pose_set(&world->dynamic_material, renderer, command->model) )
        material = &world->dynamic_material;
    if( !material || material->face_count != (uint32_t)placement->face_count )
        return;

    /*
     * The common case, and the whole point of this path: every face is opaque
     * or cutout, the pose's triangles are contiguous in its buffer, and the
     * depth buffer takes care of order. No index is written at all.
     */
    if( material->uniform )
    {
        gles2_queue_array_range(
            world,
            placement->binding,
            placement->absolute_base,
            (uint32_t)placement->face_count * 3u,
            material->cutout_count > 0u);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, placement->face_count);
        return;
    }

    /* Opaque and binary-cutout faces are depth-order independent, so they go
     * out in natural face order, split by which program they need. No winding
     * test: this lane has a depth buffer, and a back face loses the depth test
     * to the front face in front of it. */
    gles2_zbuffer_reserve_cutout_scratch(world, (uint32_t)placement->face_count * 3u);
    for( face_index = 0; face_index < placement->face_count; face_index++ )
    {
        uint32_t face = (uint32_t)face_index;
        uint8_t pass = material->face_passes[face];
        uint32_t base;
        uint16_t* destination;
        uint32_t* written;
        if( pass == GLES2_WORLD_FACE_OPAQUE )
        {
            destination = renderer->model_indices;
            written = &opaque_written;
        }
        else if( pass == GLES2_WORLD_FACE_CUTOUT )
        {
            destination = world->cutout_scratch;
            written = &cutout_written;
        }
        else
            continue;
        if( face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        destination[(*written)++] = (uint16_t)base;
        destination[(*written)++] = (uint16_t)(base + 1u);
        destination[(*written)++] = (uint16_t)(base + 2u);
    }
    if( opaque_written > 0u )
        gles2_queue_opaque_indices(
            world,
            placement->binding,
            placement->page_base,
            false,
            renderer->model_indices,
            opaque_written);
    if( cutout_written > 0u )
        gles2_queue_opaque_indices(
            world,
            placement->binding,
            placement->page_base,
            true,
            world->cutout_scratch,
            cutout_written);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, (opaque_written + cutout_written) / 3u);

    /* Only models with true blended faces pay the legacy depth/priority sort.
     * Filter the sorted result so opaque faces never enter the blend pass. */
    if( material->blended_count == 0u )
        return;
    sorted_face_count = ToriDraw_RenderModel2SortFacesWithTable(
        command->model, renderer->scene, renderer->kernel);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_SORTED_MODELS, 1);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    opaque_written = 0u;
    for( face_index = 0; face_index < sorted_face_count; face_index++ )
    {
        uint32_t face;
        uint32_t base;
        if( face_order[face_index] < 0 )
            continue;
        face = (uint32_t)face_order[face_index];
        if( face >= material->face_count ||
            material->face_passes[face] != GLES2_WORLD_FACE_BLENDED ||
            face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        renderer->model_indices[opaque_written++] = (uint16_t)base;
        renderer->model_indices[opaque_written++] = (uint16_t)(base + 1u);
        renderer->model_indices[opaque_written++] = (uint16_t)(base + 2u);
    }
    gles2_queue_alpha_submission(
        world,
        placement->binding,
        placement->page_base,
        renderer->scene->projected_vertex.z,
        renderer->model_indices,
        opaque_written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, opaque_written / 3u);
}

/* --- retained-geometry notifications ------------------------------------------ */

void
gles2_zbuffer_pose_baked(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    if( !world )
        return;
    (void)gles2_material_table_set(
        &world->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
gles2_zbuffer_element_dropped(struct ToriRS_GLES2* renderer, int element_id)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    if( !world )
        return;
    gles2_material_table_remove_element(&world->materials, element_id);
}

void
gles2_zbuffer_track_dropped(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    if( !world )
        return;
    gles2_material_table_remove_track(&world->materials, element_id, anim_index);
}

void
gles2_zbuffer_batch_pose_baked(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    if( !world )
        return;
    (void)gles2_material_table_set(
        &world->batch_materials, renderer, element_id, anim_index, pose_id, handle);
}

void
gles2_zbuffer_batch_dropped(struct ToriRS_GLES2* renderer, struct TRSPK_Batch16* cpu)
{
    struct GLES2ZBufferWorld* world = gles2_zbuffer_state(renderer);
    uint32_t entry_count;
    uint32_t entry_index;
    if( !world || !cpu )
        return;
    entry_count = trspk_batch16_entry_count(cpu);
    for( entry_index = 0u; entry_index < entry_count; entry_index++ )
    {
        const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(cpu, entry_index);
        if( entry )
            gles2_material_table_remove_element(&world->batch_materials, entry->element_id);
    }
}
