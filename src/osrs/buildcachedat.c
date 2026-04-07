#include "buildcachedat.h"

#include "graphics/dash.h"

#include <stdbool.h>
#include "texture.h"

#define BUILDCACHEDAT_EVENTBUFFER_DEFAULT_CAPACITY 1024
#define BUILDCACHEDAT_HMAP_INITIAL_CAPACITY 64

struct ContainerEntry
{
    char name[64]; // Key must be first field and fixed size for DashMap
    enum BuildCacheContainerKind kind;

    union
    {
        struct FileListDat* _filelist;
        struct
        {
            char* data;
            int data_size;
        } _jagfilepack;

        struct
        {
            char* data;
            int data_size;
            char* index_data;
            int index_data_size;
        } _jagfilepack_indexed;
    };
};

struct SpriteEntry
{
    char name[64]; // Key must be first field and fixed size for DashMap
    struct DashSprite* sprite;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

struct FlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct ConfigLocEntry
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct AnimframeEntry
{
    int id;
    struct CacheAnimframe* animframe;
};

struct AnimbaseframesEntry
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
};

struct SceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct FontEntry
{
    char name[BUILDCACHEDAT_FONT_NAME_MAX]; // Key must be first field; zero-padded
    struct DashPixFont* font;
};

struct SequenceEntry
{
    int id;
    struct CacheDatSequence* sequence;
};

struct IdkEntry
{
    int id;
    struct CacheDatConfigIdk* idk;
};

struct ObjEntry
{
    int id;
    struct CacheDatConfigObj* obj;
};

struct IdkModelEntry
{
    int id;
    struct CacheModel* model;
};

struct ObjModelEntry
{
    int id;
    struct CacheModel* model;
};

struct NpcEntry
{
    int id;
    struct CacheDatConfigNpc* npc;
};

struct NpcModelEntry
{
    int id;
    struct CacheModel* model;
};

struct ComponentEntry
{
    int id;
    struct CacheDatConfigComponent* component;
};

struct ComponentSpriteEntry
{
    char sprite_name[64]; // Key must be first field and fixed size for DashMap
    struct DashSprite* sprite;
};

static struct DashMap*
buildcachedat_create_hmap(
    size_t key_size,
    size_t entry_size,
    size_t initial_capacity)
{
    size_t buffer_size = dashmap_buffer_size_for(entry_size, initial_capacity);
    struct DashMapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = key_size,
        .entry_size = entry_size,
    };
    return dashmap_new(&config, 0);
}

static void
buildcachedat_maybe_grow_hmap(struct DashMap* map)
{
    uint32_t count = dashmap_count(map);
    uint32_t capacity = dashmap_capacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = dashmap_entry_size(map);
    size_t new_buffer_size = dashmap_buffer_size_for(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = dashmap_resize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == DASHMAP_OK);
    (void)rc;
    free(old_buffer);
}

static void
buildcachedat_eventbuffer_push(
    struct BuildCacheDat* buildcachedat,
    struct BuildCacheDatEvent event)
{
    if( !buildcachedat || !buildcachedat->eventbuffer || buildcachedat->eventbuffer_capacity <= 0 )
        return;

    if( buildcachedat->eventbuffer_count == buildcachedat->eventbuffer_capacity )
    {
        buildcachedat->eventbuffer_head =
            (buildcachedat->eventbuffer_head + 1) % buildcachedat->eventbuffer_capacity;
        buildcachedat->eventbuffer_count--;
    }

    buildcachedat->eventbuffer[buildcachedat->eventbuffer_tail] = event;
    buildcachedat->eventbuffer_tail =
        (buildcachedat->eventbuffer_tail + 1) % buildcachedat->eventbuffer_capacity;
    buildcachedat->eventbuffer_count++;
}

static struct DashMap*
buildcachedat_new_scenery_hmap(void)
{
    return buildcachedat_create_hmap(
        sizeof(int), sizeof(struct SceneryEntry), BUILDCACHEDAT_HMAP_INITIAL_CAPACITY);
}

static struct DashMap*
buildcachedat_new_map_terrains_hmap(void)
{
    return buildcachedat_create_hmap(
        sizeof(int), sizeof(struct MapTerrainEntry), BUILDCACHEDAT_HMAP_INITIAL_CAPACITY);
}

static void
buildcachedat_init_maps_and_eventbuffer(struct BuildCacheDat* buildcachedat)
{
    size_t cap = BUILDCACHEDAT_HMAP_INITIAL_CAPACITY;

    buildcachedat->textures_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct TextureEntry), cap);
    buildcachedat->fonts_hmap =
        buildcachedat_create_hmap(BUILDCACHEDAT_FONT_NAME_MAX, sizeof(struct FontEntry), cap);
    buildcachedat->flotype_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct FlotypeEntry), cap);
    buildcachedat->scenery_hmap = buildcachedat_new_scenery_hmap();
    buildcachedat->models_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct ModelEntry), cap);
    buildcachedat->config_loc_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct ConfigLocEntry), cap);
    buildcachedat->animframes_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct AnimframeEntry), cap);
    buildcachedat->animbaseframes_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct AnimbaseframesEntry), cap);
    buildcachedat->sequences_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct SequenceEntry), cap);
    buildcachedat->idk_hmap = buildcachedat_create_hmap(sizeof(int), sizeof(struct IdkEntry), cap);
    buildcachedat->obj_hmap = buildcachedat_create_hmap(sizeof(int), sizeof(struct ObjEntry), cap);
    buildcachedat->idk_models_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct IdkModelEntry), cap);
    buildcachedat->obj_models_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct ObjModelEntry), cap);
    buildcachedat->map_terrains_hmap = buildcachedat_new_map_terrains_hmap();
    buildcachedat->npc_hmap = buildcachedat_create_hmap(sizeof(int), sizeof(struct NpcEntry), cap);
    buildcachedat->npc_models_hmap =
        buildcachedat_create_hmap(sizeof(int), sizeof(struct NpcModelEntry), cap);
    if( !buildcachedat->component_hmap )
        buildcachedat->component_hmap =
            buildcachedat_create_hmap(sizeof(int), sizeof(struct ComponentEntry), cap);
    if( !buildcachedat->component_sprites_hmap )
        buildcachedat->component_sprites_hmap =
            buildcachedat_create_hmap(64, sizeof(struct ComponentSpriteEntry), cap);
    buildcachedat->sprites = buildcachedat_create_hmap(64, sizeof(struct SpriteEntry), cap);
    buildcachedat->containers_hmap =
        buildcachedat_create_hmap(64, sizeof(struct ContainerEntry), cap);

    buildcachedat->eventbuffer_capacity = BUILDCACHEDAT_EVENTBUFFER_DEFAULT_CAPACITY;
    buildcachedat->eventbuffer =
        calloc((size_t)buildcachedat->eventbuffer_capacity, sizeof(struct BuildCacheDatEvent));
    buildcachedat->eventbuffer_head = 0;
    buildcachedat->eventbuffer_tail = 0;
    buildcachedat->eventbuffer_count = 0;
}

struct BuildCacheDat*
buildcachedat_new(void)
{
    struct BuildCacheDat* buildcachedat = malloc(sizeof(struct BuildCacheDat));
    memset(buildcachedat, 0, sizeof(struct BuildCacheDat));
    buildcachedat_init_maps_and_eventbuffer(buildcachedat);
    return buildcachedat;
}

static void
dashmap_free_entries(
    struct DashMap* map,
    void (*entry_free_fn)(void*))
{
    if( !map )
        return;
    if( entry_free_fn )
    {
        struct DashMapIter* iter = dashmap_iter_new(map);
        void* entry;
        while( (entry = dashmap_iter_next(iter)) )
            entry_free_fn(entry);
        dashmap_iter_free(iter);
    }
    free(dashmap_buffer_ptr(map));
    dashmap_free(map);
}

static void
free_texture_entry(void* e)
{
    texture_free(((struct TextureEntry*)e)->texture);
}
static void
free_font_entry(void* e)
{
    dashpixfont_free(((struct FontEntry*)e)->font);
}
static void
free_flotype_entry(void* e)
{
    config_floortype_overlay_free(((struct FlotypeEntry*)e)->flotype);
}
static void
free_scenery_entry(void* e)
{
    map_locs_free(((struct SceneryEntry*)e)->locs);
}
static void
free_model_entry(void* e)
{
    model_free(((struct ModelEntry*)e)->model);
}
static void
free_config_loc_entry(void* e)
{
    config_locs_free(((struct ConfigLocEntry*)e)->config_loc);
}
static void
free_animbaseframes_entry(void* e)
{
    cache_dat_animbaseframes_free(((struct AnimbaseframesEntry*)e)->animbaseframes);
}
static void
free_sequence_entry(void* e)
{
    config_dat_sequence_free(((struct SequenceEntry*)e)->sequence);
}
static void
free_idk_entry(void* e)
{
    cache_dat_config_idk_free(((struct IdkEntry*)e)->idk);
}
static void
free_obj_entry(void* e)
{
    cache_dat_config_obj_free(((struct ObjEntry*)e)->obj);
}
static void
free_idk_model_entry(void* e)
{
    model_free(((struct IdkModelEntry*)e)->model);
}
static void
free_obj_model_entry(void* e)
{
    model_free(((struct ObjModelEntry*)e)->model);
}
static void
free_map_terrain_entry(void* e)
{
    map_terrain_free(((struct MapTerrainEntry*)e)->map_terrain);
}
static void
free_npc_entry(void* e)
{
    cache_dat_config_npc_free(((struct NpcEntry*)e)->npc);
}
static void
free_npc_model_entry(void* e)
{
    model_free(((struct NpcModelEntry*)e)->model);
}
static void
free_component_entry(void* e)
{
    cache_dat_config_component_free(((struct ComponentEntry*)e)->component);
}
static void
free_component_sprite_entry(void* e)
{
    dashsprite_free(((struct ComponentSpriteEntry*)e)->sprite);
}
static void
free_sprite_entry(void* e)
{
    dashsprite_free(((struct SpriteEntry*)e)->sprite);
}
static void
free_container_entry(void* e)
{
    struct ContainerEntry* entry = (struct ContainerEntry*)e;
    switch( entry->kind )
    {
    case BuildCacheContainerKind_Jagfile:
        filelist_dat_free(entry->_filelist);
        break;
    case BuildCacheContainerKind_JagfilePack:
        free(entry->_jagfilepack.data);
        break;
    case BuildCacheContainerKind_JagfilePackIndexed:
        free(entry->_jagfilepack_indexed.data);
        free(entry->_jagfilepack_indexed.index_data);
        break;
    }
}

void
buildcachedat_free(struct BuildCacheDat* buildcachedat)
{
    if( !buildcachedat )
        return;

    dashmap_free_entries(buildcachedat->textures_hmap, free_texture_entry);
    dashmap_free_entries(buildcachedat->fonts_hmap, free_font_entry);
    dashmap_free_entries(buildcachedat->flotype_hmap, free_flotype_entry);
    dashmap_free_entries(buildcachedat->scenery_hmap, free_scenery_entry);
    dashmap_free_entries(buildcachedat->models_hmap, free_model_entry);
    dashmap_free_entries(buildcachedat->config_loc_hmap, free_config_loc_entry);

    /* animframes_hmap entries are borrowed pointers into animbaseframes -- only free the map */
    dashmap_free_entries(buildcachedat->animframes_hmap, NULL);
    dashmap_free_entries(buildcachedat->animbaseframes_hmap, free_animbaseframes_entry);

    dashmap_free_entries(buildcachedat->sequences_hmap, free_sequence_entry);
    dashmap_free_entries(buildcachedat->idk_hmap, free_idk_entry);
    dashmap_free_entries(buildcachedat->obj_hmap, free_obj_entry);
    dashmap_free_entries(buildcachedat->idk_models_hmap, free_idk_model_entry);
    dashmap_free_entries(buildcachedat->obj_models_hmap, free_obj_model_entry);
    dashmap_free_entries(buildcachedat->map_terrains_hmap, free_map_terrain_entry);
    dashmap_free_entries(buildcachedat->npc_hmap, free_npc_entry);
    dashmap_free_entries(buildcachedat->npc_models_hmap, free_npc_model_entry);
    dashmap_free_entries(buildcachedat->component_hmap, free_component_entry);
    dashmap_free_entries(buildcachedat->component_sprites_hmap, free_component_sprite_entry);
    dashmap_free_entries(buildcachedat->sprites, free_sprite_entry);
    dashmap_free_entries(buildcachedat->containers_hmap, free_container_entry);

    filelist_dat_free(buildcachedat->cfg_config_jagfile);
    filelist_dat_free(buildcachedat->cfg_versionlist_jagfile);
    filelist_dat_free(buildcachedat->cfg_media_jagfile);

    free(buildcachedat->eventbuffer);
    free(buildcachedat);
}

static void
buildcachedat_clear_internal(struct BuildCacheDat* buildcachedat, bool keep_fonts)
{
    if( !buildcachedat )
        return;

    struct DashMap* fonts_saved = NULL;
    if( keep_fonts )
    {
        fonts_saved = buildcachedat->fonts_hmap;
        buildcachedat->fonts_hmap = NULL;
    }
    else
    {
        dashmap_free_entries(buildcachedat->fonts_hmap, free_font_entry);
    }

    // dashmap_free_entries(buildcachedat->textures_hmap, free_texture_entry);
    dashmap_free_entries(buildcachedat->flotype_hmap, free_flotype_entry);
    dashmap_free_entries(buildcachedat->scenery_hmap, free_scenery_entry);
    dashmap_free_entries(buildcachedat->models_hmap, free_model_entry);
    dashmap_free_entries(buildcachedat->config_loc_hmap, free_config_loc_entry);

    dashmap_free_entries(buildcachedat->animframes_hmap, NULL);
    dashmap_free_entries(buildcachedat->animbaseframes_hmap, free_animbaseframes_entry);

    dashmap_free_entries(buildcachedat->sequences_hmap, free_sequence_entry);
    dashmap_free_entries(buildcachedat->idk_hmap, free_idk_entry);
    dashmap_free_entries(buildcachedat->obj_hmap, free_obj_entry);
    dashmap_free_entries(buildcachedat->idk_models_hmap, free_idk_model_entry);
    dashmap_free_entries(buildcachedat->obj_models_hmap, free_obj_model_entry);
    dashmap_free_entries(buildcachedat->map_terrains_hmap, free_map_terrain_entry);
    dashmap_free_entries(buildcachedat->npc_hmap, free_npc_entry);
    dashmap_free_entries(buildcachedat->npc_models_hmap, free_npc_model_entry);
    // dashmap_free_entries(buildcachedat->component_hmap, free_component_entry);
    // dashmap_free_entries(buildcachedat->component_sprites_hmap, free_component_sprite_entry);
    dashmap_free_entries(buildcachedat->sprites, free_sprite_entry);
    dashmap_free_entries(buildcachedat->containers_hmap, free_container_entry);

    filelist_dat_free(buildcachedat->cfg_config_jagfile);
    buildcachedat->cfg_config_jagfile = NULL;
    filelist_dat_free(buildcachedat->cfg_versionlist_jagfile);
    buildcachedat->cfg_versionlist_jagfile = NULL;
    filelist_dat_free(buildcachedat->cfg_media_jagfile);
    buildcachedat->cfg_media_jagfile = NULL;

    free(buildcachedat->eventbuffer);
    buildcachedat->eventbuffer = NULL;

    buildcachedat_init_maps_and_eventbuffer(buildcachedat);

    if( keep_fonts && fonts_saved )
    {
        dashmap_free_entries(buildcachedat->fonts_hmap, NULL);
        buildcachedat->fonts_hmap = fonts_saved;
    }
}

void
buildcachedat_clear(struct BuildCacheDat* buildcachedat)
{
    buildcachedat_clear_internal(buildcachedat, false);
}

void
buildcachedat_clear_keep_fonts(struct BuildCacheDat* buildcachedat)
{
    buildcachedat_clear_internal(buildcachedat, true);
}

void
buildcachedat_clear_map_chunks(struct BuildCacheDat* buildcachedat)
{
    if( !buildcachedat )
        return;

    dashmap_free_entries(buildcachedat->map_terrains_hmap, free_map_terrain_entry);
    dashmap_free_entries(buildcachedat->scenery_hmap, free_scenery_entry);
    buildcachedat->map_terrains_hmap = buildcachedat_new_map_terrains_hmap();
    buildcachedat->scenery_hmap = buildcachedat_new_scenery_hmap();
}

void
buildcachedat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* config_jagfile)
{
    if( buildcachedat->cfg_config_jagfile )
        filelist_dat_free(buildcachedat->cfg_config_jagfile);
    buildcachedat->cfg_config_jagfile = config_jagfile;
}

struct FileListDat*
buildcachedat_config_jagfile(struct BuildCacheDat* buildcachedat)
{
    return buildcachedat->cfg_config_jagfile;
}

void
buildcachedat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* versionlist_jagfile)
{
    if( buildcachedat->cfg_versionlist_jagfile )
        filelist_dat_free(buildcachedat->cfg_versionlist_jagfile);
    buildcachedat->cfg_versionlist_jagfile = versionlist_jagfile;
}

struct FileListDat*
buildcachedat_versionlist_jagfile(struct BuildCacheDat* buildcachedat)
{
    return buildcachedat->cfg_versionlist_jagfile;
}

void
buildcachedat_set_named_jagfile(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    struct FileListDat* jagfile)
{
    if( strcmp(name, "config_jagfile") == 0 )
    {
        buildcachedat_set_config_jagfile(buildcachedat, jagfile);
        return;
    }
    else if( strcmp(name, "versionlist_jagfile") == 0 )
    {
        buildcachedat_set_versionlist_jagfile(buildcachedat, jagfile);
        return;
    }
    else if( strcmp(name, "media_jagfile") == 0 )
    {
        buildcachedat_set_2d_media_jagfile(buildcachedat, jagfile);
        return;
    }

    struct ContainerEntry* container_entry = (struct ContainerEntry*)dashmap_search(
        buildcachedat->containers_hmap, name, DASHMAP_INSERT);
    assert(container_entry && "Container must be inserted into hmap");
    strncpy(container_entry->name, name, sizeof(container_entry->name) - 1);
    container_entry->kind = BuildCacheContainerKind_Jagfile;
    container_entry->_filelist = jagfile;
    buildcachedat_maybe_grow_hmap(buildcachedat->containers_hmap);
}

struct FileListDat*
buildcachedat_named_jagfile(
    struct BuildCacheDat* buildcachedat,
    char const* name)
{
    char jagfile_name[64] = { 0 };
    strncpy(jagfile_name, name, 63);

    if( strcmp(jagfile_name, "config_jagfile") == 0 )
        return buildcachedat_config_jagfile(buildcachedat);
    else if( strcmp(jagfile_name, "versionlist_jagfile") == 0 )
        return buildcachedat_versionlist_jagfile(buildcachedat);
    else if( strcmp(jagfile_name, "media_jagfile") == 0 )
        return buildcachedat->cfg_media_jagfile;

    struct ContainerEntry* container_entry = (struct ContainerEntry*)dashmap_search(
        buildcachedat->containers_hmap, name, DASHMAP_INSERT);
    assert(container_entry && "Container must be inserted into hmap");
    struct FileListDat* result = container_entry->_filelist;
    buildcachedat_maybe_grow_hmap(buildcachedat->containers_hmap);

    return result;
}

void
buildcachedat_add_jagfilepack(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    void* data,
    int size)
{
    struct ContainerEntry* container_entry = (struct ContainerEntry*)dashmap_search(
        buildcachedat->containers_hmap, name, DASHMAP_INSERT);
    assert(container_entry && "Container must be inserted into hmap");
    strncpy(container_entry->name, name, sizeof(container_entry->name) - 1);
    container_entry->kind = BuildCacheContainerKind_JagfilePack;
    container_entry->_jagfilepack.data = data;
    container_entry->_jagfilepack.data_size = size;
    buildcachedat_maybe_grow_hmap(buildcachedat->containers_hmap);
}

void
buildcachedat_add_jagfilepack_indexed(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    void* data,
    int data_size,
    void* index_data,
    int index_data_size)
{
    struct ContainerEntry* container_entry = (struct ContainerEntry*)dashmap_search(
        buildcachedat->containers_hmap, name, DASHMAP_INSERT);
    assert(container_entry && "Container must be inserted into hmap");
    strncpy(container_entry->name, name, sizeof(container_entry->name) - 1);
    container_entry->kind = BuildCacheContainerKind_JagfilePackIndexed;
    container_entry->_jagfilepack_indexed.data = data;
    container_entry->_jagfilepack_indexed.data_size = data_size;
    container_entry->_jagfilepack_indexed.index_data = index_data;
    container_entry->_jagfilepack_indexed.index_data_size = index_data_size;
    buildcachedat_maybe_grow_hmap(buildcachedat->containers_hmap);
}

struct JagfilePack*
buildcachedat_named_jagfilepack(
    struct BuildCacheDat* buildcachedat,
    const char* name)
{
    struct ContainerEntry* container_entry =
        (struct ContainerEntry*)dashmap_search(buildcachedat->containers_hmap, name, DASHMAP_FIND);
    if( !container_entry )
        return NULL;

    struct JagfilePack* container = malloc(sizeof(struct JagfilePack));
    container->data = container_entry->_jagfilepack.data;
    container->data_size = container_entry->_jagfilepack.data_size;
    return container;
}

struct BuildCacheDatContainer*
buildcachedat_named_container(
    struct BuildCacheDat* buildcachedat,
    const char* name)
{
    struct ContainerEntry* container_entry =
        (struct ContainerEntry*)dashmap_search(buildcachedat->containers_hmap, name, DASHMAP_FIND);
    if( !container_entry )
        return NULL;

    struct FileListDat* jagfile = NULL;
    if( strcmp(name, "config_jagfile") == 0 )
    {
        jagfile = buildcachedat_config_jagfile(buildcachedat);
    }
    else if( strcmp(name, "versionlist_jagfile") == 0 )
    {
        jagfile = buildcachedat_versionlist_jagfile(buildcachedat);
    }
    else if( strcmp(name, "media_jagfile") == 0 )
    {
        jagfile = buildcachedat->cfg_media_jagfile;
    }

    if( jagfile )
    {
        struct BuildCacheDatContainer* container = malloc(sizeof(struct BuildCacheDatContainer));
        container->kind = BuildCacheContainerKind_Jagfile;
        container->_filelist = jagfile;
        return container;
    }

    struct BuildCacheDatContainer* container = malloc(sizeof(struct BuildCacheDatContainer));
    switch( container_entry->kind )
    {
    case BuildCacheContainerKind_Jagfile:
        container->kind = BuildCacheContainerKind_Jagfile;
        container->_filelist = container_entry->_filelist;
        break;
    case BuildCacheContainerKind_JagfilePack:
        container->kind = BuildCacheContainerKind_JagfilePack;
        container->_jagfilepack.data = container_entry->_jagfilepack.data;
        container->_jagfilepack.data_size = container_entry->_jagfilepack.data_size;
        break;
    case BuildCacheContainerKind_JagfilePackIndexed:
        container->kind = BuildCacheContainerKind_JagfilePackIndexed;
        container->_jagfilepack_indexed.data = container_entry->_jagfilepack_indexed.data;
        container->_jagfilepack_indexed.data_size = container_entry->_jagfilepack_indexed.data_size;
        container->_jagfilepack_indexed.index_data =
            container_entry->_jagfilepack_indexed.index_data;
        container->_jagfilepack_indexed.index_data_size =
            container_entry->_jagfilepack_indexed.index_data_size;
        break;
    }
    return container;
}

void
buildcachedat_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* media_jagile)
{
    if( buildcachedat->cfg_media_jagfile )
        filelist_dat_free(buildcachedat->cfg_media_jagfile);
    buildcachedat->cfg_media_jagfile = media_jagile;
}

void
buildcachedat_add_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id,
    struct CacheConfigOverlay* flotype)
{
    struct FlotypeEntry* flotype_entry = (struct FlotypeEntry*)dashmap_search(
        buildcachedat->flotype_hmap, &flotype_id, DASHMAP_INSERT);
    assert(flotype_entry && "Flotype must be inserted into hmap");
    flotype_entry->id = flotype_id;
    flotype_entry->flotype = flotype;
    buildcachedat_maybe_grow_hmap(buildcachedat->flotype_hmap);
}

struct CacheConfigOverlay*
buildcachedat_get_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id)
{
    struct FlotypeEntry* flotype_entry = (struct FlotypeEntry*)dashmap_search(
        buildcachedat->flotype_hmap, &flotype_id, DASHMAP_FIND);
    if( !flotype_entry )
        return NULL;
    return flotype_entry->flotype;
}

void
buildcachedat_add_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id,
    struct DashTexture* texture)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_search(
        buildcachedat->textures_hmap, &texture_id, DASHMAP_INSERT);
    assert(texture_entry && "Texture must be inserted into hmap");
    texture_entry->id = texture_id;
    texture_entry->texture = texture;
    buildcachedat_maybe_grow_hmap(buildcachedat->textures_hmap);
    buildcachedat_eventbuffer_push(
        buildcachedat,
        (struct BuildCacheDatEvent){
            .type = BUILDCACHEDAT_EVENT_TEXTURE_ADDED,
            .texture_id = texture_id,
        });
}

struct DashTexture*
buildcachedat_get_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_search(
        buildcachedat->textures_hmap, &texture_id, DASHMAP_FIND);
    if( !texture_entry )
        return NULL;
    return texture_entry->texture;
}

struct DashMapIter*
buildcachedat_iter_new_textures(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->textures_hmap);
}

struct DashTexture*
buildcachedat_iter_next_texture(struct DashMapIter* iter)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_iter_next(iter);
    if( !texture_entry )
        return NULL;
    return texture_entry->texture;
}

struct DashTexture*
buildcachedat_iter_next_texture_id(
    struct DashMapIter* iter,
    int* out_id)
{
    struct TextureEntry* texture_entry = (struct TextureEntry*)dashmap_iter_next(iter);
    if( !texture_entry )
        return NULL;
    *out_id = texture_entry->id;
    return texture_entry->texture;
}

void
buildcachedat_add_font(
    struct BuildCacheDat* buildcachedat,
    const char* font_name,
    struct DashPixFont* font)
{
    char buffer[BUILDCACHEDAT_FONT_NAME_MAX] = { 0 };
    strncpy(buffer, font_name, BUILDCACHEDAT_FONT_NAME_MAX);
    struct FontEntry* font_entry =
        (struct FontEntry*)dashmap_search(buildcachedat->fonts_hmap, buffer, DASHMAP_INSERT);
    assert(font_entry && "Font must be inserted into hmap");
    memset(font_entry->name, 0, sizeof(font_entry->name));
    strncpy(font_entry->name, font_name, BUILDCACHEDAT_FONT_NAME_MAX);
    font_entry->font = font;
    buildcachedat_maybe_grow_hmap(buildcachedat->fonts_hmap);
}

struct DashPixFont*
buildcachedat_get_font(
    struct BuildCacheDat* buildcachedat,
    const char* font_name)
{
    char buffer[BUILDCACHEDAT_FONT_NAME_MAX] = { 0 };
    strncpy(buffer, font_name, BUILDCACHEDAT_FONT_NAME_MAX);
    struct FontEntry* font_entry =
        (struct FontEntry*)dashmap_search(buildcachedat->fonts_hmap, buffer, DASHMAP_FIND);
    if( !font_entry )
        return NULL;
    return font_entry->font;
}

struct DashMapIter*
buildcachedat_iter_new_fonts(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->fonts_hmap);
}

struct DashPixFont*
buildcachedat_iter_next_font(struct DashMapIter* iter)
{
    struct FontEntry* font_entry = (struct FontEntry*)dashmap_iter_next(iter);
    if( !font_entry )
        return NULL;
    return font_entry->font;
}

struct DashPixFont*
buildcachedat_iter_next_font_name(
    struct DashMapIter* iter,
    char* out_name,
    int out_name_cap)
{
    struct FontEntry* font_entry = (struct FontEntry*)dashmap_iter_next(iter);
    if( !font_entry )
        return NULL;
    if( out_name && out_name_cap > 0 )
    {
        int i = 0;
        int lim = (int)sizeof(font_entry->name);
        if( lim > out_name_cap - 1 )
            lim = out_name_cap - 1;
        for( ; i < lim; i++ )
        {
            char c = font_entry->name[i];
            out_name[i] = c;
            if( c == '\0' )
                break;
        }
        out_name[i] = '\0';
    }
    return font_entry->font;
}

bool
buildcachedat_eventbuffer_is_empty(struct BuildCacheDat* buildcachedat)
{
    if( !buildcachedat )
        return true;
    return buildcachedat->eventbuffer_count <= 0;
}

bool
buildcachedat_eventbuffer_pop(
    struct BuildCacheDat* buildcachedat,
    struct BuildCacheDatEvent* out_event)
{
    if( !buildcachedat || !out_event || buildcachedat->eventbuffer_count <= 0 )
        return false;
    *out_event = buildcachedat->eventbuffer[buildcachedat->eventbuffer_head];
    buildcachedat->eventbuffer_head =
        (buildcachedat->eventbuffer_head + 1) % buildcachedat->eventbuffer_capacity;
    buildcachedat->eventbuffer_count--;
    return true;
}

void
buildcachedat_eventbuffer_clear(struct BuildCacheDat* buildcachedat)
{
    if( !buildcachedat )
        return;
    buildcachedat->eventbuffer_head = 0;
    buildcachedat->eventbuffer_tail = 0;
    buildcachedat->eventbuffer_count = 0;
}

void
buildcachedat_add_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapLocs* locs)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* scenery_entry =
        (struct SceneryEntry*)dashmap_search(buildcachedat->scenery_hmap, &mapxz, DASHMAP_INSERT);
    assert(scenery_entry && "Scenery must be inserted into hmap");
    scenery_entry->id = mapxz;
    scenery_entry->mapx = mapx;
    scenery_entry->mapz = mapz;
    scenery_entry->locs = locs;
    buildcachedat_maybe_grow_hmap(buildcachedat->scenery_hmap);
}

struct CacheMapLocs*
buildcachedat_get_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* scenery_entry =
        (struct SceneryEntry*)dashmap_search(buildcachedat->scenery_hmap, &mapxz, DASHMAP_FIND);
    if( !scenery_entry )
        return NULL;
    return scenery_entry->locs;
}

struct CacheMapLocs*
buildcachedat_iter_next_scenery(struct DashMapIter* iter)
{
    struct SceneryEntry* scenery_entry = (struct SceneryEntry*)dashmap_iter_next(iter);
    if( !scenery_entry )
        return NULL;
    return scenery_entry->locs;
}

struct DashMapIter*
buildcachedat_iter_new_scenery(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->scenery_hmap);
}

void
buildcachedat_add_model(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    struct CacheModel* model)
{
    struct ModelEntry* model_entry =
        (struct ModelEntry*)dashmap_search(buildcachedat->models_hmap, &model_id, DASHMAP_INSERT);
    assert(model_entry && "Model must be inserted into hmap");
    model_entry->id = model_id;
    model_entry->model = model;
    buildcachedat_maybe_grow_hmap(buildcachedat->models_hmap);
}

struct CacheModel*
buildcachedat_get_model(
    struct BuildCacheDat* buildcachedat,
    int model_id)
{
    struct ModelEntry* model_entry =
        (struct ModelEntry*)dashmap_search(buildcachedat->models_hmap, &model_id, DASHMAP_FIND);
    if( !model_entry )
        return NULL;
    return model_entry->model;
}

void
buildcachedat_add_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheModel* model)
{
    struct IdkModelEntry* idk_entry = (struct IdkModelEntry*)dashmap_search(
        buildcachedat->idk_models_hmap, &idk_id, DASHMAP_INSERT);
    assert(idk_entry && "Idk must be inserted into hmap");
    idk_entry->id = idk_id;
    idk_entry->model = model;
    buildcachedat_maybe_grow_hmap(buildcachedat->idk_models_hmap);
}

struct CacheModel*
buildcachedat_get_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id)
{
    struct IdkModelEntry* idk_entry = (struct IdkModelEntry*)dashmap_search(
        buildcachedat->idk_models_hmap, &idk_id, DASHMAP_FIND);
    if( !idk_entry )
        return NULL;
    return idk_entry->model;
}

void
buildcachedat_add_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheModel* model)
{
    struct ObjModelEntry* obj_entry = (struct ObjModelEntry*)dashmap_search(
        buildcachedat->obj_models_hmap, &obj_id, DASHMAP_INSERT);
    assert(obj_entry && "Obj must be inserted into hmap");
    obj_entry->id = obj_id;
    obj_entry->model = model;
    buildcachedat_maybe_grow_hmap(buildcachedat->obj_models_hmap);
}

struct CacheModel*
buildcachedat_get_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id)
{
    struct ObjModelEntry* obj_entry = (struct ObjModelEntry*)dashmap_search(
        buildcachedat->obj_models_hmap, &obj_id, DASHMAP_FIND);
    if( !obj_entry )
        return NULL;
    return obj_entry->model;
}

void
buildcachedat_add_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheDatConfigNpc* npc)
{
    struct NpcEntry* npc_entry =
        (struct NpcEntry*)dashmap_search(buildcachedat->npc_hmap, &npc_id, DASHMAP_INSERT);
    assert(npc_entry && "Npc must be inserted into hmap");
    npc_entry->id = npc_id;
    npc_entry->npc = npc;
    buildcachedat_maybe_grow_hmap(buildcachedat->npc_hmap);
}

struct CacheDatConfigNpc*
buildcachedat_get_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id)
{
    struct NpcEntry* npc_entry =
        (struct NpcEntry*)dashmap_search(buildcachedat->npc_hmap, &npc_id, DASHMAP_FIND);
    if( !npc_entry )
        return NULL;
    return npc_entry->npc;
}

struct DashMapIter*
buildcachedat_iter_new_npcs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->npc_hmap);
}

struct CacheDatConfigNpc*
buildcachedat_iter_next_npc(struct DashMapIter* iter)
{
    struct NpcEntry* npc_entry = (struct NpcEntry*)dashmap_iter_next(iter);
    if( !npc_entry )
        return NULL;
    return npc_entry->npc;
}

void
buildcachedat_add_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheModel* model)
{
    struct NpcModelEntry* npc_model_entry = (struct NpcModelEntry*)dashmap_search(
        buildcachedat->npc_models_hmap, &npc_id, DASHMAP_INSERT);
    assert(npc_model_entry && "Npc model must be inserted into hmap");
    npc_model_entry->id = npc_id;
    npc_model_entry->model = model;
    buildcachedat_maybe_grow_hmap(buildcachedat->npc_models_hmap);
}

struct CacheModel*
buildcachedat_get_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id)
{
    struct NpcModelEntry* npc_model_entry = (struct NpcModelEntry*)dashmap_search(
        buildcachedat->npc_models_hmap, &npc_id, DASHMAP_FIND);
    if( !npc_model_entry )
        return NULL;
    return npc_model_entry->model;
}

// TODO: Needs to only be done by loc id and not shape select.
void
buildcachedat_add_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id,
    struct CacheConfigLocation* config_loc)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        buildcachedat->config_loc_hmap, &config_loc_id, DASHMAP_INSERT);
    assert(config_loc_entry && "Config loc must be inserted into hmap");
    config_loc_entry->id = config_loc_id;
    config_loc_entry->config_loc = config_loc;
    buildcachedat_maybe_grow_hmap(buildcachedat->config_loc_hmap);
}

struct CacheConfigLocation*
buildcachedat_iter_next_config_loc(struct DashMapIter* iter)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_iter_next(iter);
    if( !config_loc_entry )
        return NULL;
    return config_loc_entry->config_loc;
}

struct CacheConfigLocation*
buildcachedat_get_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id)
{
    struct ConfigLocEntry* config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        buildcachedat->config_loc_hmap, &config_loc_id, DASHMAP_FIND);
    if( !config_loc_entry )
        return NULL;
    return config_loc_entry->config_loc;
}

struct DashMapIter*
buildcachedat_iter_new_config_locs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->config_loc_hmap);
}

void
buildcachedat_add_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id,
    struct CacheAnimframe* animframe)
{
    struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
        buildcachedat->animframes_hmap, &animframe_id, DASHMAP_INSERT);
    assert(animframe_entry && "Animframe must be inserted into hmap");
    animframe_entry->id = animframe_id;
    animframe_entry->animframe = animframe;
    buildcachedat_maybe_grow_hmap(buildcachedat->animframes_hmap);
}

struct CacheAnimframe*
buildcachedat_get_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id)
{
    struct AnimframeEntry* animframe_entry = (struct AnimframeEntry*)dashmap_search(
        buildcachedat->animframes_hmap, &animframe_id, DASHMAP_FIND);
    if( !animframe_entry )
        return NULL;
    return animframe_entry->animframe;
}

void
buildcachedat_add_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes)
{
    struct AnimbaseframesEntry* animbaseframes_entry = (struct AnimbaseframesEntry*)dashmap_search(
        buildcachedat->animbaseframes_hmap, &animbaseframes_id, DASHMAP_INSERT);
    assert(animbaseframes_entry && "Animbaseframes must be inserted into hmap");
    animbaseframes_entry->id = animbaseframes_id;
    animbaseframes_entry->animbaseframes = animbaseframes;
    buildcachedat_maybe_grow_hmap(buildcachedat->animbaseframes_hmap);
}

struct CacheDatAnimBaseFrames*
buildcachedat_get_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id)
{
    struct AnimbaseframesEntry* animbaseframes_entry = (struct AnimbaseframesEntry*)dashmap_search(
        buildcachedat->animbaseframes_hmap, &animbaseframes_id, DASHMAP_FIND);
    if( !animbaseframes_entry )
        return NULL;
    return animbaseframes_entry->animbaseframes;
}

void
buildcachedat_add_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id,
    struct CacheDatSequence* sequence)
{
    struct SequenceEntry* sequence_entry = (struct SequenceEntry*)dashmap_search(
        buildcachedat->sequences_hmap, &sequence_id, DASHMAP_INSERT);
    assert(sequence_entry && "Sequence must be inserted into hmap");
    sequence_entry->id = sequence_id;
    sequence_entry->sequence = sequence;
    buildcachedat_maybe_grow_hmap(buildcachedat->sequences_hmap);
}

struct CacheDatSequence*
buildcachedat_get_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id)
{
    struct SequenceEntry* sequence_entry = (struct SequenceEntry*)dashmap_search(
        buildcachedat->sequences_hmap, &sequence_id, DASHMAP_FIND);
    if( !sequence_entry )
        return NULL;
    return sequence_entry->sequence;
}

void
buildcachedat_add_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheDatConfigIdk* idk)
{
    struct IdkEntry* idk_entry =
        (struct IdkEntry*)dashmap_search(buildcachedat->idk_hmap, &idk_id, DASHMAP_INSERT);
    assert(idk_entry && "Idk must be inserted into hmap");
    idk_entry->id = idk_id;
    idk_entry->idk = idk;
    buildcachedat_maybe_grow_hmap(buildcachedat->idk_hmap);
}

struct CacheDatConfigIdk*
buildcachedat_get_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id)
{
    struct IdkEntry* idk_entry =
        (struct IdkEntry*)dashmap_search(buildcachedat->idk_hmap, &idk_id, DASHMAP_FIND);
    if( !idk_entry )
        return NULL;
    return idk_entry->idk;
}

struct CacheDatConfigIdk*
buildcachedat_iter_next_idk(struct DashMapIter* iter)
{
    struct IdkEntry* idk_entry = (struct IdkEntry*)dashmap_iter_next(iter);
    if( !idk_entry )
        return NULL;
    return idk_entry->idk;
}

struct DashMapIter*
buildcachedat_iter_new_idks(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->idk_hmap);
}

void
buildcachedat_add_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheDatConfigObj* obj)
{
    struct ObjEntry* obj_entry =
        (struct ObjEntry*)dashmap_search(buildcachedat->obj_hmap, &obj_id, DASHMAP_INSERT);
    assert(obj_entry && "Obj must be inserted into hmap");
    obj_entry->id = obj_id;
    obj_entry->obj = obj;
    buildcachedat_maybe_grow_hmap(buildcachedat->obj_hmap);
}

struct CacheDatConfigObj*
buildcachedat_get_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id)
{
    struct ObjEntry* obj_entry =
        (struct ObjEntry*)dashmap_search(buildcachedat->obj_hmap, &obj_id, DASHMAP_FIND);
    if( !obj_entry )
        return NULL;
    return obj_entry->obj;
}

struct CacheDatConfigObj*
buildcachedat_iter_next_obj(struct DashMapIter* iter)
{
    struct ObjEntry* obj_entry = (struct ObjEntry*)dashmap_iter_next(iter);
    if( !obj_entry )
        return NULL;
    return obj_entry->obj;
}

struct DashMapIter*
buildcachedat_iter_new_objs(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->obj_hmap);
}

void
buildcachedat_add_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* map_terrain_entry = (struct MapTerrainEntry*)dashmap_search(
        buildcachedat->map_terrains_hmap, &mapxz, DASHMAP_INSERT);
    assert(map_terrain_entry && "Map terrain must be inserted into hmap");
    map_terrain_entry->id = mapxz;
    map_terrain_entry->mapx = mapx;
    map_terrain_entry->mapz = mapz;
    map_terrain_entry->map_terrain = map_terrain;
    buildcachedat_maybe_grow_hmap(buildcachedat->map_terrains_hmap);
}

struct CacheMapTerrain*
buildcachedat_get_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* map_terrain_entry = (struct MapTerrainEntry*)dashmap_search(
        buildcachedat->map_terrains_hmap, &mapxz, DASHMAP_FIND);
    if( !map_terrain_entry )
        return NULL;
    return map_terrain_entry->map_terrain;
}

void
buildcachedat_add_component(
    struct BuildCacheDat* buildcachedat,
    int component_id,
    struct CacheDatConfigComponent* component)
{
    struct ComponentEntry* component_entry = (struct ComponentEntry*)dashmap_search(
        buildcachedat->component_hmap, &component_id, DASHMAP_INSERT);
    assert(component_entry && "Component must be inserted into hmap");
    component_entry->id = component_id;
    component_entry->component = component;
    buildcachedat_maybe_grow_hmap(buildcachedat->component_hmap);
}

struct CacheDatConfigComponent*
buildcachedat_get_component(
    struct BuildCacheDat* buildcachedat,
    int component_id)
{
    struct ComponentEntry* component_entry = (struct ComponentEntry*)dashmap_search(
        buildcachedat->component_hmap, &component_id, DASHMAP_FIND);
    if( !component_entry )
        return NULL;
    return component_entry->component;
}

struct DashMapIter*
buildcachedat_component_iter_new(struct BuildCacheDat* buildcachedat)
{
    return dashmap_iter_new(buildcachedat->component_hmap);
}

struct CacheDatConfigComponent*
buildcachedat_component_iter_next(
    struct DashMapIter* iter,
    int* id_out)
{
    struct ComponentEntry* component_entry = (struct ComponentEntry*)dashmap_iter_next(iter);
    if( !component_entry )
        return NULL;
    if( id_out )
        *id_out = component_entry->id;
    return component_entry->component;
}

void
buildcachedat_add_component_sprite(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name,
    struct DashSprite* sprite)
{
    char buffer[65] = { 0 };
    strncpy(buffer, sprite_name, 64);
    struct ComponentSpriteEntry* sprite_entry = (struct ComponentSpriteEntry*)dashmap_search(
        buildcachedat->component_sprites_hmap, buffer, DASHMAP_INSERT);
    assert(sprite_entry && "Component sprite must be inserted into hmap");
    strncpy(sprite_entry->sprite_name, sprite_name, 63);
    sprite_entry->sprite_name[63] = '\0'; // Ensure null termination
    sprite_entry->sprite = sprite;
    buildcachedat_maybe_grow_hmap(buildcachedat->component_sprites_hmap);
}

struct DashSprite*
buildcachedat_get_component_sprite(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name)
{
    char buffer[65] = { 0 };
    strncpy(buffer, sprite_name, 64);
    struct ComponentSpriteEntry* sprite_entry = (struct ComponentSpriteEntry*)dashmap_search(
        buildcachedat->component_sprites_hmap, buffer, DASHMAP_FIND);
    if( !sprite_entry )
        return NULL;
    return sprite_entry->sprite;
}