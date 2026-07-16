#include "toridraw_model_sprite.h"

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define TORIDRAW_MODEL_EXTENTS_BORDER 3
#define TORIDRAW_MODEL_EXTENTS_MAX_BBOX 1024
#define TORIDRAW_MODEL_EXTENTS_MAX_PIXELS 1060900 /* 1030 * 1030 */
#define WIDGET_MODEL_NEAR 50
#define WIDGET_MODEL_ZOOM3D 512
#define WIDGET_MODEL_CLIP_X -5000

struct WidgetModelTransform
{
    int var2;
    int var3;
    int var5;
    int var6;
    int var7;
    int var10;
    int var11;
    int var12;
    int var13;
    int var14;
    int var15;
    int var16;
    int var17;
    int zoom2d;
    int zoom3d;
    bool orthographic;
};

static void
widget_model_init_transform(
    struct WidgetModelTransform* xf,
    int zoom,
    int xan,
    int yan,
    int zan,
    int orientation,
    int model_offset,
    int model_center_y,
    bool orthographic, /* 1 = orthographic path; 0 = perspective objRender */
    bool fixed_zoom)   /* 1 = zoom3d = zoom; 0 = zoom3d = 512 */
{
    const int var1 = 0;
    const int var2 = yan & 2047;
    const int var3 = zan & 2047;
    const int var4 = xan & 2047;

    int sin_pitch = (zoom * ToriDraw_Sin(var4)) >> 16;
    int cos_pitch = (zoom * ToriDraw_Cos(var4)) >> 16;

    xf->var2 = var2;
    xf->var3 = var3;
    xf->var5 = orientation;
    xf->var6 = sin_pitch + model_offset + model_center_y;
    xf->var7 = cos_pitch + model_offset;
    xf->var10 = ToriDraw_Sin(var1);
    xf->var11 = ToriDraw_Cos(var1);
    xf->var12 = ToriDraw_Sin(var2);
    xf->var13 = ToriDraw_Cos(var2);
    xf->var14 = ToriDraw_Sin(var3);
    xf->var15 = ToriDraw_Cos(var3);
    xf->var16 = ToriDraw_Sin(var4);
    xf->var17 = ToriDraw_Cos(var4);
    xf->zoom2d = zoom > 0 ? zoom : 2000;
    xf->zoom3d = fixed_zoom ? xf->zoom2d : WIDGET_MODEL_ZOOM3D;
    xf->orthographic = orthographic;
}

static void
widget_model_transform_vertex(
    const struct WidgetModelTransform* xf,
    int vx,
    int vy,
    int vz0,
    int* out_cx,
    int* out_cy,
    int* out_cz)
{
    int t;

    if( xf->var3 != 0 )
    {
        t = (vy * xf->var14 + vx * xf->var15) >> 16;
        vy = (vy * xf->var15 - vx * xf->var14) >> 16;
        vx = t;
    }
    if( xf->var10 != 0 )
    {
        t = (vy * xf->var11 - vz0 * xf->var10) >> 16;
        vz0 = (vy * xf->var10 + vz0 * xf->var11) >> 16;
        vy = t;
    }
    if( xf->var2 != 0 )
    {
        t = (vz0 * xf->var12 + vx * xf->var13) >> 16;
        vz0 = (vz0 * xf->var13 - vx * xf->var12) >> 16;
        vx = t;
    }

    vx += xf->var5;
    vy += xf->var6;
    vz0 += xf->var7;

    t = (vy * xf->var17 - vz0 * xf->var16) >> 16;
    vz0 = (vy * xf->var16 + vz0 * xf->var17) >> 16;

    *out_cx = vx;
    *out_cy = t;
    *out_cz = vz0;
}

static bool
widget_model_project_bounds(
    struct ToriDraw_ModelHandle hnd,
    const struct WidgetModelTransform* xf,
    int* out_width,
    int* out_height,
    int* out_dx,
    int* out_dy)
{
    int vc = ToriDraw_ModelGetVertexCount(hnd);
    vertexint_t* vertices_x = ToriDraw_ModelGetVerticesX(hnd);
    vertexint_t* vertices_y = ToriDraw_ModelGetVerticesY(hnd);
    vertexint_t* vertices_z = ToriDraw_ModelGetVerticesZ(hnd);

    double min_x = INFINITY;
    double max_x = -INFINITY;
    double min_y = INFINITY;
    double max_y = -INFINITY;
    bool any = false;
    int ortho_scale = xf->zoom2d > 100 ? xf->zoom2d : 100;

    for( int i = 0; i < vc; i++ )
    {
        int cx;
        int cy;
        int cz;
        widget_model_transform_vertex(
            xf, vertices_x[i], vertices_y[i], vertices_z[i], &cx, &cy, &cz);

        double px;
        double py;
        if( xf->orthographic )
        {
            px = (double)cx * (double)xf->zoom3d / (double)ortho_scale;
            py = (double)cy * (double)xf->zoom3d / (double)ortho_scale;
        }
        else
        {
            if( cz <= WIDGET_MODEL_NEAR )
                continue;

            px = (double)cx * (double)xf->zoom3d / (double)cz;
            py = (double)cy * (double)xf->zoom3d / (double)cz;
        }
        if( px < min_x )
            min_x = px;
        if( px > max_x )
            max_x = px;
        if( py < min_y )
            min_y = py;
        if( py > max_y )
            max_y = py;
        any = true;
    }

    if( !any || !isfinite(min_x) || !isfinite(min_y) )
        return false;

    int raw_bbox_w = (int)ceil(max_x - min_x);
    int raw_bbox_h = (int)ceil(max_y - min_y);
    if( raw_bbox_w > TORIDRAW_MODEL_EXTENTS_MAX_BBOX )
        raw_bbox_w = TORIDRAW_MODEL_EXTENTS_MAX_BBOX;
    if( raw_bbox_h > TORIDRAW_MODEL_EXTENTS_MAX_BBOX )
        raw_bbox_h = TORIDRAW_MODEL_EXTENTS_MAX_BBOX;
    if( raw_bbox_w < 1 )
        raw_bbox_w = 1;
    if( raw_bbox_h < 1 )
        raw_bbox_h = 1;

    int sw = raw_bbox_w + TORIDRAW_MODEL_EXTENTS_BORDER * 2;
    int sh = raw_bbox_h + TORIDRAW_MODEL_EXTENTS_BORDER * 2;
    if( sw <= 0 || sh <= 0 || (size_t)sw * (size_t)sh > TORIDRAW_MODEL_EXTENTS_MAX_PIXELS )
        return false;

    *out_width = sw;
    *out_height = sh;
    *out_dx = (int)floor(-min_x) + TORIDRAW_MODEL_EXTENTS_BORDER;
    *out_dy = (int)floor(-min_y) + TORIDRAW_MODEL_EXTENTS_BORDER;
    return true;
}

/** Perspective projection matching Client.ts Model.objRender:
 *  origin at widget center, midZ = (eyeY*sinEyePitch + eyeZ*cosEyePitch) >> 16. */
static bool
widget_model_project_vertices_objrender(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct WidgetModelTransform* xf,
    int origin_x,
    int origin_y,
    int* out_min_x,
    int* out_min_y,
    int* out_max_x,
    int* out_max_y)
{
    int vc = ToriDraw_ModelGetVertexCount(hnd);
    vertexint_t* vertices_x = ToriDraw_ModelGetVerticesX(hnd);
    vertexint_t* vertices_y = ToriDraw_ModelGetVerticesY(hnd);
    vertexint_t* vertices_z = ToriDraw_ModelGetVerticesZ(hnd);

    int mid_z = (xf->var6 * xf->var16 + xf->var7 * xf->var17) >> 16;
    int min_x = INT_MAX;
    int min_y = INT_MAX;
    int max_x = INT_MIN;
    int max_y = INT_MIN;
    int z_count = 0;

    for( int i = 0; i < vc; i++ )
    {
        int cx;
        int cy;
        int cz;
        widget_model_transform_vertex(
            xf, vertices_x[i], vertices_y[i], vertices_z[i], &cx, &cy, &cz);

        scene->orthographic_vertices_x[i] = cx;
        scene->orthographic_vertices_y[i] = cy;
        scene->orthographic_vertices_z[i] = cz;

        if( cz <= WIDGET_MODEL_NEAR )
        {
            scene->screen_vertices_x[i] = WIDGET_MODEL_CLIP_X;
            scene->screen_vertices_y[i] = 0;
            scene->screen_vertices_z[i] = cz - mid_z;
            continue;
        }

        int px = origin_x + (cx * xf->zoom3d) / cz;
        int py = origin_y + (cy * xf->zoom3d) / cz;

        scene->screen_vertices_x[i] = px;
        scene->screen_vertices_y[i] = py;
        scene->screen_vertices_z[i] = cz - mid_z;

        if( px < min_x )
            min_x = px;
        if( px > max_x )
            max_x = px;
        if( py < min_y )
            min_y = py;
        if( py > max_y )
            max_y = py;
        z_count++;
    }

    if( z_count <= 0 )
        return false;

    *out_min_x = min_x - TORIDRAW_MODEL_EXTENTS_BORDER;
    *out_min_y = min_y - TORIDRAW_MODEL_EXTENTS_BORDER;
    *out_max_x = max_x + TORIDRAW_MODEL_EXTENTS_BORDER;
    *out_max_y = max_y + TORIDRAW_MODEL_EXTENTS_BORDER;
    return true;
}

static bool
widget_model_project_vertices(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct WidgetModelTransform* xf,
    int sw,
    int sh,
    int dx,
    int dy)
{
    int vc = ToriDraw_ModelGetVertexCount(hnd);
    vertexint_t* vertices_x = ToriDraw_ModelGetVerticesX(hnd);
    vertexint_t* vertices_y = ToriDraw_ModelGetVerticesY(hnd);
    vertexint_t* vertices_z = ToriDraw_ModelGetVerticesZ(hnd);

    int z_sum = 0;
    int z_count = 0;
    int ortho_scale = xf->zoom2d > 100 ? xf->zoom2d : 100;

    for( int i = 0; i < vc; i++ )
    {
        int cx;
        int cy;
        int cz;
        widget_model_transform_vertex(
            xf, vertices_x[i], vertices_y[i], vertices_z[i], &cx, &cy, &cz);

        scene->orthographic_vertices_x[i] = cx;
        scene->orthographic_vertices_y[i] = cy;
        scene->orthographic_vertices_z[i] = cz;

        int px;
        int py;
        if( xf->orthographic )
        {
            px = (cx * xf->zoom3d) / ortho_scale;
            py = (cy * xf->zoom3d) / ortho_scale;
        }
        else
        {
            if( cz <= WIDGET_MODEL_NEAR )
            {
                scene->screen_vertices_x[i] = WIDGET_MODEL_CLIP_X;
                scene->screen_vertices_y[i] = 0;
                scene->screen_vertices_z[i] = cz;
                continue;
            }

            px = (cx * xf->zoom3d) / cz;
            py = (cy * xf->zoom3d) / cz;
        }

        scene->screen_vertices_x[i] = px + dx - (sw >> 1);
        scene->screen_vertices_y[i] = py + dy - (sh >> 1);
        scene->screen_vertices_z[i] = cz;
        z_sum += cz;
        z_count++;
    }

    int model_mid_z = z_count > 0 ? z_sum / z_count : 0;
    for( int i = 0; i < vc; i++ )
        scene->screen_vertices_z[i] -= model_mid_z;

    return z_count > 0;
}

bool
ToriDraw_RenderModelExtentsAtWidget(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int orientation,
    int model_offset,
    int model_center_y,
    bool orthographic,
    bool fixed_zoom,
    toripixel_t* pixels,
    int stride,
    int canvas_w,
    int canvas_h,
    int widget_x,
    int widget_y,
    int widget_w,
    int widget_h,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom,
    int* out_draw_x,
    int* out_draw_y,
    int* out_width,
    int* out_height)
{
    assert(scene);
    assert(hnd.kind == TORIDRAWMK_MODEL);
    assert(pixels);
    assert(out_draw_x);
    assert(out_draw_y);
    assert(out_width);
    assert(out_height);

    if( zoom <= 0 )
        zoom = 2000;

    struct WidgetModelTransform xf;
    widget_model_init_transform(
        &xf,
        zoom,
        xan,
        yan,
        zan,
        orientation,
        model_offset,
        model_center_y,
        orthographic,
        fixed_zoom);

    int sw = 0;
    int sh = 0;
    int draw_x = 0;
    int draw_y = 0;

    scene->active_hnd = hnd;

    if( !orthographic )
    {
        int origin_x = widget_x + (widget_w > 0 ? widget_w / 2 : 0);
        int origin_y = widget_y + (widget_h > 0 ? widget_h / 2 : 0);
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;

        if( !widget_model_project_vertices_objrender(
                scene, hnd, &xf, origin_x, origin_y, &min_x, &min_y, &max_x, &max_y) )
            return false;

        /* ToriDraw_Raster adds view_port.width/2 and height/2 to vertex coords. */
        int vc = ToriDraw_ModelGetVertexCount(hnd);
        int raster_origin_x = canvas_w / 2;
        int raster_origin_y = canvas_h / 2;
        for( int i = 0; i < vc; i++ )
        {
            scene->screen_vertices_x[i] -= raster_origin_x;
            scene->screen_vertices_y[i] -= raster_origin_y;
        }

        draw_x = min_x;
        draw_y = min_y;
        sw = max_x - min_x + 1;
        sh = max_y - min_y + 1;
        if( sw <= 0 || sh <= 0 )
            return false;
    }
    else
    {
        int blit_offset_x = 0;
        int blit_offset_y = 0;
        if( !widget_model_project_bounds(hnd, &xf, &sw, &sh, &blit_offset_x, &blit_offset_y) )
            return false;

        int bw = widget_w > 0 ? widget_w : sw;
        int bh = widget_h > 0 ? widget_h : sh;
        int origin_x = widget_x + (bw / 2) - blit_offset_x;
        int origin_y = widget_y + (bh / 2) - blit_offset_y;

        if( !widget_model_project_vertices(scene, hnd, &xf, sw, sh, blit_offset_x, blit_offset_y) )
            return false;

        int vc = ToriDraw_ModelGetVertexCount(hnd);
        for( int i = 0; i < vc; i++ )
        {
            scene->screen_vertices_x[i] += origin_x;
            scene->screen_vertices_y[i] += origin_y;
        }

        /* ToriDraw_Raster adds view_port.width/2 and height/2 to vertex coords. */
        int raster_origin_x = canvas_w / 2;
        int raster_origin_y = canvas_h / 2;
        for( int i = 0; i < vc; i++ )
        {
            scene->screen_vertices_x[i] -= raster_origin_x;
            scene->screen_vertices_y[i] -= raster_origin_y;
        }

        draw_x = widget_x + (bw / 2) - blit_offset_x;
        draw_y = widget_y + (bh / 2) - blit_offset_y;
    }

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = canvas_w;
    view_port.height = canvas_h;
    view_port.clip_left = clip_left;
    view_port.clip_top = clip_top;
    view_port.clip_right = clip_right;
    view_port.clip_bottom = clip_bottom;
    view_port.stride = stride;

    struct ToriDraw_Camera camera = { 0 };
    camera.near_plane_z = WIDGET_MODEL_NEAR;
    camera.fov_rpi2048 = 512;

    ToriDraw_RenderModel2SortFaces(hnd, scene);
    ToriDraw_RenderModel3Raster(scene, &view_port, &camera, pixels, false);

    *out_draw_x = draw_x;
    *out_draw_y = draw_y;
    *out_width = sw;
    *out_height = sh;
    return true;
}

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

struct ToriDraw_ModelExtentsRaster
ToriDraw_SpriteNewFromModelRasterExtents(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int zoom,
    int xan,
    int yan,
    int zan,
    int offset_x,
    int offset_y,
    bool orthographic,
    bool postprocess_outline)
{
    struct ToriDraw_ModelExtentsRaster result = { 0 };

    assert(scene);
    assert(hnd.kind == TORIDRAWMK_MODEL);

    if( zoom <= 0 )
        zoom = 2000;

    struct WidgetModelTransform xf;
    widget_model_init_transform(
        &xf, zoom, xan, yan, zan, offset_x, offset_y, 0, orthographic, false);

    int width = 0;
    int height = 0;
    int blit_offset_x = 0;
    int blit_offset_y = 0;
    if( !widget_model_project_bounds(hnd, &xf, &width, &height, &blit_offset_x, &blit_offset_y) )
        return result;

    scene->active_hnd = hnd;
    if( !widget_model_project_vertices(
            scene, hnd, &xf, width, height, blit_offset_x, blit_offset_y) )
        return result;

    struct ToriDraw_ViewPort view_port = { 0 };
    view_port.width = width;
    view_port.height = height;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = width;
    view_port.clip_bottom = height;
    view_port.stride = width;

    struct ToriDraw_Camera camera = { 0 };
    camera.near_plane_z = WIDGET_MODEL_NEAR;
    camera.fov_rpi2048 = 512;

    size_t pixel_count = (size_t)width * (size_t)height;
    toripixel_t* pixels = calloc(pixel_count, sizeof(toripixel_t));
    if( !pixels )
        return result;

    ToriDraw_RenderModel2SortFaces(hnd, scene);
    ToriDraw_RenderModel3Raster(scene, &view_port, &camera, pixels, false);

    uint32_t* argb = malloc(pixel_count * sizeof(uint32_t));
    if( !argb )
    {
        free(pixels);
        return result;
    }

    for( size_t i = 0; i < pixel_count; i++ )
    {
        uint32_t rgb = (uint32_t)pixels[i];
        argb[i] = (rgb & 0xFFFFFFu) ? (rgb | 0xFF000000u) : 0u;
    }

    free(pixels);

    if( postprocess_outline )
        ToriDraw_SpritePostprocessObjIconOutline(argb, width, height);

    result.sprite = ToriDraw_SpriteNewFromArgbOwned(argb, width, height);
    result.offset_x = blit_offset_x;
    result.offset_y = blit_offset_y;
    return result;
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
