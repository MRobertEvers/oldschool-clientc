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
    int cur_framemap_id;
};

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
        PT_EXIT(&self->pt);

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
        if( !self->frame_filelist || self->cur_file_id >= self->frame_filelist->file_count ||
            !self->frame_filelist->files[self->cur_file_id] )
        {
            seq_drop_frame_temporaries(self);
            continue;
        }
        self->cur_framemap_id = RSCache_Dat2FrameFramemapIdFromFile(
            self->frame_filelist->files[self->cur_file_id],
            self->frame_filelist->file_sizes[self->cur_file_id]);

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
                self->frame_filelist->files[self->cur_file_id],
                self->frame_filelist->file_sizes[self->cur_file_id]);

        seq_drop_frame_temporaries(self);
    }

    /* 3. Assemble render-ready animation and register it in the scene. */
    if( self->framemap )
    {
        struct ToriDraw_Animation* anim = ToriDraw_AnimationFromRSCache(
            self->framemap, (struct RSCache_Dat2Frame const* const*)self->frames, self->delays,
            self->frame_count, self->seq ? self->seq->frame_step : 0);
        if( anim )
            ToriDraw_SceneAnimationAdd(self->scene, self->seq_id, anim);
    }

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
