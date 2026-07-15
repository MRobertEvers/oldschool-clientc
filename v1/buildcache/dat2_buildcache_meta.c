#include "dat2_buildcache.h"

#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_Font
{
    int id;
    struct Dat2BuildCache_FontAsset* asset;
};

struct MapEntry_ClientScript
{
    int id;
    struct RSCacheDat2A_ClientScript* script;
};

struct MapEntry_Param
{
    int id;
    struct RSCacheDat2A_ConfigParam* param;
};

struct MapEntry_Enum
{
    int id;
    struct RSCacheDat2A_ConfigEnum* entry;
};

struct MapEntry_Struct
{
    int id;
    struct RSCacheDat2A_ConfigStruct* entry;
};

static struct ToriDraw_Map*
dat2_buildcache_meta_map_new(
    struct Dat2BuildCache* dat2_buildcache,
    size_t entry_size,
    int capacity)
{
    return dat2_buildcache_map_new(dat2_buildcache, entry_size, capacity);
}

void
dat2_buildcache_meta_maps_init(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);

    dat2_buildcache->fonts_hmap = dat2_buildcache_meta_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Font), 256);
    dat2_buildcache->clientscripts_hmap = dat2_buildcache_meta_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ClientScript), 4096);
    dat2_buildcache->params_hmap = dat2_buildcache_meta_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Param), 4096);
    dat2_buildcache->enums_hmap = dat2_buildcache_meta_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Enum), 4096);
    dat2_buildcache->structs_hmap = dat2_buildcache_meta_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Struct), 4096);
}

static void
dat2_buildcache_meta_map_cleanup(
    struct Dat2BuildCache* dat2_buildcache,
    struct ToriDraw_Map** map_out,
    void (*free_entry)(void* entry))
{
    struct ToriDraw_MapIter* iter;
    void* entry;

    assert(dat2_buildcache);
    assert(map_out);
    if( !*map_out )
        return;

    if( free_entry )
    {
        iter = ToriDraw_MapIterNew(*map_out);
        while( (entry = ToriDraw_MapIterNext(iter)) )
            free_entry(entry);
        ToriDraw_MapIterFree(iter);
    }

    dat2_buildcache_map_free(dat2_buildcache, *map_out);
    *map_out = NULL;
}

static void
dat2_buildcache_meta_font_entry_free(void* entry_void)
{
    struct MapEntry_Font* entry = entry_void;
    if( entry && entry->asset )
        Dat2BuildCache_FontAssetFree(entry->asset);
}

static void
dat2_buildcache_meta_clientscript_entry_free(void* entry_void)
{
    struct MapEntry_ClientScript* entry = entry_void;
    if( entry && entry->script )
        RSCacheDat2A_ClientScriptFree(entry->script);
}

static void
dat2_buildcache_meta_param_entry_free(void* entry_void)
{
    struct MapEntry_Param* entry = entry_void;
    if( entry && entry->param )
        RSCacheDat2A_ConfigParamFree(entry->param);
}

static void
dat2_buildcache_meta_enum_entry_free(void* entry_void)
{
    struct MapEntry_Enum* entry = entry_void;
    if( entry && entry->entry )
        RSCacheDat2A_ConfigEnumFree(entry->entry);
}

static void
dat2_buildcache_meta_struct_entry_free(void* entry_void)
{
    struct MapEntry_Struct* entry = entry_void;
    if( entry && entry->entry )
        RSCacheDat2A_ConfigStructFree(entry->entry);
}

void
dat2_buildcache_meta_maps_free(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);

    dat2_buildcache_meta_map_cleanup(
        dat2_buildcache, &dat2_buildcache->fonts_hmap, dat2_buildcache_meta_font_entry_free);
    dat2_buildcache_meta_map_cleanup(
        dat2_buildcache,
        &dat2_buildcache->clientscripts_hmap,
        dat2_buildcache_meta_clientscript_entry_free);
    dat2_buildcache_meta_map_cleanup(
        dat2_buildcache, &dat2_buildcache->params_hmap, dat2_buildcache_meta_param_entry_free);
    dat2_buildcache_meta_map_cleanup(
        dat2_buildcache, &dat2_buildcache->enums_hmap, dat2_buildcache_meta_enum_entry_free);
    dat2_buildcache_meta_map_cleanup(
        dat2_buildcache, &dat2_buildcache->structs_hmap, dat2_buildcache_meta_struct_entry_free);
}

void
Dat2BuildCache_FontAssetFree(struct Dat2BuildCache_FontAsset* asset)
{
    if( !asset )
        return;
    if( asset->glyphs )
        RSCacheDat2A_SpritePackFree(asset->glyphs);
    free(asset);
}

bool
dat2_buildcache_font_add_from_archives(
    struct Dat2BuildCache* dat2_buildcache,
    int font_id,
    struct RSCacheDat2Disk_Archive* font_archive,
    struct RSCacheDat2Disk_Archive* sprite_archive)
{
    struct RSCacheShared_FileList* fl;
    struct Dat2BuildCache_FontAsset* asset;
    struct MapEntry_Font* entry;

    assert(dat2_buildcache);
    assert(font_archive);
    assert(sprite_archive);
    if( font_id < 0 )
        return false;

    if( dat2_buildcache_font_has(dat2_buildcache, font_id) )
        return true;

    fl = RSCacheShared_FileListNewFromCacheArchive(font_archive);
    if( !fl || fl->file_count <= 0 || fl->file_sizes[0] < DAT2_BUILDCACHE_FONT_METRICS_SIZE )
    {
        RSCacheShared_FileListFree(fl);
        return false;
    }

    asset = calloc(1, sizeof(*asset));
    if( !asset )
    {
        RSCacheShared_FileListFree(fl);
        return false;
    }

    memcpy(asset->metrics, fl->files[0], DAT2_BUILDCACHE_FONT_METRICS_SIZE);
    RSCacheShared_FileListFree(fl);

    asset->glyphs = RSCacheDat2A_SpritePackNewDecode(
        (unsigned char const*)sprite_archive->data,
        sprite_archive->data_size,
        SPRITELOAD_FLAG_NONE);
    if( !asset->glyphs )
    {
        Dat2BuildCache_FontAssetFree(asset);
        return false;
    }

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, dat2_buildcache->fonts_hmap);
    entry = (struct MapEntry_Font*)ToriDraw_MapSearch(
        dat2_buildcache->fonts_hmap, &font_id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        Dat2BuildCache_FontAssetFree(asset);
        return false;
    }

    if( entry->asset )
        Dat2BuildCache_FontAssetFree(entry->asset);
    entry->id = font_id;
    entry->asset = asset;
    dat2_buildcache_maybe_grow_hmap(dat2_buildcache, dat2_buildcache->fonts_hmap);
    return true;
}

struct Dat2BuildCache_FontAsset*
dat2_buildcache_font_get(
    struct Dat2BuildCache* dat2_buildcache,
    int font_id)
{
    struct MapEntry_Font* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Font*)ToriDraw_MapSearch(
        dat2_buildcache->fonts_hmap, &font_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->asset;
}

bool
dat2_buildcache_font_has(
    struct Dat2BuildCache* dat2_buildcache,
    int font_id)
{
    return dat2_buildcache_font_get(dat2_buildcache, font_id) != NULL;
}

void
dat2_buildcache_clientscript_add(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id,
    struct RSCacheDat2A_ClientScript* script)
{
    struct MapEntry_ClientScript* entry;

    assert(dat2_buildcache);
    assert(script);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, dat2_buildcache->clientscripts_hmap);
    entry = (struct MapEntry_ClientScript*)ToriDraw_MapSearch(
        dat2_buildcache->clientscripts_hmap, &script_id, TORIDRAW_MAP_INSERT);
    assert(entry && "Clientscript must be inserted into hmap");
    if( entry->script )
        RSCacheDat2A_ClientScriptFree(entry->script);
    entry->id = script_id;
    entry->script = script;
    dat2_buildcache_maybe_grow_hmap(dat2_buildcache, dat2_buildcache->clientscripts_hmap);
}

struct RSCacheDat2A_ClientScript*
dat2_buildcache_clientscript_get(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id)
{
    struct MapEntry_ClientScript* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ClientScript*)ToriDraw_MapSearch(
        dat2_buildcache->clientscripts_hmap, &script_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->script;
}

bool
dat2_buildcache_clientscript_has(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id)
{
    return dat2_buildcache_clientscript_get(dat2_buildcache, script_id) != NULL;
}

struct RSCacheDat2A_ClientScript*
dat2_buildcache_clientscript_decode_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    int script_id,
    int decode_flags)
{
    if( !archive || script_id < 0 )
        return NULL;
    return RSCacheDat2A_ClientScriptNewFromDecodeFlags(
        script_id,
        (const uint8_t*)archive->data,
        archive->data_size,
        decode_flags);
}

static void
dat2_buildcache_config_entries_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive,
    int config_kind,
    enum Dat2BuildCache_Kind kind,
    struct ToriDraw_Map* hmap,
    size_t entry_size,
    const int* wanted_ids,
    int wanted_count,
    void (*decode_and_insert)(
        struct Dat2BuildCache* bc,
        enum Dat2BuildCache_Kind kind,
        int id,
        const void* data,
        int data_size,
        struct ToriDraw_Map* map))
{
    struct RSCacheDat2Disk_ReferenceTable* configs_table;
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;
    int i;

    (void)config_kind;
    assert(dat2_buildcache);
    assert(archive);
    assert(hmap);
    assert(decode_and_insert);

    configs_table = dat2_buildcache_configs_table(dat2_buildcache);
    archive_ref = dat2_buildcache_archive_reference_from_table(configs_table, archive);
    if( !archive_ref )
        return;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return;

    for( i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref->children.files[i].id;
        if( wanted_ids && wanted_count > 0 )
        {
            int j;
            bool wanted = false;
            for( j = 0; j < wanted_count; j++ )
            {
                if( wanted_ids[j] == id )
                {
                    wanted = true;
                    break;
                }
            }
            if( !wanted )
                continue;
        }
        decode_and_insert(
            dat2_buildcache,
            kind,
            id,
            filelist->files[i],
            filelist->file_sizes[i],
            hmap);
    }

    RSCacheShared_FileListFree(filelist);
}

static void
dat2_buildcache_param_decode_insert(
    struct Dat2BuildCache* bc,
    enum Dat2BuildCache_Kind kind,
    int id,
    const void* data,
    int data_size,
    struct ToriDraw_Map* map)
{
    struct MapEntry_Param* entry;
    struct RSCacheDat2A_ConfigParam* param;

    (void)kind;
    if( ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_FIND) )
        return;

    param = calloc(1, sizeof(*param));
    if( !param )
        return;
    param->id = id;
    param->type_char = 'i';
    RSCacheDat2A_ConfigParamDecodeInplace(param, data, data_size);

    dat2_buildcache_prepare_hmap_insert(bc, map);
    entry = (struct MapEntry_Param*)ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        RSCacheDat2A_ConfigParamFree(param);
        return;
    }
    entry->id = id;
    entry->param = param;
    dat2_buildcache_maybe_grow_hmap(bc, map);
}

static void
dat2_buildcache_enum_decode_insert(
    struct Dat2BuildCache* bc,
    enum Dat2BuildCache_Kind kind,
    int id,
    const void* data,
    int data_size,
    struct ToriDraw_Map* map)
{
    struct MapEntry_Enum* entry;
    struct RSCacheDat2A_ConfigEnum* enum_entry;

    (void)kind;
    if( ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_FIND) )
        return;

    enum_entry = calloc(1, sizeof(*enum_entry));
    if( !enum_entry )
        return;
    enum_entry->id = id;
    enum_entry->default_int = -1;
    RSCacheDat2A_ConfigEnumDecodeInplace(enum_entry, data, data_size);

    dat2_buildcache_prepare_hmap_insert(bc, map);
    entry = (struct MapEntry_Enum*)ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        RSCacheDat2A_ConfigEnumFree(enum_entry);
        return;
    }
    entry->id = id;
    entry->entry = enum_entry;
    dat2_buildcache_maybe_grow_hmap(bc, map);
}

static void
dat2_buildcache_struct_decode_insert(
    struct Dat2BuildCache* bc,
    enum Dat2BuildCache_Kind kind,
    int id,
    const void* data,
    int data_size,
    struct ToriDraw_Map* map)
{
    struct MapEntry_Struct* entry;
    struct RSCacheDat2A_ConfigStruct* struct_entry;

    (void)kind;
    if( ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_FIND) )
        return;

    struct_entry = calloc(1, sizeof(*struct_entry));
    if( !struct_entry )
        return;
    struct_entry->id = id;
    RSCacheDat2A_ConfigStructDecodeInplace(struct_entry, data, data_size);

    dat2_buildcache_prepare_hmap_insert(bc, map);
    entry = (struct MapEntry_Struct*)ToriDraw_MapSearch(map, &id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        RSCacheDat2A_ConfigStructFree(struct_entry);
        return;
    }
    entry->id = id;
    entry->entry = struct_entry;
    dat2_buildcache_maybe_grow_hmap(bc, map);
}

void
dat2_buildcache_params_init_from_archive_ids(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* param_ids,
    int param_count)
{
    dat2_buildcache_config_entries_init_from_archive(
        dat2_buildcache,
        archive,
        RSCacheDat2A_ConfigKind_Params,
        DAT2_BUILDCACHE_KIND_PARAM,
        dat2_buildcache->params_hmap,
        sizeof(struct MapEntry_Param),
        param_ids,
        param_count,
        dat2_buildcache_param_decode_insert);
}

void
dat2_buildcache_params_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive)
{
    dat2_buildcache_params_init_from_archive_ids(dat2_buildcache, archive, NULL, 0);
}

void
dat2_buildcache_enums_init_from_archive_ids(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* enum_ids,
    int enum_count)
{
    dat2_buildcache_config_entries_init_from_archive(
        dat2_buildcache,
        archive,
        RSCacheDat2A_ConfigKind_Enum,
        DAT2_BUILDCACHE_KIND_ENUM,
        dat2_buildcache->enums_hmap,
        sizeof(struct MapEntry_Enum),
        enum_ids,
        enum_count,
        dat2_buildcache_enum_decode_insert);
}

void
dat2_buildcache_enums_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive)
{
    dat2_buildcache_enums_init_from_archive_ids(dat2_buildcache, archive, NULL, 0);
}

void
dat2_buildcache_structs_init_from_archive_ids(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive,
    const int* struct_ids,
    int struct_count)
{
    dat2_buildcache_config_entries_init_from_archive(
        dat2_buildcache,
        archive,
        RSCacheDat2A_ConfigKind_Struct,
        DAT2_BUILDCACHE_KIND_STRUCT,
        dat2_buildcache->structs_hmap,
        sizeof(struct MapEntry_Struct),
        struct_ids,
        struct_count,
        dat2_buildcache_struct_decode_insert);
}

void
dat2_buildcache_structs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCacheDat2Disk_Archive* archive)
{
    dat2_buildcache_structs_init_from_archive_ids(dat2_buildcache, archive, NULL, 0);
}

struct RSCacheDat2A_ConfigParam*
dat2_buildcache_param_get(
    struct Dat2BuildCache* dat2_buildcache,
    int param_id)
{
    struct MapEntry_Param* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Param*)ToriDraw_MapSearch(
        dat2_buildcache->params_hmap, &param_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->param;
}

struct RSCacheDat2A_ConfigEnum*
dat2_buildcache_enum_get(
    struct Dat2BuildCache* dat2_buildcache,
    int enum_id)
{
    struct MapEntry_Enum* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Enum*)ToriDraw_MapSearch(
        dat2_buildcache->enums_hmap, &enum_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->entry;
}

struct RSCacheDat2A_ConfigStruct*
dat2_buildcache_struct_get(
    struct Dat2BuildCache* dat2_buildcache,
    int struct_id)
{
    struct MapEntry_Struct* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Struct*)ToriDraw_MapSearch(
        dat2_buildcache->structs_hmap, &struct_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->entry;
}
