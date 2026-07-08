#include "toridraw_model_sprite.h"

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <stdlib.h>

void
ToriDraw_SpritePostprocessObjIconOutline(
    uint32_t* pixels,
    int w,
    int h)
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

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromModelRaster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int width,
    int height,
    bool postprocess_outline)
{
    assert(scene);
    assert(hnd.kind == TORIDRAWMK_MODEL);
    assert(width > 0 && height > 0);

    if( zoom <= 0 )
        zoom = 2000;

    int sin_pitch = (ToriDraw_Sin(xan) * zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(xan) * zoom) >> 16;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = width;
    view_port.height = height;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.x_center = width / 2;
    view_port.y_center = height / 2;
    view_port.stride = width;

    struct ToriDraw_Camera camera = { 0 };
    camera.pitch = xan;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_height = bounds ? (bounds->max_y - bounds->min_y) : 0;

    struct ToriDraw_Position position = { 0 };
    position.pitch = 0;
    position.yaw = yan;
    position.roll = 0;
    position.x = 0;
    position.y = sin_pitch - (height / 2) + (model_height / 2);
    position.z = cos_pitch;

    size_t pixel_count = (size_t)width * (size_t)height;
    toripixel_t* pixels = calloc(pixel_count, sizeof(toripixel_t));
    if( !pixels )
        return NULL;

    ToriDraw_RenderModel(hnd, scene, &position, &view_port, &camera, pixels);

    uint32_t* argb = malloc(pixel_count * sizeof(uint32_t));
    if( !argb )
    {
        free(pixels);
        return NULL;
    }

    for( size_t i = 0; i < pixel_count; i++ )
        argb[i] = (uint32_t)pixels[i];

    free(pixels);

    if( postprocess_outline )
        ToriDraw_SpritePostprocessObjIconOutline(argb, width, height);

    return ToriDraw_SpriteNewFromArgbOwned(argb, width, height);
}

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromObjIconRaster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int xof,
    int yof,
    int width,
    int height,
    bool postprocess_outline)
{
    assert(scene);
    assert(hnd.kind == TORIDRAWMK_MODEL);
    assert(width > 0 && height > 0);

    if( zoom <= 0 )
        zoom = 2000;

    int sin_pitch = (ToriDraw_Sin(xan) * zoom) >> 16;
    int cos_pitch = (ToriDraw_Cos(xan) * zoom) >> 16;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = width;
    view_port.height = height;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.x_center = (width == 36 && height == 32) ? 16 : width / 2;
    view_port.y_center = (width == 36 && height == 32) ? 16 : height / 2;
    view_port.stride = width;

    struct ToriDraw_Camera camera = { 0 };
    camera.pitch = xan;
    camera.yaw = 0;
    camera.roll = 0;
    camera.fov_rpi2048 = 512;
    camera.near_plane_z = 1;

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_min_y = bounds ? -bounds->min_y : 0;

    struct ToriDraw_Position position = { 0 };
    position.pitch = 0;
    position.yaw = yan;
    position.roll = zan;
    position.x = xof;
    position.y = sin_pitch + model_min_y / 2 + yof;
    position.z = cos_pitch + yof;

    bool osrs_icon = width == 36 && height == 32;
    int render_w = width;
    int render_h = height;
    int crop_x = 0;
    if( osrs_icon )
    {
        // ToriDraw raster centers at width/2, not view_port.x_center. Render into a padded
        // buffer so the projection center lands at (16,16) of the 36x32 icon.
        render_w = 40;
        crop_x = (render_w / 2) - 16;
    }

    size_t render_pixel_count = (size_t)render_w * (size_t)render_h;
    toripixel_t* render_pixels = calloc(render_pixel_count, sizeof(toripixel_t));
    if( !render_pixels )
        return NULL;

    view_port.width = render_w;
    view_port.clip_right = render_w;
    view_port.x_center = render_w / 2;
    view_port.stride = render_w;

    ToriDraw_RenderModel(hnd, scene, &position, &view_port, &camera, render_pixels);

    size_t out_pixel_count = (size_t)width * (size_t)height;
    uint32_t* argb = malloc(out_pixel_count * sizeof(uint32_t));
    if( !argb )
    {
        free(render_pixels);
        return NULL;
    }

    if( osrs_icon )
    {
        for( int y = 0; y < height; y++ )
        {
            for( int x = 0; x < width; x++ )
                argb[x + y * width] = (uint32_t)render_pixels[(x + crop_x) + y * render_w];
        }
    }
    else
    {
        for( size_t i = 0; i < out_pixel_count; i++ )
            argb[i] = (uint32_t)render_pixels[i];
    }

    free(render_pixels);

    if( postprocess_outline )
        ToriDraw_SpritePostprocessObjIconOutline(argb, width, height);

    return ToriDraw_SpriteNewFromArgbOwned(argb, width, height);
}
