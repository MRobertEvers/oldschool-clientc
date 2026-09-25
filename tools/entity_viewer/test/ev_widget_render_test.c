/*
 * The browser widget export must be a thin ABI over the native widget raster,
 * not a second camera implementation.  Compare its RGBA output pixel-for-pixel
 * with a direct ToriDraw_RenderModelExtentsAtWidget call for perspective,
 * orthographic/fixed-zoom, and CC_SETOBJECT-composed cases.
 *
 * Standalone and cache-free:
 *   make -C tools/entity_viewer ev_widget_render_test
 *   tools/entity_viewer/ev_widget_render_test
 */
#include "ev_render.h"
#include "ev_wire.h"
#include "toridraw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS_W 128
#define CANVAS_H 96

static int g_failed = 0;

#define CHECK(expr)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(expr) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                      \
            g_failed = 1;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct ToriDraw_Model*
make_test_model(void)
{
    struct ToriDraw_Model* model = ToriDraw_ModelNew(4, 2, 0);
    CHECK(model);

    model->vertices_x = calloc(4, sizeof(*model->vertices_x));
    model->vertices_y = calloc(4, sizeof(*model->vertices_y));
    model->vertices_z = calloc(4, sizeof(*model->vertices_z));
    model->face_indices_a = calloc(2, sizeof(*model->face_indices_a));
    model->face_indices_b = calloc(2, sizeof(*model->face_indices_b));
    model->face_indices_c = calloc(2, sizeof(*model->face_indices_c));
    model->face_colors_a = calloc(2, sizeof(*model->face_colors_a));
    model->face_colors_b = calloc(2, sizeof(*model->face_colors_b));
    model->face_colors_c = calloc(2, sizeof(*model->face_colors_c));
    CHECK(
        model->vertices_x && model->vertices_y && model->vertices_z && model->face_indices_a &&
        model->face_indices_b && model->face_indices_c && model->face_colors_a &&
        model->face_colors_b && model->face_colors_c);

    /* A slightly non-planar quad makes every angle/projection argument visible
     * in the resulting pixels.  Negative Y also gives object composition a
     * non-zero bounds-centering term. */
    model->vertices_x[0] = -18;
    model->vertices_y[0] = -28;
    model->vertices_z[0] = -5;
    model->vertices_x[1] = 20;
    model->vertices_y[1] = -24;
    model->vertices_z[1] = 3;
    model->vertices_x[2] = 17;
    model->vertices_y[2] = 14;
    model->vertices_z[2] = 7;
    model->vertices_x[3] = -16;
    model->vertices_y[3] = 12;
    model->vertices_z[3] = -2;

    /* Clockwise in projected screen coordinates for the widget raster. */
    model->face_indices_a[0] = 0;
    model->face_indices_b[0] = 2;
    model->face_indices_c[0] = 1;
    model->face_indices_a[1] = 0;
    model->face_indices_b[1] = 3;
    model->face_indices_c[1] = 2;

    for( int i = 0; i < 2; i++ )
    {
        model->face_colors_a[i] = (hsl16_t)(0x4B40 + i * 0x80);
        model->face_colors_b[i] = model->face_colors_a[i];
        model->face_colors_c[i] = model->face_colors_a[i];
    }

    ToriDraw_ModelCaptureOriginalVertices(model);
    ToriDraw_ModelSetBoundsCylinder(model);
    return model;
}

static void
compare_case(
    const char* name,
    struct ToriDraw_Model* reference_model,
    int widget_x,
    int widget_y,
    int widget_w,
    int widget_h,
    int zoom,
    int xan,
    int yan,
    int zan,
    int x_offset,
    int y_offset,
    int orthographic,
    int fixed_zoom,
    int object_composed,
    int frame)
{
    uint8_t const* got = ev_render_widget(
        CANVAS_W,
        CANVAS_H,
        widget_x,
        widget_y,
        widget_w,
        widget_h,
        zoom,
        xan,
        yan,
        zan,
        x_offset,
        y_offset,
        orthographic,
        fixed_zoom,
        object_composed,
        frame);
    CHECK(got);
    if( !got )
        return;

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    CHECK(scene);
    if( !scene )
        return;

    toripixel_t* expected = calloc((size_t)CANVAS_W * CANVAS_H, sizeof(*expected));
    CHECK(expected);
    if( !expected )
    {
        ToriDraw_SceneFree(scene);
        return;
    }

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = reference_model,
    };
    int model_zoom = zoom;
    int center_y = 0;
    if( object_composed )
    {
        int const box = widget_w < widget_h ? widget_w : widget_h;
        if( box > 0 )
            model_zoom = model_zoom * 32 / box;
        struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
        if( bounds )
            center_y = -bounds->min_y / 2;
    }
    if( model_zoom <= 0 )
        model_zoom = 2000;

    int dx = 0;
    int dy = 0;
    int out_w = 0;
    int out_h = 0;
    CHECK(ToriDraw_RenderModelExtentsAtWidget(
        scene,
        hnd,
        model_zoom,
        xan,
        yan,
        zan,
        x_offset,
        y_offset,
        center_y,
        orthographic != 0,
        fixed_zoom != 0,
        expected,
        CANVAS_W,
        CANVAS_W,
        CANVAS_H,
        widget_x,
        widget_y,
        widget_w,
        widget_h,
        0,
        0,
        CANVAS_W,
        CANVAS_H,
        &dx,
        &dy,
        &out_w,
        &out_h));

    int drawn = 0;
    int mismatches = 0;
    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
    {
        uint32_t const rgb = (uint32_t)expected[i] & 0x00FFFFFFu;
        uint8_t const want[4] = {
            (uint8_t)((rgb >> 16) & 0xFF),
            (uint8_t)((rgb >> 8) & 0xFF),
            (uint8_t)(rgb & 0xFF),
            rgb ? 0xFF : 0,
        };
        if( rgb )
            drawn++;
        if( memcmp(got + i * 4, want, sizeof(want)) != 0 )
            mismatches++;
    }

    if( mismatches )
        fprintf(stderr, "FAIL %s: %d RGBA pixel mismatches\n", name, mismatches);
    if( drawn <= 0 )
        fprintf(stderr, "FAIL %s: reference raster drew no pixels\n", name);
    CHECK(mismatches == 0);
    CHECK(drawn > 0);
    CHECK(got[3] == 0); /* untouched top-left stays transparent */

    free(expected);
    ToriDraw_SceneFree(scene);
}

static int
rgba_over_channel(int source, int alpha, int destination)
{
    return (source * alpha + destination * (255 - alpha) + 127) / 255;
}

/* A translucent widget must be a reusable source-over layer.  Rendering its
 * faces against black and marking that result opaque (the old browser bridge)
 * turns the whole quad nearly black; the native client instead blends it over
 * whatever the preceding interface models painted. */
static void
compare_translucent_over_background(
    struct ToriDraw_Model* reference_model,
    uint32_t background)
{
    enum { WX = 38, WY = 14, WW = 56, WH = 64 };
    uint8_t const* layer = ev_render_widget(
        CANVAS_W, CANVAS_H, WX, WY, WW, WH,
        420, 128, 256, 0, 3, -4, 0, 0, 0, -1);
    CHECK(layer);
    if( !layer )
        return;

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    CHECK(scene);
    toripixel_t* expected = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(*expected));
    CHECK(expected);
    if( !scene || !expected )
    {
        free(expected);
        ToriDraw_SceneFree(scene);
        return;
    }
    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
        expected[i] = (toripixel_t)(background & 0x00FFFFFFu);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = reference_model,
    };
    int dx = 0, dy = 0, out_w = 0, out_h = 0;
    CHECK(ToriDraw_RenderModelExtentsAtWidget(
        scene, hnd, 420, 128, 256, 0, 3, -4, 0,
        false, false, expected, CANVAS_W, CANVAS_W, CANVAS_H,
        WX, WY, WW, WH, 0, 0, CANVAS_W, CANVAS_H,
        &dx, &dy, &out_w, &out_h));

    int partial = 0;
    int compared = 0;
    int max_error = 0;
    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
    {
        int const a = layer[i * 4 + 3];
        if( a > 0 && a < 255 )
            partial++;
        int const dr = (int)((background >> 16) & 0xFF);
        int const dg = (int)((background >> 8) & 0xFF);
        int const db = (int)(background & 0xFF);
        int const got[3] = {
            rgba_over_channel(layer[i * 4 + 0], a, dr),
            rgba_over_channel(layer[i * 4 + 1], a, dg),
            rgba_over_channel(layer[i * 4 + 2], a, db),
        };
        int const want[3] = {
            (int)(((uint32_t)expected[i] >> 16) & 0xFF),
            (int)(((uint32_t)expected[i] >> 8) & 0xFF),
            (int)((uint32_t)expected[i] & 0xFF),
        };
        for( int c = 0; c < 3; c++ )
        {
            int error = got[c] - want[c];
            if( error < 0 ) error = -error;
            if( error > max_error ) max_error = error;
        }
        compared++;
    }
    if( max_error > 2 )
        fprintf(stderr, "FAIL translucent source-over: max channel error %d\n", max_error);
    CHECK(compared == CANVAS_W * CANVAS_H);
    CHECK(partial > 0);
    CHECK(max_error <= 2);

    free(expected);
    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    ToriDraw_Init();

    struct ToriDraw_Model* model = make_test_model();
    struct EV_WireBuf wire = { 0 };
    CHECK(ev_wire_write_model(&wire, model));

    ev_init();
    CHECK(ev_set_model(wire.data, (int)wire.len) == model->face_count);

    compare_case("perspective", model, 38, 14, 56, 64, 420, 128, 256, 0, 3, -4, 0, 0, 0, -1);
    compare_case(
        "orthographic fixed zoom", model, 21, 8, 72, 70, 128, 128, 128, 64, -5, 7, 1, 1, 0, 7);
    compare_case(
        "object composed", model, 44, 18, 40, 64, 390, 150, 96, 45, 2, -3, 0, 0, 1, -1);
    compare_case(
        "object composed default zoom", model, 44, 18, 40, 64, 0, 150, 96, 45, 2, -3, 0, 0, 1, -1);

    /* Cache alpha is transparency: 128 leaves a genuine partial-coverage
     * layer which must reconstruct the native draw over a non-black backdrop. */
    ev_wire_free(&wire);
    model->face_alphas = calloc((size_t)model->face_count, sizeof(*model->face_alphas));
    CHECK(model->face_alphas);
    for( int i = 0; i < model->face_count; i++ )
        model->face_alphas[i] = 128;
    ToriDraw_ModelCaptureOriginalVertices(model);
    CHECK(ev_wire_write_model(&wire, model));
    CHECK(ev_set_model(wire.data, (int)wire.len) == model->face_count);
    compare_translucent_over_background(model, 0x00806A4Cu);

    ev_wire_free(&wire);
    ToriDraw_ModelFree(model);

    if( g_failed )
    {
        fprintf(stderr, "ev_widget_render_test: FAILED\n");
        return 1;
    }
    printf("ev_widget_render_test: PASS\n");
    return 0;
}
