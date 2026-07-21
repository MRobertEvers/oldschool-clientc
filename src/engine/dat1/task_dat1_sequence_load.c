#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/toridraw_animation_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Dat1 sequences.
 *
 * "seq.dat" gives a frame list where each entry is
 *   (ANIMATIONS-table archive id << 16) | frame index in that archive.
 * One archive holds a set of frames plus the single base they were built
 * against, so a sequence is assembled by loading every distinct archive it
 * names and picking frames out of them in sequence order. Dat2 splits the same
 * data into a frames archive plus a separate framemap group, which is why the
 * two tasks share only the ToriDraw conversion.
 */

enum
{
    DAT1_SEQ_MAX_FRAMES = 512,
};

struct Task_Dat1SequenceLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    struct ToriDraw_Scene* scene;
    int seq_id;

    /* Protothread cursors: locals do not survive the archive-load yields. */
    int frame_i;
    int pending_archive_id;
};

/* Register the assembled animation, or an empty sentinel when the sequence
 * cannot be built. The sentinel makes SceneAnimationGet non-NULL, which is how
 * the tick driver tells "unavailable" apart from "still loading" and stops
 * re-requesting it (same contract as the dat2 task). */
static void
seq_register_result(
    struct Task_Dat1SequenceLoad* self,
    struct ToriDraw_Animation* anim)
{
    if( !anim )
    {
        anim = calloc(1, sizeof(*anim));
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(stderr, "dat1 seq_load: seq=%d unavailable\n", self->seq_id);
    }
    if( anim )
        ToriDraw_SceneAnimationAdd(self->scene, self->seq_id, anim);
}

/* Assemble once every archive the sequence names is cached. */
static struct ToriDraw_Animation*
seq_build_animation(struct Task_Dat1SequenceLoad* self)
{
    struct RSCache_Dat1ConfigSeqList* seq_list = dat1_buildcache_get_seq_list(self->bc);
    struct RSCache_Dat1ConfigSeq* seq;
    struct RSCache_Dat1AnimFrame const* frames[DAT1_SEQ_MAX_FRAMES];
    struct RSCache_Dat1AnimBase const* base = NULL;
    struct ToriDraw_Animation* anim;
    int delays[DAT1_SEQ_MAX_FRAMES];
    int frame_count;

    if( !seq_list || self->seq_id < 0 || self->seq_id >= seq_list->seqs_count )
        return NULL;

    seq = &seq_list->seqs[self->seq_id];
    frame_count = seq->frame_count;
    if( frame_count <= 0 || !seq->frames )
        return NULL;
    if( frame_count > DAT1_SEQ_MAX_FRAMES )
        frame_count = DAT1_SEQ_MAX_FRAMES;

    for( int i = 0; i < frame_count; i++ )
    {
        int archive_id = (seq->frames[i] >> 16) & 0xFFFF;
        int frame_idx = seq->frames[i] & 0xFFFF;
        struct RSCache_Dat1AnimBaseFrames* abf =
            dat1_buildcache_animbaseframes_get(self->bc, archive_id);

        if( !abf || frame_idx < 0 || frame_idx >= abf->frame_count )
            return NULL;

        /* Every frame in a dat1 animation archive shares that archive's base,
         * and a sequence's frames come from archives built against the same
         * rig, so the first one decides the animation's base. */
        if( !base )
            base = abf->base;
        frames[i] = &abf->frames[frame_idx];
        delays[i] = seq->delay ? seq->delay[i] : 0;
    }

    anim = ToriDraw_AnimationFromRSCacheDat1(base, frames, delays, frame_count, 0);
    return anim;
}

/* Next archive the sequence needs that is not cached yet, or -1 when none. */
static int
seq_next_missing_archive(
    struct Task_Dat1SequenceLoad* self,
    int from_frame)
{
    struct RSCache_Dat1ConfigSeqList* seq_list = dat1_buildcache_get_seq_list(self->bc);
    struct RSCache_Dat1ConfigSeq* seq;

    if( !seq_list || self->seq_id < 0 || self->seq_id >= seq_list->seqs_count )
        return -1;

    seq = &seq_list->seqs[self->seq_id];
    if( !seq->frames )
        return -1;

    for( int i = from_frame; i < seq->frame_count && i < DAT1_SEQ_MAX_FRAMES; i++ )
    {
        int archive_id = (seq->frames[i] >> 16) & 0xFFFF;
        if( !dat1_buildcache_animbaseframes_get(self->bc, archive_id) )
        {
            self->frame_i = i;
            return archive_id;
        }
    }
    return -1;
}

static int
Task_Dat1SequenceLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1SequenceLoad* self = (struct Task_Dat1SequenceLoad*)task_base;

    PT_BEGIN(&self->pt);

    if( !dat1_buildcache_get_config_jagfile(self->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&self->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            fprintf(stderr, "Failed to decode dat1 config jagfile for seq %d\n", self->seq_id);
            seq_register_result(self, NULL);
            PT_EXIT(&self->pt);
        }

        dat1_buildcache_set_config_jagfile(self->bc, config_jagfile);
    }

    /* Frame archives, one await each. seq_next_missing_archive re-derives the
     * cursor from the build cache every pass, so nothing has to survive the
     * yield except the id currently in flight. */
    self->frame_i = 0;
    for( ;; )
    {
        self->pending_archive_id = seq_next_missing_archive(self, self->frame_i);
        if( self->pending_archive_id < 0 )
            break;

        RSCache_IO_Dat1AnimBaseFramesLoad(io, 0, self->pending_archive_id);
        PT_YIELD(&self->pt);

        {
            struct RSCache_Dat1AnimBaseFrames* abf = RSCache_IO_Dat1AnimBaseFramesDecode(io, 0);
            if( !abf )
            {
                fprintf(
                    stderr,
                    "Failed to load dat1 anim archive %d for seq %d\n",
                    self->pending_archive_id,
                    self->seq_id);
                seq_register_result(self, NULL);
                PT_EXIT(&self->pt);
            }
            dat1_buildcache_animbaseframes_add(self->bc, self->pending_archive_id, abf);
        }
    }

    seq_register_result(self, seq_build_animation(self));

    PT_END(&self->pt);
}

static void
Task_Dat1SequenceLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1SequenceLoad_VTable = {
    .run = Task_Dat1SequenceLoad_Run,
    .free = Task_Dat1SequenceLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1SequenceLoad(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id)
{
    struct Task_Dat1SequenceLoad* task;

    assert(provider && scene);

    if( seq_id < 0 || ToriDraw_SceneAnimationHas(scene, seq_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SequenceLoad_VTable;
    strncpy(task->task.name, "Dat1SequenceLoad", sizeof(task->task.name) - 1);
    task->bc = (struct Dat1BuildCache*)provider;
    task->scene = scene;
    task->seq_id = seq_id;
    PT_INIT(&task->pt);
    return &task->task;
}
