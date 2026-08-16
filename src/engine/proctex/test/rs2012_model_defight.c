/*
 * rs2012_model_defight.c — multi-view z-fighting repair for OB3 model sets.
 *
 * The pre-DECIMATION pass of the osrsify pipeline (tools/osrs_stylizer).
 * Content authored for the painter's algorithm layers decals as coplanar
 * faces separated only by draw priority; the z-buffer path drops priorities
 * by design (see TORIDRAW_MODEL_FLAG_ZBUFFER in toridraw_render.u.c), so
 * those layers tie in depth and speckle. This tool makes the layering
 * geometric so it survives any renderer, and any later decimation:
 *
 *   - the merged part set is rasterized from MANY views (a yaw x pitch grid)
 *     with a perspective-correct top-two depth raster and the renderer's own
 *     backface-cull convention; a pixel whose two nearest faces tie within
 *     --fight-eps, where the faces share no geometry (no position-identical
 *     vertex — that filter keeps mesh seams and double-sided walls out), is
 *     a z-fight observation charged to that face pair;
 *   - pairs with enough observations across the views are ordered: the face
 *     with the higher draw priority was authored to draw later, i.e. on top
 *     (equal or missing priorities fall back to smaller-area-on-top — decals
 *     are smaller than the surface they decorate). Stacked decals get layer
 *     levels by longest path in the pair graph;
 *   - each top layer is displaced along its outward normal (sign resolved by
 *     the views that actually saw the fight — the visible side faces those
 *     cameras) by level * delta. Vertices shared with faces that stay put
 *     are DUPLICATED, keeping bone label and animaya skin, so the rig and
 *     every old animation still drive the moved layer; texture p/m/n
 *     projection vertices are never moved (duplication leaves them behind);
 *   - the delta ladder escalates and every step re-renders the full view
 *     grid to verify the fights actually died; the tool stops at the first
 *     delta that clears them and reports what remains if none does.
 *
 * Outputs are re-encoded per part (same format as the input, version-13+
 * coordinates rescaled to final units exactly like rs2012_model_decimate)
 * plus a JSON report. Vertex COUNT can grow slightly; positions move by a
 * few units on the model's own scale — both invisible at judged zooms, and
 * the whole point is that they are decisive for a depth test.
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

#define MAX_PARTS 16
#define MAX_PITCHES 8
#define MAX_DELTAS 8
#define MAX_LEVEL 4
#define PAIR_REPORT_CAP 200
#define PI 3.14159265358979323846

static int quiet = 0;

static void
die(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rs2012_model_defight: ");
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

/* ------------------------------------------------------------------ parts */

struct Part
{
    const char* in_path;
    const char* out_path;
    struct RSCache_Model* m;
    struct RSCache_ModelProvenance* prov;
    int orig_vertex_count;
    int coords_rescaled;
    int verts_added;
};

static struct Part parts[MAX_PARTS];
static int part_count;

/* ----------------------------------------------------- merged view arrays */

/* Merged = concatenated. The client's merge dedups position-identical
 * vertices; detection does not need that because face pairs are filtered by
 * position identity, not index identity. */

static int total_v, total_f;
static int* mvx;
static int* mvy;
static int* mvz;
static int* v_part;   /* merged vertex -> part */
static int* v_local;  /* merged vertex -> part-local index */
static int* f_part;
static int* f_local;
static int* f_a; /* merged vertex indices */
static int* f_b;
static int* f_c;
static int* f_prio;   /* -1 when the part carries none */
static uint8_t* f_alpha_flag;
static int* v_posid;  /* canonical position id, rebuilt per detection pass */
static double* px_scr; /* per merged vertex, screen x — sized with total_v */
static double* py_scr;
static double* pz_cam; /* camera-space depth */

static void
count_merged(void)
{
    total_v = total_f = 0;
    for (int p = 0; p < part_count; p++)
    {
        total_v += parts[p].m->vertex_count;
        total_f += parts[p].m->face_count;
    }
}

/* Faces and static attributes once; new vertices never appear after the
 * duplication pass, so callers re-run this only when counts changed. */
static void
build_merged_topology(void)
{
    count_merged();
    free(v_part); free(v_local);
    free(f_part); free(f_local);
    free(f_a); free(f_b); free(f_c);
    free(f_prio); free(f_alpha_flag);
    free(mvx); free(mvy); free(mvz); free(v_posid);
    free(px_scr); free(py_scr); free(pz_cam);
    /* the projection buffers track total_v too: vertex duplication grows it
     * between the detection pass and the verify passes */
    px_scr = malloc((size_t)total_v * sizeof *px_scr);
    py_scr = malloc((size_t)total_v * sizeof *py_scr);
    pz_cam = malloc((size_t)total_v * sizeof *pz_cam);
    assert(px_scr);
    assert(py_scr);
    assert(pz_cam);
    v_part = malloc((size_t)total_v * sizeof *v_part);
    v_local = malloc((size_t)total_v * sizeof *v_local);
    f_part = malloc((size_t)total_f * sizeof *f_part);
    f_local = malloc((size_t)total_f * sizeof *f_local);
    f_a = malloc((size_t)total_f * sizeof *f_a);
    f_b = malloc((size_t)total_f * sizeof *f_b);
    f_c = malloc((size_t)total_f * sizeof *f_c);
    f_prio = malloc((size_t)total_f * sizeof *f_prio);
    f_alpha_flag = malloc((size_t)total_f);
    mvx = malloc((size_t)total_v * sizeof *mvx);
    mvy = malloc((size_t)total_v * sizeof *mvy);
    mvz = malloc((size_t)total_v * sizeof *mvz);
    v_posid = malloc((size_t)total_v * sizeof *v_posid);
    if (!v_part || !v_local || !f_part || !f_local || !f_a || !f_b || !f_c
        || !f_prio || !f_alpha_flag || !mvx || !mvy || !mvz || !v_posid)
        die("out of memory (merged arrays)");

    int vb = 0, fb = 0;
    for (int p = 0; p < part_count; p++)
    {
        struct RSCache_Model* m = parts[p].m;
        for (int v = 0; v < m->vertex_count; v++)
        {
            v_part[vb + v] = p;
            v_local[vb + v] = v;
        }
        for (int f = 0; f < m->face_count; f++)
        {
            int g = fb + f;
            f_part[g] = p;
            f_local[g] = f;
            f_a[g] = vb + m->face_indices_a[f];
            f_b[g] = vb + m->face_indices_b[f];
            f_c[g] = vb + m->face_indices_c[f];
            f_prio[g] = m->face_priorities ? (int)m->face_priorities[f]
                                           : (int)m->model_priority;
            f_alpha_flag[g] = m->face_alphas && m->face_alphas[f] != 0;
        }
        vb += m->vertex_count;
        fb += m->face_count;
    }
}

static void
refresh_merged_positions(void)
{
    int vb = 0;
    for (int p = 0; p < part_count; p++)
    {
        struct RSCache_Model* m = parts[p].m;
        for (int v = 0; v < m->vertex_count; v++)
        {
            mvx[vb + v] = m->vertices_x[v];
            mvy[vb + v] = m->vertices_y[v];
            mvz[vb + v] = m->vertices_z[v];
        }
        vb += m->vertex_count;
    }
}

/* Canonical position ids off the CURRENT coordinates: seams between parts
 * (and double-sided geometry) share a position; a decal that has been pushed
 * off its surface no longer does, which is exactly right — it also no longer
 * ties in depth. */
static void
build_position_ids(void)
{
    uint64_t cap = 64;
    while (cap < (uint64_t)total_v * 2u)
        cap <<= 1;
    uint64_t* keys = malloc(cap * sizeof *keys);
    int* ids = malloc(cap * sizeof *ids);
    assert(keys);
    assert(ids);
    for (uint64_t i = 0; i < cap; i++)
        keys[i] = UINT64_MAX;
    int next_id = 0;
    for (int v = 0; v < total_v; v++)
    {
        /* 21 bits per axis, offset to positive: model coords are far below
         * +-1M. Collisions across genuinely distinct positions are
         * impossible in that range. */
        uint64_t key = ((uint64_t)(uint32_t)(mvx[v] + 0x100000) << 42)
                     | ((uint64_t)(uint32_t)(mvy[v] + 0x100000) << 21)
                     | (uint64_t)(uint32_t)(mvz[v] + 0x100000);
        uint64_t s = (key * 0x9E3779B97F4A7C15ull) & (cap - 1);
        while (keys[s] != UINT64_MAX && keys[s] != key)
            s = (s + 1) & (cap - 1);
        if (keys[s] == UINT64_MAX)
        {
            keys[s] = key;
            ids[s] = next_id++;
        }
        v_posid[v] = ids[s];
    }
    free(keys);
    free(ids);
}

static int
faces_share_position(int fa, int fb)
{
    int pa[3] = { v_posid[f_a[fa]], v_posid[f_b[fa]], v_posid[f_c[fa]] };
    int pb[3] = { v_posid[f_a[fb]], v_posid[f_b[fb]], v_posid[f_c[fb]] };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (pa[i] == pb[j])
                return 1;
    return 0;
}

/* ------------------------------------------------------------- pair table */

struct Pair
{
    uint64_t key; /* (lo<<32)|hi merged face ids; UINT64_MAX empty */
    long count;         /* fight pixels across all views (detection pass) */
    long count_verify;  /* fight pixels in the current verify pass */
    double fwd[3];      /* summed model-space camera forward over fights */
    int top;            /* merged face id authored on top; -1 = unresolved */
    int qualified;
    char rule; /* 'p' priority, 'a' area */
};

static struct Pair* pair_tab;
static uint64_t pair_cap;
static int pair_count;

static void
pair_init(void)
{
    pair_cap = 1u << 16;
    pair_tab = malloc(pair_cap * sizeof *pair_tab);
    if (!pair_tab)
        die("out of memory (pair table)");
    for (uint64_t i = 0; i < pair_cap; i++)
        pair_tab[i].key = UINT64_MAX;
    pair_count = 0;
}

static struct Pair*
pair_find(int fa, int fb, int create)
{
    uint64_t lo = (uint64_t)(fa < fb ? fa : fb);
    uint64_t hi = (uint64_t)(fa < fb ? fb : fa);
    uint64_t key = (lo << 32) | hi;
    uint64_t s = (key * 0x9E3779B97F4A7C15ull) & (pair_cap - 1);
    while (pair_tab[s].key != UINT64_MAX && pair_tab[s].key != key)
        s = (s + 1) & (pair_cap - 1);
    if (pair_tab[s].key == UINT64_MAX)
    {
        if (!create)
            return NULL;
        if ((uint64_t)pair_count * 2 >= pair_cap)
            die("pair table full (%d pairs) — raise --fight-eps sanity",
                pair_count);
        pair_tab[s].key = key;
        pair_tab[s].count = 0;
        pair_tab[s].count_verify = 0;
        pair_tab[s].fwd[0] = pair_tab[s].fwd[1] = pair_tab[s].fwd[2] = 0.0;
        pair_tab[s].top = -1;
        pair_tab[s].qualified = 0;
        pair_tab[s].rule = '?';
        pair_count++;
    }
    return &pair_tab[s];
}

/* ---------------------------------------------------------------- raster */

static int tile = 256;
static double fight_eps = 2.5;

struct ViewBuf
{
    double* z1;
    double* z2;
    int* f1;
    int* f2;
};

static struct ViewBuf vb_buf;

static void
raster_alloc(void)
{
    size_t n = (size_t)tile * (size_t)tile;
    vb_buf.z1 = malloc(n * sizeof(double));
    vb_buf.z2 = malloc(n * sizeof(double));
    vb_buf.f1 = malloc(n * sizeof(int));
    vb_buf.f2 = malloc(n * sizeof(int));
    assert(vb_buf.z1);
    assert(vb_buf.z2);
    assert(vb_buf.f1);
    assert(vb_buf.f2);
}

/* One view: rotate about y by yaw then about x by pitch (proper rotations,
 * same handedness as the engine: x right, y down the screen), camera on +z
 * looking at the bbox centre. fwd_out gets the camera forward in MODEL
 * space (third row of the rotation) — the sign oracle for displacement. */
static void
project_view(double yaw_rad, double pitch_rad, double cx, double cy, double cz,
             double dist, double focal, double* fwd_out)
{
    double sy = sin(yaw_rad), cyw = cos(yaw_rad);
    double sp = sin(pitch_rad), cp = cos(pitch_rad);
    /* row 3 of Rx(pitch)*Ry(yaw) */
    fwd_out[0] = -cp * sy * -1.0; /* see below; computed explicitly */
    /* Rotation: p1 = Ry(yaw) p; p2 = Rx(pitch) p1.
     *   x1 =  x*cos + z*sin
     *   z1 = -x*sin + z*cos
     *   y2 =  y*cos - z1*sin
     *   z2 =  y*sin + z1*cos
     * Camera z = z2 + dist. Forward (model dir that maps to +z_cam):
     *   row3 = (-sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch)) */
    fwd_out[0] = -sy * cp;
    fwd_out[1] = sp;
    fwd_out[2] = cyw * cp;

    double half = tile * 0.5;
    for (int v = 0; v < total_v; v++)
    {
        double x = (double)mvx[v] - cx;
        double y = (double)mvy[v] - cy;
        double z = (double)mvz[v] - cz;
        double x1 = x * cyw + z * sy;
        double z1 = -x * sy + z * cyw;
        double y2 = y * cp - z1 * sp;
        double z2 = y * sp + z1 * cp;
        double zc = z2 + dist;
        pz_cam[v] = zc;
        if (zc < dist * 0.05)
        {
            px_scr[v] = -1e18; /* near-clip sentinel */
            py_scr[v] = 0.0;
            continue;
        }
        px_scr[v] = half + x1 * focal / zc;
        py_scr[v] = half + y2 * focal / zc;
    }
}

/* Top-two z raster over the merged model. Backface cull matches toridraw:
 * winding (a-b)x(c-b) > 0 is front-facing, which in the (b-a)x(c-a) area
 * convention used here is area < 0. */
static void
raster_view(void)
{
    size_t n = (size_t)tile * (size_t)tile;
    for (size_t i = 0; i < n; i++)
    {
        vb_buf.z1[i] = 1e30;
        vb_buf.z2[i] = 1e30;
        vb_buf.f1[i] = -1;
        vb_buf.f2[i] = -1;
    }
    for (int f = 0; f < total_f; f++)
    {
        int ia = f_a[f], ib = f_b[f], ic = f_c[f];
        if (px_scr[ia] < -1e17 || px_scr[ib] < -1e17 || px_scr[ic] < -1e17)
            continue;
        double ax = px_scr[ia], ay = py_scr[ia];
        double bx = px_scr[ib], by = py_scr[ib];
        double cx = px_scr[ic], cy = py_scr[ic];
        double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (area >= -1e-9)
            continue; /* backfacing or degenerate */
        int min_x = (int)floor(fmin(ax, fmin(bx, cx)));
        int max_x = (int)ceil(fmax(ax, fmax(bx, cx)));
        int min_y = (int)floor(fmin(ay, fmin(by, cy)));
        int max_y = (int)ceil(fmax(ay, fmax(by, cy)));
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x >= tile) max_x = tile - 1;
        if (max_y >= tile) max_y = tile - 1;
        if (min_x > max_x || min_y > max_y)
            continue;
        double iza = 1.0 / pz_cam[ia];
        double izb = 1.0 / pz_cam[ib];
        double izc = 1.0 / pz_cam[ic];
        for (int y = min_y; y <= max_y; y++)
        {
            for (int x = min_x; x <= max_x; x++)
            {
                double pxc = x + 0.5, pyc = y + 0.5;
                double w0 = (bx - ax) * (pyc - ay) - (by - ay) * (pxc - ax);
                double w1 = (cx - bx) * (pyc - by) - (cy - by) * (pxc - bx);
                double w2 = (ax - cx) * (pyc - cy) - (ay - cy) * (pxc - cx);
                if (w0 > 0.0 || w1 > 0.0 || w2 > 0.0)
                    continue; /* area < 0: inside means all edges <= 0 */
                double la = w1 / area;
                double lb = w2 / area;
                double lc = w0 / area;
                double inv_z = la * iza + lb * izb + lc * izc;
                if (!(inv_z > 0.0))
                    continue;
                double z = 1.0 / inv_z;
                size_t at = (size_t)y * tile + x;
                if (z < vb_buf.z1[at])
                {
                    if (vb_buf.f1[at] >= 0 && vb_buf.f1[at] != f)
                    {
                        vb_buf.z2[at] = vb_buf.z1[at];
                        vb_buf.f2[at] = vb_buf.f1[at];
                    }
                    vb_buf.z1[at] = z;
                    vb_buf.f1[at] = f;
                }
                else if (vb_buf.f1[at] != f && z < vb_buf.z2[at])
                {
                    vb_buf.z2[at] = z;
                    vb_buf.f2[at] = f;
                }
            }
        }
    }
}

/* --------------------------------------------------------- view sweeping */

static int yaw_count = 16;
static int pitches[MAX_PITCHES] = { 34, 128, 256 };
static int pitch_count = 3;

struct SweepStats
{
    long covered;
    long fights;         /* near-tie pixels between non-adjacent faces */
    long fights_treated; /* ...restricted to qualified pairs */
};

/* detect: accumulate into the pair table. verify: only count. */
static void
sweep_views(int detect, struct SweepStats* st)
{
    double minc[3] = { 1e30, 1e30, 1e30 }, maxc[3] = { -1e30, -1e30, -1e30 };
    for (int v = 0; v < total_v; v++)
    {
        double c[3] = { (double)mvx[v], (double)mvy[v], (double)mvz[v] };
        for (int a = 0; a < 3; a++)
        {
            if (c[a] < minc[a]) minc[a] = c[a];
            if (c[a] > maxc[a]) maxc[a] = c[a];
        }
    }
    double cx = (minc[0] + maxc[0]) * 0.5;
    double cy = (minc[1] + maxc[1]) * 0.5;
    double cz = (minc[2] + maxc[2]) * 0.5;
    double radius = 0.5 * sqrt((maxc[0] - minc[0]) * (maxc[0] - minc[0])
                             + (maxc[1] - minc[1]) * (maxc[1] - minc[1])
                             + (maxc[2] - minc[2]) * (maxc[2] - minc[2]));
    if (radius < 1.0)
        radius = 1.0;
    double dist = radius * 2.2;
    double focal = (tile * 0.5 - 4.0) * dist / radius;

    st->covered = st->fights = st->fights_treated = 0;
    if (!detect)
        for (uint64_t i = 0; i < pair_cap; i++)
            if (pair_tab[i].key != UINT64_MAX)
                pair_tab[i].count_verify = 0;

    build_position_ids();
    for (int pi = 0; pi < pitch_count; pi++)
    {
        double pitch_rad = pitches[pi] * (2.0 * PI / 2048.0);
        for (int yi = 0; yi < yaw_count; yi++)
        {
            double yaw_rad = yi * (2.0 * PI / yaw_count);
            double fwd[3];
            project_view(yaw_rad, pitch_rad, cx, cy, cz, dist, focal, fwd);
            raster_view();
            size_t n = (size_t)tile * (size_t)tile;
            for (size_t i = 0; i < n; i++)
            {
                if (vb_buf.f1[i] < 0)
                    continue;
                st->covered++;
                if (vb_buf.f2[i] < 0
                    || vb_buf.z2[i] - vb_buf.z1[i] > fight_eps)
                    continue;
                int fa = vb_buf.f1[i], fb = vb_buf.f2[i];
                if (faces_share_position(fa, fb))
                    continue;
                st->fights++;
                if (detect)
                {
                    struct Pair* pr = pair_find(fa, fb, 1);
                    pr->count++;
                    pr->fwd[0] += fwd[0];
                    pr->fwd[1] += fwd[1];
                    pr->fwd[2] += fwd[2];
                }
                else
                {
                    struct Pair* pr = pair_find(fa, fb, 0);
                    if (pr && pr->qualified)
                    {
                        pr->count_verify++;
                        st->fights_treated++;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------- top choice + layering */

static double
face_area(int f)
{
    double ax = mvx[f_a[f]], ay = mvy[f_a[f]], az = mvz[f_a[f]];
    double bx = mvx[f_b[f]], by = mvy[f_b[f]], bz = mvz[f_b[f]];
    double cx = mvx[f_c[f]], cy = mvy[f_c[f]], cz = mvz[f_c[f]];
    double ux = bx - ax, uy = by - ay, uz = bz - az;
    double vx = cx - ax, vy = cy - ay, vz = cz - az;
    double nx = uy * vz - uz * vy;
    double ny = uz * vx - ux * vz;
    double nz = ux * vy - uy * vx;
    return 0.5 * sqrt(nx * nx + ny * ny + nz * nz);
}

static void
face_normal(int f, double* n)
{
    double ax = mvx[f_a[f]], ay = mvy[f_a[f]], az = mvz[f_a[f]];
    double ux = mvx[f_b[f]] - ax, uy = mvy[f_b[f]] - ay, uz = mvz[f_b[f]] - az;
    double vx = mvx[f_c[f]] - ax, vy = mvy[f_c[f]] - ay, vz = mvz[f_c[f]] - az;
    n[0] = uy * vz - uz * vy;
    n[1] = uz * vx - ux * vz;
    n[2] = ux * vy - uy * vx;
    double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 1e-12)
    {
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
    }
}

static int* face_level;      /* per merged face; 0 = does not move */
static double (*face_dir)[3]; /* per merged face outward unit normal */
static uint8_t* face_moving;

static long min_pixels = 24;

static int
resolve_pairs(void)
{
    face_level = calloc((size_t)total_f, sizeof *face_level);
    face_dir = calloc((size_t)total_f, sizeof *face_dir);
    face_moving = calloc((size_t)total_f, 1);
    assert(face_level);
    assert(face_dir);
    assert(face_moving);

    int qualified = 0;
    for (uint64_t s = 0; s < pair_cap; s++)
    {
        struct Pair* pr = &pair_tab[s];
        if (pr->key == UINT64_MAX || pr->count < min_pixels)
            continue;
        int fa = (int)(pr->key >> 32), fb = (int)(pr->key & 0xFFFFFFFFu);
        int top;
        if (f_prio[fa] != f_prio[fb])
        {
            /* Priority partition order: higher priority is emitted later in
             * the painter's draw and was authored to sit on top. */
            top = f_prio[fa] > f_prio[fb] ? fa : fb;
            pr->rule = 'p';
        }
        else
        {
            top = face_area(fa) <= face_area(fb) ? fa : fb;
            pr->rule = 'a';
        }
        pr->top = top;
        pr->qualified = 1;
        qualified++;
        face_moving[top] = 1;

        /* Outward = against the mean camera forward that saw the fight:
         * the visible side of the top layer faces those cameras. */
        double n[3];
        face_normal(top, n);
        double dot = n[0] * pr->fwd[0] + n[1] * pr->fwd[1]
                   + n[2] * pr->fwd[2];
        double sign = dot > 0.0 ? -1.0 : 1.0;
        face_dir[top][0] += sign * n[0] * (double)pr->count;
        face_dir[top][1] += sign * n[1] * (double)pr->count;
        face_dir[top][2] += sign * n[2] * (double)pr->count;
    }

    /* Layer levels: longest path over bottom->top edges, so a decal on a
     * decal moves twice as far. Relaxation with a hop cap doubles as cycle
     * protection. */
    for (int f = 0; f < total_f; f++)
        face_level[f] = face_moving[f] ? 1 : 0;
    for (int hop = 0; hop < MAX_LEVEL; hop++)
    {
        int changed = 0;
        for (uint64_t s = 0; s < pair_cap; s++)
        {
            struct Pair* pr = &pair_tab[s];
            if (pr->key == UINT64_MAX || !pr->qualified)
                continue;
            int fa = (int)(pr->key >> 32), fb = (int)(pr->key & 0xFFFFFFFFu);
            int bottom = pr->top == fa ? fb : fa;
            int want = face_level[bottom] + 1;
            if (want > MAX_LEVEL)
                want = MAX_LEVEL;
            if (face_level[pr->top] < want)
            {
                face_level[pr->top] = want;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }

    for (int f = 0; f < total_f; f++)
        if (face_moving[f])
        {
            double* d = face_dir[f];
            double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 1e-12)
            {
                d[0] /= len;
                d[1] /= len;
                d[2] /= len;
            }
            else
            {
                /* opposing observations cancelled — fall back to the raw
                 * normal; wrong sign only re-creates the tie, never worse */
                face_normal(f, d);
            }
        }
    return qualified;
}

/* ----------------------------------------- vertex instancing + movement */

/* One moving instance = (part, output vertex index): either an original
 * vertex used exclusively by moving faces of one level, or a duplicate
 * minted for that (vertex, level) so everything else keeps the original. */
struct MoveInst
{
    int part;
    int vid;    /* index in the part's (possibly grown) vertex arrays */
    int ox, oy, oz;
    double dir[3];
    int level;
};

static struct MoveInst* insts;
static int inst_count;

static void
grow_part_vertices(struct Part* pt, int extra)
{
    struct RSCache_Model* m = pt->m;
    int nv = m->vertex_count + extra;
    m->vertices_x = realloc(m->vertices_x, (size_t)nv * sizeof(int));
    m->vertices_y = realloc(m->vertices_y, (size_t)nv * sizeof(int));
    m->vertices_z = realloc(m->vertices_z, (size_t)nv * sizeof(int));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    if (m->vertex_bone_map)
    {
        m->vertex_bone_map = realloc(m->vertex_bone_map, (size_t)nv);
        if (!m->vertex_bone_map)
            die("out of memory (bone map growth)");
    }
    if (m->animaya_group_counts)
    {
        m->animaya_group_counts =
            realloc(m->animaya_group_counts, (size_t)nv);
        m->animaya_groups =
            realloc(m->animaya_groups, (size_t)nv * sizeof(uint8_t*));
        m->animaya_scales =
            realloc(m->animaya_scales, (size_t)nv * sizeof(uint8_t*));
        if (!m->animaya_group_counts || !m->animaya_groups
            || !m->animaya_scales)
            die("out of memory (animaya growth)");
    }
}

static int
dup_vertex(struct Part* pt, int v)
{
    struct RSCache_Model* m = pt->m;
    int d = m->vertex_count;
    m->vertices_x[d] = m->vertices_x[v];
    m->vertices_y[d] = m->vertices_y[v];
    m->vertices_z[d] = m->vertices_z[v];
    if (m->vertex_bone_map)
        m->vertex_bone_map[d] = m->vertex_bone_map[v];
    if (m->animaya_group_counts)
    {
        int cnt = m->animaya_group_counts[v];
        m->animaya_group_counts[d] = (uint8_t)cnt;
        m->animaya_groups[d] = malloc(cnt ? (size_t)cnt : 1);
        m->animaya_scales[d] = malloc(cnt ? (size_t)cnt : 1);
        assert(m->animaya_groups[d]);
        assert(m->animaya_scales[d]);
        memcpy(m->animaya_groups[d], m->animaya_groups[v], (size_t)cnt);
        memcpy(m->animaya_scales[d], m->animaya_scales[v], (size_t)cnt);
        m->animaya_vertex_count = d + 1;
    }
    m->vertex_count = d + 1;
    pt->verts_added++;
    return d;
}

/* Split moving layers off shared vertices and build the instance list.
 * Per part: a vertex used by moving faces of level L becomes instance
 * (v, L); it stays in place only when NOTHING else uses it — other faces,
 * other levels, or texture p/m/n projection triangles. */
static void
build_instances(void)
{
    insts = malloc((size_t)total_v * (MAX_LEVEL + 1) * sizeof *insts);
    if (!insts)
        die("out of memory (instances)");
    inst_count = 0;

    for (int p = 0; p < part_count; p++)
    {
        struct Part* pt = &parts[p];
        struct RSCache_Model* m = pt->m;
        int V = m->vertex_count;

        /* usage per local vertex */
        int* mov_level = malloc((size_t)V * sizeof(int)); /* -1 none, -2 mixed */
        uint8_t* used_static = calloc((size_t)V, 1);
        double (*dir_sum)[3] = calloc((size_t)V, sizeof *dir_sum);
        assert(mov_level);
        assert(used_static);
        if( !dir_sum )
            die("out of memory (usage scan)");
        for (int v = 0; v < V; v++)
            mov_level[v] = -1;

        int fb = 0;
        for (int q = 0; q < p; q++)
            fb += parts[q].m->face_count;
        for (int f = 0; f < m->face_count; f++)
        {
            int g = fb + f;
            int idx[3] = { m->face_indices_a[f], m->face_indices_b[f],
                           m->face_indices_c[f] };
            if (face_moving[g])
            {
                for (int k = 0; k < 3; k++)
                {
                    int v = idx[k];
                    if (mov_level[v] == -1)
                        mov_level[v] = face_level[g];
                    else if (mov_level[v] != face_level[g])
                        mov_level[v] = -2;
                    dir_sum[v][0] += face_dir[g][0];
                    dir_sum[v][1] += face_dir[g][1];
                    dir_sum[v][2] += face_dir[g][2];
                }
            }
            else
                for (int k = 0; k < 3; k++)
                    used_static[idx[k]] = 1;
        }
        if (m->textured_p_coordinate)
            for (int t = 0; t < m->textured_face_count; t++)
            {
                used_static[m->textured_p_coordinate[t]] = 1;
                used_static[m->textured_m_coordinate[t]] = 1;
                used_static[m->textured_n_coordinate[t]] = 1;
            }

        /* count duplicates needed: shared vertices, or vertices where two
         * moving levels meet (each level gets its own copy; the original
         * stays for the rest) */
        int need = 0;
        for (int v = 0; v < V; v++)
            if (mov_level[v] == -2 || (mov_level[v] >= 1 && used_static[v]))
                need += mov_level[v] == -2 ? MAX_LEVEL : 1;
        grow_part_vertices(pt, need);

        /* per (vertex, level) remap for the mixed case */
        for (int f = 0; f < m->face_count; f++)
        {
            int g = fb + f;
            if (!face_moving[g])
                continue;
            int L = face_level[g];
            int* fi[3] = { &m->face_indices_a[f], &m->face_indices_b[f],
                           &m->face_indices_c[f] };
            for (int k = 0; k < 3; k++)
            {
                int v = *fi[k];
                if (v >= V)
                    continue; /* already remapped to a duplicate */
                if (mov_level[v] >= 1 && !used_static[v])
                    continue; /* whole vertex moves in place; handled below */
                /* shared or mixed-level: find/mint the duplicate for (v,L) */
                int d = -1;
                for (int i = 0; i < inst_count; i++)
                    if (insts[i].part == p && insts[i].level == L
                        && insts[i].ox == m->vertices_x[v]
                        && insts[i].oy == m->vertices_y[v]
                        && insts[i].oz == m->vertices_z[v]
                        && insts[i].vid >= V)
                    {
                        d = insts[i].vid;
                        break;
                    }
                if (d < 0)
                {
                    d = dup_vertex(pt, v);
                    struct MoveInst* in = &insts[inst_count++];
                    in->part = p;
                    in->vid = d;
                    in->ox = m->vertices_x[v];
                    in->oy = m->vertices_y[v];
                    in->oz = m->vertices_z[v];
                    in->dir[0] = 0.0;
                    in->dir[1] = 0.0;
                    in->dir[2] = 0.0;
                    in->level = L;
                }
                *fi[k] = d;
                /* accumulate this face's direction on the instance */
                for (int i = 0; i < inst_count; i++)
                    if (insts[i].part == p && insts[i].vid == d)
                    {
                        insts[i].dir[0] += face_dir[g][0];
                        insts[i].dir[1] += face_dir[g][1];
                        insts[i].dir[2] += face_dir[g][2];
                        break;
                    }
            }
        }

        /* exclusively-moving vertices move in place */
        for (int v = 0; v < V; v++)
        {
            if (mov_level[v] < 1 || used_static[v])
                continue;
            struct MoveInst* in = &insts[inst_count++];
            in->part = p;
            in->vid = v;
            in->ox = m->vertices_x[v];
            in->oy = m->vertices_y[v];
            in->oz = m->vertices_z[v];
            in->dir[0] = dir_sum[v][0];
            in->dir[1] = dir_sum[v][1];
            in->dir[2] = dir_sum[v][2];
            in->level = mov_level[v];
        }

        free(mov_level);
        free(used_static);
        free(dir_sum);
    }

    for (int i = 0; i < inst_count; i++)
    {
        double* d = insts[i].dir;
        double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-12)
        {
            d[0] /= len;
            d[1] /= len;
            d[2] /= len;
        }
    }
}

/* Reposition every instance for a cumulative delta: from the ORIGINAL
 * coordinates every time, so the ladder never accumulates rounding. A
 * displacement that rounds to zero is bumped to one unit on the dominant
 * axis — an offset that does nothing is worse than none at all. */
static void
apply_delta(double delta)
{
    for (int i = 0; i < inst_count; i++)
    {
        struct MoveInst* in = &insts[i];
        struct RSCache_Model* m = parts[in->part].m;
        double mag = delta * in->level;
        int dx = (int)lround(in->dir[0] * mag);
        int dy = (int)lround(in->dir[1] * mag);
        int dz = (int)lround(in->dir[2] * mag);
        if (dx == 0 && dy == 0 && dz == 0 && mag > 0.0)
        {
            double ax = fabs(in->dir[0]), ay = fabs(in->dir[1]),
                   az = fabs(in->dir[2]);
            if (ax >= ay && ax >= az)
                dx = in->dir[0] < 0 ? -1 : 1;
            else if (ay >= az)
                dy = in->dir[1] < 0 ? -1 : 1;
            else
                dz = in->dir[2] < 0 ? -1 : 1;
        }
        m->vertices_x[in->vid] = in->ox + dx;
        m->vertices_y[in->vid] = in->oy + dy;
        m->vertices_z[in->vid] = in->oz + dz;
    }
}

/* ---------------------------------------------------------- verification */

static int
verify_roundtrip(struct Part* pt, uint8_t* bytes, uint32_t size)
{
    struct RSCache_Model* m = pt->m;
    struct RSCache_Model* back = RSCache_ModelNewDecode(bytes, (int)size);
    if (!back)
    {
        printf("  VERIFY FAIL: %s does not decode\n", pt->out_path);
        return 0;
    }
    int ok = 1;
    if (back->vertex_count != m->vertex_count
        || back->face_count != m->face_count)
    {
        printf("  VERIFY FAIL: counts %d/%d decoded as %d/%d\n",
               m->vertex_count, m->face_count, back->vertex_count,
               back->face_count);
        ok = 0;
    }
    if (ok)
        for (int v = 0; v < m->vertex_count; v++)
            if (back->vertices_x[v] != m->vertices_x[v]
                || back->vertices_y[v] != m->vertices_y[v]
                || back->vertices_z[v] != m->vertices_z[v]
                || (m->vertex_bone_map && back->vertex_bone_map
                    && back->vertex_bone_map[v] != m->vertex_bone_map[v]))
            {
                printf("  VERIFY FAIL: vertex %d round-trip mismatch\n", v);
                ok = 0;
                break;
            }
    if (ok)
        for (int f = 0; f < m->face_count; f++)
            if (back->face_indices_a[f] != m->face_indices_a[f]
                || back->face_indices_b[f] != m->face_indices_b[f]
                || back->face_indices_c[f] != m->face_indices_c[f])
            {
                printf("  VERIFY FAIL: face %d round-trip mismatch\n", f);
                ok = 0;
                break;
            }
    RSCache_ModelFree(back);
    return ok;
}

/* ------------------------------------------------------------------ main */

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
usage(void)
{
    fprintf(stderr,
        "usage: rs2012_model_defight --in part.ob3 [--in ...] --out fixed.ob3"
        " [--out ...]\n"
        "         --report report.json [options]\n"
        "  --yaws N        views per pitch ring (default 16)\n"
        "  --pitches CSV   camera pitches, RS units of 2048 (default"
        " 34,128,256)\n"
        "  --tile N        raster size per view (default 256)\n"
        "  --fight-eps F   depth tie threshold, model units (default 2.5)\n"
        "  --min-pixels N  observations for a pair to qualify (default 24)\n"
        "  --deltas CSV    escalating offset ladder, model units (default"
        " 3,6,9,12)\n"
        "  --quiet         summary only\n");
    exit(2);
}

int
main(int argc, char** argv)
{
    const char* in_paths[MAX_PARTS];
    const char* out_paths[MAX_PARTS];
    int n_in = 0, n_out = 0;
    const char* report_path = NULL;
    double deltas[MAX_DELTAS] = { 3.0, 6.0, 9.0, 12.0 };
    int delta_count = 4;

    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        const char* val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--in") && val && n_in < MAX_PARTS)
            in_paths[n_in++] = argv[++i];
        else if (!strcmp(a, "--out") && val && n_out < MAX_PARTS)
            out_paths[n_out++] = argv[++i];
        else if (!strcmp(a, "--report") && val)
            report_path = argv[++i];
        else if (!strcmp(a, "--yaws") && val)
            yaw_count = atoi(argv[++i]);
        else if (!strcmp(a, "--tile") && val)
            tile = atoi(argv[++i]);
        else if (!strcmp(a, "--fight-eps") && val)
            fight_eps = atof(argv[++i]);
        else if (!strcmp(a, "--min-pixels") && val)
            min_pixels = atol(argv[++i]);
        else if (!strcmp(a, "--pitches") && val)
        {
            pitch_count = 0;
            for (char* t = strtok(argv[++i], ","); t && pitch_count < MAX_PITCHES;
                 t = strtok(NULL, ","))
                pitches[pitch_count++] = atoi(t);
        }
        else if (!strcmp(a, "--deltas") && val)
        {
            delta_count = 0;
            for (char* t = strtok(argv[++i], ","); t && delta_count < MAX_DELTAS;
                 t = strtok(NULL, ","))
                deltas[delta_count++] = atof(t);
        }
        else if (!strcmp(a, "--quiet"))
            quiet = 1;
        else
            usage();
    }
    if (!n_in || n_in != n_out || !report_path)
        usage();
    if (yaw_count < 4)
        yaw_count = 4;
    if (tile < 64)
        tile = 64;

    part_count = n_in;
    for (int p = 0; p < part_count; p++)
    {
        struct Part* pt = &parts[p];
        pt->in_path = in_paths[p];
        pt->out_path = out_paths[p];
        int size = 0;
        uint8_t* bytes = read_file(pt->in_path, &size);
        pt->m = RSCache_ModelNewDecodeProvenance(bytes, size, &pt->prov);
        if (!pt->m || !pt->prov)
            die("%s does not decode as a model", pt->in_path);
        free(bytes);
        struct RSCache_Model* m = pt->m;
        if (m->animaya_group_counts
            && m->animaya_vertex_count != m->vertex_count)
            die("%s: animaya vertex count %d != vertex count %d",
                pt->in_path, m->animaya_vertex_count, m->vertex_count);
        /* Same contract as rs2012_model_decimate: version-13+ stores 4x
         * coordinates; emit honest version-1 final units. */
        if (m->format_version >= 13)
        {
            for (int v = 0; v < m->vertex_count; v++)
            {
                m->vertices_x[v] >>= 2;
                m->vertices_y[v] >>= 2;
                m->vertices_z[v] >>= 2;
            }
            m->format_version = 1;
            pt->coords_rescaled = 1;
        }
        pt->orig_vertex_count = m->vertex_count;
        if (!quiet)
            printf("part %d: %s — %d vertices, %d faces%s\n", p, pt->in_path,
                   m->vertex_count, m->face_count,
                   pt->coords_rescaled ? " (rescaled 4x->1x)" : "");
    }

    build_merged_topology();
    refresh_merged_positions();
    raster_alloc();
    pair_init();

    printf("defight: %d parts, %d vertices, %d faces; %d views (%d yaws x %d"
           " pitches), tile %d, eps %.2f\n",
           part_count, total_v, total_f, yaw_count * pitch_count, yaw_count,
           pitch_count, tile, fight_eps);

    struct SweepStats before;
    sweep_views(1, &before);
    printf("detect: %ld covered pixels, %ld fight pixels, %d fighting pairs\n",
           before.covered, before.fights, pair_count);

    int qualified = resolve_pairs();
    long qualified_pixels = 0;
    for (uint64_t s = 0; s < pair_cap; s++)
        if (pair_tab[s].key != UINT64_MAX && pair_tab[s].qualified)
            qualified_pixels += pair_tab[s].count;
    printf("qualify: %d pairs >= %ld pixels (%ld pixels total)\n", qualified,
           min_pixels, qualified_pixels);

    struct SweepStats after = before;
    double delta_used = 0.0;
    int ladder_steps = 0;
    struct { double delta; long treated; long total; } ladder[MAX_DELTAS];

    if (qualified > 0)
    {
        build_instances();
        printf("layers: %d moving instances", inst_count);
        {
            int added = 0;
            for (int p = 0; p < part_count; p++)
                added += parts[p].verts_added;
            printf(", %d duplicated vertices\n", added);
        }
        /* topology changed (duplicates + remapped faces): rebuild the merged
         * view before verifying. Face ids are stable — faces were only
         * repointed, never added — so the pair table stays valid. */
        for (int d = 0; d < delta_count; d++)
        {
            apply_delta(deltas[d]);
            build_merged_topology();
            refresh_merged_positions();
            /* face_area/face_normal above used the pre-dup arrays; only the
             * raster path runs from here on. */
            sweep_views(0, &after);
            delta_used = deltas[d];
            ladder[ladder_steps].delta = deltas[d];
            ladder[ladder_steps].treated = after.fights_treated;
            ladder[ladder_steps].total = after.fights;
            ladder_steps++;
            printf("delta %.0f: treated pairs down to %ld fight pixels (of"
                   " %ld), %ld total\n",
                   deltas[d], after.fights_treated, qualified_pixels,
                   after.fights);
            long target = qualified_pixels / 10;
            if (target < min_pixels)
                target = min_pixels;
            if (after.fights_treated <= target)
                break;
        }
    }
    else
        printf("nothing to fix\n");

    /* ------------------------------------------------------------ encode */
    int all_ok = 1;
    for (int p = 0; p < part_count; p++)
    {
        struct Part* pt = &parts[p];
        uint32_t bound = RSCache_ModelEncodeBound(pt->m, NULL);
        uint8_t* out_bytes = malloc(bound);
        if (!out_bytes)
            die("out of memory (encode buffer)");
        uint32_t written = RSCache_ModelEncodeFormat(pt->m, NULL,
                                                     pt->prov->format,
                                                     out_bytes, bound);
        if (!written)
            die("encode failed for %s (%d verts, %d faces)", pt->out_path,
                pt->m->vertex_count, pt->m->face_count);
        write_file(pt->out_path, out_bytes, written);
        int ok = verify_roundtrip(pt, out_bytes, written);
        printf("part %d: wrote %s (%d verts, +%d) decode-back %s\n", p,
               pt->out_path, pt->m->vertex_count, pt->verts_added,
               ok ? "OK" : "FAILED");
        all_ok &= ok;
        free(out_bytes);
    }

    /* ------------------------------------------------------------ report */
    {
        FILE* fp = fopen(report_path, "w");
        if (!fp)
            die("cannot create %s", report_path);
        fprintf(fp, "{\n  \"views\": {\"yaws\": %d, \"pitches\": [",
                yaw_count);
        for (int i = 0; i < pitch_count; i++)
            fprintf(fp, "%d%s", pitches[i], i + 1 < pitch_count ? "," : "");
        fprintf(fp, "], \"tile\": %d, \"fight_eps\": %g, \"min_pixels\":"
                " %ld},\n", tile, fight_eps, min_pixels);
        fprintf(fp, "  \"parts\": [\n");
        for (int p = 0; p < part_count; p++)
        {
            struct Part* pt = &parts[p];
            fprintf(fp, "    {\"input\": \"");
            json_escape(fp, pt->in_path);
            fprintf(fp, "\", \"output\": \"");
            json_escape(fp, pt->out_path);
            fprintf(fp, "\", \"vertices\": %d, \"vertices_added\": %d,"
                    " \"faces\": %d, \"coords_rescaled\": %s}%s\n",
                    pt->m->vertex_count, pt->verts_added, pt->m->face_count,
                    pt->coords_rescaled ? "true" : "false",
                    p + 1 < part_count ? "," : "");
        }
        fprintf(fp, "  ],\n");
        fprintf(fp, "  \"covered_pixels\": %ld,\n", before.covered);
        fprintf(fp, "  \"fight_pixels_before\": %ld,\n", before.fights);
        fprintf(fp, "  \"pairs_fighting\": %d,\n", pair_count);
        fprintf(fp, "  \"pairs_qualified\": %d,\n", qualified);
        fprintf(fp, "  \"qualified_pixels_before\": %ld,\n", qualified_pixels);
        fprintf(fp, "  \"delta_used\": %g,\n", delta_used);
        fprintf(fp, "  \"ladder\": [");
        for (int i = 0; i < ladder_steps; i++)
            fprintf(fp, "%s{\"delta\": %g, \"treated_remaining\": %ld,"
                    " \"total_fights\": %ld}", i ? ", " : "",
                    ladder[i].delta, ladder[i].treated, ladder[i].total);
        fprintf(fp, "],\n");
        fprintf(fp, "  \"fight_pixels_after\": %ld,\n",
                qualified ? after.fights : before.fights);
        fprintf(fp, "  \"treated_pixels_after\": %ld,\n",
                qualified ? after.fights_treated : 0L);
        fprintf(fp, "  \"pairs\": [\n");
        int emitted = 0;
        for (uint64_t s = 0; s < pair_cap && emitted < PAIR_REPORT_CAP; s++)
        {
            struct Pair* pr = &pair_tab[s];
            if (pr->key == UINT64_MAX || !pr->qualified)
                continue;
            int fa = (int)(pr->key >> 32), fb = (int)(pr->key & 0xFFFFFFFFu);
            int top = pr->top, bot = top == fa ? fb : fa;
            fprintf(fp, "%s    {\"top\": {\"part\": %d, \"face\": %d,"
                    " \"prio\": %d, \"level\": %d}, \"bottom\": {\"part\":"
                    " %d, \"face\": %d, \"prio\": %d}, \"pixels\": %ld,"
                    " \"rule\": \"%s\", \"residual\": %ld}",
                    emitted ? ",\n" : "", f_part[top], f_local[top],
                    f_prio[top], face_level[top], f_part[bot], f_local[bot],
                    f_prio[bot], pr->count,
                    pr->rule == 'p' ? "priority" : "area", pr->count_verify);
            emitted++;
        }
        fprintf(fp, "\n  ],\n  \"verify\": \"%s\"\n}\n",
                all_ok ? "OK" : "FAILED");
        fclose(fp);
        printf("wrote %s\n", report_path);
    }

    return all_ok ? 0 : 1;
}
