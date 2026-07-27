#ifndef CACHE_PROVIDER_H
#define CACHE_PROVIDER_H

#include "torirs_types.h"

#include <assert.h>
#include <hmap.h>
#include <rscache.h>
#include <stdbool.h>

struct CS2VM2_Script;
struct CacheProviderVTable;
struct ToriDraw_Scene;
struct ToriRS_Sprite;
struct ToriRS_Font;
struct ToriRS_Enum;
struct ToriRS_Struct;
struct ToriRS_ParamType;
struct ToriRS_DbTableIndex;
struct RSCache_Dat2ConfigDbRow;
struct RSCache_WorldMapGeography;
struct ToriRS_WorldMapRegionSource;

struct CacheProvider
{
    // This MUST be the very first member of the struct.
    struct CacheProviderVTable* vtable;

    struct HMap* model_cache;
    struct HMap* sprite_cache;
    struct HMap* font_cache;
    struct HMap* enum_cache;
    struct HMap* struct_cache;
    struct HMap* param_cache;
    struct HMap* componentpack_cache;
    struct HMap* clientscript_cache;
    struct HMap* objtype_cache;
    struct HMap* npctype_cache;
    struct HMap* spotanimtype_cache;
    /** Rendered sound effects (ToriRS_Sound) by effect id. */
    struct HMap* sound_cache;
    struct HMap* idk_cache;
    struct HMap* map_terrain_cache;
    struct HMap* map_scenery_cache;
    struct HMap* location_cache;
    struct HMap* flotype_cache;
    struct HMap* underlay_cache;
    struct HMap* texture_cache;
    struct HMap* mapelement_cache;
    /** name_hash (ArchiveNameHashDat2) → sprite archive id */
    struct HMap* sprite_name_cache;
    /** Lowercased-objtype-name hash → obj id, populated alongside objtype_cache
     *  in CacheProvider_ObjtypeAdd. Backs CacheProvider_ObjtypeIdByName and the
     *  OC_FIND item-name search (see CacheProvider_ObjtypeSearchByName). */
    struct HMap* objtype_name_cache;
    /** Set once every objtype in the config group has been decoded into
     *  objtype_cache (CacheProvider_ObjtypeLoadAll / Task_ObjLoadAll). The
     *  OC_FIND scan needs all names resident; this lets it load them exactly
     *  once instead of on every search. */
    bool objtypes_all_loaded;
    /** Every world map area, loaded as one object (cache table 19). NULL until
     *  the load task runs, and on caches that have no world map at all. */
    struct ToriRS_WorldMapAreas* worldmap_areas;
    /** DBROW config (kind 38) keyed by row id; the decoded rscache struct is
     *  owned directly. Backs DB_GETROW / DB_GETFIELD / DB_GETROWTABLE. */
    struct HMap* dbrow_cache;
    /** DBTABLEINDEX (cache table 21) keyed by table id; one bundle of index
     *  files per table. Backs DB_FIND / DB_FINDALL. */
    struct HMap* dbindex_cache;
    /** World map geography (cache table 18) keyed by
     *  CacheProvider_WorldMapGeographyKey — the tiles of one map surface
     *  region, decoded on demand and dropped once the renderer has baked it. */
    struct HMap* worldmap_geography_cache;

    /**
     * Which cache this provider is reading: game, epoch, revision and quirks,
     * in the one value rscache's decoders take.
     *
     * Set once at boot from the manifest (see CacheProvider_SetProfile) and read
     * wherever a decoder needs to know the era. Before this existed, era information
     * reached decoders as a bare `int revision` lifted from whichever JS5 archive the
     * record came from — a per-archive counter whose *units* changed between eras —
     * or as a raw flag constant written out at the call site.
     *
     * Unset until CacheProvider_SetProfile. CacheProvider_Profile asserts the
     * identity is stated, so a provider used before SetProfile crashes instead of
     * decoding as the wrong branch.
     */
    struct RSCache profile;
};

/** Record which cache this provider reads. Call once, before any load task runs. */
void
CacheProvider_SetProfile(
    struct CacheProvider* provider,
    const struct RSCache* profile);

/** The cache profile. Asserts identity is set. */
static inline const struct RSCache*
CacheProvider_Profile(const struct CacheProvider* provider)
{
    assert(provider);
    assert(RSCache_ProfileIsIdentified(&provider->profile));
    return &provider->profile;
}

/** World-builder map key: encodes a map square (map_x, map_z) into a single int. */
static inline int
CacheProvider_MapId(
    int map_x,
    int map_z)
{
    return (map_x << 16) | (map_z & 0xFFFF);
}

/**
 * Descriptor for a name-keyed dat1 media-jagfile sprite load (from a RevConfig
 * [sprite:] section). Neutral so cache_provider stays decoupled from
 * uitree_builder_manifest. All pointers borrowed for the CreateTask call only —
 * the task deep-copies.
 */
struct CacheProviderSpriteSource
{
    char const* name;           /* registry key, e.g. "sideicons" */
    char const* format;         /* "pix8" | "pix32" */
    char const* data_filename;  /* "sideicons.dat" */
    char const* index_filename; /* "index.dat" */
    int atlas_index;
    int atlas_count; /* <= 0: single frame at atlas_index */
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    char const (*transform)[64]; /* "flip_h"/"flip_v" entries */
    int transform_count;
};

struct CacheProviderVTable
{
    struct ToriRS_Task* (*Task_ModelLoad)(
        struct CacheProvider* provider,
        int model_id);
    struct ToriRS_Task* (*Task_ComponentPackLoad)(
        struct CacheProvider* provider,
        int iface_id);
    struct ToriRS_Task* (*Task_ClientScriptLoad)(
        struct CacheProvider* provider,
        int script_id);
    struct ToriRS_Task* (*Task_ObjLoad)(
        struct CacheProvider* provider,
        int obj_id);
    /** Decode every objtype in the config group into objtype_cache in one pass
     * (the whole group is one archive read). Backs the OC_FIND item-name
     * search. NULL on providers that cannot enumerate the obj group. */
    struct ToriRS_Task* (*Task_ObjLoadAll)(
        struct CacheProvider* provider);
    struct ToriRS_Task* (*Task_NpcLoad)(
        struct CacheProvider* provider,
        int npc_id);
    struct ToriRS_Task* (*Task_SpotanimLoad)(
        struct CacheProvider* provider,
        int spotanim_id);
    /**
     * Make sound effect `sound_id` resident as rendered PCM.
     *
     * dat1 ignores the id and decodes the whole `sounds.dat` bank in one pass —
     * it is a single blob, so there is nothing cheaper to do, and the reference
     * unpacks it at load time for the same reason. dat2 loads just that id's
     * archive. Either way the caller awaits the task and then asks
     * CacheProvider_SoundGet.
     */
    struct ToriRS_Task* (*Task_SoundLoad)(
        struct CacheProvider* provider,
        int sound_id);
    struct ToriRS_Task* (*Task_IdkLoad)(
        struct CacheProvider* provider,
        int idk_id);
    struct ToriRS_Task* (*Task_MapTerrainLoad)(
        struct CacheProvider* provider,
        int map_x,
        int map_z);
    struct ToriRS_Task* (*Task_MapSceneryLoad)(
        struct CacheProvider* provider,
        int map_x,
        int map_z);
    struct ToriRS_Task* (*Task_LocLoad)(
        struct CacheProvider* provider,
        int loc_id);
    struct ToriRS_Task* (*Task_FlotypeLoad)(
        struct CacheProvider* provider,
        int flo_id);
    struct ToriRS_Task* (*Task_UnderlayLoad)(
        struct CacheProvider* provider,
        int underlay_id);
    struct ToriRS_Task* (*Task_TextureLoad)(
        struct CacheProvider* provider,
        int texture_id);
    /** Build the scene animation for a sequence id (frames + framemap/base are
     * cache-format specific). NULL on providers without animation data. */
    struct ToriRS_Task* (*Task_SequenceLoad)(
        struct CacheProvider* provider,
        struct ToriDraw_Scene* scene,
        int seq_id);
    struct ToriRS_Task* (*Task_SpriteLoad)(
        struct CacheProvider* provider,
        int sprite_id);
    /** Resolve + load a sprites-table archive by name (e.g. "scrollbar"). */
    struct ToriRS_Task* (*Task_SpriteLoadByName)(
        struct CacheProvider* provider,
        char const* archive_name);
    /** Dat1: load a named media-jagfile sprite; id assigned + name-mapped. NULL for dat2. */
    struct ToriRS_Task* (*Task_SpriteLoadFromSource)(
        struct CacheProvider* provider,
        struct CacheProviderSpriteSource const* source);
    struct ToriRS_Task* (*Task_FontLoad)(
        struct CacheProvider* provider,
        int font_id);
    /** Dat1: load a title-jagfile font (e.g. "b12") into slot cache_font_id. NULL for dat2. */
    struct ToriRS_Task* (*Task_FontLoadByName)(
        struct CacheProvider* provider,
        char const* font_name,
        int cache_font_id);
    struct ToriRS_Task* (*Task_EnumLoad)(
        struct CacheProvider* provider,
        int enum_id);
    struct ToriRS_Task* (*Task_StructLoad)(
        struct CacheProvider* provider,
        int struct_id);
    struct ToriRS_Task* (*Task_ParamLoad)(
        struct CacheProvider* provider,
        int param_id);
    /** Decode DBROW config (kind 38) for a row id into dbrow_cache. */
    struct ToriRS_Task* (*Task_DbRowLoad)(
        struct CacheProvider* provider,
        int row_id);
    /** Decode a table's DBTABLEINDEX (cache table 21) into dbindex_cache. */
    struct ToriRS_Task* (*Task_DbTableIndexLoad)(
        struct CacheProvider* provider,
        int table_id);
    struct ToriRS_Task* (*Task_ComponentLoad)(
        struct CacheProvider* provider,
        int packed_component_id);
    /** Loads every world map area in one go; NULL on caches without one. */
    struct ToriRS_Task* (*Task_WorldMapLoad)(struct CacheProvider* provider);
    struct ToriRS_Task* (*Task_MapElementLoad)(
        struct CacheProvider* provider,
        int element_id);
    /**
     * Decode one map surface region's geography into worldmap_geography_cache.
     * `sources` are that region's compositemap records (one for a whole region,
     * several for a chunk-assembled one); the task loads every file they name.
     */
    struct ToriRS_Task* (*Task_WorldMapGeographyLoad)(
        struct CacheProvider* provider,
        int key,
        struct ToriRS_WorldMapRegionSource const* sources,
        int source_count);
    /**
     * Is `texture_id` drawable by the SD renderer? rs-map-viewer's TextureLoader.isSd.
     *
     * The RS2 procedural system marks most materials HD-only — 880 of cache.643's 1164 —
     * and the SD client draws faces and overlays that name one as PLAIN COLOUR, not as a
     * missing texture: ModelData.light() nulls the face texture and SceneBuilder falls back
     * to the overlay's own HSL. Skipping the face instead (our missing-texture behaviour)
     * or texturing it anyway (what we did before this existed) are both visibly wrong, the
     * second spectacularly: every pebble/rubble decor in 643 is an HD-only material over a
     * dark recoloured base, so the whole desert rendered as glaring white gravel.
     *
     * NULL slot means every texture is SD (sprite-backed systems, dat1) — the reference's
     * SpriteTextureLoader.isSd is a constant true.
     */
    bool (*TextureIsSd)(
        struct CacheProvider* provider,
        int texture_id);
};

void
CacheProvider_InitEngineCaches(struct CacheProvider* provider);

void
CacheProvider_FreeEngineCaches(struct CacheProvider* provider);

void
CacheProvider_ModelAdd(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model* model);

struct ToriRS_Model*
CacheProvider_ModelGet(
    struct CacheProvider* provider,
    int model_id);

bool
CacheProvider_ModelHas(
    struct CacheProvider* provider,
    int model_id);

void
CacheProvider_ModelsCleanup(struct CacheProvider* provider);

void
CacheProvider_SpriteAdd(
    struct CacheProvider* provider,
    int sprite_id,
    struct ToriRS_Sprite* sprite);

struct ToriRS_Sprite*
CacheProvider_SpriteGet(
    struct CacheProvider* provider,
    int sprite_id);

bool
CacheProvider_SpriteHas(
    struct CacheProvider* provider,
    int sprite_id);

/**
 * Record / look up a sprites-table archive id by cache-native name
 * (e.g. "scrollbar"). Returns -1 if unknown.
 */
void
CacheProvider_SpriteNameMapPut(
    struct CacheProvider* provider,
    char const* archive_name,
    int sprite_id);

int
CacheProvider_SpriteIdByName(
    struct CacheProvider* provider,
    char const* archive_name);

void
CacheProvider_SpritesCleanup(struct CacheProvider* provider);

void
CacheProvider_FontAdd(
    struct CacheProvider* provider,
    int font_id,
    struct ToriRS_Font* font);

struct ToriRS_Font*
CacheProvider_FontGet(
    struct CacheProvider* provider,
    int font_id);

bool
CacheProvider_FontHas(
    struct CacheProvider* provider,
    int font_id);

void
CacheProvider_FontsCleanup(struct CacheProvider* provider);

void
CacheProvider_EnumAdd(
    struct CacheProvider* provider,
    int enum_id,
    struct ToriRS_Enum* e);

struct ToriRS_Enum*
CacheProvider_EnumGet(
    struct CacheProvider* provider,
    int enum_id);

bool
CacheProvider_EnumHas(
    struct CacheProvider* provider,
    int enum_id);

void
CacheProvider_EnumsCleanup(struct CacheProvider* provider);

void
CacheProvider_StructAdd(
    struct CacheProvider* provider,
    int struct_id,
    struct ToriRS_Struct* s);

struct ToriRS_Struct*
CacheProvider_StructGet(
    struct CacheProvider* provider,
    int struct_id);

bool
CacheProvider_StructHas(
    struct CacheProvider* provider,
    int struct_id);

void
CacheProvider_StructsCleanup(struct CacheProvider* provider);

void
CacheProvider_ParamAdd(
    struct CacheProvider* provider,
    int param_id,
    struct ToriRS_ParamType* param);

struct ToriRS_ParamType*
CacheProvider_ParamGet(
    struct CacheProvider* provider,
    int param_id);

bool
CacheProvider_ParamHas(
    struct CacheProvider* provider,
    int param_id);

void
CacheProvider_ParamsCleanup(struct CacheProvider* provider);

void
CacheProvider_DbRowAdd(
    struct CacheProvider* provider,
    int row_id,
    struct RSCache_Dat2ConfigDbRow* row);

struct RSCache_Dat2ConfigDbRow*
CacheProvider_DbRowGet(
    struct CacheProvider* provider,
    int row_id);

bool
CacheProvider_DbRowHas(
    struct CacheProvider* provider,
    int row_id);

void
CacheProvider_DbRowsCleanup(struct CacheProvider* provider);

void
CacheProvider_DbTableIndexAdd(
    struct CacheProvider* provider,
    int table_id,
    struct ToriRS_DbTableIndex* index);

struct ToriRS_DbTableIndex*
CacheProvider_DbTableIndexGet(
    struct CacheProvider* provider,
    int table_id);

bool
CacheProvider_DbTableIndexHas(
    struct CacheProvider* provider,
    int table_id);

void
CacheProvider_DbTableIndexesCleanup(struct CacheProvider* provider);

/**
 * World map areas are one object for the whole cache, so there is no id: the
 * provider owns whatever the load task decoded, and NULL means "not loaded yet
 * or this cache has no world map".
 */
void
CacheProvider_WorldMapSet(
    struct CacheProvider* provider,
    struct ToriRS_WorldMapAreas* areas);

struct ToriRS_WorldMapAreas*
CacheProvider_WorldMapGet(struct CacheProvider* provider);

/** Cache key for one map surface region of one area. */
static inline int
CacheProvider_WorldMapGeographyKey(
    int area_id,
    int region_x,
    int region_y)
{
    return ((area_id & 0xFF) << 24) | ((region_x & 0xFFF) << 12) | (region_y & 0xFFF);
}

void
CacheProvider_WorldMapGeographyAdd(
    struct CacheProvider* provider,
    int key,
    struct RSCache_WorldMapGeography* geography);

struct RSCache_WorldMapGeography*
CacheProvider_WorldMapGeographyGet(
    struct CacheProvider* provider,
    int key);

bool
CacheProvider_WorldMapGeographyHas(
    struct CacheProvider* provider,
    int key);

/** Free the decoded tiles and drop the entry; the renderer calls this once it
 *  has baked the region, so the tile arrays are not held for every region. */
void
CacheProvider_WorldMapGeographyRelease(
    struct CacheProvider* provider,
    int key);

void
CacheProvider_MapElementAdd(
    struct CacheProvider* provider,
    int element_id,
    struct ToriRS_MapElement* element);

struct ToriRS_MapElement*
CacheProvider_MapElementGet(
    struct CacheProvider* provider,
    int element_id);

bool
CacheProvider_MapElementHas(
    struct CacheProvider* provider,
    int element_id);

void
CacheProvider_MapElementsCleanup(struct CacheProvider* provider);

void
CacheProvider_ComponentPackAdd(
    struct CacheProvider* provider,
    int iface_id,
    struct ToriRS_ComponentPack* pack);

struct ToriRS_ComponentPack*
CacheProvider_ComponentPackGet(
    struct CacheProvider* provider,
    int iface_id);

bool
CacheProvider_ComponentPackHas(
    struct CacheProvider* provider,
    int iface_id);

void
CacheProvider_ComponentPacksCleanup(struct CacheProvider* provider);

/** packed_id = (iface_id << 16) | (child & 0xFFFF). Resolves into pack cache. */
struct ToriRS_Component*
CacheProvider_ComponentGet(
    struct CacheProvider* provider,
    int packed_id);

bool
CacheProvider_ComponentHas(
    struct CacheProvider* provider,
    int packed_id);

/** True if the iface pack containing packed_id is loaded. */
bool
CacheProvider_ComponentPackHasForComponent(
    struct CacheProvider* provider,
    int packed_id);

void
CacheProvider_ClientScriptAdd(
    struct CacheProvider* provider,
    int script_id,
    struct CS2VM2_Script* script);

struct CS2VM2_Script*
CacheProvider_ClientScriptGet(
    struct CacheProvider* provider,
    int script_id);

bool
CacheProvider_ClientScriptHas(
    struct CacheProvider* provider,
    int script_id);

void
CacheProvider_ClientScriptsCleanup(struct CacheProvider* provider);

void
CacheProvider_ObjtypeAdd(
    struct CacheProvider* provider,
    int obj_id,
    struct ToriRS_Objtype* objtype);

struct ToriRS_Objtype*
CacheProvider_ObjtypeGet(
    struct CacheProvider* provider,
    int obj_id);

bool
CacheProvider_ObjtypeHas(
    struct CacheProvider* provider,
    int obj_id);

void
CacheProvider_ObjtypesCleanup(struct CacheProvider* provider);

/** Exact-name lookup via objtype_name_cache: returns the obj id whose name
 *  (case-insensitively) equals `name`, or -1 if none is resident. */
int
CacheProvider_ObjtypeIdByName(
    struct CacheProvider* provider,
    char const* name);

/** OC_FIND item-name search: scans every resident objtype and collects the ids
 *  whose name is present, is not "null", and (lowercased) contains
 *  `lower_query` (which the caller must already have lowercased). On a match it
 *  mallocs an ascending-sorted id array into *out_ids (caller frees) and
 *  returns the count; returns 0 (and *out_ids = NULL) when nothing matches. */
int
CacheProvider_ObjtypeSearchByName(
    struct CacheProvider* provider,
    char const* lower_query,
    int** out_ids);

void
CacheProvider_NpctypeAdd(
    struct CacheProvider* provider,
    int npc_id,
    struct ToriRS_Npctype* npctype);

struct ToriRS_Npctype*
CacheProvider_NpctypeGet(
    struct CacheProvider* provider,
    int npc_id);

bool
CacheProvider_NpctypeHas(
    struct CacheProvider* provider,
    int npc_id);

void
CacheProvider_NpctypesCleanup(struct CacheProvider* provider);

void
CacheProvider_SpotanimtypeAdd(
    struct CacheProvider* provider,
    int spotanim_id,
    struct ToriRS_Spotanimtype* spotanimtype);

struct ToriRS_Spotanimtype*
CacheProvider_SpotanimtypeGet(
    struct CacheProvider* provider,
    int spotanim_id);

bool
CacheProvider_SpotanimtypeHas(
    struct CacheProvider* provider,
    int spotanim_id);

void
CacheProvider_SpotanimtypesCleanup(struct CacheProvider* provider);

void
CacheProvider_SoundAdd(
    struct CacheProvider* provider,
    int sound_id,
    struct ToriRS_Sound* sound);

struct ToriRS_Sound*
CacheProvider_SoundGet(
    struct CacheProvider* provider,
    int sound_id);

bool
CacheProvider_SoundHas(
    struct CacheProvider* provider,
    int sound_id);

void
CacheProvider_SoundsCleanup(struct CacheProvider* provider);

void
CacheProvider_IdkAdd(
    struct CacheProvider* provider,
    int idk_id,
    struct ToriRS_Idk* idk);

struct ToriRS_Idk*
CacheProvider_IdkGet(
    struct CacheProvider* provider,
    int idk_id);

bool
CacheProvider_IdkHas(
    struct CacheProvider* provider,
    int idk_id);

void
CacheProvider_IdksCleanup(struct CacheProvider* provider);

void
CacheProvider_MapTerrainAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapTerrain* terrain);

struct ToriRS_MapTerrain*
CacheProvider_MapTerrainGet(
    struct CacheProvider* provider,
    int map_id);

bool
CacheProvider_MapTerrainHas(
    struct CacheProvider* provider,
    int map_id);

void
CacheProvider_MapTerrainsCleanup(struct CacheProvider* provider);

void
CacheProvider_MapSceneryAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapLocs* locs);

struct ToriRS_MapLocs*
CacheProvider_MapSceneryGet(
    struct CacheProvider* provider,
    int map_id);

bool
CacheProvider_MapSceneryHas(
    struct CacheProvider* provider,
    int map_id);

void
CacheProvider_MapSceneryCleanup(struct CacheProvider* provider);

void
CacheProvider_LocationAdd(
    struct CacheProvider* provider,
    int loc_id,
    struct ToriRS_Location* location);

struct ToriRS_Location*
CacheProvider_LocationGet(
    struct CacheProvider* provider,
    int loc_id);

bool
CacheProvider_LocationHas(
    struct CacheProvider* provider,
    int loc_id);

void
CacheProvider_LocationsCleanup(struct CacheProvider* provider);

void
CacheProvider_FlotypeAdd(
    struct CacheProvider* provider,
    int flo_id,
    struct ToriRS_Flotype* flotype);

struct ToriRS_Flotype*
CacheProvider_FlotypeGet(
    struct CacheProvider* provider,
    int flo_id);

bool
CacheProvider_FlotypeHas(
    struct CacheProvider* provider,
    int flo_id);

void
CacheProvider_FlotypesCleanup(struct CacheProvider* provider);

void
CacheProvider_UnderlayAdd(
    struct CacheProvider* provider,
    int underlay_id,
    struct ToriRS_Flotype* underlay);

struct ToriRS_Flotype*
CacheProvider_UnderlayGet(
    struct CacheProvider* provider,
    int underlay_id);

bool
CacheProvider_UnderlayHas(
    struct CacheProvider* provider,
    int underlay_id);

void
CacheProvider_UnderlaysCleanup(struct CacheProvider* provider);

void
CacheProvider_TextureAdd(
    struct CacheProvider* provider,
    int texture_id,
    struct ToriRS_Texture* texture);

struct ToriRS_Texture*
CacheProvider_TextureGet(
    struct CacheProvider* provider,
    int texture_id);

bool
CacheProvider_TextureHas(
    struct CacheProvider* provider,
    int texture_id);

/** TextureIsSd through the vtable; an unset slot answers true. See the slot's comment. */
bool
CacheProvider_TextureIsSd(
    struct CacheProvider* provider,
    int texture_id);

void
CacheProvider_TexturesCleanup(struct CacheProvider* provider);

/*
 * Every CreateTask_* below returns NULL when the provider leaves that vtable
 * slot unset: cache formats differ in what they can serve at all (a dat1 cache
 * has no world map table; a dat2 cache has no name-keyed media sprites), and
 * callers already treat NULL as "nothing to await". Degrading to "asset absent"
 * is the only safe behaviour — calling through the slot would jump to NULL.
 */
static inline struct ToriRS_Task*
CreateTask_ModelLoad(
    struct CacheProvider* provider,
    int model_id)
{
    if( !provider->vtable->Task_ModelLoad )
        return NULL;
    return provider->vtable->Task_ModelLoad(provider, model_id);
}

static inline struct ToriRS_Task*
CreateTask_ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    if( !provider->vtable->Task_ComponentPackLoad )
        return NULL;
    return provider->vtable->Task_ComponentPackLoad(provider, iface_id);
}

static inline struct ToriRS_Task*
CreateTask_ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id)
{
    if( !provider->vtable->Task_ClientScriptLoad )
        return NULL;
    return provider->vtable->Task_ClientScriptLoad(provider, script_id);
}

static inline struct ToriRS_Task*
CreateTask_ObjLoad(
    struct CacheProvider* provider,
    int obj_id)
{
    if( !provider->vtable->Task_ObjLoad )
        return NULL;
    return provider->vtable->Task_ObjLoad(provider, obj_id);
}

static inline struct ToriRS_Task*
CreateTask_ObjLoadAll(struct CacheProvider* provider)
{
    if( !provider->vtable->Task_ObjLoadAll )
        return NULL;
    return provider->vtable->Task_ObjLoadAll(provider);
}

static inline struct ToriRS_Task*
CreateTask_NpcLoad(
    struct CacheProvider* provider,
    int npc_id)
{
    if( !provider->vtable->Task_NpcLoad )
        return NULL;
    return provider->vtable->Task_NpcLoad(provider, npc_id);
}

static inline struct ToriRS_Task*
CreateTask_SpotanimLoad(
    struct CacheProvider* provider,
    int spotanim_id)
{
    if( !provider->vtable->Task_SpotanimLoad )
        return NULL;
    return provider->vtable->Task_SpotanimLoad(provider, spotanim_id);
}

static inline struct ToriRS_Task*
CreateTask_SoundLoad(
    struct CacheProvider* provider,
    int sound_id)
{
    if( !provider->vtable->Task_SoundLoad )
        return NULL;
    return provider->vtable->Task_SoundLoad(provider, sound_id);
}

static inline struct ToriRS_Task*
CreateTask_IdkLoad(
    struct CacheProvider* provider,
    int idk_id)
{
    if( !provider->vtable->Task_IdkLoad )
        return NULL;
    return provider->vtable->Task_IdkLoad(provider, idk_id);
}

static inline struct ToriRS_Task*
CreateTask_MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    if( !provider->vtable->Task_MapTerrainLoad )
        return NULL;
    return provider->vtable->Task_MapTerrainLoad(provider, map_x, map_z);
}

static inline struct ToriRS_Task*
CreateTask_MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    if( !provider->vtable->Task_MapSceneryLoad )
        return NULL;
    return provider->vtable->Task_MapSceneryLoad(provider, map_x, map_z);
}

static inline struct ToriRS_Task*
CreateTask_LocLoad(
    struct CacheProvider* provider,
    int loc_id)
{
    if( !provider->vtable->Task_LocLoad )
        return NULL;
    return provider->vtable->Task_LocLoad(provider, loc_id);
}

static inline struct ToriRS_Task*
CreateTask_FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id)
{
    if( !provider->vtable->Task_FlotypeLoad )
        return NULL;
    return provider->vtable->Task_FlotypeLoad(provider, flo_id);
}

static inline struct ToriRS_Task*
CreateTask_UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id)
{
    if( !provider->vtable->Task_UnderlayLoad )
        return NULL;
    return provider->vtable->Task_UnderlayLoad(provider, underlay_id);
}

static inline struct ToriRS_Task*
CreateTask_TextureLoad(
    struct CacheProvider* provider,
    int texture_id)
{
    if( !provider->vtable->Task_TextureLoad )
        return NULL;
    return provider->vtable->Task_TextureLoad(provider, texture_id);
}

/** Decode sequence `seq_id` into scene animation `seq_id` (no-op when already
 * registered). The frame/framemap layout is cache-format specific, so this goes
 * through the provider rather than a dat2 entry point. */
static inline struct ToriRS_Task*
CreateTask_SequenceLoad(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id)
{
    if( !provider->vtable->Task_SequenceLoad )
        return NULL;
    return provider->vtable->Task_SequenceLoad(provider, scene, seq_id);
}

static inline struct ToriRS_Task*
CreateTask_SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id)
{
    if( !provider->vtable->Task_SpriteLoad )
        return NULL;
    return provider->vtable->Task_SpriteLoad(provider, sprite_id);
}

static inline struct ToriRS_Task*
CreateTask_SpriteLoadByName(
    struct CacheProvider* provider,
    char const* archive_name)
{
    if( !provider || !provider->vtable || !provider->vtable->Task_SpriteLoadByName )
        return NULL;
    return provider->vtable->Task_SpriteLoadByName(provider, archive_name);
}

static inline struct ToriRS_Task*
CreateTask_SpriteLoadFromSource(
    struct CacheProvider* provider,
    struct CacheProviderSpriteSource const* source)
{
    if( !provider || !provider->vtable || !provider->vtable->Task_SpriteLoadFromSource )
        return NULL;
    return provider->vtable->Task_SpriteLoadFromSource(provider, source);
}

static inline struct ToriRS_Task*
CreateTask_FontLoad(
    struct CacheProvider* provider,
    int font_id)
{
    if( !provider->vtable->Task_FontLoad )
        return NULL;
    return provider->vtable->Task_FontLoad(provider, font_id);
}

static inline struct ToriRS_Task*
CreateTask_FontLoadByName(
    struct CacheProvider* provider,
    char const* font_name,
    int cache_font_id)
{
    if( !provider || !provider->vtable || !provider->vtable->Task_FontLoadByName )
        return NULL;
    return provider->vtable->Task_FontLoadByName(provider, font_name, cache_font_id);
}

static inline struct ToriRS_Task*
CreateTask_EnumLoad(
    struct CacheProvider* provider,
    int enum_id)
{
    if( !provider->vtable->Task_EnumLoad )
        return NULL;
    return provider->vtable->Task_EnumLoad(provider, enum_id);
}

static inline struct ToriRS_Task*
CreateTask_StructLoad(
    struct CacheProvider* provider,
    int struct_id)
{
    if( !provider->vtable->Task_StructLoad )
        return NULL;
    return provider->vtable->Task_StructLoad(provider, struct_id);
}

static inline struct ToriRS_Task*
CreateTask_ParamLoad(
    struct CacheProvider* provider,
    int param_id)
{
    if( !provider->vtable->Task_ParamLoad )
        return NULL;
    return provider->vtable->Task_ParamLoad(provider, param_id);
}

static inline struct ToriRS_Task*
CreateTask_DbRowLoad(
    struct CacheProvider* provider,
    int row_id)
{
    if( !provider->vtable->Task_DbRowLoad )
        return NULL;
    return provider->vtable->Task_DbRowLoad(provider, row_id);
}

static inline struct ToriRS_Task*
CreateTask_DbTableIndexLoad(
    struct CacheProvider* provider,
    int table_id)
{
    if( !provider->vtable->Task_DbTableIndexLoad )
        return NULL;
    return provider->vtable->Task_DbTableIndexLoad(provider, table_id);
}

static inline struct ToriRS_Task*
CreateTask_ComponentLoad(
    struct CacheProvider* provider,
    int packed_component_id)
{
    if( !provider->vtable->Task_ComponentLoad )
        return NULL;
    return provider->vtable->Task_ComponentLoad(provider, packed_component_id);
}

static inline struct ToriRS_Task*
CreateTask_WorldMapLoad(struct CacheProvider* provider)
{
    if( !provider->vtable->Task_WorldMapLoad )
        return NULL;
    return provider->vtable->Task_WorldMapLoad(provider);
}

static inline struct ToriRS_Task*
CreateTask_MapElementLoad(
    struct CacheProvider* provider,
    int element_id)
{
    if( !provider->vtable->Task_MapElementLoad )
        return NULL;
    return provider->vtable->Task_MapElementLoad(provider, element_id);
}

static inline struct ToriRS_Task*
CreateTask_WorldMapGeographyLoad(
    struct CacheProvider* provider,
    int key,
    struct ToriRS_WorldMapRegionSource const* sources,
    int source_count)
{
    if( !provider->vtable->Task_WorldMapGeographyLoad )
        return NULL;
    return provider->vtable->Task_WorldMapGeographyLoad(provider, key, sources, source_count);
}

#endif
