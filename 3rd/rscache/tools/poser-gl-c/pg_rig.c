#include "pg_rig.h"

#include <stdlib.h>
#include <string.h>

const char*
pg_rig_kind_name(int kind)
{
    switch( kind )
    {
    case PG_RIG_ORIGIN:
        return "Reference";
    case PG_RIG_TRANSLATE:
        return "Translation";
    case PG_RIG_ROTATE:
        return "Rotation";
    case PG_RIG_SCALE:
        return "Scale";
    default:
        return "?";
    }
}

int
pg_rig_default_delta(int kind)
{
    return kind == PG_RIG_SCALE ? 128 : 0;
}

int
pg_rig_frame_delta(const struct PG_RigFrame* frame, int transform_id, int kind, int axis)
{
    if( !frame )
        return pg_rig_default_delta(kind);
    for( int i = 0; i < frame->count; i++ )
    {
        if( frame->transform_ids[i] != transform_id )
            continue;
        return axis == 0 ? frame->dx[i] : (axis == 1 ? frame->dy[i] : frame->dz[i]);
    }
    return pg_rig_default_delta(kind);
}

/* ---- skeleton storage ------------------------------------------------------ */

void
pg_rig_skeleton_init(struct PG_RigSkeleton* skeleton)
{
    memset(skeleton, 0, sizeof(*skeleton));
    skeleton->root = -1;
}

void
pg_rig_skeleton_free(struct PG_RigSkeleton* skeleton)
{
    if( !skeleton )
        return;
    free(skeleton->joints);
    skeleton->joints = NULL;
    skeleton->joint_count = 0;
    skeleton->joint_capacity = 0;
    skeleton->root = -1;
}

static void
joint_blank(struct PG_RigJoint* joint)
{
    memset(joint, 0, sizeof(*joint));
    joint->parent = -1;
    for( int i = 0; i < 4; i++ )
    {
        joint->child_by_kind[i] = -1;
        joint->child_order[i] = -1;
    }
}

struct PG_RigJoint*
pg_rig_skeleton_push(struct PG_RigSkeleton* skeleton)
{
    struct PG_RigJoint* joint;

    if( skeleton->joint_count == skeleton->joint_capacity )
    {
        int next = skeleton->joint_capacity ? skeleton->joint_capacity * 2 : 32;
        struct PG_RigJoint* grown = realloc(skeleton->joints, (size_t)next * sizeof(*grown));
        if( !grown )
            return NULL;
        skeleton->joints = grown;
        skeleton->joint_capacity = next;
    }
    joint = &skeleton->joints[skeleton->joint_count++];
    joint_blank(joint);
    return joint;
}

void
pg_rig_skeleton_truncate(struct PG_RigSkeleton* skeleton, int count)
{
    if( count >= 0 && count < skeleton->joint_count )
        skeleton->joint_count = count;
}

struct PG_RigJoint*
pg_rig_skeleton_find(struct PG_RigSkeleton* skeleton, int id)
{
    if( !skeleton )
        return NULL;
    for( int i = 0; i < skeleton->joint_count; i++ )
        if( skeleton->joints[i].id == id )
            return &skeleton->joints[i];
    return NULL;
}

struct PG_RigJoint*
pg_rig_joint_child(struct PG_RigSkeleton* skeleton, struct PG_RigJoint* joint, int kind)
{
    if( !skeleton || !joint )
        return NULL;
    if( kind == PG_RIG_ORIGIN )
        return joint;
    if( kind < 0 || kind > 3 )
        return NULL;
    if( joint->child_by_kind[kind] < 0 )
        return NULL;
    return &skeleton->joints[joint->child_by_kind[kind]];
}

void
pg_rig_joint_attach(struct PG_RigSkeleton* skeleton, int parent_index, int child_index)
{
    struct PG_RigJoint* parent;
    int kind;

    if( parent_index < 0 || parent_index >= skeleton->joint_count )
        return;
    if( child_index < 0 || child_index >= skeleton->joint_count )
        return;
    parent = &skeleton->joints[parent_index];
    kind = skeleton->joints[child_index].kind;
    if( kind < 0 || kind > 3 )
        return;

    if( parent->child_by_kind[kind] < 0 )
    {
        if( parent->child_count < 4 )
            parent->child_order[parent->child_count++] = child_index;
    }
    else
    {
        /* Keep the slot the earlier one held: the .pgl encoder writes children in
         * insertion order, so replacing in place is what keeps a re-export
         * byte-identical. */
        for( int i = 0; i < parent->child_count; i++ )
            if( parent->child_order[i] == parent->child_by_kind[kind] )
                parent->child_order[i] = child_index;
    }
    parent->child_by_kind[kind] = child_index;
}

void
pg_rig_skeleton_copy(struct PG_RigSkeleton* dst, const struct PG_RigSkeleton* src)
{
    /*
     * Rebuilt joint by joint rather than memcpy'd, because every joint holds
     * indices into the array it lives in. They would survive a flat copy today —
     * a joint and its children are appended together — but only by accident of
     * the build order, and a copy that silently depends on that is the kind of
     * thing that breaks when the build order changes.
     */
    int* remap;

    pg_rig_skeleton_init(dst);
    if( !src || src->joint_count <= 0 )
        return;

    remap = malloc((size_t)src->joint_count * sizeof(int));
    if( !remap )
        return;
    for( int i = 0; i < src->joint_count; i++ )
        remap[i] = -1;

    for( int i = 0; i < src->joint_count; i++ )
    {
        const struct PG_RigJoint* source = &src->joints[i];
        struct PG_RigJoint* joint;
        int joint_index;

        if( !source->is_origin )
            continue;

        joint = pg_rig_skeleton_push(dst);
        if( !joint )
            break;
        joint_index = dst->joint_count - 1;
        *joint = *source;
        joint->parent = -1;
        joint->child_count = 0;
        for( int k = 0; k < 4; k++ )
        {
            joint->child_by_kind[k] = -1;
            joint->child_order[k] = -1;
        }
        remap[i] = joint_index;

        for( int o = 0; o < source->child_count; o++ )
        {
            int child_index = source->child_order[o];
            struct PG_RigJoint* child;

            if( child_index < 0 || child_index >= src->joint_count )
                continue;
            child = pg_rig_skeleton_push(dst);
            if( !child )
                break;
            *child = src->joints[child_index];
            child->parent = -1;
            child->child_count = 0;
            for( int k = 0; k < 4; k++ )
            {
                child->child_by_kind[k] = -1;
                child->child_order[k] = -1;
            }
            remap[child_index] = dst->joint_count - 1;
            pg_rig_joint_attach(dst, joint_index, dst->joint_count - 1);
        }
    }

    for( int i = 0; i < src->joint_count; i++ )
    {
        int index = remap[i];
        int parent = src->joints[i].parent;
        if( index < 0 )
            continue;
        if( parent >= 0 && parent < src->joint_count )
            dst->joints[index].parent = remap[parent];
    }
    dst->root = (src->root >= 0 && src->root < src->joint_count) ? remap[src->root] : -1;
    free(remap);
}

/* ---- building joints -------------------------------------------------------- */

/*
 * Every transform after the ORIGIN, up to the next one.
 *
 * Not just the first: a joint routinely carries a rotate and a scale, and
 * stopping at one drops the other silently — the transform is still applied when
 * the frame plays, but the editor has no handle for it.
 */
static void
add_children(
    struct PG_RigSkeleton* skeleton,
    int joint_index,
    int transform_id,
    const struct PG_RigFramemap* framemap,
    const struct PG_RigFrame* frame)
{
    int child_id = transform_id + 1;

    while( child_id < framemap->length )
    {
        int kind = framemap->types[child_id];
        struct PG_RigJoint* child;

        if( kind == PG_RIG_ORIGIN )
            break;
        if( kind == PG_RIG_TRANSLATE || kind == PG_RIG_ROTATE || kind == PG_RIG_SCALE )
        {
            child = pg_rig_skeleton_push(skeleton);
            if( !child )
                return;
            child->id = child_id;
            child->kind = kind;
            child->labels = framemap->label_sets[child_id];
            child->label_count = framemap->label_set_lengths[child_id];
            child->delta[0] = pg_rig_frame_delta(frame, child_id, kind, 0);
            child->delta[1] = pg_rig_frame_delta(frame, child_id, kind, 1);
            child->delta[2] = pg_rig_frame_delta(frame, child_id, kind, 2);
            child->is_origin = false;
            pg_rig_joint_attach(skeleton, joint_index, skeleton->joint_count - 1);
        }
        child_id++;
    }
}

int
pg_rig_skeleton_add_joint(
    struct PG_RigSkeleton* skeleton,
    const struct PG_RigFramemap* framemap,
    const struct PG_RigFrame* frame,
    int transform_id)
{
    struct PG_RigJoint* joint;
    int joint_index;

    if( transform_id < 0 || transform_id >= framemap->length )
        return -1;
    if( framemap->types[transform_id] != PG_RIG_ORIGIN )
        return -1;

    joint = pg_rig_skeleton_push(skeleton);
    if( !joint )
        return -1;
    joint_index = skeleton->joint_count - 1;
    joint->id = transform_id;
    joint->kind = PG_RIG_ORIGIN;
    joint->labels = framemap->label_sets[transform_id];
    joint->label_count = framemap->label_set_lengths[transform_id];
    joint->delta[0] = pg_rig_frame_delta(frame, transform_id, PG_RIG_ORIGIN, 0);
    joint->delta[1] = pg_rig_frame_delta(frame, transform_id, PG_RIG_ORIGIN, 1);
    joint->delta[2] = pg_rig_frame_delta(frame, transform_id, PG_RIG_ORIGIN, 2);
    joint->is_origin = true;

    add_children(skeleton, joint_index, transform_id, framemap, frame);

    if( skeleton->joints[joint_index].child_count == 0 )
    {
        /* A pivot with nothing hanging off it is not a joint. Truncating here is
         * safe precisely because the children are appended straight after it, so
         * there are none to strand. */
        pg_rig_skeleton_truncate(skeleton, joint_index);
        return -1;
    }
    return joint_index;
}

/* ---- inferring the hierarchy -------------------------------------------------- */

static struct PG_RigJoint*
rotation_of(struct PG_RigSkeleton* skeleton, const struct PG_RigJoint* joint)
{
    if( joint->child_by_kind[PG_RIG_ROTATE] < 0 )
        return NULL;
    return &skeleton->joints[joint->child_by_kind[PG_RIG_ROTATE]];
}

static bool
labels_contain_all(const int* superset, int superset_count, const int* subset, int subset_count)
{
    for( int i = 0; i < subset_count; i++ )
    {
        bool found = false;
        for( int j = 0; j < superset_count; j++ )
        {
            if( superset[j] == subset[i] )
            {
                found = true;
                break;
            }
        }
        if( !found )
            return false;
    }
    return true;
}

static void
try_set_root(struct PG_RigSkeleton* skeleton, int index)
{
    struct PG_RigJoint* joint = &skeleton->joints[index];
    struct PG_RigJoint* rotation = rotation_of(skeleton, joint);
    struct PG_RigJoint* root_rotation;

    if( !rotation )
        return;
    if( skeleton->root < 0 )
    {
        skeleton->root = index;
        return;
    }
    root_rotation = rotation_of(skeleton, &skeleton->joints[skeleton->root]);
    if( !root_rotation )
        return;
    /* The joint whose rotation moves the most labels is the one everything else
     * hangs off — the hips, in practice. */
    if( rotation->label_count > root_rotation->label_count )
        skeleton->root = index;
}

static void
try_set_parent(struct PG_RigSkeleton* skeleton, int index, int candidate)
{
    struct PG_RigJoint* joint = &skeleton->joints[index];
    struct PG_RigJoint* other = &skeleton->joints[candidate];
    struct PG_RigJoint* rotation;
    struct PG_RigJoint* parent_rotation;

    if( joint->id == other->id )
        return;
    if( joint->parent >= 0 && skeleton->joints[joint->parent].id == other->id )
        return;
    if( !rotation_of(skeleton, joint) )
        return;

    rotation = rotation_of(skeleton, other);
    if( !rotation )
        return;
    /* A joint is a candidate parent when its rotation carries every label this
     * joint's ORIGIN does: rotating the parent necessarily moves the child. */
    if( !labels_contain_all(
            rotation->labels, rotation->label_count, joint->labels, joint->label_count) )
        return;

    parent_rotation =
        joint->parent >= 0 ? rotation_of(skeleton, &skeleton->joints[joint->parent]) : NULL;
    /* A smaller superset is a closer relation. */
    if( !parent_rotation || rotation->label_count < parent_rotation->label_count )
        joint->parent = candidate;
}

/*
 * Joints parented to the root that head their own chain get reparented onto the
 * largest such chain.
 *
 * Without this the rig comes out as several disconnected sections all hanging off
 * the root, and the drawn skeleton reads as a scattering of unrelated bones
 * rather than a body.
 */
static void
connect_sections(struct PG_RigSkeleton* skeleton)
{
    int max_id = -1;
    int* section_children;
    int main_root = -1;
    int main_count = -1;
    int root_id;

    if( skeleton->root < 0 )
        return;
    root_id = skeleton->joints[skeleton->root].id;

    for( int i = 0; i < skeleton->joint_count; i++ )
        if( skeleton->joints[i].is_origin && skeleton->joints[i].id > max_id )
            max_id = skeleton->joints[i].id;
    if( max_id < 0 )
        return;

    section_children = calloc((size_t)max_id + 1, sizeof(int));
    if( !section_children )
        return;

    for( int i = 0; i < skeleton->joint_count; i++ )
    {
        struct PG_RigJoint* joint = &skeleton->joints[i];
        int parent_id;
        if( !joint->is_origin || joint->parent < 0 )
            continue;
        parent_id = skeleton->joints[joint->parent].id;
        if( parent_id >= 0 && parent_id <= max_id )
            section_children[parent_id]++;
    }

    for( int i = 0; i < skeleton->joint_count; i++ )
    {
        struct PG_RigJoint* joint = &skeleton->joints[i];
        if( !joint->is_origin || joint->parent < 0 )
            continue;
        if( skeleton->joints[joint->parent].id != root_id )
            continue;
        if( joint->id <= max_id && section_children[joint->id] > main_count )
        {
            main_count = section_children[joint->id];
            main_root = i;
        }
    }

    if( main_root >= 0 )
    {
        int main_id = skeleton->joints[main_root].id;
        for( int i = 0; i < skeleton->joint_count; i++ )
        {
            struct PG_RigJoint* joint = &skeleton->joints[i];
            if( !joint->is_origin || joint->parent < 0 )
                continue;
            if( skeleton->joints[joint->parent].id != root_id )
                continue;
            if( joint->id != main_id && joint->id <= max_id &&
                section_children[joint->id] > 0 )
                joint->parent = main_root;
        }
    }
    free(section_children);
}

void
pg_rig_skeleton_link(struct PG_RigSkeleton* skeleton)
{
    for( int i = 0; i < skeleton->joint_count; i++ )
    {
        if( !skeleton->joints[i].is_origin )
            continue;
        try_set_root(skeleton, i);
        for( int j = 0; j < skeleton->joint_count; j++ )
        {
            if( !skeleton->joints[j].is_origin )
                continue;
            try_set_parent(skeleton, i, j);
        }
    }
    connect_sections(skeleton);
}

void
pg_rig_skeleton_build(
    struct PG_RigSkeleton* skeleton,
    const struct PG_RigFramemap* framemap,
    const struct PG_RigFrame* frame,
    const int* transform_ids,
    int transform_id_count)
{
    pg_rig_skeleton_free(skeleton);
    pg_rig_skeleton_init(skeleton);
    if( !framemap || framemap->length <= 0 )
        return;
    for( int i = 0; i < transform_id_count; i++ )
        pg_rig_skeleton_add_joint(skeleton, framemap, frame, transform_ids[i]);
    pg_rig_skeleton_link(skeleton);
}
