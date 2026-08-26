/*
 * Half-shared placements: what one of them writes must not reach the others.
 *
 * ToriDraw_SharedModelStoreBorrowTopology hands every placement of one loc the
 * SAME face arrays, which is most of a built model's bytes. That is safe only
 * while nothing writes them afterwards, and one pass does: World.shareLight
 * hides the seam faces where two placements meet by setting face_infos to 2.
 * Through the loan, that hid the seam at every placement of the loc -- a run of
 * identical wall segments losing the faces of the one segment that happened to
 * butt against a neighbour, which reads on screen as a wall you can see
 * straight through. ToriDraw_ModelUnborrowTopology is the door such a write
 * goes through, and this pins both halves of it: the arrays really are shared
 * until someone un-borrows, and after that the write is private.
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
 * Two placements of one loc, and the seam hide the sharelight pass performs on
 * exactly one of them.
 */
static void
test_hide_does_not_reach_the_sibling(void)
{
    struct ToriDraw_SharedModelStore* store = ToriDraw_SharedModelStoreNew();
    struct ToriDraw_Model* first = make_placement();
    struct ToriDraw_Model* second = make_placement();
    int64_t const key = 1234;

    CHECK(store);
    ToriDraw_SharedModelStoreBorrowTopology(store, key, first);
    ToriDraw_SharedModelStoreBorrowTopology(store, key, second);

    /* The loan itself: one array, two placements, vertices still their own. */
    CHECK(first->borrowed_topology != NULL);
    CHECK(second->borrowed_topology != NULL);
    CHECK(first->face_infos == second->face_infos);
    CHECK(first->face_indices_a == second->face_indices_a);
    CHECK(first->vertices_x != second->vertices_x);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 1);

    /* What the seam hide does, through the door it has to use. */
    ToriDraw_ModelUnborrowTopology(second);
    CHECK(second->borrowed_topology == NULL);
    CHECK(second->face_infos != first->face_infos);
    CHECK(second->face_indices_a != first->face_indices_a);
    CHECK(second->face_colors[1] == first->face_colors[1]);
    second->face_infos[0] = 2;

    CHECK(second->face_infos[0] == 2);
    CHECK(first->face_infos[0] == 0);

    ToriDraw_ModelFree(second);
    /* `first` still holds the donor; the store empties only on the last one. */
    CHECK(ToriDraw_SharedModelStoreCount(store) == 1);
    ToriDraw_ModelFree(first);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 0);
    ToriDraw_SharedModelStoreFree(store);
}

/* The lone placement is the interesting one for lifetime: un-borrowing drops
 * the donor's last holder, so the copy must already have been taken. */
static void
test_unborrow_of_the_only_holder(void)
{
    struct ToriDraw_SharedModelStore* store = ToriDraw_SharedModelStoreNew();
    struct ToriDraw_Model* only = make_placement();

    CHECK(store);
    ToriDraw_SharedModelStoreBorrowTopology(store, 77, only);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 1);

    ToriDraw_ModelUnborrowTopology(only);
    CHECK(only->borrowed_topology == NULL);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 0);
    /* Reads the copy, not the donor's freed array. */
    CHECK(only->face_colors[1] == (hsl16_t)(0x4B40 + 1));
    CHECK(only->face_indices_c[1] == 3);
    only->face_infos[1] = 2;
    CHECK(only->face_infos[1] == 2);

    ToriDraw_ModelFree(only);
    ToriDraw_SharedModelStoreFree(store);
}

/* Nothing to hand back is not an error: the pass calls this without knowing. */
static void
test_unborrow_is_a_no_op_when_private(void)
{
    struct ToriDraw_Model* m = make_placement();
    int* const infos = m->face_infos;

    ToriDraw_ModelUnborrowTopology(m);
    CHECK(m->face_infos == infos);
    CHECK(m->borrowed_topology == NULL);
    ToriDraw_ModelFree(m);
}

int
main(void)
{
    test_hide_does_not_reach_the_sibling();
    test_unborrow_of_the_only_holder();
    test_unborrow_is_a_no_op_when_private();

    if( g_fail )
    {
        fprintf(stderr, "shared_model_test: FAILED\n");
        return 1;
    }
    printf("shared_model_test: PASS\n");
    return 0;
}
