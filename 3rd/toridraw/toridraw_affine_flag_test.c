/*
 * Behaviour contract for TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES.
 *
 * The flag is a per-model request that textured faces take the AFFINE texture
 * walk -- u and v derived at each span's two ends and stepped linearly between
 * them -- in place of the perspective walk that re-derives them every eight
 * pixels. Terrain sets it (world_decode_tile), and every lane is supposed to
 * honour it: the per-face C kernels through tri.texture_affine.u.c, the
 * presorted-run assembly kernels through the affine lane of the staged row.
 *
 * Everything here drives the real pipeline -- ToriDraw_RenderModel1Project /
 * 2SortFaces / 3Raster on a hand-built model -- rather than the kernels, because
 * what can go wrong is the wiring: whether the flag reaches the raster context,
 * whether the batched walk stages the lane, whether the kernel reads it.
 *
 * What is asserted, on a textured quad that recedes ALONG its spans (a wall
 * seen at an angle, so depth varies across every scanline and the two walks
 * genuinely disagree):
 *
 *   - the flag draws EXACTLY what the camera's own texture_affine draws. That
 *     route existed before the flag and is the definition of "the affine
 *     family"; the flag must reach the same kernels, not a third answer.
 *   - the flag CHANGES the picture against the perspective walk. Without this
 *     negative control the first assertion would pass for a flag nothing reads.
 *   - coverage is unchanged: the same pixels are touched. Affine changes which
 *     texel a pixel samples, never the silhouette.
 *
 * Both the gouraud-shaded and the flat-shaded textured dispatches are checked,
 * since they are separate kernels and separate rows. The batched walk is
 * exercised where the lane has run kernels (the default arm) and the per-face
 * C path where it does not; run once with TORIDRAW_RASTER_BATCH=0 to force the
 * latter on a lane that has both -- the makefile target does both.
 *
 * Build and run:
 *   make -C src test-affine-flag
 */

#include "toridraw.h"
#include <assert.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 192
#define VIEW_H 192
#define BACKGROUND ((toripixel_t)0)

#define CAMERA_DISTANCE 900
#define EXTENT 200

static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while( 0 )

/* --- model construction --------------------------------------------------- */

#define MAX_VERTICES 8
#define MAX_FACES 8

struct Mesh
{
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle hnd;

    vertexint_t vx[MAX_VERTICES];
    vertexint_t vy[MAX_VERTICES];
    vertexint_t vz[MAX_VERTICES];
    faceint_t fa[MAX_FACES];
    faceint_t fb[MAX_FACES];
    faceint_t fc[MAX_FACES];
    hsl16_t ca[MAX_FACES];
    hsl16_t cb[MAX_FACES];
    hsl16_t cc[MAX_FACES];
    int infos[MAX_FACES];
    faceint_t textures[MAX_FACES];
};

static void
mesh_init(struct Mesh* m)
{
    memset(m, 0, sizeof(*m));

    m->model.vertices_x = m->vx;
    m->model.vertices_y = m->vy;
    m->model.vertices_z = m->vz;
    m->model.face_indices_a = m->fa;
    m->model.face_indices_b = m->fb;
    m->model.face_indices_c = m->fc;
    m->model.face_colors_a = m->ca;
    m->model.face_colors_b = m->cb;
    m->model.face_colors_c = m->cc;
    m->model.face_infos = m->infos;
    m->model.has_bounds_cylinder = true;

    m->model.bounds_cylinder.radius = EXTENT * 2;
    m->model.bounds_cylinder.min_y = -EXTENT;
    m->model.bounds_cylinder.max_y = EXTENT;
    m->model.bounds_cylinder.center_to_top_edge = EXTENT * 3;
    m->model.bounds_cylinder.center_to_bottom_edge = EXTENT * 3;
    m->model.bounds_cylinder.min_z_depth_any_rotation = EXTENT * 3;

    m->hnd.kind = TORIDRAWMK_MODEL;
    m->hnd.u.model.model = &m->model;
}

static int
mesh_add_vertex(struct Mesh* m, int x, int y, int z)
{
    int const at = m->model.vertex_count;

    assert(at < MAX_VERTICES);
    m->vx[at] = (vertexint_t)x;
    m->vy[at] = (vertexint_t)y;
    m->vz[at] = (vertexint_t)z;
    m->model.vertex_count = at + 1;
    return at;
}

/* Textured faces carry a 0..127 lightness, not an hsl16 colour; 64 shades the
 * texel to half so the blend is exercised, and TORIDRAWHSL16_FLAT in colors_c
 * selects the flat-shaded textured dispatch, a separate kernel. */
#define TEX_SHADE 64

static int
mesh_add_face(struct Mesh* m, int a, int b, int c, int texture_id, bool flat_shaded)
{
    int const at = m->model.face_count;

    assert(at < MAX_FACES);
    m->fa[at] = (faceint_t)a;
    m->fb[at] = (faceint_t)b;
    m->fc[at] = (faceint_t)c;
    m->ca[at] = TEX_SHADE;
    m->cb[at] = TEX_SHADE;
    m->cc[at] = flat_shaded ? TORIDRAWHSL16_FLAT : TEX_SHADE;
    m->infos[at] = 0;
    m->textures[at] = (faceint_t)texture_id;
    m->model.face_count = at + 1;
    return at;
}

/**
 * A textured quad that RECEDES ALONG ITS SPANS: its left edge is nearer the eye
 * than its right edge, so every scanline crosses from near to far and the
 * perspective and affine walks genuinely disagree. (A floor seen head-on has
 * constant depth along each span, where the two agree exactly -- the scanline
 * parity test's own observation -- and would prove nothing here.)
 *
 * Both windings are emitted, as the z-buffer test does, so the quad is visible
 * whichever the raster culls. face_texture_coords stays NULL: the face's own
 * a/b/c are the uv frame, which is what a model with no PNM triple does.
 */
static void
mesh_build_receding_wall(struct Mesh* m, int texture_id, bool flat_shaded)
{
    int const near_z = -EXTENT;
    int const far_z = EXTENT * 2;
    int v0;
    int v1;
    int v2;
    int v3;

    mesh_init(m);
    v0 = mesh_add_vertex(m, -EXTENT, -EXTENT * 3 / 4, near_z);
    v1 = mesh_add_vertex(m, EXTENT, -EXTENT * 3 / 4, far_z);
    v2 = mesh_add_vertex(m, EXTENT, EXTENT * 3 / 4, far_z);
    v3 = mesh_add_vertex(m, -EXTENT, EXTENT * 3 / 4, near_z);

    mesh_add_face(m, v0, v1, v2, texture_id, flat_shaded);
    mesh_add_face(m, v0, v2, v3, texture_id, flat_shaded);
    mesh_add_face(m, v0, v2, v1, texture_id, flat_shaded);
    mesh_add_face(m, v0, v3, v2, texture_id, flat_shaded);

    m->model.face_textures = m->textures;
    m->model.textured_face_count = 1;
}

/* --- textures ------------------------------------------------------------- */

#define TEX_WIDTH 64
#define TEX_ID 3

/**
 * A texture with structure at every scale: a four-texel checker so that a
 * one-texel disagreement between the walks is a different colour, on top of a
 * per-texel gradient so that no two texels of a row are equal. Opaque, so the
 * colour key never turns a sampling difference into a coverage difference.
 */
static struct ToriDraw_Texture*
make_checker_texture(void)
{
    struct ToriDraw_Texture* tex = (struct ToriDraw_Texture*)calloc(1, sizeof(*tex));

    assert(tex);
    tex->texels = (int*)malloc((size_t)TEX_WIDTH * TEX_WIDTH * sizeof(int));
    assert(tex->texels);
    for( int v = 0; v < TEX_WIDTH; v++ )
        for( int u = 0; u < TEX_WIDTH; u++ )
        {
            int const checker = ((u >> 2) ^ (v >> 2)) & 1;
            int const base = checker ? 0x00C04020 : 0x002040C0;
            tex->texels[v * TEX_WIDTH + u] = base + (u << 8) + v + 1;
        }
    tex->width = TEX_WIDTH;
    tex->height = TEX_WIDTH;
    tex->opaque = true;
    return tex;
}

/* --- rendering ------------------------------------------------------------ */

struct Frame
{
    toripixel_t pixels[VIEW_W * VIEW_H];
};

static struct ToriDraw_ViewPort
viewport(void)
{
    struct ToriDraw_ViewPort vp = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    return vp;
}

static struct ToriDraw_Camera
camera(int texture_affine)
{
    struct ToriDraw_Camera cam = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
        .texture_affine = texture_affine,
    };
    return cam;
}

static bool
render_model(
    struct ToriDraw_Scene* scene,
    struct Mesh* mesh,
    struct Frame* frame,
    uint8_t model_flags,
    int camera_texture_affine)
{
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera(camera_texture_affine);
    struct ToriDraw_Position pos = { .x = 0, .y = 0, .z = CAMERA_DISTANCE };

    memset(frame->pixels, 0, sizeof(frame->pixels));
    mesh->model.flags = model_flags;

    if( ToriDraw_RenderModel1Project(mesh->hnd, scene, &pos, &vp, &cam) !=
        TORIDRAW_CULL_VISIBLE )
        return false;
    if( ToriDraw_RenderModel2SortFaces(mesh->hnd, scene) <= 0 )
        return false;
    ToriDraw_RenderModel3Raster(scene, &vp, &cam, frame->pixels, false);
    return true;
}

static long
frame_drawn(const struct Frame* f)
{
    long n = 0;

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( f->pixels[i] != BACKGROUND )
            n++;
    return n;
}

static long
frame_differences(const struct Frame* a, const struct Frame* b)
{
    long n = 0;

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( a->pixels[i] != b->pixels[i] )
            n++;
    return n;
}

static long
frame_coverage_differences(const struct Frame* a, const struct Frame* b)
{
    long n = 0;

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( (a->pixels[i] != BACKGROUND) != (b->pixels[i] != BACKGROUND) )
            n++;
    return n;
}

/* --- the contract --------------------------------------------------------- */

static void
test_flag_routes_to_the_affine_walk(struct ToriDraw_Scene* scene, bool flat_shaded)
{
    const char* const which = flat_shaded ? "flat-shaded" : "gouraud-shaded";
    struct Mesh mesh;
    struct Frame perspective;
    struct Frame flagged;
    struct Frame camera_affine;
    long drawn;
    long changed;
    long coverage;

    mesh_build_receding_wall(&mesh, TEX_ID, flat_shaded);

    CHECK(
        render_model(scene, &mesh, &perspective, 0, 0),
        "%s wall was culled or empty on the perspective walk",
        which);
    CHECK(
        render_model(scene, &mesh, &flagged, TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES, 0),
        "%s wall was culled or empty with the flag set",
        which);
    CHECK(
        render_model(scene, &mesh, &camera_affine, 0, 1),
        "%s wall was culled or empty under the camera's texture_affine",
        which);

    drawn = frame_drawn(&perspective);
    CHECK(drawn > 4000, "%s wall covered only %ld pixels; the fixture is too small", which, drawn);

    /* The flag IS the camera's affine route, pixel for pixel. */
    CHECK(
        frame_differences(&flagged, &camera_affine) == 0,
        "%s: the flag drew %ld pixels differently from camera.texture_affine -- "
        "it reached a different kernel",
        which,
        frame_differences(&flagged, &camera_affine));

    /* And it is not the perspective walk: the negative control. On a wall
     * receding across its spans the two walks sample different texels over a
     * large share of the face. */
    changed = frame_differences(&perspective, &flagged);
    CHECK(
        changed > drawn / 20,
        "%s: the flag changed only %ld of %ld drawn pixels against the perspective "
        "walk -- nothing read it",
        which,
        changed,
        drawn);

    /* Same silhouette. The one coverage difference the affine family owns is a
     * span whose two edges coincide in 16.16, which it skips and the
     * perspective walk draws as a single pixel; a handful of those along the
     * edges is that, not a walk gone wrong. */
    coverage = frame_coverage_differences(&perspective, &flagged);
    CHECK(
        coverage <= drawn / 200,
        "%s: the affine walk covered %ld pixels differently from the perspective one",
        which,
        coverage);
}

int
main(void)
{
    struct ToriDraw_Scene* scene;

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    ToriDraw_SceneSetTexture(scene, TEX_ID, make_checker_texture());

    test_flag_routes_to_the_affine_walk(scene, false);
    test_flag_routes_to_the_affine_walk(scene, true);

    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "affine flag: %d failure(s)\n", failures);
        return 1;
    }
    printf("affine flag: all checks passed\n");
    return 0;
}
