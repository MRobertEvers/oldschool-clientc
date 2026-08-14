#ifndef TORIDRAW_ANIMATION_H
#define TORIDRAW_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct ToriDraw_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

/*
 * One sound a sequence frame emits.
 *
 * `radius` is the reference's `location` field, and the name here is the one
 * the client's own use justifies: `addSequenceSoundEffect` packs it into the
 * low byte of a `(radius | tileZ << 8 | tileX << 16)` word, and the sound queue
 * reads that byte back as `(word & 255) * 128` -- an audible range in fine
 * units, beyond which the sound is discarded rather than attenuated. It is not
 * a position or a placement mode; 3,906 of osrs239's 4,329 frame sounds carry
 * one, and the 423 that do not are audible essentially nowhere.
 *
 * `weight` (rev226+) is a relative likelihood among the sounds sharing a frame.
 * A frame with several entries plays *one* of them, chosen in proportion; 67
 * osrs239 frames have up to six alternatives.
 */
struct ToriDraw_AnimFrameSound
{
    int id;
    int loops;
    int radius;
    int retain;
    int weight;
};

/*
 * Frame index -> sound, sorted by frame index, with **repeats allowed**: a
 * frame carrying several alternative sounds appears once per alternative, in a
 * contiguous run. Consumers must widen a hit to its whole run rather than stop
 * at the first match.
 */
struct ToriDraw_AnimFrameSoundMap
{
    int* frame_indices;
    struct ToriDraw_AnimFrameSound* sounds;
    int count;
};

struct ToriDraw_Animation
{
    struct ToriDraw_AnimBase* base;
    struct ToriDraw_AnimFrame* frames;
    int frame_count;
    /* Sequence loop-back offset (config opcode 2). At end of playback the frame
     * index becomes (frame_count - frame_step), clamped to 0 if out of range.
     * -1 or 0 => full loop to frame 0. */
    int frame_step;

    /* Sequence-config metadata (reference SeqType), carried so entity anim
     * stepping and the walkmerge blend need no cache access at draw time.
     * Populated via ToriDraw_AnimationSetSeqMeta; zero/NULL when the source
     * config had none. */
    /** Ascending transform-group ids driven by the SECONDARY (walk) seq when
     * this animation plays as primary, 9999999-terminated (seq opcode 3).
     * NULL = no merge: primary drives every group. Owned. */
    int* walkmerge;
    int priority;           /* seq opcode 5 (reference default 5) */
    int max_loops;          /* seq opcode 8 (reference default 99) */
    int preanim_move;       /* seq opcode 9 (PreanimMove) */
    int postanim_move;      /* seq opcode 10 (PostanimMove) */
    int duplicate_behavior; /* seq opcode 11 (RestartMode) */
    /** Whether the model stretches vertically along its motion while this seq
     * plays as the primary animation (reference SeqType.stretches, seq opcode
     * 13). Drives the entity's forward draw-padding: a stretching action makes
     * the model reach a tile ahead, so the painter must register it over that
     * extra tile or it draws in front of a wall it should sit behind. 0/1. */
    int stretches;
    /** Held-item overrides while this seq plays as the primary animation
     * (reference SeqType.replaceheldleft/right, opcodes 6/7). A value >= 0
     * replaces the player appearance's left-hand (slot 5) / right-hand (slot 3)
     * item for the model build; a small value (< 0x100) draws no model there,
     * i.e. the held item is hidden. -1 => leave the worn item as-is. */
    int replaceheldleft;
    int replaceheldright;
    /** Set when this sequence is skeletal (Animaya) rather than classic
     * frame/framemap: `base`/`frames` are then NULL and posing goes through
     * ToriDraw_ModelAnimateSkeletal instead. `frame_count` still bounds
     * playback so the frame steppers need no special case. Owned. */
    struct ToriDraw_SkeletalAnim* skeletal;
    /** Frame sounds loaded from the sequence config (cache opcode 18 and per-frame
     * sound= entries). The animation tick queues these through RS_Audio_Synth
     * when each frame is stepped. Owned. */
    struct ToriDraw_AnimFrameSoundMap frame_sounds;
};

/* Seq-config values to attach to an assembled animation. walkmerge (may be
 * NULL) is deep-copied up to its 9999999 sentinel. */
struct ToriDraw_AnimSeqMeta
{
    int const* walkmerge;
    int priority;
    int max_loops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
    int replaceheldleft;
    int replaceheldright;
    int stretches;
};

void
ToriDraw_AnimationSetSeqMeta(
    struct ToriDraw_Animation* anim,
    struct ToriDraw_AnimSeqMeta const* meta);

/**
 * Advance one frame for a scene DynamicObject (a location animation).  At the
 * end of its frame list the client subtracts SeqType.frameStep; an invalid
 * result means the DynamicObject drops its sequence and returns to its static
 * model.  Returns false in that latter case.
 */
bool
ToriDraw_AnimationAdvanceObjectFrame(struct ToriDraw_Animation const* anim, int* frame);

/** Advance a DynamicObject-style sequence by client cycles. `frame_cycle` is
 * the elapsed time within the current frame. A frame remains current while
 * frame_cycle <= its cache length; crossing the boundary preserves the
 * remainder, matching the rev239 sequence player. Returns false when the
 * sequence terminates because it has no valid frame step. */
bool
ToriDraw_AnimationAdvanceObjectCycles(
    struct ToriDraw_Animation const* anim,
    int* frame,
    int* frame_cycle,
    int cycles);

/** Advance a *looping* sequence by client cycles: a projectile or a map
 * spotanim, not a DynamicObject. Reference ClientProj.move / MapSpotAnim.update
 * both wrap `animFrame` to 0 at the end of the frame list unconditionally —
 * they never consult SeqType.frameStep and never drop the sequence. Sharing the
 * DynamicObject advance here is wrong twice over: a projectile whose seq has no
 * valid frame step (frameStep -1 is the common case) loses its animation part
 * way through the flight and snaps back to the model's un-posed bind pose,
 * which for a spotanim model that hides geometry by scaling it to zero means
 * the hidden parts reappear mid-air.
 *
 * Always returns having advanced; there is no terminating case. */
void
ToriDraw_AnimationAdvanceLoopCycles(
    struct ToriDraw_Animation const* anim,
    int* frame,
    int* frame_cycle,
    int cycles);

void
ToriDraw_AnimationFree(struct ToriDraw_Animation* anim);

/**
 * A baked skeletal (Animaya) animation.
 *
 * matrices[(frame * bone_count + bone) * 16 .. +15] is the column-major 4x4
 * float skinning matrix for that bone at that frame.
 */
struct ToriDraw_SkeletalAnim
{
    int id;
    int bone_count;
    int frame_count;
    float* matrices; /* [frame_count * bone_count * 16] */
};

void
ToriDraw_SkeletalAnimFree(struct ToriDraw_SkeletalAnim* skeletal);

#endif
