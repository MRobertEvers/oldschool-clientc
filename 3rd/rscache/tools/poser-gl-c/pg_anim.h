#ifndef POSER_GL_C_PG_ANIM_H
#define POSER_GL_C_PG_ANIM_H

/*
 * The editable animation: a sequence turned into keyframes of reference nodes.
 *
 * A cache frame is a flat list of (transform index, x, y, z). What makes it
 * editable is the structure the reference frame implies: an ORIGIN transform
 * defines a pivot and the transforms following it, up to the next ORIGIN, move
 * around that pivot. poser-gl calls that group a reference node, treats it as a
 * joint, and infers a skeleton by asking which joint's rotation label set
 * contains which other's.
 */

#include "pg_cache.h"
#include "pg_math.h"
#include "pg_model.h"

#include <stdbool.h>

#define PG_MAX_ANIMATION_LENGTH 9999
/** Sequence item fields below this are not item ids. poser-gl's ITEM_OFFSET. */
#define PG_ITEM_OFFSET 512

enum PG_TransformType
{
    PG_TF_REFERENCE = 0,
    PG_TF_TRANSLATION = 1,
    PG_TF_ROTATION = 2,
    PG_TF_SCALE = 3,
    /* Type 5 is an alpha transform; poser-gl does not edit those and neither
     * does this, so it is neither a reference nor a child here. */
};

const char*
pg_transform_type_name(int type);
/** 0 for every type but scale, whose absent components read back as 128. */
int
pg_transform_default_delta(int type);

struct PG_Transformation
{
    int id; /* index into the framemap's transform list */
    int type;
    const int* labels; /* borrowed from the framemap */
    int label_count;
    int delta[3];

    bool is_reference;

    /* Reference-node fields. Children and parent are indices into the owning
     * keyframe's array rather than pointers, because that array is grown by
     * realloc while the skeleton is being built. */
    int child_by_type[4]; /* -1 when absent */
    int child_order[4];   /* insertion order, which the .pgl format preserves */
    int child_count;
    int parent; /* -1 when none */
    struct PG_Vec3 position;
    bool highlighted;
};

struct PG_Keyframe
{
    int id;
    int frame_id; /* composite (archive << 16 | file), or -1 when authored here */
    int length;
    struct PG_FrameMapDef* framemap;
    bool modified;

    struct PG_Transformation* transformations;
    int transformation_count;
    int transformation_capacity;

    int root_node; /* index into transformations, or -1 */
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
 * Read the sequence's frames into keyframes. A no-op once loaded.
 *
 * `advanced_mode` widens each frame's transform set from the indices it actually
 * carries to the whole range up to its highest one, which surfaces joints the
 * animation never moves.
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
pg_keyframe_init(struct PG_Keyframe* kf);
void
pg_keyframe_free_contents(struct PG_Keyframe* kf);
/** Deep copy, preserving the reference/child structure and the parent links. */
void
pg_keyframe_copy(struct PG_Keyframe* dst, const struct PG_Keyframe* src, int new_id);
/** Infer parents and the root joint from the reference nodes' label sets. */
void
pg_keyframe_build_skeleton(struct PG_Keyframe* kf);
/** Reset the model to its rest pose and apply every transformation in order. */
void
pg_keyframe_apply(const struct PG_Keyframe* kf, struct PG_ModelDef* def);
/** The transformation with this id, or NULL. */
struct PG_Transformation*
pg_keyframe_find(struct PG_Keyframe* kf, int id);

/** Average position of a reference node's labelled vertices, in model space. */
void
pg_reference_set_position(
    struct PG_Transformation* node,
    const struct PG_ModelDef* def,
    float scale_divisor);

/** The child of `node` with this type, or the node itself for REFERENCE. */
struct PG_Transformation*
pg_reference_child(struct PG_Keyframe* kf, struct PG_Transformation* node, int type);

#endif
