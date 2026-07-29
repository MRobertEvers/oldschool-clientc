#ifndef POSER_GL_C_PG_RIG_H
#define POSER_GL_C_PG_RIG_H

/*
 * Deriving a rig from a framemap and a frame.
 *
 * A framemap is a flat list of transforms, each with a kind and a set of bone
 * labels, and a frame is a flat list of values against those indices. Neither
 * says anything about joints or hierarchy — the cache does not store one. This
 * file recovers both, and is the only place that knows how:
 *
 *   - a joint is an ORIGIN transform plus every transform between it and the
 *     next ORIGIN, which become its children;
 *   - a joint's parent is the joint whose rotation carries every label this
 *     one's ORIGIN does, because rotating that one necessarily moves this one;
 *     the closest such parent is the one with the smallest label set;
 *   - the root is the joint whose rotation moves the most labels, which in
 *     practice is the hips.
 *
 * All of that is inference, not decoding, which is why it is worth having in one
 * place with its reasoning attached rather than spread through the editor that
 * consumes it.
 *
 * Applying a rig is a different job and is not here: bending a mesh needs
 * per-vertex bone labels and the client's animate kernel, which live with the
 * model (`pg_model.c`). This file never touches a mesh. It depends on nothing —
 * not the cache, not the editor's types — and everything crosses the boundary as
 * plain arrays.
 *
 * Scope: the label-and-transform rig every cache era shares. Animaya's
 * per-vertex skinning is a different mechanism; see RIGGING_OSRS_RS2.md.
 */

#include <stdbool.h>

/**
 * Transform kinds, as a framemap numbers them.
 *
 * ORIGIN moves nothing: it sets the pivot the ROTATE and SCALE after it turn
 * around. That is what makes a run of transforms a joint, and why an ORIGIN with
 * nothing following it is not one. The reference implementation calls it a
 * "reference" transform.
 */
enum PG_RigKind
{
    PG_RIG_ORIGIN = 0,
    PG_RIG_TRANSLATE = 1,
    PG_RIG_ROTATE = 2,
    PG_RIG_SCALE = 3
    /* Type 5 is an alpha transform, which changes transparency rather than
     * geometry. It is not a child of anything here. */
};

const char*
pg_rig_kind_name(int kind);

/**
 * What a component reads as when the frame does not carry it: 128 for scale,
 * which is unity where the kernel divides by 128, and 0 for everything else.
 */
int
pg_rig_default_delta(int kind);

/* ---- the inputs ---------------------------------------------------------- */

/**
 * A framemap: the rig an animation is authored against. Entry `i` is one
 * transform, of `types[i]`, over the labels in `label_sets[i]`.
 */
struct PG_RigFramemap
{
    int length;
    const int* types;
    int* const* label_sets;
    const int* label_set_lengths;
};

/** One frame: values for the transforms it touches, by framemap index. */
struct PG_RigFrame
{
    int count;
    const int* transform_ids;
    const int* dx;
    const int* dy;
    const int* dz;
};

/**
 * One component of a frame's value for a transform, or the kind's default when
 * the frame does not carry it. `axis` is 0, 1 or 2.
 */
int
pg_rig_frame_delta(const struct PG_RigFrame* frame, int transform_id, int kind, int axis);

/* ---- the result ---------------------------------------------------------- */

/**
 * One entry of a derived rig.
 *
 * Both joints and their children are entries; `is_origin` says which. Children
 * and parent are indices into the owning skeleton rather than pointers, because
 * the array is grown by realloc while the rig is being derived.
 */
struct PG_RigJoint
{
    int id; /* index into the framemap's transform list */
    int kind;
    const int* labels; /* borrowed from the framemap */
    int label_count;
    int delta[3];

    bool is_origin;

    int child_by_kind[4]; /* -1 when absent */
    int child_order[4];   /* insertion order, which the .pgl format preserves */
    int child_count;
    int parent; /* -1 when none */

    /** Scratch for whoever draws the rig; nothing here reads it. */
    float position[3];
};

struct PG_RigSkeleton
{
    struct PG_RigJoint* joints;
    int joint_count;
    int joint_capacity;
    /** Index of the joint everything hangs off, or -1. */
    int root;
};

void
pg_rig_skeleton_init(struct PG_RigSkeleton* skeleton);
void
pg_rig_skeleton_free(struct PG_RigSkeleton* skeleton);

/**
 * Derive the rig: build a joint at each of `transform_ids`, then link them.
 *
 * `transform_ids` is the set of framemap indices to consider, which the caller
 * decides — an animation only touches some of a framemap's transforms, and which
 * ones is a property of the frames, not of the rig. Indices that are not ORIGINs
 * are skipped, so the whole range can be passed.
 *
 * Replaces whatever `skeleton` held.
 */
void
pg_rig_skeleton_build(
    struct PG_RigSkeleton* skeleton,
    const struct PG_RigFramemap* framemap,
    const struct PG_RigFrame* frame,
    const int* transform_ids,
    int transform_id_count);

/**
 * One joint: the ORIGIN at `transform_id` and every transform up to the next
 * one. Returns its index, or -1 when `transform_id` is out of range, is not an
 * ORIGIN, or turned out to have no children — a pivot nothing hangs off is not a
 * joint, and the partial entry is removed rather than left behind.
 *
 * Exposed for callers that build a rig an entry at a time; pg_rig_skeleton_build
 * is the usual way in.
 */
int
pg_rig_skeleton_add_joint(
    struct PG_RigSkeleton* skeleton,
    const struct PG_RigFramemap* framemap,
    const struct PG_RigFrame* frame,
    int transform_id);

/**
 * Infer parents, the root, and the reattachment of stray sections, over whatever
 * joints the skeleton already holds.
 *
 * Separate from building because a rig can arrive already built — a `.pgl` file
 * carries joints and children but no hierarchy, and this is what gives it one.
 */
void
pg_rig_skeleton_link(struct PG_RigSkeleton* skeleton);

/* ---- working with a derived rig -------------------------------------------- */

/** Append a blank joint, or NULL on allocation failure. Invalidates pointers. */
struct PG_RigJoint*
pg_rig_skeleton_push(struct PG_RigSkeleton* skeleton);

/** Drop everything from `count` on. */
void
pg_rig_skeleton_truncate(struct PG_RigSkeleton* skeleton, int count);

/** The joint or child with this framemap index, or NULL. */
struct PG_RigJoint*
pg_rig_skeleton_find(struct PG_RigSkeleton* skeleton, int id);

/** A joint's child of this kind, or the joint itself for ORIGIN. */
struct PG_RigJoint*
pg_rig_joint_child(struct PG_RigSkeleton* skeleton, struct PG_RigJoint* joint, int kind);

/**
 * Record `child_index` as `parent_index`'s child of its own kind.
 *
 * A repeated kind replaces the earlier one but keeps its slot in the insertion
 * order, which the `.pgl` encoder writes children in.
 */
void
pg_rig_joint_attach(struct PG_RigSkeleton* skeleton, int parent_index, int child_index);

/**
 * Deep copy, rebuilding the cross-references rather than trusting the indices.
 * `dst` need not be initialised; anything it held is leaked, so free it first.
 */
void
pg_rig_skeleton_copy(struct PG_RigSkeleton* dst, const struct PG_RigSkeleton* src);

#endif
