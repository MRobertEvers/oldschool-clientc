/*
 * The rev-241 depth-buffered renderer (the rszbuffered tables).
 *
 * Two halves.
 *
 * THE RASTERIZER AGAINST THE CLIENT. The triangle setups and spans in
 * impl/raster/rszbuf/ are ports of class162's, and the only honest grade of a
 * port is the original. tools/rszbuf_oracle/make_oracle.py copies those method
 * bodies verbatim out of a rev-241 deob into a standalone Java program, runs
 * them over a deterministic stream of triangles -- flat, gouraud with and
 * without the 4-pixel colour blocks, perspective and linear textures, opaque
 * and colour-keyed, every face alpha class, both write masks, clamped and
 * unclamped spans, fractional and grid-aligned corners -- and writes one hash
 * per 64 cases into toridraw_rszbuf_golden.h. This replays the same stream
 * here: every group must match.
 *
 * THE WALK THROUGH THE REAL TABLES. What the client decides around the
 * rasterizer: which faces each scene pass draws, the two-pass mode's colour
 * then depth, render-priority order, the per-face depth bias, the near clip,
 * and a tile's own rules. Driven through ToriDraw_RenderModelWithTable on a
 * hand-built model inside a frame-wide pass, as Soft3D drives it.
 *
 * Build and run:
 *   make -C src test-rszbuf
 */
#include "graphics/pixel_format.h"
#include "graphics/shared_tables.h"
#include "impl/raster/dispatch/tri.rszbuf.u.c"
#include "toridraw.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include "toridraw_rszbuf_golden.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ------------------------------------------------- the oracle's stream */

/* Oracle.java's buffer, and its generator: xorshift32, every value drawn in
 * the same order. Changing either side means regenerating the goldens. */
#define ORACLE_W 240
#define ORACLE_H 180

static uint32_t g_oracle_state;

static int32_t
oracle_next(void)
{
    g_oracle_state ^= g_oracle_state << 13;
    g_oracle_state ^= g_oracle_state >> 17;
    g_oracle_state ^= g_oracle_state << 5;
    return (int32_t)g_oracle_state;
}

static int
oracle_rand(int n)
{
    return (oracle_next() & 0x7fffffff) % n;
}

static uint32_t
oracle_fold(uint32_t h, uint32_t v)
{
    return (h ^ v) * 16777619u;
}

static void
test_rasterizer_matches_the_client(void)
{
    RSZBUF_NO_FMA
    static toripixel_t palette[65536];
    static int texels[128 * 128];
    static int keyed[128 * 128];
    static toripixel_t pixels[ORACLE_W * ORACLE_H];
    static float zbuffer[ORACLE_W * ORACLE_H];
    uint32_t group_hash = 0x811C9DC5u;
    int mismatched_groups = 0;
    int first_bad_group = -1;
    struct ToriDraw_RsZbufRaster r = {
        .pixels = pixels,
        .zbuffer = zbuffer,
        .stride = ORACLE_W,
        .clip_width = ORACLE_W,
        .clip_height = ORACLE_H,
        .center_x = ORACLE_W / 2,
        .center_y = ORACLE_H / 2,
        .gouraud_blocks4 = true,
        .color_write_mask = -1,
        .depth_write_mask = -1,
        .projection_scale = 512,
        .palette = palette,
    };

    g_oracle_state = 0x2545F491u;
    for( int i = 0; i < 65536; i++ )
        palette[i] = (toripixel_t)(((uint32_t)i * 0x9E3779B1u ^ ((uint32_t)i << 7)) & 0xFFFFFFu);
    for( int i = 0; i < 128 * 128; i++ )
        texels[i] = oracle_next() & 0xFFFFFF;
    for( int i = 0; i < 128 * 128; i++ )
        keyed[i] = (i % 7 == 0) ? 0 : texels[i];

    for( int c = 0; c < RSZBUF_GOLDEN_CASES; c++ )
    {
        for( int i = 0; i < ORACLE_W * ORACLE_H; i++ )
        {
            pixels[i] = (toripixel_t)(((uint32_t)i * 0x9E3779B1u) ^ (uint32_t)c);
            zbuffer[i] =
                (oracle_rand(4) == 0) ? 0.0f : ((float)((i * 7919 + c) % 1000) / 700.0f + 1.0f);
        }
        int const kind = oracle_rand(4);
        r.gouraud_blocks4 = oracle_rand(2) == 0;
        r.color_write_mask = oracle_rand(5) == 0 ? 0 : -1;
        r.depth_write_mask = oracle_rand(5) == 0 ? 0 : -1;
        int const alpha_class = oracle_rand(6);
        r.face_alpha = alpha_class < 3 ? 0 : (alpha_class == 3 ? 254 : oracle_rand(253) + 1);

        float ys[3];
        float xs[3];
        float keys[3];
        int const grid = oracle_rand(3);
        for( int v = 0; v < 3; v++ )
        {
            if( grid == 0 )
            {
                int const yi = oracle_rand(ORACLE_H + 80) - 40;
                ys[v] = (float)yi + (float)oracle_rand(4) / 4.0f;
                int const xi = oracle_rand(ORACLE_W + 80) - 40;
                xs[v] = (float)xi + (float)oracle_rand(8) / 8.0f;
            }
            else if( grid == 1 )
            {
                ys[v] = (float)(oracle_rand(ORACLE_H + 80) - 40);
                xs[v] = (float)(oracle_rand(ORACLE_W + 80) - 40);
            }
            else
            {
                ys[v] = (float)((oracle_rand(12) - 1) * 20);
                xs[v] = (float)((oracle_rand(16) - 1) * 20);
            }
            keys[v] = rszbuf_depth_key((float)(60 + oracle_rand(4000)));
        }
        r.clip_x = xs[0] < 0 || xs[1] < 0 || xs[2] < 0 || xs[0] > ORACLE_W || xs[1] > ORACLE_W ||
                   xs[2] > ORACLE_W;

        if( kind == 0 )
        {
            int const rgb = oracle_next() & 0xFFFFFF;
            rszbuf_triangle_flat(
                &r, ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], keys[0], keys[1], keys[2], rgb);
        }
        else if( kind == 1 )
        {
            int const color_a = oracle_rand(65536);
            int const color_b = oracle_rand(65536);
            int const color_c = oracle_rand(65536);
            rszbuf_triangle_gouraud(
                &r, ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], keys[0], keys[1], keys[2], color_a,
                color_b, color_c);
        }
        else
        {
            bool const opaque = oracle_rand(2) == 0;
            struct ToriDraw_RsZbufTextureFrame frame;
            for( int i = 0; i < 3; i++ )
            {
                frame.x[i] = oracle_rand(1000) - 500;
                frame.y[i] = oracle_rand(1000) - 500;
                frame.z[i] = 100 + oracle_rand(2000);
            }
            int const shade_a = oracle_rand(128);
            int const shade_b = oracle_rand(128);
            int const shade_c = oracle_rand(128);
            if( kind == 2 )
                rszbuf_triangle_texture(
                    &r, ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], keys[0], keys[1], keys[2],
                    shade_a, shade_b, shade_c, &frame, opaque ? texels : keyed, opaque);
            else
                rszbuf_triangle_texture_linear(
                    &r, ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], keys[0], keys[1], keys[2],
                    shade_a, shade_b, shade_c, &frame, opaque ? texels : keyed, opaque);
        }

        uint32_t pixel_hash = 0x811C9DC5u;
        uint32_t depth_hash = 0x811C9DC5u;
        for( int i = 0; i < ORACLE_W * ORACLE_H; i++ )
        {
            pixel_hash = oracle_fold(pixel_hash, (uint32_t)pixels[i]);
            depth_hash = oracle_fold(depth_hash, (uint32_t)rszbuf_float_bits(zbuffer[i]));
        }
        group_hash = oracle_fold(oracle_fold(group_hash, pixel_hash), depth_hash);

        if( c % RSZBUF_GOLDEN_GROUP == RSZBUF_GOLDEN_GROUP - 1 || c == RSZBUF_GOLDEN_CASES - 1 )
        {
            int const group = c / RSZBUF_GOLDEN_GROUP;
            if( group_hash != g_rszbuf_golden[group] )
            {
                mismatched_groups++;
                if( first_bad_group < 0 )
                    first_bad_group = group;
            }
            group_hash = 0x811C9DC5u;
        }
    }

    CHECK(
        mismatched_groups == 0,
        "%d of %d groups of the client's stream differ; the first is cases %d..%d",
        mismatched_groups, (int)(sizeof(g_rszbuf_golden) / sizeof(g_rszbuf_golden[0])),
        first_bad_group * RSZBUF_GOLDEN_GROUP, first_bad_group * RSZBUF_GOLDEN_GROUP + 63);
}

/* ----------------------------------------------------------- the walk */

#define VIEW_W 256
#define VIEW_H 192
#define MAX_VERTICES 32
#define MAX_FACES 32

#define HSL_BACK 0x2266
#define HSL_FRONT 0x7744
#define HSL_GLASS 0xA855

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
    alphaint_t alpha[MAX_FACES];
    uint8_t priorities[(MAX_FACES + 1) / 2];
    int8_t bias[MAX_FACES];
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
    m->hnd.kind = TORIDRAWMK_MODEL;
    m->hnd.u.model.model = &m->model;
}

static int
mesh_vertex(struct Mesh* m, int x, int y, int z)
{
    int const at = m->model.vertex_count++;

    assert(at < MAX_VERTICES);
    m->vx[at] = (vertexint_t)x;
    m->vy[at] = (vertexint_t)y;
    m->vz[at] = (vertexint_t)z;
    return at;
}

/** A flat face, so a drawn opaque pixel is exactly palette[hsl]. */
static int
mesh_face(struct Mesh* m, int a, int b, int c, int hsl, int alpha)
{
    int const at = m->model.face_count++;

    assert(at < MAX_FACES);
    m->fa[at] = (faceint_t)a;
    m->fb[at] = (faceint_t)b;
    m->fc[at] = (faceint_t)c;
    m->ca[at] = (hsl16_t)hsl;
    m->cb[at] = (hsl16_t)hsl;
    m->cc[at] = TORIDRAWHSL16_FLAT;
    m->alpha[at] = (alphaint_t)alpha;
    return at;
}

/**
 * A square facing the camera at camera-space depth `z`, `half` wide, both
 * windings: the client culls one, and which depends on the convention, which
 * this need not encode. Returns the first of its four faces.
 */
static int
mesh_square(struct Mesh* m, int half, int z, int hsl, int alpha)
{
    int const v0 = mesh_vertex(m, -half, -half, z);
    int const v1 = mesh_vertex(m, half, -half, z);
    int const v2 = mesh_vertex(m, half, half, z);
    int const v3 = mesh_vertex(m, -half, half, z);
    int const first = mesh_face(m, v0, v1, v2, hsl, alpha);

    mesh_face(m, v0, v2, v3, hsl, alpha);
    mesh_face(m, v0, v2, v1, hsl, alpha);
    mesh_face(m, v0, v3, v2, hsl, alpha);
    return first;
}

static void
mesh_set_priority(struct Mesh* m, int first_face, int count, int band)
{
    m->model.face_priorities = m->priorities;
    for( int f = first_face; f < first_face + count; f++ )
    {
        uint8_t* const byte = &m->priorities[f >> 1];
        int const shift = (f & 1) * 4;
        *byte = (uint8_t)((*byte & ~(0x0F << shift)) | (band << shift));
    }
}

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
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    return vp;
}

static struct ToriDraw_Camera
camera(void)
{
    struct ToriDraw_Camera cam = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
    };
    return cam;
}

/** The two passes, over `count` models, as Soft3D runs them. */
static void
render_scene(
    struct ToriDraw_Scene* scene,
    struct Mesh** meshes,
    const int* depth,
    int count,
    struct Frame* frame,
    bool translucent_pass)
{
    const struct ToriDraw_Kernel* const opaque =
        ToriDraw_KernelTake(scene, ToriDraw_KernelGetSoftwareRsZBufferedOpaque());
    const struct ToriDraw_Kernel* const translucent =
        ToriDraw_KernelTake(scene, ToriDraw_KernelGetSoftwareRsZBufferedTranslucent());
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();

    memset(frame->pixels, 0, sizeof(frame->pixels));
    ToriDraw_SceneZBufferBeginPass(scene, &cam, VIEW_W, VIEW_H);
    for( int pass = 0; pass < (translucent_pass ? 2 : 1); pass++ )
        for( int i = 0; i < count; i++ )
        {
            struct ToriDraw_Position pos = { .z = depth[i] };
            ToriDraw_RenderModelWithTable(
                meshes[i]->hnd, scene, &pos, &vp, &cam, frame->pixels,
                pass == 0 ? opaque : translucent);
        }
    ToriDraw_SceneZBufferEndPass(scene);
}

static toripixel_t
rgb(int hsl)
{
    return g_hsl16_to_pixel_table[hsl];
}

static toripixel_t
centre(const struct Frame* f)
{
    return f->pixels[(VIEW_H / 2) * VIEW_W + VIEW_W / 2];
}

static long
count_rgb(const struct Frame* f, toripixel_t value)
{
    long n = 0;

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( (f->pixels[i] & 0xFFFFFF) == (value & 0xFFFFFF) )
            n++;
    return n;
}

/**
 * The opaque pass draws no face with alpha and the translucent pass draws
 * nothing else; the glass in front then blends over the wall it did not hide.
 */
static void
test_passes_split_faces_by_alpha(struct ToriDraw_Scene* scene)
{
    struct Mesh wall;
    struct Mesh glass;
    struct Mesh* meshes[2] = { &wall, &glass };
    int const depth[2] = { 900, 600 };
    struct Frame opaque_only;
    struct Frame both;

    mesh_init(&wall);
    mesh_square(&wall, 200, 0, HSL_BACK, 0);
    mesh_init(&glass);
    mesh_square(&glass, 60, 0, HSL_GLASS, 128);
    glass.model.face_alphas = glass.alpha;

    render_scene(scene, meshes, depth, 2, &opaque_only, false);
    render_scene(scene, meshes, depth, 2, &both, true);

    CHECK(
        centre(&opaque_only) == rgb(HSL_BACK),
        "the opaque pass drew 0x%06x at the centre, not the wall: a face with alpha was "
        "drawn there",
        centre(&opaque_only) & 0xFFFFFF);
    CHECK(
        centre(&both) != rgb(HSL_BACK) && centre(&both) != rgb(HSL_GLASS),
        "after the translucent pass the centre is 0x%06x: the glass did not blend over the "
        "wall",
        centre(&both) & 0xFFFFFF);
    CHECK(
        count_rgb(&both, rgb(HSL_BACK)) > 1000,
        "the translucent pass redrew the wall: only %ld wall pixels are left",
        count_rgb(&both, rgb(HSL_BACK)));
}

/** Glass behind a wall is rejected by the depth the opaque pass wrote. */
static void
test_translucent_faces_test_depth(struct ToriDraw_Scene* scene)
{
    struct Mesh wall;
    struct Mesh glass;
    struct Mesh* meshes[2] = { &wall, &glass };
    int const depth[2] = { 600, 900 };
    struct Frame frame;

    mesh_init(&wall);
    mesh_square(&wall, 120, 0, HSL_BACK, 0);
    mesh_init(&glass);
    mesh_square(&glass, 60, 0, HSL_GLASS, 128);
    glass.model.face_alphas = glass.alpha;

    render_scene(scene, meshes, depth, 2, &frame, true);
    CHECK(
        centre(&frame) == rgb(HSL_BACK),
        "glass behind the wall showed through it (centre 0x%06x)", centre(&frame) & 0xFFFFFF);
}

/**
 * Two coplanar squares. With depth written as it goes (model order), the
 * first wins the tie. A two-pass model draws colour with depth writes off, in
 * render-priority order, so the square in the higher band is what shows,
 * wherever it sits in index order.
 */
static void
test_two_pass_colour_in_priority_order(struct ToriDraw_Scene* scene)
{
    struct Frame frame;

    for( int high_first = 0; high_first < 2; high_first++ )
    {
        struct Mesh body;
        struct Mesh* meshes[1] = { &body };
        int const depth[1] = { 600 };
        int const first_hsl = high_first ? HSL_FRONT : HSL_BACK;
        int const second_hsl = high_first ? HSL_BACK : HSL_FRONT;

        mesh_init(&body);
        int const first = mesh_square(&body, 80, 0, first_hsl, 0);
        int const second = mesh_square(&body, 80, 0, second_hsl, 0);
        mesh_set_priority(&body, first, 4, high_first ? 5 : 2);
        mesh_set_priority(&body, second, 4, high_first ? 2 : 5);

        body.model.flags = 0;
        render_scene(scene, meshes, depth, 1, &frame, false);
        CHECK(
            centre(&frame) == rgb(first_hsl),
            "model order: the first coplanar square should win the tie, got 0x%06x",
            centre(&frame) & 0xFFFFFF);

        body.model.flags = TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS;
        render_scene(scene, meshes, depth, 1, &frame, false);
        CHECK(
            centre(&frame) == rgb(HSL_FRONT),
            "two-pass, band-5 square %s in index order: it should be drawn last, got 0x%06x",
            high_first ? "first" : "second", centre(&frame) & 0xFFFFFF);
    }
}

/**
 * A two-pass model's faces are not filtered by pass before the draw
 * [method5800 skips it for that mode]; each face is, as it is drawn. Its glass
 * must not show in the opaque pass.
 */
static void
test_two_pass_filters_faces_by_pass(struct ToriDraw_Scene* scene)
{
    struct Mesh body;
    struct Mesh* meshes[1] = { &body };
    int const depth[1] = { 600 };
    struct Frame frame;

    mesh_init(&body);
    mesh_square(&body, 120, 40, HSL_BACK, 0);
    mesh_square(&body, 60, 0, HSL_GLASS, 128);
    body.model.face_alphas = body.alpha;
    body.model.flags = TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS;

    render_scene(scene, meshes, depth, 1, &frame, false);
    CHECK(
        centre(&frame) == rgb(HSL_BACK),
        "the opaque pass drew a two-pass model's glass (centre 0x%06x)",
        centre(&frame) & 0xFFFFFF);
    render_scene(scene, meshes, depth, 1, &frame, true);
    CHECK(
        centre(&frame) != rgb(HSL_BACK) && centre(&frame) != rgb(HSL_GLASS),
        "the translucent pass did not blend a two-pass model's glass (centre 0x%06x)",
        centre(&frame) & 0xFFFFFF);
}

/**
 * The write masks outlive a draw [class175's fields]. A two-pass model ends
 * them back on colour and depth; a model sets its own before it draws, but a
 * tile does not, so a tile drawn next writes through whatever was left -- a
 * tile behind a two-pass model must still lose to the depth it wrote, and must
 * still have colour.
 */
static void
test_two_pass_leaves_masks_on(struct ToriDraw_Scene* scene)
{
    struct Mesh body;
    struct Mesh tile;
    struct Mesh* meshes[2] = { &body, &tile };
    int const depth[2] = { 600, 1500 };
    struct Frame frame;

    mesh_init(&body);
    mesh_square(&body, 60, 0, HSL_FRONT, 0);
    body.model.flags = TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS;
    mesh_init(&tile);
    mesh_square(&tile, 600, 0, HSL_GLASS, 0);
    tile.model.flags = TORIDRAW_MODEL_FLAG_TERRAIN;

    render_scene(scene, meshes, depth, 2, &frame, false);
    CHECK(
        centre(&frame) == rgb(HSL_FRONT),
        "the tile behind the two-pass model overdrew it (centre 0x%06x): its depth "
        "was not written",
        centre(&frame) & 0xFFFFFF);
    CHECK(
        count_rgb(&frame, rgb(HSL_GLASS)) > 1000,
        "the tile drew only %ld pixels: the two-pass model left colour writes off",
        count_rgb(&frame, rgb(HSL_GLASS)));
}

/** A positive bias pulls a face toward the eye: of two coplanar faces, the
 *  biased second one wins the tie the first would otherwise keep. */
static void
test_depth_bias_breaks_ties(struct ToriDraw_Scene* scene)
{
    struct Mesh body;
    struct Mesh* meshes[1] = { &body };
    int const depth[1] = { 600 };
    struct Frame unbiased;
    struct Frame biased;

    mesh_init(&body);
    mesh_square(&body, 80, 0, HSL_BACK, 0);
    int const second = mesh_square(&body, 80, 0, HSL_FRONT, 0);

    render_scene(scene, meshes, depth, 1, &unbiased, false);
    for( int f = second; f < second + 4; f++ )
        body.bias[f] = 10;
    body.model.face_z_offsets = body.bias;
    render_scene(scene, meshes, depth, 1, &biased, false);

    CHECK(
        centre(&unbiased) == rgb(HSL_BACK),
        "control: without a bias the first coplanar face should win, got 0x%06x",
        centre(&unbiased) & 0xFFFFFF);
    CHECK(
        centre(&biased) == rgb(HSL_FRONT),
        "the biased face did not come forward (centre 0x%06x)", centre(&biased) & 0xFFFFFF);
}

/**
 * A floor running from behind the eye to far in front of it: every face has a
 * corner behind the near plane, so all of it goes through the clip. Drawn, it
 * fills the bottom of the view; culled or dropped, it is absent.
 */
static void
test_near_clip_draws_the_part_in_front(struct ToriDraw_Scene* scene)
{
    struct Mesh floor;
    struct Mesh* meshes[1] = { &floor };
    int const depth[1] = { 300 };
    struct Frame frame;
    long bottom = 0;

    mesh_init(&floor);
    int const v0 = mesh_vertex(&floor, -600, 150, -700);
    int const v1 = mesh_vertex(&floor, 600, 150, -700);
    int const v2 = mesh_vertex(&floor, 600, 150, 900);
    int const v3 = mesh_vertex(&floor, -600, 150, 900);
    mesh_face(&floor, v0, v1, v2, HSL_BACK, 0);
    mesh_face(&floor, v0, v2, v3, HSL_BACK, 0);
    mesh_face(&floor, v0, v2, v1, HSL_BACK, 0);
    mesh_face(&floor, v0, v3, v2, HSL_BACK, 0);

    render_scene(scene, meshes, depth, 1, &frame, false);
    for( int x = 0; x < VIEW_W; x++ )
        if( (frame.pixels[(VIEW_H - 2) * VIEW_W + x] & 0xFFFFFF) == (rgb(HSL_BACK) & 0xFFFFFF) )
            bottom++;
    CHECK(
        bottom > VIEW_W / 2,
        "the near-clipped floor covers only %ld of the bottom row's %d pixels", bottom, VIEW_W);
}

/** A tile with a corner behind the near plane is skipped whole; one in front
 *  draws in the opaque pass and not again in the translucent one. */
static void
test_terrain_is_never_clipped(struct ToriDraw_Scene* scene)
{
    struct Mesh tile;
    struct Mesh* meshes[1] = { &tile };
    int const in_front[1] = { 300 };
    struct Frame frame;

    mesh_init(&tile);
    int const v0 = mesh_vertex(&tile, -600, 150, -700);
    int const v1 = mesh_vertex(&tile, 600, 150, -700);
    int const v2 = mesh_vertex(&tile, 600, 150, 900);
    int const v3 = mesh_vertex(&tile, -600, 150, 900);
    mesh_face(&tile, v0, v1, v2, HSL_BACK, 0);
    mesh_face(&tile, v0, v2, v3, HSL_BACK, 0);
    mesh_face(&tile, v0, v2, v1, HSL_BACK, 0);
    mesh_face(&tile, v0, v3, v2, HSL_BACK, 0);
    tile.model.flags = TORIDRAW_MODEL_FLAG_TERRAIN;

    render_scene(scene, meshes, in_front, 1, &frame, true);
    CHECK(
        count_rgb(&frame, rgb(HSL_BACK)) == 0,
        "a tile reaching behind the near plane drew %ld pixels; the client skips it",
        count_rgb(&frame, rgb(HSL_BACK)));

    int const far[1] = { 1500 };
    render_scene(scene, meshes, far, 1, &frame, true);
    CHECK(
        count_rgb(&frame, rgb(HSL_BACK)) > 1000,
        "control: the same tile wholly in front drew only %ld pixels",
        count_rgb(&frame, rgb(HSL_BACK)));
}

int
main(void)
{
    struct ToriDraw_Scene* scene;

    if( !TORIPIXEL_IS_XRGB8888 )
    {
        printf("toridraw_rszbuf_test: skipped (pixel format %s; the renderer is xrgb8888's)\n",
               TORIPIXEL_FORMAT_NAME);
        return 0;
    }

    ToriDraw_Init();
    test_rasterizer_matches_the_client();

    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_MODEL_ZBUFFER, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    test_passes_split_faces_by_alpha(scene);
    test_translucent_faces_test_depth(scene);
    test_two_pass_colour_in_priority_order(scene);
    test_two_pass_filters_faces_by_pass(scene);
    test_two_pass_leaves_masks_on(scene);
    test_depth_bias_breaks_ties(scene);
    test_near_clip_draws_the_part_in_front(scene);
    test_terrain_is_never_clipped(scene);
    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("toridraw_rszbuf_test: all checks passed\n");
    return 0;
}
