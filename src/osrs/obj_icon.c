#include "obj_icon.h"

#include "bmp.h"
#include "dash_utils.h"
#include "entity_scenebuild.h"
#include "graphics/dash.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "rscache/tables/model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to get obj model for inventory icons
// Based on ObjType.getModel() - applies scaling, recoloring, and calculates normals
// This is what getIcon() calls (line 403 in ObjType.ts)
static struct CacheModel*
get_obj_inv_model(
    struct GGame* game,
    struct CacheDatConfigObj* obj)
{
    // For inventory icons, we use getModel() which includes scaling and normals
    if( obj->model == 0 || obj->model == -1 )
    {
        return NULL;
    }

    struct CacheModel* model = buildcachedat_get_model(game->buildcachedat, obj->model);
    if( !model )
    {
        printf("get_obj_inv_model: Could not load model %d\n", obj->model);
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

// Generate a 32x32 icon sprite from an object model
// Based on ObjType.getIcon() in Client.ts (lines 370-524)
struct DashSprite*
obj_icon_get(
    struct GGame* game,
    int obj_id,
    int count)
{
    // Get the object configuration
    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    if( !obj )
    {
        printf("obj_icon_get: Could not find obj %d\n", obj_id);
        return NULL;
    }

    // // DEBUG: Print detailed object configuration
    // printf("\n=== obj_icon_get DEBUG: obj_id=%d, count=%d ===\n", obj_id, count);
    // printf("  name: %s\n", obj->name ? obj->name : "(null)");
    // printf("  model: %d\n", obj->model);
    // printf("  manwear: %d, manwear2: %d, manwear3: %d\n", obj->manwear, obj->manwear2,
    // obj->manwear3); printf("  zoom2d: %d\n", obj->zoom2d); printf("  xan2d: %d, yan2d: %d, zan2d:
    // %d\n", obj->xan2d, obj->yan2d, obj->zan2d); printf("  xof2d: %d, yof2d: %d\n", obj->xof2d,
    // obj->yof2d); printf("  stackable: %d\n", obj->stackable); if( obj->recol_count > 0 )
    // {
    //     printf("  recolor count: %d\n", obj->recol_count);
    //     for( int i = 0; i < obj->recol_count && i < 3; i++ )
    //     {
    //         printf("    recol[%d]: 0x%04X -> 0x%04X\n", i, obj->recol_s[i], obj->recol_d[i]);
    //     }
    // }

    // Handle count-based object variations (e.g., coin stacks)
    if( obj->countobj && obj->countco && count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < obj->countobj_count && i < 10; i++ )
        {
            if( count >= obj->countco[i] && obj->countco[i] != 0 )
            {
                countobj_id = obj->countobj[i];
            }
        }

        if( countobj_id != -1 )
        {
            printf("  Using count variant: %d -> %d (count=%d)\n", obj_id, countobj_id, count);
            return obj_icon_get(game, countobj_id, 1);
        }
    }

    // Get or create the model for this object
    struct CacheModel* model = get_obj_inv_model(game, obj);
    if( !model )
    {
        printf(
            "obj_icon_get: Could not get inventory model for obj %d (model id: %d)\n",
            obj_id,
            obj->model);
        return NULL;
    }

    // DEBUG: Print model information
    printf("  Model loaded: %d\n", obj->model);
    printf("    vertex_count: %d\n", model->vertex_count);
    printf("    face_count: %d\n", model->face_count);
    if( model->face_count > 0 && model->face_colors )
    {
        printf("    face_colors[0]: 0x%04X\n", model->face_colors[0]);
        if( model->face_count > 1 )
            printf("    face_colors[1]: 0x%04X\n", model->face_colors[1]);
    }

    // Create a 32x32 sprite for the icon
    struct DashSprite* icon = (struct DashSprite*)malloc(sizeof(struct DashSprite));
    memset(icon, 0, sizeof(struct DashSprite));

    icon->width = 32;
    icon->height = 32;
    icon->pixels_argb = (int*)malloc(32 * 32 * sizeof(int));

    // Clear the icon buffer to black/transparent
    memset(icon->pixels_argb, 0, 32 * 32 * sizeof(int));

    // Setup viewport for 32x32 rendering (matching Pix2D.bind)
    struct DashViewPort view_port;
    view_port.width = 32;
    view_port.height = 32;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = 32;
    view_port.clip_bottom = 32;
    view_port.x_center = 16; // Center of 32x32
    view_port.y_center = 16;
    view_port.stride = 32;

    // Setup camera for orthographic-style projection (matching Pix3D.init2D)
    struct DashCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pitch = 0;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512; // Orthographic-like FOV
    camera.near_plane_z = 1;  // Very close near plane to prevent culling

    // Position for rendering - use NEGATIVE Z to place model in front of camera
    struct DashPosition position = { 0 };
    position.pitch = obj->xan2d; // Use obj rotations directly
    position.yaw = obj->yan2d;
    position.roll = obj->zan2d;

    // Position the model in front of the camera
    position.x = obj->xof2d;
    position.y = obj->yof2d;
    position.z = obj->zoom2d; // NEGATIVE Z puts it in front of camera

    // Convert CacheModel to DashModel using the proper utility function
    // IMPORTANT: Make a copy first! dashmodel_new_from_cache_model moves ownership
    // and would invalidate the cached model. See entity_scenebuild.c:219 for reference.
    struct CacheModel* model_copy = model_new_copy(model);
    struct DashModel* dash_model = dashmodel_new_from_cache_model(model_copy);
    if( !dash_model )
    {
        printf("  ERROR: Failed to create DashModel\n");
        free(icon->pixels_argb);
        free(icon);
        return NULL;
    }

    // Calculate normals and apply lighting (matching ObjType.ts line 332)
    // calculateNormals(ambient + 64, contrast + 768, -50, -10, -50, true)
    // In C, this is done via _light_model_default with ambient and contrast from config
    _light_model_default(dash_model, obj->contrast, obj->ambient);
    printf("  Normals and lighting calculated\n");

    // Project and raster the model (matching model.drawSimple)
    // Use dash3d_project_model6 for full 6DOF support (pitch, yaw, roll)
    int cull = dash3d_project_model6(game->sys_dash, dash_model, &position, &view_port, &camera);

    if( cull == DASHCULL_VISIBLE )
    {
        dash3d_raster_projected_model(
            game->sys_dash, dash_model, &position, &view_port, &camera, icon->pixels_argb);
    }
    else
    {
        printf("  Warning: Model culled during projection (cull=%d)\n", cull);
    }

    // Clean up the dash model
    dashmodel_free(dash_model);

    // DEBUG: Save sprite to BMP file
    char filename[256];
    snprintf(filename, sizeof(filename), "debug_obj_%d_count_%d.bmp", obj_id, count);
    bmp_write_file(filename, icon->pixels_argb, 32, 32);
    printf("  Saved sprite to: %s\n", filename);
    printf("========================================\n\n");

    return icon;
}
