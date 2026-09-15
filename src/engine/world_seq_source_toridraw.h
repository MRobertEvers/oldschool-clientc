#ifndef SRC_ENGINE_WORLD_SEQ_SOURCE_TORIDRAW_H
#define SRC_ENGINE_WORLD_SEQ_SOURCE_TORIDRAW_H

/**
 * World_SeqSource backed by the scene's animation registry.
 *
 * World steps animations without knowing where seq configuration comes from:
 * it asks a vtable how long a frame lasts, how many there are, what the
 * priority is. This is that vtable for a client whose animations are resident
 * in a ToriDraw scene, with spotanim resolution off the cache provider.
 *
 * Every getter answers a DEFAULT for an id that is not resident yet, and the
 * defaults are chosen so the track freezes rather than races: one cycle per
 * frame, no stepping, 99 loops. An unloaded seq is not an error -- the loader
 * is asynchronous and the caller requests it separately -- so the track simply
 * waits where it is until the load lands.
 *
 * The binding is two pointers, which is why this can leave app.c at all: the
 * seq source needs the scene and the provider and nothing else about a client.
 */

#include "engine/cache_provider.h"
#include "toridraw_scene.h"
#include "world/world.h"

struct WorldSeqSourceToriDraw
{
    struct ToriDraw_Scene* scene;
    struct CacheProvider* provider;
};

/** Point the source at the scene and provider it should read. */
void
WorldSeqSourceToriDraw_Bind(
    struct WorldSeqSourceToriDraw* source,
    struct ToriDraw_Scene* scene,
    struct CacheProvider* provider);

/** Fill a World_SeqSource whose userdata is `source`. */
void
WorldSeqSourceToriDraw_Fill(
    struct WorldSeqSourceToriDraw* source,
    struct World_SeqSource* out_seq_source);

/**
 * The resident animation for `seq_id`, or NULL when the scene has no scene, a
 * negative id, or has not loaded that seq yet.
 *
 * Exposed because the caller has its own reasons to reach an animation -- the
 * world-entity hull bob samples the skeletal root bone directly -- and both
 * paths must agree about which registry answers.
 */
struct ToriDraw_Animation*
WorldSeqSourceToriDraw_Animation(
    struct WorldSeqSourceToriDraw const* source,
    int seq_id);

/**
 * Total client cycles one loop of a seq takes TO PLAY HERE.
 *
 * It has to agree with whatever actually steps the frames, which is
 * ToriDraw_AnimationAdvanceObjectCycles, and that implements the rev239
 * `while (cycle > delay) cycle -= delay` walk -- not Client-TS MapSpotAnim's
 * `getDuration(frame) + 1` subtraction.
 *
 * The two differ by one cycle per frame. Trace the rev239 walk: from a zero
 * counter the first frame needs delay+1 cycles to trip a STRICT `>`, and it
 * then leaves 1 behind, so every later frame costs exactly its own delay. The
 * loop is therefore sum(delay) + 1 cycles long, where summing (delay + 1)
 * would be sum(delay) + frame_count.
 *
 * Overstating it by frame_count - 1 is not harmless: the sequence ends, the
 * element drops its animation and snaps back to the un-posed base model, and
 * a free-standing spotanim then sits frozen in that pose until its lifetime
 * finally expires. On a 37-frame splash that is 36 cycles of dead frame --
 * the visible "it plays, then freezes" at the end of every map spotanim.
 *
 * `delay <= 0` counts as 1 because the advance treats it that way. Never less
 * than 1, including for a seq that is not resident.
 */
int
WorldSeqSourceToriDraw_TotalDuration(
    struct WorldSeqSourceToriDraw const* source,
    int seq_id);

#endif /* SRC_ENGINE_WORLD_SEQ_SOURCE_TORIDRAW_H */
