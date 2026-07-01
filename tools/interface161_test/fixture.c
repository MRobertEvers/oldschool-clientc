#include "fixture.h"

#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toridraw_cachemodel.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
interface161_fixture_init(struct Interface161Fixture* fx)
{
    if( fx )
        memset(fx, 0, sizeof(*fx));
}

void
interface161_fixture_free(struct Interface161Fixture* fx)
{
    if( !fx )
        return;
    free(fx->slots);
    fx->slots = NULL;
    fx->slot_count = 0;
}

static int
fixture_append_slot(
    struct Interface161Fixture* fx,
    int file_index,
    int obj_id)
{
    struct Interface161FixtureSlot* next =
        realloc(fx->slots, (size_t)(fx->slot_count + 1) * sizeof(struct Interface161FixtureSlot));
    if( !next )
        return -1;
    fx->slots = next;
    fx->slots[fx->slot_count].file_index = file_index;
    fx->slots[fx->slot_count].obj_id = obj_id;
    fx->slot_count++;
    return 0;
}

static const char*
skip_ws(const char* s)
{
    while( *s && isspace((unsigned char)*s) )
        s++;
    return s;
}

int
interface161_fixture_load(
    struct Interface161Fixture* fx,
    const char* path)
{
    if( !fx || !path )
        return -1;

    interface161_fixture_free(fx);

    FILE* fp = fopen(path, "rb");
    if( !fp )
    {
        fprintf(stderr, "fixture: failed to open %s\n", path);
        return -1;
    }

    if( fseek(fp, 0, SEEK_END) != 0 )
    {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if( sz < 0 )
    {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char* buf = malloc((size_t)sz + 1);
    if( !buf )
    {
        fclose(fp);
        return -1;
    }
    if( fread(buf, 1, (size_t)sz, fp) != (size_t)sz )
    {
        free(buf);
        fclose(fp);
        return -1;
    }
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = strstr(buf, "\"slots\"");
    if( !cur )
        cur = buf;

    while( (cur = strchr(cur, '"')) != NULL )
    {
        cur++;
        if( !isdigit((unsigned char)*cur) )
            continue;

        char* end = NULL;
        long file_index = strtol(cur, &end, 10);
        if( end == cur || file_index < 0 )
            continue;

        const char* obj_key = strstr(end, "\"obj\"");
        if( !obj_key )
            continue;
        obj_key = strchr(obj_key, ':');
        if( !obj_key )
            continue;
        obj_key++;
        obj_key = skip_ws(obj_key);
        long obj_id = strtol(obj_key, &end, 10);
        if( end == obj_key || obj_id < 0 )
            continue;

        if( fixture_append_slot(fx, (int)file_index, (int)obj_id) != 0 )
        {
            free(buf);
            return -1;
        }
        cur = end;
    }

    free(buf);
    return fx->slot_count > 0 ? 0 : -1;
}

int
interface161_fixture_obj_for_file(
    const struct Interface161Fixture* fx,
    int file_index)
{
    if( !fx )
        return -1;
    for( int i = 0; i < fx->slot_count; i++ )
    {
        if( fx->slots[i].file_index == file_index )
            return fx->slots[i].obj_id;
    }
    return -1;
}

void
interface161_obj_icon_cache_free(struct Interface161ObjIconCache* cache)
{
    while( cache )
    {
        struct Interface161ObjIconCache* next = cache->next;
        free(cache->pixels);
        if( cache->model )
            ToriDraw_ModelFree(cache->model);
        free(cache);
        cache = next;
    }
}

static struct RSCacheDat2A_ConfigObject*
load_object_config(
    struct RSCacheDat2Disk* cache,
    int obj_id)
{
    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Configs];
    if( !table || !table->archives )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct RSCacheDat2Disk_ArchiveReference* ref =
        &table->archives[RSCacheDat2A_ConfigKind_Object];

    struct RSCacheDat2A_ConfigObject* out = NULL;
    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != obj_id )
            continue;

        out = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
        if( !out )
            break;

        RSCacheDat2A_ConfigObjectDecodeInplace(out, fl->files[i], fl->file_sizes[i]);
        out->_id = obj_id;
        break;
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return out;
}

static int*
rasterize_obj_icon(
    struct RSCacheDat2Disk* cache,
    struct ToriDraw_Scene* scene,
    struct RSCacheDat2A_ConfigObject* obj,
    struct ToriDraw_Model** out_model)
{
    if( out_model )
        *out_model = NULL;
    if( !obj || obj->inventory_model_id <= 0 )
        return NULL;

    struct RSCacheDat2A_Model* raw =
        RSCacheDat2A_ModelNewFromCache(cache, obj->inventory_model_id);
    if( !raw )
        return NULL;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    RSCacheDat2A_ModelFree(raw);
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

    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromModelRaster(
        scene, hnd, zoom, obj->xan2d, obj->yan2d, 32, 32, true);
    if( !spr || !spr->pixels_argb )
    {
        ToriDraw_ModelFree(td_model);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }

    int* pixels = malloc(32 * 32 * sizeof(int));
    if( !pixels )
    {
        ToriDraw_ModelFree(td_model);
        ToriDraw_SpriteFree(spr);
        return NULL;
    }
    memcpy(pixels, spr->pixels_argb, 32 * 32 * sizeof(int));
    ToriDraw_SpriteFree(spr);
    if( out_model )
        *out_model = td_model;
    else
        ToriDraw_ModelFree(td_model);
    return pixels;
}

const int*
interface161_obj_icon_get(
    struct RSCacheDat2Disk* cache,
    struct ToriDraw_Scene* scene,
    struct Interface161ObjIconCache** cache_head,
    int obj_id)
{
    if( !cache || !scene || !cache_head || obj_id < 0 )
        return NULL;

    for( struct Interface161ObjIconCache* it = *cache_head; it; it = it->next )
    {
        if( it->obj_id == obj_id )
            return it->pixels;
    }

    struct RSCacheDat2A_ConfigObject* obj = load_object_config(cache, obj_id);
    if( !obj )
        return NULL;

    struct ToriDraw_Model* model = NULL;
    int* pixels = rasterize_obj_icon(cache, scene, obj, &model);
    RSCacheDat2A_ConfigObjectFree(obj);
    if( !pixels )
    {
        if( model )
            ToriDraw_ModelFree(model);
        return NULL;
    }

    struct Interface161ObjIconCache* entry = calloc(1, sizeof(struct Interface161ObjIconCache));
    if( !entry )
    {
        free(pixels);
        if( model )
            ToriDraw_ModelFree(model);
        return NULL;
    }
    entry->obj_id = obj_id;
    entry->pixels = pixels;
    entry->model = model;
    entry->next = *cache_head;
    *cache_head = entry;
    return entry->pixels;
}
