#include "dat2_buildcache_ui.h"

#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "toriauxlib/cache/toriauxlibcache_font_convert.h"
#include "toriauxlib/cache/toriauxlibcache_sprite_convert.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_sprite.h"
#include "toridraw/toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct RSCacheDat2A_ConfigObject*
obj_icon_resolve_obj(
    struct Dat2BuildCache* buildcache,
    int obj_id,
    int count)
{
    struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(buildcache, obj_id);
    if( !obj )
        return NULL;

    if( count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < 10; i++ )
        {
            if( count >= obj->count_co[i] && obj->count_co[i] != 0 )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id >= 0 )
            return obj_icon_resolve_obj(buildcache, countobj_id, 1);
    }

    return obj;
}

struct ToriAuxLibCore_Sprite*
dat2_buildcache_widget_model_sprite(
    struct Dat2BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int model_id,
    int zoom,
    int xan,
    int yan,
    int width,
    int height)
{
    assert(buildcache && scene);
    assert(model_id > 0);
    assert(width > 0 && height > 0);

    struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(buildcache, model_id);
    if( !raw )
    {
        fprintf(
            stderr,
            "dat2_buildcache_widget_model_sprite: model not in cache model_id=%d\n",
            model_id);
        assert(raw && "dat2_buildcache_widget_model_sprite: model not in cache");
        return NULL;
    }

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    if( !copy )
    {
        assert(copy && "dat2_buildcache_widget_model_sprite: model copy failed");
        return NULL;
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
    {
        assert(td_model && "dat2_buildcache_widget_model_sprite: ToriDraw model build failed");
        return NULL;
    }

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, 0, 0);

    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "widget_model:%d", model_id);
    struct ToriDraw_Sprite* td_sprite = ToriDraw_SpriteNewFromModelRaster(
        scene, hnd, zoom, xan, yan, width, height, true);

    ToriDraw_ModelFree(td_model);

    if( !td_sprite )
    {
        fprintf(
            stderr,
            "dat2_buildcache_widget_model_sprite: raster failed model_id=%d %dx%d\n",
            model_id,
            width,
            height);
        assert(td_sprite && "dat2_buildcache_widget_model_sprite: raster failed");
        return NULL;
    }

    struct ToriAuxLibCore_Sprite* sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !sprite )
    {
        ToriDraw_SpriteFree(td_sprite);
        return NULL;
    }

    sprite->frame_count = 1;
    sprite->frames = ToriAuxLibCache_SpriteFrameNewFromToriDrawByMove(td_sprite);
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }

    strncpy(sprite->name, name_buf, sizeof(sprite->name) - 1);
    return sprite;
}

struct ToriAuxLibCore_Sprite*
dat2_buildcache_obj_icon_sprite(
    struct Dat2BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int obj_id,
    int count)
{
    if( !buildcache || !scene || obj_id < 0 )
        return NULL;

    struct RSCacheDat2A_ConfigObject* obj = obj_icon_resolve_obj(buildcache, obj_id, count);
    if( !obj || obj->inventory_model_id <= 0 )
        return NULL;

    struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(buildcache, obj->inventory_model_id);
    if( !raw )
        return NULL;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    if( !copy )
        return NULL;

    if( copy->face_colors && obj->recolor_count > 0 )
    {
        for( int i = 0; i < obj->recolor_count; i++ )
        {
            int color_src = obj->recolors_from[i];
            int color_dst = obj->recolors_to[i];
            for( int f = 0; f < copy->face_count; f++ )
            {
                if( copy->face_colors[f] == (uint16_t)color_src )
                    copy->face_colors[f] = (uint16_t)color_dst;
            }
        }
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
        return NULL;

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, obj->contrast, obj->ambient);

    int zoom = obj->zoom2d;
    if( zoom == 0 )
        zoom = 2000;

    struct ToriDraw_Sprite* td_sprite = ToriDraw_SpriteNewFromModelRaster(
        scene, hnd, zoom, obj->xan2d, obj->yan2d, 32, 32, true);
    ToriDraw_ModelFree(td_model);
    if( !td_sprite )
        return NULL;

    struct ToriAuxLibCore_Sprite* sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !sprite )
    {
        ToriDraw_SpriteFree(td_sprite);
        return NULL;
    }

    sprite->frame_count = 1;
    sprite->frames = ToriAuxLibCache_SpriteFrameNewFromToriDrawByMove(td_sprite);
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }

    if( sprite->frames[0].pixels_argb )
    {
        size_t pixel_count = (size_t)sprite->frames[0].width * (size_t)sprite->frames[0].height;
        for( size_t i = 0; i < pixel_count; i++ )
        {
            uint32_t rgb = sprite->frames[0].pixels_argb[i];
            sprite->frames[0].pixels_argb[i] = (rgb & 0xFFFFFFu) ? (rgb | 0xFF000000u) : 0u;
        }
    }

    snprintf(sprite->name, sizeof(sprite->name), "obj:%d", obj_id);
    return sprite;
}

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item)
{
    return ToriAuxLibCache_SpriteNewFromDat2Archive(archive, item);
}

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_id_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id)
{
    return ToriAuxLibCache_SpriteNewFromDat2ArchiveId(archive, sprite_id);
}

struct ToriAuxLibCore_Font*
dat2_buildcache_font_decode_from_archive(
    struct RSCacheDat2Disk_Archive* font_archive,
    struct RSCacheDat2Disk_Archive* sprite_archive,
    int font_id)
{
    return ToriAuxLibCache_FontNewFromDat2Archives(font_archive, sprite_archive, font_id);
}

static RSCacheDat2A_Component*
component_decode_from_bytes(
    int packed_id,
    char* data,
    int size)
{
    if( !data || size <= 0 )
        return NULL;

    RSCacheDat2A_Component* comp = calloc(1, sizeof(RSCacheDat2A_Component));
    if( !comp )
        return NULL;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(comp, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(comp, &buf);
    return comp;
}

RSCacheDat2A_Component*
dat2_buildcache_component_decode_iface_file_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id,
    int file_index,
    int* out_size)
{
    if( !cache || !archive )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !fl || file_index < 0 || file_index >= fl->file_count )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    char* data = fl->files[file_index];
    int size = fl->file_sizes[file_index];
    int packed_id = (iface_id << 16) | (file_index & 0xFFFF);
    RSCacheDat2A_Component* comp = component_decode_from_bytes(packed_id, data, size);

    if( out_size )
        *out_size = size;

    RSCacheShared_FileListFree(fl);
    return comp;
}

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_component_decode_iface_archive_from_archive(
    struct RSCacheDat2Disk_ReferenceTable* reference_table,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id)
{
    if( !reference_table || !archive )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    struct Dat2BuildCache_InterfaceArchive* iface_archive =
        calloc(1, sizeof(struct Dat2BuildCache_InterfaceArchive));
    if( !iface_archive )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    iface_archive->component_count = fl->file_count;
    iface_archive->components = calloc((size_t)fl->file_count, sizeof(RSCacheDat2A_Component*));
    if( !iface_archive->components )
    {
        free(iface_archive);
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    for( int i = 0; i < fl->file_count; i++ )
    {
        int packed_id = (iface_id << 16) | (i & 0xFFFF);
        iface_archive->components[i] =
            component_decode_from_bytes(packed_id, fl->files[i], fl->file_sizes[i]);
    }

    RSCacheShared_FileListFree(fl);
    return iface_archive;
}
