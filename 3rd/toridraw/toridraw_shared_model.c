#include "toridraw_shared_model.h"

#include "toridraw_model.h"

#include <assert.h>
#include <stdlib.h>

/*
 * Chained buckets, fixed width. A scene settles at a few hundred distinct loc
 * prototypes against tens of thousands of placements, so the table is under
 * load 1 and the chains are a link long; a wider or growable table would buy
 * nothing measurable and this one costs 4 KB. Chains lengthen rather than
 * break if a future scene carries more, which is a quality question and not a
 * correctness one.
 */
#define TORIDRAW_SHARED_MODEL_BUCKETS 1024

struct ToriDraw_SharedModel
{
    struct ToriDraw_Model* model;
    /* The store this entry belongs to. A model reaches its owner through
     * `shared_owner` and the owner has to be able to unlink itself, so the
     * entry carries the way back rather than making every release site pass a
     * store it has no other reason to know about. */
    struct ToriDraw_SharedModelStore* store;
    struct ToriDraw_SharedModel* next;
    int64_t key;
    /* Placements holding this model. The store is not one of them: it indexes
     * what is live, it does not retain anything, so the last release takes the
     * entry out with it. */
    int holders;
};

struct ToriDraw_SharedModelStore
{
    struct ToriDraw_SharedModel* buckets[TORIDRAW_SHARED_MODEL_BUCKETS];
    int count;
};

static size_t
shared_model_bucket(int64_t key)
{
    /* Splitmix64's finaliser. The keys are packed bitfields (loc id, shape,
     * rotation), so the low bits alone would pile every rotation of one loc
     * into four adjacent buckets. */
    uint64_t x = (uint64_t)key;

    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return (size_t)(x % TORIDRAW_SHARED_MODEL_BUCKETS);
}

struct ToriDraw_SharedModelStore*
ToriDraw_SharedModelStoreNew(void)
{
    struct ToriDraw_SharedModelStore* store = calloc(1, sizeof(*store));

    assert(store);
    return store;
}

void
ToriDraw_SharedModelStoreFree(struct ToriDraw_SharedModelStore* store)
{
    if( !store )
        return;
    /* Entries are removed by their last holder, so an occupied store here means
     * a placement outlived the scene that placed it. */
    assert(store->count == 0);
    free(store);
}

int
ToriDraw_SharedModelStoreCount(const struct ToriDraw_SharedModelStore* store)
{
    assert(store);
    return store->count;
}

struct ToriDraw_Model*
ToriDraw_SharedModelStoreAcquire(
    struct ToriDraw_SharedModelStore* store,
    int64_t key)
{
    struct ToriDraw_SharedModel* entry;

    assert(store);

    for( entry = store->buckets[shared_model_bucket(key)]; entry; entry = entry->next )
    {
        if( entry->key != key )
            continue;
        entry->holders++;
        return entry->model;
    }
    return NULL;
}

struct ToriDraw_Model*
ToriDraw_SharedModelStorePublish(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_SharedModel* entry;
    size_t bucket;

    assert(store);
    assert(model);
    /* Publishing a model that already belongs to a store would leave one of the
     * two entries owning geometry it can never free. */
    assert(!model->shared_owner);

    bucket = shared_model_bucket(key);
    for( entry = store->buckets[bucket]; entry; entry = entry->next )
        assert(entry->key != key && "key already published");

    entry = calloc(1, sizeof(*entry));
    assert(entry);
    entry->model = model;
    entry->store = store;
    entry->key = key;
    entry->holders = 1;
    entry->next = store->buckets[bucket];
    store->buckets[bucket] = entry;
    store->count++;

    model->shared_owner = entry;
    return model;
}

void
ToriDraw_SharedModelRelease(struct ToriDraw_SharedModel* shared)
{
    struct ToriDraw_SharedModelStore* store;
    struct ToriDraw_SharedModel** link;
    struct ToriDraw_Model* model;

    assert(shared);
    assert(shared->holders > 0);

    if( --shared->holders > 0 )
        return;

    store = shared->store;
    link = &store->buckets[shared_model_bucket(shared->key)];
    while( *link && *link != shared )
        link = &(*link)->next;
    assert(*link == shared);
    *link = shared->next;
    store->count--;

    /* Clear the back-pointer first: ToriDraw_ModelFree routes any model that
     * still has one straight back into this function. */
    model = shared->model;
    model->shared_owner = NULL;
    free(shared);
    ToriDraw_ModelFree(model);
}

struct ToriDraw_Model*
ToriDraw_SharedModelStoreBorrowTopology(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_SharedModel* entry;
    struct ToriDraw_Model* donor;

    assert(store);
    assert(model);
    assert(!model->shared_owner);
    assert(!model->borrowed_topology);

    donor = ToriDraw_SharedModelStoreAcquire(store, key);
    if( donor )
    {
        /*
         * The donor was cut from a model built from the same loc config at the
         * same rotation, so it describes the faces this build just produced.
         * A disagreement here means the key does not identify the topology,
         * and every placement past this one would draw the wrong faces.
         */
        assert(donor->face_count == model->face_count);
        assert(donor->textured_face_count == model->textured_face_count);

#define TORIDRAW_TOPOLOGY_ADOPT(field)                                                                 free(model->field);                                                                                model->field = donor->field;
        TORIDRAW_TOPOLOGY_FIELDS(TORIDRAW_TOPOLOGY_ADOPT)
#undef TORIDRAW_TOPOLOGY_ADOPT

        model->borrowed_topology = donor->shared_owner;
        return model;
    }

    /*
     * First placement of this topology. The donor is a shell with no vertices:
     * it exists to own the face arrays and to be counted, and the store's
     * holder bookkeeping frees it when the last placement lets go.
     */
    donor = ToriDraw_ModelNew(0, model->face_count, 0);
    donor->textured_face_count = model->textured_face_count;

#define TORIDRAW_TOPOLOGY_STEAL(field) donor->field = model->field;
    TORIDRAW_TOPOLOGY_FIELDS(TORIDRAW_TOPOLOGY_STEAL)
#undef TORIDRAW_TOPOLOGY_STEAL

    entry = ToriDraw_SharedModelStorePublish(store, key, donor)->shared_owner;
    model->borrowed_topology = entry;
    return model;
}
