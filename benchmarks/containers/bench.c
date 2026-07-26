/*
 * Container lookup microbenchmark.
 *
 * Measures ID-keyed lookup cost across:
 *   flat-direct, flat-binary, flat-linear, vec-linear,
 *   hmap-fnv, hmap-intmix, rbtree, list-malloc, list-pool
 *
 * From this directory:
 *   make && ./bench_containers
 *   ./bench_containers --csv
 */

#include "bench_object.h"
#include "bench_vec.h"
#include "bench_list.h"
#include "bench_rbtree.h"

#include <hmap.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Config ---- */

static const size_t g_ns[] = { 8, 16, 32, 64, 128, 256, 512, 1024, 2048 };
#define BENCH_N_COUNT (sizeof(g_ns) / sizeof(g_ns[0]))

enum Dist
{
    DIST_DENSE = 0,
    DIST_SPARSE = 1,
    DIST_COUNT = 2
};

enum Mix
{
    MIX_HIT = 0,
    MIX_MISS = 1,
    MIX_MIXED = 2,
    MIX_COUNT = 3
};

enum ContKind
{
    CONT_FLAT_DIRECT = 0,
    CONT_FLAT_BINARY,
    CONT_FLAT_LINEAR,
    CONT_VEC_LINEAR,
    CONT_HMAP_FNV,
    CONT_HMAP_INTMIX,
    CONT_RBTREE,
    CONT_LIST_MALLOC,
    CONT_LIST_POOL,
    CONT_COUNT
};

static const char* const g_cont_names[CONT_COUNT] = {
    "flat-direct", "flat-binary", "flat-linear", "vec-linear",
    "hmap-fnv",    "hmap-intmix", "rbtree",      "list-malloc",
    "list-pool"
};

static const char* const g_dist_names[DIST_COUNT] = { "dense", "sparse" };
static const char* const g_mix_names[MIX_COUNT] = { "hit", "miss", "mixed" };

/* flat-direct only makes sense for dense IDs */
static int
cont_applicable(enum ContKind k, enum Dist d)
{
    if( k == CONT_FLAT_DIRECT && d != DIST_DENSE )
        return 0;
    return 1;
}

/* ---- hmap helpers ---- */

struct MapEntry_Bench
{
    uint32_t id;
    struct BenchObject obj;
};

static size_t
hmap_buffer_bytes(size_t entry_size, size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static size_t
hmap_pow2_cap(size_t n)
{
    /* ~50% load factor */
    size_t need = n * 2;
    if( need < 16 )
        need = 16;
    size_t cap = 16;
    while( cap < need )
        cap <<= 1;
    return cap;
}

static uint64_t
hmap_hash_intmix(const void* key, size_t key_size, void* arg)
{
    (void)key_size;
    (void)arg;
    uint32_t x = *(const uint32_t*)key;
    /* splitmix32 */
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    uint64_t h = (uint64_t)x;
    if( h == 0 )
        h = 1;
    return h;
}

static int
hmap_eq_u32(const void* a, const void* b, size_t key_size, void* arg)
{
    (void)key_size;
    (void)arg;
    return *(const uint32_t*)a == *(const uint32_t*)b;
}

struct BenchHMap
{
    struct HMap* map;
    void* buffer;
};

static void
bench_hmap_create(
    struct BenchHMap* hm,
    size_t n,
    hmap_hash_fn hash_fn)
{
    assert(hm);
    size_t capacity = hmap_pow2_cap(n);
    size_t buffer_size =
        hmap_buffer_bytes(sizeof(struct MapEntry_Bench), capacity);
    void* buffer = malloc(buffer_size);
    if( !buffer )
        bench_die("hmap buffer alloc failed");
    memset(buffer, 0, buffer_size);

    struct HashConfig config = {
        .buffer = buffer,
        .buffer_size = buffer_size,
        .key_size = sizeof(uint32_t),
        .entry_size = sizeof(struct MapEntry_Bench),
        .capacity = capacity,
        .hash_fn_nullable = hash_fn,
        .eq_fn_nullable = hmap_eq_u32,
        .arg_nullable = NULL,
    };
    struct HMap* map = hmap_new(&config, 0);
    if( !map )
        bench_die("hmap_new failed");
    hm->map = map;
    hm->buffer = buffer;
}

static void
bench_hmap_insert(struct BenchHMap* hm, const struct BenchObject* obj)
{
    assert(hm && hm->map && obj);
    struct MapEntry_Bench* e =
        (struct MapEntry_Bench*)hmap_search(hm->map, &obj->id, HMAP_INSERT);
    assert(e && "hmap insert must succeed at planned load factor");
    e->id = obj->id;
    e->obj = *obj;
}

static const struct BenchObject*
bench_hmap_find(const struct BenchHMap* hm, uint32_t id)
{
    assert(hm && hm->map);
    struct MapEntry_Bench* e =
        (struct MapEntry_Bench*)hmap_search(hm->map, &id, HMAP_FIND);
    return e ? &e->obj : NULL;
}

static void
bench_hmap_free(struct BenchHMap* hm)
{
    assert(hm);
    if( hm->map )
    {
        void* buf = hmap_free(hm->map);
        (void)buf;
        free(hm->buffer);
    }
    hm->map = NULL;
    hm->buffer = NULL;
}

/* ---- Flat array helpers ---- */

static int
cmp_bench_object_id(const void* a, const void* b)
{
    const struct BenchObject* oa = (const struct BenchObject*)a;
    const struct BenchObject* ob = (const struct BenchObject*)b;
    if( oa->id < ob->id )
        return -1;
    if( oa->id > ob->id )
        return 1;
    return 0;
}

static const struct BenchObject*
flat_binary_find(const struct BenchObject* arr, size_t n, uint32_t id)
{
    size_t lo = 0;
    size_t hi = n;
    while( lo < hi )
    {
        size_t mid = lo + (hi - lo) / 2;
        if( arr[mid].id < id )
            lo = mid + 1;
        else if( arr[mid].id > id )
            hi = mid;
        else
            return &arr[mid];
    }
    return NULL;
}

static const struct BenchObject*
flat_linear_find(const struct BenchObject* arr, size_t n, uint32_t id)
{
    for( size_t i = 0; i < n; i++ )
    {
        if( arr[i].id == id )
            return &arr[i];
    }
    return NULL;
}

static const struct BenchObject*
flat_direct_find(const struct BenchObject* arr, size_t n, uint32_t id)
{
    if( id >= (uint32_t)n )
        return NULL;
    /* Dense: objects[i].id == i. Absent ids are never stored for dense;
     * for safety treat out-of-range as miss. */
    return &arr[id];
}

static const struct BenchObject*
vec_linear_find(const struct BenchObject* v, uint32_t id)
{
    size_t n = bench_vec_len(v);
    return flat_linear_find(v, n, id);
}

/* ---- Workload generation ---- */

struct Workload
{
    size_t n;
    enum Dist dist;
    enum Mix mix;
    uint32_t* member_ids;   /* length n, the IDs stored */
    uint8_t present[BENCH_ID_MAX];
    uint32_t queries[BENCH_QUERY_COUNT];
    struct BenchObject* objects; /* length n, parallel to member_ids */
};

static void
workload_build(struct Workload* w, size_t n, enum Dist dist, enum Mix mix, uint32_t seed)
{
    assert(w);
    assert(n > 0 && n <= BENCH_ID_MAX);
    memset(w, 0, sizeof(*w));
    w->n = n;
    w->dist = dist;
    w->mix = mix;

    struct BenchRng rng;
    bench_rng_seed(&rng, seed);

    w->member_ids = (uint32_t*)malloc(n * sizeof(uint32_t));
    w->objects = (struct BenchObject*)malloc(n * sizeof(struct BenchObject));
    if( !w->member_ids || !w->objects )
        bench_die("workload alloc failed");

    if( dist == DIST_DENSE )
    {
        for( size_t i = 0; i < n; i++ )
            w->member_ids[i] = (uint32_t)i;
    }
    else
    {
        /* Sample n distinct IDs from 0..BENCH_ID_MAX-1 */
        uint32_t pool[BENCH_ID_MAX];
        for( uint32_t i = 0; i < BENCH_ID_MAX; i++ )
            pool[i] = i;
        bench_shuffle_u32(pool, BENCH_ID_MAX, &rng);
        for( size_t i = 0; i < n; i++ )
            w->member_ids[i] = pool[i];
    }

    memset(w->present, 0, sizeof(w->present));
    for( size_t i = 0; i < n; i++ )
    {
        uint32_t id = w->member_ids[i];
        assert(id < BENCH_ID_MAX);
        w->present[id] = 1;
        bench_object_init(&w->objects[i], id);
    }

    /* Shuffled insertion order copy lives in member_ids after this shuffle. */
    bench_shuffle_u32(w->member_ids, n, &rng);
    /* Reorder objects to match shuffled member_ids. */
    for( size_t i = 0; i < n; i++ )
        bench_object_init(&w->objects[i], w->member_ids[i]);

    /* Build miss pool */
    uint32_t miss_ids[BENCH_ID_MAX];
    size_t miss_count = 0;
    if( dist == DIST_DENSE )
    {
        /* Miss keys are >= n and < BENCH_ID_MAX (or wrap with offset). */
        for( uint32_t id = (uint32_t)n; id < BENCH_ID_MAX; id++ )
            miss_ids[miss_count++] = id;
        /* If n == BENCH_ID_MAX there are no in-range misses; use synthetic
         * out-of-range IDs that every container treats as absent. */
        if( miss_count == 0 )
        {
            for( uint32_t k = 0; k < 64; k++ )
                miss_ids[miss_count++] = BENCH_ID_MAX + k;
        }
    }
    else
    {
        for( uint32_t id = 0; id < BENCH_ID_MAX; id++ )
        {
            if( !w->present[id] )
                miss_ids[miss_count++] = id;
        }
        assert(miss_count > 0 || n == BENCH_ID_MAX);
        if( miss_count == 0 )
        {
            for( uint32_t k = 0; k < 64; k++ )
                miss_ids[miss_count++] = BENCH_ID_MAX + k;
        }
    }

    uint32_t hit_ids[BENCH_ID_MAX];
    size_t hit_count = n;
    for( size_t i = 0; i < n; i++ )
        hit_ids[i] = w->member_ids[i];

    for( size_t q = 0; q < BENCH_QUERY_COUNT; q++ )
    {
        int want_hit;
        if( mix == MIX_HIT )
            want_hit = 1;
        else if( mix == MIX_MISS )
            want_hit = 0;
        else
            want_hit = (int)(bench_rng_u32(&rng) & 1u);

        if( want_hit )
            w->queries[q] = hit_ids[bench_rng_range(&rng, (uint32_t)hit_count)];
        else
            w->queries[q] = miss_ids[bench_rng_range(&rng, (uint32_t)miss_count)];
    }
}

static void
workload_free(struct Workload* w)
{
    assert(w);
    free(w->member_ids);
    free(w->objects);
    memset(w, 0, sizeof(*w));
}

/* ---- Container bundle ---- */

struct ContBundle
{
    /* Flat: sorted by id (binary/linear). Direct uses dense layout. */
    struct BenchObject* flat_sorted;
    struct BenchObject* flat_direct; /* length n, index == id; dense only */
    size_t flat_n;

    struct BenchObject* vec; /* stb-style */

    struct BenchHMap hmap_fnv;
    struct BenchHMap hmap_intmix;

    struct BenchRbTree rbtree;

    struct BenchList list_malloc;
    struct BenchList list_pool;
};

static void
bundle_build(struct ContBundle* b, const struct Workload* w)
{
    assert(b && w);
    memset(b, 0, sizeof(*b));
    b->flat_n = w->n;

    b->flat_sorted =
        (struct BenchObject*)malloc(w->n * sizeof(struct BenchObject));
    if( !b->flat_sorted )
        bench_die("flat_sorted alloc failed");
    memcpy(b->flat_sorted, w->objects, w->n * sizeof(struct BenchObject));
    qsort(b->flat_sorted, w->n, sizeof(struct BenchObject), cmp_bench_object_id);

    if( w->dist == DIST_DENSE )
    {
        b->flat_direct =
            (struct BenchObject*)calloc(w->n, sizeof(struct BenchObject));
        if( !b->flat_direct )
            bench_die("flat_direct alloc failed");
        for( size_t i = 0; i < w->n; i++ )
        {
            uint32_t id = w->objects[i].id;
            assert(id < w->n);
            b->flat_direct[id] = w->objects[i];
        }
    }

    b->vec = NULL;
    for( size_t i = 0; i < w->n; i++ )
        bench_vec_push(b->vec, w->objects[i]);

    bench_hmap_create(&b->hmap_fnv, w->n, NULL); /* default FNV */
    bench_hmap_create(&b->hmap_intmix, w->n, hmap_hash_intmix);
    for( size_t i = 0; i < w->n; i++ )
    {
        bench_hmap_insert(&b->hmap_fnv, &w->objects[i]);
        bench_hmap_insert(&b->hmap_intmix, &w->objects[i]);
    }

    bench_rb_init(&b->rbtree);
    for( size_t i = 0; i < w->n; i++ )
        bench_rb_insert(&b->rbtree, &w->objects[i]);

    bench_list_init(&b->list_malloc, BENCH_LIST_MALLOC, w->n);
    bench_list_init(&b->list_pool, BENCH_LIST_POOL, w->n);
    for( size_t i = 0; i < w->n; i++ )
    {
        bench_list_insert(&b->list_malloc, &w->objects[i]);
        bench_list_insert(&b->list_pool, &w->objects[i]);
    }
}

static void
bundle_free(struct ContBundle* b)
{
    assert(b);
    free(b->flat_sorted);
    free(b->flat_direct);
    bench_vec_free(b->vec);
    bench_hmap_free(&b->hmap_fnv);
    bench_hmap_free(&b->hmap_intmix);
    bench_rb_free(&b->rbtree);
    bench_list_free(&b->list_malloc);
    bench_list_free(&b->list_pool);
    memset(b, 0, sizeof(*b));
}

static const struct BenchObject*
bundle_find(const struct ContBundle* b, enum ContKind k, uint32_t id)
{
    assert(b);
    switch( k )
    {
    case CONT_FLAT_DIRECT:
        assert(b->flat_direct);
        return flat_direct_find(b->flat_direct, b->flat_n, id);
    case CONT_FLAT_BINARY:
        return flat_binary_find(b->flat_sorted, b->flat_n, id);
    case CONT_FLAT_LINEAR:
        return flat_linear_find(b->flat_sorted, b->flat_n, id);
    case CONT_VEC_LINEAR:
        return vec_linear_find(b->vec, id);
    case CONT_HMAP_FNV:
        return bench_hmap_find(&b->hmap_fnv, id);
    case CONT_HMAP_INTMIX:
        return bench_hmap_find(&b->hmap_intmix, id);
    case CONT_RBTREE:
        return bench_rb_find(&b->rbtree, id);
    case CONT_LIST_MALLOC:
        return bench_list_find(&b->list_malloc, id);
    case CONT_LIST_POOL:
        return bench_list_find(&b->list_pool, id);
    default:
        assert(0 && "unknown container kind");
        return NULL;
    }
}

/* ---- Verify ---- */

static void
verify_bundle(const struct ContBundle* b, const struct Workload* w)
{
    assert(b && w);

    if( !bench_rb_validate(&b->rbtree) )
        bench_die("rbtree invariant check failed");

    /* Cross-check every key in 0..BENCH_ID_MAX-1 (and a few OOR misses). */
    for( uint32_t id = 0; id < BENCH_ID_MAX + 64u; id++ )
    {
        int expect_present = (id < BENCH_ID_MAX) && w->present[id];
        const struct BenchObject* ref = NULL;

        for( int ki = 0; ki < CONT_COUNT; ki++ )
        {
            enum ContKind k = (enum ContKind)ki;
            if( !cont_applicable(k, w->dist) )
                continue;

            const struct BenchObject* got = bundle_find(b, k, id);
            if( expect_present )
            {
                if( !got )
                {
                    fprintf(
                        stderr,
                        "verify: %s missed present id %u (n=%zu dist=%s)\n",
                        g_cont_names[k],
                        id,
                        w->n,
                        g_dist_names[w->dist]);
                    bench_die("verify failed");
                }
                if( got->id != id )
                {
                    fprintf(
                        stderr,
                        "verify: %s returned wrong id %u for key %u\n",
                        g_cont_names[k],
                        got->id,
                        id);
                    bench_die("verify failed");
                }
                if( !ref )
                    ref = got;
                else if( got->flags != ref->flags || got->hp != ref->hp )
                {
                    fprintf(
                        stderr,
                        "verify: %s payload mismatch for id %u\n",
                        g_cont_names[k],
                        id);
                    bench_die("verify failed");
                }
            }
            else
            {
                if( got )
                {
                    /* flat-direct: for dense, id >= n is miss; ids in range
                     * are always present. Sparse skips flat-direct. */
                    fprintf(
                        stderr,
                        "verify: %s returned object for absent id %u "
                        "(n=%zu dist=%s)\n",
                        g_cont_names[k],
                        id,
                        w->n,
                        g_dist_names[w->dist]);
                    bench_die("verify failed");
                }
            }
        }
    }
}

/* ---- Timing ---- */

struct TimedResult
{
    double min_ns;
    double med_ns;
    uint64_t checksum;
};

static int
cmp_double(const void* a, const void* b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    if( da < db )
        return -1;
    if( da > db )
        return 1;
    return 0;
}

static struct TimedResult
time_container(
    const struct ContBundle* b,
    enum ContKind k,
    const uint32_t* queries,
    size_t qcount)
{
    assert(b && queries && qcount > 0);

    volatile uint64_t sink = 0;
    double samples[BENCH_TIMED_PASSES];

    for( int pass = 0; pass < BENCH_WARMUP_PASSES; pass++ )
    {
        uint64_t acc = 0;
        for( size_t i = 0; i < qcount; i++ )
        {
            const struct BenchObject* o = bundle_find(b, k, queries[i]);
            if( o )
                acc = bench_object_checksum(o, acc);
            else
                acc ^= 0xdeadbeefull ^ queries[i];
        }
        sink = acc;
    }

    for( int pass = 0; pass < BENCH_TIMED_PASSES; pass++ )
    {
        uint64_t acc = 0;
        double t0 = now_seconds();
        for( size_t i = 0; i < qcount; i++ )
        {
            const struct BenchObject* o = bundle_find(b, k, queries[i]);
            if( o )
                acc = bench_object_checksum(o, acc);
            else
                acc ^= 0xdeadbeefull ^ queries[i];
        }
        double t1 = now_seconds();
        sink = acc;
        samples[pass] = (t1 - t0) * 1e9 / (double)qcount;
    }

    (void)sink;

    double min_ns = samples[0];
    for( int i = 1; i < BENCH_TIMED_PASSES; i++ )
    {
        if( samples[i] < min_ns )
            min_ns = samples[i];
    }
    qsort(samples, BENCH_TIMED_PASSES, sizeof(double), cmp_double);
    double med_ns = samples[BENCH_TIMED_PASSES / 2];

    struct TimedResult r;
    r.min_ns = min_ns;
    r.med_ns = med_ns;
    r.checksum = (uint64_t)sink;
    return r;
}

/* ---- Results storage & output ---- */

/* results[dist][mix][cont][n_idx] — NaN if not applicable */
static double g_min_ns[DIST_COUNT][MIX_COUNT][CONT_COUNT][BENCH_N_COUNT];
static double g_med_ns[DIST_COUNT][MIX_COUNT][CONT_COUNT][BENCH_N_COUNT];

static void
print_table(
    enum Dist dist,
    enum Mix mix,
    int use_min)
{
    printf(
        "\n=== %s / %s  (%s ns/lookup) ===\n",
        g_dist_names[dist],
        g_mix_names[mix],
        use_min ? "min" : "median");

    printf("%-12s", "container");
    for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
        printf(" %8zu", g_ns[ni]);
    printf("\n");
    printf("%-12s", "------------");
    for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
        printf(" --------");
    printf("\n");

    for( int ki = 0; ki < CONT_COUNT; ki++ )
    {
        if( !cont_applicable((enum ContKind)ki, dist) )
            continue;
        printf("%-12s", g_cont_names[ki]);
        for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
        {
            double v = use_min ? g_min_ns[dist][mix][ki][ni]
                               : g_med_ns[dist][mix][ki][ni];
            if( isnan(v) )
                printf(" %8s", "-");
            else
                printf(" %8.2f", v);
        }
        printf("\n");
    }
}

static void
print_csv(void)
{
    printf(
        "dist,mix,container,n,min_ns,med_ns\n");
    for( int d = 0; d < DIST_COUNT; d++ )
    {
        for( int m = 0; m < MIX_COUNT; m++ )
        {
            for( int k = 0; k < CONT_COUNT; k++ )
            {
                if( !cont_applicable((enum ContKind)k, (enum Dist)d) )
                    continue;
                for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
                {
                    double mn = g_min_ns[d][m][k][ni];
                    double md = g_med_ns[d][m][k][ni];
                    if( isnan(mn) )
                        continue;
                    printf(
                        "%s,%s,%s,%zu,%.4f,%.4f\n",
                        g_dist_names[d],
                        g_mix_names[m],
                        g_cont_names[k],
                        g_ns[ni],
                        mn,
                        md);
                }
            }
        }
    }
}

/* ---- Main ---- */

int
main(int argc, char** argv)
{
    int csv = 0;
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--csv") == 0 )
            csv = 1;
        else if(
            strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 )
        {
            printf(
                "Usage: %s [--csv]\n"
                "  Container lookup microbenchmark (ids 0..2047).\n",
                argv[0]);
            return 0;
        }
    }

    for( int d = 0; d < DIST_COUNT; d++ )
        for( int m = 0; m < MIX_COUNT; m++ )
            for( int k = 0; k < CONT_COUNT; k++ )
                for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
                {
                    g_min_ns[d][m][k][ni] = NAN;
                    g_med_ns[d][m][k][ni] = NAN;
                }

    fprintf(
        stderr,
        "Container lookup benchmark\n"
        "  N sweep: 8..2048  queries/pass: %d  warmup: %d  timed: %d\n"
        "  object size: %zu bytes\n",
        BENCH_QUERY_COUNT,
        BENCH_WARMUP_PASSES,
        BENCH_TIMED_PASSES,
        sizeof(struct BenchObject));

    for( size_t ni = 0; ni < BENCH_N_COUNT; ni++ )
    {
        size_t n = g_ns[ni];
        for( int di = 0; di < DIST_COUNT; di++ )
        {
            enum Dist dist = (enum Dist)di;
            for( int mi = 0; mi < MIX_COUNT; mi++ )
            {
                enum Mix mix = (enum Mix)mi;
                uint32_t seed =
                    0xC0FFEEu ^ (uint32_t)n ^ ((uint32_t)di << 16) ^
                    ((uint32_t)mi << 24);

                struct Workload w;
                workload_build(&w, n, dist, mix, seed);

                struct ContBundle b;
                bundle_build(&b, &w);
                verify_bundle(&b, &w);

                for( int ki = 0; ki < CONT_COUNT; ki++ )
                {
                    enum ContKind k = (enum ContKind)ki;
                    if( !cont_applicable(k, dist) )
                        continue;
                    struct TimedResult r =
                        time_container(&b, k, w.queries, BENCH_QUERY_COUNT);
                    g_min_ns[di][mi][ki][ni] = r.min_ns;
                    g_med_ns[di][mi][ki][ni] = r.med_ns;
                }

                bundle_free(&b);
                workload_free(&w);

                fprintf(
                    stderr,
                    "done n=%4zu dist=%-6s mix=%-5s\n",
                    n,
                    g_dist_names[di],
                    g_mix_names[mi]);
            }
        }
    }

    if( csv )
    {
        print_csv();
    }
    else
    {
        for( int di = 0; di < DIST_COUNT; di++ )
        {
            for( int mi = 0; mi < MIX_COUNT; mi++ )
            {
                print_table((enum Dist)di, (enum Mix)mi, 1);
                print_table((enum Dist)di, (enum Mix)mi, 0);
            }
        }
    }

    return 0;
}
