#include "uitree_anim.h"

#include "engine/cache_provider.h"
#include "perf/torirs_perf.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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
        seq = c->u.rs_model.anim_seq_id;
        if( seq < 0 || c->u.rs_model.gamecache_model_id < 0 )
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
            c->u.rs_model.xan =
                (c->u.rs_model.xan + c->u.rs_model.rotate_x_speed * cycles) & 2047;
            c->u.rs_model.yan =
                (c->u.rs_model.yan + c->u.rs_model.rotate_y_speed * cycles) & 2047;
            UITree_MarkNodeDirty(tree, idx);
            applied = 1;
        }
        seq = c->u.rs_model.anim_seq_id;
        model_id = c->u.rs_model.gamecache_model_id;
        if( seq < 0 || model_id < 0 )
            continue;

        anim = ToriDraw_SceneAnimationGet(scene, seq);
        /* Not registered: load still in flight — rest pose until it lands.
         * Registered but empty: the load task's unavailable sentinel — skip. */
        if( !anim || !anim->base || anim->frame_count <= 0 )
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
                fprintf(
                    stderr,
                    "uitree_anim: com=0x%x seq=%d not posable (anim=%p base=%p frames=%d)\n",
                    c->component_id,
                    seq,
                    (void*)anim,
                    (void*)(anim ? anim->base : NULL),
                    anim ? anim->frame_count : -1);
            continue;
        }

        hnd = ToriDraw_SceneModelGet(scene, model_id);
        if( hnd.kind != TORIDRAWMK_MODEL || !hnd.u.model.model )
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

            c->u.rs_model.anim_frame = fr;
            c->u.rs_model.anim_frame_cycle = cyc;

            ToriDraw_ModelAnimateReset(hnd.u.model.model);
            /* An empty frame (no translators) is the rest pose — reset only. */
            if( anim->frames[fr].length > 0 )
                ToriDraw_ModelAnimateFrame(hnd.u.model.model, anim->base, &anim->frames[fr]);
            applied = 1;
        }
    }
    return applied;
}
