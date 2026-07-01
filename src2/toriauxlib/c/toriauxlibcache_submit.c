#include "toriauxlibcache_submit.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_config_idk.h"
#include "osrs/rscache/dat1a/dat1a_config_npc.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_idk.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_skeletalbase.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <stdio.h>

struct SubmitSequenceCtx
{
    struct ToriAuxLibCache* c;
};

static void
submit_sequence_cb(
    int seq_id,
    struct RSCacheDat1A_ConfigSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDatSequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(ctx->c), seq_id, neutral);
}

struct SubmitFlotypeCtx
{
    struct ToriAuxLibCache* c;
};

static struct ToriAuxLibCore_Flotype*
submit_flotype_copy(const struct ToriAuxLibCore_Flotype* src)
{
    struct ToriAuxLibCore_Flotype* copy;
    if( !src )
        return NULL;
    copy = malloc(sizeof(struct ToriAuxLibCore_Flotype));
    if( !copy )
        return NULL;
    *copy = *src;
    return copy;
}

static void
submit_flotype_cb(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    struct ToriAuxLibCore_Flotype* underlay_copy;
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibCache_Core(ctx->c), flo_id, neutral);
    underlay_copy = submit_flotype_copy(neutral);
    if( underlay_copy )
        ToriAuxLibCore_UnderlayAdd(ToriAuxLibCache_Core(ctx->c), flo_id, underlay_copy);
}

struct SubmitLocationCtx
{
    struct ToriAuxLibCache* c;
};

static void
submit_location_cb(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
    void* user_data)
{
    struct SubmitLocationCtx* ctx = user_data;
    struct ToriAuxLibCore_Location* neutral =
        ToriAuxLibCache_LocationNewFromCacheConfigLocation(config_loc);
    if( !neutral )
        return;
    ToriAuxLibCore_LocationAdd(ToriAuxLibCache_Core(ctx->c), loc_id, neutral);
}

void
ToriAuxLibCache_SubmitMapTerrainFromDat1(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapTerrain* terrain = dat1_buildcache_map_terrain_get(dat1(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitMapSceneryFromDat1(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapLocs* locs = dat1_buildcache_map_scenery_get(dat1(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibCache_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitAnimationFromDat1(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct RSCacheDat1A_AnimBaseFrames* abf = dat1_buildcache_animbaseframes_take(dat1(c), anim_id);
    if( !abf )
        return;

    struct ToriAuxLibCore_Animation* anim =
        ToriAuxLibCache_AnimationNewFromCacheDatAnimbaseframes(abf);
    if( !anim )
        return;

    ToriAuxLibCore_AnimationAdd(ToriAuxLibCache_Core(c), anim_id, anim);
}

void
ToriAuxLibCache_SubmitModelFromDat1(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat1_buildcache_model_get(dat1(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibCache_Core(c), model_id, gc_model);
}

static void
submit_recolor_raw(
    struct RSCacheDat2A_Model* model,
    int color_src,
    int color_dst)
{
    if( !model || !model->face_colors )
        return;

    for( int f = 0; f < model->face_count; f++ )
    {
        if( model->face_colors[f] == (uint16_t)color_src )
            model->face_colors[f] = (uint16_t)color_dst;
    }
}

void
ToriAuxLibCache_SubmitIdkModelFromDat1(
    struct ToriAuxLibCache* c,
    int idk_id)
{
    struct RSCacheDat1A_ConfigIdk* idk = dat1_buildcache_idk_get(dat1(c), idk_id);
    if( !idk || !idk->models || idk->models_count <= 0 )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->models_count && model_count < 12; i++ )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), idk->models[i]);
        if( !raw )
            continue;
        raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_IdkModelAdd(ToriAuxLibCache_Core(c), idk_id, gc_model);
}

void
ToriAuxLibCache_SubmitObjModelFromDat1(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(dat1(c), obj_id);
    if( !obj )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;

    if( obj->manwear != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->manwear2 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear2);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->manwear3 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(dat1(c), obj->manwear3);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    for( int i = 0; i < obj->recol_count; i++ )
        submit_recolor_raw(merged, obj->recol_s[i], obj->recol_d[i]);

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_ObjModelAdd(ToriAuxLibCache_Core(c), obj_id, gc_model);
}

void
ToriAuxLibCache_SubmitIdkModelFromDat2(
    struct ToriAuxLibCache* c,
    int idk_id)
{
    struct RSCacheDat2A_ConfigIdk* idk = dat2_buildcache_identkit_get(dat2(c), idk_id);
    if( !idk || !idk->model_ids || idk->model_ids_count <= 0 )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->model_ids_count && model_count < 12; i++ )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), idk->model_ids[i]);
        if( !raw )
            continue;
        raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_IdkModelAdd(ToriAuxLibCache_Core(c), idk_id, gc_model);
}

void
ToriAuxLibCache_SubmitObjModelFromDat2(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(dat2(c), obj_id);
    if( !obj )
        return;

    struct RSCacheDat2A_Model* raw_models[12] = { 0 };
    int model_count = 0;

    if( obj->male_model_0 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_0);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->male_model_1 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_1);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( obj->male_model_2 != -1 )
    {
        struct RSCacheDat2A_Model* raw = dat2_buildcache_model_get(dat2(c), obj->male_model_2);
        if( raw )
            raw_models[model_count++] = raw;
    }
    if( model_count == 0 )
        return;

    struct RSCacheDat2A_Model* merged = RSCacheDat2A_ModelNewMerge(raw_models, model_count);
    if( !merged )
        return;

    for( int i = 0; i < obj->recolor_count; i++ )
        submit_recolor_raw(merged, obj->recolors_from[i], obj->recolors_to[i]);

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(merged);
    RSCacheDat2A_ModelFree(merged);
    if( !gc_model )
        return;

    ToriAuxLibCore_ObjModelAdd(ToriAuxLibCache_Core(c), obj_id, gc_model);
}

static void
toriauxlibcache_ensure_dat2_configs_reference_table(
    struct Dat2BuildCache* dat2_bc,
    struct RSCacheDat2Disk* disk)
{
    struct RSCacheDat2Disk_Archive* table_archive;
    struct RSCacheDat2Disk_ReferenceTable* table;

    if( !dat2_bc || !disk ||
        dat2_buildcache_reference_table_has(dat2_bc, RSCacheDat2Disk_Table_Configs) )
        return;

    table_archive =
        RSCacheDat2Disk_ArchiveNewReferenceTableLoad(disk, RSCacheDat2Disk_Table_Configs);
    if( !table_archive )
        return;

    table = RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCacheDat2Disk_ArchiveFree(table_archive);
    if( !table )
        return;

    dat2_buildcache_reference_table_add(dat2_bc, RSCacheDat2Disk_Table_Configs, table);
}

static bool
toriauxlibcache_ensure_dat2_object_in_buildcache(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    struct Dat2BuildCache* dat2_bc;
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_Archive* archive;

    if( !c || obj_id <= 0 )
        return false;

    dat2_bc = dat2(c);
    if( dat2_buildcache_object_get(dat2_bc, obj_id) )
        return true;

    disk = ToriAuxLibCache_Dat2Disk(c);
    if( !disk || !dat2_bc )
        return false;

    toriauxlibcache_ensure_dat2_configs_reference_table(dat2_bc, disk);

    archive = RSCacheDat2Disk_ArchiveNewLoad(
        disk, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !archive )
        return false;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, archive);
    dat2_buildcache_objects_init_from_archive(dat2_bc, archive, &obj_id, 1);
    RSCacheDat2Disk_ArchiveFree(archive);
    return dat2_buildcache_object_get(dat2_bc, obj_id) != NULL;
}

static bool
toriauxlibcache_ensure_dat2_npctype_in_buildcache(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    struct Dat2BuildCache* dat2_bc;
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_Archive* archive;

    if( !c || npc_id < 0 )
        return false;

    dat2_bc = dat2(c);
    if( dat2_buildcache_npctype_get(dat2_bc, npc_id) )
        return true;

    disk = ToriAuxLibCache_Dat2Disk(c);
    if( !disk || !dat2_bc )
        return false;

    toriauxlibcache_ensure_dat2_configs_reference_table(dat2_bc, disk);

    archive = RSCacheDat2Disk_ArchiveNewLoad(
        disk, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Npc);
    if( !archive )
        return false;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, archive);
    dat2_buildcache_npctypes_init_from_archive(dat2_bc, archive, &npc_id, 1);
    RSCacheDat2Disk_ArchiveFree(archive);
    return dat2_buildcache_npctype_get(dat2_bc, npc_id) != NULL;
}

bool
ToriAuxLibCache_EnsureObjtype(
    struct ToriAuxLibCache* c,
    int obj_id)
{
    if( !c || obj_id <= 0 )
        return false;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    if( ToriAuxLibCore_ObjtypeHas(core, obj_id) )
        return true;

    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        if( !dat2_buildcache_object_get(dat2(c), obj_id) )
            toriauxlibcache_ensure_dat2_object_in_buildcache(c, obj_id);

        struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(dat2(c), obj_id);
        if( !obj )
            return false;

        struct ToriAuxLibCore_Objtype* neutral =
            ToriAuxLibCache_ObjtypeNewFromDat2ConfigObject(obj, obj_id);
        if( !neutral )
            return false;

        ToriAuxLibCore_ObjtypeAdd(core, obj_id, neutral);
        return true;
    }

    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(dat1(c), obj_id);
    if( !obj )
        return false;

    struct ToriAuxLibCore_Objtype* neutral =
        ToriAuxLibCache_ObjtypeNewFromDat1ConfigObj(obj, obj_id);
    if( !neutral )
        return false;

    ToriAuxLibCore_ObjtypeAdd(core, obj_id, neutral);
    return true;
}

bool
ToriAuxLibCache_EnsureNpctype(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    if( !c || npc_id < 0 )
        return false;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    if( ToriAuxLibCore_NpctypeHas(core, npc_id) )
        return true;

    if( ToriAuxLibCache_Mode(c) == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        if( !dat2_buildcache_npctype_get(dat2(c), npc_id) )
            toriauxlibcache_ensure_dat2_npctype_in_buildcache(c, npc_id);

        struct RSCacheDat2A_ConfigNpctype* npc = dat2_buildcache_npctype_get(dat2(c), npc_id);
        if( !npc )
            return false;

        struct ToriAuxLibCore_Npctype* neutral =
            ToriAuxLibCache_NpctypeNewFromDat2ConfigNpctype(npc, npc_id);
        if( !neutral )
            return false;

        ToriAuxLibCore_NpctypeAdd(core, npc_id, neutral);
        return true;
    }

    struct Dat1BuildCache* dat1_bc = dat1(c);
    struct RSCacheDat1A_ConfigNpc* npc = dat1_buildcache_npc_get(dat1_bc, npc_id);
    if( !npc )
        npc = dat1_buildcache_npc_load_from_config_jagfile(dat1_bc, npc_id);
    if( !npc )
        return false;

    struct ToriAuxLibCore_Npctype* neutral =
        ToriAuxLibCache_NpctypeNewFromDat1ConfigNpc(npc, npc_id);
    if( !neutral )
        return false;

    ToriAuxLibCore_NpctypeAdd(core, npc_id, neutral);
    return true;
}

void
ToriAuxLibCache_SubmitTexture(
    struct ToriAuxLibCache* c,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture)
{
    ToriAuxLibCore_TextureAdd(ToriAuxLibCache_Core(c), texture_id, texture);
}

void
ToriAuxLibCache_SubmitAllSequencesFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat1_buildcache_foreach_sequence(dat1(c), submit_sequence_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllFlotypesFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat1_buildcache_foreach_flotype(dat1(c), submit_flotype_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllLocationsFromDat1(struct ToriAuxLibCache* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat1_buildcache_foreach_config_loc(dat1(c), submit_location_cb, &ctx);
}

void
ToriAuxLibCache_SubmitSprite(
    struct ToriAuxLibCache* c,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite)
{
    if( !c || !sprite )
        return;
    ToriAuxLibCore_SpriteAdd(ToriAuxLibCache_Core(c), sprite_id, sprite);
}

void
ToriAuxLibCache_SubmitFont(
    struct ToriAuxLibCache* c,
    int font_id,
    struct ToriAuxLibCore_Font* font)
{
    if( !c || !font )
        return;
    ToriAuxLibCore_FontAdd(ToriAuxLibCache_Core(c), font_id, font);
}

void
ToriAuxLibCache_SubmitComponent(
    struct ToriAuxLibCache* c,
    int component_id,
    struct ToriAuxLibCore_Component* component)
{
    if( !c || !component )
        return;
    ToriAuxLibCore_ComponentAdd(ToriAuxLibCache_Core(c), component_id, component);
}

void
ToriAuxLibCache_SubmitComponentsFromDat2(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache_InterfaceArchive* archive)
{
    if( !c || !archive || !archive->components )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    for( int i = 0; i < archive->component_count; i++ )
    {
        Component* comp = archive->components[i];
        if( !comp )
            continue;

        struct ToriAuxLibCore_Component* neutral =
            ToriAuxLibCache_ComponentNewFromCacheDat2Component(comp);
        if( !neutral )
            continue;

        int component_id = comp->id >= 0 ? comp->id : i;
        ToriAuxLibCore_ComponentAdd(core, component_id, neutral);
    }
}

void
ToriAuxLibCache_SubmitComponentsFromDat1(
    struct ToriAuxLibCache* c,
    const bool* needed,
    int needed_count)
{
    if( !c )
        return;

    struct RSCacheDat1A_ConfigComponentList* interfaces = dat1_buildcache_get_interfaces(dat1(c));
    if( !interfaces || !interfaces->components )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    for( int i = 0; i < interfaces->components_count; i++ )
    {
        if( needed )
        {
            if( i >= needed_count || !needed[i] )
                continue;
        }

        struct RSCacheDat1A_ConfigComponent* comp = interfaces->components[i];
        if( !comp )
            continue;

        struct ToriAuxLibCore_Component* neutral =
            ToriAuxLibCache_ComponentNewFromCacheComponent(comp);
        if( !neutral )
            continue;

        int component_id = comp->id >= 0 ? comp->id : i;
        ToriAuxLibCore_ComponentAdd(core, component_id, neutral);
    }
}

void
ToriAuxLibCache_SubmitAllComponentsFromDat1(struct ToriAuxLibCache* c)
{
    ToriAuxLibCache_SubmitComponentsFromDat1(c, NULL, 0);
}

static void
submit_dat2_sequence_cb(
    int seq_id,
    struct RSCacheDat2A_ConfigSequence* sequence,
    void* user_data)
{
    struct SubmitSequenceCtx* ctx = user_data;
    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(sequence, seq_id);
    if( !neutral )
        return;
    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(ctx->c), seq_id, neutral);
}

static void
submit_dat2_flotype_cb(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(flotype, flo_id);
    if( !neutral )
        return;
    ToriAuxLibCore_FlotypeAdd(ToriAuxLibCache_Core(ctx->c), flo_id, neutral);
}

static void
submit_dat2_underlay_cb(
    int underlay_id,
    struct RSCacheDat2A_ConfigUnderlay* underlay,
    void* user_data)
{
    struct SubmitFlotypeCtx* ctx = user_data;
    struct ToriAuxLibCore_Flotype* neutral =
        ToriAuxLibCache_UnderlayNewFromCacheConfigUnderlay(underlay, underlay_id);
    if( !neutral )
        return;
    ToriAuxLibCore_UnderlayAdd(ToriAuxLibCache_Core(ctx->c), underlay_id, neutral);
}

void
ToriAuxLibCache_SubmitMapTerrainFromDat2(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapTerrain* terrain = dat2_buildcache_map_terrain_get(dat2(c), map_id);
    if( !terrain )
        return;

    struct ToriAuxLibCore_MapTerrain* neutral =
        ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(terrain);
    if( !neutral )
        return;

    ToriAuxLibCore_MapTerrainAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitMapSceneryFromDat2(
    struct ToriAuxLibCache* c,
    int map_id)
{
    struct RSCacheDat2A_MapLocs* locs = dat2_buildcache_map_scenery_get(dat2(c), map_id);
    if( !locs )
        return;

    struct ToriAuxLibCore_MapLocs* neutral = ToriAuxLibCache_MapLocsNewFromCacheMapLocs(locs);
    if( !neutral )
        return;

    ToriAuxLibCore_MapSceneryAdd(ToriAuxLibCache_Core(c), map_id, neutral);
}

void
ToriAuxLibCache_SubmitModelFromDat2(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct RSCacheDat2A_Model* model = dat2_buildcache_model_get(dat2(c), model_id);
    if( !model )
        return;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(model);
    if( !copy )
        return;

    struct ToriAuxLibCore_Model* gc_model = ToriAuxLibCache_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !gc_model )
        return;

    ToriAuxLibCore_ModelAdd(ToriAuxLibCache_Core(c), model_id, gc_model);
}

void
ToriAuxLibCache_SubmitAllSequencesFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitSequenceCtx ctx = { .c = c };
    dat2_buildcache_foreach_sequence(dat2(c), submit_dat2_sequence_cb, &ctx);
}

void
ToriAuxLibCache_SubmitSequenceFromDat2(
    struct ToriAuxLibCache* c,
    int seq_id)
{
    struct RSCacheDat2A_ConfigSequence* sequence = dat2_buildcache_sequence_get(dat2(c), seq_id);
    if( !sequence )
        return;

    struct ToriAuxLibCore_Sequence* neutral =
        ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(sequence, seq_id);
    if( !neutral )
        return;

    ToriAuxLibCore_SequenceAdd(ToriAuxLibCache_Core(c), seq_id, neutral);
}

void
ToriAuxLibCache_SubmitAllFlotypesFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_flotype(dat2(c), submit_dat2_flotype_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllUnderlaysFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitFlotypeCtx ctx = { .c = c };
    dat2_buildcache_foreach_underlay(dat2(c), submit_dat2_underlay_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAllLocationsFromDat2(struct ToriAuxLibCache* c)
{
    struct SubmitLocationCtx ctx = { .c = c };
    dat2_buildcache_foreach_config_loc(dat2(c), submit_location_cb, &ctx);
}

void
ToriAuxLibCache_SubmitAnimationFromDat2(
    struct ToriAuxLibCache* c,
    int archive_id)
{
    struct Dat2BuildCache_FramesArchive* fa = dat2_buildcache_frames_take(dat2(c), archive_id);
    assert(fa);

    struct ToriAuxLibCore_Animation* anim =
        ToriAuxLibCache_AnimationNewFromDat2FramesArchive(fa, archive_id);
    Dat2BuildCache_FramesArchiveFree(fa);

    assert(anim);

    ToriAuxLibCore_AnimationAdd(ToriAuxLibCache_Core(c), archive_id, anim);
}

void
ToriAuxLibCache_SubmitSkeletalFromDat2(
    struct ToriAuxLibCache* c,
    int anim_maya_id)
{
    struct RSCacheDat2A_AnimMaya* maya = dat2_buildcache_skeletal_take(dat2(c), anim_maya_id);
    assert(maya);

    /* Bind pose must have been preloaded during Task_Dat2AnimResolve skeletal phase. */
    struct RSCacheDat2A_SkeletalBase* skelbase =
        dat2_buildcache_skeletal_base_get(dat2(c), maya->base_id);

    if( !skelbase )
    {
        fprintf(
            stderr,
            "ToriAuxLibCache_SubmitSkeletalFromDat2: SkeletalBase load failed "
            "(anim_maya_id=%d base_id=%d)\n",
            anim_maya_id,
            maya->base_id);
        RSCacheDat2A_AnimMayaFree(maya);
        return;
    }

    /* Bake per-frame per-bone skinning matrix palette */
    int frame_count = 0, bone_count = 0;
    float* palette =
        RSCacheDat2A_SkeletalBaseBakePalette(maya, skelbase, &frame_count, &bone_count);

    RSCacheDat2A_AnimMayaFree(maya);

    if( !palette )
    {
        fprintf(
            stderr,
            "ToriAuxLibCache_SubmitSkeletalFromDat2: BakePalette failed "
            "(anim_maya_id=%d)\n",
            anim_maya_id);
        return;
    }

    struct ToriAuxLibCore_SkeletalAnim* skeletal = ToriAuxLibCache_SkeletalAnimNewFromBakedPalette(
        anim_maya_id, palette, frame_count, bone_count);

    if( !skeletal )
    {
        fprintf(
            stderr,
            "ToriAuxLibCache_SubmitSkeletalFromDat2: SkeletalAnimNew failed "
            "(anim_maya_id=%d frame_count=%d bone_count=%d)\n",
            anim_maya_id,
            frame_count,
            bone_count);
        return;
    }

    ToriAuxLibCore_SkeletalAnimAdd(ToriAuxLibCache_Core(c), anim_maya_id, skeletal);
}

void
ToriAuxLibCache_SubmitClientScript(
    struct ToriAuxLibCache* c,
    int script_id,
    struct ToriAuxLibCore_ClientScript* script)
{
    if( !c || !script )
        return;
    ToriAuxLibCore_ClientScriptAdd(ToriAuxLibCache_Core(c), script_id, script);
}

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptResolve(
    struct ToriAuxLibCache* c,
    int script_id)
{
    if( !c || script_id < 0 )
        return NULL;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);
    struct ToriAuxLibCore_ClientScript* existing = ToriAuxLibCore_ClientScriptGet(core, script_id);
    if( existing )
        return existing;

    if( ToriAuxLibCache_Mode(c) != TORIAUXLIBCACHE_MODE_DAT2 )
        return NULL;

    struct RSCacheDat2Disk* disk = ToriAuxLibCache_Dat2Disk(c);
    if( !disk )
        return NULL;

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
        return NULL;

    struct ToriAuxLibCore_ClientScript* script =
        ToriAuxLibCache_ClientScriptNewFromDat2Archive(disk, archive, script_id);
    if( script )
        ToriAuxLibCache_SubmitClientScript(c, script_id, script);
    return script;
}
