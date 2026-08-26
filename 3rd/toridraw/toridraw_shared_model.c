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

struct ToriDraw_ModelHandle
ToriDraw_SharedModelStoreAcquire(
    struct ToriDraw_SharedModelStore* store,
    int64_t key)
{
    struct ToriDraw_ModelHandle hnd = { 0 };
    struct ToriDraw_SharedModel* entry;

    assert(store);

    for( entry = store->buckets[shared_model_bucket(key)]; entry; entry = entry->next )
    {
        if( entry->key != key )
            continue;
        entry->holders++;
        return ToriDraw_ModelHandleShared(entry);
    }
    /* TORIDRAWMK_NONE: no such key. */
    return hnd;
}

struct ToriDraw_ModelHandle
ToriDraw_SharedModelStorePublish(
    struct ToriDraw_SharedModelStore* store,
    int64_t key,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_SharedModel* entry;
    size_t bucket;

    assert(store);
    assert(model);

    bucket = shared_model_bucket(key);
    for( entry = store->buckets[bucket]; entry; entry = entry->next )
        assert(entry->key != key && "key already published");

    /* The shared model IS the entry: `base` by value at offset zero, so the
     * arrays move across with the shell and the caller's ToriDraw_Model is
     * spent. Taking ownership is the whole transaction -- there is no moment
     * afterwards at which a plain ToriDraw_Model* to this geometry exists. */
    entry = calloc(1, sizeof(*entry));
    assert(entry);
    entry->base = *model;
    free(model);
    entry->store = store;
    entry->key = key;
    entry->holders = 1;
    entry->next = store->buckets[bucket];
    store->buckets[bucket] = entry;
    store->count++;

    return ToriDraw_ModelHandleShared(entry);
}

void
ToriDraw_SharedModelRelease(struct ToriDraw_SharedModel* shared)
{
    struct ToriDraw_SharedModelStore* store;
    struct ToriDraw_SharedModel** link;

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

    /* The base is embedded, so its arrays go here and the shell goes with it.
     * ToriDraw_ModelFree is not the right call: it would free a shell this
     * struct does not have. */
    ToriDraw_ModelFree_arrays(&shared->base);
    free(shared);
}

/*
 * The lendable face buffers and the store that indexes them.
 *
 * Deliberately a separate store from the whole-model one rather than a second
 * key space inside it: two things with different types and different lifetimes
 * were being told apart by a tag bit in the key that both callers had to
 * remember to set. Two stores cannot collide.
 */
struct ToriDraw_SharedFaces
{
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    hsl16_t* face_colors;
    faceint_t* face_textures;
    alphaint_t* face_alphas;
    uint8_t* face_priorities;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    uint8_t* texture_render_types;
    faceint_t* face_texture_coords;

    /* What the arrays above are dimensioned by. A lender whose counts differ
     * is not a placement of the same loc, whatever its key says. */
    int face_count;
    int textured_face_count;

    /* Bookkeeping. The store this set belongs to, so a release can unlink
     * itself without every call site passing a store it has no other reason to
     * know about; and the lenders, which the store is not one of. */
    struct ToriDraw_SharedFacesStore* store;
    struct ToriDraw_SharedFaces* next;
    int64_t key;
    int lenders;
};

/* The member list above is spelled out because each one needs its own type, so
 * this is what stops it drifting from TORIDRAW_SHARED_FACE_FIELDS: add a field
 * to the macro and forget the member and this fails to compile, rather than
 * leaving one array still aliasing a freed set. */
enum
{
    TORIDRAW_SHARED_FACE_FIELD_COUNT = 0
#define TORIDRAW_SHARED_FACES_COUNT_ONE(field) +1
    TORIDRAW_SHARED_FACE_FIELDS(TORIDRAW_SHARED_FACES_COUNT_ONE)
#undef TORIDRAW_SHARED_FACES_COUNT_ONE
};
typedef char toridraw_shared_faces_has_a_member_per_field
    [TORIDRAW_SHARED_FACE_FIELD_COUNT == 12 ? 1 : -1];

struct ToriDraw_SharedFacesStore
{
    struct ToriDraw_SharedFaces* buckets[TORIDRAW_SHARED_MODEL_BUCKETS];
    int count;
};

struct ToriDraw_SharedFacesStore*
ToriDraw_SharedFacesStoreNew(void)
{
    struct ToriDraw_SharedFacesStore* store = calloc(1, sizeof(*store));

    assert(store);
    return store;
}

void
ToriDraw_SharedFacesStoreFree(struct ToriDraw_SharedFacesStore* store)
{
    if( !store )
        return;
    /* Sets are removed by their last lender, so an occupied store here means a
     * placement outlived the scene that placed it. */
    assert(store->count == 0);
    free(store);
}

int
ToriDraw_SharedFacesStoreCount(const struct ToriDraw_SharedFacesStore* store)
{
    assert(store);
    return store->count;
}

static struct ToriDraw_SharedFaces*
shared_faces_find(struct ToriDraw_SharedFacesStore* store, int64_t key)
{
    struct ToriDraw_SharedFaces* faces;

    for( faces = store->buckets[shared_model_bucket(key)]; faces; faces = faces->next )
    {
        if( faces->key == key )
            return faces;
    }
    return NULL;
}

struct ToriDraw_ModelHandle
ToriDraw_SharedFacesStoreBorrow(
    struct ToriDraw_SharedFacesStore* store,
    int64_t key,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_ModelLentFaces* lent;
    struct ToriDraw_SharedFaces* faces;
    size_t bucket;

    assert(store);
    assert(model);

    /* The placement changes type here: what the caller built owns everything,
     * and what comes back owns its vertices and borrows its faces. `base` is at
     * offset zero and copied by value, so the arrays move with the shell and
     * the caller's ToriDraw_Model is spent -- there is no moment afterwards at
     * which a plain, writable pointer to this geometry exists. */
    lent = calloc(1, sizeof(*lent));
    assert(lent);
    lent->base = *model;
    free(model);

    faces = shared_faces_find(store, key);
    if( faces )
    {
        /*
         * The set was cut from a model built from the same loc config at the
         * same rotation, so it describes the faces this build just produced.
         * A disagreement here means the key does not identify the topology,
         * and every placement past this one would draw the wrong faces.
         */
        assert(faces->face_count == lent->base.face_count);
        assert(faces->textured_face_count == lent->base.textured_face_count);

#define TORIDRAW_SHARED_FACES_ADOPT(field)                                                             free(lent->base.field);                                                                            lent->base.field = faces->field;
        TORIDRAW_SHARED_FACE_FIELDS(TORIDRAW_SHARED_FACES_ADOPT)
#undef TORIDRAW_SHARED_FACES_ADOPT

        faces->lenders++;
        lent->faces = faces;
        return ToriDraw_ModelHandleLentFaces(lent);
    }

    /* First placement of this topology: its arrays move into the set, and it
     * keeps aliases to them like every later lender. */
    faces = calloc(1, sizeof(*faces));
    assert(faces);
    faces->face_count = lent->base.face_count;
    faces->textured_face_count = lent->base.textured_face_count;
#define TORIDRAW_SHARED_FACES_TAKE(field) faces->field = lent->base.field;
    TORIDRAW_SHARED_FACE_FIELDS(TORIDRAW_SHARED_FACES_TAKE)
#undef TORIDRAW_SHARED_FACES_TAKE

    bucket = shared_model_bucket(key);
    faces->store = store;
    faces->key = key;
    faces->lenders = 1;
    faces->next = store->buckets[bucket];
    store->buckets[bucket] = faces;
    store->count++;

    lent->faces = faces;
    return ToriDraw_ModelHandleLentFaces(lent);
}

/**
 * Release a lent-faces placement: its own arrays, then its share of the loan.
 *
 * The private half and the borrowed half are freed by different rules, and the
 * type is what says which is which -- there is no flag to read and no way to
 * ask a plain model the question, because a plain model is never in this state.
 */
void
ToriDraw_ModelLentFacesFree(struct ToriDraw_ModelLentFaces* lent)
{
    if( !lent )
        return;

    /* The twelve lent arrays are the set's; drop the alias without freeing. */
#define TORIDRAW_SHARED_FACES_DISOWN(field) lent->base.field = NULL;
    TORIDRAW_SHARED_FACE_FIELDS(TORIDRAW_SHARED_FACES_DISOWN)
#undef TORIDRAW_SHARED_FACES_DISOWN

    ToriDraw_ModelFree_arrays(&lent->base);
    ToriDraw_SharedFacesRelease(lent->faces);
    free(lent);
}

void
ToriDraw_SharedFacesRelease(struct ToriDraw_SharedFaces* faces)
{
    struct ToriDraw_SharedFacesStore* store;
    struct ToriDraw_SharedFaces** link;

    assert(faces);
    assert(faces->lenders > 0);

    if( --faces->lenders > 0 )
        return;

    store = faces->store;
    link = &store->buckets[shared_model_bucket(faces->key)];
    while( *link && *link != faces )
        link = &(*link)->next;
    assert(*link == faces);
    *link = faces->next;
    store->count--;

#define TORIDRAW_SHARED_FACES_FREE(field) free(faces->field);
    TORIDRAW_SHARED_FACE_FIELDS(TORIDRAW_SHARED_FACES_FREE)
#undef TORIDRAW_SHARED_FACES_FREE
    free(faces);
}
