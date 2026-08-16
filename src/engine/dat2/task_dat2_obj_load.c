#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/torirs_objtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ObjLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int obj_id;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

/*
 * Decode one member of the objtype group and hand the adapted objtype to the
 * provider. The raw record is freed immediately: ToriRS_ObjtypeFromRSCacheDat2
 * deep-copies, so keeping it only meant storing every objtype twice.
 */
static int
obj_adapt_member(
    struct Dat2BuildCache* dat2_buildcache,
    struct Dat2Group const* group,
    int pos,
    int obj_id)
{
    struct RSCache_Dat2ConfigObj* rscache_obj;
    struct ToriRS_Objtype* torirs_obj;

    /*
     * Profile, not flags-0. `RSCache_Dat2ConfigObjNewDecode` decodes with no
     * revision flags at all, and the obj decoder's `default:` case is
     * `return false` — stop rather than misalign. A rev-239 record carrying
     * opcode 160 (stackable=2) or 200-202 (EntityOps) therefore stopped there,
     * and since `params` is opcode 249 it was always past the stop: every
     * objtype in this client reached the CS2 VM with param_count == 0, so
     * `oc_param` answered the ParamType default for all of them. The prayer
     * book is built from `oc_param(<prayer obj>, param_1751)`, so it rendered
     * as an empty panel. Every other config type here already decodes with the
     * profile; obj was the one that did not.
     */
    rscache_obj = RSCache_Dat2ConfigObjNewDecodeProfile(
        CacheProvider_Profile(&dat2_buildcache->base),
        group->filelist->files[pos],
        group->filelist->file_sizes[pos]);
    if( !rscache_obj )
        return 0;
    rscache_obj->_id = obj_id;

    torirs_obj = ToriRS_ObjtypeFromRSCacheDat2(obj_id, rscache_obj);
    RSCache_Dat2ConfigObjFree(rscache_obj);
    if( !torirs_obj )
        return 0;

    CacheProvider_ObjtypeAdd(&dat2_buildcache->base, obj_id, torirs_obj);
    return 1;
}

static int
Task_Dat2ObjLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ObjLoad* task = (struct Task_Dat2ObjLoad*)task_base;
    int pos;

    PT_BEGIN(&task->pt);

    /* One record, not the group's 30,891 — see the note in task_dat2_loc_load.c.
     * The group stays split in the LRU, so the next id is an array index. */
    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache,
        RSCACHE_DAT2_TABLE_CONFIGS, RSCACHE_DAT2_CONFIG_KIND_OBJECT, task->group);

    if( !task->group )
    {
        fprintf(
            stderr, "Failed to decode dat2 object config group for obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, task->obj_id);
    if( pos < 0 || !obj_adapt_member(task->bc, task->group, pos, task->obj_id) )
    {
        fprintf(stderr, "Failed to load dat2 obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    /*
     * A note (opcodes 97/98) and a bank placeholder (148/149) carry no name and
     * no examine of their own — they borrow the linked item's, which is what
     * `CacheProvider_ObjtypeGet` patches in as soon as that item is resident.
     * Adopt it here rather than leaving it to whoever happens to load the link
     * first: the icon loader (Task_ObjModelLoad) does pull it in, but a CS2
     * script that only asks for the *name* — `bankmain_drawitem`'s
     * `cc_setopbase("<oc_name($obj0)>")` — has no reason to wait for an icon,
     * and it read "null" whenever it won that race. The link is a member of the
     * group already decompressed above, so this costs one record decode.
     */
    {
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(&task->bc->base, task->obj_id);
        int link = -1;
        int link_pos;

        /* The dat2 decoder's default name is the literal "null", so that — not
         * "" — is what a note or placeholder record carries. */
        if( obj && (obj->name[0] == '\0' || strcmp(obj->name, "null") == 0) )
        {
            if( obj->cert_template > 0 && obj->cert_link > 0 )
                link = obj->cert_link;
            else if( obj->placeholder_template >= 0 && obj->placeholder_link > 0 )
                link = obj->placeholder_link;
        }
        if( link > 0 && !CacheProvider_ObjtypeHas(&task->bc->base, link) )
        {
            link_pos = Dat2Group_IndexOf(task->group, link);
            if( link_pos >= 0 )
                (void)obj_adapt_member(task->bc, task->group, link_pos, link);
        }
    }

    PT_END(&task->pt);
}

static void
Task_Dat2ObjLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ObjLoad_VTable = {
    .run = Task_Dat2ObjLoad_Run,
    .free = Task_Dat2ObjLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ObjLoad(
    struct CacheProvider* provider,
    int obj_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ObjLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ObjtypeHas(provider, obj_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ObjLoad_VTable;
    strcpy(task->task.name, "Dat2ObjLoad");
    task->bc = dat2_buildcache;
    task->obj_id = obj_id;
    PT_INIT(&task->pt);
    return &task->task;
}

/* --- Bulk load-all: decode every objtype in the config group in one pass. Backs
 * the OC_FIND item-name search, which needs every name resident. The group is a
 * single archive, so this is one decompress plus N cheap conversions. --- */

struct Task_Dat2ObjLoadAll
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

static int
Task_Dat2ObjLoadAll_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* cache_io)
{
    struct Task_Dat2ObjLoadAll* task = (struct Task_Dat2ObjLoadAll*)task_base;
    int idx;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, cache_io, 0, task->bc->group_cache,
        RSCACHE_DAT2_TABLE_CONFIGS, RSCACHE_DAT2_CONFIG_KIND_OBJECT, task->group);

    if( !task->group )
    {
        fprintf(stderr, "Failed to decode dat2 object config group for load-all\n");
        PT_EXIT(&task->pt);
    }

    /* This one really does want every record — OC_FIND searches names — but it
     * wants the *adapted* objtypes. Decoding and releasing each raw record in
     * turn keeps one of them live at a time instead of all 30,891. */
    for( idx = 0; idx < task->group->filelist->file_count; idx++ )
    {
        int obj_id = task->group->file_ids ? task->group->file_ids[idx] : idx;

        if( CacheProvider_ObjtypeHas(&task->bc->base, obj_id) )
            continue;
        (void)obj_adapt_member(task->bc, task->group, idx, obj_id);
    }

    task->bc->base.objtypes_all_loaded = true;

    PT_END(&task->pt);
}

static void
Task_Dat2ObjLoadAll_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ObjLoadAll_VTable = {
    .run = Task_Dat2ObjLoadAll_Run,
    .free = Task_Dat2ObjLoadAll_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ObjLoadAll(struct CacheProvider* provider)
{
    struct Task_Dat2ObjLoadAll* task;

    assert(provider);

    if( provider->objtypes_all_loaded )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ObjLoadAll_VTable;
    strcpy(task->task.name, "Dat2ObjLoadAll");
    task->bc = (struct Dat2BuildCache*)provider;
    PT_INIT(&task->pt);
    return &task->task;
}
