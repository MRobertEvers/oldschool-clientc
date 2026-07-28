#include "pg_anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char*
pg_transform_type_name(int type)
{
    switch( type )
    {
    case PG_TF_REFERENCE:
        return "Reference";
    case PG_TF_TRANSLATION:
        return "Translation";
    case PG_TF_ROTATION:
        return "Rotation";
    case PG_TF_SCALE:
        return "Scale";
    default:
        return "?";
    }
}

int
pg_transform_default_delta(int type)
{
    return type == PG_TF_SCALE ? 128 : 0;
}

/* ---- keyframe primitives -------------------------------------------------- */

void
pg_keyframe_init(struct PG_Keyframe* kf)
{
    memset(kf, 0, sizeof(*kf));
    kf->id = -1;
    kf->frame_id = -1;
    kf->length = -1;
    kf->root_node = -1;
}

void
pg_keyframe_free_contents(struct PG_Keyframe* kf)
{
    if( !kf )
        return;
    free(kf->transformations);
    kf->transformations = NULL;
    kf->transformation_count = 0;
    kf->transformation_capacity = 0;
}

static struct PG_Transformation*
keyframe_push(struct PG_Keyframe* kf)
{
    struct PG_Transformation* tf;
    if( kf->transformation_count == kf->transformation_capacity )
    {
        int next = kf->transformation_capacity ? kf->transformation_capacity * 2 : 32;
        struct PG_Transformation* grown =
            realloc(kf->transformations, (size_t)next * sizeof(*grown));
        if( !grown )
            return NULL;
        kf->transformations = grown;
        kf->transformation_capacity = next;
    }
    tf = &kf->transformations[kf->transformation_count++];
    memset(tf, 0, sizeof(*tf));
    tf->parent = -1;
    for( int i = 0; i < 4; i++ )
    {
        tf->child_by_type[i] = -1;
        tf->child_order[i] = -1;
    }
    return tf;
}

struct PG_Transformation*
pg_keyframe_find(struct PG_Keyframe* kf, int id)
{
    if( !kf )
        return NULL;
    for( int i = 0; i < kf->transformation_count; i++ )
        if( kf->transformations[i].id == id )
            return &kf->transformations[i];
    return NULL;
}

struct PG_Transformation*
pg_reference_child(struct PG_Keyframe* kf, struct PG_Transformation* node, int type)
{
    if( !kf || !node )
        return NULL;
    if( type == PG_TF_REFERENCE )
        return node;
    if( type < 0 || type > 3 )
        return NULL;
    if( node->child_by_type[type] < 0 )
        return NULL;
    return &kf->transformations[node->child_by_type[type]];
}

void
pg_keyframe_copy(struct PG_Keyframe* dst, const struct PG_Keyframe* src, int new_id)
{
    /*
     * A reference node and its children must stay adjacent and keep their
     * cross-references. Copying the flat array wholesale would preserve the
     * indices too, but only because references and children are appended
     * together — so the safe copy rebuilds them reference by reference, exactly
     * as the reference implementation's copy constructor does.
     */
    int* remap;

    pg_keyframe_init(dst);
    dst->id = new_id;
    dst->frame_id = src->frame_id;
    dst->length = src->length;
    dst->framemap = src->framemap;
    dst->modified = src->modified;

    remap = malloc((size_t)(src->transformation_count > 0 ? src->transformation_count : 1) *
                   sizeof(int));
    if( !remap )
        return;
    for( int i = 0; i < src->transformation_count; i++ )
        remap[i] = -1;

    for( int i = 0; i < src->transformation_count; i++ )
    {
        const struct PG_Transformation* ref = &src->transformations[i];
        struct PG_Transformation* new_ref;
        int ref_index;

        if( !ref->is_reference )
            continue;

        new_ref = keyframe_push(dst);
        if( !new_ref )
            break;
        ref_index = dst->transformation_count - 1;
        *new_ref = *ref;
        new_ref->parent = -1;
        new_ref->child_count = 0;
        for( int t = 0; t < 4; t++ )
        {
            new_ref->child_by_type[t] = -1;
            new_ref->child_order[t] = -1;
        }
        remap[i] = ref_index;

        for( int o = 0; o < ref->child_count; o++ )
        {
            int child_index = ref->child_order[o];
            const struct PG_Transformation* child;
            struct PG_Transformation* new_child;

            if( child_index < 0 || child_index >= src->transformation_count )
                continue;
            child = &src->transformations[child_index];
            new_child = keyframe_push(dst);
            if( !new_child )
                break;
            /* keyframe_push may have moved the array. */
            new_ref = &dst->transformations[ref_index];
            *new_child = *child;
            new_child->parent = -1;
            new_child->child_count = 0;
            for( int t = 0; t < 4; t++ )
            {
                new_child->child_by_type[t] = -1;
                new_child->child_order[t] = -1;
            }
            remap[child_index] = dst->transformation_count - 1;
            new_ref->child_by_type[child->type] = dst->transformation_count - 1;
            new_ref->child_order[new_ref->child_count++] = dst->transformation_count - 1;
        }
    }

    /* Parent links and the root, translated through the remap. */
    for( int i = 0; i < src->transformation_count; i++ )
    {
        int dst_index = remap[i];
        int src_parent = src->transformations[i].parent;
        if( dst_index < 0 )
            continue;
        if( src_parent >= 0 && src_parent < src->transformation_count )
            dst->transformations[dst_index].parent = remap[src_parent];
    }
    dst->root_node =
        (src->root_node >= 0 && src->root_node < src->transformation_count)
            ? remap[src->root_node]
            : -1;
    free(remap);
}

/* ---- skeleton ------------------------------------------------------------- */

static struct PG_Transformation*
rotation_of(struct PG_Keyframe* kf, struct PG_Transformation* node)
{
    if( node->child_by_type[PG_TF_ROTATION] < 0 )
        return NULL;
    return &kf->transformations[node->child_by_type[PG_TF_ROTATION]];
}

static bool
labels_contain_all(
    const int* superset,
    int superset_count,
    const int* subset,
    int subset_count)
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
try_set_root(struct PG_Keyframe* kf, int index)
{
    struct PG_Transformation* node = &kf->transformations[index];
    struct PG_Transformation* rotation = rotation_of(kf, node);
    struct PG_Transformation* root_rotation;

    if( !rotation )
        return;
    if( kf->root_node < 0 )
    {
        kf->root_node = index;
        return;
    }
    root_rotation = rotation_of(kf, &kf->transformations[kf->root_node]);
    if( !root_rotation )
        return;
    /* The joint whose rotation moves the most labels is the one everything else
     * hangs off — the hips, in practice. */
    if( rotation->label_count > root_rotation->label_count )
        kf->root_node = index;
}

static void
try_set_parent(struct PG_Keyframe* kf, int index, int candidate)
{
    struct PG_Transformation* node = &kf->transformations[index];
    struct PG_Transformation* other = &kf->transformations[candidate];
    struct PG_Transformation* rotation;
    struct PG_Transformation* parent_rotation;

    if( node->id == other->id )
        return;
    if( node->parent >= 0 && kf->transformations[node->parent].id == other->id )
        return;
    if( !rotation_of(kf, node) )
        return;

    rotation = rotation_of(kf, other);
    if( !rotation )
        return;
    /* A joint is a parent when its rotation carries every label this joint's
     * reference does: rotating the parent necessarily moves the child. */
    if( !labels_contain_all(rotation->labels, rotation->label_count, node->labels, node->label_count) )
        return;

    parent_rotation =
        node->parent >= 0 ? rotation_of(kf, &kf->transformations[node->parent]) : NULL;
    /* Smaller superset means a closer relation. */
    if( !parent_rotation || rotation->label_count < parent_rotation->label_count )
        node->parent = candidate;
}

/*
 * Joints whose parent is the root but which head their own chain get reparented
 * onto the largest such chain.
 *
 * Without this a rig splits into disconnected sections hanging off the root, and
 * the drawn skeleton reads as a scattering of unrelated bones.
 */
static void
try_connect_sections(struct PG_Keyframe* kf)
{
    int max_id = -1;
    int* section_children;
    int main_root = -1;
    int main_count = -1;

    if( kf->root_node < 0 )
        return;
    for( int i = 0; i < kf->transformation_count; i++ )
        if( kf->transformations[i].is_reference && kf->transformations[i].id > max_id )
            max_id = kf->transformations[i].id;
    if( max_id < 0 )
        return;

    section_children = calloc((size_t)max_id + 1, sizeof(int));
    if( !section_children )
        return;

    for( int i = 0; i < kf->transformation_count; i++ )
    {
        struct PG_Transformation* node = &kf->transformations[i];
        int parent_id;
        if( !node->is_reference || node->parent < 0 )
            continue;
        parent_id = kf->transformations[node->parent].id;
        if( parent_id >= 0 && parent_id <= max_id )
            section_children[parent_id]++;
    }

    for( int i = 0; i < kf->transformation_count; i++ )
    {
        struct PG_Transformation* node = &kf->transformations[i];
        int parent_id;
        if( !node->is_reference || node->parent < 0 )
            continue;
        parent_id = kf->transformations[node->parent].id;
        if( parent_id != kf->transformations[kf->root_node].id )
            continue;
        if( node->id <= max_id && section_children[node->id] > main_count )
        {
            main_count = section_children[node->id];
            main_root = i;
        }
    }

    if( main_root >= 0 )
    {
        int main_id = kf->transformations[main_root].id;
        for( int i = 0; i < kf->transformation_count; i++ )
        {
            struct PG_Transformation* node = &kf->transformations[i];
            int parent_id;
            if( !node->is_reference || node->parent < 0 )
                continue;
            parent_id = kf->transformations[node->parent].id;
            if( parent_id != kf->transformations[kf->root_node].id )
                continue;
            if( node->id != main_id && node->id <= max_id && section_children[node->id] > 0 )
                node->parent = main_root;
        }
    }
    free(section_children);
}

void
pg_keyframe_build_skeleton(struct PG_Keyframe* kf)
{
    for( int i = 0; i < kf->transformation_count; i++ )
    {
        if( !kf->transformations[i].is_reference )
            continue;
        try_set_root(kf, i);
        for( int j = 0; j < kf->transformation_count; j++ )
        {
            if( !kf->transformations[j].is_reference )
                continue;
            try_set_parent(kf, i, j);
        }
    }
    try_connect_sections(kf);
}

void
pg_keyframe_apply(const struct PG_Keyframe* kf, struct PG_ModelDef* def)
{
    if( !kf || !def )
        return;
    pg_model_reset_transformations(def);
    for( int i = 0; i < kf->transformation_count; i++ )
    {
        const struct PG_Transformation* tf = &kf->transformations[i];
        pg_model_animate(
            def, tf->type, tf->labels, tf->label_count, tf->delta[0], tf->delta[1], tf->delta[2]);
    }
}

void
pg_reference_set_position(
    struct PG_Transformation* node,
    const struct PG_ModelDef* def,
    float scale_divisor)
{
    struct PG_Vec3 position =
        pg_vec3((float)node->delta[0], (float)node->delta[1], (float)node->delta[2]);
    float count = 0.0f;

    for( int i = 0; i < node->label_count; i++ )
    {
        int label = node->labels[i];
        if( label < 0 || label >= def->group_count || !def->groups[label] )
            continue;
        count += (float)def->group_counts[label];
        for( int j = 0; j < def->group_counts[label]; j++ )
        {
            int v = def->groups[label][j];
            position.x += (float)def->vertices_x[v];
            position.y += (float)def->vertices_y[v];
            position.z += (float)def->vertices_z[v];
        }
    }
    if( count > 0.0f )
        position = pg_vec3_div(position, count);

    /* The entity shader flips x, so a node drawn in world space has to be
     * flipped here to land on the joint it belongs to. */
    position.x = -position.x;
    node->position = pg_vec3_div(position, scale_divisor);
}

/* ---- animation ------------------------------------------------------------ */

static struct PG_Animation*
animation_alloc(int id)
{
    struct PG_Animation* anim = calloc(1, sizeof(*anim));
    if( !anim )
        return NULL;
    anim->id = id;
    anim->loop_offset = -1;
    anim->left_hand_item = -1;
    anim->right_hand_item = -1;
    return anim;
}

struct PG_Animation*
pg_animation_new_from_sequence(const struct PG_SeqDef* seq)
{
    struct PG_Animation* anim = animation_alloc(seq->id);
    if( !anim )
        return NULL;
    anim->frame_count = seq->frame_count;
    if( seq->frame_count > 0 )
    {
        size_t n = (size_t)seq->frame_count * sizeof(int);
        anim->frame_ids = malloc(n);
        anim->frame_lengths = malloc(n);
        if( anim->frame_ids )
            memcpy(anim->frame_ids, seq->frame_ids, n);
        if( anim->frame_lengths )
            memcpy(anim->frame_lengths, seq->frame_lengths, n);
    }
    anim->loop_offset = seq->loop_offset;
    anim->left_hand_item = seq->left_hand_item;
    anim->right_hand_item = seq->right_hand_item;
    if( seq->debug_name )
    {
        size_t n = strlen(seq->debug_name) + 1;
        anim->name = malloc(n);
        if( anim->name )
            memcpy(anim->name, seq->debug_name, n);
    }
    return anim;
}

struct PG_Animation*
pg_animation_new_empty(int id)
{
    struct PG_Animation* anim = animation_alloc(id);
    if( anim )
        anim->modified = true;
    return anim;
}

void
pg_animation_free(struct PG_Animation* anim)
{
    if( !anim )
        return;
    for( int i = 0; i < anim->keyframe_count; i++ )
        pg_keyframe_free_contents(&anim->keyframes[i]);
    free(anim->keyframes);
    free(anim->frame_ids);
    free(anim->frame_lengths);
    free(anim->name);
    free(anim);
}

static struct PG_Keyframe*
animation_push_keyframe(struct PG_Animation* anim)
{
    if( anim->keyframe_count == anim->keyframe_capacity )
    {
        int next = anim->keyframe_capacity ? anim->keyframe_capacity * 2 : 16;
        struct PG_Keyframe* grown = realloc(anim->keyframes, (size_t)next * sizeof(*grown));
        if( !grown )
            return NULL;
        anim->keyframes = grown;
        anim->keyframe_capacity = next;
    }
    return &anim->keyframes[anim->keyframe_count++];
}

struct PG_Animation*
pg_animation_copy(const struct PG_Animation* src, int new_id)
{
    struct PG_Animation* anim = animation_alloc(new_id);
    if( !anim )
        return NULL;

    for( int i = 0; i < src->keyframe_count; i++ )
    {
        struct PG_Keyframe* dst = animation_push_keyframe(anim);
        if( !dst )
            break;
        pg_keyframe_copy(dst, &src->keyframes[i], src->keyframes[i].id);
    }

    anim->frame_count = src->frame_count;
    if( src->frame_count > 0 )
    {
        size_t n = (size_t)src->frame_count * sizeof(int);
        anim->frame_ids = malloc(n);
        anim->frame_lengths = malloc(n);
        if( anim->frame_ids && src->frame_ids )
            memcpy(anim->frame_ids, src->frame_ids, n);
        if( anim->frame_lengths && src->frame_lengths )
            memcpy(anim->frame_lengths, src->frame_lengths, n);
    }
    anim->left_hand_item = src->left_hand_item;
    anim->right_hand_item = src->right_hand_item;
    anim->loop_offset = src->loop_offset;
    anim->loaded = true;
    anim->modified = true;
    anim->length = pg_animation_calculate_length(anim);
    return anim;
}

int
pg_animation_calculate_length(const struct PG_Animation* anim)
{
    int total = 0;
    for( int i = 0; i < anim->keyframe_count; i++ )
        total += anim->keyframes[i].length;
    return total < PG_MAX_ANIMATION_LENGTH ? total : PG_MAX_ANIMATION_LENGTH;
}

int
pg_animation_frame_index(const struct PG_Animation* anim, int index)
{
    if( anim->keyframe_count <= 0 )
        return 0;
    return pg_floor_mod(index, anim->keyframe_count);
}

bool
pg_animation_insert_keyframe(
    struct PG_Animation* anim,
    const struct PG_Keyframe* keyframe,
    int index)
{
    struct PG_Keyframe copy;
    if( pg_animation_calculate_length(anim) + keyframe->length > PG_MAX_ANIMATION_LENGTH )
        return false;
    if( index < 0 )
        index = 0;
    if( index > anim->keyframe_count )
        index = anim->keyframe_count;

    /* Copy before growing: `keyframe` may point into the very array being grown. */
    pg_keyframe_copy(&copy, keyframe, keyframe->id);
    if( !animation_push_keyframe(anim) )
    {
        pg_keyframe_free_contents(&copy);
        return false;
    }
    memmove(
        &anim->keyframes[index + 1],
        &anim->keyframes[index],
        (size_t)(anim->keyframe_count - 1 - index) * sizeof(struct PG_Keyframe));
    anim->keyframes[index] = copy;
    anim->length = pg_animation_calculate_length(anim);
    return true;
}

void
pg_animation_remove_keyframe_at(struct PG_Animation* anim, int index)
{
    if( index < 0 || index >= anim->keyframe_count )
        return;
    pg_keyframe_free_contents(&anim->keyframes[index]);
    memmove(
        &anim->keyframes[index],
        &anim->keyframes[index + 1],
        (size_t)(anim->keyframe_count - index - 1) * sizeof(struct PG_Keyframe));
    anim->keyframe_count--;
    anim->length = pg_animation_calculate_length(anim);
}

/* ---- parsing a sequence into keyframes ------------------------------------- */

static int
frame_delta_component(const struct PG_FrameDef* frame, int id, int type, int axis)
{
    for( int i = 0; i < frame->count; i++ )
    {
        if( frame->indices[i] != id )
            continue;
        return axis == 0 ? frame->delta_x[i] : (axis == 1 ? frame->delta_y[i] : frame->delta_z[i]);
    }
    return pg_transform_default_delta(type);
}

/*
 * Additional children beyond the first: every transform between this reference
 * and the next one belongs to this joint, so the walk runs forward until it hits
 * another ORIGIN rather than stopping at the first child.
 */
static void
find_children(
    struct PG_Keyframe* kf,
    int ref_index,
    int id,
    const struct PG_FrameDef* frame,
    const struct PG_FrameMapDef* framemap)
{
    int child_id = id + 1;

    while( child_id < framemap->length )
    {
        int child_type = framemap->types[child_id];
        struct PG_Transformation* child;

        if( child_type == PG_TF_REFERENCE )
            break;
        if( child_type == PG_TF_TRANSLATION || child_type == PG_TF_ROTATION ||
            child_type == PG_TF_SCALE )
        {
            struct PG_Transformation* ref;
            child = keyframe_push(kf);
            if( !child )
                return;
            child->id = child_id;
            child->type = child_type;
            child->labels = framemap->maps[child_id];
            child->label_count = framemap->map_lengths[child_id];
            child->delta[0] = frame_delta_component(frame, child_id, child_type, 0);
            child->delta[1] = frame_delta_component(frame, child_id, child_type, 1);
            child->delta[2] = frame_delta_component(frame, child_id, child_type, 2);
            child->is_reference = false;

            ref = &kf->transformations[ref_index];
            if( ref->child_by_type[child_type] < 0 )
            {
                ref->child_order[ref->child_count++] = kf->transformation_count - 1;
            }
            else
            {
                /* A repeated type replaces the earlier one but keeps its slot,
                 * matching the LinkedHashMap the reference builds these in — the
                 * .pgl encoder writes children in exactly this order. */
                for( int o = 0; o < ref->child_count; o++ )
                    if( ref->child_order[o] == ref->child_by_type[child_type] )
                        ref->child_order[o] = kf->transformation_count - 1;
            }
            ref->child_by_type[child_type] = kf->transformation_count - 1;
        }
        child_id++;
    }
}

/* An ordered, deduplicated set of transform indices, built across every frame of
 * the sequence so all keyframes carry the same joints. */
struct IndexSet
{
    int* values;
    int count;
    int capacity;
};

static void
index_set_add(struct IndexSet* set, int value)
{
    int lo = 0, hi = set->count - 1, pos;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( set->values[mid] == value )
            return;
        if( set->values[mid] < value )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    pos = lo;
    if( set->count == set->capacity )
    {
        int next = set->capacity ? set->capacity * 2 : 64;
        int* grown = realloc(set->values, (size_t)next * sizeof(int));
        if( !grown )
            return;
        set->values = grown;
        set->capacity = next;
    }
    memmove(&set->values[pos + 1], &set->values[pos], (size_t)(set->count - pos) * sizeof(int));
    set->values[pos] = value;
    set->count++;
}

void
pg_animation_load(struct PG_Animation* anim, struct PG_Cache* cache, bool advanced_mode)
{
    struct IndexSet indices = { 0 };
    const struct PG_FrameDef** frames;
    int* frame_slot;
    int usable = 0;

    if( !anim || anim->keyframe_count > 0 || anim->frame_count <= 0 )
    {
        if( anim )
            anim->loaded = true;
        return;
    }

    frames = calloc((size_t)anim->frame_count, sizeof(*frames));
    frame_slot = calloc((size_t)anim->frame_count, sizeof(int));
    if( !frames || !frame_slot )
    {
        free(frames);
        free(frame_slot);
        return;
    }

    for( int i = 0; i < anim->frame_count; i++ )
    {
        /* A sequence's frame id is a composite, not an archive id: the top 16
         * bits name the archive and the bottom 16 the file inside it. */
        int composite = anim->frame_ids[i];
        int archive_id = (composite >> 16) & 0xFFFF;
        int file_id = composite & 0xFFFF;
        const struct PG_FrameArchive* archive = pg_cache_frame_archive(cache, archive_id);
        const struct PG_FrameDef* frame = NULL;

        if( !archive )
        {
            fprintf(stderr, "poser-gl: seq %d frame archive %d absent\n", anim->id, archive_id);
            continue;
        }
        for( int f = 0; f < archive->frame_count; f++ )
        {
            if( archive->frames[f].id == file_id )
            {
                frame = &archive->frames[f];
                break;
            }
        }
        if( !frame )
        {
            fprintf(
                stderr,
                "poser-gl: seq %d frame %d absent from archive %d\n",
                anim->id,
                file_id,
                archive_id);
            continue;
        }

        if( advanced_mode )
        {
            int max_id = -1;
            for( int k = 0; k < frame->count; k++ )
                if( frame->indices[k] > max_id )
                    max_id = frame->indices[k];
            if( max_id < 0 )
                continue;
            for( int k = 0; k <= max_id; k++ )
                index_set_add(&indices, k);
        }
        else
        {
            for( int k = 0; k < frame->count; k++ )
                index_set_add(&indices, frame->indices[k]);
        }
        frames[usable] = frame;
        frame_slot[usable] = i;
        usable++;
    }

    for( int f = 0; f < usable; f++ )
    {
        const struct PG_FrameDef* frame = frames[f];
        struct PG_FrameMapDef* framemap = frame->framemap;
        struct PG_Keyframe* kf = animation_push_keyframe(anim);
        int slot = frame_slot[f];

        if( !kf )
            break;
        pg_keyframe_init(kf);
        kf->id = slot;
        kf->frame_id = anim->frame_ids[slot];
        kf->length = anim->frame_lengths[slot];
        kf->framemap = framemap;

        for( int i = 0; i < indices.count; i++ )
        {
            int index = indices.values[i];
            struct PG_Transformation* ref;
            int ref_index;

            if( index < 0 || index >= framemap->length )
                continue;
            if( framemap->types[index] != PG_TF_REFERENCE )
                continue;

            ref = keyframe_push(kf);
            if( !ref )
                break;
            ref_index = kf->transformation_count - 1;
            ref->id = index;
            ref->type = PG_TF_REFERENCE;
            ref->labels = framemap->maps[index];
            ref->label_count = framemap->map_lengths[index];
            ref->delta[0] = frame_delta_component(frame, index, PG_TF_REFERENCE, 0);
            ref->delta[1] = frame_delta_component(frame, index, PG_TF_REFERENCE, 1);
            ref->delta[2] = frame_delta_component(frame, index, PG_TF_REFERENCE, 2);
            ref->is_reference = true;

            find_children(kf, ref_index, index, frame, framemap);

            /* A pivot with nothing hanging off it is not a joint. Dropping it
             * also drops the children that were appended after it — there are
             * none, which is the condition. */
            if( kf->transformations[ref_index].child_count == 0 )
                kf->transformation_count = ref_index;
        }
        pg_keyframe_build_skeleton(kf);
    }

    anim->length = pg_animation_calculate_length(anim);
    anim->loaded = true;
    free(indices.values);
    free(frames);
    free(frame_slot);
}

void
pg_animation_reload(struct PG_Animation* anim, struct PG_Cache* cache, bool advanced_mode)
{
    if( !anim )
        return;
    for( int i = 0; i < anim->keyframe_count; i++ )
        pg_keyframe_free_contents(&anim->keyframes[i]);
    anim->keyframe_count = 0;
    anim->loaded = false;
    pg_animation_load(anim, cache, advanced_mode);
}
