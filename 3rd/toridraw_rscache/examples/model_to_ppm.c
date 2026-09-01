/*
 * The whole small-client path, end to end, in one file:
 *
 *     a model blob  ->  decode  ->  convert  ->  light  ->  raster  ->  a PPM
 *
 *     cc -I3rd/toridraw_rscache/include -I3rd/toridraw \
 *        -I3rd/rscache/include -I3rd/rscache/src \
 *        -DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565 \
 *        examples/model_to_ppm.c toridraw_rscache_unity.o toridraw_unity.o \
 *        rscache_unity.o ... -lm -o model_to_ppm
 *
 *     ./model_to_ppm model.dat out.ppm 128 512
 *
 * The point of the example is the memory: ONE static arena, sized from the
 * model that was actually decoded, and no allocation at all between the view
 * being built and the frame being written. Everything else here -- argument
 * parsing, reading a file, writing a PPM -- is scaffolding.
 *
 * PPM because it is six lines to write and every viewer reads it. A real
 * client hands `framebuffer` to its panel instead.
 */

#include <toridraw_rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The one buffer. 64 KB covers a model of roughly 700 vertices and 1,400
 * faces at a typical depth extent -- comfortably more than any item or npc
 * model. ToriDraw_MiniViewBytes says what the decoded model actually needs and
 * this refuses to run rather than overrunning.
 */
static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t g_arena[64 * 1024];

#define CANVAS_MAX 512
static toripixel_t g_framebuffer[CANVAS_MAX * CANVAS_MAX];

static uint8_t*
read_file(
    const char* path,
    int* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* data;
    long size;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t)size);
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (int)size;
    return data;
}

static int
write_ppm(
    const char* path,
    const toripixel_t* pixels,
    int width,
    int height)
{
    FILE* f = fopen(path, "wb");
    int i;

    if( !f )
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for( i = 0; i < width * height; i++ )
    {
        /* toripixel_to_argb8888 is the inverse of the pack, and exists for
         * exactly this: code that has to look at a finished pixel rather than
         * produce one. It is lossy on the 16-bit formats -- five bits cannot
         * carry eight -- and replicates the high bits so a saturated channel
         * comes back saturated. */
        uint32_t argb = toripixel_to_argb8888(pixels[i]);
        uint8_t rgb[3];

        rgb[0] = (uint8_t)((argb >> 16) & 0xFF);
        rgb[1] = (uint8_t)((argb >> 8) & 0xFF);
        rgb[2] = (uint8_t)(argb & 0xFF);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    const char* in_path;
    const char* out_path;
    int canvas = 128;
    int yaw = 0;
    uint8_t* blob;
    int blob_size = 0;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_MiniLimits limits;
    struct ToriDraw_MiniView* view;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    size_t want;

    if( argc < 3 )
    {
        fprintf(stderr, "usage: %s <model.dat> <out.ppm> [canvas] [yaw]\n", argv[0]);
        return 2;
    }
    in_path = argv[1];
    out_path = argv[2];
    if( argc > 3 )
        canvas = atoi(argv[3]);
    if( argc > 4 )
        yaw = atoi(argv[4]);

    if( canvas <= 0 || canvas > CANVAS_MAX )
    {
        fprintf(stderr, "canvas must be 1..%d\n", CANVAS_MAX);
        return 2;
    }

    /* The palette and the trigonometric tables are process-wide, not per
     * view, and every draw reads them. */
    ToriDraw_Init();

    blob = read_file(in_path, &blob_size);
    if( !blob )
    {
        fprintf(stderr, "cannot read %s\n", in_path);
        return 1;
    }

    model = ToriDraw_RSCacheModelFromBlob(blob, blob_size);
    free(blob);
    if( !model )
    {
        fprintf(stderr, "%s does not decode as a model\n", in_path);
        return 1;
    }

    /* Un-lit, the model draws black: face_colors_a/b/c are what the raster
     * interpolates and they are separate from the flat colours the cache
     * carries. NULL selects the reference client's own rig. */
    ToriDraw_RSCacheModelLight(model, NULL);

    hnd = ToriDraw_ModelHandleOwned(model);

    ToriDraw_MiniLimitsForModel(hnd, &limits);
    want = ToriDraw_MiniViewBytes(&limits);
    fprintf(
        stderr,
        "%s: %d vertices, %d faces, depth %d, textures %s -> %zu byte view\n",
        in_path,
        limits.scene.max_vertices,
        limits.scene.max_faces,
        limits.scene.depth_levels,
        limits.scene.textures ? "yes" : "no",
        want);

    if( want > sizeof(g_arena) )
    {
        /* Refuse rather than overrun: the arena is a fixed buffer and this is
         * the one place that can tell. */
        fprintf(
            stderr,
            "model needs %zu bytes and the arena is %zu; rebuild with a larger one\n",
            want,
            sizeof(g_arena));
        ToriDraw_ModelHandleFree(hnd);
        return 1;
    }

    view = ToriDraw_MiniViewInit(g_arena, sizeof(g_arena), &limits);

    target.pixels = g_framebuffer;
    target.width = canvas;
    target.height = canvas;
    target.stride = canvas;

    /* Nothing below this line allocates. */
    ToriDraw_MiniClear(&target, toripixel_pack_argb8888(0));
    pose.yaw = yaw;
    if( !ToriDraw_MiniDrawModel(view, hnd, &target, &pose) )
    {
        fprintf(stderr, "model projected to nothing at this pose\n");
        ToriDraw_ModelHandleFree(hnd);
        return 1;
    }

    if( write_ppm(out_path, g_framebuffer, canvas, canvas) != 0 )
    {
        fprintf(stderr, "cannot write %s\n", out_path);
        ToriDraw_ModelHandleFree(hnd);
        return 1;
    }

    fprintf(stderr, "wrote %s (%dx%d, %s)\n", out_path, canvas, canvas, TORIPIXEL_FORMAT_NAME);
    ToriDraw_ModelHandleFree(hnd);
    return 0;
}
