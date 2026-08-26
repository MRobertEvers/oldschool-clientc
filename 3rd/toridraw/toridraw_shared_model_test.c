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
    struct ToriDraw_ModelHandle h1;
    struct ToriDraw_ModelHandle h2;
    struct ToriDraw_Model* p1;
    struct ToriDraw_Model* p2;
    int64_t const key = 1234;

    CHECK(store);
    /* Borrow CONSUMES what it is given: a model that owns everything goes in,
     * a placement that borrows its faces comes out, and the caller's pointer is
     * spent. That is the type change, and it is why these are handles. */
    h1 = ToriDraw_SharedFacesStoreBorrow(store, key, make_placement());
    h2 = ToriDraw_SharedFacesStoreBorrow(store, key, make_placement());

    CHECK(h1.kind == TORIDRAWMK_MODEL_LENT_FACES);
    CHECK(h2.kind == TORIDRAWMK_MODEL_LENT_FACES);
    CHECK(h1.u.lent->faces == h2.u.lent->faces);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);

    /* The private half is reachable only through the accessor that names it. */
    p1 = ToriDraw_ModelLentFacesPrivate(h1);
    p2 = ToriDraw_ModelLentFacesPrivate(h2);

    /* Twelve arrays on loan, vertices and face_infos each placement's own. */
    CHECK(p1->face_indices_a == p2->face_indices_a);
    CHECK(p1->face_colors == p2->face_colors);
    CHECK(p1->vertices_x != p2->vertices_x);
    CHECK(p1->face_infos != p2->face_infos);

    /* What the seam hide does. */
    p2->face_infos[0] = 2;
    CHECK(p2->face_infos[0] == 2);
    CHECK(p1->face_infos[0] == 0);

    ToriDraw_ModelHandleFree(h2);
    /* h1 is still a lender; the set goes only with the last one. */
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);
    ToriDraw_ModelHandleFree(h1);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 0);
    ToriDraw_SharedFacesStoreFree(store);
}

/* The lone lender: releasing it takes the set with it, and its own face_infos
 * was never in the loan to be freed twice. */
static void
test_single_lender_owns_its_face_infos(void)
{
    struct ToriDraw_SharedFacesStore* store = ToriDraw_SharedFacesStoreNew();
    struct ToriDraw_ModelHandle h;
    struct ToriDraw_Model* priv;

    CHECK(store);
    h = ToriDraw_SharedFacesStoreBorrow(store, 77, make_placement());
    CHECK(h.kind == TORIDRAWMK_MODEL_LENT_FACES);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 1);

    priv = ToriDraw_ModelLentFacesPrivate(h);
    priv->face_infos[1] = 2;
    CHECK(priv->face_infos[1] == 2);
    CHECK(priv->face_colors[1] == (hsl16_t)(0x4B40 + 1));

    ToriDraw_ModelHandleFree(h);
    CHECK(ToriDraw_SharedFacesStoreCount(store) == 0);
    ToriDraw_SharedFacesStoreFree(store);
}

/* The whole-model store: what comes back is a ToriDraw_SharedModel, and the
 * handle is what says so. There is no moment at which a writable
 * ToriDraw_Model* to this geometry exists -- Publish consumed the one the
 * caller built. */
static void
test_acquire_hands_back_a_shared_handle(void)
{
    struct ToriDraw_SharedModelStore* store = ToriDraw_SharedModelStoreNew();
    struct ToriDraw_ModelHandle miss;
    struct ToriDraw_ModelHandle published;
    struct ToriDraw_ModelHandle acquired;

    CHECK(store);

    miss = ToriDraw_SharedModelStoreAcquire(store, 900);
    CHECK(miss.kind == TORIDRAWMK_NONE);

    published = ToriDraw_SharedModelStorePublish(store, 900, make_placement());
    CHECK(published.kind == TORIDRAWMK_MODEL_SHARED);

    acquired = ToriDraw_SharedModelStoreAcquire(store, 900);
    CHECK(acquired.kind == TORIDRAWMK_MODEL_SHARED);
    CHECK(acquired.u.shared == published.u.shared);
    CHECK(ToriDraw_ModelRead(acquired) == &published.u.shared->base);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 1);

    /* Two holders: the publisher and the acquirer. */
    ToriDraw_ModelHandleFree(acquired);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 1);
    ToriDraw_ModelHandleFree(published);
    CHECK(ToriDraw_SharedModelStoreCount(store) == 0);
    ToriDraw_SharedModelStoreFree(store);
}

int
main(void)
{
    test_seam_hide_does_not_reach_the_sibling();
    test_single_lender_owns_its_face_infos();
    test_acquire_hands_back_a_shared_handle();

    if( g_fail )
    {
        fprintf(stderr, "shared_model_test: FAILED\n");
        return 1;
    }
    printf("shared_model_test: PASS\n");
    return 0;
}
