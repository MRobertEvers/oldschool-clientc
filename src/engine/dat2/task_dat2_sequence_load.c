#include "engine/dat2/task_dat2_sequence_load.h"

#include "engine/cache_provider.h"
#include "engine/toridraw_animation_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "filelist.h"
#include "rscache.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SequenceLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct CacheProvider* provider;
    struct ToriDraw_Scene* scene;
    int seq_id;

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
};

/*
 * Map a file ID to its position in the archive's filelist. Animation archives
 * number their frame files 1-based (and can be sparse), while the filelist stores
 * files densely at positions 0..file_count-1; archive->file_ids[pos] holds each
 * position's actual ID. Returns -1 if the ID is not present.
 */
static int
seq_file_pos_for_id(struct RSCache_Dat2DiskArchive const* archive, int file_id)
{
    if( !archive || !archive->file_ids )
        return -1;
    for( int pos = 0; pos < archive->file_count; pos++ )
        if( archive->file_ids[pos] == file_id )
            return pos;
    return -1;
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

    if( self->framemap && self->frames )
        anim = ToriDraw_AnimationFromRSCache(
            self->framemap,
            (struct RSCache_Dat2Frame const* const*)self->frames,
            self->delays,
            self->frame_count,
            self->seq ? self->seq->frame_step : 0);
    /* Held-item override (reference SeqType.replaceheldleft/right, opcodes 6/7 =
     * dat2 left_hand_item/right_hand_item): a woodcutting/mining-style seq swaps
     * the worn hand item for an obj so the per-frame player-model swap can pull
     * in the tool/log model. Set only these two fields directly rather than
     * through SetSeqMeta — the dat2 sequence decoder memsets its def to 0, so a
     * full meta copy would clobber priority/max_loops with 0 instead of the
     * reference defaults (5/99) the constructor already left in place. The dat2
     * decoder does not convert the 65535 "no override" sentinel and defaults an
     * absent opcode to 0, so map both <=0 and 65535 to -1 (no override); this
     * gives up the rare explicit "hide via 0", which the reference-accurate dat1
     * path still honours through its proper -1 defaults. */
    if( anim && self->seq )
    {
        int held_left = self->seq->left_hand_item;
        int held_right = self->seq->right_hand_item;
        anim->replaceheldleft = ( held_left <= 0 || held_left == 65535 ) ? -1 : held_left;
        anim->replaceheldright = ( held_right <= 0 || held_right == 65535 ) ? -1 : held_right;
        /* Set directly for the same reason as the held-item fields above: a full
         * SetSeqMeta copy would clobber the constructor's priority/max_loops
         * defaults with the memset-0 config. Drives forward draw-padding
         * (reference SeqType.stretches, dat2 opcode 13). */
        anim->stretches = self->seq->stretches ? 1 : 0;
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
seq_take_archive(struct ToriRS_IO* io, int slot)
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

    /* 1. Sequence config (CONFIGS table, Seq group). */
    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE);
    PT_YIELD(&self->pt);
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE);
        if( archive )
        {
            struct RSCache_FileList* fl = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( fl && self->seq_id >= 0 && self->seq_id < fl->file_count &&
                fl->files[self->seq_id] )
            {
                self->seq = RSCache_Dat2ConfigSequenceNewDecode(
                    archive->revision, fl->files[self->seq_id], fl->file_sizes[self->seq_id]);
            }
            if( fl )
                RSCache_FileListFree(fl);
            RSCache_Dat2DiskArchiveFree(archive);
        }
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
            io, 0, 0, RSCACHE_DAT2_DISK_TABLE_ANIMATIONS, (self->cur_frame_id >> 16) & 0xFFFF,
            TORIRS_IO_CACHE_DAT2);
        PT_YIELD(&self->pt);
        self->frame_archive = seq_take_archive(io, 0);
        if( !self->frame_archive )
            continue;
        self->frame_filelist = RSCache_FileListNewFromDecode(
            self->frame_archive->data, self->frame_archive->data_size,
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
        self->cur_framemap_id = RSCache_Dat2FrameFramemapIdFromFile(
            self->frame_filelist->files[self->cur_file_pos],
            self->frame_filelist->file_sizes[self->cur_file_pos]);

        /* Framemap (SKELETONS table). All frames of a seq share one; load once. */
        if( self->cur_framemap_id != self->loaded_framemap_id )
        {
            ToriRS_IO_QueueCache(
                io, 1, 0, RSCACHE_DAT2_DISK_TABLE_SKELETONS, self->cur_framemap_id,
                TORIRS_IO_CACHE_DAT2);
            PT_YIELD(&self->pt);
            {
                struct RSCache_Dat2DiskArchive* fm_archive = seq_take_archive(io, 1);
                if( fm_archive )
                {
                    if( self->framemap )
                        RSCache_Dat2FramemapFree(self->framemap);
                    self->framemap =
                        RSCache_Dat2FramemapNewFromArchive(fm_archive, self->cur_framemap_id);
                    self->loaded_framemap_id = self->cur_framemap_id;
                    RSCache_Dat2DiskArchiveFree(fm_archive);
                }
            }
        }

        if( self->framemap )
            self->frames[self->frame_i] = RSCache_Dat2FrameNewDecode2(
                self->cur_frame_id, self->framemap,
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
