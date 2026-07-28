#include "engine/cache_provider.h"

#include "cs2vm2/cs2vm2_script.h"
#include "engine/torirs_db.h"
#include "engine/torirs_worldmap_from_rscache.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PROVIDER_MODEL_CAPACITY 8192
#define CACHE_PROVIDER_SPRITE_CAPACITY 4096
#define CACHE_PROVIDER_FONT_CAPACITY 256
#define CACHE_PROVIDER_ENUM_CAPACITY 2048
#define CACHE_PROVIDER_STRUCT_CAPACITY 2048
#define CACHE_PROVIDER_PARAM_CAPACITY 2048
#define CACHE_PROVIDER_COMPONENTPACK_CAPACITY 512
#define CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY 4096
#define CACHE_PROVIDER_OBJTYPE_CAPACITY 4096
#define CACHE_PROVIDER_NPCTYPE_CAPACITY 4096
#define CACHE_PROVIDER_SPOTANIMTYPE_CAPACITY 1024
#define CACHE_PROVIDER_SOUND_CAPACITY 1024
#define CACHE_PROVIDER_IDK_CAPACITY 512
#define CACHE_PROVIDER_MAP_TERRAIN_CAPACITY 512
#define CACHE_PROVIDER_MAP_SCENERY_CAPACITY 512
#define CACHE_PROVIDER_LOCATION_CAPACITY 4096
#define CACHE_PROVIDER_FLOTYPE_CAPACITY 512
#define CACHE_PROVIDER_UNDERLAY_CAPACITY 512
#define CACHE_PROVIDER_TEXTURE_CAPACITY 512
#define CACHE_PROVIDER_SPRITE_NAME_CAPACITY 256
#define CACHE_PROVIDER_OBJTYPE_NAME_CAPACITY 4096
#define CACHE_PROVIDER_MAPELEMENT_CAPACITY 1024
#define CACHE_PROVIDER_DBROW_CAPACITY 2048
/* The renderer releases each region once baked, so only the in-flight ones sit
 * here — a map surface never has hundreds resident. */
#define CACHE_PROVIDER_WORLDMAP_GEOGRAPHY_CAPACITY 64
#define CACHE_PROVIDER_DBINDEX_CAPACITY 256

/*
 * `last_used` is the LRU clock for the two caches that grow without bound over
 * a session: models and sprites. Both are *derived* — every reader converts
 * what it gets (ToriDraw_ModelFromToriRS and friends copy) and none of them
 * retain the pointer — so dropping an entry costs a reload, never a dangling
 * reference.
 *
 * Eviction is not triggered by the insert, though. A world build preloads its
 * whole model set and then consumes it synchronously, so a capacity trigger
 * during the preload would evict the models the build is about to read and the
 * scenery would silently go missing. Trimming therefore happens at one explicit
 * seam — the start of the next build, see CacheProvider_TrimDerivedCaches —
 * which is the same place Client-TS clears its LruCaches (Client.mapBuild).
 */
struct MapEntry_ProviderModel
{
    int id;
    struct ToriRS_Model* model;
    uint64_t last_used;
};

struct MapEntry_ProviderSprite
{
    int id;
    struct ToriRS_Sprite* sprite;
    uint64_t last_used;
};

struct MapEntry_ProviderSpriteName
{
    int name_hash;
    int sprite_id;
};

struct MapEntry_ProviderFont
{
    int id;
    struct ToriRS_Font* font;
};

struct MapEntry_ProviderEnum
{
    int id;
    struct ToriRS_Enum* e;
};

struct MapEntry_ProviderStruct
{
    int id;
    struct ToriRS_Struct* s;
};

struct MapEntry_ProviderParamType
{
    int id;
    struct ToriRS_ParamType* param;
};

struct MapEntry_ProviderSound
{
    int id;
    struct ToriRS_Sound* sound;
};

struct MapEntry_ProviderMapElement
{
    int id;
    struct ToriRS_MapElement* element;
};

struct MapEntry_ProviderWorldMapGeography
{
    int id;
    struct RSCache_WorldMapGeography* geography;
};

struct MapEntry_ProviderComponentPack
{
    int id;
    struct ToriRS_ComponentPack* pack;
};

struct MapEntry_ProviderClientScript
{
    int id;
    struct CS2VM2_Script* script;
};

struct MapEntry_ProviderObjtype
{
    int id;
    struct ToriRS_Objtype* objtype;
};

struct MapEntry_ProviderObjtypeName
{
    int name_hash;
    int obj_id;
};

struct MapEntry_ProviderNpctype
{
    int id;
    struct ToriRS_Npctype* npctype;
};

struct MapEntry_ProviderSpotanimtype
{
    int id;
    struct ToriRS_Spotanimtype* spotanimtype;
};

struct MapEntry_ProviderIdk
{
    int id;
    struct ToriRS_Idk* idk;
};

struct MapEntry_ProviderMapTerrain
{
    int id;
    struct ToriRS_MapTerrain* terrain;
};

struct MapEntry_ProviderMapScenery
{
    int id;
    struct ToriRS_MapLocs* locs;
};

struct MapEntry_ProviderLocation
{
    int id;
    struct ToriRS_Location* location;
};

struct MapEntry_ProviderFlotype
{
    int id;
    struct ToriRS_Flotype* flotype;
};

struct MapEntry_ProviderTexture
{
    int id;
    struct ToriRS_Texture* texture;
};

struct MapEntry_ProviderDbRow
{
    int id;
    struct RSCache_Dat2ConfigDbRow* row;
};

struct MapEntry_ProviderDbIndex
{
    int id;
    struct ToriRS_DbTableIndex* index;
};

static size_t
cache_provider_hmap_buffer_bytes(
    size_t entry_size,
    size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static struct HMap*
cache_provider_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = cache_provider_hmap_buffer_bytes(entry_size, capacity);
    void* buffer = malloc(buffer_size);
    assert(buffer);

    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = entry_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .capacity = capacity,
    };
    struct HMap* map = hmap_new(&config, 0);
    assert(map);
    return map;
}

static void
cache_provider_hmap_free(struct HMap* map)
{
    if( !map )
        return;
    free(hmap_free(map));
}

static void
cache_provider_hmap_maybe_grow(struct HMap** map_out)
{
    struct HMap* map;
    size_t new_capacity;
    size_t buffer_size;
    void* new_buffer;
    void* old_buffer;

    assert(map_out);
    map = *map_out;
    assert(map);

    if( map->size * 4 <= map->capacity * 3 )
        return;

    new_capacity = map->capacity * 2;
    buffer_size = cache_provider_hmap_buffer_bytes(map->entry_size, new_capacity);
    new_buffer = malloc(buffer_size);
    assert(new_buffer);

    if( hmap_resize(*map_out, new_buffer, buffer_size, new_capacity, &old_buffer) != HMAP_OK )
    {
        free(new_buffer);
        return;
    }

    free(old_buffer);
}

static void
cache_provider_hmap_prepare_insert(struct HMap** map_out)
{
    assert(map_out);
    assert(*map_out);
    if( (*map_out)->capacity > 0 && (*map_out)->size * 4 >= (*map_out)->capacity * 3 )
        cache_provider_hmap_maybe_grow(map_out);
}

void
CacheProvider_SetProfile(
    struct CacheProvider* provider,
    const struct RSCache* profile)
{
    assert(provider);
    assert(profile);
    assert(RSCache_ProfileIsIdentified(profile));
    provider->profile = *profile;
}

void
CacheProvider_InitEngineCaches(struct CacheProvider* provider)
{
    assert(provider);

    /* Unset until the boot path calls CacheProvider_SetProfile. Decoding through
     * CacheProvider_Profile before that asserts. */
    provider->profile = RSCache_ProfileZero();

    provider->model_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderModel), CACHE_PROVIDER_MODEL_CAPACITY);
    provider->sprite_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSprite), CACHE_PROVIDER_SPRITE_CAPACITY);
    provider->font_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFont), CACHE_PROVIDER_FONT_CAPACITY);
    provider->enum_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderEnum), CACHE_PROVIDER_ENUM_CAPACITY);
    provider->struct_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderStruct), CACHE_PROVIDER_STRUCT_CAPACITY);
    provider->param_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderParamType), CACHE_PROVIDER_PARAM_CAPACITY);
    provider->componentpack_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderComponentPack), CACHE_PROVIDER_COMPONENTPACK_CAPACITY);
    provider->clientscript_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderClientScript), CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY);
    provider->objtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderObjtype), CACHE_PROVIDER_OBJTYPE_CAPACITY);
    provider->npctype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderNpctype), CACHE_PROVIDER_NPCTYPE_CAPACITY);
    provider->spotanimtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSpotanimtype), CACHE_PROVIDER_SPOTANIMTYPE_CAPACITY);
    provider->sound_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSound), CACHE_PROVIDER_SOUND_CAPACITY);
    provider->idk_cache =
        cache_provider_hmap_new(sizeof(struct MapEntry_ProviderIdk), CACHE_PROVIDER_IDK_CAPACITY);
    provider->map_terrain_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapTerrain), CACHE_PROVIDER_MAP_TERRAIN_CAPACITY);
    provider->map_scenery_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapScenery), CACHE_PROVIDER_MAP_SCENERY_CAPACITY);
    provider->location_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderLocation), CACHE_PROVIDER_LOCATION_CAPACITY);
    provider->flotype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_FLOTYPE_CAPACITY);
    provider->underlay_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_UNDERLAY_CAPACITY);
    provider->texture_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderTexture), CACHE_PROVIDER_TEXTURE_CAPACITY);
    provider->sprite_name_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSpriteName), CACHE_PROVIDER_SPRITE_NAME_CAPACITY);
    provider->objtype_name_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderObjtypeName), CACHE_PROVIDER_OBJTYPE_NAME_CAPACITY);
    provider->objtypes_all_loaded = false;
    provider->mapelement_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapElement), CACHE_PROVIDER_MAPELEMENT_CAPACITY);
    provider->dbrow_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderDbRow), CACHE_PROVIDER_DBROW_CAPACITY);
    provider->dbindex_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderDbIndex), CACHE_PROVIDER_DBINDEX_CAPACITY);
    provider->worldmap_geography_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderWorldMapGeography),
        CACHE_PROVIDER_WORLDMAP_GEOGRAPHY_CAPACITY);
}

void
CacheProvider_FreeEngineCaches(struct CacheProvider* provider)
{
    assert(provider);

    CacheProvider_ModelsCleanup(provider);
    CacheProvider_SpritesCleanup(provider);
    CacheProvider_FontsCleanup(provider);
    CacheProvider_EnumsCleanup(provider);
    CacheProvider_StructsCleanup(provider);
    CacheProvider_ParamsCleanup(provider);
    CacheProvider_ComponentPacksCleanup(provider);
    CacheProvider_ClientScriptsCleanup(provider);
    CacheProvider_ObjtypesCleanup(provider);
    CacheProvider_NpctypesCleanup(provider);
    CacheProvider_SpotanimtypesCleanup(provider);
    CacheProvider_SoundsCleanup(provider);
    CacheProvider_IdksCleanup(provider);
    CacheProvider_MapTerrainsCleanup(provider);
    CacheProvider_MapSceneryCleanup(provider);
    CacheProvider_LocationsCleanup(provider);
    CacheProvider_FlotypesCleanup(provider);
    CacheProvider_UnderlaysCleanup(provider);
    CacheProvider_TexturesCleanup(provider);
    CacheProvider_MapElementsCleanup(provider);
    CacheProvider_DbRowsCleanup(provider);
    CacheProvider_DbTableIndexesCleanup(provider);

    ToriRS_WorldMapAreasFree(provider->worldmap_areas);
    provider->worldmap_areas = NULL;

    cache_provider_hmap_free(provider->model_cache);
    provider->model_cache = NULL;
    cache_provider_hmap_free(provider->sprite_cache);
    provider->sprite_cache = NULL;
    cache_provider_hmap_free(provider->font_cache);
    provider->font_cache = NULL;
    cache_provider_hmap_free(provider->enum_cache);
    provider->enum_cache = NULL;
    cache_provider_hmap_free(provider->struct_cache);
    provider->struct_cache = NULL;
    cache_provider_hmap_free(provider->param_cache);
    provider->param_cache = NULL;
    cache_provider_hmap_free(provider->componentpack_cache);
    provider->componentpack_cache = NULL;
    cache_provider_hmap_free(provider->clientscript_cache);
    provider->clientscript_cache = NULL;
    cache_provider_hmap_free(provider->objtype_cache);
    provider->objtype_cache = NULL;
    cache_provider_hmap_free(provider->npctype_cache);
    provider->npctype_cache = NULL;
    cache_provider_hmap_free(provider->spotanimtype_cache);
    provider->spotanimtype_cache = NULL;
    cache_provider_hmap_free(provider->sound_cache);
    provider->sound_cache = NULL;
    cache_provider_hmap_free(provider->idk_cache);
    provider->idk_cache = NULL;
    cache_provider_hmap_free(provider->map_terrain_cache);
    provider->map_terrain_cache = NULL;
    cache_provider_hmap_free(provider->map_scenery_cache);
    provider->map_scenery_cache = NULL;
    cache_provider_hmap_free(provider->location_cache);
    provider->location_cache = NULL;
    cache_provider_hmap_free(provider->flotype_cache);
    provider->flotype_cache = NULL;
    cache_provider_hmap_free(provider->underlay_cache);
    provider->underlay_cache = NULL;
    cache_provider_hmap_free(provider->texture_cache);
    provider->texture_cache = NULL;
    cache_provider_hmap_free(provider->sprite_name_cache);
    provider->sprite_name_cache = NULL;
    cache_provider_hmap_free(provider->objtype_name_cache);
    provider->objtype_name_cache = NULL;
    cache_provider_hmap_free(provider->mapelement_cache);
    provider->mapelement_cache = NULL;
    cache_provider_hmap_free(provider->dbrow_cache);
    provider->dbrow_cache = NULL;
    cache_provider_hmap_free(provider->dbindex_cache);
    provider->dbindex_cache = NULL;
}

void
CacheProvider_ModelAdd(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model* model)
{
    struct MapEntry_ProviderModel* entry;

    assert(provider);
    assert(model);

    cache_provider_hmap_prepare_insert(&provider->model_cache);
    entry = (struct MapEntry_ProviderModel*)hmap_search(
        provider->model_cache, &model_id, HMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");

    entry->id = model_id;
    entry->model = model;
    entry->last_used = ++provider->derived_clock;
}

struct ToriRS_Model*
CacheProvider_ModelGet(
    struct CacheProvider* provider,
    int model_id)
{
    struct MapEntry_ProviderModel* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderModel*)hmap_search(
        provider->model_cache, &model_id, HMAP_FIND);
    if( !entry )
        return NULL;
    entry->last_used = ++provider->derived_clock;
    return entry->model;
}

bool
CacheProvider_ModelHas(
    struct CacheProvider* provider,
    int model_id)
{
    return CacheProvider_ModelGet(provider, model_id) != NULL;
}

/*
 * Evict the least recently used entries of one cache until `keep` remain.
 *
 * hmap has no ordered iteration, so this is a select-the-k-oldest pass: walk
 * once to find the last_used threshold that leaves `keep` entries above it,
 * then walk again removing everything at or below it. Both walks are over a
 * cache measured in thousands of entries and run once per world build, so the
 * two passes are cheaper than maintaining an intrusive list on every get.
 */
static uint64_t
cache_provider_lru_threshold(
    struct HMap* map,
    size_t entry_size,
    size_t last_used_offset,
    size_t keep)
{
    struct HMapIter* iter;
    void* raw;
    uint64_t* stamps;
    size_t count = 0;
    uint64_t threshold;

    (void)entry_size;
    if( map->size <= keep )
        return 0;

    stamps = malloc((size_t)map->size * sizeof(*stamps));
    if( !stamps )
        return 0;

    iter = hmap_iter_new(map);
    while( (raw = hmap_iter_next(iter)) && count < (size_t)map->size )
        stamps[count++] = *(uint64_t*)((char*)raw + last_used_offset);
    hmap_iter_free(iter);

    if( count <= keep )
    {
        free(stamps);
        return 0;
    }

    /* Partial selection: the (count - keep)th smallest stamp is the cutoff.
     * count is in the thousands, so an insertion-free O(n * k) scan would be
     * worse than just sorting; qsort is fine and this runs once per build. */
    {
        size_t i;
        for( i = 1; i < count; i++ )
        {
            uint64_t key = stamps[i];
            size_t j = i;
            while( j > 0 && stamps[j - 1] > key )
            {
                stamps[j] = stamps[j - 1];
                j--;
            }
            stamps[j] = key;
        }
    }
    threshold = stamps[count - keep - 1];
    free(stamps);
    return threshold;
}

static void
cache_provider_trim_models(struct CacheProvider* provider, size_t keep)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderModel* entry;
    uint64_t threshold;
    int* doomed;
    int doomed_count = 0;

    if( !provider->model_cache || (size_t)provider->model_cache->size <= keep )
        return;

    threshold = cache_provider_lru_threshold(
        provider->model_cache,
        sizeof(*entry),
        offsetof(struct MapEntry_ProviderModel, last_used),
        keep);
    if( threshold == 0 )
        return;

    /* Removing during iteration would disturb the probe sequence, so collect
     * the keys first and delete afterwards. */
    doomed = malloc((size_t)provider->model_cache->size * sizeof(*doomed));
    if( !doomed )
        return;

    iter = hmap_iter_new(provider->model_cache);
    while( (entry = (struct MapEntry_ProviderModel*)hmap_iter_next(iter)) )
    {
        if( entry->last_used <= threshold )
            doomed[doomed_count++] = entry->id;
    }
    hmap_iter_free(iter);

    for( int i = 0; i < doomed_count; i++ )
    {
        entry = (struct MapEntry_ProviderModel*)hmap_search(
            provider->model_cache, &doomed[i], HMAP_REMOVE);
        if( entry && entry->model )
            ToriRS_ModelFree(entry->model);
    }
    free(doomed);
}

static void
cache_provider_trim_sprites(struct CacheProvider* provider, size_t keep)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderSprite* entry;
    uint64_t threshold;
    int* doomed;
    int doomed_count = 0;

    if( !provider->sprite_cache || (size_t)provider->sprite_cache->size <= keep )
        return;

    threshold = cache_provider_lru_threshold(
        provider->sprite_cache,
        sizeof(*entry),
        offsetof(struct MapEntry_ProviderSprite, last_used),
        keep);
    if( threshold == 0 )
        return;

    doomed = malloc((size_t)provider->sprite_cache->size * sizeof(*doomed));
    if( !doomed )
        return;

    iter = hmap_iter_new(provider->sprite_cache);
    while( (entry = (struct MapEntry_ProviderSprite*)hmap_iter_next(iter)) )
    {
        if( entry->last_used <= threshold )
            doomed[doomed_count++] = entry->id;
    }
    hmap_iter_free(iter);

    for( int i = 0; i < doomed_count; i++ )
    {
        entry = (struct MapEntry_ProviderSprite*)hmap_search(
            provider->sprite_cache, &doomed[i], HMAP_REMOVE);
        if( entry && entry->sprite )
            ToriRS_SpriteFree(entry->sprite);
    }
    free(doomed);
}

void
CacheProvider_TrimDerivedCaches(struct CacheProvider* provider)
{
    assert(provider);
    cache_provider_trim_models(provider, CACHE_PROVIDER_MODEL_KEEP);
    cache_provider_trim_sprites(provider, CACHE_PROVIDER_SPRITE_KEEP);
}

void
CacheProvider_ModelsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderModel* entry;

    assert(provider);
    if( !provider->model_cache )
        return;

    iter = hmap_iter_new(provider->model_cache);
    while( (entry = (struct MapEntry_ProviderModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            ToriRS_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->model_cache);
    provider->model_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderModel), CACHE_PROVIDER_MODEL_CAPACITY);
}

void
CacheProvider_SpriteAdd(
    struct CacheProvider* provider,
    int sprite_id,
    struct ToriRS_Sprite* sprite)
{
    struct MapEntry_ProviderSprite* entry;

    assert(provider);
    assert(sprite);

    cache_provider_hmap_prepare_insert(&provider->sprite_cache);
    entry = (struct MapEntry_ProviderSprite*)hmap_search(
        provider->sprite_cache, &sprite_id, HMAP_INSERT);
    assert(entry && "Sprite must be inserted into hmap");

    entry->id = sprite_id;
    entry->sprite = sprite;
    entry->last_used = ++provider->derived_clock;
}

struct ToriRS_Sprite*
CacheProvider_SpriteGet(
    struct CacheProvider* provider,
    int sprite_id)
{
    struct MapEntry_ProviderSprite* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderSprite*)hmap_search(
        provider->sprite_cache, &sprite_id, HMAP_FIND);
    if( !entry )
        return NULL;
    entry->last_used = ++provider->derived_clock;
    return entry->sprite;
}

bool
CacheProvider_SpriteHas(
    struct CacheProvider* provider,
    int sprite_id)
{
    return CacheProvider_SpriteGet(provider, sprite_id) != NULL;
}

static int
sprite_name_hash(char const* archive_name)
{
    char buf[64];
    size_t n;

    assert(archive_name);
    n = strlen(archive_name);
    if( n >= sizeof(buf) )
        n = sizeof(buf) - 1;
    memcpy(buf, archive_name, n);
    buf[n] = '\0';
    return RSCache_ArchiveNameHashDat2(buf);
}

void
CacheProvider_SpriteNameMapPut(
    struct CacheProvider* provider,
    char const* archive_name,
    int sprite_id)
{
    struct MapEntry_ProviderSpriteName* entry;
    int name_hash;

    assert(provider);
    assert(archive_name);
    assert(provider->sprite_name_cache);
    if( sprite_id < 0 )
        return;

    name_hash = sprite_name_hash(archive_name);
    cache_provider_hmap_prepare_insert(&provider->sprite_name_cache);
    entry = (struct MapEntry_ProviderSpriteName*)hmap_search(
        provider->sprite_name_cache, &name_hash, HMAP_INSERT);
    assert(entry && "Sprite name must be inserted into hmap");
    entry->name_hash = name_hash;
    entry->sprite_id = sprite_id;
}

int
CacheProvider_SpriteIdByName(
    struct CacheProvider* provider,
    char const* archive_name)
{
    struct MapEntry_ProviderSpriteName* entry;
    int name_hash;

    assert(provider);
    if( !archive_name || !provider->sprite_name_cache )
        return -1;
    name_hash = sprite_name_hash(archive_name);
    entry = (struct MapEntry_ProviderSpriteName*)hmap_search(
        provider->sprite_name_cache, &name_hash, HMAP_FIND);
    if( !entry )
        return -1;
    return entry->sprite_id;
}

void
CacheProvider_SpritesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderSprite* entry;

    assert(provider);
    if( !provider->sprite_cache )
        return;

    iter = hmap_iter_new(provider->sprite_cache);
    while( (entry = (struct MapEntry_ProviderSprite*)hmap_iter_next(iter)) )
    {
        if( entry->sprite )
            ToriRS_SpriteFree(entry->sprite);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->sprite_cache);
    provider->sprite_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSprite), CACHE_PROVIDER_SPRITE_CAPACITY);
}

void
CacheProvider_FontAdd(
    struct CacheProvider* provider,
    int font_id,
    struct ToriRS_Font* font)
{
    struct MapEntry_ProviderFont* entry;

    assert(provider);
    assert(font);

    cache_provider_hmap_prepare_insert(&provider->font_cache);
    entry =
        (struct MapEntry_ProviderFont*)hmap_search(provider->font_cache, &font_id, HMAP_INSERT);
    assert(entry && "Font must be inserted into hmap");

    entry->id = font_id;
    entry->font = font;
}

struct ToriRS_Font*
CacheProvider_FontGet(
    struct CacheProvider* provider,
    int font_id)
{
    struct MapEntry_ProviderFont* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFont*)hmap_search(provider->font_cache, &font_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->font;
}

bool
CacheProvider_FontHas(
    struct CacheProvider* provider,
    int font_id)
{
    return CacheProvider_FontGet(provider, font_id) != NULL;
}

void
CacheProvider_FontsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFont* entry;

    assert(provider);
    if( !provider->font_cache )
        return;

    iter = hmap_iter_new(provider->font_cache);
    while( (entry = (struct MapEntry_ProviderFont*)hmap_iter_next(iter)) )
    {
        if( entry->font )
            ToriRS_FontFree(entry->font);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->font_cache);
    provider->font_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFont), CACHE_PROVIDER_FONT_CAPACITY);
}

void
CacheProvider_EnumAdd(
    struct CacheProvider* provider,
    int enum_id,
    struct ToriRS_Enum* e)
{
    struct MapEntry_ProviderEnum* entry;

    assert(provider);
    assert(e);

    cache_provider_hmap_prepare_insert(&provider->enum_cache);
    entry =
        (struct MapEntry_ProviderEnum*)hmap_search(provider->enum_cache, &enum_id, HMAP_INSERT);
    assert(entry && "Enum must be inserted into hmap");

    entry->id = enum_id;
    entry->e = e;
}

struct ToriRS_Enum*
CacheProvider_EnumGet(
    struct CacheProvider* provider,
    int enum_id)
{
    struct MapEntry_ProviderEnum* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderEnum*)hmap_search(provider->enum_cache, &enum_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->e;
}

bool
CacheProvider_EnumHas(
    struct CacheProvider* provider,
    int enum_id)
{
    return CacheProvider_EnumGet(provider, enum_id) != NULL;
}

void
CacheProvider_EnumsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderEnum* entry;

    assert(provider);
    if( !provider->enum_cache )
        return;

    iter = hmap_iter_new(provider->enum_cache);
    while( (entry = (struct MapEntry_ProviderEnum*)hmap_iter_next(iter)) )
    {
        if( entry->e )
            ToriRS_EnumFree(entry->e);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->enum_cache);
    provider->enum_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderEnum), CACHE_PROVIDER_ENUM_CAPACITY);
}

void
CacheProvider_StructAdd(
    struct CacheProvider* provider,
    int struct_id,
    struct ToriRS_Struct* s)
{
    struct MapEntry_ProviderStruct* entry;

    assert(provider);
    assert(s);

    cache_provider_hmap_prepare_insert(&provider->struct_cache);
    entry = (struct MapEntry_ProviderStruct*)hmap_search(
        provider->struct_cache, &struct_id, HMAP_INSERT);
    assert(entry && "Struct must be inserted into hmap");

    entry->id = struct_id;
    entry->s = s;
}

struct ToriRS_Struct*
CacheProvider_StructGet(
    struct CacheProvider* provider,
    int struct_id)
{
    struct MapEntry_ProviderStruct* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderStruct*)hmap_search(
        provider->struct_cache, &struct_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->s;
}

bool
CacheProvider_StructHas(
    struct CacheProvider* provider,
    int struct_id)
{
    return CacheProvider_StructGet(provider, struct_id) != NULL;
}

void
CacheProvider_StructsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderStruct* entry;

    assert(provider);
    if( !provider->struct_cache )
        return;

    iter = hmap_iter_new(provider->struct_cache);
    while( (entry = (struct MapEntry_ProviderStruct*)hmap_iter_next(iter)) )
    {
        if( entry->s )
            ToriRS_StructFree(entry->s);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->struct_cache);
    provider->struct_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderStruct), CACHE_PROVIDER_STRUCT_CAPACITY);
}

void
CacheProvider_ParamAdd(
    struct CacheProvider* provider,
    int param_id,
    struct ToriRS_ParamType* param)
{
    struct MapEntry_ProviderParamType* entry;

    assert(provider);
    assert(param);

    cache_provider_hmap_prepare_insert(&provider->param_cache);
    entry = (struct MapEntry_ProviderParamType*)hmap_search(
        provider->param_cache, &param_id, HMAP_INSERT);
    assert(entry && "Param must be inserted into hmap");

    entry->id = param_id;
    entry->param = param;
}

struct ToriRS_ParamType*
CacheProvider_ParamGet(
    struct CacheProvider* provider,
    int param_id)
{
    struct MapEntry_ProviderParamType* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderParamType*)hmap_search(
        provider->param_cache, &param_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->param;
}

bool
CacheProvider_ParamHas(
    struct CacheProvider* provider,
    int param_id)
{
    return CacheProvider_ParamGet(provider, param_id) != NULL;
}

void
CacheProvider_ParamsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderParamType* entry;

    assert(provider);
    if( !provider->param_cache )
        return;

    iter = hmap_iter_new(provider->param_cache);
    while( (entry = (struct MapEntry_ProviderParamType*)hmap_iter_next(iter)) )
    {
        if( entry->param )
            ToriRS_ParamTypeFree(entry->param);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->param_cache);
    provider->param_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderParamType), CACHE_PROVIDER_PARAM_CAPACITY);
}

void
CacheProvider_DbRowAdd(
    struct CacheProvider* provider,
    int row_id,
    struct RSCache_Dat2ConfigDbRow* row)
{
    struct MapEntry_ProviderDbRow* entry;

    assert(provider);
    assert(row);

    cache_provider_hmap_prepare_insert(&provider->dbrow_cache);
    entry =
        (struct MapEntry_ProviderDbRow*)hmap_search(provider->dbrow_cache, &row_id, HMAP_INSERT);
    assert(entry && "DbRow must be inserted into hmap");

    entry->id = row_id;
    entry->row = row;
}

struct RSCache_Dat2ConfigDbRow*
CacheProvider_DbRowGet(
    struct CacheProvider* provider,
    int row_id)
{
    struct MapEntry_ProviderDbRow* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderDbRow*)hmap_search(provider->dbrow_cache, &row_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->row;
}

bool
CacheProvider_DbRowHas(
    struct CacheProvider* provider,
    int row_id)
{
    return CacheProvider_DbRowGet(provider, row_id) != NULL;
}

void
CacheProvider_DbRowsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderDbRow* entry;

    assert(provider);
    if( !provider->dbrow_cache )
        return;

    iter = hmap_iter_new(provider->dbrow_cache);
    while( (entry = (struct MapEntry_ProviderDbRow*)hmap_iter_next(iter)) )
    {
        if( entry->row )
            ToriRS_DbRowFree(entry->row);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->dbrow_cache);
    provider->dbrow_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderDbRow), CACHE_PROVIDER_DBROW_CAPACITY);
}

void
CacheProvider_DbTableIndexAdd(
    struct CacheProvider* provider,
    int table_id,
    struct ToriRS_DbTableIndex* index)
{
    struct MapEntry_ProviderDbIndex* entry;

    assert(provider);
    assert(index);

    cache_provider_hmap_prepare_insert(&provider->dbindex_cache);
    entry = (struct MapEntry_ProviderDbIndex*)hmap_search(
        provider->dbindex_cache, &table_id, HMAP_INSERT);
    assert(entry && "DbTableIndex must be inserted into hmap");

    entry->id = table_id;
    entry->index = index;
}

struct ToriRS_DbTableIndex*
CacheProvider_DbTableIndexGet(
    struct CacheProvider* provider,
    int table_id)
{
    struct MapEntry_ProviderDbIndex* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderDbIndex*)hmap_search(
        provider->dbindex_cache, &table_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->index;
}

bool
CacheProvider_DbTableIndexHas(
    struct CacheProvider* provider,
    int table_id)
{
    return CacheProvider_DbTableIndexGet(provider, table_id) != NULL;
}

void
CacheProvider_DbTableIndexesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderDbIndex* entry;

    assert(provider);
    if( !provider->dbindex_cache )
        return;

    iter = hmap_iter_new(provider->dbindex_cache);
    while( (entry = (struct MapEntry_ProviderDbIndex*)hmap_iter_next(iter)) )
    {
        if( entry->index )
            ToriRS_DbTableIndexFree(entry->index);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->dbindex_cache);
    provider->dbindex_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderDbIndex), CACHE_PROVIDER_DBINDEX_CAPACITY);
}

void
CacheProvider_WorldMapSet(
    struct CacheProvider* provider,
    struct ToriRS_WorldMapAreas* areas)
{
    assert(provider);

    if( provider->worldmap_areas == areas )
        return;
    ToriRS_WorldMapAreasFree(provider->worldmap_areas);
    provider->worldmap_areas = areas;
}

struct ToriRS_WorldMapAreas*
CacheProvider_WorldMapGet(struct CacheProvider* provider)
{
    assert(provider);
    return provider->worldmap_areas;
}

void
CacheProvider_WorldMapGeographyAdd(
    struct CacheProvider* provider,
    int key,
    struct RSCache_WorldMapGeography* geography)
{
    struct MapEntry_ProviderWorldMapGeography* entry;

    assert(provider);
    assert(provider->worldmap_geography_cache);
    assert(geography);

    cache_provider_hmap_prepare_insert(&provider->worldmap_geography_cache);
    entry = (struct MapEntry_ProviderWorldMapGeography*)hmap_search(
        provider->worldmap_geography_cache, &key, HMAP_INSERT);
    assert(entry && "World map geography must be inserted into hmap");

    entry->id = key;
    entry->geography = geography;
}

struct RSCache_WorldMapGeography*
CacheProvider_WorldMapGeographyGet(
    struct CacheProvider* provider,
    int key)
{
    struct MapEntry_ProviderWorldMapGeography* entry;

    assert(provider);
    if( !provider->worldmap_geography_cache )
        return NULL;

    entry = (struct MapEntry_ProviderWorldMapGeography*)hmap_search(
        provider->worldmap_geography_cache, &key, HMAP_FIND);
    return entry ? entry->geography : NULL;
}

bool
CacheProvider_WorldMapGeographyHas(
    struct CacheProvider* provider,
    int key)
{
    return CacheProvider_WorldMapGeographyGet(provider, key) != NULL;
}

void
CacheProvider_WorldMapGeographyRelease(
    struct CacheProvider* provider,
    int key)
{
    struct MapEntry_ProviderWorldMapGeography* entry;

    assert(provider);
    if( !provider->worldmap_geography_cache )
        return;

    entry = (struct MapEntry_ProviderWorldMapGeography*)hmap_search(
        provider->worldmap_geography_cache, &key, HMAP_FIND);
    if( !entry )
        return;

    RSCache_WorldMapGeographyFreeInplace(entry->geography);
    free(entry->geography);
    entry->geography = NULL;
    hmap_search(provider->worldmap_geography_cache, &key, HMAP_REMOVE);
}

void
CacheProvider_MapElementAdd(
    struct CacheProvider* provider,
    int element_id,
    struct ToriRS_MapElement* element)
{
    struct MapEntry_ProviderMapElement* entry;

    assert(provider);
    assert(provider->mapelement_cache);
    assert(element);

    cache_provider_hmap_prepare_insert(&provider->mapelement_cache);
    entry = (struct MapEntry_ProviderMapElement*)hmap_search(
        provider->mapelement_cache, &element_id, HMAP_INSERT);
    assert(entry && "Map element must be inserted into hmap");

    entry->id = element_id;
    entry->element = element;
}

struct ToriRS_MapElement*
CacheProvider_MapElementGet(
    struct CacheProvider* provider,
    int element_id)
{
    struct MapEntry_ProviderMapElement* entry;

    assert(provider);
    if( !provider->mapelement_cache )
        return NULL;

    entry = (struct MapEntry_ProviderMapElement*)hmap_search(
        provider->mapelement_cache, &element_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->element;
}

bool
CacheProvider_MapElementHas(
    struct CacheProvider* provider,
    int element_id)
{
    return CacheProvider_MapElementGet(provider, element_id) != NULL;
}

void
CacheProvider_MapElementsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderMapElement* entry;

    assert(provider);
    if( !provider->mapelement_cache )
        return;

    iter = hmap_iter_new(provider->mapelement_cache);
    while( (entry = (struct MapEntry_ProviderMapElement*)hmap_iter_next(iter)) )
    {
        if( entry->element )
            ToriRS_MapElementFree(entry->element);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->mapelement_cache);
    provider->mapelement_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapElement), CACHE_PROVIDER_MAPELEMENT_CAPACITY);
}

void
CacheProvider_ComponentPackAdd(
    struct CacheProvider* provider,
    int iface_id,
    struct ToriRS_ComponentPack* pack)
{
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);
    assert(pack);

    cache_provider_hmap_prepare_insert(&provider->componentpack_cache);
    entry = (struct MapEntry_ProviderComponentPack*)hmap_search(
        provider->componentpack_cache, &iface_id, HMAP_INSERT);
    assert(entry && "Component pack must be inserted into hmap");

    entry->id = iface_id;
    entry->pack = pack;
}

struct ToriRS_ComponentPack*
CacheProvider_ComponentPackGet(
    struct CacheProvider* provider,
    int iface_id)
{
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderComponentPack*)hmap_search(
        provider->componentpack_cache, &iface_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->pack;
}

bool
CacheProvider_ComponentPackHas(
    struct CacheProvider* provider,
    int iface_id)
{
    return CacheProvider_ComponentPackGet(provider, iface_id) != NULL;
}

void
CacheProvider_ComponentPacksCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderComponentPack* entry;

    assert(provider);
    if( !provider->componentpack_cache )
        return;

    iter = hmap_iter_new(provider->componentpack_cache);
    while( (entry = (struct MapEntry_ProviderComponentPack*)hmap_iter_next(iter)) )
    {
        if( entry->pack )
            ToriRS_ComponentPackFree(entry->pack);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->componentpack_cache);
    provider->componentpack_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderComponentPack), CACHE_PROVIDER_COMPONENTPACK_CAPACITY);
}

struct ToriRS_Component*
CacheProvider_ComponentGet(
    struct CacheProvider* provider,
    int packed_id)
{
    int iface;
    int child;
    struct ToriRS_ComponentPack* pack;
    int i;

    assert(provider);

    iface = packed_id >> 16;
    child = packed_id & 0xFFFF;
    pack = CacheProvider_ComponentPackGet(provider, iface);
    if( !pack )
        return NULL;

    if( child >= 0 && child < pack->component_count )
    {
        struct ToriRS_Component* by_index = &pack->components[child];
        if( by_index->id == packed_id )
            return by_index;
    }

    for( i = 0; i < pack->component_count; i++ )
    {
        if( pack->components[i].id == packed_id )
            return &pack->components[i];
    }

    /* Fallback when id encoding differs: treat low 16 bits as child index. */
    if( child >= 0 && child < pack->component_count )
        return &pack->components[child];

    return NULL;
}

bool
CacheProvider_ComponentHas(
    struct CacheProvider* provider,
    int packed_id)
{
    return CacheProvider_ComponentGet(provider, packed_id) != NULL;
}

bool
CacheProvider_ComponentPackHasForComponent(
    struct CacheProvider* provider,
    int packed_id)
{
    assert(provider);
    return CacheProvider_ComponentPackHas(provider, packed_id >> 16);
}

void
CacheProvider_ClientScriptAdd(
    struct CacheProvider* provider,
    int script_id,
    struct CS2VM2_Script* script)
{
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);
    assert(script);

    cache_provider_hmap_prepare_insert(&provider->clientscript_cache);
    entry = (struct MapEntry_ProviderClientScript*)hmap_search(
        provider->clientscript_cache, &script_id, HMAP_INSERT);
    assert(entry && "Client script must be inserted into hmap");

    entry->id = script_id;
    entry->script = script;
}

struct CS2VM2_Script*
CacheProvider_ClientScriptGet(
    struct CacheProvider* provider,
    int script_id)
{
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderClientScript*)hmap_search(
        provider->clientscript_cache, &script_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->script;
}

bool
CacheProvider_ClientScriptHas(
    struct CacheProvider* provider,
    int script_id)
{
    return CacheProvider_ClientScriptGet(provider, script_id) != NULL;
}

void
CacheProvider_ClientScriptsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderClientScript* entry;

    assert(provider);
    if( !provider->clientscript_cache )
        return;

    iter = hmap_iter_new(provider->clientscript_cache);
    while( (entry = (struct MapEntry_ProviderClientScript*)hmap_iter_next(iter)) )
    {
        if( entry->script )
            CS2VM2_ScriptFree(entry->script);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->clientscript_cache);
    provider->clientscript_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderClientScript), CACHE_PROVIDER_CLIENTSCRIPT_CAPACITY);
}

/* Lowercase `name` into `out` (NUL-terminated, capped at out_size) and return a
 * case-insensitive djb2 hash of the lowercased text. The item search matches on
 * the lowercased name (reference: obj.name.toLowerCase()), so indexing and
 * lookup share this one transform. */
static int
objtype_name_lower_hash(
    char const* name,
    char* out,
    size_t out_size)
{
    size_t i = 0;
    unsigned int hash = 5381u;

    assert(name);
    assert(out && out_size > 0);

    for( ; name[i] != '\0' && i + 1 < out_size; i++ )
    {
        char c = name[i];
        if( c >= 'A' && c <= 'Z' )
            c = (char)(c - 'A' + 'a');
        out[i] = c;
        hash = ((hash << 5) + hash) + (unsigned char)c;
    }
    out[i] = '\0';
    return (int)hash;
}

void
CacheProvider_ObjtypeAdd(
    struct CacheProvider* provider,
    int obj_id,
    struct ToriRS_Objtype* objtype)
{
    struct MapEntry_ProviderObjtype* entry;

    assert(provider);
    assert(objtype);

    cache_provider_hmap_prepare_insert(&provider->objtype_cache);
    entry = (struct MapEntry_ProviderObjtype*)hmap_search(
        provider->objtype_cache, &obj_id, HMAP_INSERT);
    assert(entry && "Objtype must be inserted into hmap");

    entry->id = obj_id;
    entry->objtype = objtype;

    /* Mirror the insert into the name index so CacheProvider_ObjtypeIdByName and
     * the OC_FIND search resolve by name. Skip nameless entries and the "null"
     * sentinel; on a duplicate name the later id wins (an exact-name index only
     * needs one representative, and the substring search reads objtype_cache). */
    if( provider->objtype_name_cache && objtype->name[0] != '\0' )
    {
        char lower[TORIRS_NAME_MAX];
        int name_hash = objtype_name_lower_hash(objtype->name, lower, sizeof(lower));
        if( strcmp(lower, "null") != 0 )
        {
            struct MapEntry_ProviderObjtypeName* name_entry;
            cache_provider_hmap_prepare_insert(&provider->objtype_name_cache);
            name_entry = (struct MapEntry_ProviderObjtypeName*)hmap_search(
                provider->objtype_name_cache, &name_hash, HMAP_INSERT);
            if( name_entry )
            {
                name_entry->name_hash = name_hash;
                name_entry->obj_id = obj_id;
            }
        }
    }
}

struct ToriRS_Objtype*
CacheProvider_ObjtypeGet(
    struct CacheProvider* provider,
    int obj_id)
{
    struct MapEntry_ProviderObjtype* entry;
    struct ToriRS_Objtype* objtype;

    assert(provider);

    entry = (struct MapEntry_ProviderObjtype*)hmap_search(
        provider->objtype_cache, &obj_id, HMAP_FIND);
    if( !entry )
        return NULL;
    objtype = entry->objtype;

    /* Lazy genCert (reference ObjType.list runs genCert in the getter): a bank
     * note is always stackable and takes its display name from the cert_link
     * base item — the raw cache config carries neither. The link loads async
     * here (ObjModelLoad pulls it in), so patch whenever it has become
     * resident; a non-empty name marks the copy as already done. */
    if( objtype && objtype->cert_template > 0 )
    {
        objtype->stackable = 1;
        if( objtype->name[0] == '\0' && objtype->cert_link > 0 )
        {
            struct MapEntry_ProviderObjtype* link_entry =
                (struct MapEntry_ProviderObjtype*)hmap_search(
                    provider->objtype_cache, &objtype->cert_link, HMAP_FIND);
            if( link_entry && link_entry->objtype && link_entry->objtype->name[0] )
            {
                char const* link_name = link_entry->objtype->name;
                memcpy(objtype->name, link_name, sizeof(objtype->name));
                /* genCert also synthesizes the examine (reference ObjType.genCert):
                 * "Swap this note at any bank for a/an <base item>.", the article
                 * chosen by the base name's first letter. */
                char first = link_name[0];
                if( first >= 'A' && first <= 'Z' )
                    first = (char)(first - 'A' + 'a');
                char const* article =
                    (first == 'a' || first == 'e' || first == 'i' || first == 'o' ||
                     first == 'u')
                        ? "an"
                        : "a";
                snprintf(objtype->desc, sizeof(objtype->desc),
                         "Swap this note at any bank for %s %s.", article, link_name);
            }
        }
    }
    return objtype;
}

bool
CacheProvider_ObjtypeHas(
    struct CacheProvider* provider,
    int obj_id)
{
    return CacheProvider_ObjtypeGet(provider, obj_id) != NULL;
}

void
CacheProvider_ObjtypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderObjtype* entry;

    assert(provider);
    if( !provider->objtype_cache )
        return;

    iter = hmap_iter_new(provider->objtype_cache);
    while( (entry = (struct MapEntry_ProviderObjtype*)hmap_iter_next(iter)) )
    {
        if( entry->objtype )
            ToriRS_ObjtypeFree(entry->objtype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->objtype_cache);
    provider->objtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderObjtype), CACHE_PROVIDER_OBJTYPE_CAPACITY);

    /* The name index only holds ids into objtype_cache, so it must be reset in
     * lockstep; the full-scan flag is likewise no longer true. */
    if( provider->objtype_name_cache )
    {
        cache_provider_hmap_free(provider->objtype_name_cache);
        provider->objtype_name_cache = cache_provider_hmap_new(
            sizeof(struct MapEntry_ProviderObjtypeName), CACHE_PROVIDER_OBJTYPE_NAME_CAPACITY);
    }
    provider->objtypes_all_loaded = false;
}

int
CacheProvider_ObjtypeIdByName(
    struct CacheProvider* provider,
    char const* name)
{
    struct MapEntry_ProviderObjtypeName* entry;
    struct ToriRS_Objtype* obj;
    char lower[TORIRS_NAME_MAX];
    int name_hash;

    assert(provider);
    if( !name || !name[0] || !provider->objtype_name_cache )
        return -1;

    name_hash = objtype_name_lower_hash(name, lower, sizeof(lower));
    entry = (struct MapEntry_ProviderObjtypeName*)hmap_search(
        provider->objtype_name_cache, &name_hash, HMAP_FIND);
    if( !entry )
        return -1;

    /* Reject a hash collision that would hand back a different name. */
    obj = CacheProvider_ObjtypeGet(provider, entry->obj_id);
    if( obj )
    {
        char check[TORIRS_NAME_MAX];
        objtype_name_lower_hash(obj->name, check, sizeof(check));
        if( strcmp(check, lower) != 0 )
            return -1;
    }
    return entry->obj_id;
}

static int
objtype_id_cmp(const void* lhs, const void* rhs)
{
    int lid = *(const int*)lhs;
    int rid = *(const int*)rhs;
    return (lid > rid) - (lid < rid);
}

int
CacheProvider_ObjtypeSearchByName(
    struct CacheProvider* provider,
    char const* lower_query,
    int** out_ids)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderObjtype* entry;
    int* ids = NULL;
    int count = 0;
    int cap = 0;

    if( out_ids )
        *out_ids = NULL;
    if( !provider || !provider->objtype_cache || !lower_query || !lower_query[0] )
        return 0;

    iter = hmap_iter_new(provider->objtype_cache);
    while( (entry = (struct MapEntry_ProviderObjtype*)hmap_iter_next(iter)) )
    {
        struct ToriRS_Objtype* objtype = entry->objtype;
        char lower[TORIRS_NAME_MAX];

        if( !objtype || objtype->name[0] == '\0' )
            continue;
        objtype_name_lower_hash(objtype->name, lower, sizeof(lower));
        if( strcmp(lower, "null") == 0 )
            continue;
        if( !strstr(lower, lower_query) )
            continue;

        if( count == cap )
        {
            int new_cap = cap ? cap * 2 : 64;
            int* grown = realloc(ids, (size_t)new_cap * sizeof(int));
            if( !grown )
                break;
            ids = grown;
            cap = new_cap;
        }
        ids[count++] = entry->id;
    }
    hmap_iter_free(iter);

    /* Ascending id order, matching the reference scan (0..65534). */
    if( count > 1 )
        qsort(ids, (size_t)count, sizeof(int), objtype_id_cmp);

    if( out_ids )
        *out_ids = ids;
    else
        free(ids);
    return count;
}

void
CacheProvider_NpctypeAdd(
    struct CacheProvider* provider,
    int npc_id,
    struct ToriRS_Npctype* npctype)
{
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);
    assert(npctype);

    cache_provider_hmap_prepare_insert(&provider->npctype_cache);
    entry = (struct MapEntry_ProviderNpctype*)hmap_search(
        provider->npctype_cache, &npc_id, HMAP_INSERT);
    assert(entry && "Npctype must be inserted into hmap");

    entry->id = npc_id;
    entry->npctype = npctype;
}

struct ToriRS_Npctype*
CacheProvider_NpctypeGet(
    struct CacheProvider* provider,
    int npc_id)
{
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderNpctype*)hmap_search(
        provider->npctype_cache, &npc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->npctype;
}

bool
CacheProvider_NpctypeHas(
    struct CacheProvider* provider,
    int npc_id)
{
    return CacheProvider_NpctypeGet(provider, npc_id) != NULL;
}

void
CacheProvider_NpctypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderNpctype* entry;

    assert(provider);
    if( !provider->npctype_cache )
        return;

    iter = hmap_iter_new(provider->npctype_cache);
    while( (entry = (struct MapEntry_ProviderNpctype*)hmap_iter_next(iter)) )
    {
        if( entry->npctype )
            ToriRS_NpctypeFree(entry->npctype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->npctype_cache);
    provider->npctype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderNpctype), CACHE_PROVIDER_NPCTYPE_CAPACITY);
}

void
CacheProvider_SpotanimtypeAdd(
    struct CacheProvider* provider,
    int spotanim_id,
    struct ToriRS_Spotanimtype* spotanimtype)
{
    struct MapEntry_ProviderSpotanimtype* entry;

    assert(provider);
    assert(spotanimtype);

    cache_provider_hmap_prepare_insert(&provider->spotanimtype_cache);
    entry = (struct MapEntry_ProviderSpotanimtype*)hmap_search(
        provider->spotanimtype_cache, &spotanim_id, HMAP_INSERT);
    assert(entry && "Spotanimtype must be inserted into hmap");

    entry->id = spotanim_id;
    entry->spotanimtype = spotanimtype;
}

struct ToriRS_Spotanimtype*
CacheProvider_SpotanimtypeGet(
    struct CacheProvider* provider,
    int spotanim_id)
{
    struct MapEntry_ProviderSpotanimtype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderSpotanimtype*)hmap_search(
        provider->spotanimtype_cache, &spotanim_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->spotanimtype;
}

bool
CacheProvider_SpotanimtypeHas(
    struct CacheProvider* provider,
    int spotanim_id)
{
    return CacheProvider_SpotanimtypeGet(provider, spotanim_id) != NULL;
}

void
CacheProvider_SpotanimtypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderSpotanimtype* entry;

    assert(provider);
    if( !provider->spotanimtype_cache )
        return;

    iter = hmap_iter_new(provider->spotanimtype_cache);
    while( (entry = (struct MapEntry_ProviderSpotanimtype*)hmap_iter_next(iter)) )
    {
        if( entry->spotanimtype )
            ToriRS_SpotanimtypeFree(entry->spotanimtype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->spotanimtype_cache);
    provider->spotanimtype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSpotanimtype), CACHE_PROVIDER_SPOTANIMTYPE_CAPACITY);
}

/*
 * Sounds are cached as rendered PCM, one entry per effect id.
 *
 * Both eras load them differently — dat1 decodes the whole bank in one pass and
 * fills every id at once, dat2 loads one archive per id on demand — but both end
 * up here, so the game only ever asks the provider for a sound id.
 */
void
CacheProvider_SoundAdd(
    struct CacheProvider* provider,
    int sound_id,
    struct ToriRS_Sound* sound)
{
    struct MapEntry_ProviderSound* entry;

    assert(provider);
    assert(sound);

    cache_provider_hmap_prepare_insert(&provider->sound_cache);
    entry =
        (struct MapEntry_ProviderSound*)hmap_search(provider->sound_cache, &sound_id, HMAP_INSERT);
    assert(entry && "Sound must be inserted into hmap");

    entry->id = sound_id;
    entry->sound = sound;
}

struct ToriRS_Sound*
CacheProvider_SoundGet(
    struct CacheProvider* provider,
    int sound_id)
{
    struct MapEntry_ProviderSound* entry;

    assert(provider);

    entry =
        (struct MapEntry_ProviderSound*)hmap_search(provider->sound_cache, &sound_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->sound;
}

bool
CacheProvider_SoundHas(
    struct CacheProvider* provider,
    int sound_id)
{
    return CacheProvider_SoundGet(provider, sound_id) != NULL;
}

void
CacheProvider_SoundsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderSound* entry;

    assert(provider);
    if( !provider->sound_cache )
        return;

    iter = hmap_iter_new(provider->sound_cache);
    while( (entry = (struct MapEntry_ProviderSound*)hmap_iter_next(iter)) )
    {
        if( entry->sound )
            ToriRS_SoundFree(entry->sound);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->sound_cache);
    provider->sound_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSound), CACHE_PROVIDER_SOUND_CAPACITY);
}

void
CacheProvider_IdkAdd(
    struct CacheProvider* provider,
    int idk_id,
    struct ToriRS_Idk* idk)
{
    struct MapEntry_ProviderIdk* entry;

    assert(provider);
    assert(idk);

    cache_provider_hmap_prepare_insert(&provider->idk_cache);
    entry = (struct MapEntry_ProviderIdk*)hmap_search(provider->idk_cache, &idk_id, HMAP_INSERT);
    assert(entry && "Idk must be inserted into hmap");

    entry->id = idk_id;
    entry->idk = idk;
}

struct ToriRS_Idk*
CacheProvider_IdkGet(
    struct CacheProvider* provider,
    int idk_id)
{
    struct MapEntry_ProviderIdk* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderIdk*)hmap_search(provider->idk_cache, &idk_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->idk;
}

bool
CacheProvider_IdkHas(
    struct CacheProvider* provider,
    int idk_id)
{
    return CacheProvider_IdkGet(provider, idk_id) != NULL;
}

void
CacheProvider_IdksCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderIdk* entry;

    assert(provider);
    if( !provider->idk_cache )
        return;

    iter = hmap_iter_new(provider->idk_cache);
    while( (entry = (struct MapEntry_ProviderIdk*)hmap_iter_next(iter)) )
    {
        if( entry->idk )
            ToriRS_IdkFree(entry->idk);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->idk_cache);
    provider->sound_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderSound), CACHE_PROVIDER_SOUND_CAPACITY);
    provider->idk_cache =
        cache_provider_hmap_new(sizeof(struct MapEntry_ProviderIdk), CACHE_PROVIDER_IDK_CAPACITY);
}

void
CacheProvider_MapTerrainAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapTerrain* terrain)
{
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);
    assert(terrain);

    cache_provider_hmap_prepare_insert(&provider->map_terrain_cache);
    entry = (struct MapEntry_ProviderMapTerrain*)hmap_search(
        provider->map_terrain_cache, &map_id, HMAP_INSERT);
    assert(entry && "Map terrain must be inserted into hmap");

    entry->id = map_id;
    entry->terrain = terrain;
}

struct ToriRS_MapTerrain*
CacheProvider_MapTerrainGet(
    struct CacheProvider* provider,
    int map_id)
{
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderMapTerrain*)hmap_search(
        provider->map_terrain_cache, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
CacheProvider_MapTerrainHas(
    struct CacheProvider* provider,
    int map_id)
{
    return CacheProvider_MapTerrainGet(provider, map_id) != NULL;
}

void
CacheProvider_MapTerrainsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderMapTerrain* entry;

    assert(provider);
    if( !provider->map_terrain_cache )
        return;

    iter = hmap_iter_new(provider->map_terrain_cache);
    while( (entry = (struct MapEntry_ProviderMapTerrain*)hmap_iter_next(iter)) )
    {
        if( entry->terrain )
            ToriRS_MapTerrainFree(entry->terrain);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->map_terrain_cache);
    provider->map_terrain_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapTerrain), CACHE_PROVIDER_MAP_TERRAIN_CAPACITY);
}

void
CacheProvider_MapSceneryAdd(
    struct CacheProvider* provider,
    int map_id,
    struct ToriRS_MapLocs* locs)
{
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);
    assert(locs);

    cache_provider_hmap_prepare_insert(&provider->map_scenery_cache);
    entry = (struct MapEntry_ProviderMapScenery*)hmap_search(
        provider->map_scenery_cache, &map_id, HMAP_INSERT);
    assert(entry && "Map scenery must be inserted into hmap");

    entry->id = map_id;
    entry->locs = locs;
}

struct ToriRS_MapLocs*
CacheProvider_MapSceneryGet(
    struct CacheProvider* provider,
    int map_id)
{
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderMapScenery*)hmap_search(
        provider->map_scenery_cache, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
CacheProvider_MapSceneryHas(
    struct CacheProvider* provider,
    int map_id)
{
    return CacheProvider_MapSceneryGet(provider, map_id) != NULL;
}

void
CacheProvider_MapSceneryCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderMapScenery* entry;

    assert(provider);
    if( !provider->map_scenery_cache )
        return;

    iter = hmap_iter_new(provider->map_scenery_cache);
    while( (entry = (struct MapEntry_ProviderMapScenery*)hmap_iter_next(iter)) )
    {
        if( entry->locs )
            ToriRS_MapLocsFree(entry->locs);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->map_scenery_cache);
    provider->map_scenery_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderMapScenery), CACHE_PROVIDER_MAP_SCENERY_CAPACITY);
}

void
CacheProvider_LocationAdd(
    struct CacheProvider* provider,
    int loc_id,
    struct ToriRS_Location* location)
{
    struct MapEntry_ProviderLocation* entry;

    assert(provider);
    assert(location);

    cache_provider_hmap_prepare_insert(&provider->location_cache);
    entry = (struct MapEntry_ProviderLocation*)hmap_search(
        provider->location_cache, &loc_id, HMAP_INSERT);
    assert(entry && "Location must be inserted into hmap");

    entry->id = loc_id;
    entry->location = location;
}

struct ToriRS_Location*
CacheProvider_LocationGet(
    struct CacheProvider* provider,
    int loc_id)
{
    struct MapEntry_ProviderLocation* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderLocation*)hmap_search(
        provider->location_cache, &loc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->location;
}

bool
CacheProvider_LocationHas(
    struct CacheProvider* provider,
    int loc_id)
{
    return CacheProvider_LocationGet(provider, loc_id) != NULL;
}

void
CacheProvider_LocationsCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderLocation* entry;

    assert(provider);
    if( !provider->location_cache )
        return;

    iter = hmap_iter_new(provider->location_cache);
    while( (entry = (struct MapEntry_ProviderLocation*)hmap_iter_next(iter)) )
    {
        if( entry->location )
            ToriRS_LocationFree(entry->location);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->location_cache);
    provider->location_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderLocation), CACHE_PROVIDER_LOCATION_CAPACITY);
}

void
CacheProvider_FlotypeAdd(
    struct CacheProvider* provider,
    int flo_id,
    struct ToriRS_Flotype* flotype)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    assert(flotype);

    cache_provider_hmap_prepare_insert(&provider->flotype_cache);
    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->flotype_cache, &flo_id, HMAP_INSERT);
    assert(entry && "Flotype must be inserted into hmap");

    entry->id = flo_id;
    entry->flotype = flotype;
}

struct ToriRS_Flotype*
CacheProvider_FlotypeGet(
    struct CacheProvider* provider,
    int flo_id)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->flotype_cache, &flo_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
CacheProvider_FlotypeHas(
    struct CacheProvider* provider,
    int flo_id)
{
    return CacheProvider_FlotypeGet(provider, flo_id) != NULL;
}

void
CacheProvider_FlotypesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    if( !provider->flotype_cache )
        return;

    iter = hmap_iter_new(provider->flotype_cache);
    while( (entry = (struct MapEntry_ProviderFlotype*)hmap_iter_next(iter)) )
    {
        if( entry->flotype )
            ToriRS_FlotypeFree(entry->flotype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->flotype_cache);
    provider->flotype_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_FLOTYPE_CAPACITY);
}

void
CacheProvider_UnderlayAdd(
    struct CacheProvider* provider,
    int underlay_id,
    struct ToriRS_Flotype* underlay)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    assert(underlay);

    cache_provider_hmap_prepare_insert(&provider->underlay_cache);
    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->underlay_cache, &underlay_id, HMAP_INSERT);
    assert(entry && "Underlay must be inserted into hmap");

    entry->id = underlay_id;
    entry->flotype = underlay;
}

struct ToriRS_Flotype*
CacheProvider_UnderlayGet(
    struct CacheProvider* provider,
    int underlay_id)
{
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderFlotype*)hmap_search(
        provider->underlay_cache, &underlay_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->flotype;
}

bool
CacheProvider_UnderlayHas(
    struct CacheProvider* provider,
    int underlay_id)
{
    return CacheProvider_UnderlayGet(provider, underlay_id) != NULL;
}

void
CacheProvider_UnderlaysCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderFlotype* entry;

    assert(provider);
    if( !provider->underlay_cache )
        return;

    iter = hmap_iter_new(provider->underlay_cache);
    while( (entry = (struct MapEntry_ProviderFlotype*)hmap_iter_next(iter)) )
    {
        if( entry->flotype )
            ToriRS_FlotypeFree(entry->flotype);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->underlay_cache);
    provider->underlay_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderFlotype), CACHE_PROVIDER_UNDERLAY_CAPACITY);
}

void
CacheProvider_TextureAdd(
    struct CacheProvider* provider,
    int texture_id,
    struct ToriRS_Texture* texture)
{
    struct MapEntry_ProviderTexture* entry;

    assert(provider);
    assert(texture);

    cache_provider_hmap_prepare_insert(&provider->texture_cache);
    entry = (struct MapEntry_ProviderTexture*)hmap_search(
        provider->texture_cache, &texture_id, HMAP_INSERT);
    assert(entry && "Texture must be inserted into hmap");

    entry->id = texture_id;
    entry->texture = texture;
}

struct ToriRS_Texture*
CacheProvider_TextureGet(
    struct CacheProvider* provider,
    int texture_id)
{
    struct MapEntry_ProviderTexture* entry;

    assert(provider);

    entry = (struct MapEntry_ProviderTexture*)hmap_search(
        provider->texture_cache, &texture_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

bool
CacheProvider_TextureHas(
    struct CacheProvider* provider,
    int texture_id)
{
    return CacheProvider_TextureGet(provider, texture_id) != NULL;
}

bool
CacheProvider_TextureIsSd(
    struct CacheProvider* provider,
    int texture_id)
{
    assert(provider);
    if( !provider->vtable->TextureIsSd )
        return true;
    return provider->vtable->TextureIsSd(provider, texture_id);
}

void
CacheProvider_TexturesCleanup(struct CacheProvider* provider)
{
    struct HMapIter* iter;
    struct MapEntry_ProviderTexture* entry;

    assert(provider);
    if( !provider->texture_cache )
        return;

    iter = hmap_iter_new(provider->texture_cache);
    while( (entry = (struct MapEntry_ProviderTexture*)hmap_iter_next(iter)) )
    {
        if( entry->texture )
            ToriRS_TextureFree(entry->texture);
    }
    hmap_iter_free(iter);

    cache_provider_hmap_free(provider->texture_cache);
    provider->texture_cache = cache_provider_hmap_new(
        sizeof(struct MapEntry_ProviderTexture), CACHE_PROVIDER_TEXTURE_CAPACITY);
}
