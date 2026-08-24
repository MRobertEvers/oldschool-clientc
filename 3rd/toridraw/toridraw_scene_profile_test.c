/* Scratch-buffer profile allocation and API contract. */
#include "toridraw.h"
#include "toridraw_font.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

struct ProfileExpectation
{
    enum ToriDraw_ScratchBufferSize size;
    int max_vertices;
    int max_faces;
    int priority_stride;
};

static void
check_profile(const struct ProfileExpectation* expected)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, expected->size);

    CHECK(scene != NULL);
    if( !scene )
        return;

    CHECK(scene->max_vertices == expected->max_vertices);
    CHECK(scene->max_faces == expected->max_faces);
    CHECK(scene->depth_levels == 1500);
    CHECK(scene->depth_stride == 512);
    CHECK(scene->priority_stride == expected->priority_stride);
    CHECK(scene->tmp_depth_faces != NULL);
    CHECK(scene->tmp_priority_faces != NULL);
    CHECK(scene->sm_faces_by_depth == NULL);
    ToriDraw_SceneFree(scene);
}

static void
seed_one_face_at_depth(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    int depth)
{
    scene->screen_vertices_x[0] = 0;
    scene->screen_vertices_y[0] = 0;
    scene->screen_vertices_z[0] = depth;
    scene->screen_vertices_x[1] = 0;
    scene->screen_vertices_y[1] = 1;
    scene->screen_vertices_z[1] = depth;
    scene->screen_vertices_x[2] = 1;
    scene->screen_vertices_y[2] = 0;
    scene->screen_vertices_z[2] = depth;
    ToriDraw_RenderModel2SortFaces(hnd, scene);
}

static void
check_selectable_depth_capacity(void)
{
    vertexint_t vertices[3] = { 0, 0, 0 };
    faceint_t face_a[1] = { 0 };
    faceint_t face_b[1] = { 1 };
    faceint_t face_c[1] = { 2 };
    struct ToriDraw_BoundsCylinder bounds = { 0 };
    struct ToriDraw_Model model = { 0 };
    struct ToriDraw_ModelHandle hnd = { 0 };
    struct ToriDraw_Scene* reference;
    struct ToriDraw_Scene* deep;

    model.vertex_count = 3;
    model.face_count = 1;
    model.vertices_x = vertices;
    model.vertices_y = vertices;
    model.vertices_z = vertices;
    model.face_indices_a = face_a;
    model.face_indices_b = face_b;
    model.face_indices_c = face_c;
    model.bounds_cylinder = &bounds;
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = &model;

    reference = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    deep = ToriDraw_SceneNew(
        TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(reference != NULL);
    CHECK(deep != NULL);
    if( !reference || !deep )
    {
        ToriDraw_SceneFree(reference);
        ToriDraw_SceneFree(deep);
        return;
    }

    CHECK(reference->depth_levels == 1500);
    CHECK(deep->depth_levels == 16384);
    CHECK(ToriDraw_SceneSize(
              TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_LOW_2K) >
          ToriDraw_SceneSize(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K));
    CHECK(ToriDraw_SceneSize(
              TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_LOW_2K) -
              ToriDraw_SceneSize(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K) ==
          (size_t)(16384 - 1500) * (512 + 1) * sizeof(faceint_t));

    /*
     * Animated QBD reaches a 4,791-unit conservative radius. The sorter adds
     * that radius to relative face depth, so representing it exactly wants
     * 2*4,791+1 = 9,583 levels -- more than the 1,500-level reference table.
     *
     * That used to mean the model VANISHED on the reference table: every face
     * quantised outside the buckets, the sort emitted nothing, and the model
     * was invisible while still picking. The table is now a budget on depth
     * PRECISION rather than a limit on model SIZE -- an oversized model is
     * bucketed through a right shift and sorts coarsely instead of
     * disappearing -- so both tables must order the face, and the deep table
     * (which needs no shift) remains exact.
     */
    bounds.min_z_depth_any_rotation = 4791;
    seed_one_face_at_depth(reference, hnd, 4791);
    seed_one_face_at_depth(deep, hnd, 4791);
    CHECK(ToriDraw_FaceOrderCount(reference) == 1);
    CHECK(ToriDraw_FaceOrder(reference)[0] == 0);
    CHECK(ToriDraw_FaceOrderCount(deep) == 1);
    CHECK(ToriDraw_FaceOrder(deep)[0] == 0);

    ToriDraw_SceneFree(reference);
    ToriDraw_SceneFree(deep);
}

static struct ToriDraw_Sprite**
one_pixel_sprite_array(uint32_t color)
{
    uint32_t* pixels = malloc(sizeof(*pixels));
    struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
    CHECK(pixels != NULL);
    CHECK(sprites != NULL);
    if( !pixels || !sprites )
    {
        free(pixels);
        free(sprites);
        return NULL;
    }
    pixels[0] = color;
    sprites[0] = ToriDraw_SpriteNewFromArgbOwned(pixels, 1, 1);
    CHECK(sprites[0] != NULL);
    if( !sprites[0] )
    {
        free(sprites);
        return NULL;
    }
    return sprites;
}

/* Map counts cannot distinguish replacement under an existing scene id. The
 * UI-facing revision must, especially for font metrics consumed by MEASURE_TEXT. */
static void
check_ui_asset_revision(void)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Font* font;
    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    uint64_t revision;
    uint32_t count;

    CHECK(scene != NULL);
    if( !scene )
        return;

    revision = ToriDraw_SceneUIAssetRevision(scene);
    sprites = one_pixel_sprite_array(0xff112233u);
    if( sprites )
        ToriDraw_SceneSpriteAdd(scene, 17, sprites, 1);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    count = ToriDraw_MapCount(scene->sprites_hmap);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    sprites = one_pixel_sprite_array(0xff445566u);
    if( sprites )
        ToriDraw_SceneSpriteAdd(scene, 17, sprites, 1);
    CHECK(ToriDraw_MapCount(scene->sprites_hmap) == count);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    ToriDraw_SceneSpriteRemove(scene, 17);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    ToriDraw_SceneSpriteRemove(scene, 17);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) == revision);

    font = calloc(1, sizeof(*font));
    CHECK(font != NULL);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    if( font )
        ToriDraw_SceneFontAdd(scene, 9, font);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    count = ToriDraw_MapCount(scene->fonts_hmap);
    font = calloc(1, sizeof(*font));
    CHECK(font != NULL);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    if( font )
        ToriDraw_SceneFontAdd(scene, 9, font);
    CHECK(ToriDraw_MapCount(scene->fonts_hmap) == count);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);

    revision = ToriDraw_SceneUIAssetRevision(scene);
    ToriDraw_SceneModelAdd(scene, 23, model);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    count = ToriDraw_MapCount(scene->models_hmap);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    ToriDraw_SceneModelAdd(scene, 23, model);
    CHECK(ToriDraw_MapCount(scene->models_hmap) == count);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    (void)ToriDraw_SceneModelRemove(scene, 23);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) != revision);
    revision = ToriDraw_SceneUIAssetRevision(scene);
    (void)ToriDraw_SceneModelRemove(scene, 23);
    CHECK(ToriDraw_SceneUIAssetRevision(scene) == revision);

    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    static const struct ProfileExpectation profiles[] = {
        { TORIDRAW_SCRATCH_BUFFER_LOW_2K, 2048, 4096, 4096 },
        { TORIDRAW_SCRATCH_BUFFER_MED_4K, 4096, 8192, 8192 },
        { TORIDRAW_SCRATCH_BUFFER_HIGH_8K, 8192, 16384, 16384 },
    };
    size_t low_bytes;
    size_t med_bytes;
    size_t high_bytes;
    struct ToriDraw_Scene* scene;

    for( size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++ )
        check_profile(&profiles[i]);

    check_selectable_depth_capacity();
    check_ui_asset_revision();

    low_bytes = ToriDraw_SceneSize(
        TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    med_bytes = ToriDraw_SceneSize(
        TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_MED_4K);
    high_bytes = ToriDraw_SceneSize(
        TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    CHECK(low_bytes < med_bytes);
    CHECK(med_bytes < high_bytes);
    CHECK(ToriDraw_SceneSize(TORIDRAW_SCENE_FULL, (enum ToriDraw_ScratchBufferSize)99) == 0);
    CHECK(ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, (enum ToriDraw_ScratchBufferSize)-1) == NULL);

    /* The existing SMALL flag still selects its CSR sorter; the size enum is
     * explicit and validated, but does not change that separate algorithm.
     * The depth flag remains an independent capacity axis in SMALL mode too. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K,
        TORIDRAW_SCRATCH_BUFFER_MED_4K);
    CHECK(scene != NULL);
    if( scene )
    {
        CHECK(scene->max_vertices == 1024);
        CHECK(scene->max_faces == 2048);
        CHECK(scene->depth_levels == 16384);
        CHECK(scene->tmp_depth_faces == NULL);
        CHECK(scene->sm_faces_by_depth != NULL);
        CHECK(scene->sm_prio_faces != NULL);
        ToriDraw_SceneFree(scene);
    }

    /* Texture laziness remains orthogonal to scratch capacity. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_LAZY_TEXTURES, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(scene != NULL);
    if( scene )
    {
        CHECK(scene->tex_state == NULL);
        CHECK(ToriDraw_SceneTexState(scene) != NULL);
        ToriDraw_SceneFree(scene);
    }

    if( failures )
        return 1;
    puts("toridraw scene profile tests: PASS");
    return 0;
}
