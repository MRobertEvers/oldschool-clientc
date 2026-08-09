#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_soundscape.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Ambient soundscapes (config group 15), whole-group and eager at boot.
 *
 * Same shape and the same reasons as Task_Dat2HitsplatLoad: the consumer is the
 * audio tick, which runs inside the logic tick with nowhere to yield to a cache
 * read, and the group is tiny -- eight records averaging 58 bytes in
 * `cache.osrs239`, so this is one group read rather than a walk.
 *
 * A cache without the group is normal, not an error: group 15 is an OldSchool
 * 231+ addition and `cache.osrs230` has none. The table simply stays empty and
 * RS_Audio_SetAmbient falls back to treating an AMBIENTSOUND_START id as a
 * sound-effect id, which is the only thing that revision can mean by it.
 */

struct Task_Dat2SoundscapeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RS_Soundscapes* soundscapes;
    /* Must outlive the PT_YIELD in the preload walk below. */
    int preload_index;
    int preload_sub;
};

/** Copy one decoded record into the game-side mirror, applying the caps. */
static void
soundscape_copy(
    struct RS_Soundscape* dst,
    const struct RSCache_Dat2ConfigSoundscape* src)
{
    memset(dst, 0, sizeof(*dst));
    dst->present = true;
    dst->fade_in_ms = src->fade_in_ms;
    dst->fade_out_ms = src->fade_out_ms;

    dst->loop_count = src->loop_count < RS_SOUNDSCAPE_MAX_LOOPS ? src->loop_count
                                                                : RS_SOUNDSCAPE_MAX_LOOPS;
    for( int i = 0; i < dst->loop_count; i++ )
        dst->loop_ids[i] = src->loop_ids[i];

    dst->set_count = src->set_count < RS_SOUNDSCAPE_MAX_SETS ? src->set_count
                                                             : RS_SOUNDSCAPE_MAX_SETS;
    for( int i = 0; i < dst->set_count; i++ )
    {
        const struct RSCache_SoundscapeSet* s = &src->sets[i];
        int n = s->id_count < RS_SOUNDSCAPE_MAX_SET_IDS ? s->id_count : RS_SOUNDSCAPE_MAX_SET_IDS;
        dst->sets[i].id_count = n;
        dst->sets[i].min_ms = s->min_ms;
        dst->sets[i].max_ms = s->max_ms;
        for( int j = 0; j < n; j++ )
            dst->sets[i].ids[j] = s->ids[j];
    }
}

static int
Task_Dat2SoundscapeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SoundscapeLoad* task = (struct Task_Dat2SoundscapeLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RS_Soundscape* entries = NULL;
    int count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_SOUNDSCAPE);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_SOUNDSCAPE);
    if( !archive )
        PT_EXIT(&task->pt); /* pre-231 cache: absent is normal */

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* Ids are sparse; size from the largest rather than from the count. */
    for( int i = 0; i < filelist->file_count; i++ )
        if( archive->file_ids[i] + 1 > count )
            count = archive->file_ids[i] + 1;
    if( count <= 0 )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    entries = calloc((size_t)count, sizeof(*entries));
    if( !entries )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigSoundscape entry;

        if( id < 0 || id >= count || filelist->file_sizes[i] <= 0 )
            continue;
        RSCache_Dat2ConfigSoundscapeDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        if( entry._consumed != filelist->file_sizes[i] )
            fprintf(
                stderr,
                "soundscape %d: decode consumed %d of %d bytes\n",
                id,
                entry._consumed,
                filelist->file_sizes[i]);
        soundscape_copy(&entries[id], &entry);
        RSCache_Dat2ConfigSoundscapeFreeInplace(&entry);
        decoded++;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !RS_Soundscapes_SetEntries(task->soundscapes, entries, count) )
    {
        free(entries);
        PT_EXIT(&task->pt);
    }
    printf("soundscape load: %d ids (%d records)\n", count, decoded);

    /*
     * Bring the clips into residence.
     *
     * The same argument the hitsplat sprites make: the audio tick binds a
     * *resident* clip and cannot load one, so without this the first few seconds
     * in a new region are silent while the bed loads -- which is exactly the
     * stretch the bed exists for. A soundscape's whole clip set is a handful of
     * short effects.
     */
    for( task->preload_index = 0; task->preload_index < task->soundscapes->count;
         task->preload_index++ )
    {
        struct RS_Soundscape const* s = &task->soundscapes->entries[task->preload_index];
        if( !s->present )
            continue;
        for( task->preload_sub = 0; task->preload_sub < s->loop_count; task->preload_sub++ )
            TASK_AWAITEX_IF(
                &task->pt,
                io,
                CreateTask_SoundLoad(&task->bc->base, s->loop_ids[task->preload_sub]));
    }

    PT_END(&task->pt);
}

static void
Task_Dat2SoundscapeLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SoundscapeLoad_VTable = {
    .run = Task_Dat2SoundscapeLoad_Run,
    .free = Task_Dat2SoundscapeLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SoundscapeLoad(
    struct CacheProvider* provider,
    struct RS_Soundscapes* soundscapes)
{
    assert(provider);
    assert(soundscapes);

    struct Task_Dat2SoundscapeLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SoundscapeLoad_VTable;
    strcpy(task->task.name, "Dat2SoundscapeLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->soundscapes = soundscapes;
    PT_INIT(&task->pt);
    return &task->task;
}
