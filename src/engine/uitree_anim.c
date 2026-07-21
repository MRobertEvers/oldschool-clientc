#include "uitree_anim.h"

#include "engine/dat2/task_dat2_sequence_load.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"

#include <assert.h>

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
    uint32_t i;

    assert(tree && scene && queue && tracker);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int seq;
        if( c->type != UIELEM_RS_MODEL )
            continue;
        seq = c->u.rs_model.anim_seq_id;
        if( seq < 0 || c->u.rs_model.gamecache_model_id < 0 )
            continue;
        if( ToriDraw_SceneAnimationGet(scene, seq) )
            continue;
        if( tracker_has(tracker, seq) )
            continue;
        {
            struct ToriRS_Task* task = CreateTask_Dat2SequenceLoad(provider, scene, seq);
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
    struct SeqLoadTracker* tracker,
    int cycles,
    int queue_idle)
{
    int applied = 0;
    uint32_t i;

    assert(tree && scene && tracker);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        int seq;
        int model_id;
        struct ToriDraw_Animation* anim;
        struct ToriDraw_ModelHandle hnd;
        if( c->type != UIELEM_RS_MODEL )
            continue;
        seq = c->u.rs_model.anim_seq_id;
        model_id = c->u.rs_model.gamecache_model_id;
        if( seq < 0 || model_id < 0 )
            continue;

        anim = ToriDraw_SceneAnimationGet(scene, seq);
        if( !anim )
        {
            /* Load requested but the queue has drained without producing it:
             * the sequence is unavailable in this cache — disable so we don't
             * re-request every tick. Otherwise it's still in flight; rest
             * pose until it lands. */
            if( queue_idle && tracker_has(tracker, seq) )
                c->u.rs_model.anim_seq_id = -1;
            continue;
        }
        if( !anim->base || anim->frame_count <= 0 )
            continue;

        hnd = ToriDraw_SceneModelGet(scene, model_id);
        if( hnd.kind != TORIDRAWMK_MODEL || !hnd.u.model.model )
            continue;

        {
            int fr = c->u.rs_model.anim_frame;
            int cyc = c->u.rs_model.anim_frame_cycle + cycles;

            if( fr < 0 || fr >= anim->frame_count )
                fr = 0;

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
