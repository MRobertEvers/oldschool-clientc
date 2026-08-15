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

    env_free(&e);

    if( g_fail )
    {
        printf("\n%d of %d HD routing check(s) FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d HD routing checks passed\n", g_checks);
    return 0;
}
