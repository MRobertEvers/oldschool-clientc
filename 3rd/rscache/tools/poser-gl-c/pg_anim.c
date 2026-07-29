#include "pg_anim.h"

#include "pg_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- keyframes ------------------------------------------------------------- */

void
pg_keyframe_init(struct PG_Keyframe* keyframe)
{
    memset(keyframe, 0, sizeof(*keyframe));
    keyframe->id = -1;
    keyframe->frame_id = -1;
    keyframe->length = -1;
    pg_rig_skeleton_init(&keyframe->skeleton);
}

void
pg_keyframe_free_contents(struct PG_Keyframe* keyframe)
{
    if( !keyframe )
        return;
    pg_rig_skeleton_free(&keyframe->skeleton);
}

void
pg_keyframe_copy(struct PG_Keyframe* dst, const struct PG_Keyframe* src, int new_id)
{
    pg_keyframe_init(dst);
    dst->id = new_id;
    dst->frame_id = src->frame_id;
    dst->length = src->length;
    dst->framemap = src->framemap;
    dst->modified = src->modified;
    pg_rig_skeleton_copy(&dst->skeleton, &src->skeleton);
}

void
pg_keyframe_apply(const struct PG_Keyframe* keyframe, struct PG_ModelDef* def)
{
    if( !keyframe || !def )
        return;
    pg_model_reset_transformations(def);
    for( int i = 0; i < keyframe->skeleton.joint_count; i++ )
    {
        const struct PG_RigJoint* joint = &keyframe->skeleton.joints[i];
        pg_model_animate(
            def,
            joint->kind,
            joint->labels,
            joint->label_count,
            joint->delta[0],
            joint->delta[1],
            joint->delta[2]);
    }
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

/* ---- reading a sequence ----------------------------------------------------- */

/*
 * Which framemap transforms this animation touches.
 *
 * Accumulated across every frame of the sequence rather than per frame, so all
 * its keyframes derive the same joints and a joint does not appear and vanish as
 * the animation plays. Kept sorted and deduplicated; the rig is built in this
 * order, which is the order the .pgl encoder then writes.
 */
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

/** The rig views over this tool's own framemap and frame structs. */
static struct PG_RigFramemap
framemap_view(const struct PG_FrameMapDef* framemap)
{
    struct PG_RigFramemap view;
    view.length = framemap->length;
    view.types = framemap->types;
    view.label_sets = framemap->maps;
    view.label_set_lengths = framemap->map_lengths;
    return view;
}

static struct PG_RigFrame
frame_view(const struct PG_FrameDef* frame)
{
    struct PG_RigFrame view;
    view.count = frame->count;
    view.transform_ids = frame->indices;
    view.dx = frame->delta_x;
    view.dy = frame->delta_y;
    view.dz = frame->delta_z;
    return view;
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
        struct PG_Keyframe* keyframe = animation_push_keyframe(anim);
        struct PG_RigFramemap rig_framemap;
        struct PG_RigFrame rig_frame;
        int slot = frame_slot[f];

        if( !keyframe )
            break;
        pg_keyframe_init(keyframe);
        keyframe->id = slot;
        keyframe->frame_id = anim->frame_ids[slot];
        keyframe->length = anim->frame_lengths[slot];
        keyframe->framemap = frame->framemap;

        rig_framemap = framemap_view(frame->framemap);
        rig_frame = frame_view(frame);
        pg_rig_skeleton_build(
            &keyframe->skeleton, &rig_framemap, &rig_frame, indices.values, indices.count);
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
