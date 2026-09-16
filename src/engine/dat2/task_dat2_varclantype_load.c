#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every VarClanType's base type (config group 47), once, at boot.
 *
 * A VARCLAN packet carries a var id and then a value whose width is the var's
 * type -- g4, g8 or a string -- and nothing on the wire says which, so the
 * decode needs this table before the first one arrives. The same whole-group
 * eager shape as Task_Dat2WevConfigLoad; an absent group leaves it empty and
 * every VARCLAN is then undecodable, which is what a cache without clan
 * profiles is.
 *
 * The record (class558.method12105): key 0 ends it; key 1 is a ScriptVarType id
 * (the only field this keeps); key 2 a one-byte enum; key 10 a name (gjstr2);
 * key 3 is rejected by the client; any other key reads nothing.
 */

/* ScriptVarType id -> base type (0 int, 1 long, 2 string, -1 not a type), the
 * client's class662.field7070. */
static signed char const k_script_var_base_type[] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  1, -1,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,
     0,  0,  0,  1,  1,  0,  0,  0,  0,  0,  0, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1,  0, -1, -1,  0,  1,  0, -1, -1, -1, -1, -1,
};

static int
varclan_base_type(uint8_t const* data, int size)
{
    int position = 0;
    int base_type = -1;
    while( position < size )
    {
        int const key = data[position++];
        if( key == 0 )
            break;
        switch( key )
        {
        case 1:
        {
            if( position >= size )
                return base_type;
            int const id = data[position++];
            base_type = id < (int)sizeof(k_script_var_base_type) ? k_script_var_base_type[id] : -1;
            break;
        }
        case 2:
            position++;
            break;
        case 10:
            position++; /* gjstr2's leading 0 */
            while( position < size && data[position] != 0 )
                position++;
            position++;
            break;
        case 3:
            fprintf(stderr, "varclantype: unrecognised encoding key 3\n");
            return base_type;
        default:
            break;
        }
    }
    return base_type;
}

struct Task_Dat2VarClanTypeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
};

static int
Task_Dat2VarClanTypeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2VarClanTypeLoad* task = (struct Task_Dat2VarClanTypeLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    int count = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARCLAN);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_VARCLAN);
    if( !archive )
        PT_EXIT(&task->pt);

    filelist = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "varclantype: failed to split config group 47\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }
    for( int i = 0; i < filelist->file_count; i++ )
        if( archive->file_ids[i] + 1 > count )
            count = archive->file_ids[i] + 1;
    if( count > 0 )
    {
        struct ToriRS_VarClanTypes* const types = &task->bc->base.varclan_types;
        free(types->base_type);
        types->base_type = malloc((size_t)count);
        assert(types->base_type);
        memset(types->base_type, -1, (size_t)count);
        types->count = count;
        for( int i = 0; i < filelist->file_count; i++ )
        {
            int const id = archive->file_ids[i];
            if( id >= 0 && id < count && filelist->file_sizes[i] > 0 )
                types->base_type[id] = (signed char)varclan_base_type(
                    (uint8_t const*)filelist->files[i], filelist->file_sizes[i]);
        }
    }
    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    PT_END(&task->pt);
}

static void
Task_Dat2VarClanTypeLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2VarClanTypeLoad_VTable = {
    .run = Task_Dat2VarClanTypeLoad_Run,
    .free = Task_Dat2VarClanTypeLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2VarClanTypeLoad(struct CacheProvider* provider)
{
    assert(provider);
    struct Task_Dat2VarClanTypeLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2VarClanTypeLoad_VTable;
    strcpy(task->task.name, "Dat2VarClanTypeLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    PT_INIT(&task->pt);
    return &task->task;
}
