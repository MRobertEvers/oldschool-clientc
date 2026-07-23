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
 * "seq.dat" frame entries are GLOBAL anim-frame ids (reference SeqType
 * frames[i] = g2). Frames live inside ANIMATIONS-table archives — each
 * archive holds a set of frames (each tagged with its global id) plus the
 * single base they were built against. The version-list "anim_index" file
 * maps global frame id -> archive id (reference OnDemand.animFrameIndex), so
 * a sequence is assembled by resolving each frame id through anim_index,
 * loading the named archives, and picking frames out by id. Dat2 splits the
 * same data into a frames archive plus a separate framemap group, which is
 * why the two tasks share only the ToriDraw conversion.
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

    /* Protothread cursor: locals do not survive the archive-load yields. */
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
    {
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "dat1 seq_load: seq=%d no list (list=%p count=%d)\n",
                self->seq_id,
                (void*)seq_list,
                seq_list ? seq_list->seqs_count : -1);
        return NULL;
    }

    seq = &seq_list->seqs[self->seq_id];
    frame_count = seq->frame_count;
    if( frame_count <= 0 || !seq->frames )
    {
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "dat1 seq_load: seq=%d empty (frame_count=%d frames=%p)\n",
                self->seq_id,
                frame_count,
                (void*)seq->frames);
        return NULL;
    }
    if( frame_count > DAT1_SEQ_MAX_FRAMES )
        frame_count = DAT1_SEQ_MAX_FRAMES;

    for( int i = 0; i < frame_count; i++ )
    {
        int frame_id = seq->frames[i];
        struct RSCache_Dat1AnimFrame const* frame =
            dat1_buildcache_anim_frame_get(self->bc, frame_id);

        if( !frame )
        {
            if( getenv("TORIRS_ANIM_DEBUG") )
                fprintf(
                    stderr,
                    "dat1 seq_load: seq=%d frame %d/%d id=%d not in any archive\n",
                    self->seq_id,
                    i,
                    frame_count,
                    frame_id);
            return NULL;
        }

        /* A sequence's frames come from archives built against the same rig,
         * so the first one decides the animation's base. */
        if( !base )
            base = frame->base;
        frames[i] = frame;
        delays[i] = seq->delay ? seq->delay[i] : 0;
    }

    anim = ToriDraw_AnimationFromRSCacheDat1(base, frames, delays, frame_count, seq->loops);
    if( anim )
    {
        /* Carry the seq-config metadata the entity anim stepping and the
         * walkmerge blend read at runtime (dat1_config_seq decodes opcodes
         * 3/5/8/9/10/11 already; only the plumbing was missing). */
        struct ToriDraw_AnimSeqMeta meta = {
            .walkmerge = seq->walkmerge,
            .priority = seq->priority,
            .max_loops = seq->maxloops,
            .preanim_move = seq->preanim_move,
            .postanim_move = seq->postanim_move,
            .duplicate_behavior = seq->duplicate_behavior,
            .replaceheldleft = seq->replaceheldleft,
            .replaceheldright = seq->replaceheldright,
        };
        /* Reference SeqType.unpack post-decode: unset move behaviors default
         * to DELAYMOVE (0), or MERGE (2) when a walkmerge mask exists. */
        if( meta.preanim_move == -1 )
            meta.preanim_move = seq->walkmerge ? 2 : 0;
        if( meta.postanim_move == -1 )
            meta.postanim_move = seq->walkmerge ? 2 : 0;
        ToriDraw_AnimationSetSeqMeta(anim, &meta);
    }
    return anim;
}

/* Next ANIMATIONS archive to load, or -1 when every frame of the sequence
 * already resolves through the global directory (or nothing is left to
 * load). LostCity's version list ships a zero-filled "anim_index", so frame
 * ids cannot be mapped to archives up front — sweep the archive table in
 * order until the directory covers the needed ids (the reference client
 * simply loads every archive at login). */
static int
seq_next_missing_archive(struct Task_Dat1SequenceLoad* self)
{
    struct RSCache_Dat1ConfigSeqList* seq_list = dat1_buildcache_get_seq_list(self->bc);
    struct RSCache_Dat1ConfigSeq* seq;
    uint16_t const* versions;
    int version_count = 0;
    int unresolved = 0;

    if( !seq_list || self->seq_id < 0 || self->seq_id >= seq_list->seqs_count )
        return -1;
    seq = &seq_list->seqs[self->seq_id];
    if( !seq->frames )
        return -1;

    for( int i = 0; i < seq->frame_count && i < DAT1_SEQ_MAX_FRAMES; i++ )
        if( !dat1_buildcache_anim_frame_get(self->bc, seq->frames[i]) )
            unresolved = 1;
    if( !unresolved )
        return -1;

    versions = dat1_buildcache_get_anim_versions(self->bc, &version_count);
    if( !versions )
        return -1;
    for( int a = 0; a < version_count; a++ )
        if( versions[a] != 0 && !dat1_buildcache_animbaseframes_get(self->bc, a) )
            return a;
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

    /* Version list: "anim_index" maps global frame ids to ANIMATIONS
     * archives; without it no frame can be resolved. */
    if( !dat1_buildcache_get_versionlist_jagfile(self->bc) )
    {
        struct RSCache_FileListDat* versionlist_jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_VERSION_LIST);
        PT_YIELD(&self->pt);

        versionlist_jagfile =
            RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_VERSION_LIST);
        if( !versionlist_jagfile )
        {
            fprintf(
                stderr, "Failed to decode dat1 version list for seq %d\n", self->seq_id);
            seq_register_result(self, NULL);
            PT_EXIT(&self->pt);
        }

        dat1_buildcache_set_versionlist_jagfile(self->bc, versionlist_jagfile);
    }

    /* Frame archives, one await each. seq_next_missing_archive re-derives
     * everything from the build cache every pass, so nothing has to survive
     * the yield except the id currently in flight. */
    for( ;; )
    {
        self->pending_archive_id = seq_next_missing_archive(self);
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
