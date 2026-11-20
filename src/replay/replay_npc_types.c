#include "replay/replay_npc_types.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <rscache.h>

#include <assert.h>
#include <stdlib.h>

struct ReplayNpcTypes*
ReplayNpcTypes_New(int count)
{
    struct ReplayNpcTypes* types = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*types));

    assert(types);
    assert(count >= 0);
    types->count = count;
    if( count > 0 )
    {
        types->present = ToriMem_Calloc(ToriMem_Net(), (size_t)count, 1);
        types->recolours = ToriMem_Calloc(ToriMem_Net(), (size_t)count, 1);
        types->retextures = ToriMem_Calloc(ToriMem_Net(), (size_t)count, 1);
        assert(types->present);
        assert(types->recolours);
        assert(types->retextures);
    }
    return types;
}

void
ReplayNpcTypes_Set(
    struct ReplayNpcTypes* types,
    int npc_id,
    int recolours,
    int retextures)
{
    assert(types);
    assert(npc_id >= 0);
    assert(npc_id < types->count);
    /* A config's counts are a g1 each (npc opcodes 40 and 41). */
    assert(recolours >= 0);
    assert(recolours <= 255);
    assert(retextures >= 0);
    assert(retextures <= 255);
    types->present[npc_id] = 1;
    types->recolours[npc_id] = (uint8_t)recolours;
    types->retextures[npc_id] = (uint8_t)retextures;
}

struct ReplayNpcTypes*
ReplayNpcTypes_NewFromCacheDir(
    char const* cache_dir,
    struct RSCache const* profile)
{
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_RecordAddress addr;
    struct RSCache local;
    struct ReplayNpcTypes* types;
    int max_id = -1;
    int table;

    assert(cache_dir);
    assert(profile);
    addr = RSCache_RecordAddressFor(profile, RSCACHE_TYPE_NPC);
    if( addr.group_shift != 0 )
    {
        TORIRS_LOG("replay: this cache keeps its npcs in sharded groups; the ledger reads no npc types\n");
        return NULL;
    }
    disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !disk )
    {
        TORIRS_LOG("replay: no dat2 cache at %s; the ledger reads no npc types\n", cache_dir);
        return NULL;
    }
    /* The disk keeps the profile it is read with: this function's copy,
     * which outlives every read below. */
    local = *profile;
    RSCache_Dat2DiskSetProfile(disk, &local);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = table == RSCACHE_DAT2_DISK_TABLE_ABSENT
                  ? NULL
                  : RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
    {
        TORIRS_LOG("replay: %s has no npc configs; the ledger reads no npc types\n", cache_dir);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return NULL;
    }
    /* Decoded as the client's npc load decodes them: its profile, at the
     * group's own revision (the head-icon shape is gated on it). */
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_NPC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    assert(files);
    for( int i = 0; i < files->file_count; i++ )
    {
        int const id = archive->file_ids ? archive->file_ids[i] : i;

        if( id > max_id )
            max_id = id;
    }
    types = ReplayNpcTypes_New(max_id + 1);
    for( int i = 0; i < files->file_count; i++ )
    {
        int const id = archive->file_ids ? archive->file_ids[i] : i;
        struct RSCache_Dat2ConfigNpc* npc;

        if( files->file_sizes[i] <= 0 )
            continue;
        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(&local, files->files[i], files->file_sizes[i]);
        if( !npc )
            continue;
        ReplayNpcTypes_Set(types, id, npc->recolor_count, npc->retexture_count);
        RSCache_Dat2ConfigNpcFree(npc);
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return types;
}

void
ReplayNpcTypes_Free(struct ReplayNpcTypes* types)
{
    if( !types )
        return;
    ToriMem_Free(ToriMem_Net(), types->present);
    ToriMem_Free(ToriMem_Net(), types->recolours);
    ToriMem_Free(ToriMem_Net(), types->retextures);
    ToriMem_Free(ToriMem_Net(), types);
}

int
ReplayNpcTypes_Lengths(
    void* ctx,
    int old,
    int npc_type,
    int head,
    int* recolours,
    int* retextures)
{
    struct ReplayNpcTypes const* types = ctx;

    (void)head; /* the head block reads the type's same two lists */
    assert(types);
    assert(old < 0);
    assert(npc_type >= 0);
    assert(recolours);
    assert(retextures);
    *recolours = 0;
    *retextures = 0;
    if( npc_type < types->count && types->present[npc_type] )
    {
        *recolours = types->recolours[npc_type];
        *retextures = types->retextures[npc_type];
    }
    return 1;
}
