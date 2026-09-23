/**
 * The depth-buffered renderer for OpenGL ES 2.0 -- a WHOLE renderer, not a hook
 * layer.
 *
 * It owns its own frame: the GL context it asks for (16 depth bits), a
 * material pre-pass, array ranges for uniform poses, a sorted blended pass,
 * begin_3d, draw_model, end_3d, the draw sequence's issue loop, the command
 * dispatch and the frame lifecycle. It COMPOSES those out of platform_androidarmv7_renderer_opengles2_core.c,
 * which is a toolkit -- the scene walk, the bake, the arenas, the streams,
 * the uploads, the shaders and the state cache, with no test anywhere in it
 * for which world path is running. platform_androidarmv7_renderer_opengles2_painter.c is the painter's-algorithm
 * peer, written the same way; neither calls the other.
 *
 * The material table below is why the retained-store commands are here and
 * not in the toolkit: a pose classified opaque/cutout/blended has to be
 * reclassified whenever the arena's copy of it is baked or dropped, and
 * only this renderer keeps that table. The painter has no equivalent and
 * pays nothing for it -- which is the point of the split, and why every
 * ::zbuffer test that used to gate these calls is gone rather than moved.
 */

#include "es2/es2_core.h"

#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "toridraw.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/trspk_math.h"
#include "engine/boot_bar.h"
#include "painters/painters.h"
#include "platform/platform_androidarmv7_renderer_opengles2_placement.h"
#include "es2/es2_indices.h"
#include "toridraw_element_id.h"
#include "toridraw_math.h"

/* es2z_draw_model rebakes a missing animation through the load path. */
static void
es2z_animation_load(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command);

enum ES2WorldFacePass
{
    ES2_WORLD_FACE_SKIP = 0,
    ES2_WORLD_FACE_OPAQUE = 1,
    ES2_WORLD_FACE_CUTOUT = 2,
    ES2_WORLD_FACE_BLENDED = 3,
};

struct ES2MaterialPose
{
    uint8_t* face_passes;
    uint32_t face_count;
    uint32_t opaque_count;
    uint32_t cutout_count;
    uint32_t blended_count;
    /** Every face is opaque or cutout: the pose is one array range. */
    bool uniform;
};

struct ES2MaterialTrack
{
    struct ES2MaterialPose* poses;
    uint32_t pose_count;
    uint32_t pose_capacity;
};

struct ES2MaterialElement
{
    struct ES2MaterialTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct ES2MaterialTable
{
    struct ES2MaterialElement* elements;
    uint32_t element_count;
    uint32_t element_capacity;
};

struct ES2AlphaSubmission
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
struct ES2OpaqueBucket
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
struct ES2ArrayRange
{
    uint32_t binding;
    uint32_t first;
    uint32_t count;
    bool cutout;
};

/** Everything depth mode owns that painter mode has no use for. */
struct ES2ZBufferWorld
{
    struct ES2MaterialTable materials;
    struct ES2MaterialTable batch_materials;
    struct ES2MaterialPose dynamic_material;

    uint16_t* alpha_indices;
    uint32_t alpha_index_count;
    uint32_t alpha_index_capacity;
    struct ES2AlphaSubmission* alpha_submissions;
    uint32_t alpha_submission_count;
    uint32_t alpha_submission_capacity;

    struct ES2OpaqueBucket* opaque_buckets;
    uint32_t opaque_bucket_count;
    uint32_t opaque_bucket_capacity;
    /* Scratch for one model's cutout indices while its opaque ones are in
     * the core's model_indices. */
    uint16_t* cutout_scratch;
    uint32_t cutout_scratch_capacity;

    struct ES2ArrayRange* array_ranges;
    uint32_t array_range_count;
    uint32_t array_range_capacity;
    /* After coalescing: the ranges actually drawn this frame. */
    uint32_t array_draw_count;
};

static struct ES2ZBufferWorld*
es2_zbuffer_state(struct TRSPK_Renderer_ES2* renderer)
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
static enum ES2WorldFacePass
es2_textured_face_pass(struct TRSPK_Renderer_ES2* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return ES2_WORLD_FACE_CUTOUT;
    texture = renderer->scene
        ? ToriDraw_TextureMapGet(
              &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id)
        : NULL;
    if( !texture || !texture->opaque || renderer->tex_slot_of_id[tex_id] < 0 )
        return ES2_WORLD_FACE_CUTOUT;
    return ES2_WORLD_FACE_OPAQUE;
}

static enum ES2WorldFacePass
es2_world_face_pass(
    struct TRSPK_Renderer_ES2* renderer,
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
            return ES2_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return ES2_WORLD_FACE_SKIP;
        /* Animation baking has already written the pose's final face alpha.
         * Apply it before texture classification: textured faces can still be
         * truly translucent, and fully faded faces must not write depth. */
        alpha = model->face_alphas ? (uint8_t)(0xffu - model->face_alphas[face]) : 0xffu;
        if( alpha <= 1u )
            return ES2_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return ES2_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
            return es2_textured_face_pass(renderer, tex_id);
        return ES2_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        int tex_id;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return ES2_WORLD_FACE_SKIP;
        tex_id = ground->face_textures ? (int)ground->face_textures[face] : -1;
        if( tex_id >= 0 )
            return es2_textured_face_pass(renderer, tex_id);
        return ES2_WORLD_FACE_OPAQUE;
    }
    return ES2_WORLD_FACE_SKIP;
}

/* --- the material tables --------------------------------------------------- */

static void
es2_material_pose_clear(struct ES2MaterialPose* pose)
{
    assert(pose);
    free(pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

static bool
es2_material_pose_set(
    struct ES2MaterialPose* pose,
    struct TRSPK_Renderer_ES2* renderer,
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
        enum ES2WorldFacePass pass = es2_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == ES2_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == ES2_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == ES2_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    pose->uniform = pose->opaque_count + pose->cutout_count == pose->face_count;
    return true;
}

static bool
es2_material_table_set(
    struct ES2MaterialTable* table,
    struct TRSPK_Renderer_ES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES2MaterialTrack* track;
    uint32_t needed;

    assert(table);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        pose_id < 0 )
        return false;
    needed = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct ES2MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct ES2MaterialElement*)realloc(
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
        struct ES2MaterialPose* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct ES2MaterialPose*)realloc(
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
    return es2_material_pose_set(&track->poses[pose_id], renderer, handle);
}

static const struct ES2MaterialPose*
es2_material_table_get(
    const struct ES2MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct ES2MaterialTrack* track;
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
es2_material_table_remove_track(
    struct ES2MaterialTable* table,
    int element_id,
    int anim_index)
{
    struct ES2MaterialTrack* track;
    uint32_t pose;
    assert(table);
    if( !table->elements || element_id < 0 ||
        (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    for( pose = 0u; pose < track->pose_count; pose++ )
        es2_material_pose_clear(&track->poses[pose]);
    track->pose_count = 0u;
}

static void
es2_material_table_remove_element(struct ES2MaterialTable* table, int element_id)
{
    int track;
    for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        es2_material_table_remove_track(table, element_id, track);
}

static void
es2_material_table_free(struct ES2MaterialTable* table)
{
    uint32_t element;
    int track;
    if( !table )
        return;
    for( element = 0u; element < table->element_count; element++ )
        es2_material_table_remove_element(table, (int)element);
    for( element = 0u; element < table->element_capacity; element++ )
        for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
            free(table->elements[element].tracks[track].poses);
    free(table->elements);
    memset(table, 0, sizeof(*table));
}

static uint64_t
es2_material_table_bytes(const struct ES2MaterialTable* table)
{
    uint64_t bytes = (uint64_t)table->element_capacity * sizeof(struct ES2MaterialElement);
    uint32_t element_index;
    uint32_t track;
    uint32_t pose;
    for( element_index = 0u; element_index < table->element_count; element_index++ )
        for( track = 0u; track < TRSPK_POSE_TRACK_COUNT; track++ )
        {
            const struct ES2MaterialTrack* material_track =
                &table->elements[element_index].tracks[track];
            bytes += (uint64_t)material_track->pose_capacity * sizeof(struct ES2MaterialPose);
            for( pose = 0u; pose < material_track->pose_count; pose++ )
                bytes += material_track->poses[pose].face_count;
        }
    return bytes;
}

/* --- per-frame queues ------------------------------------------------------ */

static void
es2_queue_alpha_submission(
    struct ES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t page_base,
    int depth,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct ES2AlphaSubmission* submission;
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
        struct ES2AlphaSubmission* grown = (struct ES2AlphaSubmission*)realloc(
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
es2_queue_opaque_indices(
    struct ES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    const uint16_t* indices,
    uint32_t index_count)
{
    struct ES2OpaqueBucket* bucket = NULL;
    uint32_t bucket_index;

    assert(world);
    assert(indices);
    if( index_count == 0u )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct ES2OpaqueBucket* candidate = &world->opaque_buckets[bucket_index];
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
            struct ES2OpaqueBucket* grown = (struct ES2OpaqueBucket*)realloc(
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
es2_queue_array_range(
    struct ES2ZBufferWorld* world,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout)
{
    struct ES2ArrayRange* range;
    assert(world);
    if( count == 0u )
        return;
    if( world->array_range_count >= world->array_range_capacity )
    {
        uint32_t capacity =
            world->array_range_capacity ? world->array_range_capacity * 2u : 1024u;
        struct ES2ArrayRange* grown = (struct ES2ArrayRange*)realloc(
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
es2_compare_opaque_bucket(const void* lhs, const void* rhs)
{
    const struct ES2OpaqueBucket* a = (const struct ES2OpaqueBucket*)lhs;
    const struct ES2OpaqueBucket* b = (const struct ES2OpaqueBucket*)rhs;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->page_base != b->page_base )
        return a->page_base < b->page_base ? -1 : 1;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    return 0;
}

static int
es2_compare_array_range(const void* lhs, const void* rhs)
{
    const struct ES2ArrayRange* a = (const struct ES2ArrayRange*)lhs;
    const struct ES2ArrayRange* b = (const struct ES2ArrayRange*)rhs;
    if( a->cutout != b->cutout )
        return a->cutout ? 1 : -1;
    if( a->binding != b->binding )
        return a->binding < b->binding ? -1 : 1;
    if( a->first != b->first )
        return a->first < b->first ? -1 : 1;
    return 0;
}

static int
es2_compare_alpha_submission(const void* lhs, const void* rhs)
{
    const struct ES2AlphaSubmission* a = (const struct ES2AlphaSubmission*)lhs;
    const struct ES2AlphaSubmission* b = (const struct ES2AlphaSubmission*)rhs;
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
es2_push_alpha_submissions(struct TRSPK_Renderer_ES2* renderer, struct ES2ZBufferWorld* world)
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
        es2_compare_alpha_submission);
    for( submission_index = 0u; submission_index < world->alpha_submission_count;
         submission_index++ )
    {
        const struct ES2AlphaSubmission* submission =
            &world->alpha_submissions[submission_index];
        es2_sequence_push_indexed(
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
es2_zbuffer_create(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world;
    assert(renderer);
    world = (struct ES2ZBufferWorld*)calloc(1u, sizeof(struct ES2ZBufferWorld));
    assert(world);
    renderer->zbuffer = world;
    return true;
}

void
es2_zbuffer_destroy(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world;
    uint32_t bucket_index;
    if( !renderer || !renderer->zbuffer )
        return;
    world = renderer->zbuffer;
    es2_material_table_free(&world->materials);
    es2_material_table_free(&world->batch_materials);
    es2_material_pose_clear(&world->dynamic_material);
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
es2_zbuffer_report_memory(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    uint64_t bucket_bytes = 0u;
    uint64_t material_bytes;
    uint64_t batch_material_bytes;
    uint32_t bucket_index;
    if( !world )
        return;
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_capacity; bucket_index++ )
        bucket_bytes += sizeof(world->opaque_buckets[bucket_index]) +
            (uint64_t)world->opaque_buckets[bucket_index].index_capacity * sizeof(uint16_t);
    material_bytes = es2_material_table_bytes(&world->materials);
    batch_material_bytes = es2_material_table_bytes(&world->batch_materials);
    /* TORIRS_LOG compiles out of a release build; computed regardless so the
     * function producing the figure is not dead code there. */
    (void)material_bytes;
    (void)batch_material_bytes;
    TORIRS_LOG("es2_mem: zb_materials          %10.2f MB\n"
               "es2_mem: zb_batch_materials    %10.2f MB\n"
               "es2_mem: zb_alpha_arena        %10.2f MB\n"
               "es2_mem: zb_opaque_buckets     %10.2f MB\n"
               "es2_mem: zb_array_ranges       %10.2f MB\n",
        (double)material_bytes / 1048576.0,
        (double)batch_material_bytes / 1048576.0,
        ((double)world->alpha_index_capacity * sizeof(uint16_t) +
            (double)world->alpha_submission_capacity * sizeof(struct ES2AlphaSubmission)) /
            1048576.0,
        (double)bucket_bytes / 1048576.0,
        (double)world->array_range_capacity * sizeof(struct ES2ArrayRange) / 1048576.0);
}

void
es2_zbuffer_reset_pass(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    uint32_t bucket_index;
    assert(world);
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
es2_zbuffer_begin_pass(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    es2_zbuffer_reset_pass(renderer);
    /*
     * Once per world pass, scissored to the world viewport so the UI drawn
     * around it is untouched. The depth mask is forced on for the clear:
     * glClear obeys it, and a clear issued while the mask happens to be false
     * silently does nothing -- which leaves last frame's depth to reject this
     * frame's geometry, and looks like random missing models.
     */
    es2_set_scissor(renderer, &renderer->world_viewport);
    es2_set_depth(renderer, true, true);
    glClear(GL_DEPTH_BUFFER_BIT);
    es2_set_scissor(renderer, NULL);
}

void
es2_zbuffer_setup_projection(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    float near_z;
    float far_z = ES2_WORLD_FAR;
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
        near_z = ES2_WORLD_NEAR;
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
es2_zbuffer_cull_mode(void)
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
es2_zbuffer_apply_world_states(struct TRSPK_Renderer_ES2* renderer)
{
    int mode = es2_zbuffer_cull_mode();
    assert(renderer);
    /* LEQUAL, not LESS: coplanar geometry submitted twice (a decor plane on
     * its floor tile) must keep the later one, which is what painter order did
     * and what the content is authored against. */
    es2_set_depth(renderer, true, true);
    glDepthFunc(GL_LEQUAL);
    if( mode == 0 )
        es2_set_cull(renderer, false);
    else
    {
        glFrontFace(mode == 1 ? GL_CCW : GL_CW);
        glCullFace(GL_BACK);
        es2_set_cull(renderer, true);
    }
}

void
es2_zbuffer_apply_pass_states(struct TRSPK_Renderer_ES2* renderer, bool blended_pass)
{
    assert(renderer);
    /* The blended chain is already sorted back-to-front, so it blends against
     * the opaque result without contributing depth of its own. */
    es2_set_depth(renderer, true, !blended_pass);
    es2_set_blend(renderer, blended_pass);
}

void
es2_zbuffer_flush_opaque(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    uint32_t bucket_index;
    uint32_t range_index;
    uint32_t write_index = 0u;
    assert(world);

    /* Array ranges first: sorted so that contiguous poses in one buffer
     * become one draw, and so the plain program runs before the cutout one.
     * They are the bulk of the static world. */
    if( world->array_range_count > 0u )
    {
        qsort(
            world->array_ranges,
            world->array_range_count,
            sizeof(world->array_ranges[0]),
            es2_compare_array_range);
        for( range_index = 1u; range_index < world->array_range_count; range_index++ )
        {
            struct ES2ArrayRange* previous = &world->array_ranges[write_index];
            const struct ES2ArrayRange* current = &world->array_ranges[range_index];
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
            const struct ES2ArrayRange* range = &world->array_ranges[range_index];
            es2_sequence_push_array(
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
            es2_compare_opaque_bucket);
    for( bucket_index = 0u; bucket_index < world->opaque_bucket_count; bucket_index++ )
    {
        struct ES2OpaqueBucket* bucket = &world->opaque_buckets[bucket_index];
        if( bucket->index_count == 0u )
            continue;
        es2_sequence_push_indexed(
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
es2_zbuffer_end_pass(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    assert(world);
    es2_push_alpha_submissions(renderer, world);
}

static void
es2_zbuffer_reserve_cutout_scratch(struct ES2ZBufferWorld* world, uint32_t needed)
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
es2_zbuffer_emit_model(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES2ModelPlacement* placement)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    const struct ES2MaterialPose* material = NULL;
    const uint32_t local_base = placement->local_base;
    uint32_t opaque_written = 0u;
    uint32_t cutout_written = 0u;
    int sorted_face_count;
    const int* face_order;
    int projected_depth;
    int face_index;

    assert(command);
    assert(placement);
    assert(world);
    if( placement->dynamic )
    {
        if( es2_material_pose_set(&world->dynamic_material, renderer, command->model) )
            material = &world->dynamic_material;
    }
    else
        material = es2_material_table_get(
            placement->binding >= ES2_STATIC_PAGE_BINDING ? &world->batch_materials
                                                            : &world->materials,
            command->element_id,
            placement->anim_index,
            placement->pose_id);
    if( !material &&
        es2_material_pose_set(&world->dynamic_material, renderer, command->model) )
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
        es2_queue_array_range(
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
    es2_zbuffer_reserve_cutout_scratch(world, (uint32_t)placement->face_count * 3u);
    for( face_index = 0; face_index < placement->face_count; face_index++ )
    {
        uint32_t face = (uint32_t)face_index;
        uint8_t pass = material->face_passes[face];
        uint32_t base;
        uint16_t* destination;
        uint32_t* written;
        if( pass == ES2_WORLD_FACE_OPAQUE )
        {
            destination = renderer->model_indices;
            written = &opaque_written;
        }
        else if( pass == ES2_WORLD_FACE_CUTOUT )
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
        es2_queue_opaque_indices(
            world,
            placement->binding,
            placement->page_base,
            false,
            renderer->model_indices,
            opaque_written);
    if( cutout_written > 0u )
        es2_queue_opaque_indices(
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
            material->face_passes[face] != ES2_WORLD_FACE_BLENDED ||
            face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        renderer->model_indices[opaque_written++] = (uint16_t)base;
        renderer->model_indices[opaque_written++] = (uint16_t)(base + 1u);
        renderer->model_indices[opaque_written++] = (uint16_t)(base + 2u);
    }
    es2_queue_alpha_submission(
        world,
        placement->binding,
        placement->page_base,
        projected_depth,
        renderer->model_indices,
        opaque_written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, opaque_written / 3u);
}

/* --- retained-geometry notifications ------------------------------------------ */

void
es2_zbuffer_pose_baked(
    struct TRSPK_Renderer_ES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    assert(world);
    (void)es2_material_table_set(
        &world->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
es2_zbuffer_element_dropped(struct TRSPK_Renderer_ES2* renderer, int element_id)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    assert(world);
    es2_material_table_remove_element(&world->materials, element_id);
}

void
es2_zbuffer_track_dropped(
    struct TRSPK_Renderer_ES2* renderer,
    int element_id,
    int anim_index)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    assert(world);
    es2_material_table_remove_track(&world->materials, element_id, anim_index);
}

void
es2_zbuffer_batch_pose_baked(
    struct TRSPK_Renderer_ES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    assert(world);
    (void)es2_material_table_set(
        &world->batch_materials, renderer, element_id, anim_index, pose_id, handle);
}

void
es2_zbuffer_batch_dropped(struct TRSPK_Renderer_ES2* renderer, struct TRSPK_Batch16* cpu)
{
    struct ES2ZBufferWorld* world = es2_zbuffer_state(renderer);
    uint32_t entry_count;
    uint32_t entry_index;
    assert(world);
    if( !cpu )
        return;
    entry_count = trspk_batch16_entry_count(cpu);
    for( entry_index = 0u; entry_index < entry_count; entry_index++ )
    {
        const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(cpu, entry_index);
        if( entry )
            es2_material_table_remove_element(&world->batch_materials, entry->element_id);
    }
}

void es2z_sequence_issue(struct TRSPK_Renderer_ES2* renderer,uint32_t index_base_bytes)
{
    uint32_t item_index,draw_calls=0;int program_cutout=-1,pass_blended=-1;
    es2_zbuffer_apply_world_states(renderer);

    for( item_index = 0u; item_index < renderer->draw_item_count; item_index++ )
    {
        const struct ES2DrawItem* item = &renderer->draw_items[item_index];
        if( pass_blended != (int)item->blended )
        {
            es2_zbuffer_apply_pass_states(renderer, item->blended != 0u);
            pass_blended = (int)item->blended;
        }
        if( program_cutout != (int)item->cutout )
        {
            es2_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !es2_bind_stream(renderer, item->binding, item->indexed ? item->page_base : 0u) )
            continue;
        if( item->indexed )
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)item->count,
                GL_UNSIGNED_SHORT,
                (const void*)(uintptr_t)(index_base_bytes + (size_t)item->first * sizeof(uint16_t)));
        else
            glDrawArrays(GL_TRIANGLES, (GLint)item->first, (GLsizei)item->count);
        draw_calls++;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
}

void
es2z_sequence_draw(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t index_base_bytes = 0u;

    assert(renderer);
    if( renderer->draw_item_count == 0u )
        return;
    if( renderer->ibo_staging_count > 0u )
    {
        uint32_t bytes = renderer->ibo_staging_count * (uint32_t)sizeof(uint16_t);
        index_base_bytes = es2_stream_set_append(
            &renderer->index_stream,
            renderer->frame_slot,
            GL_ELEMENT_ARRAY_BUFFER,
            ES2_INDEX_STREAM_INIT_BYTES,
            renderer->ibo_staging,
            bytes,
            false);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)bytes);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);
    }

#if defined(TORIRS_SHADER_PROBE)
    es2_shader_probe(renderer,index_base_bytes);
#endif
    es2z_sequence_issue(renderer,index_base_bytes);
}

void
es2z_begin_3d(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Begin3D* command)
{
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    int left;
    int top;
    int right;
    int bottom;
    uint32_t group;

    assert(renderer);
    assert(command);
    if( !renderer->gl_context )
        return;
    es2_sequence_reset(renderer);
    renderer->current_3d = *command;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    g_chain_pass++;
#endif
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureBeginPass();
#endif
    renderer->has_3d = true;
    renderer->in3d = true;
    /* Publish the prepared camera block: the prepared projection kernels are
     * gated on this pointer being the one the projection is called with. */
    if( renderer->scene )
        ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
    /* Every scene event of the frame has been dispatched by now (the frame
     * drains them before its first non-event command): a stage source may
     * start reading and posing models. */
    if( renderer->model_stage_source && renderer->model_stage_source->begin_3d )
        renderer->model_stage_source->begin_3d(renderer->model_stage_source->user, command);

    viewport = &renderer->current_3d.view_port;
    pass_w = viewport->width > 0 ? viewport->width : renderer->width;
    pass_h = viewport->height > 0 ? viewport->height : renderer->height;
    logical_x = viewport->x_center - pass_w / 2;
    logical_y = viewport->y_center - pass_h / 2;
    left = renderer->letterbox_x + (int)((int64_t)logical_x * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_top + (int)((int64_t)logical_y * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
        (int)((int64_t)(logical_x + pass_w) * renderer->letterbox_width / renderer->width);
    bottom = renderer->letterbox_top +
        (int)((int64_t)(logical_y + pass_h) * renderer->letterbox_height / renderer->height);
    if( right <= left )
        right = left + 1;
    if( bottom <= top )
        bottom = top + 1;
    renderer->world_viewport.x = left;
    renderer->world_viewport.y = renderer->target_height - bottom;
    renderer->world_viewport.width = right - left;
    renderer->world_viewport.height = bottom - top;
    glViewport(
        renderer->world_viewport.x,
        renderer->world_viewport.y,
        renderer->world_viewport.width,
        renderer->world_viewport.height);
    es2_zbuffer_begin_pass(renderer);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->projection,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h,
        (int)command->camera.projection_mode,
        command->camera.projection_scale,
        command->camera.fov_rpi2048,
        command->camera.parallel_zoom16);
    es2_zbuffer_setup_projection(renderer, command);
    es2_mat4_multiply(renderer->projection, renderer->view, renderer->model_view_projection);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            es2_reset_group(&renderer->groups[group]);
}

void
es2z_draw_model(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Model* command)
{
    struct ToriDraw_Position projected_position;
    struct ES2ModelPlacement placement;
    struct ES2ModelStage stage;
    struct ES2StaticPrimary static_placement;
    int static_state;
    bool staged = false;
    const int* face_order;
    bool projected_in_scene;
    int projected_depth;
    int face_count;
    int sorted_face_count = 0;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t page_base = 0u;
    uint32_t local_base;
    uint32_t binding;
    uint32_t group;

    assert(renderer);
    assert(command);
    /* The stage source, when one is installed, is asked about EVERY model
     * command before any early return: it hands results out in dispatch
     * order and pairs them with the asks by count. */
    if( renderer->model_stage_source )
        staged = renderer->model_stage_source->take(
            renderer->model_stage_source->user, command, &stage);
    if( !renderer->has_3d || !renderer->scene || command->model.kind == TORIDRAWMK_NONE )
        return;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    es2_chain_capture(renderer, command);
#endif
    placement.page_id = UINT32_MAX;
    placement.batch_slot = UINT32_MAX;
    placement.entry_index = UINT32_MAX;
    placement.entry_vertex_count = 0u;
    if( staged )
    {
        /* Pose, cull, projection, pick test and sort were done by the source
         * (the dual-core lane's worker, on its own scratch view of the
         * scene); this thread consumes. The pose the source applied is the
         * one the bakes below read -- its results were published after it. */
        if( stage.cull != TORIDRAW_CULL_VISIBLE )
            return;
        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            stage.pick_hit )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;
        face_count = trspk_toridraw_face_count(command->model);
        sorted_face_count = stage.sorted ? stage.sorted_face_count : 0;
        face_order = stage.sorted ? stage.face_order : NULL;
        projected_depth = stage.projected_depth;
        projected_in_scene = false;
    }
    else
    {
        if( !renderer->poses_prepared && command->animation && command->element_id >= 0 )
        {
            es2_apply_animation(renderer, command);
        }
        projected_position = command->position;
        if( ToriDraw_RenderModel1ProjectWithTable(
                command->model,
                renderer->scene,
                &projected_position,
                &renderer->current_3d.view_port,
                &renderer->current_3d.camera,
                renderer->kernel) != TORIDRAW_CULL_VISIBLE )
            return;

        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            (command->pick_aabb
                 ? ToriDraw_ProjectedModelContainsAabb(
                       renderer->scene, renderer->pick_mouse_x, renderer->pick_mouse_y)
                 : command->pick_terrain
                     ? ToriDraw_ProjectedTileMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)
                     : ToriDraw_ProjectedModelMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)) )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;

        /* The depth path classifies per face during emission and needs no
         * order up front. */
        face_count = trspk_toridraw_face_count(command->model);
        face_order = ToriDraw_FaceOrder(renderer->scene);
        projected_depth = renderer->scene->projected_vertex.z;
        projected_in_scene = true;
    }
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = es2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    pose_id = command->animation && command->anim_frame >= 0 ? command->anim_frame : 0;
    if( command->animation && command->animation->frame_count > 0 &&
        pose_id >= command->animation->frame_count )
        pose_id = 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    binding = group;
    if( dynamic )
    {
        vertex_base = es2_bake_into_arena(
            renderer,
            &renderer->groups[group],
            command->element_id,
            anim_index,
            pose_id,
            command->model,
            &command->world_position,
            false);
    }
    else if( (static_state=es2_static_resolve_recorded(renderer,command->element_id,anim_index,pose_id,&static_placement))!=0 )
    {
        if( static_state<0 ) return;
        binding=ES2_STATIC_PAGE_BINDING;
        page_base=static_placement.page_base;
        placement.page_id=static_placement.page_id;
        placement.batch_slot=static_placement.batch_slot;
        placement.entry_index=static_placement.entry_index;
        placement.entry_vertex_count=static_placement.vertex_count;
        vertex_base=static_placement.vertex_base;
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        /* A live static element without its load event (renderer creation,
         * an event-queue overflow): bake the complete animation once on the
         * first miss, not one pose every frame. */
        if( command->animation )
        {
            struct ToriRS_RenderCommand_AnimLoad load;
            memset(&load, 0, sizeof(load));
            load.element_id = command->element_id;
            load.anim_index = anim_index;
            load.animation = command->animation;
            load.model = command->model;
            load.world_position = command->world_position;
            es2z_animation_load(renderer, &load);
        }
        if( !trspk_pose_table_get(
                &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
        {
            vertex_base = es2_bake_into_arena(
                renderer,
                &renderer->groups[group],
                command->element_id,
                anim_index,
                pose_id,
                command->model,
                &command->world_position,
                true);
            if( vertex_base != UINT32_MAX )
                es2_zbuffer_pose_baked(
                    renderer, command->element_id, anim_index, pose_id, command->model);
        }
    }
    if( vertex_base == UINT32_MAX )
        return;

    if( binding < ES2_STATIC_PAGE_BINDING )
    {
        page_base = vertex_base & ~(ES2_VBO_PAGE - 1u);
        local_base = vertex_base - page_base;
    }
    else
        local_base = vertex_base;
    /* model_indices is this path's per-model index scratch. */
    if( !es2_reserve_model_indices(renderer, (uint32_t)face_count * 3u) )
        return;

    placement.binding = binding;
    placement.page_base = page_base;
    placement.local_base = local_base;
    placement.absolute_base = page_base + local_base;
    placement.face_count = face_count;
    placement.sorted_face_count = sorted_face_count;
    placement.anim_index = anim_index;
    placement.pose_id = pose_id;
    placement.dynamic = dynamic;
    placement.face_order = face_order;
    placement.projected_in_scene = projected_in_scene;
    placement.projected_depth = projected_depth;
    es2_zbuffer_emit_model(renderer, command, &placement);
}

void
es2z_end_3d(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    /* The prepared block describes a camera about to go out of scope;
     * unpublishing it is what stops a later pass reading a stale one. */
    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !es2_upload_atlas(renderer) )
        goto done;
    /* The retained world is drawn from the GPU: push what changed, then
     * the opaque and the blended halves of the sequence. */
    es2_zbuffer_flush_opaque(renderer);
    if( !es2_upload_geometry(renderer) )
        goto done;
    es2_zbuffer_end_pass(renderer);
    es2z_sequence_draw(renderer);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    es2_sequence_reset(renderer);
    es2_zbuffer_reset_pass(renderer);
    /* The world pass leaves a world-sized viewport and depth state; restore
     * so 2D that follows is neither clipped nor occluded. */
    es2_set_letterbox_viewport(renderer);
    es2_set_depth(renderer, false, false);
    es2_set_cull(renderer, false);
    es2_set_scissor(renderer, NULL);
}

/* ---- the retained model store ------------------------------------------------------- */

static void
es2z_model_load(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    /* A model replacement invalidates every pose from the old geometry. */
    if( es2_model_unload(renderer, command->element_id) )
        es2_zbuffer_element_dropped(renderer, command->element_id);
    if( es2_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            0,
            0,
            command->model,
            &command->world_position,
            true) != UINT32_MAX )
        es2_zbuffer_pose_baked(renderer, command->element_id, 0, 0, command->model);
}

static void
es2z_animation_load(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command)
{
    int anim_index = 0;
    int frame;

    assert(renderer);
    assert(command);
    if( !es2_animation_load_check(command, &anim_index) )
        return;
    /* Pose keys do not carry a sequence id. Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter
     * replacement cannot serve stale tail frames. */
    if( es2_animation_track_unload(renderer, command->element_id, anim_index) )
        es2_zbuffer_track_dropped(renderer, command->element_id, anim_index);
    for( frame = 0; frame < command->animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = es2_animation_pose_frame(command, frame);
        struct ToriDraw_ModelHandle handle;
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        if( es2_bake_into_arena(
                renderer,
                &renderer->groups[TRSPK_VBO_GROUP_STATIC],
                command->element_id,
                anim_index,
                frame,
                handle,
                &command->world_position,
                true) != UINT32_MAX )
            es2_zbuffer_pose_baked(
                renderer, command->element_id, anim_index, frame, handle);
        ToriDraw_ModelFree(baked);
    }
}

/* ---- batch commands ----------------------------------------------------------------- */

static void
es2z_batch_begin(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Batch* command)
{
    struct ES2StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    if( command->batch_id < 0 )
        return;
    slot = es2_static_batch_slot(renderer, command->batch_id, true);
    if( slot < 0 )
        return;
    batch = &renderer->static_batches[slot];
    es2_zbuffer_batch_dropped(renderer, batch->cpu);
    es2_invalidate_batch_pages(renderer, batch);
    trspk_batch16_begin(batch->cpu);
    batch->active = false;
    batch->building = true;
    es2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = slot;
}


static void
es2z_batch_add(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    struct ES2StaticBatch* batch;
    struct TRSPK_Batch16Reservation reservation;
    int anim_index;
    int pose_id;
    int face_count;
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    if( renderer->current_batch_slot < 0 ||
        (uint32_t)renderer->current_batch_slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[renderer->current_batch_slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    anim_index = animated ? es2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1) : 0;
    pose_id = command->pose_id >= 0 ? command->pose_id : 0;
    if( !trspk_batch16_reserve_pose(
            batch->cpu,
            command->element_id,
            anim_index,
            pose_id,
            (uint32_t)face_count * 3u,
            &reservation) )
        return;
    if( es2_bake_pose_vertices(
            renderer,
            reservation.vbo,
            reservation.triangles,
            reservation.vertex_base,
            command->model,
            &command->world_position,
            NULL,
            0,
            false) )
        es2_zbuffer_batch_pose_baked(
            renderer, command->element_id, anim_index, pose_id, command->model);
}


static void
es2z_batch_clear(struct TRSPK_Renderer_ES2* renderer, int batch_id, bool clear_all)
{
    uint32_t slot;
    assert(renderer);
    for( slot = 0u; slot < renderer->static_batch_count; slot++ )
    {
        struct ES2StaticBatch* batch = &renderer->static_batches[slot];
        if( !clear_all && batch->batch_id != batch_id )
            continue;
        es2_zbuffer_batch_dropped(renderer, batch->cpu);
        es2_invalidate_batch_pages(renderer, batch);
        trspk_batch16_clear(batch->cpu);
        batch->active = false;
        batch->building = false;
    }
    if( clear_all )
    {
        /* Nothing valid remains, so the bump allocator starts over: every
         * page re-allocates its range the next time its chunk commits. */
        uint32_t page_id;
        for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
            renderer->static_pages[page_id].gpu_capacity = 0u;
        renderer->static_batch_gpu_vertex_used = 0u;
    }
    es2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = -1;
}


/* ---- dispatch ------------------------------------------------------------------------ */

static void
es2z_dispatch(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        es2z_begin_3d(renderer, &command->u.begin_3d);
        break;
    case TORIRSRC_END_3D:
        es2z_end_3d(renderer);
        break;
    case TORIRSRC_BEGIN_2D:
        es2_begin_2d(renderer);
        break;
    case TORIRSRC_END_2D:
        es2_end_2d(renderer);
        break;
    case TORIRSRC_TEX_LOAD:
        if( command->u.tex_load.texture )
            (void)es2_load_texture_object(
                renderer, command->u.tex_load.texture_id, command->u.tex_load.texture);
        break;
    case TORIRSRC_TEX_UNLOAD:
        es2_unload_texture(renderer, command->u.tex_load.texture_id);
        break;
    case TORIRSRC_MODEL_LOAD:
        es2z_model_load(renderer, &command->u.model_load);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        /* The material table is keyed by the same pose keys the arena is,
         * so a drop has to reach it or a rebake reads a stale class. */
        if( es2_model_unload(renderer, command->u.model_load.element_id) )
            es2_zbuffer_element_dropped(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_ANIM_LOAD:
        es2z_animation_load(renderer, &command->u.anim_load);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        if( es2_animation_track_unload(
                renderer, command->u.anim_load.element_id, command->u.anim_load.anim_index) )
            es2_zbuffer_track_dropped(
                renderer, command->u.anim_load.element_id, command->u.anim_load.anim_index);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        es2z_batch_begin(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        es2z_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        es2z_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        es2_batch_end(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        es2z_batch_clear(renderer, command->u.batch.batch_id, command->u.batch.clear_all);
        if( command->u.batch.clear_all )
        {
            trspk_pose_table_clear(&renderer->poses);
            es2_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        }
        break;
    case TORIRSRC_DRAW_MODEL:
        es2z_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
        es2_ui_draw_clear_rect(renderer, &command->u.clear_rect);
        break;
    case TORIRSRC_FILL_RECT:
        es2_ui_draw_fill_rect(renderer, &command->u.fill_rect);
        break;
    case TORIRSRC_DRAW_MODEL_WIDGET:
        es2_ui_draw_model_widget(renderer, &command->u.model_widget);
        break;
    case TORIRSRC_SPRITE:
        es2_ui_draw_sprite(renderer, &command->u.sprite);
        break;
    case TORIRSRC_FONT:
        es2_ui_draw_font(renderer, &command->u.font);
        break;
    case TORIRSRC_LINE:
        es2_ui_draw_line(renderer, &command->u.line);
        break;
    case TORIRSRC_POLYGON_BEGIN:
        es2_ui_polygon_begin(renderer, &command->u.polygon_begin);
        break;
    case TORIRSRC_POLYGON_POINT:
        es2_ui_polygon_point(renderer, &command->u.polygon_point);
        break;
    case TORIRSRC_POLYGON_END:
        es2_ui_polygon_end(renderer);
        break;
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;
    case TORIRSRC_SPRITE_LOAD:
        /* The scene owns pixels. Upload stays lazy so assets never drawn by
         * this backend consume atlas space or transfer bandwidth. */
        break;
    case TORIRSRC_SPRITE_UNLOAD:
        es2_ui_sprite_invalidate(renderer, command->u.sprite_load.element_id);
        break;
    case TORIRSRC_FONT_LOAD:
        es2_ui_font_load(renderer, command->u.font_load.font_id, command->u.font_load.font);
        break;
    case TORIRSRC_FONT_UNLOAD:
        es2_ui_font_unload(renderer, command->u.font_load.font_id);
        break;
    case TORIRSRC_NONE:
        break;
    }
}


/* ---- the frame ---------------------------------------------------------------------- */

/*
 * The surface, then the framebuffer this renderer draws into, then the
 * clear. Two toolkit calls with this renderer's own two lines between them.
 */
static bool
es2z_begin_frame(struct TRSPK_Renderer_ES2* renderer, bool clear_to_black_only, bool allow_offscreen)
{
    if( !es2_frame_surface_begin(renderer, allow_offscreen) )
        return false;
    if( renderer->target_offscreen )
    {
        es2_scale_target_ensure(renderer);
        es2_scale_target_ensure_depth(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    }
    else
    {
        /* The limit is off or no longer binds: give the memory back now. */
        es2_scale_target_destroy_buffers(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    es2_frame_surface_clear(renderer, clear_to_black_only);
    return true;
}

void
TRSPK_Renderer_ES2_ZBufferDrawBootBar(
    struct TRSPK_Renderer_ES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    assert(renderer);
    /* progress < 0: clear only, no bar -- the post-login loading screen,
     * which is a black screen and the sentence alone on every lane. */
    if( !es2z_begin_frame(renderer, progress < 0, false) )
        return;
    if( progress >= 0 )
    {
        int bar_x;
        int bar_y;
        int fill_w;
        progress = es2_clampi(progress, 0, 100);
        /* The references' bar, not one of ours (engine/boot_bar.h): a filled
         * red track, a black inset one pixel in, then the fill two pixels in. */
        bar_x = renderer->width / 2 - BOOT_BAR_W / 2;
        bar_y = renderer->height / 2 - BOOT_BAR_ABOVE_CENTRE;
        fill_w = progress * BOOT_BAR_PX_PER_PERCENT;
        es2_draw_solid_rect(
            renderer, bar_x, bar_y, BOOT_BAR_W, BOOT_BAR_H, 0xff000000u | BOOT_BAR_COLOR);
        es2_draw_solid_rect(
            renderer, bar_x + 1, bar_y + 1, BOOT_BAR_W - 2, BOOT_BAR_H - 2, 0xff000000u);
        if( fill_w > 0 )
            es2_draw_solid_rect(
                renderer,
                bar_x + BOOT_BAR_INSET,
                bar_y + BOOT_BAR_INSET,
                fill_w,
                BOOT_BAR_FILL_H,
                0xff000000u | BOOT_BAR_COLOR);
    }
    if( caption && caption[0] && caption_font_id >= 0 )
        es2_draw_boot_caption(renderer, caption_font_id, caption);
}

bool
es2z_render_frame_begin(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    if( !es2z_begin_frame(renderer, false, true) )
        return false;
    renderer->has_3d = false;
    renderer->in3d = false;
    renderer->in2d = false;
    renderer->frame_clock += 1.0;
    return true;
}

void
es2z_render_frame_commands(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    assert(renderer);
    assert(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        es2_prefetch_ahead(renderer, frame);
        es2z_dispatch(renderer, &command);
    }
}

void
es2z_render_frame_end(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureEndPass();
#endif
#if defined(TORIRS_PLACEMENT_CAPTURE)
    es2_placement_capture_end();
#endif
    if( renderer->in3d )
        es2z_end_3d(renderer);
    if( renderer->in2d )
        es2_end_2d(renderer);
    if( renderer->target_offscreen )
        es2_scale_target_present(renderer);

    es2_frame_readback(renderer);
}

void
TRSPK_Renderer_ES2_ZBufferExecute(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_RenderCommand const* command)
{
    es2z_dispatch(renderer, command);
}

void
TRSPK_Renderer_ES2_ZBufferRenderFrame(struct TRSPK_Renderer_ES2* renderer, struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !es2z_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    es2z_render_frame_commands(renderer, frame);
    ToriRS_FrameEnd(frame);
    es2z_render_frame_end(renderer);
}

/* ---- lifetime ------------------------------------------------------------------------ */

/*
 * 16 depth bits, a CREATION attribute of the context, which is why this
 * renderer and not the toolkit asks for it. The depth state is made first so
 * a context failure leaves nothing behind.
 */
bool
TRSPK_Renderer_ES2_ZBufferInit(
    struct TRSPK_Renderer_ES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    assert(window);
    assert(scene);
    if( renderer->gl_context )
        return false;
    es2_zbuffer_destroy(renderer);
    if( !es2_zbuffer_create(renderer) )
        return false;
    if( !es2_init_gl(renderer, window, scene, 16, "depth-buffered") )
    {
        es2_zbuffer_destroy(renderer);
        return false;
    }
    return true;
}
