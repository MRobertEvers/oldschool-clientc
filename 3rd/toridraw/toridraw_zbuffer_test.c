/*
 * Behaviour contract for the depth-tested raster family.
 *
 * The painter's sort orders whole faces, so it cannot express "these two
 * triangles each occlude the other" — the case a model whose parts
 * interpenetrate is made of. TORIDRAW_MODEL_FLAG_ZBUFFER routes such a model
 * through graphics/raster/zbuffer/zbuf.screen.u.c instead, which resolves it per
 * pixel against the scene's z-buffer after resetting it.
 *
 * Everything here drives the real pipeline — ToriDraw_RenderModel1Project /
 * 2SortFaces / 3Raster on a hand-built model — rather than calling the kernels
 * directly, because most of what can go wrong lives in the wiring: which depth
 * the key is taken of, whether the buffer was reset, whether the model flag
 * reaches the raster at all. A kernel-level test would have passed through every
 * defect found while writing this.
 *
 * What is asserted:
 *
 *   - two interpenetrating quads resolve to the nearer surface at every pixel,
 *     and the painter's sort demonstrably does NOT (the negative control: a
 *     test whose "before" already passes is measuring nothing)
 *   - the result is INDEPENDENT of the order faces are drawn in. This is the
 *     load-bearing property, and the only one that grades the depth arithmetic
 *     without a second implementation to compare against: reversing the face
 *     order changes every occlusion decision and must change no pixel.
 *   - coverage is unchanged. A depth-tested face must touch exactly the pixels
 *     its stock twin touched, or the flag alters silhouettes and seams it was
 *     only meant to reorder.
 *   - depth is perspective-correct. A long quad receding from the eye crosses
 *     another at a place that linear-in-screen-space depth gets wrong, and the
 *     crossing has to land where the geometry says.
 *   - opaque faces write depth; translucent ones test but do not, so a
 *     back-to-front blend still composites in order.
 *   - the buffer is reset per model, so one model's depths cannot reject
 *     another's pixels, and a model that does not opt in is unaffected.
 *
 * Build and run:
 *   make -C src test-zbuffer
 *
 * Or standalone:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw -o /tmp/zbuffer_test \
 *      3rd/toridraw/toridraw_zbuffer_test.c build/toridraw_unity.o \
 *      build/toridraw_font.o build/bmp.o -lm
 *
 * Negative controls run against this file. Each mutation was applied to the
 * engine, the test run, and the mutation reverted:
 *
 *   | mutation                                      | caught by                |
 *   |-----------------------------------------------|--------------------------|
 *   | depth key linear in z rather than 1/z          | crossing, order, crossing|
 *   |                                                | position (4 checks)      |
 *   | translucent faces also write depth             | translucency             |
 *   | the per-model reset made a no-op               | reset, coverage,         |
 *   |                                                | translucency             |
 *
 * Two of the fixtures also carry their control inline rather than in the table,
 * because the property they assert is only evidence if the stock renderer fails
 * it: the crossing tests require the painter's sort to get the crossing WRONG,
 * and the order-independence test requires reversing the order to change the
 * picture when the depth buffer is off. Both assert that in the same run.
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

/* Far enough that the whole model sits well inside the frame and no vertex can
 * approach the near plane; the near-clip path is the near_clip test's subject. */
#define CAMERA_DISTANCE 900

/* Half-extent of every quad below, in model units. */
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

#define MAX_VERTICES 64
#define MAX_FACES 64

struct Mesh
{
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_BoundsCylinder bounds;

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
    int infos[MAX_FACES];
    uint8_t priorities[MAX_FACES];
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
    m->model.bounds_cylinder = &m->bounds;

    /* A sphere about the origin that contains every vertex any test builds, so
     * the cull and the near-clip gate see a model shaped like the geometry. */
    m->bounds.radius = EXTENT * 2;
    m->bounds.min_y = -EXTENT;
    m->bounds.max_y = EXTENT;
    m->bounds.center_to_top_edge = EXTENT * 3;
    m->bounds.center_to_bottom_edge = EXTENT * 3;
    m->bounds.min_z_depth_any_rotation = EXTENT * 3;

    m->hnd.kind = TORIDRAWMK_MODEL;
    m->hnd.u.model.model = &m->model;
}

static int
mesh_add_vertex(struct Mesh* m, int x, int y, int z)
{
    int const at = m->model.vertex_count;

    if( at >= MAX_VERTICES )
    {
        fprintf(stderr, "mesh_add_vertex: out of room\n");
        exit(2);
    }
    m->vx[at] = (vertexint_t)x;
    m->vy[at] = (vertexint_t)y;
    m->vz[at] = (vertexint_t)z;
    m->model.vertex_count = at + 1;
    return at;
}

/**
 * A flat-shaded triangle. Flat rather than gouraud so a drawn pixel has one
 * exact expected colour: g_hsl16_to_rgb_table[hsl], with no interpolation to
 * reason about. The selector for flat shading is TORIDRAWHSL16_FLAT in
 * colors_c, which is how the model decoder spells it.
 */
static int
mesh_add_face(struct Mesh* m, int a, int b, int c, int hsl, int face_alpha)
{
    int const at = m->model.face_count;

    if( at >= MAX_FACES )
    {
        fprintf(stderr, "mesh_add_face: out of room\n");
        exit(2);
    }
    m->fa[at] = (faceint_t)a;
    m->fb[at] = (faceint_t)b;
    m->fc[at] = (faceint_t)c;
    m->ca[at] = (hsl16_t)hsl;
    m->cb[at] = (hsl16_t)hsl;
    m->cc[at] = TORIDRAWHSL16_FLAT;
    m->alpha[at] = (alphaint_t)face_alpha;
    m->infos[at] = 0;
    m->model.face_count = at + 1;
    return at;
}

/**
 * Both windings of a quad.
 *
 * The raster culls one screen-space winding, and which one a given quad lands
 * on depends on the camera. Emitting both makes every quad here visible without
 * the test having to encode the winding convention — and it costs nothing, since
 * the two copies are coincident and the same colour, so whichever is culled the
 * surviving one paints the same pixels.
 */
static void
mesh_add_quad(struct Mesh* m, int v0, int v1, int v2, int v3, int hsl, int face_alpha)
{
    mesh_add_face(m, v0, v1, v2, hsl, face_alpha);
    mesh_add_face(m, v0, v2, v3, hsl, face_alpha);
    mesh_add_face(m, v0, v2, v1, hsl, face_alpha);
    mesh_add_face(m, v0, v3, v2, hsl, face_alpha);
}

/** Translucent faces need the alpha array bound; opaque ones must not have it,
 *  because the raster reads 0xFF - alpha and an unset array would hide them. */
static void
mesh_enable_alpha(struct Mesh* m)
{
    m->model.face_alphas = m->alpha;
}

/* --- textured meshes ------------------------------------------------------ */

#define TEX_WIDTH 64
#define TEX_ID_LEFT 3
#define TEX_ID_RIGHT 4
#define TEX_RGB_LEFT 0x00FF4000
#define TEX_RGB_RIGHT 0x000040FF
/* shade_blend(base, 256) == base, and the textured kernels carry the shade as a
 * 0..127 lightness that they double, so 128 is the identity. */
#define TEX_SHADE 128

static faceint_t g_face_textures[MAX_FACES];

/**
 * A texture whose every texel is one colour.
 *
 * The point of this test is depth, not filtering: with a constant texture a
 * drawn pixel has one exact expected value, so a wrong pixel means the wrong
 * SURFACE won rather than the right surface sampling slightly differently. The
 * uv mapping is exercised by toridraw_texture_span_uv_test.
 */
static struct ToriDraw_Texture*
make_flat_texture(int rgb)
{
    struct ToriDraw_Texture* tex = (struct ToriDraw_Texture*)calloc(1, sizeof(*tex));

    assert(tex);
    tex->texels = (int*)malloc((size_t)TEX_WIDTH * TEX_WIDTH * sizeof(int));
    assert(tex->texels);
    for( int i = 0; i < TEX_WIDTH * TEX_WIDTH; i++ )
        tex->texels[i] = rgb;
    tex->width = TEX_WIDTH;
    tex->height = TEX_WIDTH;
    tex->opaque = true;
    return tex;
}

/**
 * Retag every face as textured.
 *
 * face_texture_coords stays NULL so the kernels take the face's own a/b/c as the
 * uv origin/u-end/v-end, which is what a model with no separate PNM triple does.
 * textured_face_count must be non-zero: it is what makes the projection retain
 * the camera-space vertices the perspective uv basis is built from.
 */
static void
mesh_make_textured(struct Mesh* m, int texture_id, bool flat_shaded)
{
    for( int f = 0; f < m->model.face_count; f++ )
    {
        g_face_textures[f] = (faceint_t)texture_id;
        /* Textured faces carry a 0..127 lightness here, not an hsl16 colour.
         * TORIDRAWHSL16_FLAT in colors_c selects the flat-shaded textured
         * kernel, which is a separate dispatch taking one lightness for the
         * whole face rather than three. */
        m->ca[f] = TEX_SHADE;
        m->cb[f] = TEX_SHADE;
        m->cc[f] = flat_shaded ? TORIDRAWHSL16_FLAT : TEX_SHADE;
    }
    m->model.face_textures = g_face_textures;
    m->model.textured_face_count = 1;
}

/** Two textured faces cannot share the module-level texture id array, so a mesh
 *  that needs two textures gets its own. */
static void
mesh_set_face_texture(struct Mesh* m, int face, int texture_id)
{
    m->model.face_textures[face] = (faceint_t)texture_id;
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
camera(void)
{
    struct ToriDraw_Camera cam = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .near_plane_z = 50,
    };
    return cam;
}

static struct ToriDraw_Position
position(void)
{
    struct ToriDraw_Position pos = { .x = 0, .y = 0, .z = CAMERA_DISTANCE };
    return pos;
}

enum RenderOrder
{
    ORDER_SORTED,
    ORDER_REVERSED,
};

/**
 * Project, sort, raster. `reverse` flips the face order the sorter produced,
 * which is the whole point of the order-independence check: same projection,
 * same faces, every occlusion decision made in the opposite sequence.
 */
static bool
render_model(
    struct ToriDraw_Scene* scene,
    struct Mesh* mesh,
    struct Frame* frame,
    bool zbuffered,
    enum RenderOrder order)
{
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();
    struct ToriDraw_Position pos = position();
    int count;

    mesh->model.flags = (uint8_t)(zbuffered ? TORIDRAW_MODEL_FLAG_ZBUFFER : 0);

    if( ToriDraw_RenderModel1Project(mesh->hnd, scene, &pos, &vp, &cam) !=
        TORIDRAW_CULL_VISIBLE )
        return false;

    count = ToriDraw_RenderModel2SortFaces(mesh->hnd, scene);
    if( count <= 0 )
        return false;

    if( order == ORDER_REVERSED )
    {
        int* face_order = ToriDraw_FaceOrder(scene);

        for( int i = 0, j = count - 1; i < j; i++, j-- )
        {
            int const t = face_order[i];
            face_order[i] = face_order[j];
            face_order[j] = t;
        }
    }

    ToriDraw_RenderModel3Raster(scene, &vp, &cam, frame->pixels, false);
    return true;
}

static void
frame_clear(struct Frame* f)
{
    memset(f->pixels, 0, sizeof(f->pixels));
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

/**
 * Differences, ignoring a vertical band about the frame's centre column.
 *
 * The crossing fixture's two quads meet exactly there, at equal depth. A depth
 * test resolves "nearer", and at a tie there is no nearer — the kernels keep
 * whatever is already in the buffer (strictly-greater wins), which is what stops
 * the shared edge between two faces of one model from fighting, and which makes
 * the tie itself order-dependent. That is a property of ties, not a defect, so
 * the order-independence assertion is made everywhere else and the tie is
 * counted separately.
 */
static long
frame_differences_outside_band(const struct Frame* a, const struct Frame* b, int margin)
{
    long n = 0;

    for( int y = 0; y < VIEW_H; y++ )
        for( int x = 0; x < VIEW_W; x++ )
        {
            int const dx = x - VIEW_W / 2;

            if( dx > -margin && dx < margin )
                continue;
            if( a->pixels[y * VIEW_W + x] != b->pixels[y * VIEW_W + x] )
                n++;
        }
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

/* --- the fixtures --------------------------------------------------------- */

#define HSL_NEAR_LEFT 5000
#define HSL_NEAR_RIGHT 40000

static int
rgb_of(int hsl)
{
    return (int)g_hsl16_to_rgb_table[hsl];
}

/**
 * What a textured pixel must come out as.
 *
 * Independent of graphics/shade.h on purpose: asserting against the same
 * expression the kernel evaluates would pass whatever that expression became.
 * The kernels double the stored 0..127 lightness, so TEX_SHADE == 128 makes the
 * multiplier 256 — an exact 8.8 identity, hence the texel unchanged.
 */
static int
shade_blend_reference(int texel_rgb)
{
    return texel_rgb & 0x00FFFFFF;
}

/**
 * Two quads crossing like an X, seen edge-on from the front.
 *
 * Quad A runs from near-left to far-right, quad B from near-right to far-left,
 * and they intersect on the vertical line x == 0. So the correct picture is
 * quad A on the left half of the overlap and quad B on the right half — and no
 * ordering of whole faces can produce it, because each quad is partly in front
 * of the other.
 */
static void
build_crossing_quads(struct Mesh* m)
{
    mesh_init(m);

    {
        int const a0 = mesh_add_vertex(m, -EXTENT, -EXTENT, -EXTENT);
        int const a1 = mesh_add_vertex(m, EXTENT, -EXTENT, EXTENT);
        int const a2 = mesh_add_vertex(m, EXTENT, EXTENT, EXTENT);
        int const a3 = mesh_add_vertex(m, -EXTENT, EXTENT, -EXTENT);

        mesh_add_quad(m, a0, a1, a2, a3, HSL_NEAR_LEFT, 0);
    }
    {
        int const b0 = mesh_add_vertex(m, EXTENT, -EXTENT, -EXTENT);
        int const b1 = mesh_add_vertex(m, -EXTENT, -EXTENT, EXTENT);
        int const b2 = mesh_add_vertex(m, -EXTENT, EXTENT, EXTENT);
        int const b3 = mesh_add_vertex(m, EXTENT, EXTENT, -EXTENT);

        mesh_add_quad(m, b0, b1, b2, b3, HSL_NEAR_RIGHT, 0);
    }
}

/**
 * How many pixels show the wrong quad.
 *
 * `margin` skips a band either side of the crossing line, where the two
 * surfaces are within a fraction of a unit of each other and either answer is
 * defensible. Everywhere else the near surface is unambiguous.
 */
static void
count_crossing_errors(
    const struct Frame* f,
    int margin,
    long* out_wrong,
    long* out_checked)
{
    int const rgb_left = rgb_of(HSL_NEAR_LEFT);
    int const rgb_right = rgb_of(HSL_NEAR_RIGHT);
    long wrong = 0;
    long checked = 0;

    for( int y = 0; y < VIEW_H; y++ )
    {
        for( int x = 0; x < VIEW_W; x++ )
        {
            int const px = (int)f->pixels[y * VIEW_W + x];
            int const dx = x - VIEW_W / 2;

            if( px == BACKGROUND )
                continue;
            if( dx > -margin && dx < margin )
                continue;
            checked++;
            if( dx < 0 && px != rgb_left )
                wrong++;
            else if( dx > 0 && px != rgb_right )
                wrong++;
        }
    }

    *out_wrong = wrong;
    *out_checked = checked;
}

/* --- tests ---------------------------------------------------------------- */

static void
test_crossing_quads(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame painter;
    struct Frame depth;
    long wrong;
    long checked;

    build_crossing_quads(&mesh);

    frame_clear(&painter);
    CHECK(render_model(scene, &mesh, &painter, false, ORDER_SORTED), "painter render culled");
    frame_clear(&depth);
    CHECK(render_model(scene, &mesh, &depth, true, ORDER_SORTED), "depth render culled");

    /* Negative control. If the sort could already resolve this, the depth test
     * below would pass on a model that never needed it and prove nothing. */
    count_crossing_errors(&painter, 3, &wrong, &checked);
    CHECK(checked > 1000, "crossing quads covered only %ld pixels", checked);
    CHECK(
        wrong > checked / 8,
        "painter's sort resolved the crossing (%ld/%ld wrong) — the fixture no longer "
        "exercises the case the depth test exists for",
        wrong,
        checked);

    count_crossing_errors(&depth, 3, &wrong, &checked);
    CHECK(
        wrong == 0,
        "depth test left %ld/%ld pixels showing the farther quad",
        wrong,
        checked);
}

/**
 * Render through ToriDraw_RenderZBuffered — the entry point that skips the sort.
 *
 * Deliberately does not touch `flags`: this path is opted into by CALLING it,
 * and a test that set the flag anyway could not tell the two features apart.
 */
static bool
render_model_unsorted(
    struct ToriDraw_Scene* scene,
    struct Mesh* mesh,
    struct Frame* frame)
{
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();
    struct ToriDraw_Position pos = position();

    return ToriDraw_RenderZBuffered(
               mesh->hnd, scene, &pos, &vp, &cam, frame->pixels, false) ==
           TORIDRAW_CULL_VISIBLE;
}

/**
 * The unsorted entry point resolves the crossing, on a model carrying no flag.
 *
 * Two claims, and the second is the one worth the fixture: that it works at all,
 * and that it lands on the SAME picture the flagged, sorted depth render does.
 * The second is what says "no sort" removed a step rather than changed an
 * answer — the sort's other job, back-face culling, still has to happen, and if
 * it did not this frame would carry the quads' reverse windings on top of them.
 */
static void
test_unsorted_entry_point(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame unsorted;
    struct Frame sorted_depth;
    long wrong;
    long checked;

    build_crossing_quads(&mesh);
    mesh.model.flags = 0;

    frame_clear(&unsorted);
    CHECK(render_model_unsorted(scene, &mesh, &unsorted), "unsorted render culled");
    CHECK(mesh.model.flags == 0, "the entry point wrote TORIDRAW_MODEL_FLAG_ZBUFFER back");

    count_crossing_errors(&unsorted, 3, &wrong, &checked);
    CHECK(checked > 1000, "unsorted render covered only %ld pixels", checked);
    CHECK(
        wrong == 0,
        "unsorted depth render left %ld/%ld pixels showing the farther quad",
        wrong,
        checked);

    frame_clear(&sorted_depth);
    CHECK(render_model(scene, &mesh, &sorted_depth, true, ORDER_SORTED), "depth render culled");
    CHECK(
        frame_differences_outside_band(&unsorted, &sorted_depth, 2) == 0,
        "sorted and unsorted depth renders differ on %ld pixels away from the equal-depth "
        "crossing — dropping the sort changed more than the order",
        frame_differences_outside_band(&unsorted, &sorted_depth, 2));
}

/**
 * Two quads at exactly the same place and depth, different colours.
 *
 * The fixture exists because draw order is normally INVISIBLE under a depth
 * test — that is the whole point of one — which makes "the sort was skipped"
 * unfalsifiable on ordinary geometry. At an exact depth tie it is visible: the
 * kernels take strictly-nearer, so a tie keeps what is already there and the
 * face drawn FIRST is the one you see. The picture then names the order.
 */
static void
build_coincident_quads(struct Mesh* m)
{
    int const v0 = 0;
    int const v1 = 1;
    int const v2 = 2;
    int const v3 = 3;

    mesh_init(m);

    mesh_add_vertex(m, -EXTENT, -EXTENT, 0);
    mesh_add_vertex(m, EXTENT, -EXTENT, 0);
    mesh_add_vertex(m, EXTENT, EXTENT, 0);
    mesh_add_vertex(m, -EXTENT, EXTENT, 0);

    /* The same four vertices twice: identical geometry, identical depth. */
    mesh_add_quad(m, v0, v1, v2, v3, HSL_NEAR_LEFT, 0);
    mesh_add_quad(m, v0, v1, v2, v3, HSL_NEAR_RIGHT, 0);
}

/**
 * Face priorities cannot reach the unsorted path, because nothing ranks faces.
 *
 * The evidence is an EQUALITY: the same model with and without priorities must
 * produce the identical frame. That is only worth anything on geometry where
 * the order is observable, hence the tie fixture — on the crossing quads the
 * depth test resolves every pixel the same way whatever order they arrive in,
 * so an equality there cannot tell "ignored" from "obeyed but harmless".
 *
 * The control is the painter render, which must CHANGE when the priorities are
 * added. Without it, an equality could just as well mean the priority array
 * never reached the engine at all.
 */
static void
test_unsorted_ignores_priorities(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame plain;
    struct Frame prioritised;
    struct Frame painter_plain;
    struct Frame painter_prioritised;

    build_coincident_quads(&mesh);

    frame_clear(&plain);
    frame_clear(&painter_plain);
    CHECK(render_model_unsorted(scene, &mesh, &plain), "unsorted render culled");
    CHECK(
        render_model(scene, &mesh, &painter_plain, false, ORDER_SORTED),
        "painter render culled");

    /*
     * Quad B's faces are ranked AHEAD of quad A's, so any sorting path draws B
     * first and B wins the tie — the reverse of the model's own face order.
     * Priority 0 is emitted in the first run and 9 in the last but one, so
     * giving B the 0 puts it first. mesh_add_quad emits four faces per quad
     * (both windings of each triangle), so faces 0-3 are A and 4-7 are B.
     *
     * Priorities are stored packed, two faces to a byte — writing one byte per
     * face gives every even face its neighbour's priority and every odd face a
     * zero, which is a silent way to produce no ordering change at all.
     */
    for( int f = 0; f < mesh.model.face_count; f++ )
    {
        int const prio = (f < 4) ? 9 : 0;
        uint8_t* byte = &mesh.priorities[f >> 1];

        if( f & 1 )
            *byte = (uint8_t)((*byte & 0x0F) | (prio << 4));
        else
            *byte = (uint8_t)((*byte & 0xF0) | prio);
    }
    mesh.model.face_priorities = mesh.priorities;

    frame_clear(&prioritised);
    frame_clear(&painter_prioritised);
    CHECK(render_model_unsorted(scene, &mesh, &prioritised), "unsorted render culled");
    CHECK(
        render_model(scene, &mesh, &painter_prioritised, false, ORDER_SORTED),
        "painter render culled");

    CHECK(
        frame_differences(&painter_plain, &painter_prioritised) > 0,
        "adding face priorities changed nothing in the PAINTER render — the priority "
        "array is not reaching the sort, so the equality below proves nothing");

    CHECK(
        frame_differences(&plain, &prioritised) == 0,
        "%ld pixels changed when face priorities were added to a model drawn through "
        "ToriDraw_RenderZBuffered, which does not sort at all",
        frame_differences(&plain, &prioritised));
}

static void
test_order_independence(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame forward;
    struct Frame backward;
    struct Frame painter_forward;
    struct Frame painter_backward;

    build_crossing_quads(&mesh);

    frame_clear(&forward);
    frame_clear(&backward);
    CHECK(render_model(scene, &mesh, &forward, true, ORDER_SORTED), "forward render culled");
    CHECK(
        render_model(scene, &mesh, &backward, true, ORDER_REVERSED),
        "reversed render culled");

    CHECK(
        frame_differences_outside_band(&forward, &backward, 2) == 0,
        "depth-tested output changed when the face order was reversed (%ld pixels away "
        "from the equal-depth crossing) — opaque geometry must not depend on draw order",
        frame_differences_outside_band(&forward, &backward, 2));

    /* The tie band itself may differ, but only there: a wider spread would mean
     * the depth resolution, not the tie, is deciding the picture. */
    CHECK(
        frame_differences(&forward, &backward) < 4 * VIEW_H,
        "%ld pixels differ in total — far more than the equal-depth crossing can "
        "account for",
        frame_differences(&forward, &backward));

    /* The same control as above, in the dimension this test measures: without
     * the depth buffer, reversing the order has to change the picture, or
     * "unchanged" was never evidence of anything. */
    frame_clear(&painter_forward);
    frame_clear(&painter_backward);
    render_model(scene, &mesh, &painter_forward, false, ORDER_SORTED);
    render_model(scene, &mesh, &painter_backward, false, ORDER_REVERSED);
    CHECK(
        frame_differences(&painter_forward, &painter_backward) > 0,
        "reversing the face order changed nothing even WITHOUT the depth buffer — the "
        "order reversal is not reaching the raster");
}

static void
test_coverage_parity(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame painter;
    struct Frame depth;
    long covered = 0;

    /* One quad, so there is nothing to resolve and any difference in which
     * pixels are touched is a difference in the fill rule, not in depth. */
    mesh_init(&mesh);
    {
        int const v0 = mesh_add_vertex(&mesh, -EXTENT, -EXTENT, 40);
        int const v1 = mesh_add_vertex(&mesh, EXTENT, -EXTENT, -40);
        int const v2 = mesh_add_vertex(&mesh, EXTENT, EXTENT, -40);
        int const v3 = mesh_add_vertex(&mesh, -EXTENT, EXTENT, 40);

        mesh_add_quad(&mesh, v0, v1, v2, v3, HSL_NEAR_LEFT, 0);
    }

    frame_clear(&painter);
    frame_clear(&depth);
    CHECK(render_model(scene, &mesh, &painter, false, ORDER_SORTED), "painter render culled");
    CHECK(render_model(scene, &mesh, &depth, true, ORDER_SORTED), "depth render culled");

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( painter.pixels[i] != BACKGROUND )
            covered++;

    CHECK(covered > 1000, "single quad covered only %ld pixels", covered);
    CHECK(
        frame_coverage_differences(&painter, &depth) == 0,
        "the depth-tested kernels touched %ld pixels the stock ones did not (or vice "
        "versa) — the flag must reorder, not reshape",
        frame_coverage_differences(&painter, &depth));
}

/**
 * A crossing placed where interpolating depth linearly in screen space gets the
 * answer wrong.
 *
 * One quad recedes steeply from the eye, the other sits at a constant depth
 * chosen to equal the receding quad's TRUE depth at a known screen column. Depth
 * is a hyperbola across the screen, so a linear interpolant sags below it
 * everywhere between the corners, and the crossing it reports lands measurably
 * off. Asserting the crossing's position is therefore an assertion that the key
 * being interpolated is the reciprocal.
 */
static void
test_perspective_correct_depth(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame depth;
    int const near_z = -600;
    int const far_z = 600;
    /* Depth of the receding quad at model x == 0, i.e. the crossing the correct
     * interpolation produces: the two quads meet on the screen column where the
     * flat one's depth equals the receding one's. */
    int const flat_z = (near_z + far_z) / 2;
    int boundary = -1;
    int const row = VIEW_H / 2;
    int const rgb_recede = rgb_of(HSL_NEAR_LEFT);

    mesh_init(&mesh);
    {
        int const r0 = mesh_add_vertex(&mesh, -EXTENT, -EXTENT, near_z);
        int const r1 = mesh_add_vertex(&mesh, EXTENT, -EXTENT, far_z);
        int const r2 = mesh_add_vertex(&mesh, EXTENT, EXTENT, far_z);
        int const r3 = mesh_add_vertex(&mesh, -EXTENT, EXTENT, near_z);

        mesh_add_quad(&mesh, r0, r1, r2, r3, HSL_NEAR_LEFT, 0);
    }
    {
        int const f0 = mesh_add_vertex(&mesh, -EXTENT, -EXTENT, flat_z);
        int const f1 = mesh_add_vertex(&mesh, EXTENT, -EXTENT, flat_z);
        int const f2 = mesh_add_vertex(&mesh, EXTENT, EXTENT, flat_z);
        int const f3 = mesh_add_vertex(&mesh, -EXTENT, EXTENT, flat_z);

        mesh_add_quad(&mesh, f0, f1, f2, f3, HSL_NEAR_RIGHT, 0);
    }

    frame_clear(&depth);
    CHECK(render_model(scene, &mesh, &depth, true, ORDER_SORTED), "depth render culled");

    /* Walk the middle row left to right and find where the receding quad stops
     * winning. */
    for( int x = 1; x < VIEW_W; x++ )
    {
        int const prev = (int)depth.pixels[row * VIEW_W + x - 1];
        int const cur = (int)depth.pixels[row * VIEW_W + x];

        if( prev == rgb_recede && cur != rgb_recede && cur != BACKGROUND )
        {
            boundary = x;
            break;
        }
    }

    CHECK(boundary >= 0, "no crossing found on row %d", row);
    if( boundary < 0 )
        return;

    /*
     * Where the crossing must be. The receding quad's depth is linear in MODEL
     * x, and model x maps to screen x through the perspective divide, so solve
     * for the model x whose depth is flat_z and project it.
     *
     *   z(t)      = near_z + t * (far_z - near_z),  t in [0,1] across the quad
     *   x_model(t)= -EXTENT + t * 2 * EXTENT
     *   screen_x  = x_model * scale / (CAMERA_DISTANCE + z)
     */
    {
        double const t = (double)(flat_z - near_z) / (double)(far_z - near_z);
        double const x_model = -(double)EXTENT + t * 2.0 * (double)EXTENT;
        double const z_camera = (double)CAMERA_DISTANCE + (double)flat_z;
        double const screen_x =
            x_model * (double)TORIDRAW_PROJ_SCALE_DEFAULT / z_camera + (double)(VIEW_W / 2);
        double const error = (double)boundary - screen_x;

        CHECK(
            error > -2.0 && error < 2.0,
            "crossing landed at x=%d, geometry says %.2f (error %.2f px) — depth is not "
            "being interpolated as a reciprocal",
            boundary,
            screen_x,
            error);
    }
}

static void
test_translucent_does_not_write_depth(struct ToriDraw_Scene* scene)
{
    struct Mesh opaque_first;
    struct Mesh translucent_first;
    struct Frame after_opaque;
    struct Frame after_translucent;
    int const rgb_far = rgb_of(HSL_NEAR_RIGHT);
    long far_visible_behind_opaque = 0;
    long far_visible_behind_translucent = 0;

    /*
     * Two coplanar-in-screen quads, one nearer than the other, both covering the
     * middle of the frame. The far one is given a LOWER priority so the sort
     * draws it after the near one — which is the only case that distinguishes
     * the two policies: if the near quad wrote depth, the far one is rejected;
     * if it did not, the far one paints over it.
     *
     * Draw order is forced by hand rather than by priority, via ORDER_REVERSED
     * below, so the fixture does not depend on how the sorter breaks ties.
     */
    for( int translucent = 0; translucent < 2; translucent++ )
    {
        struct Mesh* m = translucent ? &translucent_first : &opaque_first;
        struct Frame* f = translucent ? &after_translucent : &after_opaque;
        long* counter =
            translucent ? &far_visible_behind_translucent : &far_visible_behind_opaque;

        mesh_init(m);
        /* Near quad, drawn first. */
        {
            int const v0 = mesh_add_vertex(m, -EXTENT / 2, -EXTENT / 2, -EXTENT);
            int const v1 = mesh_add_vertex(m, EXTENT / 2, -EXTENT / 2, -EXTENT);
            int const v2 = mesh_add_vertex(m, EXTENT / 2, EXTENT / 2, -EXTENT);
            int const v3 = mesh_add_vertex(m, -EXTENT / 2, EXTENT / 2, -EXTENT);

            /* face_alphas is stored inverted by the raster (0xFF - a), so 0 is
             * fully opaque and 0x80 is half coverage. */
            mesh_add_quad(m, v0, v1, v2, v3, HSL_NEAR_LEFT, translucent ? 0x80 : 0x00);
        }
        /* Far quad, drawn second, same screen footprint. */
        {
            int const v0 = mesh_add_vertex(m, -EXTENT / 2, -EXTENT / 2, EXTENT);
            int const v1 = mesh_add_vertex(m, EXTENT / 2, -EXTENT / 2, EXTENT);
            int const v2 = mesh_add_vertex(m, EXTENT / 2, EXTENT / 2, EXTENT);
            int const v3 = mesh_add_vertex(m, -EXTENT / 2, EXTENT / 2, EXTENT);

            mesh_add_quad(m, v0, v1, v2, v3, HSL_NEAR_RIGHT, 0x00);
        }
        mesh_enable_alpha(m);

        /* The sorter puts the near quad last (nearest drawn last); reversing
         * gives near-then-far, which is the order this test needs. */
        frame_clear(f);
        CHECK(render_model(scene, m, f, true, ORDER_REVERSED), "render culled");

        for( int i = 0; i < VIEW_W * VIEW_H; i++ )
            if( (int)f->pixels[i] == rgb_far )
                (*counter)++;
    }

    CHECK(
        far_visible_behind_opaque == 0,
        "%ld pixels of the FAR quad survived behind an opaque near one — an opaque face "
        "must write depth",
        far_visible_behind_opaque);
    CHECK(
        far_visible_behind_translucent > 1000,
        "only %ld pixels of the far quad drew behind a TRANSLUCENT near one — a "
        "translucent face must test depth without writing it, so what is drawn after it "
        "still composites",
        far_visible_behind_translucent);
}

/**
 * The textured mode of the depth-tested kernels.
 *
 * Same crossing fixture, drawn through the perspective texture path instead of
 * the flat one — a separate third of the kernel, with its own span entry rule,
 * its own uv plane setup and its own shade gradient. It has to resolve the
 * crossing exactly as the flat path does, and it has to cover the same pixels
 * the stock textured kernel covers.
 */
static void
test_textured_faces(struct ToriDraw_Scene* scene, bool flat_shaded)
{
    struct Mesh mesh;
    struct Frame painter;
    struct Frame depth;
    int const rgb_left = shade_blend_reference(TEX_RGB_LEFT);
    int const rgb_right = shade_blend_reference(TEX_RGB_RIGHT);
    const char* const which = flat_shaded ? "flat-shaded" : "gouraud-shaded";
    long wrong = 0;
    long checked = 0;

    build_crossing_quads(&mesh);
    mesh_make_textured(&mesh, TEX_ID_LEFT, flat_shaded);
    /* build_crossing_quads emits quad A's four faces first, then quad B's. */
    for( int f = 4; f < mesh.model.face_count; f++ )
        mesh_set_face_texture(&mesh, f, TEX_ID_RIGHT);

    frame_clear(&painter);
    frame_clear(&depth);
    CHECK(render_model(scene, &mesh, &painter, false, ORDER_SORTED), "painter render culled");
    CHECK(render_model(scene, &mesh, &depth, true, ORDER_SORTED), "depth render culled");

    /* Graded twice: once on the stock render, which must get it WRONG (else the
     * fixture is not exercising the crossing at all), then on the depth-tested
     * one, which must get it exactly right. */
    for( int pass = 0; pass < 2; pass++ )
    {
        const struct Frame* f = pass == 0 ? &painter : &depth;

        wrong = 0;
        checked = 0;
        for( int y = 0; y < VIEW_H; y++ )
            for( int x = 0; x < VIEW_W; x++ )
            {
                int const px = (int)f->pixels[y * VIEW_W + x];
                int const dx = x - VIEW_W / 2;

                if( px == BACKGROUND )
                    continue;
                if( dx > -3 && dx < 3 )
                    continue;
                checked++;
                if( dx < 0 && px != rgb_left )
                    wrong++;
                else if( dx > 0 && px != rgb_right )
                    wrong++;
            }

        if( pass == 0 )
            CHECK(
                wrong > checked / 8,
                "the painter's sort resolved the %s textured crossing (%ld/%ld wrong) — "
                "the fixture no longer exercises the case",
                which,
                wrong,
                checked);
    }

    CHECK(checked > 1000, "%s textured crossing covered only %ld pixels", which, checked);
    CHECK(
        wrong == 0,
        "depth-tested %s TEXTURED faces left %ld/%ld pixels showing the farther quad",
        which,
        wrong,
        checked);
    CHECK(
        frame_coverage_differences(&painter, &depth) == 0,
        "the depth-tested %s texture path covered %ld pixels differently from the stock "
        "one",
        which,
        frame_coverage_differences(&painter, &depth));
}

/** Both textures live for the whole run; the scene frees them with itself. */
static void
register_test_textures(struct ToriDraw_Scene* scene)
{
    struct ToriDraw_Texture* left = make_flat_texture(TEX_RGB_LEFT);
    struct ToriDraw_Texture* right = make_flat_texture(TEX_RGB_RIGHT);

    CHECK(left && right, "could not build the test textures");
    if( !left || !right )
        return;
    ToriDraw_SceneSetTexture(scene, TEX_ID_LEFT, left);
    ToriDraw_SceneSetTexture(scene, TEX_ID_RIGHT, right);
}

static void
test_buffer_is_reset_per_model(struct ToriDraw_Scene* scene)
{
    struct Mesh near_mesh;
    struct Mesh far_mesh;
    struct Frame frame;
    struct ToriDraw_ViewPort vp = viewport();
    struct ToriDraw_Camera cam = camera();
    struct ToriDraw_Position pos = position();
    int const rgb_far = rgb_of(HSL_NEAR_RIGHT);
    long far_pixels = 0;

    /* Same footprint, different depths, as two separate models. */
    mesh_init(&near_mesh);
    {
        int const v0 = mesh_add_vertex(&near_mesh, -EXTENT / 2, -EXTENT / 2, -EXTENT);
        int const v1 = mesh_add_vertex(&near_mesh, EXTENT / 2, -EXTENT / 2, -EXTENT);
        int const v2 = mesh_add_vertex(&near_mesh, EXTENT / 2, EXTENT / 2, -EXTENT);
        int const v3 = mesh_add_vertex(&near_mesh, -EXTENT / 2, EXTENT / 2, -EXTENT);

        mesh_add_quad(&near_mesh, v0, v1, v2, v3, HSL_NEAR_LEFT, 0);
    }
    mesh_init(&far_mesh);
    {
        int const v0 = mesh_add_vertex(&far_mesh, -EXTENT / 2, -EXTENT / 2, EXTENT);
        int const v1 = mesh_add_vertex(&far_mesh, EXTENT / 2, -EXTENT / 2, EXTENT);
        int const v2 = mesh_add_vertex(&far_mesh, EXTENT / 2, EXTENT / 2, EXTENT);
        int const v3 = mesh_add_vertex(&far_mesh, -EXTENT / 2, EXTENT / 2, EXTENT);

        mesh_add_quad(&far_mesh, v0, v1, v2, v3, HSL_NEAR_RIGHT, 0);
    }

    frame_clear(&frame);
    near_mesh.model.flags = TORIDRAW_MODEL_FLAG_ZBUFFER;
    far_mesh.model.flags = TORIDRAW_MODEL_FLAG_ZBUFFER;

    ToriDraw_RenderModel(near_mesh.hnd, scene, &pos, &vp, &cam, frame.pixels);
    ToriDraw_RenderModel(far_mesh.hnd, scene, &pos, &vp, &cam, frame.pixels);

    for( int i = 0; i < VIEW_W * VIEW_H; i++ )
        if( (int)frame.pixels[i] == rgb_far )
            far_pixels++;

    /* The far model is drawn second and must win outright: models layer by the
     * order the scene draws them, and the buffer the first model filled has to
     * have been reset before the second consulted it. */
    CHECK(
        far_pixels > 1000,
        "only %ld pixels of the second model survived the first model's depths — the "
        "buffer is not being reset per model",
        far_pixels);
}

static void
test_flag_is_opt_in(struct ToriDraw_Scene* scene)
{
    struct Mesh mesh;
    struct Frame with_scene_buffer;
    struct Frame without_flag;

    /* A scene that CAN z-buffer must still draw a model that did not ask for it
     * exactly as a scene that cannot. */
    build_crossing_quads(&mesh);

    frame_clear(&with_scene_buffer);
    frame_clear(&without_flag);
    CHECK(
        render_model(scene, &mesh, &with_scene_buffer, false, ORDER_SORTED),
        "render culled");

    {
        struct ToriDraw_Scene* plain = ToriDraw_SceneNew(
            TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);

        CHECK(plain != NULL, "could not allocate a scene without the z-buffer flag");
        if( plain )
        {
            CHECK(
                render_model(plain, &mesh, &without_flag, false, ORDER_SORTED),
                "render culled");
            CHECK(
                !ToriDraw_SceneHasZBuffer(plain, VIEW_W, VIEW_H),
                "a scene without TORIDRAW_SCENE_MODEL_ZBUFFER allocated one anyway");
            ToriDraw_SceneFree(plain);
        }
    }

    CHECK(
        frame_differences(&with_scene_buffer, &without_flag) == 0,
        "a model that did not opt in rendered differently on a z-buffer-capable scene "
        "(%ld pixels)",
        frame_differences(&with_scene_buffer, &without_flag));
}

static void
test_inert_without_a_buffer(void)
{
    struct Mesh mesh;
    struct Frame flagged;
    struct Frame unflagged;
    struct ToriDraw_Scene* plain =
        ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);

    CHECK(plain != NULL, "could not allocate a plain scene");
    if( !plain )
        return;

    /* The model asks for depth testing; the scene cannot provide it. That has to
     * degrade to the stock kernels, not to a crash or a blank model. */
    build_crossing_quads(&mesh);
    frame_clear(&flagged);
    frame_clear(&unflagged);
    CHECK(render_model(plain, &mesh, &flagged, true, ORDER_SORTED), "render culled");
    CHECK(render_model(plain, &mesh, &unflagged, false, ORDER_SORTED), "render culled");

    CHECK(
        frame_differences(&flagged, &unflagged) == 0,
        "the model flag changed the picture on a scene with no z-buffer (%ld pixels) — "
        "it must be inert there",
        frame_differences(&flagged, &unflagged));

    ToriDraw_SceneFree(plain);
}

static void
test_explicit_sizing(void)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);

    CHECK(scene != NULL, "could not allocate a scene");
    if( !scene )
        return;

    CHECK(!ToriDraw_SceneHasZBuffer(scene, 64, 64), "buffer present before it was asked for");
    CHECK(ToriDraw_SceneZBufferResize(scene, 64, 64), "explicit resize failed");
    CHECK(ToriDraw_SceneHasZBuffer(scene, 64, 64), "buffer missing after an explicit resize");

    /* Growing must keep both dimensions; shrinking must keep the capacity, so a
     * caller alternating between two viewport sizes does not thrash. */
    CHECK(ToriDraw_SceneZBufferResize(scene, 128, 32), "grow failed");
    CHECK(ToriDraw_SceneHasZBuffer(scene, 128, 64), "grow dropped the larger row count");
    CHECK(ToriDraw_SceneZBufferResize(scene, 16, 16), "shrink request failed");
    CHECK(ToriDraw_SceneHasZBuffer(scene, 128, 64), "shrink discarded existing capacity");

    ToriDraw_SceneZBufferFree(scene);
    CHECK(!ToriDraw_SceneHasZBuffer(scene, 1, 1), "free left the buffer resident");

    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    struct ToriDraw_Scene* scene;

    ToriDraw_Init();

    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_MODEL_ZBUFFER, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    if( !scene )
    {
        fprintf(stderr, "FAIL: could not allocate a z-buffer-capable scene\n");
        return 1;
    }

    printf(
        "zbuffer depth storage: %zu bytes/pixel (%s)\n",
        sizeof(torizdepth_t),
        TORIDRAW_ZDEPTH_HALF ? "16-bit half" : "32-bit float");

    test_crossing_quads(scene);
    test_unsorted_entry_point(scene);
    test_unsorted_ignores_priorities(scene);
    test_order_independence(scene);
    test_coverage_parity(scene);
    test_perspective_correct_depth(scene);
    test_translucent_does_not_write_depth(scene);
    register_test_textures(scene);
    test_textured_faces(scene, false);
    test_textured_faces(scene, true);
    test_buffer_is_reset_per_model(scene);
    test_flag_is_opt_in(scene);
    test_inert_without_a_buffer();
    test_explicit_sizing();

    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("toridraw_zbuffer_test: all checks passed\n");
    return 0;
}
