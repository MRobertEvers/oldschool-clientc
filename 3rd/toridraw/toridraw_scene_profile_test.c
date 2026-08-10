/* Scratch-buffer profile allocation and API contract. */
#include "toridraw.h"

#include <stdio.h>

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
     * explicit and validated, but does not change that separate algorithm. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_MED_4K);
    CHECK(scene != NULL);
    if( scene )
    {
        CHECK(scene->max_vertices == 1024);
        CHECK(scene->max_faces == 2048);
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
