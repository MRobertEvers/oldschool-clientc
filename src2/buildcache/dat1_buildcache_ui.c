#include "dat1_buildcache_ui.h"

#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toriauxlib/td/toridraw_cachesprite.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toridraw/graphics/shared_tables.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_model_transform.h"
#include "toridraw/toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
obj_icon_postprocess_outline(uint32_t* pixels, int w, int h)
{
    for( int x = w - 1; x >= 0; x-- )
    {
        for( int y = h - 1; y >= 0; y-- )
        {
            int idx = x + y * w;
            if( pixels[idx] != 0 )
                continue;

            if( (x > 0 && pixels[idx - 1] > 1) || (y > 0 && pixels[idx - w] > 1) ||
                (x < w - 1 && pixels[idx + 1] > 1) || (y < h - 1 && pixels[idx + w] > 1) )
                pixels[idx] = 1;
        }
    }

    for( int x = w - 1; x >= 0; x-- )
    {
        for( int y = h - 1; y >= 0; y-- )
        {
            int idx = x + y * w;
            if( pixels[idx] == 0 && x > 0 && y > 0 && pixels[idx - 1 - w] > 0 )
                pixels[idx] = 1;
        }
    }
}

static struct ToriDraw_Sprite*
sprite_decode_from_filelist(
    struct RSCacheShared_FileListDat* filelist,
    struct RevConfigCacheItem const* item,
    int atlas_index)
{
    if( !filelist || !item )
        return NULL;

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, item->index_filename);
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, item->data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    if( strcmp(item->format, "pix8") == 0 )
    {
        struct RSCacheDat1A_Pix8Palette* pix8 = RSCacheDat1A_Pix8PaletteNew(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix8 )
            return NULL;
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8);
        if( sprite )
        {
            if( item->crop_width > 0 && item->crop_height > 0 )
            {
                sprite->crop_x = item->crop_x;
                sprite->crop_y = item->crop_y;
                sprite->crop_width = item->crop_width;
                sprite->crop_height = item->crop_height;
            }
            else
            {
                sprite->crop_width = sprite->width;
                sprite->crop_height = sprite->height;
            }
        }
        RSCacheDat1A_Pix8PaletteFree(pix8);
        return sprite;
    }

    if( strcmp(item->format, "pix32") == 0 )
    {
        struct RSCacheDat1A_Pix32* pix32 = RSCacheDat1A_Pix32New(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix32 )
            return NULL;
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        RSCacheDat1A_Pix32Free(pix32);
        return sprite;
    }

    return NULL;
}

static void
sprite_apply_transforms(
    struct ToriDraw_Sprite* sprite,
    struct RevConfigCacheItem const* item)
{
    if( !sprite || !item )
        return;
    for( int i = 0; i < item->transform_count; i++ )
    {
        if( strcmp(item->transform[i], "flip_h") == 0 )
            ToriDraw_SpriteFlipHorizontal(sprite);
        else if( strcmp(item->transform[i], "flip_v") == 0 )
            ToriDraw_SpriteFlipVertical(sprite);
    }
}

struct ToriDraw_Sprite**
dat1_buildcache_sprite_decode(
    struct Dat1BuildCache* buildcache,
    struct RevConfigCacheItem const* item,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    if( !buildcache || !item )
        return NULL;

    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !filelist )
        return NULL;

    int start = item->atlas_count > 0 ? 0 : item->atlas_index;
    int count = item->atlas_count > 0 ? item->atlas_count : 1;
    struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
        return NULL;

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        sprites[j] = sprite_decode_from_filelist(filelist, item, start + j);
        if( sprites[j] )
        {
            sprite_apply_transforms(sprites[j], item);
            loaded++;
        }
    }

    if( loaded <= 0 )
    {
        free(sprites);
        return NULL;
    }

    if( out_count )
        *out_count = count;
    return sprites;
}

struct ToriDraw_Sprite*
dat1_buildcache_sprite_decode_ref(
    struct Dat1BuildCache* buildcache,
    char const* sprite_ref)
{
    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !filelist || !sprite_ref || !sprite_ref[0] )
        return NULL;

    char filename_buf[256];
    int sprite_idx = 0;

    if( sscanf(sprite_ref, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        char name_buf[256];
        char index_buf[32];
        if( sscanf(sprite_ref, "%255[^[][%31[^]]", name_buf, index_buf) == 2 )
        {
            strncpy(filename_buf, name_buf, sizeof(filename_buf) - 1);
            filename_buf[sizeof(filename_buf) - 1] = '\0';
            sprite_idx = atoi(index_buf);
        }
        else
        {
            strncpy(filename_buf, sprite_ref, sizeof(filename_buf) - 1);
            filename_buf[sizeof(filename_buf) - 1] = '\0';
            sprite_idx = 0;
        }
    }

    size_t len = strlen(filename_buf);
    if( len + 5 <= sizeof(filename_buf) &&
        (len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0) )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, "index.dat");
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, filename_buf);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    struct RSCacheDat1A_Pix32* pix32 = RSCacheDat1A_Pix32New(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( pix32 )
    {
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        RSCacheDat1A_Pix32Free(pix32);
        if( sprite )
            return sprite;
    }

    struct RSCacheDat1A_Pix8Palette* pix8 = RSCacheDat1A_Pix8PaletteNew(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix8 )
        return NULL;
    struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8);
    RSCacheDat1A_Pix8PaletteFree(pix8);
    return sprite;
}

struct ToriDraw_Font*
dat1_buildcache_font_decode(
    struct Dat1BuildCache* buildcache,
    char const* font_name)
{
    if( !buildcache || !font_name || !font_name[0] )
        return NULL;

    struct RSCacheShared_FileListDat* filelist = buildcache->title_fonts_jagfile;
    if( !filelist )
        return NULL;

    char data_filename[32];
    snprintf(data_filename, sizeof(data_filename), "%s.dat", font_name);

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, "index.dat");
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    return ToriDraw_FontNewFromRSBytes(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
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

struct ToriDraw_Sprite*
dat1_buildcache_obj_icon_sprite(
    struct Dat1BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int obj_id,
    int count)
{
    if( !buildcache || !scene || obj_id < 0 )
        return NULL;

    struct RSCacheDat1A_ConfigObj* obj = obj_icon_resolve_obj(buildcache, obj_id, count);
    if( !obj || obj->model <= 0 )
        return NULL;

    struct RSCacheDat2A_Model* raw = dat1_buildcache_model_get(buildcache, obj->model);
    if( !raw )
        return NULL;

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    if( !copy )
        return NULL;

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

    int sin_pitch = (g_sin_table[obj->xan2d] * zoom) >> 16;
    int cos_pitch = (RSCacheDat2A_NoiseCosTable[obj->xan2d] * zoom) >> 16;

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
    if( !pixels )
    {
        ToriDraw_ModelFree(td_model);
        return NULL;
    }

    ToriDraw_RenderModel(hnd, scene, &position, &view_port, &camera, pixels);

    uint32_t* argb = malloc(32u * 32u * sizeof(uint32_t));
    if( !argb )
    {
        free(pixels);
        ToriDraw_ModelFree(td_model);
        return NULL;
    }

    for( int i = 0; i < 32 * 32; i++ )
        argb[i] = (uint32_t)pixels[i];

    free(pixels);
    ToriDraw_ModelFree(td_model);

    obj_icon_postprocess_outline(argb, 32, 32);
    return ToriDraw_SpriteNewFromArgbOwned(argb, 32, 32);
}
