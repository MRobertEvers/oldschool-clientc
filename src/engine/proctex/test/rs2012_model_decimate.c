/*
 * rs2012_model_decimate.c — rig-preserving vertex decimation for OB3 models.
 *
 * The REDUCE half of the osrsify pipeline (tools/osrs_stylizer): takes one
 * imported high-poly part and produces a lower-vertex version that the same
 * animations still drive, by half-edge collapses restricted to the rig:
 *
 *   - a vertex may only collapse into a vertex of the SAME bone label
 *     (sequence frames transform label groups; a cross-label collapse would
 *     re-skin geometry, i.e. change what the animation moves);
 *   - vertices referenced by texture triangles (p/m/n) are pinned by default
 *     (--no-keep-textured lifts this and remaps the triangles instead);
 *   - the survivor keeps its position, so surviving geometry never drifts —
 *     what changes is only which vertices exist;
 *   - label-group centroid drift is priced into the cost: type-0 origin
 *     transforms average a label's vertices, so thinning one side of a group
 *     shifts every frame built on it. Penalty scales with 1/group_size^2 —
 *     small groups are fragile, big ones barely notice;
 *   - collapse cost is quadric error (area-weighted face planes + boundary
 *     constraint planes) plus penalties for collapsing color-, priority-,
 *     alpha- or texture-boundary vertices, all tunable from the CLI;
 *   - --seed/--jitter N pick uniformly among the N cheapest legal collapses,
 *     so an outer search can explore the space instead of replaying greedy.
 *
 * Output is the re-encoded model plus a JSON mapping. The encode is
 * provenance-less: collapsing changes the counts, which invalidates the
 * byte-exact provenance, so we rely on the encoder's "decodes back to an
 * equal struct" guarantee — and verify it here by decoding the output and
 * comparing every array. The vertex map has no holes: a removed vertex maps
 * to the live vertex it merged into, which IS the port of any per-vertex
 * data (and of the old animations, whose label groups all survive).
 *
 * Candidates are rescanned from the live faces every step — slower than a
 * heap, immune to stale-entry bugs; this tool sits inside overnight searches
 * where per-model seconds are irrelevant and silent corruption is not.
 */

#include "datatypes/model.h"
#include <assert.h>

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_MAX 64
#define AREA_EPS 1e-12

static void
die(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rs2012_model_decimate: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

/* ---------------------------------------------------------------- file io */

static uint8_t*
read_file(const char* path, int* out_size)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
        die("cannot open %s", path);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0)
        die("empty file %s", path);
    uint8_t* bytes = malloc((size_t)size);
    assert(bytes);
    if( fread(bytes, 1, (size_t)size, fp) != (size_t)size )
        die("short read on %s", path);
    fclose(fp);
    *out_size = (int)size;
    return bytes;
}

static void
write_file(const char* path, const uint8_t* bytes, uint32_t size)
{
    FILE* fp = fopen(path, "wb");
    if (!fp)
        die("cannot create %s", path);
    if (fwrite(bytes, 1, size, fp) != size)
        die("short write on %s", path);
    fclose(fp);
}

/* ------------------------------------------------------------------- prng */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t
rng_next(void)
{
    /* xorshift64* — deterministic across platforms, seedable, no libc rand */
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static uint32_t
rng_below(uint32_t n)
{
    return n <= 1 ? 0 : (uint32_t)(rng_next() % n);
}

/* ---------------------------------------------------------- vec + quadric */

struct V3
{
    double x, y, z;
};

static struct V3
v3_sub(struct V3 a, struct V3 b)
{
    struct V3 r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

static struct V3
v3_cross(struct V3 a, struct V3 b)
{
    struct V3 r = { a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x };
    return r;
}

static double
v3_dot(struct V3 a, struct V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double
v3_len2(struct V3 a)
{
    return v3_dot(a, a);
}

/* Symmetric 4x4 quadric, 10 uniques: [aa ab ac ad bb bc bd cc cd dd]. */

static void
quad_add_plane(double* q, double a, double b, double c, double d, double w)
{
    q[0] += w * a * a;
    q[1] += w * a * b;
    q[2] += w * a * c;
    q[3] += w * a * d;
    q[4] += w * b * b;
    q[5] += w * b * c;
    q[6] += w * b * d;
    q[7] += w * c * c;
    q[8] += w * c * d;
    q[9] += w * d * d;
}

static double
quad_eval(const double* q, struct V3 p)
{
    return q[0] * p.x * p.x + q[4] * p.y * p.y + q[7] * p.z * p.z
         + 2.0 * (q[1] * p.x * p.y + q[2] * p.x * p.z + q[5] * p.y * p.z)
         + 2.0 * (q[3] * p.x + q[6] * p.y + q[8] * p.z)
         + q[9];
}

/* -------------------------------------------------------- working state */

static struct RSCache_Model* M;

static uint8_t* alive;      /* per vertex */
static int* survivor;       /* per vertex; -1 while alive, else merge target */
static uint8_t* face_live;  /* per face */
static uint8_t* pinned;     /* per vertex */
static double* quadrics;    /* vertex_count * 10 */
static int n_label[257];    /* live vertices per bone label; 256 = "no map" */
static int live_verts, live_faces;

/* CSR vertex->face adjacency, rebuilt every step */
static int* adj_off;  /* vertex_count + 1 */
static int* adj_face; /* face_count * 3 */

/* per-vertex attribute-boundary flags, rebuilt every step */
static uint8_t* v_multi_color;
static uint8_t* v_multi_prio;
static uint8_t* v_has_alpha;
static uint8_t* v_has_tex;

static double w_label = 4.0, w_color = 30.0, w_prio = 30.0, w_alpha = 100.0,
              w_tex = 60.0, w_boundary = 2.0;
static double max_cost = HUGE_VAL;
static int jitter = 1;
static int keep_textured = 1;
static int quiet = 0;

static struct V3
vert_pos(int v)
{
    struct V3 r = { (double)M->vertices_x[v], (double)M->vertices_y[v],
                    (double)M->vertices_z[v] };
    return r;
}

static int
label_of(int v)
{
    return M->vertex_bone_map ? M->vertex_bone_map[v] : 256;
}

static int
resolve(int v)
{
    while (survivor[v] >= 0)
        v = survivor[v];
    return v;
}

static int
face_is_textured(int f)
{
    if (M->face_textures)
        return M->face_textures[f] >= 0;
    /* OB2-era: info byte low bits 2/3 mean textured */
    return M->face_infos && (M->face_infos[f] & 3) >= 2;
}

/* --------------------------------------------------- quadric initialization */

struct EdgeEnt
{
    uint64_t key; /* (min<<32)|max, UINT64_MAX = empty */
    int count;
    int face; /* one incident face, for the boundary plane */
};

static int
face_plane(int f, double* a, double* b, double* c, double* d, double* area)
{
    struct V3 pa = vert_pos(M->face_indices_a[f]);
    struct V3 pb = vert_pos(M->face_indices_b[f]);
    struct V3 pc = vert_pos(M->face_indices_c[f]);
    struct V3 n = v3_cross(v3_sub(pb, pa), v3_sub(pc, pa));
    double len2 = v3_len2(n);
    if (len2 < AREA_EPS)
        return 0;
    double len = sqrt(len2);
    *a = n.x / len;
    *b = n.y / len;
    *c = n.z / len;
    *d = -(*a * pa.x + *b * pa.y + *c * pa.z);
    *area = len * 0.5;
    return 1;
}

static void
init_quadrics(void)
{
    int V = M->vertex_count, F = M->face_count;
    quadrics = calloc((size_t)V * 10, sizeof *quadrics);
    if (!quadrics)
        die("out of memory (quadrics)");

    /* face planes, area-weighted */
    for (int f = 0; f < F; f++)
    {
        double a, b, c, d, area;
        if (!face_plane(f, &a, &b, &c, &d, &area))
            continue;
        int idx[3] = { M->face_indices_a[f], M->face_indices_b[f],
                       M->face_indices_c[f] };
        for (int k = 0; k < 3; k++)
            quad_add_plane(quadrics + (size_t)idx[k] * 10, a, b, c, d, area);
    }

    /* boundary edges (exactly one incident face) get a constraint plane
     * perpendicular to that face through the edge, so open borders (cape
     * hems, mouth bags, part seams) resist being eaten */
    uint64_t cap = 64;
    while (cap < (uint64_t)F * 6u)
        cap <<= 1;
    struct EdgeEnt* tab = malloc(cap * sizeof *tab);
    if (!tab)
        die("out of memory (edge table)");
    for (uint64_t i = 0; i < cap; i++)
        tab[i].key = UINT64_MAX;
    for (int f = 0; f < F; f++)
    {
        int idx[3] = { M->face_indices_a[f], M->face_indices_b[f],
                       M->face_indices_c[f] };
        for (int k = 0; k < 3; k++)
        {
            int i = idx[k], j = idx[(k + 1) % 3];
            if (i == j)
                continue;
            uint64_t lo = (uint64_t)(i < j ? i : j);
            uint64_t hi = (uint64_t)(i < j ? j : i);
            uint64_t key = (lo << 32) | hi;
            uint64_t s = (key * 0x9E3779B97F4A7C15ull) & (cap - 1);
            while (tab[s].key != UINT64_MAX && tab[s].key != key)
                s = (s + 1) & (cap - 1);
            if (tab[s].key == UINT64_MAX)
            {
                tab[s].key = key;
                tab[s].count = 1;
                tab[s].face = f;
            }
            else
                tab[s].count++;
        }
    }
    int boundary_edges = 0;
    for (uint64_t s = 0; s < cap; s++)
    {
        if (tab[s].key == UINT64_MAX || tab[s].count != 1)
            continue;
        boundary_edges++;
        int i = (int)(tab[s].key >> 32), j = (int)(tab[s].key & 0xFFFFFFFFu);
        double a, b, c, d, area;
        if (!face_plane(tab[s].face, &a, &b, &c, &d, &area))
            continue;
        struct V3 pi = vert_pos(i), pj = vert_pos(j);
        struct V3 e = v3_sub(pj, pi);
        struct V3 fn = { a, b, c };
        struct V3 nb = v3_cross(e, fn);
        double nlen2 = v3_len2(nb);
        if (nlen2 < AREA_EPS)
            continue;
        double nlen = sqrt(nlen2);
        double ba = nb.x / nlen, bb = nb.y / nlen, bc = nb.z / nlen;
        double bd = -(ba * pi.x + bb * pi.y + bc * pi.z);
        double w = w_boundary * v3_len2(e);
        quad_add_plane(quadrics + (size_t)i * 10, ba, bb, bc, bd, w);
        quad_add_plane(quadrics + (size_t)j * 10, ba, bb, bc, bd, w);
    }
    free(tab);
    if (!quiet)
        printf("  boundary edges: %d\n", boundary_edges);
}

/* -------------------------------------------------- per-step rebuild passes */

static void
build_adjacency(void)
{
    int V = M->vertex_count, F = M->face_count;
    memset(adj_off, 0, (size_t)(V + 1) * sizeof *adj_off);
    for (int f = 0; f < F; f++)
    {
        if (!face_live[f])
            continue;
        adj_off[M->face_indices_a[f] + 1]++;
        adj_off[M->face_indices_b[f] + 1]++;
        adj_off[M->face_indices_c[f] + 1]++;
    }
    for (int v = 0; v < V; v++)
        adj_off[v + 1] += adj_off[v];
    int* cursor = malloc((size_t)V * sizeof *cursor);
    if (!cursor)
        die("out of memory (adjacency cursor)");
    memcpy(cursor, adj_off, (size_t)V * sizeof *cursor);
    for (int f = 0; f < F; f++)
    {
        if (!face_live[f])
            continue;
        adj_face[cursor[M->face_indices_a[f]]++] = f;
        adj_face[cursor[M->face_indices_b[f]]++] = f;
        adj_face[cursor[M->face_indices_c[f]]++] = f;
    }
    free(cursor);
}

static void
build_vertex_attr(void)
{
    int V = M->vertex_count, F = M->face_count;
    memset(v_multi_color, 0, (size_t)V);
    memset(v_multi_prio, 0, (size_t)V);
    memset(v_has_alpha, 0, (size_t)V);
    memset(v_has_tex, 0, (size_t)V);
    static int* first_color; /* -1 = unseen */
    static int* first_prio;
    if (!first_color)
    {
        first_color = malloc((size_t)V * sizeof *first_color);
        first_prio = malloc((size_t)V * sizeof *first_prio);
        assert(first_color);
        assert(first_prio);
    }
    for (int v = 0; v < V; v++)
        first_color[v] = first_prio[v] = -1;
    for (int f = 0; f < F; f++)
    {
        if (!face_live[f])
            continue;
        int color = M->face_colors ? M->face_colors[f] : 0;
        int prio = M->face_priorities ? M->face_priorities[f] : -2;
        int has_alpha = M->face_alphas && M->face_alphas[f] != 0;
        int has_tex = face_is_textured(f);
        int idx[3] = { M->face_indices_a[f], M->face_indices_b[f],
                       M->face_indices_c[f] };
        for (int k = 0; k < 3; k++)
        {
            int v = idx[k];
            if (first_color[v] < 0)
                first_color[v] = color;
            else if (first_color[v] != color)
                v_multi_color[v] = 1;
            if (prio != -2)
            {
                if (first_prio[v] < 0)
                    first_prio[v] = prio;
                else if (first_prio[v] != prio)
                    v_multi_prio[v] = 1;
            }
            if (has_alpha)
                v_has_alpha[v] = 1;
            if (has_tex)
                v_has_tex[v] = 1;
        }
    }
}

/* ------------------------------------------------------- candidate scan */

struct Cand
{
    double cost;
    int v, u; /* v collapses into u */
};

static struct Cand pool[POOL_MAX];
static int pool_n;

static void
pool_insert(double cost, int v, int u)
{
    if (pool_n == POOL_MAX && cost >= pool[POOL_MAX - 1].cost)
        return;
    for (int i = 0; i < pool_n; i++)
        if (pool[i].v == v && pool[i].u == u)
            return; /* duplicate directed edge from an adjacent face */
    int at = pool_n < POOL_MAX ? pool_n : POOL_MAX - 1;
    while (at > 0 && pool[at - 1].cost > cost)
    {
        pool[at] = pool[at - 1];
        at--;
    }
    pool[at].cost = cost;
    pool[at].v = v;
    pool[at].u = u;
    if (pool_n < POOL_MAX)
        pool_n++;
}

static void
try_candidate(int v, int u)
{
    if (v == u || pinned[v])
        return;
    if (label_of(v) != label_of(u))
        return;
    double q[10];
    const double* qv = quadrics + (size_t)v * 10;
    const double* qu = quadrics + (size_t)u * 10;
    for (int i = 0; i < 10; i++)
        q[i] = qv[i] + qu[i];
    struct V3 pu = vert_pos(u);
    double cost = quad_eval(q, pu);
    if (M->vertex_bone_map)
    {
        int nl = n_label[label_of(v)];
        double d2 = v3_len2(v3_sub(vert_pos(v), pu));
        cost += w_label * d2 / ((double)nl * (double)nl);
    }
    cost += w_color * v_multi_color[v] + w_prio * v_multi_prio[v]
          + w_alpha * v_has_alpha[v] + w_tex * v_has_tex[v];
    if (cost < 0.0)
        cost = 0.0;
    pool_insert(cost, v, u);
}

static void
scan_candidates(void)
{
    pool_n = 0;
    for (int f = 0; f < M->face_count; f++)
    {
        if (!face_live[f])
            continue;
        int a = M->face_indices_a[f], b = M->face_indices_b[f],
            c = M->face_indices_c[f];
        try_candidate(a, b);
        try_candidate(b, a);
        try_candidate(b, c);
        try_candidate(c, b);
        try_candidate(c, a);
        try_candidate(a, c);
    }
}

/* Would collapsing v->u flip or crush any surviving face around v? */
static int
flip_guard_ok(int v, int u)
{
    struct V3 pu = vert_pos(u);
    for (int s = adj_off[v]; s < adj_off[v + 1]; s++)
    {
        int f = adj_face[s];
        if (!face_live[f])
            continue;
        int a = M->face_indices_a[f], b = M->face_indices_b[f],
            c = M->face_indices_c[f];
        if (a == u || b == u || c == u)
            continue; /* dies with the collapse */
        struct V3 pa = vert_pos(a), pb = vert_pos(b), pc = vert_pos(c);
        struct V3 before = v3_cross(v3_sub(pb, pa), v3_sub(pc, pa));
        struct V3 qa = a == v ? pu : pa;
        struct V3 qb = b == v ? pu : pb;
        struct V3 qc = c == v ? pu : pc;
        struct V3 after = v3_cross(v3_sub(qb, qa), v3_sub(qc, qa));
        if (v3_len2(after) < AREA_EPS)
            return 0; /* crushed to a sliver */
        if (v3_dot(before, after) <= 0.0)
            return 0; /* normal flipped */
    }
    return 1;
}

static void
apply_collapse(int v, int u)
{
    for (int s = adj_off[v]; s < adj_off[v + 1]; s++)
    {
        int f = adj_face[s];
        if (!face_live[f])
            continue;
        int* fa = &M->face_indices_a[f];
        int* fb = &M->face_indices_b[f];
        int* fc = &M->face_indices_c[f];
        if (*fa == u || *fb == u || *fc == u)
        {
            face_live[f] = 0; /* shared edge face collapses away */
            live_faces--;
            continue;
        }
        if (*fa == v)
            *fa = u;
        if (*fb == v)
            *fb = u;
        if (*fc == v)
            *fc = u;
        if (*fa == *fb || *fb == *fc || *fa == *fc)
        {
            face_live[f] = 0; /* degenerate (double-vertex input face) */
            live_faces--;
        }
    }
    double* qu = quadrics + (size_t)u * 10;
    const double* qv = quadrics + (size_t)v * 10;
    for (int i = 0; i < 10; i++)
        qu[i] += qv[i];
    alive[v] = 0;
    survivor[v] = u;
    n_label[label_of(v)]--;
    live_verts--;
}

/* ------------------------------------------------------------ compaction */

static int uv_degenerate; /* textured triangles crushed by --no-keep-textured */

static void
compact(int* new_vid, int* new_fid)
{
    int V = M->vertex_count, F = M->face_count;
    int nv = 0;
    for (int v = 0; v < V; v++)
        new_vid[v] = alive[v] ? nv++ : -1;

    for (int v = 0; v < V; v++)
    {
        if (!alive[v])
            continue;
        int d = new_vid[v];
        M->vertices_x[d] = M->vertices_x[v];
        M->vertices_y[d] = M->vertices_y[v];
        M->vertices_z[d] = M->vertices_z[v];
        if (M->vertex_bone_map)
            M->vertex_bone_map[d] = M->vertex_bone_map[v];
    }
    if (M->animaya_group_counts)
    {
        /* jagged per-vertex skin rows: free dead rows, pack live ones */
        for (int v = 0; v < V; v++)
            if (!alive[v])
            {
                free(M->animaya_groups[v]);
                free(M->animaya_scales[v]);
            }
        for (int v = 0; v < V; v++)
        {
            if (!alive[v])
                continue;
            int d = new_vid[v];
            M->animaya_group_counts[d] = M->animaya_group_counts[v];
            M->animaya_groups[d] = M->animaya_groups[v];
            M->animaya_scales[d] = M->animaya_scales[v];
        }
        M->animaya_vertex_count = nv;
    }

    int nf = 0;
    for (int f = 0; f < F; f++)
    {
        new_fid[f] = -1;
        if (!face_live[f])
            continue;
        int a = new_vid[resolve(M->face_indices_a[f])];
        int b = new_vid[resolve(M->face_indices_b[f])];
        int c = new_vid[resolve(M->face_indices_c[f])];
        if (a < 0 || b < 0 || c < 0 || a == b || b == c || a == c)
            continue; /* belt + braces; collapse should have killed these */
        int d = nf++;
        new_fid[f] = d;
        M->face_indices_a[d] = a;
        M->face_indices_b[d] = b;
        M->face_indices_c[d] = c;
        if (M->face_colors)
            M->face_colors[d] = M->face_colors[f];
        if (M->face_infos)
            M->face_infos[d] = M->face_infos[f];
        if (M->face_priorities)
            M->face_priorities[d] = M->face_priorities[f];
        if (M->face_alphas)
            M->face_alphas[d] = M->face_alphas[f];
        if (M->face_bone_map)
            M->face_bone_map[d] = M->face_bone_map[f];
        if (M->face_textures)
            M->face_textures[d] = M->face_textures[f];
        if (M->face_texture_coords)
            M->face_texture_coords[d] = M->face_texture_coords[f];
        if (M->face_z_offsets)
            M->face_z_offsets[d] = M->face_z_offsets[f];
    }

    for (int t = 0; t < M->textured_face_count; t++)
    {
        int p = new_vid[resolve(M->textured_p_coordinate[t])];
        int m = new_vid[resolve(M->textured_m_coordinate[t])];
        int n = new_vid[resolve(M->textured_n_coordinate[t])];
        if (p < 0 || m < 0 || n < 0)
            die("texture triangle %d references a vanished vertex", t);
        if (p == m || m == n || p == n)
            uv_degenerate++;
        M->textured_p_coordinate[t] = (uint16_t)p;
        M->textured_m_coordinate[t] = (uint16_t)m;
        M->textured_n_coordinate[t] = (uint16_t)n;
    }

    M->vertex_count = nv;
    M->face_count = nf;
}

/* ------------------------------------------------------------- verification */

static int verify_fail;

static void
mismatch(const char* name, int index, long long ours, long long theirs)
{
    printf("  VERIFY FAIL %s[%d]: wrote %lld, decoded %lld\n", name, index,
           ours, theirs);
    verify_fail = 1;
}

#define CMP_ARRAY(name, A, B, n, T)                                           \
    do                                                                        \
    {                                                                         \
        const T* pa = (A);                                                    \
        const T* pb = (B);                                                    \
        if (pa && pb)                                                         \
        {                                                                     \
            for (int i_ = 0; i_ < (n); i_++)                                  \
                if (pa[i_] != pb[i_])                                         \
                {                                                             \
                    mismatch(name, i_, (long long)pa[i_], (long long)pb[i_]); \
                    break;                                                    \
                }                                                             \
        }                                                                     \
        else if (pa != pb)                                                    \
        {                                                                     \
            const T* present = pa ? pa : pb;                                  \
            int nonzero = 0;                                                  \
            for (int i_ = 0; i_ < (n); i_++)                                  \
                if (present[i_] != 0)                                         \
                {                                                             \
                    nonzero = 1;                                              \
                    break;                                                    \
                }                                                             \
            if (nonzero)                                                      \
            {                                                                 \
                printf("  VERIFY FAIL %s: present only on %s side, "          \
                       "with non-zero data\n",                                \
                       name, pa ? "our" : "decoded");                         \
                verify_fail = 1;                                              \
            }                                                                 \
            else                                                              \
                printf("  verify note: %s present only on %s side "           \
                       "(all zero — decoder synthesis)\n",                    \
                       name, pa ? "our" : "decoded");                         \
        }                                                                     \
    } while (0)

static int
verify_roundtrip(uint8_t* bytes, uint32_t size)
{
    struct RSCache_Model* back = RSCache_ModelNewDecode(bytes, (int)size);
    if (!back)
    {
        printf("  VERIFY FAIL: output does not decode\n");
        return 0;
    }
    verify_fail = 0;
    if (back->vertex_count != M->vertex_count)
        mismatch("vertex_count", -1, M->vertex_count, back->vertex_count);
    if (back->face_count != M->face_count)
        mismatch("face_count", -1, M->face_count, back->face_count);
    if (back->textured_face_count != M->textured_face_count)
        mismatch("textured_face_count", -1, M->textured_face_count,
                 back->textured_face_count);
    int V = M->vertex_count, F = M->face_count, T = M->textured_face_count;
    if (!verify_fail)
    {
        CMP_ARRAY("vertices_x", M->vertices_x, back->vertices_x, V, int);
        CMP_ARRAY("vertices_y", M->vertices_y, back->vertices_y, V, int);
        CMP_ARRAY("vertices_z", M->vertices_z, back->vertices_z, V, int);
        CMP_ARRAY("vertex_bone_map", M->vertex_bone_map, back->vertex_bone_map,
                  V, uint8_t);
        CMP_ARRAY("face_indices_a", M->face_indices_a, back->face_indices_a, F,
                  int);
        CMP_ARRAY("face_indices_b", M->face_indices_b, back->face_indices_b, F,
                  int);
        CMP_ARRAY("face_indices_c", M->face_indices_c, back->face_indices_c, F,
                  int);
        CMP_ARRAY("face_colors", M->face_colors, back->face_colors, F,
                  uint16_t);
        CMP_ARRAY("face_infos", M->face_infos, back->face_infos, F, uint8_t);
        CMP_ARRAY("face_priorities", M->face_priorities, back->face_priorities,
                  F, uint8_t);
        CMP_ARRAY("face_alphas", M->face_alphas, back->face_alphas, F,
                  uint8_t);
        CMP_ARRAY("face_bone_map", M->face_bone_map, back->face_bone_map, F,
                  uint8_t);
        CMP_ARRAY("face_textures", M->face_textures, back->face_textures, F,
                  int16_t);
        CMP_ARRAY("face_texture_coords", M->face_texture_coords,
                  back->face_texture_coords, F, int16_t);
        CMP_ARRAY("face_z_offsets", M->face_z_offsets, back->face_z_offsets, F,
                  int8_t);
        CMP_ARRAY("texture_render_types", M->texture_render_types,
                  back->texture_render_types, T, uint8_t);
        CMP_ARRAY("textured_p", M->textured_p_coordinate,
                  back->textured_p_coordinate, T, uint16_t);
        CMP_ARRAY("textured_m", M->textured_m_coordinate,
                  back->textured_m_coordinate, T, uint16_t);
        CMP_ARRAY("textured_n", M->textured_n_coordinate,
                  back->textured_n_coordinate, T, uint16_t);
        if (!M->face_priorities && back->model_priority != M->model_priority)
            mismatch("model_priority", -1, M->model_priority,
                     back->model_priority);
        if (back->format_version != M->format_version)
            printf("  verify note: format_version %d -> %d\n",
                   M->format_version, back->format_version);
    }
    RSCache_ModelFree(back);
    return !verify_fail;
}

/* ------------------------------------------------------------ JSON output */

static void
json_escape(FILE* fp, const char* s)
{
    for (; *s; s++)
    {
        if (*s == '\\' || *s == '"')
            fputc('\\', fp);
        fputc(*s, fp);
    }
}

static void
json_int_array(FILE* fp, const char* name, const int* vals, int n,
               const char* trailing)
{
    fprintf(fp, "  \"%s\": [", name);
    for (int i = 0; i < n; i++)
    {
        if (i % 20 == 0)
            fprintf(fp, "\n    ");
        fprintf(fp, "%d%s", vals[i], i + 1 < n ? "," : "");
    }
    fprintf(fp, "\n  ]%s\n", trailing);
}

/* ------------------------------------------------------------------ main */

static const char*
format_name(int format)
{
    switch (format)
    {
    case RSCACHE_MODEL_FORMAT_OB2: return "OB2";
    case RSCACHE_MODEL_FORMAT_OB3: return "OB3";
    case RSCACHE_MODEL_FORMAT_V2: return "V2";
    case RSCACHE_MODEL_FORMAT_V3: return "V3";
    default: return "?";
    }
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: rs2012_model_decimate --in model.ob3 --out reduced.ob3\n"
        "         --map mapping.json [options]\n"
        "  --target-frac F    stop at F * original vertices (default 0.5)\n"
        "  --target-verts N   stop at N vertices (overrides --target-frac)\n"
        "  --max-cost C       stop when the cheapest legal collapse costs > C\n"
        "  --seed S           PRNG seed for --jitter (default 1)\n"
        "  --jitter K         pick uniformly among the K cheapest collapses\n"
        "  --no-keep-textured allow collapsing texture-triangle vertices\n"
        "  --w-label W        label-centroid drift weight (default 4)\n"
        "  --w-color W        color-boundary vertex penalty (default 30)\n"
        "  --w-prio W         priority-boundary vertex penalty (default 30)\n"
        "  --w-alpha W        alpha-face vertex penalty (default 100)\n"
        "  --w-tex W          textured-face vertex penalty (default 60)\n"
        "  --w-boundary W     open-edge constraint weight (default 2)\n"
        "  --quiet            summary only\n");
    exit(2);
}

int
main(int argc, char** argv)
{
    const char* in_path = NULL;
    const char* out_path = NULL;
    const char* map_path = NULL;
    double target_frac = 0.5;
    int target_verts = -1;
    uint64_t seed = 1;

    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        const char* val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--in") && val)
            in_path = argv[++i];
        else if (!strcmp(a, "--out") && val)
            out_path = argv[++i];
        else if (!strcmp(a, "--map") && val)
            map_path = argv[++i];
        else if (!strcmp(a, "--target-frac") && val)
            target_frac = atof(argv[++i]);
        else if (!strcmp(a, "--target-verts") && val)
            target_verts = atoi(argv[++i]);
        else if (!strcmp(a, "--max-cost") && val)
            max_cost = atof(argv[++i]);
        else if (!strcmp(a, "--seed") && val)
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--jitter") && val)
            jitter = atoi(argv[++i]);
        else if (!strcmp(a, "--no-keep-textured"))
            keep_textured = 0;
        else if (!strcmp(a, "--w-label") && val)
            w_label = atof(argv[++i]);
        else if (!strcmp(a, "--w-color") && val)
            w_color = atof(argv[++i]);
        else if (!strcmp(a, "--w-prio") && val)
            w_prio = atof(argv[++i]);
        else if (!strcmp(a, "--w-alpha") && val)
            w_alpha = atof(argv[++i]);
        else if (!strcmp(a, "--w-tex") && val)
            w_tex = atof(argv[++i]);
        else if (!strcmp(a, "--w-boundary") && val)
            w_boundary = atof(argv[++i]);
        else if (!strcmp(a, "--quiet"))
            quiet = 1;
        else
            usage();
    }
    if (!in_path || !out_path || !map_path)
        usage();
    if (jitter < 1)
        jitter = 1;
    rng_state = seed ? seed * 0x9E3779B97F4A7C15ull : 0x9E3779B97F4A7C15ull;

    int in_size = 0;
    uint8_t* in_bytes = read_file(in_path, &in_size);
    struct RSCache_ModelProvenance* prov = NULL;
    M = RSCache_ModelNewDecodeProvenance(in_bytes, in_size, &prov);
    if (!M || !prov)
        die("%s does not decode as a model", in_path);
    if (M->animaya_group_counts && M->animaya_vertex_count != M->vertex_count)
        die("animaya vertex count %d != vertex count %d — refusing to guess",
            M->animaya_vertex_count, M->vertex_count);

    /* Version-13+ models store vertices at 4x; the version byte that says so
     * lives in the header flags, which a provenance-less encode cannot carry
     * (counts changed, provenance void). Writing 4x coords into a version-1
     * file would render 4x too large — torirs_model_from_rscache.c is the
     * scaleDown site and keys on the version. So apply the reference's
     * scaleDown(2) here and emit an honest version-1 model in final units;
     * the renderer sees bit-identical geometry either way. */
    int coords_rescaled = 0;
    if (M->format_version >= 13)
    {
        for (int v = 0; v < M->vertex_count; v++)
        {
            M->vertices_x[v] >>= 2;
            M->vertices_y[v] >>= 2;
            M->vertices_z[v] >>= 2;
        }
        printf("  version %d stores vertices at 4x — applied scaleDown(2); "
               "output will be version-1 final units\n", M->format_version);
        M->format_version = 1;
        coords_rescaled = 1;
    }
    if (M->texture_render_types)
        for (int t = 0; t < M->textured_face_count; t++)
            if (M->texture_render_types[t] != 0)
            {
                printf("  WARNING: complex texture faces present — their "
                       "mapping block lives in the dropped tail\n");
                break;
            }

    int V = M->vertex_count, F = M->face_count;
    int orig_verts = V, orig_faces = F, orig_textured = M->textured_face_count;
    printf("decimate: %s\n  %d vertices, %d faces, %d texture triangles, "
           "format %s (v%d), tail %dB\n",
           in_path, V, F, M->textured_face_count, format_name(prov->format),
           M->format_version, prov->tail_size);

    if (target_verts < 0)
        target_verts = (int)(V * target_frac + 0.5);
    if (target_verts < 3)
        target_verts = 3;

    alive = malloc((size_t)V);
    survivor = malloc((size_t)V * sizeof *survivor);
    face_live = malloc((size_t)F);
    pinned = calloc((size_t)V, 1);
    adj_off = malloc(((size_t)V + 1) * sizeof *adj_off);
    adj_face = malloc((size_t)F * 3 * sizeof *adj_face);
    v_multi_color = malloc((size_t)V);
    v_multi_prio = malloc((size_t)V);
    v_has_alpha = malloc((size_t)V);
    v_has_tex = malloc((size_t)V);
    if (!alive || !survivor || !face_live || !pinned || !adj_off || !adj_face
        || !v_multi_color || !v_multi_prio || !v_has_alpha || !v_has_tex)
        die("out of memory");
    memset(alive, 1, (size_t)V);
    memset(face_live, 1, (size_t)F);
    for (int v = 0; v < V; v++)
        survivor[v] = -1;
    live_verts = V;
    live_faces = F;

    int pinned_count = 0;
    if (keep_textured && M->textured_face_count > 0
        && M->textured_p_coordinate)
        for (int t = 0; t < M->textured_face_count; t++)
        {
            int idx[3] = { M->textured_p_coordinate[t],
                           M->textured_m_coordinate[t],
                           M->textured_n_coordinate[t] };
            for (int k = 0; k < 3; k++)
                if (idx[k] < V && !pinned[idx[k]])
                {
                    pinned[idx[k]] = 1;
                    pinned_count++;
                }
        }

    memset(n_label, 0, sizeof n_label);
    for (int v = 0; v < V; v++)
        n_label[label_of(v)]++;
    int n_label_before[257];
    memcpy(n_label_before, n_label, sizeof n_label);

    /* Per-label extents of the ORIGINAL mesh, in output analysis units.
     * Downstream drivers use these to frame close-up region renders (a
     * whole-body camera on a huge model leaves small detail regions — faces,
     * hands — too few pixels for any judge to defend). */
    static int lab_min[257][3], lab_max[257][3];
    static long long lab_sum[257][3];
    for (int l = 0; l < 257; l++)
        for (int a = 0; a < 3; a++)
        {
            lab_min[l][a] = INT_MAX;
            lab_max[l][a] = INT_MIN;
            lab_sum[l][a] = 0;
        }
    for (int v = 0; v < V; v++)
    {
        int const l = label_of(v);
        int const c[3] = { M->vertices_x[v], M->vertices_y[v],
                           M->vertices_z[v] };
        for (int a = 0; a < 3; a++)
        {
            if (c[a] < lab_min[l][a])
                lab_min[l][a] = c[a];
            if (c[a] > lab_max[l][a])
                lab_max[l][a] = c[a];
            lab_sum[l][a] += c[a];
        }
    }

    init_quadrics();

    int steps = 0;
    double cost_sum = 0.0, cost_last = 0.0;
    const char* stop_reason = NULL;
    while (live_verts > target_verts)
    {
        build_adjacency();
        build_vertex_attr();
        scan_candidates();
        if (max_cost < HUGE_VAL)
            while (pool_n && pool[pool_n - 1].cost > max_cost)
                pool_n--;
        if (!pool_n)
        {
            stop_reason = max_cost < HUGE_VAL
                              ? "no legal collapse under --max-cost"
                              : "no legal collapse (labels/pins exhausted)";
            break;
        }
        int chosen = -1;
        while (pool_n)
        {
            int span = jitter < pool_n ? jitter : pool_n;
            int r = (int)rng_below((uint32_t)span);
            if (flip_guard_ok(pool[r].v, pool[r].u))
            {
                chosen = r;
                break;
            }
            memmove(&pool[r], &pool[r + 1],
                    (size_t)(pool_n - r - 1) * sizeof pool[0]);
            pool_n--;
        }
        if (chosen < 0)
        {
            stop_reason = "guard-locked (every cheap collapse flips a face)";
            break;
        }
        cost_last = pool[chosen].cost;
        cost_sum += cost_last;
        apply_collapse(pool[chosen].v, pool[chosen].u);
        steps++;
        if (!quiet && steps % 500 == 0)
            printf("  %d collapses, %d vertices live, last cost %.3f\n", steps,
                   live_verts, cost_last);
    }
    if (!stop_reason)
        stop_reason = "target reached";

    /* loose vertices (no live face) are preserved by construction —
     * count them for the report */
    int loose = 0;
    {
        uint8_t* seen = calloc((size_t)V, 1);
        for (int f = 0; f < F; f++)
            if (face_live[f])
            {
                seen[M->face_indices_a[f]] = 1;
                seen[M->face_indices_b[f]] = 1;
                seen[M->face_indices_c[f]] = 1;
            }
        for (int v = 0; v < V; v++)
            if (alive[v] && !seen[v])
                loose++;
        free(seen);
    }

    int* new_vid = malloc((size_t)V * sizeof *new_vid);
    int* new_fid = malloc((size_t)F * sizeof *new_fid);
    assert(new_vid);
    assert(new_fid);
    compact(new_vid, new_fid);

    uint32_t bound = RSCache_ModelEncodeBound(M, NULL);
    uint8_t* out_bytes = malloc(bound);
    if (!out_bytes)
        die("out of memory (encode buffer)");
    uint32_t written = RSCache_ModelEncodeFormat(M, NULL, prov->format,
                                                 out_bytes, bound);
    if (!written)
        die("encode failed (format %s, %d verts, %d faces)",
            format_name(prov->format), M->vertex_count, M->face_count);
    write_file(out_path, out_bytes, written);

    printf("  %d -> %d vertices (%d collapses, %d loose kept), "
           "%d -> %d faces\n  stop: %s; mean collapse cost %.3f, last %.3f\n",
           orig_verts, M->vertex_count, steps, loose, orig_faces,
           M->face_count, stop_reason, steps ? cost_sum / steps : 0.0,
           cost_last);
    if (pinned_count)
        printf("  pinned texture vertices: %d\n", pinned_count);
    if (uv_degenerate)
        printf("  WARNING: %d texture triangles collapsed to a line/point\n",
               uv_degenerate);
    if (M->vertex_bone_map && !quiet)
    {
        printf("  labels:");
        for (int l = 0; l < 256; l++)
            if (n_label_before[l])
                printf(" %d:%d->%d", l, n_label_before[l], n_label[l]);
        printf("\n");
    }

    int ok = verify_roundtrip(out_bytes, written);
    printf("  decode-back verify: %s (%u bytes, was %d, tail %dB dropped)\n",
           ok ? "OK" : "FAILED", written, in_size, prov->tail_size);

    /* mapping JSON: the port of the old per-vertex world onto the new one */
    {
        FILE* fp = fopen(map_path, "w");
        if (!fp)
            die("cannot create %s", map_path);
        fprintf(fp, "{\n  \"input\": \"");
        json_escape(fp, in_path);
        fprintf(fp, "\",\n  \"output\": \"");
        json_escape(fp, out_path);
        fprintf(fp, "\",\n");
        fprintf(fp, "  \"format\": \"%s\",\n  \"format_version\": %d,\n",
                format_name(prov->format), M->format_version);
        fprintf(fp, "  \"coords_rescaled\": %s,\n",
                coords_rescaled ? "true" : "false");
        fprintf(fp, "  \"tail_bytes_dropped\": %d,\n", prov->tail_size);
        fprintf(fp,
                "  \"original\": {\"vertices\": %d, \"faces\": %d, "
                "\"textured_faces\": %d},\n",
                orig_verts, orig_faces, orig_textured);
        fprintf(fp,
                "  \"reduced\": {\"vertices\": %d, \"faces\": %d, "
                "\"loose_vertices\": %d},\n",
                M->vertex_count, M->face_count, loose);
        fprintf(fp,
                "  \"search\": {\"seed\": %llu, \"jitter\": %d, "
                "\"target_verts\": %d, \"steps\": %d, \"stop\": \"%s\",\n"
                "    \"weights\": {\"label\": %g, \"color\": %g, \"prio\": %g,"
                " \"alpha\": %g, \"tex\": %g, \"boundary\": %g},\n"
                "    \"pinned_vertices\": %d, \"uv_degenerate\": %d},\n",
                (unsigned long long)seed, jitter, target_verts, steps,
                stop_reason, w_label, w_color, w_prio, w_alpha, w_tex,
                w_boundary, pinned_count, uv_degenerate);
        if (M->vertex_bone_map)
        {
            fprintf(fp, "  \"labels\": {");
            int first = 1;
            for (int l = 0; l < 256; l++)
                if (n_label_before[l])
                {
                    int const n = n_label_before[l];
                    fprintf(fp,
                            "%s\"%d\": {\"before\": %d, \"after\": %d, "
                            "\"centroid\": [%lld,%lld,%lld], "
                            "\"bbox\": [[%d,%d],[%d,%d],[%d,%d]]}",
                            first ? "" : ", ", l, n, n_label[l],
                            lab_sum[l][0] / n, lab_sum[l][1] / n,
                            lab_sum[l][2] / n,
                            lab_min[l][0], lab_max[l][0],
                            lab_min[l][1], lab_max[l][1],
                            lab_min[l][2], lab_max[l][2]);
                    first = 0;
                }
            fprintf(fp, "},\n");
        }
        int* tmp = malloc((size_t)(V > F ? V : F) * sizeof *tmp);
        if (!tmp)
            die("out of memory (json scratch)");
        for (int v = 0; v < V; v++)
            tmp[v] = new_vid[resolve(v)];
        json_int_array(fp, "vertex_map", tmp, V, ",");
        for (int v = 0; v < V; v++)
            tmp[v] = alive[v] ? -1 : resolve(v);
        json_int_array(fp, "vertex_merged_into", tmp, V, ",");
        json_int_array(fp, "face_map", new_fid, F, "");
        fprintf(fp, "}\n");
        fclose(fp);
        free(tmp);
    }
    printf("  wrote %s and %s\n", out_path, map_path);

    RSCache_ModelProvenanceFree(prov);
    RSCache_ModelFree(M);
    free(in_bytes);
    free(out_bytes);
    free(new_vid);
    free(new_fid);
    return ok ? 0 : 1;
}
