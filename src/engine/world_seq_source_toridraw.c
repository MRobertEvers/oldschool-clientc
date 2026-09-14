#include "engine/world_seq_source_toridraw.h"

#include "toridraw_animation.h"

#include <assert.h>
#include <stddef.h>

void
WorldSeqSourceToriDraw_Bind(
    struct WorldSeqSourceToriDraw* source,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider)
{
    assert(source);
    source->scene = scene;
    source->provider = provider;
}

struct ToriDraw_Animation*
WorldSeqSourceToriDraw_Animation(
    struct WorldSeqSourceToriDraw const* source,
    int seq_id)
{
    assert(source);
    if( !source->scene || seq_id < 0 )
        return NULL;
    return ToriDraw_SceneAnimationGet(source->scene, seq_id);
}

int
WorldSeqSourceToriDraw_TotalDuration(
    struct WorldSeqSourceToriDraw const* source,
    int seq_id)
{
    struct ToriDraw_Animation* anim;
    int total = 1;

    assert(source);
    anim = WorldSeqSourceToriDraw_Animation(source, seq_id);
    if( !anim || anim->frame_count <= 0 )
        return 1;
    for( int frame = 0; frame < anim->frame_count; frame++ )
    {
        int delay = 1;

        if( anim->frames && anim->frames[frame].delay > 0 )
            delay = anim->frames[frame].delay;
        total += delay;
    }
    return total > 0 ? total : 1;
}

/* The vtable's own accessor: same lookup, through the void* World hands back.
 * Unloaded ids fall through to each getter's default, which freezes the track
 * until the lazy seq load lands rather than racing it. */
static struct ToriDraw_Animation*
world_seq_source_toridraw_anim(
    void* userdata,
    int seq_id)
{
    return WorldSeqSourceToriDraw_Animation(
        (struct WorldSeqSourceToriDraw const*)userdata, seq_id);
}

static int
world_seq_source_toridraw_frame_count(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->frame_count : 0;
}

static int
world_seq_source_toridraw_frame_duration(
    void* userdata,
    int seq_id,
    int frame)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    /* Skeletal seqs carry no per-frame lengths — their curves are sampled one
     * tick per client cycle, so every frame is a single cycle long. */
    if( !anim || !anim->frames || frame < 0 || frame >= anim->frame_count )
        return 1;
    return anim->frames[frame].delay > 0 ? anim->frames[frame].delay : 1;
}

static int
world_seq_source_toridraw_frame_step(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->frame_step : 0;
}

static int
world_seq_source_toridraw_max_loops(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim && anim->max_loops > 0 ? anim->max_loops : 99;
}

static int
world_seq_source_toridraw_priority(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->priority : 5;
}

static int
world_seq_source_toridraw_duplicate_behavior(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->duplicate_behavior : -1;
}

static int
world_seq_source_toridraw_preanim_move(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->preanim_move : 0;
}

static int
world_seq_source_toridraw_postanim_move(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->postanim_move : 0;
}

static int
world_seq_source_toridraw_stretches(
    void* userdata,
    int seq_id)
{
    struct ToriDraw_Animation* anim = world_seq_source_toridraw_anim(userdata, seq_id);
    return anim ? anim->stretches : 0;
}

/* World_SeqSource.spotanim_seq: resolve a spotanim id to its animation seq id so
 * the world can step an entity's attached-graphic frame. -1 when the id is
 * invalid or the spotanimtype is not yet resident (the world then waits). */
static int
world_seq_source_toridraw_spotanim_seq(
    void* userdata,
    int spotanim_id)
{
    struct WorldSeqSourceToriDraw const* source = (struct WorldSeqSourceToriDraw const*)userdata;
    struct ToriRS_Spotanimtype* spot =
        spotanim_id >= 0 ? CacheProvider_SpotanimtypeGet(source->provider, spotanim_id) : NULL;
    return spot ? spot->seq : -1;
}

void
WorldSeqSourceToriDraw_Fill(
    struct WorldSeqSourceToriDraw* source,
    struct World_SeqSource* out_seq_source)
{
    assert(source);
    assert(out_seq_source);
    out_seq_source->userdata = source;
    out_seq_source->frame_count = world_seq_source_toridraw_frame_count;
    out_seq_source->frame_duration = world_seq_source_toridraw_frame_duration;
    out_seq_source->frame_step = world_seq_source_toridraw_frame_step;
    out_seq_source->max_loops = world_seq_source_toridraw_max_loops;
    out_seq_source->priority = world_seq_source_toridraw_priority;
    out_seq_source->duplicate_behavior = world_seq_source_toridraw_duplicate_behavior;
    out_seq_source->preanim_move = world_seq_source_toridraw_preanim_move;
    out_seq_source->postanim_move = world_seq_source_toridraw_postanim_move;
    out_seq_source->stretches = world_seq_source_toridraw_stretches;
    out_seq_source->spotanim_seq = world_seq_source_toridraw_spotanim_seq;
}
