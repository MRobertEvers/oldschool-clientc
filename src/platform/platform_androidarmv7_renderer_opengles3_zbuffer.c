/**
 * The hardware depth-test world path for the WebGL2 renderer.
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
 * All of it lives in struct ES3ZBufferWorld, private to this file -- the
 * core only ever sees the pointer, and its being non-NULL is what selects this
 * implementation. platform_renderer_es3_painter.c is the
 * order-dependent alternative. The two are peers and neither calls the other.
 */

#include "platform/platform_renderer_es3_core.h"

#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "toridraw.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ES3WorldFacePass
{
    ES3_WORLD_FACE_SKIP = 0,
    ES3_WORLD_FACE_OPAQUE = 1,
    ES3_WORLD_FACE_CUTOUT = 2,
    ES3_WORLD_FACE_BLENDED = 3,
};

struct ES3MaterialPose
{
    uint8_t* face_passes;
    uint32_t face_count;
    uint32_t opaque_count;
    uint32_t cutout_count;
    uint32_t blended_count;
    /** Every face is opaque or cutout: the pose is one array range. */
    bool uniform;
};

struct ES3MaterialTrack
{
    struct ES3MaterialPose* poses;
    uint32_t pose_count;
    uint32_t pose_capacity;
};

struct ES3MaterialElement
{
    struct ES3MaterialTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct ES3MaterialTable
{
    struct ES3MaterialElement* elements;
    uint32_t element_count;
    uint32_t element_capacity;
};

struct ES3AlphaSubmission
{
    uint32_t binding;
    /** The vertex range these indices name, for glDrawRangeElements. */
    uint32_t vertex_min;
    uint32_t vertex_max;
    uint32_t index_offset;
    uint32_t index_count;
    int depth;
    uint32_t ordinal;
};

/**
 * One frame's indexed opaque-or-cutout faces for a single (binding, program)
 * pair. Models arrive in scene-traversal order, which ping-pongs across the
 * buffer; pushed straight onto the chain that order becomes one node per
 * model. Depth testing makes opaque submission order irrelevant, so the faces
 * are gathered per binding here and flushed as one node each. Slots keep
 * their storage across frames; only the counts reset.
 *
 * The GLES2 renderer needs one of these per PAGE as well, because a U16
 * index cannot cross one. With 32-bit indices the binding is the whole
 * partition, so a world that scattered across fifteen pages there is one
 * bucket -- one draw -- here.
 */
struct ES3OpaqueBucket
{
    uint32_t binding;
    bool cutout;
    uint32_t* indices;
    uint32_t index_count;
    uint32_t index_capacity;
    uint32_t vertex_min;
    uint32_t vertex_max;
};

/** One uniform pose, or after coalescing a run of them: `first` is the
 *  absolute vertex index in the binding's buffer. */
struct ES3ArrayRange
{
    uint32_t binding;
    uint32_t first;
    uint32_t count;
    bool cutout;
};

/** Everything depth mode owns that painter mode has no use for. */
struct ES3ZBufferWorld
{
    struct ES3MaterialTable materials;
    struct ES3MaterialTable batch_materials;
    struct ES3MaterialPose dynamic_material;

    uint32_t* alpha_indices;
    uint32_t alpha_index_count;
    uint32_t alpha_index_capacity;
    struct ES3AlphaSubmission* alpha_submissions;
    uint32_t alpha_submission_count;
    uint32_t alpha_submission_capacity;

    struct ES3OpaqueBucket* opaque_buckets;
    uint32_t opaque_bucket_count;
    uint32_t opaque_bucket_capacity;
    /* Scratch for one model's cutout indices while its opaque ones are in
     * the core's model_indices. */
    uint32_t* cutout_scratch;
    uint32_t cutout_scratch_capacity;

    struct ES3ArrayRange* array_ranges;
    uint32_t array_range_count;
    uint32_t array_range_capacity;
    /* After coalescing: the ranges actually drawn this frame. */
    uint32_t array_draw_count;
};

static struct ES3ZBufferWorld*
es3_zbuffer_state(struct ToriRS_ES3* renderer)
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
static enum ES3WorldFacePass
es3_textured_face_pass(struct ToriRS_ES3* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return ES3_WORLD_FACE_CUTOUT;
    texture = renderer->scene
        ? ToriDraw_TextureMapGet(
              &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id)
        : NULL;
    if( !texture || !texture->opaque || renderer->tex_slot_of_id[tex_id] < 0 )
        return ES3_WORLD_FACE_CUTOUT;
    return ES3_WORLD_FACE_OPAQUE;
}

static enum ES3WorldFacePass
es3_world_face_pass(
    struct ToriRS_ES3* renderer,
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
            return ES3_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return ES3_WORLD_FACE_SKIP;
        /* Animation baking has already written the pose's final face alpha.
         * Apply it before texture classification: textured faces can still be
         * truly translucent, and fully faded faces must not write depth. */
        alpha = model->face_alphas ? (uint8_t)(0xffu - model->face_alphas[face]) : 0xffu;
        if( alpha <= 1u )
            return ES3_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return ES3_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
            return es3_textured_face_pass(renderer, tex_id);
        return ES3_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        int tex_id;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return ES3_WORLD_FACE_SKIP;
        tex_id = ground->face_textures ? (int)ground->face_textures[face] : -1;
        if( tex_id >= 0 )
            return es3_textured_face_pass(renderer, tex_id);
        return ES3_WORLD_FACE_OPAQUE;
    }
    return ES3_WORLD_FACE_SKIP;
}

/* --- the material tables --------------------------------------------------- */

static void
es3_material_pose_clear(struct ES3MaterialPose* pose)
{
    assert(pose);
    free(pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

static bool
es3_material_pose_set(
    struct ES3MaterialPose* pose,
    struct ToriRS_ES3* renderer,
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
        enum ES3WorldFacePass pass = es3_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == ES3_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == ES3_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == ES3_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    pose->uniform = pose->opaque_count + pose->cutout_count == pose->face_count;
    return true;
}

static bool
es3_material_table_set(
    struct ES3MaterialTable* table,
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES3MaterialTrack* track;
    uint32_t needed;

    assert(table);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        pose_id < 0 )
        return false;
    needed = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct ES3MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct ES3MaterialElement*)realloc(
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
        struct ES3MaterialPose* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct ES3MaterialPose*)realloc(
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
    return es3_material_pose_set(&track->poses[pose_id], renderer, handle);
}

static const struct ES3MaterialPose*
es3_material_table_get(
    const struct ES3MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct ES3MaterialTrack* track;
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
es3_material_table_remove_track(
    struct ES3MaterialTable* table,
    int element_id,
    int anim_index)
{
    struct ES3MaterialTrack* track;
    uint32_t pose;
    assert(table);
    if( !table->elements || element_id < 0 ||
        (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    for( pose = 0u; pose < track->pose_count; pose++ )
        es3_material_pose_clear(&track->poses[pose]);
    track->pose_count = 0u;
}

static void
es3_material_table_remove_element(struct ES3MaterialTable* table, int element_id)
{
    int track;
    for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        es3_material_table_remove_track(table, element_id, track);
}

static void
es3_material_table_free(struct ES3MaterialTable* table)
{
    uint32_t element;
    int track;
    if( !table )
        return;
    for( element = 0u; element < table->element_count; element++ )
        es3_material_table_remove_element(table, (int)element);
    for( element = 0u; element < table->element_capacity; element++ )
        for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
            free(table->elements[element].tracks[track].poses);
    free(table->elements);
    memset(table, 0, sizeof(*table));
}

static uint64_t
es3_material_table_bytes(const struct ES3MaterialTable* table)
{
    uint64_t bytes = (uint64_t)table->element_capacity * sizeof(struct ES3MaterialElement);
    uint32_t element_index;
    uint32_t track;
    uint32_t pose;
    for( element_index = 0u; element_index < table->element_count; element_index++ )
        for( track = 0u; track < TRSPK_POSE_TRACK_COUNT; track++ )
        {
            const struct ES3MaterialTrack* material_track =
                &table->elements[element_index].tracks[track];
            bytes += (uint64_t)material_track->pose_capacity * sizeof(struct ES3MaterialPose);
            for( pose = 0u; pose < material_track->pose_count; pose++ )
                bytes += material_track->poses[pose].face_count;
        }
    return bytes;
}

/* --- per-frame queues ------------------------------------------------------ */

static void
es3_queue_alpha_submission(
    struct ES3ZBufferWorld* world,
    uint32_t binding,
    uint32_t vertex_min,
    uint32_t vertex_max,
    int depth,
    const uint32_t* indices,
    uint32_t index_count)
{
    struct ES3AlphaSubmission* submission;
    uint32_t needed_indices;

    assert(world);
    assert(indices);
    if( index_count == 0u )
        return;
    needed_indices = world->alpha_index_count + index_count;
    if( needed_indices > world->alpha_index_capacity )
    {
        uint32_t capacity = world->alpha_index_capacity ? world->alpha_index_capacity : 1024u;
        uint32_t* grown;
        while( capacity < needed_indices )
            capacity *= 2u;
        grown = (uint32_t*)realloc(world->alpha_indices, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->alpha_indices = grown;
        world->alpha_index_capacity = capacity;
    }
    if( world->alpha_submission_count >= world->alpha_submission_capacity )
    {
        uint32_t capacity =
            world->alpha_submission_capacity ? world->alpha_submission_capacity * 2u : 128u;
        struct ES3AlphaSubmission* grown = (struct ES3AlphaSubmission*)realloc(
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
    submission->vertex_min = vertex_min;
    submission->vertex_max = vertex_max;
    submission->index_offset = world->alpha_index_count;
    submission->index_count = index_count;
    submission->depth = depth;
    submission->ordinal = world->alpha_submission_count;
    world->alpha_index_count = needed_indices;
    world->alpha_submission_count++;
}

static void
es3_queue_opaque_indices(
    struct ES3ZBufferWorld* world,
    uint32_t binding,
    uint32_t vertex_min,
    uint32_t vertex_max,
    bool cutout,
    const uint32_t* indices,
    uint32_t index_count)
{
    struct ES3OpaqueBucket* bucket = NULL;
    uint32_t bucket_index;

    assert(world);
    assert(indices);
    if( index_count == 0u )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct ES3OpaqueBucket* candidate = &world->opaque_buckets[bucket_index];
        if( candidate->binding == binding && candidate->cutout == cutout )
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
            struct ES3OpaqueBucket* grown = (struct ES3OpaqueBucket*)realloc(
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
        bucket->cutout = cutout;
        bucket->index_count = 0u;
        bucket->vertex_min = vertex_min;
        bucket->vertex_max = vertex_max;
    }
    if( bucket->index_count == 0u )
    {
        bucket->vertex_min = vertex_min;
        bucket->vertex_max = vertex_max;
    }
    else
    {
        if( vertex_min < bucket->vertex_min )
            bucket->vertex_min = vertex_min;
        if( vertex_max > bucket->vertex_max )
            bucket->vertex_max = vertex_max;
    }
    if( bucket->index_count + index_count > bucket->index_capacity )
    {
        uint32_t capacity = bucket->index_capacity ? bucket->index_capacity : 1024u;
        uint32_t* grown;
        while( capacity < bucket->index_count + index_count )
            capacity *= 2u;
        grown = (uint32_t*)realloc(bucket->indices, (size_t)capacity * sizeof(*grown));
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
es3_queue_array_range(
    struct ES3ZBufferWorld* world,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout)
{
    struct ES3ArrayRange* range;
    assert(world);
    if( count == 0u )
        return;
    if( world->array_range_count >= world->array_range_capacity )
    {
        uint32_t capacity =
            world->array_range_capacity ? world->array_range_capacity * 2u : 1024u;
        struct ES3ArrayRange* grown = (struct ES3ArrayRange*)realloc(
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
es3_compare_opaque_bucket(const void* lhs, const void* rhs)
{
    const struct ES3OpaqueBucket* a = (const struct ES3OpaqueBucket*)lhs;
    const struct ES3OpaqueBucket* b = (const struct ES3OpaqueBucket*)rhs;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    return 0;
}

static int
es3_compare_array_range(const void* lhs, const void* rhs)
{
    const struct ES3ArrayRange* a = (const struct ES3ArrayRange*)lhs;
    const struct ES3ArrayRange* b = (const struct ES3ArrayRange*)rhs;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->first != b->first )
        return a->first < b->first ? -1 : 1;
    return 0;
}

static int
es3_compare_alpha_submission(const void* lhs, const void* rhs)
{
    const struct ES3AlphaSubmission* a = (const struct ES3AlphaSubmission*)lhs;
    const struct ES3AlphaSubmission* b = (const struct ES3AlphaSubmission*)rhs;
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
es3_push_alpha_submissions(struct ToriRS_ES3* renderer, struct ES3ZBufferWorld* world)
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
        es3_compare_alpha_submission);
    for( submission_index = 0u; submission_index < world->alpha_submission_count;
         submission_index++ )
    {
        const struct ES3AlphaSubmission* submission =
            &world->alpha_submissions[submission_index];
        es3_sequence_push_indexed(
            renderer,
            submission->binding,
            submission->vertex_min,
            submission->vertex_max,
            true,
            true,
            world->alpha_indices + submission->index_offset,
            submission->index_count);
    }
}

/* --- lifetime ----------------------------------------------------------------- */

bool
es3_zbuffer_create(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world;
    assert(renderer);
    world = (struct ES3ZBufferWorld*)calloc(1u, sizeof(struct ES3ZBufferWorld));
    assert(world);
    renderer->zbuffer = world;
    return true;
}

void
es3_zbuffer_destroy(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world;
    uint32_t bucket_index;
    if( !renderer || !renderer->zbuffer )
        return;
    world = renderer->zbuffer;
    es3_material_table_free(&world->materials);
    es3_material_table_free(&world->batch_materials);
    es3_material_pose_clear(&world->dynamic_material);
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
es3_zbuffer_report_memory(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    uint64_t bucket_bytes = 0u;
    uint64_t material_bytes;
    uint64_t batch_material_bytes;
    uint32_t bucket_index;
    if( !world )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_capacity; bucket_index++ )
        bucket_bytes += sizeof(world->opaque_buckets[bucket_index]) +
            (uint64_t)world->opaque_buckets[bucket_index].index_capacity * sizeof(uint32_t);
    material_bytes = es3_material_table_bytes(&world->materials);
    batch_material_bytes = es3_material_table_bytes(&world->batch_materials);
    /* TORIRS_LOG compiles out of a release build; computed regardless so the
     * function producing the figure is not dead code there. */
    (void)material_bytes;
    (void)batch_material_bytes;
    TORIRS_LOG("es3_mem: zb_materials          %10.2f MB\n"
               "es3_mem: zb_batch_materials    %10.2f MB\n"
               "es3_mem: zb_alpha_arena        %10.2f MB\n"
               "es3_mem: zb_opaque_buckets     %10.2f MB\n"
               "es3_mem: zb_array_ranges       %10.2f MB\n",
        (double)material_bytes / 1048576.0,
        (double)batch_material_bytes / 1048576.0,
        ((double)world->alpha_index_capacity * sizeof(uint32_t) +
            (double)world->alpha_submission_capacity * sizeof(struct ES3AlphaSubmission)) /
            1048576.0,
        (double)bucket_bytes / 1048576.0,
        (double)world->array_range_capacity * sizeof(struct ES3ArrayRange) / 1048576.0);
}

void
es3_zbuffer_reset_pass(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
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
es3_zbuffer_begin_pass(struct ToriRS_ES3* renderer)
{
    assert(renderer);
    es3_zbuffer_reset_pass(renderer);
    /*
     * Once per world pass, scissored to the world viewport so the UI drawn
     * around it is untouched. The depth mask is forced on for the clear:
     * glClear obeys it, and a clear issued while the mask happens to be false
     * silently does nothing -- which leaves last frame's depth to reject this
     * frame's geometry, and looks like random missing models.
     */
    es3_set_scissor(renderer, &renderer->world_viewport);
    es3_set_depth(renderer, true, true);
    glClear(GL_DEPTH_BUFFER_BIT);
    es3_set_scissor(renderer, NULL);
}

void
es3_zbuffer_setup_projection(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    float near_z;
    float far_z = ES3_WORLD_FAR;
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
        near_z = ES3_WORLD_NEAR;
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
 * TORIRS_ES3_CULL overrides it -- `ccw`, `cw`, or `none` to draw both
 * sides, which is the useful one when a model looks inside out. */
static int
es3_zbuffer_cull_mode(void)
{
    static int mode = -1;
    if( mode < 0 )
    {
        const char* value = getenv("TORIRS_ES3_CULL");
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
es3_zbuffer_apply_world_states(struct ToriRS_ES3* renderer)
{
    int mode = es3_zbuffer_cull_mode();
    assert(renderer);
    /* LEQUAL, not LESS: coplanar geometry submitted twice (a decor plane on
     * its floor tile) must keep the later one, which is what painter order did
     * and what the content is authored against. */
    es3_set_depth(renderer, true, true);
    glDepthFunc(GL_LEQUAL);
    if( mode == 0 )
        es3_set_cull(renderer, false);
    else
    {
        glFrontFace(mode == 1 ? GL_CCW : GL_CW);
        glCullFace(GL_BACK);
        es3_set_cull(renderer, true);
    }
}

void
es3_zbuffer_apply_pass_states(struct ToriRS_ES3* renderer, bool blended_pass)
{
    assert(renderer);
    /* The blended chain is already sorted back-to-front, so it blends against
     * the opaque result without contributing depth of its own. */
    es3_set_depth(renderer, true, !blended_pass);
    es3_set_blend(renderer, blended_pass);
}

void
es3_zbuffer_flush_opaque(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
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
            es3_compare_array_range);
        for( range_index = 1u; range_index < world->array_range_count; range_index++ )
        {
            struct ES3ArrayRange* previous = &world->array_ranges[write_index];
            const struct ES3ArrayRange* current = &world->array_ranges[range_index];
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
            const struct ES3ArrayRange* range = &world->array_ranges[range_index];
            es3_sequence_push_array(
                renderer, range->binding, range->first, range->count, range->cutout, false);
        }
        world->array_range_count = 0u;
    }

    /* Then the index buckets, in binding order so consecutive items stay on
     * one buffer binding. */
    if( world->opaque_bucket_count > 1u )
        qsort(
            world->opaque_buckets,
            world->opaque_bucket_count,
            sizeof(world->opaque_buckets[0]),
            es3_compare_opaque_bucket);
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct ES3OpaqueBucket* bucket = &world->opaque_buckets[bucket_index];
        if( bucket->index_count == 0u )
            continue;
        es3_sequence_push_indexed(
            renderer,
            bucket->binding,
            bucket->vertex_min,
            bucket->vertex_max,
            bucket->cutout,
            false,
            bucket->indices,
            bucket->index_count);
        bucket->index_count = 0u;
    }
    world->opaque_bucket_count = 0u;
}

void
es3_zbuffer_end_pass(struct ToriRS_ES3* renderer)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    if( !world )
        return;
    es3_push_alpha_submissions(renderer, world);
}

static void
es3_zbuffer_reserve_cutout_scratch(struct ES3ZBufferWorld* world, uint32_t needed)
{
    uint32_t* grown;
    uint32_t capacity;
    if( needed <= world->cutout_scratch_capacity )
        return;
    capacity = world->cutout_scratch_capacity ? world->cutout_scratch_capacity : 256u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (uint32_t*)realloc(world->cutout_scratch, (size_t)capacity * sizeof(*grown));
    assert(grown);
    world->cutout_scratch = grown;
    world->cutout_scratch_capacity = capacity;
}

void
es3_zbuffer_emit_model(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    const struct ES3MaterialPose* material = NULL;
    /* Indices are absolute in the binding's buffer: there is no page to be
     * local to, and no 65,535 ceiling to refuse a face over. */
    const uint32_t vertex_base = placement->absolute_base;
    uint32_t opaque_written = 0u;
    uint32_t cutout_written = 0u;
    int sorted_face_count;
    const int* face_order;
    int projected_depth;
    int face_index;

    assert(command);
    assert(placement);
    if( !world )
        return;
    if( placement->dynamic )
    {
        if( es3_material_pose_set(&world->dynamic_material, renderer, command->model) )
            material = &world->dynamic_material;
    }
    else
        material = es3_material_table_get(
            placement->binding >= ES3_STATIC_PAGE_BINDING ? &world->batch_materials
                                                            : &world->materials,
            command->element_id,
            placement->anim_index,
            placement->pose_id);
    if( !material &&
        es3_material_pose_set(&world->dynamic_material, renderer, command->model) )
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
        es3_queue_array_range(
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
    es3_zbuffer_reserve_cutout_scratch(world, (uint32_t)placement->face_count * 3u);
    for( face_index = 0; face_index < placement->face_count; face_index++ )
    {
        uint32_t face = (uint32_t)face_index;
        uint8_t pass = material->face_passes[face];
        uint32_t base;
        uint32_t* destination;
        uint32_t* written;
        if( pass == ES3_WORLD_FACE_OPAQUE )
        {
            destination = renderer->model_indices;
            written = &opaque_written;
        }
        else if( pass == ES3_WORLD_FACE_CUTOUT )
        {
            destination = world->cutout_scratch;
            written = &cutout_written;
        }
        else
            continue;
        if( face > (UINT32_MAX - vertex_base - 2u) / 3u )
            continue;
        base = vertex_base + face * 3u;
        destination[(*written)++] = base;
        destination[(*written)++] = base + 1u;
        destination[(*written)++] = base + 2u;
    }
    if( opaque_written > 0u )
        es3_queue_opaque_indices(
            world,
            placement->binding,
            vertex_base,
            vertex_base + (uint32_t)placement->face_count * 3u - 1u,
            false,
            renderer->model_indices,
            opaque_written);
    if( cutout_written > 0u )
        es3_queue_opaque_indices(
            world,
            placement->binding,
            vertex_base,
            vertex_base + (uint32_t)placement->face_count * 3u - 1u,
            true,
            world->cutout_scratch,
            cutout_written);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, (opaque_written + cutout_written) / 3u);

    /* Only models with true blended faces pay the legacy depth/priority sort.
     * Filter the sorted result so opaque faces never enter the blend pass. */
    if( material->blended_count == 0u )
        return;
    if( placement->face_order )
    {
        /* Sorted already, by whoever ran the stage. */
        sorted_face_count = placement->sorted_face_count;
        face_order = placement->face_order;
        projected_depth = placement->projected_depth;
    }
    else
    {
        if( !placement->projected_in_scene )
        {
            /* A stage source classified this model as having no blended
             * face and left it unsorted, and the material table disagrees.
             * The scene's bench holds some other model's projection, so
             * project again here before sorting. Counted: the two tests are
             * meant to agree, and this should stay at zero. */
            struct ToriDraw_Position position = command->position;
            renderer->stage_reprojected_models++;
            if( ToriDraw_RenderModel1ProjectWithTable(
                    command->model,
                    renderer->scene,
                    &position,
                    &renderer->current_3d.view_port,
                    &renderer->current_3d.camera,
                    renderer->kernel) != TORIDRAW_CULL_VISIBLE )
                return;
        }
        sorted_face_count = ToriDraw_RenderModel2SortFacesWithTable(
            command->model, renderer->scene, renderer->kernel);
        face_order = ToriDraw_FaceOrder(renderer->scene);
        projected_depth = renderer->scene->projected_vertex.z;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_SORTED_MODELS, 1);
    if( sorted_face_count <= 0 )
        return;
    opaque_written = 0u;
    for( face_index = 0; face_index < sorted_face_count; face_index++ )
    {
        uint32_t face;
        uint32_t base;
        if( face_order[face_index] < 0 )
            continue;
        face = (uint32_t)face_order[face_index];
        if( face >= material->face_count ||
            material->face_passes[face] != ES3_WORLD_FACE_BLENDED ||
            face > (UINT32_MAX - vertex_base - 2u) / 3u )
            continue;
        base = vertex_base + face * 3u;
        renderer->model_indices[opaque_written++] = base;
        renderer->model_indices[opaque_written++] = base + 1u;
        renderer->model_indices[opaque_written++] = base + 2u;
    }
    es3_queue_alpha_submission(
        world,
        placement->binding,
        vertex_base,
        vertex_base + (uint32_t)placement->face_count * 3u - 1u,
        projected_depth,
        renderer->model_indices,
        opaque_written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, opaque_written / 3u);
}

/* --- retained-geometry notifications ------------------------------------------ */

void
es3_zbuffer_pose_baked(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    if( !world )
        return;
    (void)es3_material_table_set(
        &world->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
es3_zbuffer_element_dropped(struct ToriRS_ES3* renderer, int element_id)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    if( !world )
        return;
    es3_material_table_remove_element(&world->materials, element_id);
}

void
es3_zbuffer_track_dropped(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    if( !world )
        return;
    es3_material_table_remove_track(&world->materials, element_id, anim_index);
}

void
es3_zbuffer_batch_pose_baked(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    if( !world )
        return;
    (void)es3_material_table_set(
        &world->batch_materials, renderer, element_id, anim_index, pose_id, handle);
}

void
es3_zbuffer_batch_dropped(struct ToriRS_ES3* renderer, struct TRSPK_Batch16* cpu)
{
    struct ES3ZBufferWorld* world = es3_zbuffer_state(renderer);
    uint32_t entry_count;
    uint32_t entry_index;
    if( !world || !cpu )
        return;
    entry_count = trspk_batch16_entry_count(cpu);
    for( entry_index = 0u; entry_index < entry_count; entry_index++ )
    {
        const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(cpu, entry_index);
        if( entry )
            es3_material_table_remove_element(&world->batch_materials, entry->element_id);
    }
}
