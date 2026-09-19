/*
 * A plugin-authored mesh, and the model built from it.
 *
 * The mesh is the plugin's SHAPE. It is appended to one vertex and one
 * triangle at a time by a script, and every object standing on it builds its
 * own model from these arrays -- so a growth bug here does not show up as a
 * crash in the growing, it shows up later as a model with somebody else's
 * geometry in it.
 *
 * What is asserted:
 *
 *   - the two halves grow independently and the counts they carry are not
 *     disturbed by growing the other one. The eight arrays of a half are one
 *     list seen eight ways and grow together.
 *   - growth preserves content across every reallocation, checked by
 *     appending past several doublings and reading the whole thing back.
 *   - asking for a capacity already held is free and does not move anything.
 *   - release leaves the mesh readable as a free slot, and a released mesh can
 *     be grown again from scratch without leaking the old arrays.
 *   - a built model copies the mesh rather than aliasing it: editing the mesh
 *     afterwards does not change a model already built, which is the whole
 *     reason the two have separate lifetimes.
 *   - the per-vertex colour arrays a built model carries are zeroed, because
 *     the lighting pass that follows is what fills them. A model shipped with
 *     uninitialised a/b/c renders as noise.
 *
 * Build and run:
 *   make -C src test-plugin-mesh
 */

#include "plugin/torirs_plugin_mesh.h"

#include "toridraw.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The shape of the real caller: append one vertex, growing first. */
static void
push_vertex(
    struct ToriRS_PluginMesh* mesh,
    int16_t x,
    int16_t y,
    int16_t z)
{
    ToriRS_PluginMeshGrow(mesh, 0, mesh->vertex_count + 1);
    mesh->vertices_x[mesh->vertex_count] = x;
    mesh->vertices_y[mesh->vertex_count] = y;
    mesh->vertices_z[mesh->vertex_count] = z;
    mesh->vertex_count++;
}

static void
push_face(
    struct ToriRS_PluginMesh* mesh,
    int16_t a,
    int16_t b,
    int16_t c,
    uint16_t color,
    uint8_t alpha)
{
    ToriRS_PluginMeshGrow(mesh, 1, mesh->face_count + 1);
    mesh->face_a[mesh->face_count] = a;
    mesh->face_b[mesh->face_count] = b;
    mesh->face_c[mesh->face_count] = c;
    mesh->face_color[mesh->face_count] = color;
    mesh->face_alpha[mesh->face_count] = alpha;
    mesh->face_count++;
}

static void
test_growth_keeps_what_was_written(void)
{
    struct ToriRS_PluginMesh mesh;
    int const count = 300; /* past 64 -> 128 -> 256 -> 512 */

    memset(&mesh, 0, sizeof(mesh));

    for( int i = 0; i < count; i++ )
        push_vertex(&mesh, (int16_t)i, (int16_t)(-i), (int16_t)(i * 2));

    CHECK(mesh.vertex_count == count, "vertex_count %d, want %d", mesh.vertex_count, count);
    CHECK(mesh.vertex_cap >= count, "vertex_cap %d below count %d", mesh.vertex_cap, count);
    CHECK(mesh.face_cap == 0, "growing vertices allocated the face half too");

    for( int i = 0; i < count; i++ )
    {
        CHECK(mesh.vertices_x[i] == (int16_t)i, "vertex %d x is %d", i, mesh.vertices_x[i]);
        CHECK(mesh.vertices_y[i] == (int16_t)(-i), "vertex %d y is %d", i, mesh.vertices_y[i]);
        CHECK(mesh.vertices_z[i] == (int16_t)(i * 2), "vertex %d z is %d", i, mesh.vertices_z[i]);
    }

    /* The other half, and the first half must survive it. */
    for( int i = 0; i < count; i++ )
        push_face(&mesh, (int16_t)i, (int16_t)(i + 1), (int16_t)(i + 2), (uint16_t)(i * 3), (uint8_t)(i & 0xFF));

    CHECK(mesh.face_count == count, "face_count %d, want %d", mesh.face_count, count);
    CHECK(mesh.vertex_count == count, "growing faces disturbed vertex_count");
    for( int i = 0; i < count; i++ )
    {
        CHECK(mesh.face_a[i] == (int16_t)i, "face %d a is %d", i, mesh.face_a[i]);
        CHECK(mesh.face_color[i] == (uint16_t)(i * 3), "face %d colour is %u", i, mesh.face_color[i]);
        CHECK(mesh.face_alpha[i] == (uint8_t)(i & 0xFF), "face %d alpha is %u", i, mesh.face_alpha[i]);
        CHECK(mesh.vertices_x[i] == (int16_t)i, "vertex %d x lost to a face grow", i);
    }

    /* Already big enough: nothing moves. */
    {
        int16_t const* before = mesh.vertices_x;
        int const cap_before = mesh.vertex_cap;

        ToriRS_PluginMeshGrow(&mesh, 0, 1);
        ToriRS_PluginMeshGrow(&mesh, 0, cap_before);
        CHECK(mesh.vertices_x == before, "a grow to a capacity already held reallocated");
        CHECK(mesh.vertex_cap == cap_before, "a grow to a capacity already held changed the cap");
    }

    ToriRS_PluginMeshRelease(&mesh);
    CHECK(!mesh.in_use, "a released mesh still reads as in use");
    CHECK(mesh.vertex_count == 0 && mesh.face_count == 0, "a released mesh kept its counts");
    CHECK(mesh.vertex_cap == 0 && mesh.face_cap == 0, "a released mesh kept its capacities");
    CHECK(mesh.vertices_x == NULL && mesh.face_a == NULL, "a released mesh kept its arrays");

    /* Re-usable: the zeroed caps mean the next grow allocates rather than
     * reallocating a freed pointer. */
    push_vertex(&mesh, 7, 8, 9);
    CHECK(mesh.vertex_count == 1 && mesh.vertices_x[0] == 7, "a released mesh could not be reused");
    ToriRS_PluginMeshRelease(&mesh);
}

static void
test_the_model_is_a_copy(void)
{
    struct ToriRS_PluginMesh mesh;
    struct ToriDraw_Model* model;

    memset(&mesh, 0, sizeof(mesh));
    push_vertex(&mesh, 0, 0, 0);
    push_vertex(&mesh, 100, 0, 0);
    push_vertex(&mesh, 0, 100, 0);
    push_face(&mesh, 0, 1, 2, 12345, 64);

    model = ToriRS_PluginMeshBuildModel(&mesh);
    CHECK(model != NULL, "no model built");
    if( !model )
        return;

    CHECK(model->vertex_count == 3, "model vertex_count %d, want 3", model->vertex_count);
    CHECK(model->face_count == 1, "model face_count %d, want 1", model->face_count);
    CHECK(model->vertices_x[1] == 100, "model vertex x is %d, want 100", model->vertices_x[1]);
    CHECK(model->face_colors[0] == 12345, "model face colour is %u", model->face_colors[0]);
    CHECK(model->face_alphas[0] == 64, "model face alpha is %u", model->face_alphas[0]);

    /* The per-vertex colours belong to the lighting pass that has not run yet.
     * Uninitialised here means a model that renders as noise. */
    CHECK(model->face_colors_a[0] == 0, "face_colors_a was not zeroed");
    CHECK(model->face_colors_b[0] == 0, "face_colors_b was not zeroed");
    CHECK(model->face_colors_c[0] == 0, "face_colors_c was not zeroed");

    /* Re-authoring the mesh must not reach into a model already built -- that
     * separation is the reason the mesh is not stored as a model. */
    mesh.vertices_x[1] = -999;
    mesh.face_color[0] = 1;
    CHECK(model->vertices_x[1] == 100, "editing the mesh changed a built model's vertex");
    CHECK(model->face_colors[0] == 12345, "editing the mesh changed a built model's colour");

    ToriDraw_ModelFree(model);
    ToriRS_PluginMeshRelease(&mesh);
}

int
main(void)
{
    test_growth_keeps_what_was_written();
    test_the_model_is_a_copy();

    if( g_failures )
    {
        printf("plugin_mesh_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_mesh_test: OK\n");
    return 0;
}
