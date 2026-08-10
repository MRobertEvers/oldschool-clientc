/* Actor vs scene light regimes — ToriDraw_LightModelActor / Scene produce
 * different baked face colours for the same geometry, and LightSetProfiles
 * takes effect. Standalone; no cache. */
#include "toridraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while( 0 )

static struct ToriDraw_Model*
make_tri(void)
{
    struct ToriDraw_Model* m = ToriDraw_ModelNew(3, 1, 0);
    CHECK(m);
    m->vertices_x = calloc(3, sizeof(*m->vertices_x));
    m->vertices_y = calloc(3, sizeof(*m->vertices_y));
    m->vertices_z = calloc(3, sizeof(*m->vertices_z));
    m->face_indices_a = calloc(1, sizeof(*m->face_indices_a));
    m->face_indices_b = calloc(1, sizeof(*m->face_indices_b));
    m->face_indices_c = calloc(1, sizeof(*m->face_indices_c));
    m->face_colors = calloc(1, sizeof(*m->face_colors));
    m->face_colors_a = calloc(1, sizeof(*m->face_colors_a));
    m->face_colors_b = calloc(1, sizeof(*m->face_colors_b));
    m->face_colors_c = calloc(1, sizeof(*m->face_colors_c));
    CHECK(m->vertices_x && m->face_colors && m->face_colors_a);

    /* Right triangle with a non-axis normal so actor/scene directions diverge. */
    m->vertices_x[0] = 0;
    m->vertices_y[0] = 0;
    m->vertices_z[0] = 0;
    m->vertices_x[1] = 128;
    m->vertices_y[1] = 64;
    m->vertices_z[1] = 0;
    m->vertices_x[2] = 0;
    m->vertices_y[2] = 0;
    m->vertices_z[2] = 128;
    m->face_indices_a[0] = 0;
    m->face_indices_b[0] = 1;
    m->face_indices_c[0] = 2;
    /* Non-zero lightness in the low 7 bits — lighting_multiply_hsl16 multiplies
     * by (hsl & 0x7f), so a colour with L=0 always clamps to the same output. */
    m->face_colors[0] = (hsl16_t)0x4B40;
    return m;
}

static void
test_actor_differs_from_scene(void)
{
    struct ToriDraw_Model* a = make_tri();
    struct ToriDraw_Model* s = make_tri();
    struct ToriDraw_ModelHandle ha = { .kind = TORIDRAWMK_MODEL, .u.model.model = a };
    struct ToriDraw_ModelHandle hs = { .kind = TORIDRAWMK_MODEL, .u.model.model = s };

    ToriDraw_LightModelActor(ha, 0, 0);
    ToriDraw_LightModelScene(hs, 0, 0);

    CHECK(a->face_colors_a[0] != 0);
    CHECK(s->face_colors_a[0] != 0);
    CHECK(a->face_colors_a[0] != s->face_colors_a[0]);

    ToriDraw_ModelFree(a);
    ToriDraw_ModelFree(s);
}

static void
test_set_profiles_takes_effect(void)
{
    struct ToriDraw_LightProfile actor = {
        .ambient = 64,
        .attenuation = 850,
        .src_x = -30,
        .src_y = -50,
        .src_z = -30,
    };
    struct ToriDraw_LightProfile flat = {
        .ambient = 200,
        .attenuation = 100,
        .src_x = 0,
        .src_y = -50,
        .src_z = 0,
    };
    struct ToriDraw_Model* m0 = make_tri();
    struct ToriDraw_Model* m1 = make_tri();
    struct ToriDraw_ModelHandle h0 = { .kind = TORIDRAWMK_MODEL, .u.model.model = m0 };
    struct ToriDraw_ModelHandle h1 = { .kind = TORIDRAWMK_MODEL, .u.model.model = m1 };

    ToriDraw_LightSetProfiles(&actor, NULL);
    ToriDraw_LightModelActor(h0, 0, 0);

    ToriDraw_LightSetProfiles(&flat, NULL);
    ToriDraw_LightModelActor(h1, 0, 0);

    CHECK(m0->face_colors_a[0] != m1->face_colors_a[0]);

    /* Restore reference defaults so later tests / other TU runs stay clean. */
    {
        struct ToriDraw_LightProfile restore_actor = {
            .ambient = 64, .attenuation = 850, .src_x = -30, .src_y = -50, .src_z = -30
        };
        struct ToriDraw_LightProfile restore_scene = {
            .ambient = 64, .attenuation = 768, .src_x = -50, .src_y = -10, .src_z = -50
        };
        ToriDraw_LightSetProfiles(&restore_actor, &restore_scene);
    }

    ToriDraw_ModelFree(m0);
    ToriDraw_ModelFree(m1);
}

static void
test_profile_readers(void)
{
    struct ToriDraw_LightProfile const* a = ToriDraw_LightActorProfile();
    struct ToriDraw_LightProfile const* s = ToriDraw_LightSceneProfile();
    CHECK(a && s);
    CHECK(a->attenuation == 850);
    CHECK(s->attenuation == 768);
    CHECK(a->src_y == -50);
    CHECK(s->src_y == -10);
}

int
main(void)
{
    test_profile_readers();
    test_actor_differs_from_scene();
    test_set_profiles_takes_effect();

    if( g_fail )
    {
        fprintf(stderr, "light_model_test: FAILED\n");
        return 1;
    }
    printf("light_model_test: PASS\n");
    return 0;
}
