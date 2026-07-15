#include "dat1_buildcache_ui.h"

#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toriauxlib/cache/toriauxlibcache_font_convert.h"
#include "toriauxlib/cache/toriauxlibcache_sprite_convert.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCore_Sprite*
dat1_buildcache_sprite_decode(
    struct Dat1BuildCache* buildcache,
    struct RevConfigCacheItem const* item)
{
    assert(buildcache);
    assert(item);

    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    assert(filelist);
    return ToriAuxLibCache_SpriteNewFromDat1RevConfigItem(filelist, item);
}

struct ToriAuxLibCore_Sprite*
dat1_buildcache_sprite_decode_ref(
    struct Dat1BuildCache* buildcache,
    char const* sprite_ref)
{
    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    assert(filelist);
    return ToriAuxLibCache_SpriteNewFromDat1Ref(filelist, sprite_ref);
}

struct ToriAuxLibCore_Font*
dat1_buildcache_font_decode(
    struct Dat1BuildCache* buildcache,
    char const* font_name)
{
    assert(buildcache);
    assert(font_name);
    assert(font_name[0]);

    struct RSCacheShared_FileListDat* filelist = buildcache->title_fonts_jagfile;
    if( !filelist )
        return NULL;

    return ToriAuxLibCache_FontNewFromDat1Jagfile(filelist, font_name);
}

static struct RSCacheDat1A_ConfigObj*
obj_icon_resolve_obj(
    struct Dat1BuildCache* buildcache,
    int obj_id,
    int count)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(buildcache, obj_id);
    if( !obj )
        return NULL;

    if( obj->countobj && obj->countco && count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < obj->countobj_count && i < 10; i++ )
        {
            if( count >= obj->countco[i] && obj->countco[i] != 0 )
                countobj_id = obj->countobj[i];
        }
        if( countobj_id >= 0 )
            return obj_icon_resolve_obj(buildcache, countobj_id, 1);
    }

    return obj;
}

struct ToriAuxLibCore_Sprite*
dat1_buildcache_widget_model_sprite(
    struct Dat1BuildCache* buildcache,
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

    struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(buildcache, model_id);
    if( !raw )
    {
        fprintf(
            stderr,
            "dat1_buildcache_widget_model_sprite: model not in cache model_id=%d\n",
            model_id);
        assert(raw && "dat1_buildcache_widget_model_sprite: model not in cache");
        return NULL;
    }

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    if( !copy )
    {
        assert(copy && "dat1_buildcache_widget_model_sprite: model copy failed");
        return NULL;
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
    {
        assert(td_model && "dat1_buildcache_widget_model_sprite: ToriDraw model build failed");
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
    struct ToriDraw_Sprite* td_sprite =
        ToriDraw_SpriteNewFromModelRaster(scene, hnd, zoom, xan, yan, width, height, true);

    ToriDraw_ModelFree(td_model);

    if( !td_sprite )
    {
        fprintf(
            stderr,
            "dat1_buildcache_widget_model_sprite: raster failed model_id=%d %dx%d\n",
            model_id,
            width,
            height);
        assert(td_sprite && "dat1_buildcache_widget_model_sprite: raster failed");
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
dat1_buildcache_obj_icon_sprite(
    struct Dat1BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int obj_id,
    int count)
{
    assert(buildcache);
    assert(scene);
    assert(obj_id >= 0);

    struct RSCacheDat1A_ConfigObj* obj = obj_icon_resolve_obj(buildcache, obj_id, count);
    assert(obj);
    assert(obj->model > 0);

    struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(buildcache, obj->model);
    assert(raw);

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    assert(copy);

    if( copy->face_colors )
    {
        for( int i = 0; i < obj->recol_count; i++ )
        {
            int color_src = obj->recol_s[i];
            int color_dst = obj->recol_d[i];
            for( int f = 0; f < copy->face_count; f++ )
            {
                if( copy->face_colors[f] == (uint16_t)color_src )
                    copy->face_colors[f] = (uint16_t)color_dst;
            }
        }
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    assert(td_model);

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, obj->contrast, obj->ambient);

    int zoom = obj->zoom2d;
    if( zoom == 0 )
        zoom = 2000;

    int sin_pitch = (ToriDraw_Sin(obj->xan2d) * zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(obj->xan2d) * zoom) >> 16;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = 32;
    view_port.height = 32;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = 32;
    view_port.clip_bottom = 32;
    view_port.x_center = 16;
    view_port.y_center = 16;
    view_port.stride = 32;

    struct ToriDraw_Camera camera = { 0 };
    camera.pitch = obj->xan2d;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_min_y = bounds ? -bounds->min_y : 0;

    struct ToriDraw_Position position = { 0 };
    position.pitch = 0;
    position.yaw = obj->yan2d;
    position.roll = obj->zan2d;
    position.x = obj->xof2d;
    position.y = sin_pitch + (model_min_y / 2) + obj->yof2d;
    position.z = cos_pitch + obj->yof2d;

    toripixel_t* pixels = calloc(32u * 32u, sizeof(toripixel_t));
    assert(pixels);

    ToriDraw_RenderModel(hnd, scene, &position, &view_port, &camera, pixels);

    uint32_t* argb = malloc(32u * 32u * sizeof(uint32_t));
    assert(argb);

    for( int i = 0; i < 32 * 32; i++ )
        argb[i] = (uint32_t)pixels[i];

    free(pixels);
    ToriDraw_ModelFree(td_model);

    ToriDraw_SpritePostprocessObjIconOutline(argb, 32, 32);

    struct ToriAuxLibCore_Sprite* sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !sprite )
    {
        free(argb);
        return NULL;
    }

    sprite->frames = calloc(1, sizeof(struct ToriAuxLibCore_SpriteFrame));
    if( !sprite->frames )
    {
        free(argb);
        free(sprite);
        return NULL;
    }

    sprite->frame_count = 1;
    sprite->frames[0].pixels_argb = argb;
    sprite->frames[0].width = 32;
    sprite->frames[0].height = 32;
    sprite->frames[0].crop_width = 32;
    sprite->frames[0].crop_height = 32;
    snprintf(sprite->name, sizeof(sprite->name), "obj:%d", obj_id);
    return sprite;
}
