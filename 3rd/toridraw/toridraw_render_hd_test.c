/*
 * Routing test for ToriDraw_RenderHD.
 *
 * The kernels themselves are tested elsewhere (test-texture-matrix,
 * test-texmap). What is tested here is the *decision*: given a model's render
 * types, a material table and per-face alphas, does each face reach the kernel
 * it should?
 *
 * That needs its own test because it fails silently. A cube face drawn through
 * the plane kernel still produces pixels, and a material whose gate is ignored
 * still produces pixels; nothing about the image says which kernel drew it.
 * ToriDraw_HDRenderStats exists for exactly this reason and is what the checks
 * below assert on.
 *
 * Build and run:
 *   make -C src test-render-hd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "graphics/raster/texture/texmap_common.h"

#define W 160
#define H 120

static int g_fail;
static int g_checks;

static void
check(int cond, const char* what, const char* detail)
{
    g_checks++;
    if( cond )
        return;
    printf("  FAIL %-52s %s\n", what, detail ? detail : "");
    g_fail++;
}

static void
check_eq(int got, int want, const char* what)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "got %d, want %d", got, want);
    check(got == want, what, buf);
}

/* ---------------------------------------------------------------- model */

/*
 * A quad per render type, so one render exercises all four projection families
 * at once and the counters have to add up rather than merely be non-zero.
 *
 * Two triangles each, 8 faces, 4 texture-coord entries — one per render type,
 * with face_texture_coords pointing both triangles of a quad at the same entry
 * (which is what a real face group looks like).
 */
#define QUADS 4
#define FACES (QUADS * 2)
#define VERTS (QUADS * 4)

static struct ToriDraw_ModelHD*
build_model(int with_alpha_on_last_quad)
{
    struct ToriDraw_ModelHD* hd =
        (struct ToriDraw_ModelHD*)calloc(1, sizeof(struct ToriDraw_ModelHD));
    struct ToriDraw_Model* m = &hd->base;

    m->vertex_count = VERTS;
    m->face_count = FACES;
    m->textured_face_count = QUADS;

    m->vertices_x = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));
    m->vertices_y = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));
    m->vertices_z = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));
    m->original_vertices_x = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));
    m->original_vertices_y = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));
    m->original_vertices_z = (vertexint_t*)calloc(VERTS, sizeof(vertexint_t));

    m->face_indices_a = (faceint_t*)calloc(FACES, sizeof(faceint_t));
    m->face_indices_b = (faceint_t*)calloc(FACES, sizeof(faceint_t));
    m->face_indices_c = (faceint_t*)calloc(FACES, sizeof(faceint_t));
    m->face_colors_a = (hsl16_t*)calloc(FACES, sizeof(hsl16_t));
    m->face_colors_b = (hsl16_t*)calloc(FACES, sizeof(hsl16_t));
    m->face_colors_c = (hsl16_t*)calloc(FACES, sizeof(hsl16_t));
    m->face_infos = (int*)calloc(FACES, sizeof(int));
    m->face_textures = (faceint_t*)calloc(FACES, sizeof(faceint_t));
    m->face_texture_coords = (faceint_t*)calloc(FACES, sizeof(faceint_t));
    m->face_alphas = (alphaint_t*)calloc(FACES, sizeof(alphaint_t));

    m->texture_render_types = (uint8_t*)calloc(QUADS, sizeof(uint8_t));
    m->textured_p_coordinate = (faceint_t*)calloc(QUADS, sizeof(faceint_t));
    m->textured_m_coordinate = (faceint_t*)calloc(QUADS, sizeof(faceint_t));
    m->textured_n_coordinate = (faceint_t*)calloc(QUADS, sizeof(faceint_t));

    for( int q = 0; q < QUADS; q++ )
    {
        int v = q * 4;
        int base_x = -140 + q * 70;
        /* Facing the camera, well inside the near plane. */
        m->vertices_x[v + 0] = base_x;
        m->vertices_y[v + 0] = -40;
        m->vertices_z[v + 0] = 0;
        m->vertices_x[v + 1] = base_x + 55;
        m->vertices_y[v + 1] = -40;
        m->vertices_z[v + 1] = 0;
        m->vertices_x[v + 2] = base_x + 55;
        m->vertices_y[v + 2] = 40;
        m->vertices_z[v + 2] = 0;
        m->vertices_x[v + 3] = base_x;
        m->vertices_y[v + 3] = 40;
        m->vertices_z[v + 3] = 0;

        int f = q * 2;
        /* Wound so the quad faces the camera: the face sort culls the other
         * winding and the whole model silently orders to zero faces. */
        m->face_indices_a[f] = v + 0;
        m->face_indices_b[f] = v + 2;
        m->face_indices_c[f] = v + 1;
        m->face_indices_a[f + 1] = v + 0;
        m->face_indices_b[f + 1] = v + 3;
        m->face_indices_c[f + 1] = v + 2;

        for( int k = 0; k < 2; k++ )
        {
            /* Textured faces carry 0-127 lightness, not hsl16. */
            m->face_colors_a[f + k] = 90;
            m->face_colors_b[f + k] = 90;
            m->face_colors_c[f + k] = 90;
            m->face_infos[f + k] = 0;
            m->face_textures[f + k] = (faceint_t)q; /* texture id == quad index */
            m->face_texture_coords[f + k] = (faceint_t)q;
            m->face_alphas[f + k] =
                (with_alpha_on_last_quad && q == QUADS - 1) ? (alphaint_t)0x40 : 0;
        }

        /* One quad per render type: 0 plane, 1 cylinder, 2 cube, 3 sphere. */
        m->texture_render_types[q] = (uint8_t)q;
        /* For type 0 these are vertex indices; for 1-3 the decoder stores a raw
         * axis triple, which the mapping builder consumes instead. */
        m->textured_p_coordinate[q] = (faceint_t)(v + 0);
        m->textured_m_coordinate[q] = (faceint_t)(v + 1);
        m->textured_n_coordinate[q] = (faceint_t)(v + 3);
    }

    memcpy(m->original_vertices_x, m->vertices_x, VERTS * sizeof(vertexint_t));
    memcpy(m->original_vertices_y, m->vertices_y, VERTS * sizeof(vertexint_t));
    memcpy(m->original_vertices_z, m->vertices_z, VERTS * sizeof(vertexint_t));

    /* Without this the projection returns CULL_ERROR before a single face is
     * routed — the frustum test reads the cylinder's radius. */
    ToriDraw_ModelSetBoundsCylinder(m);

    return hd;
}

/* ------------------------------------------------------------ materials */

#define TEX_W 64
static int g_texels_opaque[TEX_W * TEX_W];
static int g_texels_alpha[TEX_W * TEX_W];

static void
init_texels(void)
{
    for( int i = 0; i < TEX_W * TEX_W; i++ )
    {
        int rgb = 0x00C08040 + (i & 0x1F);
        g_texels_opaque[i] = (int)(0xFF000000u | (unsigned)rgb);
        /* A continuous ramp, which only the texalpha gate can express. */
        g_texels_alpha[i] = (int)((unsigned)((i * 7) & 0xFF) << 24 | (unsigned)rgb);
    }
}

/* ---------------------------------------------------------------- render */

struct render_env
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ViewPort vp;
    struct ToriDraw_Camera cam;
    struct ToriDraw_Position pos;
    int* pixels;
};

static void
env_init(struct render_env* e)
{
    memset(e, 0, sizeof(*e));
    e->pixels = (int*)calloc(W * H, sizeof(int));
    e->scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);

    e->vp.width = W;
    e->vp.height = H;
    e->vp.stride = W;
    e->vp.x_center = W / 2;
    e->vp.y_center = H / 2;
    e->vp.clip_left = 0;
    e->vp.clip_top = 0;
    e->vp.clip_right = W;
    e->vp.clip_bottom = H;

    e->cam.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    e->cam.proj_scale = 512;
    e->cam.near_plane_z = 50;

    /* Push the model away from the eye so every vertex clears the near plane;
     * the mapped kernels reject a triangle with a non-positive camera z. */
    e->pos.z = 600;
}

static void
env_free(struct render_env* e)
{
    ToriDraw_SceneFree(e->scene);
    free(e->pixels);
}

static struct ToriDraw_HDRenderStats
render(
    struct render_env* e,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_HDMaterials* materials)
{
    struct ToriDraw_HDRenderStats st;
    memset(e->pixels, 0, (size_t)W * H * sizeof(int));
    int cull = ToriDraw_RenderHD(
        hnd, e->scene, &e->pos, &e->vp, &e->cam, e->pixels, materials, &st);
    if( getenv("HD_TEST_DEBUG") )
        printf("    [debug] cull=%d faces_ordered=%d stats.faces=%d\n",
               cull, e->scene->tmp_face_order_count, st.faces);
    (void)cull;
    return st;
}

/** The same render through the depth-tested twins, with no face sort. */
static struct ToriDraw_HDRenderStats
render_zbuf(
    struct render_env* e,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_HDMaterials* materials)
{
    struct ToriDraw_HDRenderStats st;
    memset(e->pixels, 0, (size_t)W * H * sizeof(int));
    ToriDraw_RenderHDZBuffered(
        hnd, e->scene, &e->pos, &e->vp, &e->cam, e->pixels, materials, &st);
    return st;
}

static long
covered(const struct render_env* e)
{
    long n = 0;
    for( int i = 0; i < W * H; i++ )
        if( (e->pixels[i] & 0x00FFFFFF) != 0 )
            n++;
    return n;
}

/* ----------------------------------------------------------------- tests */

/*
 * Every face reaches the family its render type names — the check that a cube
 * face is not quietly drawn as a plane. Each quad is 2 faces, so each family
 * must show exactly 2.
 */
static void
test_projection_routing(struct render_env* e)
{
    printf("each render type reaches its own projection family\n");

    struct ToriDraw_ModelHD* hd = build_model(0);
    check(
        ToriDraw_ModelBuildTextureMappings(hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
        "mappings build", NULL);

    struct ToriDraw_HDMaterial mats[QUADS];
    memset(mats, 0, sizeof(mats));
    for( int q = 0; q < QUADS; q++ )
    {
        mats[q].texels = g_texels_opaque;
        mats[q].width = TEX_W;
        mats[q].gate = TORIDRAW_HD_GATE_OPAQUE;
    }
    struct ToriDraw_HDMaterials table = { mats, QUADS };

    struct ToriDraw_HDRenderStats st =
        render(e, ToriDraw_ModelHandleFromHD(hd), &table);

    check_eq(st.drawn_plane, 2, "render type 0 -> texplane");
    check_eq(st.drawn_cylinder, 2, "render type 1 -> texcylinder");
    check_eq(st.drawn_cube, 2, "render type 2 -> texcube");
    check_eq(st.drawn_sphere, 2, "render type 3 -> texsphere");
    check_eq(st.fallback_no_texels, 0, "no material fell back");
    check_eq(st.fallback_no_mapping, 0, "no mapping fell back");
    check(covered(e) > 0, "something was actually drawn", NULL);

    ToriDraw_ModelHDFree(hd);
}

/*
 * The same model through a PLAIN handle. Every mapped face must fall back to
 * the plane family and say so — the HD flow is meant to accept a non-HD model,
 * not to crash or to silently drop 6 of 8 faces.
 */
static void
test_plain_handle_falls_back(struct render_env* e)
{
    printf("a plain handle falls back to texplane, and counts it\n");

    struct ToriDraw_ModelHD* hd = build_model(0);
    struct ToriDraw_HDMaterial mats[QUADS];
    memset(mats, 0, sizeof(mats));
    for( int q = 0; q < QUADS; q++ )
    {
        mats[q].texels = g_texels_opaque;
        mats[q].width = TEX_W;
    }
    struct ToriDraw_HDMaterials table = { mats, QUADS };

    struct ToriDraw_ModelHandle plain;
    memset(&plain, 0, sizeof(plain));
    plain.kind = TORIDRAWMK_MODEL;
    plain.u.model.model = &hd->base;

    struct ToriDraw_HDRenderStats st = render(e, plain, &table);

    check_eq(st.drawn_plane, 8, "all 8 faces drew through texplane");
    check_eq(st.drawn_cylinder + st.drawn_cube + st.drawn_sphere, 0, "no mapped kernel used");
    check_eq(st.fallback_no_mapping, 6, "the 6 mapped faces were counted as fallbacks");
    check(covered(e) > 0, "something was actually drawn", NULL);

    ToriDraw_ModelHDFree(hd);
}

/*
 * A material with no texels must draw the face as flat colour rather than skip
 * it. The stock raster skips — right for a game, wrong for a viewer — so this
 * asserts the opposite behaviour explicitly.
 */
static void
test_missing_material_is_visible(struct render_env* e)
{
    printf("a material with no texels draws flat, and is counted\n");

    struct ToriDraw_ModelHD* hd = build_model(0);
    ToriDraw_ModelBuildTextureMappings(hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    /* Untextured faces carry hsl16, not lightness. */
    for( int f = 0; f < FACES; f++ )
    {
        hd->base.face_colors_a[f] = 0x2A40;
        hd->base.face_colors_b[f] = 0x2A40;
        hd->base.face_colors_c[f] = 0x2A40;
    }

    struct ToriDraw_HDRenderStats st = render(e, ToriDraw_ModelHandleFromHD(hd), NULL);

    check_eq(st.fallback_no_texels, 8, "all 8 textured faces fell back");
    check_eq(st.drawn_untextured, 8, "and all 8 drew as untextured");
    check_eq(st.drawn_plane + st.drawn_cube + st.drawn_cylinder + st.drawn_sphere, 0,
             "no textured kernel was reached");
    check(covered(e) > 0, "the model is still visible", NULL);

    ToriDraw_ModelHDFree(hd);
}

/* The gate and modulate columns come from the material, the facealpha column
 * from the face. Each is counted independently. */
static void
test_gate_alpha_modulate_selection(struct render_env* e)
{
    printf("gate / facealpha / modulate are selected independently\n");

    struct ToriDraw_ModelHD* hd = build_model(1 /* alpha on the last quad */);
    ToriDraw_ModelBuildTextureMappings(hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    struct ToriDraw_HDMaterial mats[QUADS];
    memset(mats, 0, sizeof(mats));
    for( int q = 0; q < QUADS; q++ )
    {
        mats[q].width = TEX_W;
        mats[q].texels = (q == 2) ? g_texels_alpha : g_texels_opaque;
    }
    mats[0].gate = TORIDRAW_HD_GATE_OPAQUE;
    mats[1].gate = TORIDRAW_HD_GATE_TRANS;
    mats[2].gate = TORIDRAW_HD_GATE_ALPHA;
    mats[3].gate = TORIDRAW_HD_GATE_OPAQUE;
    mats[1].modulate = 1;
    mats[3].modulate = 1;
    mats[3].clamp_t = 1;

    struct ToriDraw_HDMaterials table = { mats, QUADS };
    struct ToriDraw_HDRenderStats st =
        render(e, ToriDraw_ModelHandleFromHD(hd), &table);

    check_eq(st.gate_opaque, 4, "two quads on the opaque gate");
    check_eq(st.gate_trans, 2, "one quad on the colour-key gate");
    check_eq(st.gate_alpha, 2, "one quad on the per-texel alpha gate");
    check_eq(st.with_modulate, 4, "two quads modulate");
    check_eq(st.with_facealpha, 2, "only the alpha-carrying quad uses facealpha");

    ToriDraw_ModelHDFree(hd);
}

/*
 * Routing has to change pixels, or the counters are measuring a decision that
 * does not reach the raster. Renders the same geometry with every material on
 * the opaque gate, then with every material on the alpha gate, and requires the
 * images to differ.
 */
static void
test_routing_changes_pixels(struct render_env* e)
{
    printf("changing the routing changes the image\n");

    struct ToriDraw_ModelHD* hd = build_model(0);
    ToriDraw_ModelBuildTextureMappings(hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    struct ToriDraw_HDMaterial mats[QUADS];
    memset(mats, 0, sizeof(mats));
    for( int q = 0; q < QUADS; q++ )
    {
        mats[q].texels = g_texels_alpha;
        mats[q].width = TEX_W;
        mats[q].gate = TORIDRAW_HD_GATE_OPAQUE;
    }
    struct ToriDraw_HDMaterials table = { mats, QUADS };

    render(e, ToriDraw_ModelHandleFromHD(hd), &table);
    int* first = (int*)malloc((size_t)W * H * sizeof(int));
    memcpy(first, e->pixels, (size_t)W * H * sizeof(int));

    for( int q = 0; q < QUADS; q++ )
        mats[q].gate = TORIDRAW_HD_GATE_ALPHA;
    render(e, ToriDraw_ModelHandleFromHD(hd), &table);

    long differ = 0;
    for( int i = 0; i < W * H; i++ )
        if( (first[i] & 0x00FFFFFF) != (e->pixels[i] & 0x00FFFFFF) )
            differ++;

    check(differ > 0, "opaque gate and alpha gate produce different pixels", NULL);

    free(first);
    ToriDraw_ModelHDFree(hd);
}

/*
 * The depth twins land on the same pixels as their plain siblings.
 *
 * On geometry where nothing overlaps, a depth test can reject nothing, so the
 * two disciplines must agree exactly — and that is the strongest statement
 * available about 48 kernels at once: every quad of this fixture takes a
 * different projection family, and the second pass puts every gate, facealpha
 * and modulate combination the fixture can reach through the same comparison.
 *
 * A difference here means a depth twin drifted from its sibling in the WALK,
 * not in the depth test: coverage, uv fit, shade plane and composite are all
 * meant to be the plain kernel's, untouched.
 */
static void
test_zbuffered_matches_when_nothing_overlaps(struct render_env* e)
{
    printf("depth twins draw the same pixels when nothing overlaps\n");

    for( int pass = 0; pass < 2; pass++ )
    {
        struct ToriDraw_ModelHD* hd = build_model(pass /* alpha on the last quad */);
        ToriDraw_ModelBuildTextureMappings(hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

        struct ToriDraw_HDMaterial mats[QUADS];
        memset(mats, 0, sizeof(mats));
        for( int q = 0; q < QUADS; q++ )
        {
            mats[q].width = TEX_W;
            mats[q].texels = (q == 2) ? g_texels_alpha : g_texels_opaque;
        }
        /* Second pass: spread the compositing matrix over the four quads, so the
         * comparison covers gates and modulate as well as the four projections. */
        if( pass )
        {
            mats[1].gate = TORIDRAW_HD_GATE_TRANS;
            mats[2].gate = TORIDRAW_HD_GATE_ALPHA;
            mats[1].modulate = 1;
            mats[3].modulate = 1;
        }
        struct ToriDraw_HDMaterials table = { mats, QUADS };
        struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleFromHD(hd);

        struct ToriDraw_HDRenderStats sorted = render(e, hnd, &table);
        int* first = (int*)malloc((size_t)W * H * sizeof(int));
        memcpy(first, e->pixels, (size_t)W * H * sizeof(int));
        long const sorted_covered = covered(e);

        struct ToriDraw_HDRenderStats depth = render_zbuf(e, hnd, &table);

        long differ = 0;
        for( int i = 0; i < W * H; i++ )
            if( first[i] != e->pixels[i] )
                differ++;

        char what[96];
        snprintf(what, sizeof(what), "pass %d: identical pixels", pass);
        check(differ == 0, what, NULL);
        check(sorted_covered > 0, "the sorted render drew something", NULL);

        snprintf(what, sizeof(what), "pass %d: same routing", pass);
        check(
            depth.drawn_plane == sorted.drawn_plane &&
                depth.drawn_cylinder == sorted.drawn_cylinder &&
                depth.drawn_cube == sorted.drawn_cube &&
                depth.drawn_sphere == sorted.drawn_sphere &&
                depth.with_facealpha == sorted.with_facealpha &&
                depth.with_modulate == sorted.with_modulate,
            what, NULL);

        free(first);
        ToriDraw_ModelHDFree(hd);
    }
}

/* ------------------------------------------------- interpenetration fixture */

/*
 * Two textured quads crossing like an X: A runs near-left to far-right, B the
 * other way. Each is partly in front of the other, so NO order of whole faces
 * produces the right picture — which is what makes this the fixture that
 * separates "depth-tested" from "sorted".
 */
#define XW 80
#define XH 50
#define XZ 60

static struct ToriDraw_ModelHD*
build_crossing_model(void)
{
    struct ToriDraw_ModelHD* hd =
        (struct ToriDraw_ModelHD*)calloc(1, sizeof(struct ToriDraw_ModelHD));
    struct ToriDraw_Model* m = &hd->base;
    /* z per corner, per quad: A tilts one way about y, B the other. */
    static const int qz[2][4] = { { -XZ, XZ, XZ, -XZ }, { XZ, -XZ, -XZ, XZ } };

    m->vertex_count = 8;
    m->face_count = 4;
    m->textured_face_count = 2;

    m->vertices_x = (vertexint_t*)calloc(8, sizeof(vertexint_t));
    m->vertices_y = (vertexint_t*)calloc(8, sizeof(vertexint_t));
    m->vertices_z = (vertexint_t*)calloc(8, sizeof(vertexint_t));
    m->original_vertices_x = (vertexint_t*)calloc(8, sizeof(vertexint_t));
    m->original_vertices_y = (vertexint_t*)calloc(8, sizeof(vertexint_t));
    m->original_vertices_z = (vertexint_t*)calloc(8, sizeof(vertexint_t));

    m->face_indices_a = (faceint_t*)calloc(4, sizeof(faceint_t));
    m->face_indices_b = (faceint_t*)calloc(4, sizeof(faceint_t));
    m->face_indices_c = (faceint_t*)calloc(4, sizeof(faceint_t));
    m->face_colors_a = (hsl16_t*)calloc(4, sizeof(hsl16_t));
    m->face_colors_b = (hsl16_t*)calloc(4, sizeof(hsl16_t));
    m->face_colors_c = (hsl16_t*)calloc(4, sizeof(hsl16_t));
    m->face_infos = (int*)calloc(4, sizeof(int));
    m->face_textures = (faceint_t*)calloc(4, sizeof(faceint_t));
    m->face_texture_coords = (faceint_t*)calloc(4, sizeof(faceint_t));

    m->texture_render_types = (uint8_t*)calloc(2, sizeof(uint8_t));
    m->textured_p_coordinate = (faceint_t*)calloc(2, sizeof(faceint_t));
    m->textured_m_coordinate = (faceint_t*)calloc(2, sizeof(faceint_t));
    m->textured_n_coordinate = (faceint_t*)calloc(2, sizeof(faceint_t));

    for( int q = 0; q < 2; q++ )
    {
        int const v = q * 4;
        int const f = q * 2;

        m->vertices_x[v + 0] = -XW; m->vertices_y[v + 0] = -XH;
        m->vertices_x[v + 1] = XW;  m->vertices_y[v + 1] = -XH;
        m->vertices_x[v + 2] = XW;  m->vertices_y[v + 2] = XH;
        m->vertices_x[v + 3] = -XW; m->vertices_y[v + 3] = XH;
        for( int k = 0; k < 4; k++ )
            m->vertices_z[v + k] = (vertexint_t)qz[q][k];

        m->face_indices_a[f] = (faceint_t)(v + 0);
        m->face_indices_b[f] = (faceint_t)(v + 2);
        m->face_indices_c[f] = (faceint_t)(v + 1);
        m->face_indices_a[f + 1] = (faceint_t)(v + 0);
        m->face_indices_b[f + 1] = (faceint_t)(v + 3);
        m->face_indices_c[f + 1] = (faceint_t)(v + 2);

        for( int k = 0; k < 2; k++ )
        {
            m->face_colors_a[f + k] = 90;
            m->face_colors_b[f + k] = 90;
            m->face_colors_c[f + k] = 90;
            m->face_textures[f + k] = (faceint_t)q;
            m->face_texture_coords[f + k] = (faceint_t)q;
        }

        m->texture_render_types[q] = 0; /* the plane projector */
        m->textured_p_coordinate[q] = (faceint_t)(v + 0);
        m->textured_m_coordinate[q] = (faceint_t)(v + 1);
        m->textured_n_coordinate[q] = (faceint_t)(v + 3);
    }

    memcpy(m->original_vertices_x, m->vertices_x, 8 * sizeof(vertexint_t));
    memcpy(m->original_vertices_y, m->vertices_y, 8 * sizeof(vertexint_t));
    memcpy(m->original_vertices_z, m->vertices_z, 8 * sizeof(vertexint_t));

    ToriDraw_ModelSetBoundsCylinder(m);
    return hd;
}

/* Quad A's texture is red, quad B's is blue; which channel dominates says which
 * SURFACE won, with no dependence on the exact shade arithmetic. */
static int g_texels_red[TEX_W * TEX_W];
static int g_texels_blue[TEX_W * TEX_W];

/** 0 = quad A (red), 1 = quad B (blue), -1 = background or neither. */
static int
surface_at(const struct render_env* e, int x, int y)
{
    int const px = e->pixels[y * W + x];
    int const r = (px >> 16) & 0xFF;
    int const b = px & 0xFF;

    if( (px & 0x00FFFFFF) == 0 )
        return -1;
    if( r > b + 8 )
        return 0;
    if( b > r + 8 )
        return 1;
    return -1;
}

/**
 * How many drawn pixels show the farther surface.
 *
 * The quads meet on the model's x == 0 plane, which projects to the frame's
 * centre column, so left of it quad A is nearer and right of it quad B is.
 * `margin` skips a band about the meeting line where the two surfaces are within
 * a fraction of a unit of each other and either answer is defensible; a pixel
 * only one quad covers is on the near side of that quad by the same rule, so it
 * needs no special case.
 *
 * Counted over the whole overlap rather than sampled at two points: the face
 * sort ranks TRIANGLES, and a two-triangle quad can come out right at any
 * particular pixel by luck while still being unable to express the crossing.
 */
static void
count_crossing_errors(const struct render_env* e, int margin, long* out_wrong, long* out_checked)
{
    long wrong = 0;
    long checked = 0;

    for( int y = 0; y < H; y++ )
    {
        for( int x = 0; x < W; x++ )
        {
            int const dx = x - W / 2;
            int const got = surface_at(e, x, y);

            if( got < 0 || (dx > -margin && dx < margin) )
                continue;
            checked++;
            if( got != (dx < 0 ? 0 : 1) )
                wrong++;
        }
    }

    *out_wrong = wrong;
    *out_checked = checked;
}

/*
 * The case the whole feature exists for: two surfaces each in front of the other
 * somewhere. The sorted render CANNOT get both halves right — that is the
 * negative control, and without it "the depth render is correct" would be a
 * claim about a fixture that never needed depth.
 */
static void
test_zbuffered_resolves_interpenetration(struct render_env* e)
{
    printf("interpenetrating quads resolve per pixel, and the sort cannot\n");

    for( int i = 0; i < TEX_W * TEX_W; i++ )
    {
        g_texels_red[i] = (int)0xFFFF3030u;
        g_texels_blue[i] = (int)0xFF3030FFu;
    }

    struct ToriDraw_ModelHD* hd = build_crossing_model();
    struct ToriDraw_HDMaterial mats[2];
    memset(mats, 0, sizeof(mats));
    mats[0].texels = g_texels_red;
    mats[1].texels = g_texels_blue;
    mats[0].width = mats[1].width = TEX_W;

    struct ToriDraw_HDMaterials table = { mats, 2 };
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleFromHD(hd);

    long wrong;
    long checked;
    char detail[128];

    struct ToriDraw_HDRenderStats st = render(e, hnd, &table);
    check_eq(st.drawn_plane, 4, "all four faces drew through texplane");

    count_crossing_errors(e, 3, &wrong, &checked);
    snprintf(detail, sizeof(detail), "%ld of %ld pixels wrong", wrong, checked);
    check(checked > 2000, "the crossing covers enough pixels to judge", detail);
    check(
        wrong > checked / 8,
        "the painter's sort resolved the crossing — the fixture no longer exercises the "
        "case the depth kernels exist for", detail);

    render_zbuf(e, hnd, &table);
    count_crossing_errors(e, 3, &wrong, &checked);
    snprintf(detail, sizeof(detail), "%ld of %ld pixels show the farther quad", wrong, checked);
    check(wrong == 0, "the depth kernels resolve every pixel to the nearer surface", detail);

    ToriDraw_ModelHDFree(hd);
}

/* -------------------------------------------- type 0 with a foreign frame */

/*
 * One type-0 quad whose P/M/N frame is NOT the face's own plane: tilted about
 * y and sitting 100-220 units behind a 60-unit face, which is the RS727 regime
 * (every one of TzTok-Jad's 2320 type-0 faces uses a frame like this).
 *
 * Vertices 0-3 are the quad, 4-6 the frame.
 */
static struct ToriDraw_ModelHD*
build_foreign_frame_model(void)
{
    enum
    {
        NV = 7,
        NF = 2
    };
    struct ToriDraw_ModelHD* hd =
        (struct ToriDraw_ModelHD*)calloc(1, sizeof(struct ToriDraw_ModelHD));
    struct ToriDraw_Model* m = &hd->base;

    m->vertex_count = NV;
    m->face_count = NF;
    m->textured_face_count = 1;

    m->vertices_x = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->vertices_y = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->vertices_z = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->original_vertices_x = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->original_vertices_y = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->original_vertices_z = (vertexint_t*)calloc(NV, sizeof(vertexint_t));
    m->face_indices_a = (faceint_t*)calloc(NF, sizeof(faceint_t));
    m->face_indices_b = (faceint_t*)calloc(NF, sizeof(faceint_t));
    m->face_indices_c = (faceint_t*)calloc(NF, sizeof(faceint_t));
    m->face_colors_a = (hsl16_t*)calloc(NF, sizeof(hsl16_t));
    m->face_colors_b = (hsl16_t*)calloc(NF, sizeof(hsl16_t));
    m->face_colors_c = (hsl16_t*)calloc(NF, sizeof(hsl16_t));
    m->face_infos = (int*)calloc(NF, sizeof(int));
    m->face_textures = (faceint_t*)calloc(NF, sizeof(faceint_t));
    m->face_texture_coords = (faceint_t*)calloc(NF, sizeof(faceint_t));
    m->face_alphas = (alphaint_t*)calloc(NF, sizeof(alphaint_t));
    m->texture_render_types = (uint8_t*)calloc(1, sizeof(uint8_t));
    m->textured_p_coordinate = (faceint_t*)calloc(1, sizeof(faceint_t));
    m->textured_m_coordinate = (faceint_t*)calloc(1, sizeof(faceint_t));
    m->textured_n_coordinate = (faceint_t*)calloc(1, sizeof(faceint_t));

    /* The face: a 60x60 quad in the z == 0 plane, facing the camera. Every
     * vertex at one depth, so screen-space interpolation is affine over it and
     * the pixel at the screen centroid samples exactly the mean vertex uv. */
    int const qx[4] = { -30, 30, 30, -30 };
    int const qy[4] = { -30, -30, 30, 30 };
    for( int i = 0; i < 4; i++ )
    {
        m->vertices_x[i] = (vertexint_t)qx[i];
        m->vertices_y[i] = (vertexint_t)qy[i];
        m->vertices_z[i] = 0;
    }
    /* The frame: U = (200, 0, 120) tilts it 31 degrees off the face; P sits
     * 100 units behind the face, M 220. */
    m->vertices_x[4] = -90;
    m->vertices_y[4] = -90;
    m->vertices_z[4] = 100;
    m->vertices_x[5] = 110;
    m->vertices_y[5] = -90;
    m->vertices_z[5] = 220;
    m->vertices_x[6] = -90;
    m->vertices_y[6] = 110;
    m->vertices_z[6] = 100;

    m->face_indices_a[0] = 0;
    m->face_indices_b[0] = 2;
    m->face_indices_c[0] = 1;
    m->face_indices_a[1] = 0;
    m->face_indices_b[1] = 3;
    m->face_indices_c[1] = 2;
    for( int f = 0; f < NF; f++ )
    {
        /* Full lightness, so a texel channel comes back as (c * 254) >> 8 and
         * decodes without knowing anything else about the shade path. */
        m->face_colors_a[f] = 127;
        m->face_colors_b[f] = 127;
        m->face_colors_c[f] = 127;
        m->face_textures[f] = 0;
        m->face_texture_coords[f] = 0;
    }
    m->texture_render_types[0] = 0;
    m->textured_p_coordinate[0] = 4;
    m->textured_m_coordinate[0] = 5;
    m->textured_n_coordinate[0] = 6;

    memcpy(m->original_vertices_x, m->vertices_x, NV * sizeof(vertexint_t));
    memcpy(m->original_vertices_y, m->vertices_y, NV * sizeof(vertexint_t));
    memcpy(m->original_vertices_z, m->vertices_z, NV * sizeof(vertexint_t));
    ToriDraw_ModelSetBoundsCylinder(m);
    return hd;
}

/* Every texel names itself: R = 4u, G = 4v. Blue is a constant so an (0,0)
 * texel is still a drawn pixel to `covered`. */
static int g_texels_uv[TEX_W * TEX_W];

/** Decode the texel a rendered pixel came from. Shade is 254 of 256. */
static void
decode_uv_texel(int px, int* out_u, int* out_v)
{
    int const r = (px >> 16) & 0xFF;
    int const g = (px >> 8) & 0xFF;
    *out_u = (int)((r * 256.0 / 254.0) / 4.0 + 0.5);
    *out_v = (int)((g * 256.0 / 254.0) / 4.0 + 0.5);
}

/** |a - b| on a 64-texel repeat. */
static int
texel_dist(int a, int b)
{
    int d = a - b;
    if( d < 0 )
        d = -d;
    d &= TEX_W - 1;
    return d > TEX_W / 2 ? TEX_W - d : d;
}

/*
 * A type-0 face whose frame is off its own plane must draw the SAME texel at a
 * given point of the face from every viewpoint, and it must be the texel the
 * frame projection names.
 *
 * This is the property the eye-ray plane walk lacks: intersecting each pixel's
 * ray with a plane that is not the face's makes the texture a projector at the
 * eye, and moving the model 60 units sideways at this depth slides its
 * projection across the face by 3-7 texels. Under the frame kernel the uv is
 * solved per vertex in model space, and moving the model changes nothing.
 */
static void
test_foreign_frame_is_view_independent(struct render_env* e)
{
    printf("a type-0 face with an off-plane frame draws the same texel from every view\n");

    for( int v = 0; v < TEX_W; v++ )
        for( int u = 0; u < TEX_W; u++ )
            g_texels_uv[v * TEX_W + u] =
                (int)(0xFF000000u | ((unsigned)(u * 4) << 16) | ((unsigned)(v * 4) << 8) | 0x40u);

    struct ToriDraw_ModelHD* hd = build_foreign_frame_model();
    struct ToriDraw_Model* m = &hd->base;
    struct ToriDraw_HDMaterial mat;
    memset(&mat, 0, sizeof(mat));
    mat.texels = g_texels_uv;
    mat.width = TEX_W;
    struct ToriDraw_HDMaterials table = { &mat, 1 };
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleFromHD(hd);

    /* The fixture must actually be off-plane, or this proves nothing: the
     * frame's furthest vertex is 220 units off a face 60 units wide. */
    {
        int off = m->vertices_z[5] - m->vertices_z[0];
        char detail[64];
        snprintf(detail, sizeof(detail), "frame is %d units off a 60-unit face", off);
        check(off > 60, "the fixture's frame is genuinely off the face plane", detail);
    }

    /* What the frame projection says triangle 0's centroid samples. */
    int want_u;
    int want_v;
    {
        struct ToriDraw_TexPlaneFrame frame = {
            m->vertices_x[4], m->vertices_y[4], m->vertices_z[4],
            m->vertices_x[5], m->vertices_y[5], m->vertices_z[5],
            m->vertices_x[6], m->vertices_y[6], m->vertices_z[6],
        };
        int a = m->face_indices_a[0], b = m->face_indices_b[0], c = m->face_indices_c[0];
        float u[3];
        float vv[3];
        toridraw_texmap_project_plane(
            &frame, m->vertices_x[a], m->vertices_y[a], m->vertices_z[a], m->vertices_x[b],
            m->vertices_y[b], m->vertices_z[b], m->vertices_x[c], m->vertices_y[c],
            m->vertices_z[c], u, vv);
        float mu = (u[0] + u[1] + u[2]) / 3.0f;
        float mv = (vv[0] + vv[1] + vv[2]) / 3.0f;
        want_u = (int)(mu * TEX_W) & (TEX_W - 1);
        want_v = (int)(mv * TEX_W) & (TEX_W - 1);
    }

    /* +-60 units at 600 deep is a 6 degree swing of every eye ray, which
     * moves the old projector's hit on this frame by 3-7 texels — well past
     * the tolerance below — while keeping the face inside the 160-px frame. */
    int const offsets[3] = { -60, 0, 60 };
    int got_u[3];
    int got_v[3];
    int saved_x = e->pos.x;

    for( int i = 0; i < 3; i++ )
    {
        e->pos.x = offsets[i];
        struct ToriDraw_HDRenderStats st = render(e, hnd, &table);
        check_eq(st.drawn_plane, 2, "both faces routed as render type 0");

        /* Triangle 0's screen centroid, from the projection the render used. */
        int a = m->face_indices_a[0], b = m->face_indices_b[0], c = m->face_indices_c[0];
        int cx = (e->scene->screen_vertices_x[a] + e->scene->screen_vertices_x[b] +
                  e->scene->screen_vertices_x[c]) /
                     3 +
                 W / 2;
        int cy = (e->scene->screen_vertices_y[a] + e->scene->screen_vertices_y[b] +
                  e->scene->screen_vertices_y[c]) /
                     3 +
                 H / 2;
        int px = e->pixels[cy * W + cx];
        char detail[96];
        snprintf(detail, sizeof(detail), "view %d: centroid (%d,%d) is background", i, cx, cy);
        check((px & 0x00FFFFFF) != 0, "the face covers its own centroid", detail);
        decode_uv_texel(px, &got_u[i], &got_v[i]);

        snprintf(
            detail, sizeof(detail), "view %d: got texel (%d,%d), frame projection says (%d,%d)",
            i, got_u[i], got_v[i], want_u, want_v);
        check(
            texel_dist(got_u[i], want_u) <= 1 && texel_dist(got_v[i], want_v) <= 1,
            "the centroid samples the texel the frame projection names", detail);
    }
    e->pos.x = saved_x;

    for( int i = 1; i < 3; i++ )
    {
        char detail[96];
        snprintf(
            detail, sizeof(detail), "view 0 texel (%d,%d), view %d texel (%d,%d)", got_u[0],
            got_v[0], i, got_u[i], got_v[i]);
        check(
            texel_dist(got_u[i], got_u[0]) <= 1 && texel_dist(got_v[i], got_v[0]) <= 1,
            "moving the model does not move its texture", detail);
    }

    /*
     * Now POSE it: move the quad's four vertices as an animation would and
     * leave the frame's three where they are — a frame not rigged with the
     * faces that use it, which is what HD helper vertices are. The uv must
     * come from the bind pose (`original_vertices_*`, which the fixture
     * captured), so the centroid texel is unchanged. Solving from the posed
     * vertices instead moves it by 4-5 texels here.
     */
    {
        for( int i = 0; i < 4; i++ )
        {
            m->vertices_x[i] = (vertexint_t)(m->original_vertices_x[i] + 20);
            m->vertices_y[i] = (vertexint_t)(m->original_vertices_y[i] + 10);
        }
        render(e, hnd, &table);

        int a = m->face_indices_a[0], b = m->face_indices_b[0], c = m->face_indices_c[0];
        int cx = (e->scene->screen_vertices_x[a] + e->scene->screen_vertices_x[b] +
                  e->scene->screen_vertices_x[c]) /
                     3 +
                 W / 2;
        int cy = (e->scene->screen_vertices_y[a] + e->scene->screen_vertices_y[b] +
                  e->scene->screen_vertices_y[c]) /
                     3 +
                 H / 2;
        int pu;
        int pv;
        decode_uv_texel(e->pixels[cy * W + cx], &pu, &pv);
        char detail[96];
        snprintf(
            detail, sizeof(detail), "bind pose texel (%d,%d), posed texel (%d,%d)", got_u[1],
            got_v[1], pu, pv);
        check(
            texel_dist(pu, got_u[1]) <= 1 && texel_dist(pv, got_v[1]) <= 1,
            "posing the face does not move its texture", detail);
    }

    ToriDraw_ModelHDFree(hd);
}

int
main(void)
{
    ToriDraw_Init();
    init_texels();

    struct render_env e;
    env_init(&e);

    test_projection_routing(&e);
    test_plain_handle_falls_back(&e);
    test_missing_material_is_visible(&e);
    test_gate_alpha_modulate_selection(&e);
    test_routing_changes_pixels(&e);
    test_zbuffered_matches_when_nothing_overlaps(&e);
    test_zbuffered_resolves_interpenetration(&e);
    test_foreign_frame_is_view_independent(&e);

    env_free(&e);

    if( g_fail )
    {
        printf("\n%d of %d HD routing check(s) FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d HD routing checks passed\n", g_checks);
    return 0;
}
