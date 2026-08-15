#include "engine/dat2/task_dat2_sequence_load.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "datatypes/dat2_animaya.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_skeletalbase.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/toridraw_animation_from_rscache.h"
#include "filelist.h"
#include "rscache.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SequenceLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct CacheProvider* provider;
    struct ToriDraw_Scene* scene;
    int seq_id;
    struct RSCache_RecordAddress addr;

    /* Split config group holding this sequence. Owned by the buildcache's LRU,
     * borrowed here for the length of the decode below — never freed by us. */
    struct Dat2Group const* group;
    int group_table;
    int group_id;

    struct RSCache_Dat2ConfigSequence* seq;
    struct RSCache_Dat2Framemap* framemap; /* shared rigging (all frames share one) */
    struct RSCache_Dat2Frame** frames;
    int* delays;
    int frame_count;
    int loaded_framemap_id;

    int frame_i;
    struct RSCache_Dat2DiskArchive* frame_archive;
    struct RSCache_FileList* frame_filelist;
    int cur_frame_id;
    int cur_file_id;
    int cur_file_pos; /* filelist array position of cur_file_id (IDs are not 0-based) */
    int cur_framemap_id;

    /* Skeletal (Animaya) branch: the idx22 curve set, the idx1 rig it drives,
     * and the palette baked from the pair — handed to the registered animation. */
    struct RSCache_Dat2AnimMaya* maya;
    struct RSCache_Dat2SkeletalBase* skeletal_base;
    struct ToriDraw_SkeletalAnim* skeletal;
};

/*
 * Map a file ID to its position in the archive's filelist. Animation archives
 * number their frame files 1-based (and can be sparse), while the filelist stores
 * files densely at positions 0..file_count-1; archive->file_ids[pos] holds each
 * position's actual ID. Returns -1 if the ID is not present.
 */
static int
seq_file_pos_for_id(
    struct RSCache_Dat2DiskArchive const* archive,
    int file_id)
{
    assert(archive);
    if( !archive->file_ids )
        return -1;
    for( int pos = 0; pos < archive->file_count; pos++ )
        if( archive->file_ids[pos] == file_id )
            return pos;
    return -1;
}

/* Map dat2 SequenceDefinition fields onto ToriDraw_AnimSeqMeta (dat1 names).
 * Opcode 3 is interleave_leave here / walkmerge on the animation; opcodes 9/10
 * are precedence_animating / priority and default to MERGE when a mask exists
 * (SeqType.ts:154-166). */
static void
seq_apply_meta(
    struct ToriDraw_Animation* anim,
    struct RSCache_Dat2ConfigSequence const* seq)
{
    int held_left;
    int held_right;
    struct ToriDraw_AnimSeqMeta meta;

    assert(anim && seq);
    held_left = seq->left_hand_item;
    held_right = seq->right_hand_item;
    /* A few records still encode the unsigned "no override" as 65535. */
    if( held_left == 65535 )
        held_left = -1;
    if( held_right == 65535 )
        held_right = -1;

    meta = (struct ToriDraw_AnimSeqMeta){
        .walkmerge = seq->interleave_leave,
        .priority = seq->forced_priority,
        .max_loops = seq->max_loops,
        .preanim_move = seq->precedence_animating,
        .postanim_move = seq->priority,
        .duplicate_behavior = seq->reply_mode,
        .replaceheldleft = held_left,
        .replaceheldright = held_right,
        .stretches = seq->stretches ? 1 : 0,
    };
    if( meta.preanim_move == -1 )
        meta.preanim_move = meta.walkmerge ? 2 : 0;
    if( meta.postanim_move == -1 )
        meta.postanim_move = meta.walkmerge ? 2 : 0;
    ToriDraw_AnimationSetSeqMeta(anim, &meta);
}

/* Copy frame sounds from the cache sequence definition to the animation.
 * The animation owns the copy; the cache definition is freed after this
 * registration returns. */
static void
seq_copy_frame_sounds(
    struct ToriDraw_Animation* anim,
    struct RSCache_Dat2ConfigSequence const* seq)
{
    assert(seq);
    if( seq->frame_sounds.count <= 0 )
        return;
    assert(anim);

    anim->frame_sounds.count = seq->frame_sounds.count;
    anim->frame_sounds.frame_indices =
        malloc((size_t)seq->frame_sounds.count * sizeof(int));
    anim->frame_sounds.sounds = malloc(
        (size_t)seq->frame_sounds.count * sizeof(struct ToriDraw_AnimFrameSound));

    if( anim->frame_sounds.frame_indices && anim->frame_sounds.sounds )
    {
        memcpy(
            anim->frame_sounds.frame_indices,
            seq->frame_sounds.frames,
            (size_t)seq->frame_sounds.count * sizeof(int));
        for( int i = 0; i < seq->frame_sounds.count; i++ )
        {
            anim->frame_sounds.sounds[i].id = seq->frame_sounds.sounds[i].id;
            anim->frame_sounds.sounds[i].loops = seq->frame_sounds.sounds[i].loops;
            /* `location` is the audible radius in tiles -- see the struct's
             * header. Dropping it left every frame sound on the queue's
             * fallback radius, so a hammer blow meant to carry three tiles
             * carried twelve. */
            anim->frame_sounds.sounds[i].radius = seq->frame_sounds.sounds[i].location;
            anim->frame_sounds.sounds[i].retain = seq->frame_sounds.sounds[i].retain;
            anim->frame_sounds.sounds[i].weight = seq->frame_sounds.sounds[i].weight;
        }
    }
    else
    {
        /* Allocation failed; clear the partial state */
        free(anim->frame_sounds.frame_indices);
        free(anim->frame_sounds.sounds);
        anim->frame_sounds.frame_indices = NULL;
        anim->frame_sounds.sounds = NULL;
        anim->frame_sounds.count = 0;
    }
}

/*
 * Register what was produced — or, when decode failed, an empty sentinel
 * animation. The sentinel makes ToriDraw_SceneAnimationGet non-NULL so the
 * tick driver (UITreeAnim) can tell "unavailable" (registered, no frames)
 * apart from "still loading" (not registered) without guessing from queue
 * state, and UITreeAnim_RequestMissing never re-requests the sequence.
 */
static void
seq_register_result(struct Task_Dat2SequenceLoad* self)
{
    struct ToriDraw_Animation* anim = NULL;

    /* Skeletal sequence: no base/frames at all, just the baked matrix palette.
     * frame_count still bounds playback (animMayaStart/End when the config gives
     * a range, else the whole bake) so every frame stepper — the world entity
     * sim through World_SeqSource, the scene tick, the renderer's apply — treats
     * it exactly like a classic seq and only the pose call branches. */
    if( self->skeletal )
    {
        anim = calloc(1, sizeof(*anim));
        assert(anim);
        int play_frames = self->skeletal->frame_count;
        if( self->seq && self->seq->anim_maya_end > self->seq->anim_maya_start )
        {
            play_frames = self->seq->anim_maya_end - self->seq->anim_maya_start;
            if( play_frames > self->skeletal->frame_count )
                play_frames = self->skeletal->frame_count;
        }
        anim->skeletal = self->skeletal;
        self->skeletal = NULL;
        anim->frame_count = play_frames > 0 ? play_frames : 1;
        if( self->seq )
        {
            seq_apply_meta(anim, self->seq);
            seq_copy_frame_sounds(anim, self->seq);
        }
        else
        {
            anim->replaceheldleft = -1;
            anim->replaceheldright = -1;
        }
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "seq_load: seq=%d skeletal maya=%d bones=%d baked=%d play=%d\n",
                self->seq_id,
                self->seq ? self->seq->anim_maya_id : -1,
                anim->skeletal->bone_count,
                anim->skeletal->frame_count,
                anim->frame_count);
        ToriDraw_SceneAnimationAdd(self->scene, self->seq_id, anim);
        return;
        /* Allocation failed — drop the bake and register the empty sentinel
         * below so the seq is not re-requested forever. */
        ToriDraw_SkeletalAnimFree(self->skeletal);
        self->skeletal = NULL;
    }

    if( self->framemap && self->frames )
        anim = ToriDraw_AnimationFromRSCache(
            self->framemap,
            (struct RSCache_Dat2Frame const* const*)self->frames,
            self->delays,
            self->frame_count,
            self->seq ? self->seq->frame_step : -1);
    /* Seq-config metadata (walkmerge / priority / preanim / postanim / held
     * items / stretches). Dat2 stores the mask as interleave_leave (opcode 3);
     * SetSeqMeta deep-copies it onto anim->walkmerge for the masked draw path.
     * Decoder now applies RuneLite defaults so a full meta copy is safe. */
    if( anim && self->seq )
    {
        seq_apply_meta(anim, self->seq);
        seq_copy_frame_sounds(anim, self->seq);
    }
    if( !anim )
    {
        anim = calloc(1, sizeof(*anim));
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "seq_load: seq=%d unavailable (config=%p frame_count=%d framemap=%p)\n",
                self->seq_id,
                (void*)self->seq,
                self->seq ? self->seq->frame_count : -1,
                (void*)self->framemap);
    }
    if( anim )
        ToriDraw_SceneAnimationAdd(self->scene, self->seq_id, anim);
}

static struct RSCache_Dat2DiskArchive*
seq_take_archive(
    struct ToriRS_IO* io,
    int slot)
{
    struct ToriRS_IOItem* item = &io->io_slots[slot];
    struct RSCache_Dat2DiskArchive* archive = (struct RSCache_Dat2DiskArchive*)item->data;
    item->data = NULL;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static void
seq_drop_frame_temporaries(struct Task_Dat2SequenceLoad* self)
{
    if( self->frame_filelist )
    {
        RSCache_FileListFree(self->frame_filelist);
        self->frame_filelist = NULL;
    }
    if( self->frame_archive )
    {
        RSCache_Dat2DiskArchiveFree(self->frame_archive);
        self->frame_archive = NULL;
    }
}

static int
Task_Dat2SequenceLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SequenceLoad* self = (struct Task_Dat2SequenceLoad*)base;

    PT_BEGIN(&self->pt);

    /*
     * Sequence config addressing is era-dependent (same story as loc/npc):
     *   OSRS — config table group 12, file id == seq id
     *   RS2  — table 20, sharded 128 per group
     */
    self->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(self->provider), RSCACHE_TYPE_SEQUENCE);

    /* The group stays split in the LRU, so a run of sequence loads out of the
     * same group pays the split once instead of once per id — which for the
     * OSRS layout (every sequence in one group) was a malloc per sequence in
     * the cache, per lookup. */
    self->group_table = self->addr.group_shift ? self->addr.table
                                               : RSCACHE_DAT2_TABLE_CONFIGS;
    self->group_id = self->addr.group_shift
                         ? (self->seq_id >> self->addr.group_shift)
                         : RSCACHE_DAT2_CONFIG_KIND_SEQUENCE;
    DAT2_GROUP_AWAIT(
        &self->pt,
        io,
        0,
        ((struct Dat2BuildCache*)self->provider)->group_cache,
        self->group_table,
        self->group_id,
        self->group);

    if( self->group )
    {
        int want_file = self->addr.group_shift
                            ? (self->seq_id & self->addr.file_mask)
                            : self->seq_id;
        int pos = Dat2Group_IndexOf(self->group, want_file);
        if( pos >= 0 && self->group->filelist->files[pos] )
        {
            self->seq = RSCache_Dat2ConfigSequenceNewDecodeProfile(
                CacheProvider_Profile(self->provider),
                self->group->filelist->files[pos],
                self->group->filelist->file_sizes[pos]);
        }
    }
    /* 1b. Skeletal (Animaya) sequence — no classic frames at all. Modern
     * OldSchool NPCs (the DT2 bosses and everything after) animate this way:
     * seq opcode 13 names an idx22 curve set, and the rig it drives is the
     * skeletal blob in the tail of the idx1 framemap the curve set points at.
     * A cache with only the classic path here leaves them standing still. */
    if( self->seq && self->seq->frame_count <= 0 && self->seq->anim_maya_id > 0 )
    {
        ToriRS_IO_QueueCache(
            io,
            0,
            0,
            RSCACHE_DAT2_TABLE_ANIMAYAS,
            (self->seq->anim_maya_id >> 16) & 0xFFFF,
            TORIRS_IO_CACHE_DAT2);
        PT_YIELD(&self->pt);
        self->frame_archive = seq_take_archive(io, 0);
        if( self->frame_archive )
        {
            self->frame_filelist = RSCache_FileListNewFromDecode(
                self->frame_archive->data,
                self->frame_archive->data_size,
                self->frame_archive->file_count);
            self->cur_file_id = self->seq->anim_maya_id & 0xFFFF;
            self->cur_file_pos = seq_file_pos_for_id(self->frame_archive, self->cur_file_id);
            if( self->frame_filelist && self->cur_file_pos >= 0 &&
                self->cur_file_pos < self->frame_filelist->file_count &&
                self->frame_filelist->files[self->cur_file_pos] )
            {
                self->maya = RSCache_Dat2AnimMayaNewDecode(
                    self->seq->anim_maya_id,
                    self->frame_filelist->files[self->cur_file_pos],
                    self->frame_filelist->file_sizes[self->cur_file_pos]);
            }
        }
        seq_drop_frame_temporaries(self);

        if( self->maya && self->maya->base_id >= 0 )
        {
            self->cur_framemap_id = self->maya->base_id;
            ToriRS_IO_QueueCache(
                io, 1, 0, RSCACHE_DAT2_TABLE_SKELETONS, self->cur_framemap_id,
                TORIRS_IO_CACHE_DAT2);
            PT_YIELD(&self->pt);
            {
                struct RSCache_Dat2DiskArchive* fm_archive = seq_take_archive(io, 1);
                if( fm_archive )
                {
                    /* The rig rides in the framemap's trailing bytes, so decode
                     * the framemap with the cache's own codec and read the tail
                     * rather than re-walking the era-dependent header here. */
                    struct RSCache_Dat2Framemap* fm = RSCache_Dat2FramemapNewFromArchiveProfile(
                        CacheProvider_Profile(self->provider), fm_archive, self->cur_framemap_id);
                    if( fm )
                    {
                        self->skeletal_base = RSCache_Dat2SkeletalBaseNewFromFramemap(fm);
                        RSCache_Dat2FramemapFree(fm);
                    }
                    RSCache_Dat2DiskArchiveFree(fm_archive);
                }
            }
            self->skeletal = ToriDraw_SkeletalAnimFromRSCache(
                self->seq_id, self->maya, self->skeletal_base);
        }
        if( !self->skeletal && getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "seq_load: seq=%d skeletal bake failed (maya_id=%d maya=%p base=%p)\n",
                self->seq_id,
                self->seq->anim_maya_id,
                (void*)self->maya,
                (void*)self->skeletal_base);
        seq_register_result(self);
        PT_EXIT(&self->pt);
    }

    if( !self->seq || self->seq->frame_count <= 0 || !self->seq->frame_ids )
    {
        seq_register_result(self);
        PT_EXIT(&self->pt);
    }

    self->frame_count = self->seq->frame_count;
    self->frames = calloc((size_t)self->frame_count, sizeof(struct RSCache_Dat2Frame*));
    self->delays = calloc((size_t)self->frame_count, sizeof(int));
    self->loaded_framemap_id = -1;

    /* 2. Per frame: load its animation archive, then its framemap, then decode. */
    for( self->frame_i = 0; self->frame_i < self->frame_count; self->frame_i++ )
    {
        self->cur_frame_id = self->seq->frame_ids[self->frame_i];
        self->delays[self->frame_i] =
            self->seq->frame_lengths ? self->seq->frame_lengths[self->frame_i] : 0;
        if( self->cur_frame_id < 0 )
            continue;

        ToriRS_IO_QueueCache(
            io,
            0,
            0,
            RSCACHE_DAT2_TABLE_ANIMATIONS,
            (self->cur_frame_id >> 16) & 0xFFFF,
            TORIRS_IO_CACHE_DAT2);
        PT_YIELD(&self->pt);
        self->frame_archive = seq_take_archive(io, 0);
        if( !self->frame_archive )
            continue;
        self->frame_filelist = RSCache_FileListNewFromDecode(
            self->frame_archive->data,
            self->frame_archive->data_size,
            self->frame_archive->file_count);
        self->cur_file_id = self->cur_frame_id & 0xFFFF;
        /* Frame file IDs are not 0-based/dense — resolve to the filelist position. */
        self->cur_file_pos = seq_file_pos_for_id(self->frame_archive, self->cur_file_id);
        if( !self->frame_filelist || self->cur_file_pos < 0 ||
            self->cur_file_pos >= self->frame_filelist->file_count ||
            !self->frame_filelist->files[self->cur_file_pos] )
        {
            seq_drop_frame_temporaries(self);
            continue;
        }
        self->cur_framemap_id = RSCache_Dat2FrameFramemapIdFromFileProfile(
            CacheProvider_Profile(self->provider),
            self->frame_filelist->files[self->cur_file_pos],
            self->frame_filelist->file_sizes[self->cur_file_pos]);

        /* Framemap (SKELETONS table). All frames of a seq share one; load once. */
        if( self->cur_framemap_id != self->loaded_framemap_id )
        {
            ToriRS_IO_QueueCache(
                io,
                1,
                0,
                RSCACHE_DAT2_TABLE_SKELETONS,
                self->cur_framemap_id,
                TORIRS_IO_CACHE_DAT2);
            PT_YIELD(&self->pt);
            {
                struct RSCache_Dat2DiskArchive* fm_archive = seq_take_archive(io, 1);
                if( fm_archive )
                {
                    if( self->framemap )
                        RSCache_Dat2FramemapFree(self->framemap);
                    self->framemap = RSCache_Dat2FramemapNewFromArchiveProfile(
                        CacheProvider_Profile(self->provider),
                        fm_archive,
                        self->cur_framemap_id);
                    self->loaded_framemap_id = self->cur_framemap_id;
                    RSCache_Dat2DiskArchiveFree(fm_archive);
                }
            }
        }

        if( self->framemap )
            self->frames[self->frame_i] = RSCache_Dat2FrameNewDecodeProfile(
                CacheProvider_Profile(self->provider),
                self->cur_frame_id,
                self->framemap,
                self->frame_filelist->files[self->cur_file_pos],
                self->frame_filelist->file_sizes[self->cur_file_pos]);

        seq_drop_frame_temporaries(self);
    }

    /* 3. Assemble render-ready animation and register it in the scene. */
    seq_register_result(self);

    PT_END(&self->pt);
}

static void
Task_Dat2SequenceLoad_Free(struct ToriRS_Task* base)
{
    struct Task_Dat2SequenceLoad* self = (struct Task_Dat2SequenceLoad*)base;
    seq_drop_frame_temporaries(self);
    if( self->frames )
    {
        for( int i = 0; i < self->frame_count; i++ )
            if( self->frames[i] )
                RSCache_Dat2FrameFree(self->frames[i]);
        free(self->frames);
    }
    free(self->delays);
    if( self->framemap )
        RSCache_Dat2FramemapFree(self->framemap);
    if( self->seq )
        RSCache_Dat2ConfigSequenceFree(self->seq);
    if( self->maya )
        RSCache_Dat2AnimMayaFree(self->maya);
    if( self->skeletal_base )
        RSCache_Dat2SkeletalBaseFree(self->skeletal_base);
    /* Non-NULL only when the bake never reached seq_register_result (task
     * cancelled mid-flight); the registered animation owns it otherwise. */
    ToriDraw_SkeletalAnimFree(self->skeletal);
    free(self);
}

static struct ToriRS_TaskVTable Task_Dat2SequenceLoad_VTable = {
    .run = Task_Dat2SequenceLoad_Run,
    .free = Task_Dat2SequenceLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SequenceLoad(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id)
{
    struct Task_Dat2SequenceLoad* task;
    assert(provider && scene);
    if( seq_id < 0 || ToriDraw_SceneAnimationHas(scene, seq_id) )
        return NULL;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SequenceLoad_VTable;
    strncpy(task->task.name, "Dat2SequenceLoad", sizeof(task->task.name) - 1);
    task->provider = provider;
    task->scene = scene;
    task->seq_id = seq_id;
    task->loaded_framemap_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
