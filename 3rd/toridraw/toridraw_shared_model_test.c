/*
 * Half-shared placements: the loan must not cover what the world build writes.
 *
 * ToriDraw_SharedFacesStoreBorrow hands every placement of one loc the SAME
 * face arrays, which is most of a built model's bytes. One pass writes a face
 * array afterwards: World.shareLight hides the seam faces where two placements
 * of a sharelight loc meet, by setting face_infos to 2. Lending face_infos hid
 * the seam at every placement of the loc -- a run of identical wall segments
 * losing the faces of the one segment that happened to butt against a
 * neighbour, which reads on screen as a wall you can see straight through from
 * one side and not the other.
 *
 * Both reference clients draw the line in the same place -- `Model(sharelight,
 * 0, proto, hillskew)` in the deob, `Model.hillSkewCopy(model, hillskew,
 * sharelight)` in Client-TS -- so this pins that ours does too: twelve arrays
 * lent, face_infos always the placement's own.
 */
#include "toridraw_model.h"
#include "toridraw_shared_model.h"

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

#define TEST_FACE_COUNT 2

/* One placement's freshly built model: the same geometry every time, which is
 * what the store's key promises about two placements of one loc. */
static struct ToriDraw_Model*
make_placement(void)
{
    struct ToriDraw_Model* m = ToriDraw_ModelNew(4, TEST_FACE_COUNT, 0);

    CHECK(m);
    m->vertices_x = calloc(4, sizeof(*m->vertices_x));
    m->vertices_y = calloc(4, sizeof(*m->vertices_y));
    m->vertices_z = calloc(4, sizeof(*m->vertices_z));
    m->face_indices_a = calloc(TEST_FACE_COUNT, sizeof(*m->face_indices_a));
    m->face_indices_b = calloc(TEST_FACE_COUNT, sizeof(*m->face_indices_b));
    m->face_indices_c = calloc(TEST_FACE_COUNT, sizeof(*m->face_indices_c));
    m->face_colors = calloc(TEST_FACE_COUNT, sizeof(*m->face_colors));
    m->face_infos = calloc(TEST_FACE_COUNT, sizeof(*m->face_infos));
    CHECK(m->vertices_x && m->face_indices_a && m->face_infos);

    for( int v = 0; v < 4; v++ )
        m->vertices_x[v] = (vertexint_t)(v * 64);
    for( int f = 0; f < TEST_FACE_COUNT; f++ )
    {
        m->face_indices_a[f] = (faceint_t)f;
        m->face_indices_b[f] = (faceint_t)(f + 1);
        m->face_indices_c[f] = (faceint_t)(f + 2);
        m->face_colors[f] = (hsl16_t)(0x4B40 + f);
    }
    return m;
}

/*
 * A sharelight loc: two placements of one wall, and the seam hide the
 * neighbour merge performs on exactly one of them.
 */
static void
test_seam_hide_does_not_reach_the_sibling(void)
{
    struct ToriDraw_SharedFacesStore* store = ToriDraw_SharedFacesStoreNew();
    struct ToriDraw_Model* first = make_placement();
    struct ToriDraw_Model* second = make_placement();
    int64_t const key = 1234;

    CHECK(store);
    ToriDraw_SharedFacesStoreBorrow(store, key, first);
    ToriDraw_SharedFacesStoreBorrow(store, key, second);

    /* Twelve arrays on loan, vertices and face_infos each placement's own. */
    CHECK(first->shared_faces != NULL);
    CHECK(second->shared_faces != NULL);
    CHECK(first->face_indices_a == second->face_indices_a);
    CHECK(first->face_colors == second->face_colors);
    CHECK(first->vertices_x != second->vertices_x);
    CHECK(first->face_infos != second->face_infos);
    CHECK(first->shared_faces == second->shared_faces);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);

    /* What the seam hide does. */
    second->face_infos[0] = 2;
    CHECK(second->face_infos[0] == 2);
    CHECK(first->face_infos[0] == 0);

    ToriDraw_ModelFree(second);
    /* `first` is still a lender; the set goes only with the last one. */
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);
    ToriDraw_ModelFree(first);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 0);
    ToriDraw_SharedFacesStoreFree(store);
}

/* The lone lender: releasing it takes the set with it, and the placement's own
 * face_infos was never in it to be freed twice. */
static void
test_single_lender_owns_its_face_infos(void)
{
    struct ToriDraw_SharedFacesStore* store = ToriDraw_SharedFacesStoreNew();
    struct ToriDraw_Model* only = make_placement();
    int* const infos = only->face_infos;

    CHECK(store);
    ToriDraw_SharedFacesStoreBorrow(store, 77, only);
    CHECK(only->face_infos == infos);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);

    only->face_infos[1] = 2;
    CHECK(only->face_infos[1] == 2);
    CHECK(only->face_colors[1] == (hsl16_t)(0x4B40 + 1));

    ToriDraw_ModelFree(only);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 0);
    ToriDraw_SharedFacesStoreFree(store);
}

int
main(void)
{
    test_seam_hide_does_not_reach_the_sibling();
    test_single_lender_owns_its_face_infos();

    if( g_fail )
    {
        fprintf(stderr, "shared_model_test: FAILED\n");
        return 1;
    }
    printf("shared_model_test: PASS\n");
    return 0;
}
