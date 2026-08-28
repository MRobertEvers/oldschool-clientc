#ifndef RSCACHE_IO_H
#define RSCACHE_IO_H

#include <assert.h>
#include <asyncio.h>
#include <rscache.h>

/*
 * Dat2 loads name a table by role, not by number.
 *
 * `item->u.cache.table_id` on a dat2 item therefore holds an enum RSCache_Dat2Table, and
 * the on-disk id is settled in the platform layer, which holds the open cache and knows
 * its branch (see dat2_resolve_table in platform_x_io.c). Ids are not portable: 19 is
 * OldSchool's worldmap and RS2's objs, so a shim that queued a number would be queueing a
 * different thing depending on which cache happened to be open.
 *
 * Dat1 items are unaffected — they carry RSCACHE_DAT1_DISK_TABLE_* ids, a separate
 * container with its own layout.
 */

/** Does this cache's branch have `table` at all? Tasks for a table only one branch has
 *  (world map, db index, materials) gate on this rather than queueing a load that the
 *  platform layer will only refuse. */
static inline bool
RSCache_IO_ProfileHasDat2Table(
    const struct RSCache* profile,
    enum RSCache_Dat2Table table)
{
    int game = profile ? profile->game : RSCACHE_GAME_OLDSCHOOL;
    return RSCache_Dat2DiskTableForGame(game, table) != RSCACHE_DAT2_DISK_TABLE_ABSENT;
}

static inline void
RSCache_IO_Dat2ModelLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int model_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_MODELS, model_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Model*
RSCache_IO_Dat2ModelDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Model* model = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_MODELS);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;

    model = RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    return model;
}

static inline void
RSCache_IO_Dat1ModelLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int model_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT1_DISK_TABLE_MODELS, model_id, TORIRS_IO_CACHE_DAT1);
}

static inline struct RSCache_Model*
RSCache_IO_Dat1ModelDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Model* model = NULL;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT1_DISK_TABLE_MODELS);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT1);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;

    model = RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return model;
}

static inline void
RSCache_IO_ModelLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int model_id)
{
    RSCache_IO_Dat2ModelLoad(io, slot_id, model_id);
}

static inline struct RSCache_Model*
RSCache_IO_ModelDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    return RSCache_IO_Dat2ModelDecode(io, slot_id);
}

static inline void
RSCache_IO_Dat2ComponentPackLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int component_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_INTERFACES, component_id, TORIRS_IO_CACHE_DAT2);
}

/* interfaces_revision is the interfaces reference-table version
 * (RSCache_ReferenceTable.version); it selects the revision-gated if1/if3
 * fields. Pass RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN when the caller
 * has not loaded that table.
 *
 * `profile` picks the IF3 field-layout family. It cannot be inferred from the
 * revision alone — RS2 (634/643) numbers its reference tables in the same small
 * range OldSchool used before unix timestamps — so the branch has to come from
 * the identified cache. Passing NULL keeps the OldSchool layout. */
static inline struct RSCache_Dat2ComponentPack*
RSCache_IO_Dat2ComponentPackDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int interfaces_revision,
    const struct RSCache* profile)
{
    assert(io);
    struct RSCache_Dat2ComponentPack* component = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_INTERFACES);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;

    component = RSCache_Dat2ComponentPackNewFromArchive(
        archive,
        archive->archive_id,
        profile ? RSCache_Dat2ComponentDecodeRevFromProfile(profile, interfaces_revision)
                : RSCache_Dat2ComponentDecodeRevOsrs(interfaces_revision));
    RSCache_Dat2DiskArchiveFree(archive);
    return component;
}

static inline void
RSCache_IO_ClientScriptLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int script_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_CLIENTSCRIPT, script_id, TORIRS_IO_CACHE_DAT2);
}

/**
 * Trailer family. Legacy (14-byte footer) is tried first because every cache
 * here decodes that way, then modern (18 bytes, the long-locals/args fields) as
 * a fallback. Not the other way round, and not `RSCache_ClientScriptFlags`
 * alone: preferring modern on cache.osrs239 (revision >= 237, so the flag says
 * modern) made script 8489 decode into a different opcode stream that reached
 * an opcode the VM has no signature for. The decode is a validating *try* —
 * op_count must match and the body must consume exactly up to the trailer — so
 * ordering only decides which family gets first refusal, never whether a
 * mismatch is caught.
 *
 * The fallback is what matters: on cache.osrs239 scripts 392, 714 and 1707 (the
 * world map onload and its helpers) only decode as modern. Without it they load
 * as nothing and their panels render as empty widgets, with no error near them.
 */
static inline struct RSCache_ClientScript*
RSCache_IO_ClientScriptDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int script_id)
{
    assert(io);
    struct RSCache_ClientScript* script = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
    assert(item->u.cache.archive_id == script_id);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    if( item->error_code != 0 || !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    ToriRS_IO_ClearItem(item);

    /* The first attempt is quiet: on a modern cache every modern-trailer script
     * refuses here and loads on the next line, so reporting it named scripts
     * that were about to decode fine. The fallback stays loud — if both refuse,
     * the script really is undecodable and that is worth a line. */
    script = RSCache_ClientScriptNewFromArchive(
        archive,
        script_id,
        RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY | RSCACHE_CLIENTSCRIPT_DECODE_QUIET);
    if( !script )
        script = RSCache_ClientScriptNewFromArchive(
            archive, script_id, RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN);
    RSCache_Dat2DiskArchiveFree(archive);
    return script;
}

/** Queue a CONFIGS-table jagfile archive (RSCACHE_DAT1_CONFIG_* archive id). */
static inline void
RSCache_IO_Dat1JagfileLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int jag_archive_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT1_DISK_TABLE_CONFIGS, jag_archive_id, TORIRS_IO_CACHE_DAT1);
}

static inline struct RSCache_FileListDat*
RSCache_IO_Dat1JagfileDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int expected_archive_id)
{
    assert(io);
    struct RSCache_FileListDat* filelist = NULL;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS);
    assert(item->u.cache.archive_id == expected_archive_id);
    (void)expected_archive_id; /* assert-only: the OPT=1 lane compiles with -DNDEBUG */
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT1);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;

    filelist = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return filelist;
}

/*
 * Dat1 map chunk. archive_id carries the map square id, not a cache archive
 * id: only the disk layer holds the versionlist map_index that maps a region
 * to its terrain and loc archives (see load_cache_item_dat1). Both decoders
 * return NULL when the cache ships no square for that region.
 */
static inline void
RSCache_IO_Dat1MapTerrainLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int map_x,
    int map_z)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io,
        slot_id,
        0,
        RSCACHE_DAT1_DISK_TABLE_MAPS,
        RSCache_MapSquareId(map_x, map_z),
        TORIRS_IO_CACHE_DAT1_MAP_TERRAIN);
}

static inline struct RSCache_MapTerrain*
RSCache_IO_Dat1MapTerrainDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int map_x,
    int map_z)
{
    assert(io);
    struct RSCache_MapTerrain* terrain = NULL;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    if( !archive )
        return NULL;

    /* Dat1 terrain stores overlay/underlay ids in one byte; the dat2 format
     * widened them to two. */
    terrain = RSCache_MapTerrainNewFromDecodeFlags(
        archive->data, archive->data_size, map_x, map_z, RSCACHE_MAP_TERRAIN_DECODE_U8);
    RSCache_Dat1DiskArchiveFree(archive);
    return terrain;
}

static inline void
RSCache_IO_Dat1MapSceneryLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int map_x,
    int map_z)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io,
        slot_id,
        0,
        RSCACHE_DAT1_DISK_TABLE_MAPS,
        RSCache_MapSquareId(map_x, map_z),
        TORIRS_IO_CACHE_DAT1_MAP_SCENERY);
}

static inline struct RSCache_MapLocs*
RSCache_IO_Dat1MapSceneryDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_MapLocs* locs = NULL;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    if( !archive )
        return NULL;

    locs = RSCache_MapLocsNewDecode(archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return locs;
}

/** Queue an ANIMATIONS-table archive (one AnimBaseFrames set). */
static inline void
RSCache_IO_Dat1AnimBaseFramesLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int animbaseframes_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io,
        slot_id,
        0,
        RSCACHE_DAT1_DISK_TABLE_ANIMATIONS,
        animbaseframes_id,
        TORIRS_IO_CACHE_DAT1);
}

static inline struct RSCache_Dat1AnimBaseFrames*
RSCache_IO_Dat1AnimBaseFramesDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat1AnimBaseFrames* abf = NULL;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT1_DISK_TABLE_ANIMATIONS);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    if( !archive )
        return NULL;

    abf = RSCache_Dat1AnimBaseFramesNewDecode(archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return abf;
}

static inline void
RSCache_IO_Dat1ConfigJagfileLoad(
    struct ToriRS_IO* io,
    int slot_id)
{
    RSCache_IO_Dat1JagfileLoad(io, slot_id, RSCACHE_DAT1_CONFIG_CONFIGS);
}

static inline struct RSCache_FileListDat*
RSCache_IO_Dat1ConfigJagfileDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    return RSCache_IO_Dat1JagfileDecode(io, slot_id, RSCACHE_DAT1_CONFIG_CONFIGS);
}

/*
 * Load a record group by table and group id, rather than by config kind.
 *
 * The RS2 (643) branch promotes loc, npc, obj, seq and spotanim out of the config table
 * into their own tables, sharded into groups — so a record is addressed by
 * (table, id >> shift) with the record at file (id & mask). See
 * RSCache_RecordAddressFor, which supplies all three. OSRS keeps them as one config
 * group, which is the `group_shift == 0` case and still goes through
 * RSCache_IO_Dat2ConfigGroupLoad.
 */
static inline void
RSCache_IO_Dat2RecordGroupLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id,
    int group_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(io, slot_id, 0, table_id, group_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2RecordGroupDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    assert(io);
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == table_id);
    (void)table_id; /* assert-only: the OPT=1 lane compiles with -DNDEBUG */

    struct RSCache_Dat2DiskArchive* archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2ConfigGroupLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int config_kind)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_CONFIGS, config_kind, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2ConfigGroupDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int expected_config_kind)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_CONFIGS);
    assert(item->u.cache.archive_id == expected_config_kind);
    (void)expected_config_kind; /* assert-only: the OPT=1 lane compiles with -DNDEBUG */
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;
    return archive;
}

static inline void
RSCache_IO_Dat2ReferenceTableLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueReferenceTable(io, slot_id, table_id);
}

static inline struct RSCache_ReferenceTable*
RSCache_IO_Dat2ReferenceTableDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_ReferenceTable* table = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_REFERENCE_TABLE);

    table = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return table;
}

static inline void
RSCache_IO_Dat2MapArchiveLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int archive_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_MAPS, archive_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2MapArchiveDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_MAPS);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    if( !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

/**
 * One group of the world map geography table (dat2 table 18): the tiles behind
 * one or more compositemap records, addressed by the group/file pair those
 * records carry.
 */
static inline void
RSCache_IO_Dat2WorldMapGeographyLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int group_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY, group_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2WorldMapGeographyDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

/** One group of the world map ground table (dat2 table 20): a 64x64 PNG of a
 *  region's blended ground colours, addressed by the same group id as its
 *  geography. */
static inline void
RSCache_IO_Dat2WorldMapGroundLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int group_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_WORLDMAP_GROUND, group_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2WorldMapGroundDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_WORLDMAP_GROUND);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2WorldMapArchiveLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int archive_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_WORLDMAP, archive_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2WorldMapArchiveDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_WORLDMAP);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    if( !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

/* DBTABLEINDEX (cache table 21): one archive per table id, each holding the
 * table's index files (master + one per column). */
static inline void
RSCache_IO_Dat2DbTableIndexLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_DBTABLE_INDEX, table_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2DbTableIndexDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_DBTABLE_INDEX);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    if( !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2TextureGroupLoad(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(io, slot_id, 0, RSCACHE_DAT2_TABLE_TEXTURES, 0, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2TextureGroupDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_TEXTURES);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);

    /* Archive absent from this cache (e.g. a live server references an id our
     * local dat2 does not carry): the disk layer logged the miss and left data
     * NULL. Fail gracefully — callers already handle a NULL decode — rather than
     * assert and abort the client. */
    if( archive == NULL )
        return NULL;
    return archive;
}

/*
 * Procedural-texture loads (RS2 / 643).
 *
 * Two shapes, both different from the sprite-backed system above. That one has a single
 * texture *group* holding every definition as a file; RS2 gives each texture id its own
 * archive in table 9, and puts the shared material table in table 26 group 0.
 */
static inline void
RSCache_IO_Dat2MaterialTableLoad(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_MATERIALS, 0, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2MaterialTableDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_MATERIALS);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2ProcTextureLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int texture_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_TEXTURES, texture_id, TORIRS_IO_CACHE_DAT2);
}

static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2ProcTextureDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_TEXTURES);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2SpriteLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int sprite_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_SPRITES, sprite_id, TORIRS_IO_CACHE_DAT2);
}

/** Takes ownership of the archive from the slot; converter frees it.
 *  Returns NULL when the platform failed to load the archive. */
static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2SpriteDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_SPRITES);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    if( item->error_code != 0 || !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    ToriRS_IO_ClearItem(item);
    return archive;
}

/*
 * Music tables: tracks (6), jingles (11), samples (14) and patches (15).
 *
 * One generic pair rather than four, because the caller already knows which
 * table it wants and every one of them is "give me this archive whole": a song
 * is one blob, a patch is one blob, a sample is one blob. The table is passed
 * through so the decode side can assert it got what it asked for.
 */
static inline void
RSCache_IO_Dat2MusicLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id,
    int archive_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(io, slot_id, 0, table_id, archive_id, TORIRS_IO_CACHE_DAT2);
}

/** Takes ownership of the archive from the slot. NULL when the cache has no
 *  archive for that id, which is normal: a song can name a patch a local cache
 *  does not carry. */
static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2MusicDecode(
    struct ToriRS_IO* io,
    int slot_id,
    int table_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == table_id);
    (void)table_id; /* assert-only: the OPT=1 lane compiles with -DNDEBUG */
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    if( item->error_code != 0 || !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    ToriRS_IO_ClearItem(item);
    return archive;
}

/* Sound effects: one archive per effect id. Modern OldSchool sometimes makes it
 * a two-file group (synth record + compressed sample), so the archive is handed
 * over whole and the caller splits it — see task_dat2_sound_load.c. */
static inline void
RSCache_IO_Dat2SoundLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int sound_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_SOUND_EFFECTS, sound_id, TORIRS_IO_CACHE_DAT2);
}

/** Takes ownership of the archive from the slot. NULL when the cache has no
 *  archive for that id — normal, since servers reference ids a local cache may
 *  not carry. */
static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2SoundDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_SOUND_EFFECTS);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    if( item->error_code != 0 || !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    ToriRS_IO_ClearItem(item);
    return archive;
}

static inline void
RSCache_IO_Dat2FontLoad(
    struct ToriRS_IO* io,
    int slot_id,
    int font_id)
{
    assert(io);
    assert(ToriRS_IO_TaskSlot(io, slot_id)->kind == TORIRS_IOK_NONE);
    ToriRS_IO_QueueCache(
        io, slot_id, 0, RSCACHE_DAT2_TABLE_FONTS, font_id, TORIRS_IO_CACHE_DAT2);
}

/** Takes ownership of the archive from the slot; converter frees it.
 *  Returns NULL when the platform failed to load the archive. */
static inline struct RSCache_Dat2DiskArchive*
RSCache_IO_Dat2FontDecode(
    struct ToriRS_IO* io,
    int slot_id)
{
    assert(io);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, slot_id);
    assert(item->kind == TORIRS_IOK_CACHE);
    assert(item->u.cache.table_id == RSCACHE_DAT2_TABLE_FONTS);
    assert(item->u.cache.flags == TORIRS_IO_CACHE_DAT2);

    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    if( item->error_code != 0 || !archive )
    {
        ToriRS_IO_ClearItem(item);
        return NULL;
    }
    ToriRS_IO_ClearItem(item);
    return archive;
}

#endif
