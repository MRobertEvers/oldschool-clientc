#include "obj_icon.h"

#include "bmp.h"
#include "dash_utils.h"
#include "graphics/dash.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/gamecache/gamecache_obj.h"
#include "rscache/tables/model.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── LRU sprite cache (mirrors ObjType.spriteCache size 200 in Client.ts) ── */

struct ObjIconCacheEntry g_obj_icon_cache[OBJ_ICON_CACHE_SIZE];
uint32_t                 g_obj_icon_cache_frame = 0;
static int               s_cache_initialised    = 0;

static void
cache_init(void)
{
    if( s_cache_initialised )
        return;
    for( int i = 0; i < OBJ_ICON_CACHE_SIZE; i++ )
    {
        g_obj_icon_cache[i].obj_id    = -1;
        g_obj_icon_cache[i].count     = 0;
        g_obj_icon_cache[i].sprite    = NULL;
        g_obj_icon_cache[i].last_used = 0;
    }
    s_cache_initialised = 1;
}

void
obj_icon_cache_tick(void)
{
    g_obj_icon_cache_frame++;
}

void
obj_icon_cache_clear(void)
{
    cache_init();
    for( int i = 0; i < OBJ_ICON_CACHE_SIZE; i++ )
    {
        if( g_obj_icon_cache[i].sprite )
        {
            dashsprite_free(g_obj_icon_cache[i].sprite);
            g_obj_icon_cache[i].sprite = NULL;
        }
        g_obj_icon_cache[i].obj_id    = -1;
        g_obj_icon_cache[i].last_used = 0;
    }
}

/* Returns cached sprite (touching last_used) or NULL on miss. */
static struct DashSprite*
cache_lookup(int obj_id, int count)
{
    cache_init();
    for( int i = 0; i < OBJ_ICON_CACHE_SIZE; i++ )
    {
        if( g_obj_icon_cache[i].obj_id == obj_id &&
            g_obj_icon_cache[i].count  == count  &&
            g_obj_icon_cache[i].sprite != NULL )
        {
            g_obj_icon_cache[i].last_used = g_obj_icon_cache_frame;
            return g_obj_icon_cache[i].sprite;
        }
    }
    return NULL;
}

/* Evict the LRU entry and store the new sprite.  The cache takes ownership of sprite. */
static void
cache_insert(int obj_id, int count, struct DashSprite* sprite)
{
    cache_init();
    /* Find the LRU (lowest last_used) or an empty slot. */
    int    lru_idx  = 0;
    uint32_t lru_val = g_obj_icon_cache[0].last_used;
    for( int i = 1; i < OBJ_ICON_CACHE_SIZE; i++ )
    {
        if( g_obj_icon_cache[i].obj_id < 0 )
        {
            lru_idx = i;
            lru_val = 0;
            break;
        }
        if( g_obj_icon_cache[i].last_used < lru_val )
        {
            lru_val = g_obj_icon_cache[i].last_used;
            lru_idx = i;
        }
    }
    /* Free evicted sprite. */
    if( g_obj_icon_cache[lru_idx].sprite )
        dashsprite_free(g_obj_icon_cache[lru_idx].sprite);

    g_obj_icon_cache[lru_idx].obj_id    = obj_id;
    g_obj_icon_cache[lru_idx].count     = count;
    g_obj_icon_cache[lru_idx].sprite    = sprite;
    g_obj_icon_cache[lru_idx].last_used = g_obj_icon_cache_frame;
}

// Helper function to get obj model for inventory icons
// Based on ObjType.getModel() - applies scaling, recoloring, and calculates normals
// This is what getIcon() calls (line 403 in ObjType.ts)
static struct GameCacheModel*
get_obj_inv_model(
    struct GGame* game,
    struct GameCacheObj* obj)
{
    // For inventory icons, we use getModel() which includes scaling and normals
    if( obj->model == 0 || obj->model == -1 )
    {
        return NULL;
    }

    struct GameCacheModel* model = gamecache_get_model(game->gamecache, obj->model);
    if( !model )
    {
        // printf("get_obj_inv_model: Could not load model %d\n", obj->model);
        return NULL;
    }

    // NOTE: In the TypeScript version, getModel() applies:
    // 1. Scaling (resizex, resizey, resizez) - if not 128,128,128
    // 2. Recoloring (recol_s, recol_d)
    // 3. calculateNormals(ambient + 64, contrast + 768, -50, -10, -50, true)
    //
    // For now, we're just returning the base model
    // TODO: Apply scaling, recoloring, and calculate normals

    return model;
}

/* Internal uncached generator – mirrors ObjType.getSprite() in ObjType.ts. */
static struct DashSprite*
obj_icon_generate(
    struct GGame* game,
    int obj_id,
    int count);

/** Cert overlay sprite: ObjType.getSprite(id, count, -1) — 1.5x zoom, no LRU cache, no shadow pass. */
static struct DashSprite*
obj_icon_generate_cert_inner(
    struct GGame* game,
    int obj_id,
    int count);

/** Raster one obj icon; cert_inner selects outlineRgb==-1 semantics (inner linked icon). */
static struct DashSprite*
obj_icon_raster_uncached(
    struct GGame* game,
    struct GameCacheObj* obj,
    int cert_inner);

struct DashSprite*
obj_icon_get(
    struct GGame* game,
    int obj_id,
    int count)
{
    /* Fast path: LRU cache hit. */
    struct DashSprite* cached = cache_lookup(obj_id, count);
    if( cached )
        return cached;

    /* Cache miss: generate and insert. */
    struct DashSprite* icon = obj_icon_generate(game, obj_id, count);
    if( icon )
        cache_insert(obj_id, count, icon);
    return icon;
}

static struct DashSprite*
obj_icon_generate_cert_inner(
    struct GGame* game,
    int obj_id,
    int count)
{
    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
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
        if( countobj_id != -1 )
            return obj_icon_generate_cert_inner(game, countobj_id, 1);
    }

    return obj_icon_raster_uncached(game, obj, 1);
}

static struct DashSprite*
obj_icon_raster_uncached(
    struct GGame* game,
    struct GameCacheObj* obj,
    int cert_inner)
{
    struct GameCacheModel* model = get_obj_inv_model(game, obj);
    if( !model )
    {
        printf(
            "obj_icon_get: Could not get inventory model for obj (model id: %d)\n",
            obj->model);
        return NULL;
    }

    struct DashSprite* icon = (struct DashSprite*)malloc(sizeof(struct DashSprite));
    memset(icon, 0, sizeof(struct DashSprite));

    icon->width = 32;
    icon->height = 32;
    icon->pixels_argb = (uint32_t*)malloc(32 * 32 * sizeof(uint32_t));

    memset(icon->pixels_argb, 0, 32 * 32 * sizeof(uint32_t));

    struct DashViewPort view_port;
    view_port.width = 32;
    view_port.height = 32;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = 32;
    view_port.clip_bottom = 32;
    view_port.x_center = 16;
    view_port.y_center = 16;
    view_port.stride = 32;

    struct DashCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pitch = obj->xan2d;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    int zoom = obj->zoom2d;
    if( zoom == 0 )
        zoom = 2000;
    if( cert_inner )
        zoom = (zoom * 3) >> 1;

    extern int g_sin_table[2048];
    extern int g_cos_table[2048];

    int sinPitch = (g_sin_table[obj->xan2d] * zoom) >> 16;
    int cosPitch = (g_cos_table[obj->xan2d] * zoom) >> 16;

    struct DashPosition position = { 0 };
    position.pitch = 0;
    position.yaw = obj->yan2d;
    position.roll = obj->zan2d;
    position.x = obj->xof2d;
    position.y = 0;
    position.z = 0;

    struct GameCacheModel* model_copy = gamecache_model_new_copy(model);
    for( int i = 0; i < obj->recol_count; i++ )
        gamecache_model_transform_recolor(model_copy, obj->recol_s[i], obj->recol_d[i]);
    struct DashModel* dash_model = dashmodel_new_from_gamecache_model(model_copy);

    if( !dash_model )
    {
        printf("  ERROR: Failed to create DashModel\n");
        free(icon->pixels_argb);
        free(icon);
        return NULL;
    }

    dashmodel_alloc_normals(dash_model);
    _light_model_default(dash_model, obj->contrast, obj->ambient);

    const struct DashBoundsCylinder* bc = dashmodel_bounds_cylinder_const(dash_model);
    int model_min_y = -bc->min_y;
    position.y = sinPitch + (model_min_y / 2) + obj->yof2d;
    position.z = cosPitch + obj->yof2d;

    int cull = dash3d_project_model6(game->sys_dash, dash_model, &position, &view_port, &camera);

    if( cull == DASHCULL_VISIBLE )
    {
        dash3d_raster_projected_model(
            game->sys_dash,
            dash_model,
            &position,
            &view_port,
            &camera,
            (int*)icon->pixels_argb,
            true);
    }
    else
    {
        printf("  Warning: Model culled during projection (cull=%d)\n", cull);
    }

    for( int x = 31; x >= 0; x-- )
    {
        for( int y = 31; y >= 0; y-- )
        {
            if( icon->pixels_argb[x + y * 32] != 0 )
                continue;

            if( x > 0 && icon->pixels_argb[(x - 1) + y * 32] > 1 )
                icon->pixels_argb[x + y * 32] = 1;
            else if( y > 0 && icon->pixels_argb[x + (y - 1) * 32] > 1 )
                icon->pixels_argb[x + y * 32] = 1;
            else if( x < 31 && icon->pixels_argb[x + 1 + y * 32] > 1 )
                icon->pixels_argb[x + y * 32] = 1;
            else if( y < 31 && icon->pixels_argb[x + (y + 1) * 32] > 1 )
                icon->pixels_argb[x + y * 32] = 1;
        }
    }

    if( !cert_inner )
    {
        for( int x = 31; x >= 0; x-- )
        {
            for( int y = 31; y >= 0; y-- )
            {
                if( icon->pixels_argb[x + y * 32] == 0 && x > 0 && y > 0 &&
                    icon->pixels_argb[(x - 1) + (y - 1) * 32] > 0 )
                    icon->pixels_argb[x + y * 32] = 1;
            }
        }
    }

    dashmodel_free(dash_model);
    return icon;
}

static struct DashSprite*
obj_icon_generate(
    struct GGame* game,
    int obj_id,
    int count)
{
    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
    if( !obj )
    {
        printf("obj_icon_get: Could not find obj %d in buildcachedat\n", obj_id);
        return NULL;
    }

    if( obj->countobj && obj->countco && count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < obj->countobj_count && i < 10; i++ )
        {
            if( count >= obj->countco[i] && obj->countco[i] != 0 )
                countobj_id = obj->countobj[i];
        }

        if( countobj_id != -1 )
            return obj_icon_get(game, countobj_id, 1);
    }

    struct DashSprite* linked = NULL;
    if( obj->certtemplate != -1 )
    {
        linked = obj_icon_generate_cert_inner(game, obj->certlink, 10);
        if( !linked )
            return NULL;
    }

    struct DashSprite* icon = obj_icon_raster_uncached(game, obj, 0);
    if( !icon )
    {
        if( linked )
            dashsprite_free(linked);
        return NULL;
    }

    if( linked )
    {
        struct DashViewPort overlay_vp;
        overlay_vp.width = 32;
        overlay_vp.height = 32;
        overlay_vp.clip_left = 0;
        overlay_vp.clip_top = 0;
        overlay_vp.clip_right = 32;
        overlay_vp.clip_bottom = 32;
        overlay_vp.x_center = 16;
        overlay_vp.y_center = 16;
        overlay_vp.stride = 32;
        dash2d_blit_sprite_alpha(
            game->sys_dash, linked, &overlay_vp, 0, 0, 256, (int*)icon->pixels_argb);
        dashsprite_free(linked);
    }

    return icon;
}

struct DashModel*
obj_icon_new_dash_model_for_obj(struct GGame* game, int obj_id_0based)
{
    if( !game || !game->gamecache )
        return NULL;
    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id_0based);
    if( !obj )
        return NULL;
    struct GameCacheModel* cache_model = get_obj_inv_model(game, obj);
    if( !cache_model )
        return NULL;
    struct GameCacheModel* model_copy = gamecache_model_new_copy(cache_model);
    if( !model_copy )
        return NULL;
    struct DashModel* m = dashmodel_new_from_gamecache_model(model_copy);
    gamecache_model_free(model_copy);
    if( !m )
        return NULL;
    dashmodel_alloc_normals(m);
    _light_model_default(m, obj->contrast, obj->ambient);
    return m;
}

void
head_model_render(
    struct GGame* game,
    struct DashModel* dash_model,
    int* buffer,
    int width,
    int height,
    int zoom,
    int xan,
    int yan)
{
    if( !game || !game->sys_dash || !dash_model || !buffer || width <= 0 || height <= 0 )
        return;

    if( zoom == 0 )
        zoom = 2000;

    /* Depth sort buckets use bounds_cylinder — update after pose morph (see head_model_render_to_region). */
    dashmodel_set_bounds_cylinder(dash_model);

    extern int g_sin_table[2048];
    extern int g_cos_table[2048];

    int sinPitch = (g_sin_table[xan] * zoom) >> 16;
    int cosPitch = (g_cos_table[xan] * zoom) >> 16;

    struct DashViewPort view_port;
    view_port.width = width;
    view_port.height = height;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.x_center = width / 2;
    view_port.y_center = height / 2;
    view_port.stride = width;

    struct DashCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pitch = xan;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    struct DashPosition position = { 0 };
    position.pitch = 0;
    position.yaw = yan;
    position.roll = 0;
    position.x = 0;
    int model_min_y = dashmodel_bounds_cylinder_const(dash_model)->min_y;
    position.y = sinPitch - (model_min_y / 2);
    position.z = cosPitch;

    memset(buffer, 0, (size_t)width * height * sizeof(int));

    int cull = dash3d_project_model6(game->sys_dash, dash_model, &position, &view_port, &camera);
    if( cull == DASHCULL_VISIBLE )
    {
        dash3d_raster_projected_model(
            game->sys_dash, dash_model, &position, &view_port, &camera, buffer, true);
    }
}

/* Render head model directly to a region of the buffer.
 * Client.ts: Pix3D.centerX = childX + (child.width/2), Pix3D.centerY = childY + (child.height/2).
 * No intermediate buffer - draws to pixel_buffer at (x, y) with size (width, height). */
void
head_model_render_to_region(
    struct GGame* game,
    struct DashModel* dash_model,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int zoom,
    int xan,
    int yan)
{
    if( !game || !game->sys_dash || !dash_model || !pixel_buffer || width <= 0 || height <= 0 )
        return;

    if( zoom == 0 )
        zoom = 2000;

    /* Face depth buckets use bounds_cylinder.min_z_depth_any_rotation; refresh after animate so
     * bucket_sort matches deformed vertices (chat heads morph each frame). */
    dashmodel_set_bounds_cylinder(dash_model);

    extern int g_sin_table[2048];
    extern int g_cos_table[2048];

    int sinPitch = (g_sin_table[xan] * zoom) >> 16;
    int cosPitch = (g_cos_table[xan] * zoom) >> 16;

    int* dst = pixel_buffer + y * stride + x;

    struct DashViewPort view_port;
    view_port.width = width;
    view_port.height = height;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.x_center = width / 2;
    view_port.y_center = height / 2;
    view_port.stride = stride;

    struct DashCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pitch = xan;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    struct DashPosition position = { 0 };
    position.pitch = 0;
    position.yaw = yan;
    position.roll = 0;
    position.x = 0;
    const struct DashBoundsCylinder* bc2 = dashmodel_bounds_cylinder_const(dash_model);
    int model_height = (bc2->max_y - bc2->min_y);
    position.y = sinPitch - (height / 2) + (model_height / 2);
    position.z = cosPitch;

    int cull = dash3d_project_model6(game->sys_dash, dash_model, &position, &view_port, &camera);
    if( cull == DASHCULL_VISIBLE )
    {
        dash3d_raster_projected_model(
            game->sys_dash, dash_model, &position, &view_port, &camera, dst, true);
    }
}
