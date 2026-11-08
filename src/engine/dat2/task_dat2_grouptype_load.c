#include "engine/dat2/dat2_tasks.h"
#include "mem/torirs_mem.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every group config (config group 75), once, at boot: the variables of
 * each GROUP_FULL record, whose widths nothing on the wire states. The eager
 * whole-group shape of Task_Dat2VarClanTypeLoad.
 *
 * The record (deob class437): key 0 ends it; keys 7, 8 and 11 each list a
 * scope's variables, as a u2 count and for each a smart type id and a default
 * by that type's base (int g4, long g8, string).
 */

static int
group_key_scope(int key)
{
    return key == 7 ? 0 : key == 8 ? 1 : key == 11 ? 2 : -1;
}

struct GroupParse
{
    uint8_t const* data;
    int size;
    int pos;
};

static int
parse_u8(struct GroupParse* p)
{
    return p->pos < p->size ? p->data[p->pos++] : (p->pos = p->size + 1, 0);
}

static bool
parse_ok(struct GroupParse const* p)
{
    return p->pos <= p->size;
}

static void
parse_var(struct GroupParse* p, struct ToriRS_GroupTypeVar* var)
{
    int const first = parse_u8(p);
    int const type_id = first < 252 ? first : ((first - 252) << 8) + parse_u8(p);

    var->type_id = type_id;
    var->base = (signed char)torirs_script_var_base_type(type_id);
    if( var->base == 2 )
    {
        int const start = p->pos;

        while( p->pos < p->size && p->data[p->pos] != 0 )
            p->pos++;
        if( p->pos < p->size )
        {
            size_t const n = (size_t)(p->pos - start);

            var->default_string = ToriMem_Alloc(ToriMem_Assets(), n + 1);
            assert(var->default_string);
            memcpy(var->default_string, p->data + start, n);
            var->default_string[n] = '\0';
        }
        p->pos++;
    }
    else if( var->base == 1 )
    {
        uint64_t value = 0;

        for( int i = 0; i < 8; i++ )
            value = (value << 8) | (uint64_t)parse_u8(p);
        var->default_long = (int64_t)value;
    }
    else if( var->base == 0 )
    {
        uint32_t value = 0;

        for( int i = 0; i < 4; i++ )
            value = (value << 8) | (uint32_t)parse_u8(p);
        var->default_int = (int32_t)value;
    }
    else
    {
        /* The deob's reader for a non-type throws; so nothing past it reads. */
        p->pos = p->size + 1;
    }
}

static void
parse_group_type(uint8_t const* data, int size, struct ToriRS_GroupType* type)
{
    struct GroupParse p = { data, size, 0 };

    while( parse_ok(&p) && p.pos < p.size )
    {
        int const key = parse_u8(&p);
        int const scope = group_key_scope(key);
        int count;

        if( key == 0 )
            break;
        if( scope < 0 )
            continue;
        count = parse_u8(&p) << 8;
        count |= parse_u8(&p);
        ToriMem_Free(ToriMem_Assets(), type->vars[scope]);
        type->vars[scope] = NULL;
        type->var_count[scope] = 0;
        if( count <= 0 )
            continue;
        type->vars[scope] =
            ToriMem_Calloc(ToriMem_Assets(), (size_t)count, sizeof(*type->vars[scope]));
        assert(type->vars[scope]);
        type->var_count[scope] = count;
        for( int i = 0; i < count && parse_ok(&p); i++ )
            parse_var(&p, &type->vars[scope][i]);
    }
    if( !parse_ok(&p) )
        fprintf(stderr, "grouptype: record ended early\n");
}

struct Task_Dat2GroupTypeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
};

static int
Task_Dat2GroupTypeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2GroupTypeLoad* task = (struct Task_Dat2GroupTypeLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    int count = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_GROUP);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_GROUP);
    if( !archive )
        PT_EXIT(&task->pt);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "grouptype: failed to split config group 75\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }
    for( int i = 0; i < filelist->file_count; i++ )
        if( archive->file_ids[i] + 1 > count )
            count = archive->file_ids[i] + 1;
    if( count > 0 )
    {
        struct ToriRS_GroupTypes* const types = &task->bc->base.group_types;

        types->types = ToriMem_Calloc(ToriMem_Assets(), (size_t)count, sizeof(*types->types));
        assert(types->types);
        types->count = count;
        for( int i = 0; i < filelist->file_count; i++ )
        {
            int const id = archive->file_ids[i];

            if( id >= 0 && id < count && filelist->file_sizes[i] > 0 )
                parse_group_type(
                    (uint8_t const*)filelist->files[i],
                    filelist->file_sizes[i],
                    &types->types[id]);
        }
    }
    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    PT_END(&task->pt);
}

static void
Task_Dat2GroupTypeLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat2GroupTypeLoad_VTable = {
    .run = Task_Dat2GroupTypeLoad_Run,
    .free = Task_Dat2GroupTypeLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2GroupTypeLoad(struct CacheProvider* provider)
{
    assert(provider);
    struct Task_Dat2GroupTypeLoad* task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2GroupTypeLoad_VTable;
    strcpy(task->task.name, "Dat2GroupTypeLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    PT_INIT(&task->pt);
    return &task->task;
}
