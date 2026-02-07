#include "obj_icon.h"

#include "bmp.h"
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
    memset(icon->pixels_argb, 0, 32 * 32 * sizeof(int));

    // Clear the icon buffer to black
    memset(icon->pixels_argb, 0, 32 * 32 * sizeof(int));

    // Calculate zoom and angles for rendering
    int zoom = obj->zoom2d;
    if( zoom == 0 )
        zoom = 2000; // Default zoom

    // Calculate sine and cosine for pitch rotation
    int xan = obj->xan2d;
    int yan = obj->yan2d;
    int zan = obj->zan2d;

    // Calculate center offsets
    int xof = obj->xof2d;
    int yof = obj->yof2d;

    // Use sine/cosine tables for rotation
    // For now, skip the complex 3D rendering and create a placeholder

    // Calculate base color from the model's first face color
    int base_color = 0x808080; // Gray base
    if( model->face_count > 0 && model->face_colors )
    {
        // Use the first face color as the base
        base_color = model->face_colors[0];
    }

    // Draw a simple filled circle/square representing the item
    int center_x = 16;
    int center_y_pixel = 16;
    int radius = 12;

    for( int y = 0; y < 32; y++ )
    {
        for( int x = 0; x < 32; x++ )
        {
            int dx = x - center_x;
            int dy = y - center_y_pixel;
            int dist_sq = dx * dx + dy * dy;

            if( dist_sq < radius * radius )
            {
                // Inside the circle
                int shade = 255 - (dist_sq * 100 / (radius * radius));
                int r = ((base_color >> 16) & 0xFF) * shade / 255;
                int g = ((base_color >> 8) & 0xFF) * shade / 255;
                int b = (base_color & 0xFF) * shade / 255;
                icon->pixels_argb[y * 32 + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

    // Add a simple outline (1 pixel border in darker color)
    for( int y = 1; y < 31; y++ )
    {
        for( int x = 1; x < 31; x++ )
        {
            if( icon->pixels_argb[y * 32 + x] == 0 )
            {
                // Check if any neighbor pixel is filled
                bool has_filled_neighbor = false;
                if( x > 0 && icon->pixels_argb[y * 32 + x - 1] != 0 )
                    has_filled_neighbor = true;
                if( y > 0 && icon->pixels_argb[(y - 1) * 32 + x] != 0 )
                    has_filled_neighbor = true;
                if( x < 31 && icon->pixels_argb[y * 32 + x + 1] != 0 )
                    has_filled_neighbor = true;
                if( y < 31 && icon->pixels_argb[(y + 1) * 32 + x] != 0 )
                    has_filled_neighbor = true;

                if( has_filled_neighbor )
                {
                    icon->pixels_argb[y * 32 + x] = 0xFF000000; // Black outline
                }
            }
        }
    }

    // DEBUG: Save sprite to BMP file
    char filename[256];
    snprintf(filename, sizeof(filename), "debug_obj_%d_count_%d.bmp", obj_id, count);
    bmp_write_file(filename, icon->pixels_argb, 32, 32);
    printf("  Saved sprite to: %s\n", filename);
    printf("========================================\n\n");

    return icon;
}
