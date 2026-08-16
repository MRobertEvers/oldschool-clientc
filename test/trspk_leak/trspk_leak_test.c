/**
 * CPU-only retained-renderer leak/regrowth regression.
 *
 * This links the real fixed-function D3D9 command dispatcher and drives
 * MODEL_LOAD, ANIM_LOAD, and BATCH3D_CLEAR without creating a window or D3D
 * device.  A forced-in allocator shim checks actual C-heap ownership while
 * renderer diagnostics check arena high-water marks and pose relocation.
 */

#include "retained_alloc_tracker.h"

#include "platform/platform_win32_renderer_d3d9.h"

#include "core/trspk_ibo.h"

#include "toridraw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VBO_PAGE_VERTICES 65536u
#define LARGE_FACE_COUNT 12000
#define LARGE_VERTEX_COUNT ((uint32_t)LARGE_FACE_COUNT * 3u)
#define ANCHOR_FACE_COUNT 4
#define ANCHOR_VERTEX_COUNT ((uint32_t)ANCHOR_FACE_COUNT * 3u)
#define MAIN_ELEMENT_ID 7
#define ANCHOR_ELEMENT_ID 1
#define LONG_FRAME_COUNT 8
#define SHORT_FRAME_COUNT 2
#define WARMUP_CYCLES 3
#define STEADY_CYCLES 24
#define CLEAR_CYCLES 32

struct TestAnimation
{
    struct ToriDraw_Animation animation;
    struct ToriDraw_AnimBase base;
};

static int
report_failure(const char* message)
{
    fprintf(stderr, "trspk_leak_test: %s\n", message);
    return 1;
}

static struct ToriDraw_Model*
make_untextured_model(int face_count)
{
    struct ToriDraw_Model* model = ToriDraw_ModelNew(3, face_count, 0u);
    int face;
    if( !model )
        return NULL;

    model->vertices_x = (vertexint_t*)calloc(3u, sizeof(*model->vertices_x));
    model->vertices_y = (vertexint_t*)calloc(3u, sizeof(*model->vertices_y));
    model->vertices_z = (vertexint_t*)calloc(3u, sizeof(*model->vertices_z));
    model->face_indices_a =
        (faceint_t*)calloc((size_t)face_count, sizeof(*model->face_indices_a));
    model->face_indices_b =
        (faceint_t*)calloc((size_t)face_count, sizeof(*model->face_indices_b));
    model->face_indices_c =
        (faceint_t*)calloc((size_t)face_count, sizeof(*model->face_indices_c));
    model->face_colors_a =
        (hsl16_t*)calloc((size_t)face_count, sizeof(*model->face_colors_a));
    model->face_colors_b =
        (hsl16_t*)calloc((size_t)face_count, sizeof(*model->face_colors_b));
    model->face_colors_c =
        (hsl16_t*)calloc((size_t)face_count, sizeof(*model->face_colors_c));
    if( !model->vertices_x || !model->vertices_y || !model->vertices_z ||
        !model->face_indices_a || !model->face_indices_b || !model->face_indices_c ||
        !model->face_colors_a || !model->face_colors_b || !model->face_colors_c )
    {
        ToriDraw_ModelFree(model);
        return NULL;
    }

    model->vertices_x[0] = 0;
    model->vertices_y[0] = 0;
    model->vertices_z[0] = 0;
    model->vertices_x[1] = 128;
    model->vertices_y[1] = 0;
    model->vertices_z[1] = 0;
    model->vertices_x[2] = 0;
    model->vertices_y[2] = 0;
    model->vertices_z[2] = 128;
    for( face = 0; face < face_count; face++ )
    {
        model->face_indices_a[face] = 0;
        model->face_indices_b[face] = 1;
        model->face_indices_c[face] = 2;
        model->face_colors_a[face] = (hsl16_t)0x4b40u;
        model->face_colors_b[face] = (hsl16_t)0x4b40u;
        model->face_colors_c[face] = (hsl16_t)0x4b40u;
    }
    return model;
}

static int
init_test_animation(struct TestAnimation* out, int frame_count)
{
    memset(out, 0, sizeof(*out));
    out->animation.base = &out->base;
    out->animation.frames = (struct ToriDraw_AnimFrame*)calloc(
        (size_t)frame_count, sizeof(*out->animation.frames));
    out->animation.frame_count = frame_count;
    /* Empty classic frames intentionally mean "hold the captured rest pose".
     * The retained renderer still copies and prebakes each frame. */
    return out->animation.frames != NULL;
}

static void
free_test_animation(struct TestAnimation* animation)
{
    free(animation->animation.frames);
    animation->animation.frames = NULL;
}

static struct ToriDraw_ModelHandle
model_handle(struct ToriDraw_Model* model)
{
    struct ToriDraw_ModelHandle handle;
    memset(&handle, 0, sizeof(handle));
    handle.kind = TORIDRAWMK_MODEL;
    handle.u.model.model = model;
    return handle;
}

static void
execute_model_load(
    struct ToriRS_D3D9* renderer,
    int element_id,
    struct ToriDraw_Model* model)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_MODEL_LOAD;
    command.u.model_load.element_id = element_id;
    command.u.model_load.model = model_handle(model);
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_animation_load(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int track,
    struct ToriDraw_Model* model,
    struct TestAnimation* animation)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_ANIM_LOAD;
    command.u.anim_load.element_id = element_id;
    command.u.anim_load.anim_index = track;
    command.u.anim_load.animation = &animation->animation;
    command.u.anim_load.model = model_handle(model);
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_model_unload(struct ToriRS_D3D9* renderer, int element_id)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_MODEL_UNLOAD;
    command.u.model_load.element_id = element_id;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_animation_unload(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_ANIM_UNLOAD;
    command.u.anim_load.element_id = element_id;
    command.u.anim_load.anim_index = anim_index;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_batch_clear(struct ToriRS_D3D9* renderer)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_BATCH3D_CLEAR;
    command.u.batch.batch_id = TORIDRAW_SCENE_INVALID_BATCH_ID;
    command.u.batch.clear_all = true;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_batch_begin(struct ToriRS_D3D9* renderer, int batch_id)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_BATCH3D_BEGIN;
    command.u.batch.batch_id = batch_id;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_batch_add(
    struct ToriRS_D3D9* renderer,
    int batch_id,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_Model* model,
    int animated)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = animated ? TORIRSRC_BATCH3D_ANIM_ADD
                            : TORIRSRC_BATCH3D_MODEL_ADD;
    command.u.batch.batch_id = batch_id;
    command.u.batch.element_id = element_id;
    command.u.batch.anim_index = anim_index;
    command.u.batch.pose_id = pose_id;
    command.u.batch.model = model_handle(model);
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_batch_end(struct ToriRS_D3D9* renderer, int batch_id)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_BATCH3D_END;
    command.u.batch.batch_id = batch_id;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_batch_target_clear(struct ToriRS_D3D9* renderer, int batch_id)
{
    struct ToriRS_RenderCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = TORIRSRC_BATCH3D_CLEAR;
    command.u.batch.batch_id = batch_id;
    command.u.batch.clear_all = false;
    ToriRS_D3D9_Execute(renderer, &command);
}

static void
execute_static_batch(
    struct ToriRS_D3D9* renderer,
    int batch_id,
    int element_id,
    struct ToriDraw_Model* model,
    int include_animated_pose)
{
    execute_batch_begin(renderer, batch_id);
    execute_batch_add(renderer, batch_id, element_id, 0, 0, model, 0);
    if( include_animated_pose )
        execute_batch_add(renderer, batch_id, element_id, 1, 3, model, 1);
    execute_batch_end(renderer, batch_id);
}

/* Alternating the two tracks is intentional.  Each replacement leaves the
 * other track alive across several 64K VBO pages.  A page-local-only compactor
 * makes the two tracks leapfrog forever; a retained renderer must repack the
 * arena and remap every surviving pose before appending the replacement. */
static void
execute_replacement_cycle(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_Model* model,
    struct ToriDraw_Model* anchor_model,
    struct TestAnimation* long_animation,
    struct TestAnimation* short_animation)
{
    execute_model_load(renderer, ANCHOR_ELEMENT_ID, anchor_model);
    execute_animation_load(renderer, MAIN_ELEMENT_ID, 0, model, short_animation);
    execute_animation_load(renderer, MAIN_ELEMENT_ID, 1, model, long_animation);
    execute_animation_load(renderer, MAIN_ELEMENT_ID, 0, model, long_animation);
    execute_animation_load(renderer, MAIN_ELEMENT_ID, 1, model, short_animation);
}

static int
same_group_stats(
    const struct ToriRS_D3D9RetainedGroupStats* expected,
    const struct ToriRS_D3D9RetainedGroupStats* actual)
{
    return expected->write_cursor == actual->write_cursor &&
           expected->vertex_count == actual->vertex_count &&
           expected->vertex_capacity == actual->vertex_capacity &&
           expected->slot_count == actual->slot_count &&
           expected->slot_capacity == actual->slot_capacity &&
           expected->alive_slot_count == actual->alive_slot_count;
}

static int
same_retained_stats(
    const struct ToriRS_D3D9RetainedStats* expected,
    const struct ToriRS_D3D9RetainedStats* actual)
{
    uint32_t group;
    for( group = 0u; group < TORIRS_D3D9_RETAINED_GROUP_COUNT; group++ )
        if( !same_group_stats(&expected->groups[group], &actual->groups[group]) )
            return 0;
    return expected->pose_element_count == actual->pose_element_count &&
           expected->pose_element_capacity == actual->pose_element_capacity;
}

static int
verify_pose_layout(
    struct ToriRS_D3D9 const* renderer,
    const struct ToriRS_D3D9RetainedStats* stats)
{
    uint32_t bases[1u + LONG_FRAME_COUNT + SHORT_FRAME_COUNT];
    uint32_t counts[1u + LONG_FRAME_COUNT + SHORT_FRAME_COUNT];
    uint32_t base_count = 0u;
    uint32_t base;
    uint32_t i;
    uint32_t j;
    int pose;

    if( stats->groups[TRSPK_VBO_GROUP_STATIC].alive_slot_count !=
        1u + LONG_FRAME_COUNT + SHORT_FRAME_COUNT )
        return report_failure("unexpected number of live retained slots");
    if( stats->groups[TRSPK_VBO_GROUP_STATIC].write_cursor !=
        stats->groups[TRSPK_VBO_GROUP_STATIC].vertex_count )
        return report_failure("arena cursor and VBO vertex count diverged");

    if( !ToriRS_D3D9_GetPoseBase(
            renderer, ANCHOR_ELEMENT_ID, 0, 0, &base) )
        return report_failure("anchor pose mapping was lost");
    bases[base_count] = base;
    counts[base_count++] = ANCHOR_VERTEX_COUNT;

    for( pose = 0; pose < LONG_FRAME_COUNT; pose++ )
    {
        if( !ToriRS_D3D9_GetPoseBase(renderer, MAIN_ELEMENT_ID, 0, pose, &base) )
            return report_failure("long animation pose mapping was lost");
        bases[base_count] = base;
        counts[base_count++] = LARGE_VERTEX_COUNT;
    }
    for( pose = 0; pose < SHORT_FRAME_COUNT; pose++ )
    {
        if( !ToriRS_D3D9_GetPoseBase(renderer, MAIN_ELEMENT_ID, 1, pose, &base) )
            return report_failure("short animation pose mapping was lost");
        bases[base_count] = base;
        counts[base_count++] = LARGE_VERTEX_COUNT;
    }
    for( pose = SHORT_FRAME_COUNT; pose < LONG_FRAME_COUNT; pose++ )
        if( ToriRS_D3D9_GetPoseBase(renderer, MAIN_ELEMENT_ID, 1, pose, &base) )
            return report_failure("short animation retained a stale tail pose");

    for( i = 0u; i < base_count; i++ )
    {
        if( bases[i] + counts[i] >
            stats->groups[TRSPK_VBO_GROUP_STATIC].write_cursor )
            return report_failure("pose mapping points beyond the retained VBO");
        if( bases[i] % VBO_PAGE_VERTICES + counts[i] > VBO_PAGE_VERTICES )
            return report_failure("retained pose crosses a 16-bit VBO page");
        for( j = i + 1u; j < base_count; j++ )
            if( bases[i] < bases[j] + counts[j] && bases[j] < bases[i] + counts[i] )
                return report_failure("two live pose mappings overlap");
    }
    return 0;
}

static int
test_d3d9_retained_execute_steady_state(void)
{
    struct RetainedAllocSnapshot entry_alloc = retained_alloc_snapshot();
    struct RetainedAllocSnapshot plateau_alloc;
    struct RetainedAllocSnapshot current_alloc;
    struct ToriRS_D3D9RetainedStats plateau_stats;
    struct ToriRS_D3D9RetainedStats current_stats;
    struct ToriDraw_Scene* scene = NULL;
    struct ToriDraw_Model* large_model = NULL;
    struct ToriDraw_Model* anchor_model = NULL;
    struct ToriRS_D3D9* renderer = NULL;
    struct TestAnimation long_animation;
    struct TestAnimation short_animation;
    uint32_t probe_base;
    int long_animation_ready = 0;
    int short_animation_ready = 0;
    int failed = 0;
    int cycle;

    memset(&long_animation, 0, sizeof(long_animation));
    memset(&short_animation, 0, sizeof(short_animation));
    ToriDraw_Init();
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_LAZY_TEXTURES,
        TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    large_model = make_untextured_model(LARGE_FACE_COUNT);
    anchor_model = make_untextured_model(ANCHOR_FACE_COUNT);
    long_animation_ready = init_test_animation(&long_animation, LONG_FRAME_COUNT);
    short_animation_ready = init_test_animation(&short_animation, SHORT_FRAME_COUNT);
    renderer = ToriRS_D3D9_New(320, 240);
    if( !scene || !large_model || !anchor_model || !long_animation_ready ||
        !short_animation_ready || !renderer )
    {
        failed = report_failure("fixture allocation failed");
        goto cleanup;
    }
    if( !ToriRS_D3D9_AttachSceneHeadlessForTest(renderer, scene) )
    {
        failed = report_failure("headless scene attach failed");
        goto cleanup;
    }

    execute_model_load(renderer, ANCHOR_ELEMENT_ID, anchor_model);
    execute_model_load(renderer, MAIN_ELEMENT_ID, large_model);
    execute_animation_load(
        renderer, MAIN_ELEMENT_ID, 0, large_model, &long_animation);
    execute_animation_load(
        renderer, MAIN_ELEMENT_ID, 1, large_model, &short_animation);
    for( cycle = 0; cycle < WARMUP_CYCLES; cycle++ )
        execute_replacement_cycle(
            renderer,
            large_model,
            anchor_model,
            &long_animation,
            &short_animation);

    if( !ToriRS_D3D9_GetRetainedStats(renderer, &plateau_stats) )
    {
        failed = report_failure("could not read retained stats after warmup");
        goto cleanup;
    }
    if( verify_pose_layout(renderer, &plateau_stats) != 0 )
    {
        failed = 1;
        goto cleanup;
    }
    plateau_alloc = retained_alloc_snapshot();

    for( cycle = 0; cycle < STEADY_CYCLES; cycle++ )
    {
        execute_replacement_cycle(
            renderer,
            large_model,
            anchor_model,
            &long_animation,
            &short_animation);
        if( !ToriRS_D3D9_GetRetainedStats(renderer, &current_stats) )
        {
            failed = report_failure("could not read retained stats during replacement");
            goto cleanup;
        }
        if( !same_retained_stats(&plateau_stats, &current_stats) )
        {
            fprintf(
                stderr,
                "trspk_leak_test: retained state grew in cycle %d "
                "(cursor %lu -> %lu, vertices %lu -> %lu, capacity %lu -> %lu, "
                "slots %lu/%lu -> %lu/%lu)\n",
                cycle,
                (unsigned long)plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].write_cursor,
                (unsigned long)current_stats.groups[TRSPK_VBO_GROUP_STATIC].write_cursor,
                (unsigned long)plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_count,
                (unsigned long)current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_count,
                (unsigned long)plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity,
                (unsigned long)current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity,
                (unsigned long)plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].alive_slot_count,
                (unsigned long)plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_count,
                (unsigned long)current_stats.groups[TRSPK_VBO_GROUP_STATIC].alive_slot_count,
                (unsigned long)current_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_count);
            failed = 1;
            goto cleanup;
        }
        if( verify_pose_layout(renderer, &current_stats) != 0 )
        {
            failed = 1;
            goto cleanup;
        }
        current_alloc = retained_alloc_snapshot();
        if( current_alloc.live_blocks != plateau_alloc.live_blocks ||
            current_alloc.live_bytes != plateau_alloc.live_bytes )
        {
            fprintf(
                stderr,
                "trspk_leak_test: live heap grew in cycle %d "
                "(%lu blocks/%lu bytes -> %lu blocks/%lu bytes)\n",
                cycle,
                (unsigned long)plateau_alloc.live_blocks,
                (unsigned long)plateau_alloc.live_bytes,
                (unsigned long)current_alloc.live_blocks,
                (unsigned long)current_alloc.live_bytes);
            failed = 1;
            goto cleanup;
        }
    }

    execute_batch_clear(renderer);
    if( !ToriRS_D3D9_GetRetainedStats(renderer, &current_stats) )
    {
        failed = report_failure("could not read retained stats after clear");
        goto cleanup;
    }
    if( current_stats.groups[TRSPK_VBO_GROUP_STATIC].write_cursor != 0u ||
        current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_count != 0u ||
        current_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_count != 0u ||
        current_stats.groups[TRSPK_VBO_GROUP_STATIC].alive_slot_count != 0u )
    {
        failed = report_failure("BATCH3D_CLEAR did not release retained occupancy");
        goto cleanup;
    }
    if( current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity !=
            plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity ||
        current_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_capacity !=
            plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_capacity )
    {
        failed = report_failure("BATCH3D_CLEAR unexpectedly reallocated retained capacity");
        goto cleanup;
    }
    if( ToriRS_D3D9_GetPoseBase(renderer, MAIN_ELEMENT_ID, 0, 0, &probe_base) ||
        ToriRS_D3D9_GetPoseBase(renderer, MAIN_ELEMENT_ID, 1, 0, &probe_base) ||
        ToriRS_D3D9_GetPoseBase(renderer, ANCHOR_ELEMENT_ID, 0, 0, &probe_base) )
    {
        failed = report_failure("BATCH3D_CLEAR left a live pose mapping");
        goto cleanup;
    }
    current_alloc = retained_alloc_snapshot();
    if( current_alloc.live_blocks != plateau_alloc.live_blocks ||
        current_alloc.live_bytes != plateau_alloc.live_bytes )
    {
        failed = report_failure("BATCH3D_CLEAR changed retained heap ownership");
        goto cleanup;
    }

    for( cycle = 0; cycle < CLEAR_CYCLES; cycle++ )
    {
        execute_model_load(renderer, ANCHOR_ELEMENT_ID, anchor_model);
        execute_batch_clear(renderer);
        if( !ToriRS_D3D9_GetRetainedStats(renderer, &current_stats) ||
            current_stats.groups[TRSPK_VBO_GROUP_STATIC].write_cursor != 0u ||
            current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_count != 0u ||
            current_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_count != 0u ||
            current_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity !=
                plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_capacity ||
            current_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_capacity !=
                plateau_stats.groups[TRSPK_VBO_GROUP_STATIC].slot_capacity )
        {
            failed = report_failure("clear/reload cycle changed retained high-water state");
            goto cleanup;
        }
        current_alloc = retained_alloc_snapshot();
        if( current_alloc.live_blocks != plateau_alloc.live_blocks ||
            current_alloc.live_bytes != plateau_alloc.live_bytes )
        {
            failed = report_failure("clear/reload cycle leaked heap ownership");
            goto cleanup;
        }
    }

cleanup:
    ToriRS_D3D9_Free(renderer);
    if( long_animation_ready )
        free_test_animation(&long_animation);
    if( short_animation_ready )
        free_test_animation(&short_animation);
    ToriDraw_ModelFree(large_model);
    ToriDraw_ModelFree(anchor_model);
    ToriDraw_SceneFree(scene);

    current_alloc = retained_alloc_snapshot();
    if( current_alloc.live_blocks != entry_alloc.live_blocks ||
        current_alloc.live_bytes != entry_alloc.live_bytes )
    {
        fprintf(
            stderr,
            "trspk_leak_test: teardown leaked %ld blocks / %ld bytes\n",
            (long)(current_alloc.live_blocks - entry_alloc.live_blocks),
            (long)(current_alloc.live_bytes - entry_alloc.live_bytes));
        failed = 1;
    }
    return failed;
}

static int
test_d3d9_static_batch_lifecycle(void)
{
    enum
    {
        LEGACY_ELEMENT = 9,
        BATCH_A_ELEMENT = 10,
        BATCH_B_ELEMENT = 11,
        BATCH_A = 101,
        BATCH_B = 202,
        REBUILD_CYCLES = 16
    };
    struct RetainedAllocSnapshot entry_alloc = retained_alloc_snapshot();
    struct RetainedAllocSnapshot plateau_alloc;
    struct RetainedAllocSnapshot current_alloc;
    struct ToriRS_D3D9RetainedStats stats;
    struct ToriDraw_Scene* scene = NULL;
    struct ToriDraw_Model* model = NULL;
    struct ToriRS_D3D9* renderer = NULL;
    uint32_t batch_a_model_base;
    uint32_t batch_a_anim_base;
    uint32_t probe;
    int failed = 0;
    int cycle;

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_LAZY_TEXTURES,
        TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    model = make_untextured_model(ANCHOR_FACE_COUNT);
    renderer = ToriRS_D3D9_New(320, 240);
    if( !scene || !model || !renderer ||
        !ToriRS_D3D9_AttachSceneHeadlessForTest(renderer, scene) )
    {
        failed = report_failure("static-batch fixture allocation failed");
        goto cleanup;
    }

    execute_model_load(renderer, LEGACY_ELEMENT, model);
    execute_static_batch(renderer, BATCH_A, BATCH_A_ELEMENT, model, 1);
    execute_static_batch(renderer, BATCH_B, BATCH_B_ELEMENT, model, 0);
    if( !ToriRS_D3D9_GetPoseBase(
            renderer, LEGACY_ELEMENT, 0, 0, &probe) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 0, 0, &batch_a_model_base) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 1, 3, &batch_a_anim_base) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_B_ELEMENT, 0, 0, &probe) )
    {
        failed = report_failure("batch end did not retain every pose mapping");
        goto cleanup;
    }

    execute_model_unload(renderer, BATCH_A_ELEMENT);
    execute_animation_unload(renderer, BATCH_A_ELEMENT, 1);
    if( !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 0, 0, &probe) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 1, 3, &probe) )
    {
        failed = report_failure(
            "individual unload mutated geometry owned by a committed batch");
        goto cleanup;
    }

    /* One identical rebuild warms all reusable pose/hash/page storage. */
    execute_static_batch(renderer, BATCH_A, BATCH_A_ELEMENT, model, 1);
    plateau_alloc = retained_alloc_snapshot();
    for( cycle = 0; cycle < REBUILD_CYCLES; cycle++ )
    {
        uint32_t rebuilt_model_base;
        uint32_t rebuilt_anim_base;
        execute_static_batch(renderer, BATCH_A, BATCH_A_ELEMENT, model, 1);
        if( !ToriRS_D3D9_GetPoseBase(
                renderer, LEGACY_ELEMENT, 0, 0, &probe) ||
            !ToriRS_D3D9_GetPoseBase(
                renderer, BATCH_B_ELEMENT, 0, 0, &probe) ||
            !ToriRS_D3D9_GetPoseBase(
                renderer, BATCH_A_ELEMENT, 0, 0, &rebuilt_model_base) ||
            !ToriRS_D3D9_GetPoseBase(
                renderer, BATCH_A_ELEMENT, 1, 3, &rebuilt_anim_base) ||
            rebuilt_model_base != batch_a_model_base ||
            rebuilt_anim_base != batch_a_anim_base )
        {
            failed = report_failure("batch rebuild changed/lost a stable pose mapping");
            goto cleanup;
        }
        current_alloc = retained_alloc_snapshot();
        if( current_alloc.live_blocks != plateau_alloc.live_blocks ||
            current_alloc.live_bytes != plateau_alloc.live_bytes )
        {
            failed = report_failure("batch rebuild did not reuse retained allocations");
            goto cleanup;
        }
    }

    execute_batch_target_clear(renderer, BATCH_A);
    if( ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 0, 0, &probe) ||
        ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 1, 3, &probe) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_B_ELEMENT, 0, 0, &probe) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, LEGACY_ELEMENT, 0, 0, &probe) )
    {
        failed = report_failure("targeted clear affected the wrong batch");
        goto cleanup;
    }

    execute_static_batch(renderer, BATCH_A, BATCH_A_ELEMENT, model, 1);
    execute_batch_target_clear(renderer, BATCH_B);
    if( !ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 0, 0, &probe) ||
        ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_B_ELEMENT, 0, 0, &probe) ||
        !ToriRS_D3D9_GetPoseBase(
            renderer, LEGACY_ELEMENT, 0, 0, &probe) )
    {
        failed = report_failure("second targeted clear affected the wrong owner");
        goto cleanup;
    }

    execute_batch_clear(renderer);
    if( ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 0, 0, &probe) ||
        ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_A_ELEMENT, 1, 3, &probe) ||
        ToriRS_D3D9_GetPoseBase(
            renderer, BATCH_B_ELEMENT, 0, 0, &probe) ||
        ToriRS_D3D9_GetPoseBase(
            renderer, LEGACY_ELEMENT, 0, 0, &probe) ||
        !ToriRS_D3D9_GetRetainedStats(renderer, &stats) ||
        stats.groups[TRSPK_VBO_GROUP_STATIC].write_cursor != 0u ||
        stats.groups[TRSPK_VBO_GROUP_STATIC].vertex_count != 0u )
    {
        failed = report_failure("full clear left retained batch or legacy occupancy");
        goto cleanup;
    }
    current_alloc = retained_alloc_snapshot();
    if( current_alloc.live_blocks != plateau_alloc.live_blocks ||
        current_alloc.live_bytes != plateau_alloc.live_bytes )
    {
        failed = report_failure("batch clear discarded reusable CPU allocations");
        goto cleanup;
    }

cleanup:
    ToriRS_D3D9_Free(renderer);
    ToriDraw_ModelFree(model);
    ToriDraw_SceneFree(scene);
    current_alloc = retained_alloc_snapshot();
    if( current_alloc.live_blocks != entry_alloc.live_blocks ||
        current_alloc.live_bytes != entry_alloc.live_bytes )
    {
        failed = report_failure("static-batch teardown leaked heap ownership");
    }
    return failed;
}

int
main(void)
{
    if( test_d3d9_retained_execute_steady_state() != 0 )
        return 1;
    if( test_d3d9_static_batch_lifecycle() != 0 )
        return 1;
    printf("trspk_leak_test ok\n");
    return 0;
}
