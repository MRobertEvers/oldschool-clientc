/*
 * A/B probe: the widget-model raster, native, on the exact inputs the wasm
 * build receives.
 *
 * The cs2dom pixel-parity harness found rotated widget models differing from
 * the C client while identity-pose models matched byte-for-byte, and every
 * intermediate suspect (SIMD lanes, libm sine tables) checked out equal. This
 * probe closes the remaining gap: it drives ev_set_model / ev_render_widget —
 * the same entry points the browser worker calls into ev_wasm — compiled
 * natively, and writes the raw RGBA out. Comparing that file against the
 * worker's bytes answers, in one diff, whether the divergence is
 * native-vs-wasm codegen or something upstream in how each side is fed.
 *
 *   ev_widget_ab_probe <model.wire> <out.rgba> <canvasW> <canvasH>
 *       <widgetX> <widgetY> <widgetW> <widgetH> <zoom> <xan> <yan> <zan>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ev_render.h"

static uint8_t*
read_file(const char* path, size_t* out_len)
{
    assert(path);
    assert(out_len);
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size >= 0);
    uint8_t* data = malloc((size_t)size);
    assert(data);
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        fprintf(stderr, "short read on %s\n", path);
        exit(2);
    }
    fclose(file);
    *out_len = (size_t)size;
    return data;
}

int
main(int argc, char** argv)
{
    if( argc != 13 )
    {
        fprintf(
            stderr,
            "usage: %s <model.wire> <out.rgba> <canvasW> <canvasH> "
            "<widgetX> <widgetY> <widgetW> <widgetH> <zoom> <xan> <yan> <zan>\n",
            argv[0]);
        return 2;
    }

    size_t model_len = 0;
    uint8_t* model = read_file(argv[1], &model_len);
    int const canvas_w = atoi(argv[3]);
    int const canvas_h = atoi(argv[4]);
    int const widget_x = atoi(argv[5]);
    int const widget_y = atoi(argv[6]);
    int const widget_w = atoi(argv[7]);
    int const widget_h = atoi(argv[8]);
    int const zoom = atoi(argv[9]);
    int const xan = atoi(argv[10]);
    int const yan = atoi(argv[11]);
    int const zan = atoi(argv[12]);

    ev_init();
    if( !ev_set_model(model, (int)model_len) )
    {
        fprintf(stderr, "ev_set_model rejected %s\n", argv[1]);
        return 2;
    }

    uint8_t* rgba = ev_render_widget(
        canvas_w,
        canvas_h,
        widget_x,
        widget_y,
        widget_w,
        widget_h,
        zoom,
        xan,
        yan,
        zan,
        0,  /* x_offset */
        0,  /* y_offset */
        0,  /* orthographic */
        0,  /* fixed_zoom */
        0,  /* object_composed */
        -1 /* frame: no animation */);
    if( !rgba )
    {
        fprintf(stderr, "ev_render_widget produced nothing\n");
        return 2;
    }

    FILE* out = fopen(argv[2], "wb");
    assert(out);
    size_t const pixel_bytes = (size_t)canvas_w * (size_t)canvas_h * 4;
    if( fwrite(rgba, 1, pixel_bytes, out) != pixel_bytes )
    {
        fprintf(stderr, "short write on %s\n", argv[2]);
        return 2;
    }
    fclose(out);
    printf("wrote %s (%dx%d)\n", argv[2], canvas_w, canvas_h);
    free(model);
    return 0;
}
