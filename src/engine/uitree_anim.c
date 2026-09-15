#include "uitree_anim.h"

#include "engine/cache_provider.h"
#include "perf/torirs_perf.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "log/torirs_log.h"

struct WidgetModelPose
{
    struct ToriDraw_Model* model;
    uint64_t source_revision;
    int sequence;
    int frame;
};

static void
widget_pose_free(void* data)
{
    struct WidgetModelPose* pose = data;
    if( !pose ) return;
    if( pose->model ) ToriDraw_ModelFree(pose->model);
    free(pose);
}

struct ToriDraw_ModelHandle
UITreeAnim_ModelForDraw(struct ToriDraw_Scene* scene, struct UITreeModelRenderCache* cache,
                       int model_id, int sequence, int frame)
{
    struct ToriDraw_ModelHandle source = ToriDraw_SceneModelGet(scene, model_id);
    if( !cache ) return source;
    if( sequence < 0 || !ToriDraw_ModelKindIsFull(source.kind) || !source.u.model.model )
    {
        if( cache->release ) cache->release(cache->data);
        cache->data = NULL;
        cache->release = NULL;
        return source;
    }
    struct ToriDraw_Animation* animation = ToriDraw_SceneAnimationGet(scene, sequence);
    if( !animation || (!animation->base && !animation->skeletal) || animation->frame_count <= 0 ) return source;
    if( frame < 0 || frame >= animation->frame_count ) frame = 0;
    uint64_t revision = ToriDraw_SceneModelRevision(scene, model_id);
    struct WidgetModelPose* pose = cache->data;
    if( !pose || pose->source_revision != revision )
    {
        if( cache->release ) cache->release(cache->data);
        pose = calloc(1, sizeof(*pose));
        if( !pose ) abort();
        pose->model = ToriDraw_ModelCopy(source.u.model.model);
        if( !pose->model->original_vertices_x )
            ToriDraw_ModelCaptureOriginalVertices(pose->model);
        pose->source_revision = revision;
        pose->sequence = pose->frame = -1;
        cache->data = pose;
        cache->release = widget_pose_free;
    }
    if( pose->sequence != sequence || pose->frame != frame )
    {
        ToriDraw_ModelAnimateReset(pose->model);
        if( animation->skeletal )
        {
            if( pose->model->animaya_vertex_count > 0 && pose->model->animaya_group_counts &&
                pose->model->animaya_groups && pose->model->animaya_scales )
                ToriDraw_ModelAnimateSkeletal(pose->model, animation->skeletal, frame);
        }
        else if( animation->frames && animation->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(pose->model, animation->base, &animation->frames[frame]);
        else
            ToriDraw_ModelSetBoundsCylinder(pose->model);
        pose->sequence = sequence;
        pose->frame = frame;
    }
    struct ToriDraw_ModelHandle result = {.kind=TORIDRAWMK_MODEL, .u.model.model=pose->model};
    return result;
}

static int
tracker_has(struct SeqLoadTracker const* tracker, int seq_id)
{
    for( int i = 0; i < tracker->count; i++ )
        if( tracker->seq_ids[i] == seq_id )
            return 1;
    return 0;
}

static void
tracker_add(struct SeqLoadTracker* tracker, int seq_id)
{
    if( tracker->count >= UITREE_ANIM_SEQ_TRACK_MAX )
        return;
    tracker->seq_ids[tracker->count++] = seq_id;
}

int
UITreeAnim_RequestMissing(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider,
    struct ToriRS_TaskQueue* queue,
    struct SeqLoadTracker* tracker)
{
    int requested = 0;
    int model_n;
    int mi;

    assert(tree && scene && queue && tracker);
    model_n = tree->models.count;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_SCAN, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_SCAN_NODES, (int64_t)model_n);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_MODEL_NODES, (int64_t)model_n);
    for( mi = 0; mi < model_n; mi++ )
    {
        int32_t idx = tree->models.slots[mi];
        struct UITreeComponent const* c;
        int seq;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->type != UIELEM_RS_MODEL )
            continue;
        seq = c->cs1_active ? c->u.rs_model.active_anim_seq_id : c->u.rs_model.anim_seq_id;
        if( seq < 0 || (c->cs1_active ? c->u.rs_model.active_model_id : c->u.rs_model.gamecache_model_id) < 0 )
            continue;
        if( ToriDraw_SceneAnimationGet(scene, seq) )
            continue;
        if( tracker_has(tracker, seq) )
            continue;
        {
            struct ToriRS_Task* task = CreateTask_SequenceLoad(provider, scene, seq);
            if( !task )
                continue;
            ToriRS_TaskQueue_Add(queue, task);
            tracker_add(tracker, seq);
            requested++;
        }
    }
    return requested;
}

int
UITreeAnim_Advance(
    struct UITree* tree,
    struct ToriDraw_Scene* scene,
    int cycles)
{
    int applied = 0;
    int model_n;
    int mi;
    uint64_t begin_marks = g_torirs_dirty_mark_seq;

    assert(tree && scene);
    model_n = tree->models.count;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_SCAN, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_SCAN_NODES, (int64_t)model_n);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_ANIM_MODEL_NODES, (int64_t)model_n);
    for( mi = 0; mi < model_n; mi++ )
    {
        int32_t idx = tree->models.slots[mi];
        struct UITreeComponent* c;
        int seq;
        int model_id;
        struct ToriDraw_Animation* anim;
        struct ToriDraw_ModelHandle hnd;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->type != UIELEM_RS_MODEL )
            continue;
        if( cycles > 0 &&
            (c->u.rs_model.rotate_x_speed != 0 || c->u.rs_model.rotate_y_speed != 0) )
        {
            int const xan =
                (c->u.rs_model.xan + c->u.rs_model.rotate_x_speed * cycles) & 2047;
            int const yan =
                (c->u.rs_model.yan + c->u.rs_model.rotate_y_speed * cycles) & 2047;
            (void)UITree_SetModelPoseAt(
                tree,
                idx,
                c->u.rs_model.x_offset,
                c->u.rs_model.y_offset,
                xan,
                yan,
                c->u.rs_model.zan,
                0);
            applied = 1;
        }
        seq = c->cs1_active ? c->u.rs_model.active_anim_seq_id : c->u.rs_model.anim_seq_id;
        model_id = c->cs1_active ? c->u.rs_model.active_model_id : c->u.rs_model.gamecache_model_id;
        if( seq < 0 || model_id < 0 )
            continue;

        anim = ToriDraw_SceneAnimationGet(scene, seq);
        /* Not registered: load still in flight — rest pose until it lands.
         * Registered but empty: the load task's unavailable sentinel — skip. */
        if( !anim || (!anim->base && !anim->skeletal) || anim->frame_count <= 0 )
        {
            /* Silent by default and per-component under TORIRS_ANIM_DEBUG: a
             * widget stuck in its rest pose looks identical whether its
             * sequence is still loading, decoded to nothing, or is skeletal
             * (no framemap for the frame animator to walk), and those three
             * want different fixes. */
            static int debug = -1;
            if( debug < 0 )
                debug = getenv("TORIRS_ANIM_DEBUG") != NULL;
            if( debug )
                TORIRS_ERR("uitree_anim: com=0x%x seq=%d not posable (anim=%p base=%p frames=%d)\n",
                    c->component_id,
                    seq,
                    (void*)anim,
                    (void*)(anim ? anim->base : NULL),
                    anim ? anim->frame_count : -1);
            continue;
        }

        hnd = ToriDraw_SceneModelGet(scene, model_id);
        if( !ToriDraw_ModelKindIsFull(hnd.kind) || !hnd.u.model.model )
            continue;

        {
            int fr = c->u.rs_model.anim_frame;
            int cyc = c->u.rs_model.anim_frame_cycle + cycles;

            if( fr < 0 || fr >= anim->frame_count )
                fr = 0;

            /* Held nodes stay on anim_frame — the reference poses the design
             * preview once and only spins modelYAn afterwards. */
            if( c->u.rs_model.anim_hold )
            {
                cyc = 0;
            }
            else if( anim->skeletal )
            {
                int64_t next = (int64_t)fr + cycles;
                if( next >= anim->frame_count )
                {
                    int repeat = anim->frame_step > 0 && anim->frame_step <= anim->frame_count
                        ? anim->frame_step : anim->frame_count;
                    next = anim->frame_count - repeat + (next - anim->frame_count) % repeat;
                }
                fr = next < 0 ? 0 : (int)next;
                cyc = 0;
            }
            else
            {
                /* Advance frames while the accumulated cycles exceed the current
                 * frame's on-screen length. A non-positive delay is clamped to 1
                 * so the loop always terminates. */
                while( 1 )
                {
                    int delay = anim->frames[fr].delay;
                    if( delay <= 0 )
                        delay = 1;
                    if( cyc <= delay )
                        break;
                    cyc -= delay;
                    fr++;
                    if( fr >= anim->frame_count )
                    {
                        fr -= anim->frame_step;
                        if( fr < 0 || fr >= anim->frame_count )
                            fr = 0;
                    }
                }
            }

            (void)UITree_SetModelAnimationCursorAt(tree, idx, fr, cyc);
            /* Posing is per widget at draw translation. The registered model
             * remains an immutable asset shared by any number of widgets. */
            applied = 1;
        }
    }
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_ANIM_MARKS, (int64_t)(g_torirs_dirty_mark_seq - begin_marks));
    return applied;
}
