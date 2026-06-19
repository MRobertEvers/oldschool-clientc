#include "dat1_interface.h"

#include "../buildcache/dat1_buildcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "../ui/ui_scene.h"
#include "dat1_sprite.h"
#include "graphics/dashmap.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static struct CacheDatConfigComponent*
get_component(
    struct CacheDatConfigComponentList* ifaces,
    int component_id)
{
    if( !ifaces || component_id < 0 || component_id >= ifaces->components_count )
        return NULL;
    return ifaces->components[component_id];
}

static void
iface_add_archive_request(
    struct Dat1_Interface* iface,
    int table_id,
    int archive_id,
    int flags)
{
    /* De-dup within the element's waiting list. */
    for( int i = 0; i < iface->waiting_count; i++ )
    {
        if( iface->waiting[i].table_id == table_id && iface->waiting[i].archive_id == archive_id &&
            iface->waiting[i].flags == flags )
            return;
    }
    if( iface->waiting_count >= DAT1_INTERFACE_MAX_REQUESTS )
        return;
    struct Dat1_ArchiveRequest* req = &iface->waiting[iface->waiting_count++];
    req->table_id = table_id;
    req->archive_id = archive_id;
    req->flags = flags;
    req->resolved = false;
}

static void
iface_add_model_request(
    struct Dat1_Interface* iface,
    int model_id)
{
    /* De-dup model_ids. */
    for( int i = 0; i < iface->model_count; i++ )
        if( iface->model_ids[i] == model_id )
            return;
    if( iface->model_count >= DAT1_INTERFACE_MAX_MODELS )
        return;
    iface->model_ids[iface->model_count++] = model_id;
    iface_add_archive_request(iface, CACHE_DAT_MODELS, model_id, 0);
}

/* Preload one media sprite ref into UIScene and record it in sprite_map. */
static void
iface_preload_sprite(
    struct Dat1BuildCache* buildcache,
    struct UIScene* scene,
    struct DashMap* sprite_map,
    const char* sprite_ref)
{
    if( !sprite_ref || !sprite_ref[0] )
        return;
    /* Already preloaded? */
    if( dashmap_search(sprite_map, sprite_ref, DASHMAP_FIND) )
        return;

    struct ToriDraw_Sprite* sprite = dat1_sprite_decode_media_ref(buildcache, sprite_ref);
    if( !sprite )
        return;

    struct ToriDraw_Sprite** row = malloc(sizeof(struct ToriDraw_Sprite*));
    if( !row )
    {
        ToriDraw_SpriteFree(sprite);
        return;
    }
    row[0] = sprite;

    int scene_id = ui_scene_element_acquire_with_sprites(scene, row, 1, false, sprite_ref);
    if( scene_id < 0 )
    {
        ToriDraw_SpriteFree(sprite);
        free(row);
        return;
    }
    /* ownership of row transferred to UIScene */

    struct Dat1_SpriteMapEntry* entry = dashmap_search(sprite_map, sprite_ref, DASHMAP_INSERT);
    if( entry )
    {
        strncpy(entry->name, sprite_ref, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->scene_id = scene_id;
        entry->atlas_index = 0;
    }
}

/* Recursively walk component tree: collect model/font deps and
 * preload graphic sprites. */
static void
iface_collect_and_preload(
    struct Dat1_Interface* iface,
    struct CacheDatConfigComponentList* ifaces,
    struct Dat1BuildCache* buildcache,
    struct UIScene* scene,
    struct DashMap* sprite_map,
    int component_id)
{
    struct CacheDatConfigComponent* comp = get_component(ifaces, component_id);
    if( !comp )
        return;

    switch( comp->type )
    {
    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
        iface->need_fonts = true;
        break;

    case COMPONENT_TYPE_GRAPHIC:
        iface_preload_sprite(buildcache, scene, sprite_map, comp->graphic);
        iface_preload_sprite(buildcache, scene, sprite_map, comp->activeGraphic);
        break;

    case COMPONENT_TYPE_MODEL:
        if( comp->modelType == 1 && comp->model > 0 )
            iface_add_model_request(iface, comp->model);
        break;

    case COMPONENT_TYPE_INV:
        if( comp->invSlotGraphic )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                const char* gname = comp->invSlotGraphic[si];
                if( gname && gname[0] )
                    iface_preload_sprite(buildcache, scene, sprite_map, gname);
            }
        }
        break;

    case COMPONENT_TYPE_LAYER:
        if( comp->children )
        {
            for( int i = 0; i < comp->children_count; i++ )
                iface_collect_and_preload(
                    iface, ifaces, buildcache, scene, sprite_map, comp->children[i]);
        }
        break;

    default:
        break;
    }
}

static bool
iface_all_resolved(const struct Dat1_Interface* iface)
{
    for( int i = 0; i < iface->waiting_count; i++ )
        if( !iface->waiting[i].resolved )
            return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
dat1_interface_init(
    struct Dat1_Interface* iface,
    int component_id)
{
    if( !iface )
        return;
    memset(iface, 0, sizeof(*iface));
    iface->state = DAT1_STATE_NEED_ARCHIVE;
    iface->component_id = component_id;
}

void
dat1_interface_step(
    struct Dat1_Interface* iface,
    struct Dat1BuildCache* buildcache,
    struct ToriAuxLibCore* core,
    struct UIScene* scene,
    struct DashMap* sprite_map)
{
    if( !iface || !buildcache || !core || !scene || !sprite_map )
        return;

reswitch:
    switch( iface->state )
    {
    case DAT1_STATE_NEED_ARCHIVE:
    {
        bool has_ifaces = (dat1_buildcache_get_interfaces(buildcache) != NULL);
        bool has_media = (dat1_buildcache_get_media_2d_graphics_jagfile(buildcache) != NULL);

        if( !has_ifaces )
            iface_add_archive_request(iface, CACHE_DAT_CONFIGS, CONFIG_DAT_INTERFACES, 0);
        if( !has_media )
            iface_add_archive_request(iface, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS, 0);

        if( has_ifaces && has_media )
        {
            iface->state = DAT1_STATE_INSPECT;
            goto reswitch;
        }
        iface->state = DAT1_STATE_WAIT;
        return;
    }

    case DAT1_STATE_WAIT:
        if( !iface_all_resolved(iface) )
            return;
        iface->state = DAT1_STATE_INSPECT;
        goto reswitch;

    case DAT1_STATE_INSPECT:
    {
        struct CacheDatConfigComponentList* ifaces = dat1_buildcache_get_interfaces(buildcache);
        if( !ifaces )
        {
            iface->state = DAT1_STATE_FAILED;
            return;
        }

        int prev_waiting = iface->waiting_count;

        iface_collect_and_preload(
            iface, ifaces, buildcache, scene, sprite_map, iface->component_id);

        if( iface->need_fonts )
            iface_add_archive_request(iface, CACHE_DAT_CONFIGS, CONFIG_DAT_TITLE_AND_FONTS, 0);

        if( iface->waiting_count > prev_waiting )
        {
            /* New requests declared; wait for them. */
            iface->state = DAT1_STATE_WAIT_DEPS;
            return;
        }
        iface->state = DAT1_STATE_CONVERT;
        goto reswitch;
    }

    case DAT1_STATE_WAIT_DEPS:
        if( !iface_all_resolved(iface) )
            return;
        iface->state = DAT1_STATE_CONVERT;
        goto reswitch;

    case DAT1_STATE_CONVERT:
    {
        for( int i = 0; i < iface->model_count; i++ )
        {
            int model_id = iface->model_ids[i];

            struct ToriDraw_ModelHandle existing = ToriAuxLibCore_ModelGet(core, model_id);
            if( existing.kind == TORIDRAWMK_MODEL )
                continue;

            struct CacheModel* raw = dat1_buildcache_model_get(buildcache, model_id);
            if( !raw )
            {
                printf("dat1_interface: model %d missing from buildcache\n", model_id);
                continue;
            }

            struct CacheModel* copy = model_new_copy(raw);
            if( !copy )
                continue;
            struct ToriDraw_Model* td = ToriDraw_ModelNewFromCacheModel(copy);
            model_free(copy);
            if( !td )
                continue;

            struct ToriDraw_ModelHandle hnd = { 0 };
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = td;
            ToriAuxLibCore_ModelAdd(core, model_id, hnd);
            printf("dat1_interface: converted model %d to gamecache\n", model_id);
        }

        printf("dat1_interface: component %d done\n", iface->component_id);
        iface->state = DAT1_STATE_DONE;
        return;
    }

    case DAT1_STATE_DONE:
    case DAT1_STATE_FAILED:
        return;

    default:
        return;
    }
}
