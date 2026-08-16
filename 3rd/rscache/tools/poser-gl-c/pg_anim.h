#ifndef POSER_GL_C_PG_ANIM_H
#define POSER_GL_C_PG_ANIM_H

/*
 * The editable animation: a sequence, its keyframes, and the rig each keyframe
 * poses.
 *
 * Working out the rig is `pg_rig.c`'s job and none of it is here — this file is
 * about the sequence around it: which frames it plays, for how long, and how a
 * keyframe is copied, inserted and removed while the editor works on it.
 */

#include "pg_cache.h"
#include "pg_model.h"
#include "pg_rig.h"

#include <stdbool.h>

#define PG_MAX_ANIMATION_LENGTH 9999
/** Sequence item fields below this are not item ids. poser-gl's ITEM_OFFSET. */
#define PG_ITEM_OFFSET 512

struct PG_Keyframe
{
    int id;
    int frame_id; /* composite (archive << 16 | file), or -1 when authored here */
    int length;
    struct PG_FrameMapDef* framemap;
    bool modified;

    /** The joints and hierarchy this frame implies, from pg_rig. */
    struct PG_RigSkeleton skeleton;
};

struct PG_Animation
{
    int id;
    /* The source sequence, kept so an unmodified animation can be re-read and a
     * modified one can be written back with its non-frame fields intact. */
    int* frame_ids;
    int* frame_lengths;
    int frame_count;
    int loop_offset;
    int left_hand_item;
    int right_hand_item;
    char* name; /* nullable debug name from the cache */

    struct PG_Keyframe* keyframes;
    int keyframe_count;
    int keyframe_capacity;

    bool modified;
    bool loaded;
    int length;
};

/* ---- lifecycle ------------------------------------------------------------ */

/** An animation over a cache sequence; keyframes are read on first load. */
struct PG_Animation*
pg_animation_new_from_sequence(const struct PG_SeqDef* seq);

/** An empty modified animation, for imports and copies. */
struct PG_Animation*
pg_animation_new_empty(int id);

void
pg_animation_free(struct PG_Animation* anim);

/** Deep copy under a new id, marked modified. poser-gl's copy constructor. */
struct PG_Animation*
pg_animation_copy(const struct PG_Animation* src, int new_id);

/**
 * Read the sequence's frames into keyframes, deriving each one's rig. A no-op
 * once loaded.
 *
 * `advanced_mode` widens the set of transforms considered from the ones the
 * frames actually carry to the whole range up to the highest, which surfaces
 * joints the animation never moves.
 */
void
pg_animation_load(struct PG_Animation* anim, struct PG_Cache* cache, bool advanced_mode);

/** Drop the keyframes so the next load re-reads them. */
void
pg_animation_reload(struct PG_Animation* anim, struct PG_Cache* cache, bool advanced_mode);

int
pg_animation_calculate_length(const struct PG_Animation* anim);
/** floorMod over the keyframe count, so a negative frame counter wraps. */
int
pg_animation_frame_index(const struct PG_Animation* anim, int index);

/** Insert a keyframe. False when it would exceed the maximum animation length. */
bool
pg_animation_insert_keyframe(
    struct PG_Animation* anim,
    const struct PG_Keyframe* keyframe,
    int index);
void
pg_animation_remove_keyframe_at(struct PG_Animation* anim, int index);

/* ---- keyframes ------------------------------------------------------------ */

void
pg_keyframe_init(struct PG_Keyframe* keyframe);
void
pg_keyframe_free_contents(struct PG_Keyframe* keyframe);
/** Deep copy, including the rig. */
void
pg_keyframe_copy(struct PG_Keyframe* dst, const struct PG_Keyframe* src, int new_id);
/** Reset the model to its rest pose and apply every transform of the rig, in order. */
void
pg_keyframe_apply(const struct PG_Keyframe* keyframe, struct PG_ModelDef* def);

#endif
