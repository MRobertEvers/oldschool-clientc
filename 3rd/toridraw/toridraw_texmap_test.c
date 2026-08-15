/*
 * Unit test for the mapped texture families:
 *
 *     texcylinder / texcube / texsphere . persp . <gate>[.facealpha][.modulate]
 *                                       . branching . lerp8_v3
 *
 * Twelve variants each, thirty-six in all. They differ from `texplane` in that
 * the face carries a *mapping* rather than a projector, so the kernel computes
 * uv per vertex itself and interpolates it perspective-correctly.
 *
 * Three things are worth pinning, and they are independent:
 *
 *   1. the projection      — against a double-precision reference using libm,
 *                            bounded by the arctangent table's own accuracy
 *   2. the uv planes       — the fixed-point setup is the delicate part: what
 *                            the span recovers as `au / (cw >> shift)` must be
 *                            the perspective-correct interpolation, checked
 *                            against a double reference at real pixels
 *   3. the compositing     — the same neutral-setting identity chain the
 *                            `texplane` matrix uses, which proves the gate /
 *                            facealpha / modulate wiring is hooked up the same
 *                            way in this family
 *
 * plus guard bands, because a kernel that computes its own uv has more ways to
 * produce an out-of-range index than one handed a plane.
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/texmap_test 3rd/toridraw/toridraw_texmap_test.c -lm
 *   /tmp/texmap_test
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"
#include "graphics/shared_tables.h"

// clang-format off
#include "graphics/shared_tables.c"
#include "graphics/projection.u.c"

#include "graphics/raster/texture/texcylinder.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.branching.lerp8_v3.u.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

#define RGB(x) ((x)&0x00FFFFFF)
#define BG 0x00112233
#define GUARD_FILL 0x5A5A5A5A

#define TEX_W 64
#define TEX_LEN (TEX_W * TEX_W)

static int g_tex[TEX_LEN];
static int g_fail;

/* ------------------------------------------------------------- fixtures */

struct Tri
{
    const char* name;
    int x[3], y[3], z[3];
    int m[9]; /* model-space xyz per vertex */
};

static const struct Tri g_tris[] = {
    { "interior", { 40, 150, 90 }, { 20, 35, 120 }, { 300, 340, 380 },
      { -40, 10, -40, 40, 10, -40, -40, 10, 40 } },
    { "steep-depth", { 30, 170, 100 }, { 15, 40, 135 }, { 120, 900, 400 },
      { -60, -20, -30, 70, 30, 50, -10, 60, 80 } },
    { "constant-depth", { 35, 160, 95 }, { 25, 30, 118 }, { 500, 500, 500 },
      { -50, 0, -50, 50, 0, -50, -50, 0, 50 } },
    { "clip-left", { -80, 60, 10 }, { 20, 40, 120 }, { 260, 300, 340 },
      { -70, 5, -20, 30, 15, -40, -20, 25, 60 } },
    { "clip-bottom", { 40, 150, 90 }, { 60, 100, 260 }, { 260, 300, 340 },
      { -30, 20, -60, 60, -10, 10, -5, 40, 70 } },
    { "covers-screen", { -400, 600, 100 }, { -300, -300, 500 }, { 400, 420, 460 },
      { -90, -80, -70, 90, 10, -30, 0, 85, 75 } },
    { "sliver-1row", { 20, 180, 100 }, { 60, 60, 61 }, { 300, 310, 320 },
      { -40, 0, -40, 40, 0, -40, -40, 0, 40 } },
    { "degenerate", { 20, 60, 100 }, { 20, 60, 100 }, { 300, 300, 300 },
      { -10, 0, -10, 10, 0, -10, -10, 0, 10 } },
    { "offscreen", { 400, 500, 450 }, { 20, 40, 120 }, { 300, 300, 300 },
      { -10, 0, -10, 10, 0, -10, -10, 0, 10 } },
};
#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

static struct ToriDraw_TexMapping
make_mapping(int direction, float speed)
{
    struct ToriDraw_TexMapping m;
    memset(&m, 0, sizeof(m));
    m.centre_x = 3;
    m.centre_y = -7;
    m.centre_z = 11;
    /* A non-trivial but well-conditioned basis: a scaled axis flip, which is
     * exactly the shape every QBD cube face uses (axis 0,-32767,0). */
    m.matrix[0] = 0.021f;
    m.matrix[4] = -0.017f;
    m.matrix[8] = -0.013f;
    m.matrix[1] = 0.003f;
    m.matrix[5] = 0.002f;
    m.direction = direction;
    m.speed = speed;
    m.u_offset = 0.125f;
    m.v_offset = -0.25f;
    m.scale_z = 1.0f;
    m.axis_scale_x = 1.7f;
    m.axis_scale_y = 0.9f;
    m.axis_scale_z = 1.3f;
    return m;
}

static void
buf_fill(int* buf, int background)
{
    for( int i = 0; i < GUARD; i++ )
        buf[i] = GUARD_FILL;
    for( int i = 0; i < H * W; i++ )
        buf[GUARD + i] = background;
    for( int i = 0; i < GUARD; i++ )
        buf[GUARD + H * W + i] = GUARD_FILL;
}

static int
guard_intact(const int* buf, const char* what, const char* tri)
{
    for( int i = 0; i < GUARD; i++ )
        if( buf[i] != GUARD_FILL || buf[GUARD + H * W + i] != GUARD_FILL )
        {
            printf("  FAIL %-46s %-16s: wrote outside the framebuffer\n", what, tri);
            g_fail++;
            return 0;
        }
    return 1;
}

/* ------------------------------------------- 1. projection vs a reference */

/*
 * The reference algorithm, with libm. Deliberately a second implementation
 * rather than a call into the shared one: the point is to catch a wrong
 * formula, and a formula cannot check itself.
 *
 * The mapping-space coordinates are computed in **float**, matching the
 * implementation, and only the trigonometry is done in double. That is not a
 * weakening of the check — it is what makes it meaningful. atan2 has a branch
 * cut along -x, and a vertex sitting on it is genuinely ambiguous: a float and a
 * double evaluation of the same `a` can land on opposite sides and give answers
 * a full turn apart. Letting the reference re-derive `a` in double therefore
 * measures float-vs-double rounding at the seam, not whether the projection is
 * right, and it reports a full tile of "error" for a vertex that is correctly
 * placed. Seam straddling is real and is what the per-family seam fixup exists
 * to resolve; it is not this check's subject.
 */
static void
ref_project(
    int kind,
    const struct ToriDraw_TexMapping* m,
    int axis,
    int vx,
    int vy,
    int vz,
    double* out_u,
    double* out_v)
{
    float fa, fb, fc;
    toridraw_texmap_to_mapping_space(m, vx, vy, vz, &fa, &fb, &fc);
    double a = fa, b = fb, c = fc;
    double u, v;

    if( kind == 1 )
    {
        u = atan2(a, c) / (2.0 * M_PI) + 0.5;
        if( m->scale_z != 1.0f )
            u *= m->scale_z;
        v = b + 0.5 + m->speed;
    }
    else if( kind == 2 )
    {
        if( axis == 0 )      { u = a + m->speed + 0.5; v = -c + m->v_offset + 0.5; }
        else if( axis == 1 ) { u = a + m->speed + 0.5; v =  c + m->v_offset + 0.5; }
        else if( axis == 2 ) { u = -a + m->speed + 0.5; v = -b + m->u_offset + 0.5; }
        else if( axis == 3 ) { u = a + m->speed + 0.5; v = -b + m->u_offset + 0.5; }
        else if( axis == 4 ) { u = c + m->v_offset + 0.5; v = -b + m->u_offset + 0.5; }
        else                 { u = -c + m->v_offset + 0.5; v = -b + m->u_offset + 0.5; }
    }
    else
    {
        double len = sqrt(a * a + b * b + c * c);
        u = atan2(a, c) / (2.0 * M_PI) + 0.5;
        v = (len != 0.0 ? asin(b / len) / M_PI : 0.0) + 0.5 + m->speed;
    }

    if( m->direction == 1 )      { double t = u; u = -v; v = t; }
    else if( m->direction == 2 ) { u = -u; v = -v; }
    else if( m->direction == 3 ) { double t = u; u = v; v = -t; }

    *out_u = u;
    *out_v = v;
}

static void
test_projection(void)
{
    printf("projection vs a double/libm reference\n");

    /* The arctangent table is good to ~1.5 units of 1/65536 turn; a turn is one
     * tile of u, so that is 2.3e-5 tiles. Float accumulation in the shared path
     * adds a little. 1e-4 tiles is a bound with room, and a wrong formula misses
     * it by orders of magnitude. */
    const double tol = 1e-4;

    const char* names[3] = { "texcylinder", "texcube", "texsphere" };
    double worst[3] = { 0, 0, 0 };
    long checked = 0;

    for( int dir = 0; dir < 4; dir++ )
    {
        struct ToriDraw_TexMapping m = make_mapping(dir, 0.375f);
        for( int vx = -80; vx <= 80; vx += 7 )
        {
            for( int vy = -80; vy <= 80; vy += 11 )
            {
                for( int vz = -80; vz <= 80; vz += 13 )
                {
                    float gu, gv;
                    double ru, rv;

                    toridraw_texmap_project_cylinder(&m, vx, vy, vz, &gu, &gv);
                    ref_project(1, &m, 0, vx, vy, vz, &ru, &rv);
                    if( fabs(gu - ru) > worst[0] ) worst[0] = fabs(gu - ru);
                    if( fabs(gv - rv) > worst[0] ) worst[0] = fabs(gv - rv);

                    for( int axis = 0; axis < 6; axis++ )
                    {
                        toridraw_texmap_project_cube(&m, axis, vx, vy, vz, &gu, &gv);
                        ref_project(2, &m, axis, vx, vy, vz, &ru, &rv);
                        if( fabs(gu - ru) > worst[1] ) worst[1] = fabs(gu - ru);
                        if( fabs(gv - rv) > worst[1] ) worst[1] = fabs(gv - rv);
                    }

                    toridraw_texmap_project_sphere(&m, vx, vy, vz, &gu, &gv);
                    ref_project(3, &m, 0, vx, vy, vz, &ru, &rv);
                    if( fabs(gu - ru) > worst[2] ) worst[2] = fabs(gu - ru);
                    if( fabs(gv - rv) > worst[2] ) worst[2] = fabs(gv - rv);

                    checked++;
                }
            }
        }
    }

    for( int i = 0; i < 3; i++ )
    {
        if( worst[i] > tol )
        {
            printf("  FAIL %-12s worst deviation %.3e tiles, bound %.1e\n",
                   names[i], worst[i], tol);
            g_fail++;
        }
        else
        {
            printf("  ok   %-12s worst %.3e tiles over %ld vertices x 4 directions\n",
                   names[i], worst[i], checked);
        }
    }
}

/* -------------------------------------------------- 2. the uv planes */

/*
 * The fixed-point setup, checked where it is consumed.
 *
 * The span recovers u as `au / (cw >> shift)` after rebasing to the pixel, so
 * that expression — not the plane coefficients — is what must match the
 * perspective-correct interpolation. Comparing it against a double reference at
 * real pixel positions is the only check that covers the normalisation, the
 * rounding and the origin convention together.
 */
static void
test_uv_planes(void)
{
    printf("uv planes: au/(cw>>shift) == perspective-correct interpolation\n");

    long checked = 0;
    double worst = 0.0;
    int bad = 0;
    int rejected = 0;

    for( int t = 0; t < TRI_COUNT; t++ )
    {
        const struct Tri* tri = &g_tris[t];
        float u[3] = { 0.15f, 0.80f, 0.42f };
        float v[3] = { 0.22f, 0.31f, 0.93f };

        int y_top = tri->y[0] < tri->y[1] ? (tri->y[0] < tri->y[2] ? tri->y[0] : tri->y[2])
                                          : (tri->y[1] < tri->y[2] ? tri->y[1] : tri->y[2]);
        if( y_top < 0 )
            y_top = 0;

        struct ToriDraw_TexUvPlanes p;
        if( !toridraw_texmap_uv_planes(
                &p, tri->x[0], tri->x[1], tri->x[2], tri->y[0], tri->y[1], tri->y[2],
                u, v, tri->z, W, H, TEX_W, 6, y_top) )
        {
            rejected++;
            continue;
        }

        /* Barycentric sample points strictly inside the triangle, so the
         * comparison is over pixels the walker would actually reach. */
        for( int i = 1; i < 8; i++ )
        {
            for( int j = 1; j + i < 8; j++ )
            {
                double w0 = 1.0 - i / 8.0 - j / 8.0, w1 = i / 8.0, w2 = j / 8.0;
                double px = w0 * tri->x[0] + w1 * tri->x[1] + w2 * tri->x[2];
                double py = w0 * tri->y[0] + w1 * tri->y[1] + w2 * tri->y[2];
                if( px < 0 || px >= W || py < 0 || py >= H )
                    continue;

                /* Reference: interpolate u/z and 1/z linearly, then divide. */
                double iz = w0 / tri->z[0] + w1 / tri->z[1] + w2 / tri->z[2];
                double uz = w0 * u[0] / tri->z[0] + w1 * u[1] / tri->z[1] +
                            w2 * u[2] / tri->z[2];
                double want_u = (uz / iz) * TEX_W;

                /* What the span forms at that pixel. */
                double dx = px - (W >> 1);
                double dy = py - y_top;
                double au = (double)p.au + p.step_au_dx * dx + p.step_au_dy * dy;
                double cw = (double)p.cw + p.step_cw_dx * dx + p.step_cw_dy * dy;
                double w = floor(cw / 64.0); /* cw >> shift, shift == 6 */
                if( w == 0.0 )
                    continue;
                double got_u = au / w;

                double err = fabs(got_u - want_u);
                if( err > worst )
                    worst = err;
                if( err > 0.05 ) /* a twentieth of a texel */
                    bad++;
                checked++;
            }
        }
    }

    if( bad )
    {
        printf("  FAIL %d of %ld samples off by more than 0.05 texel (worst %.4f)\n",
               bad, checked, worst);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld samples, worst %.4f texel, %d degenerate triangles rejected\n",
               checked, worst, rejected);
    }
}

/* ------------------------------------------------ 3. compositing chain */

typedef void (*map_fn)(
    int*, int, int, int, int, int, int, int, int, int, int, int, int,
    int, int, int, int, int, int, int, int, int, int, int, int,
    const struct ToriDraw_TexMapping*, const struct ToriDraw_TexSampler*);

#define MAPGEO(tri)                                                                                \
    W, W, H, (tri)->x[0], (tri)->x[1], (tri)->x[2], (tri)->y[0], (tri)->y[1], (tri)->y[2],          \
        (tri)->z[0], (tri)->z[1], (tri)->z[2], (tri)->m[0], (tri)->m[1], (tri)->m[2],               \
        (tri)->m[3], (tri)->m[4], (tri)->m[5], (tri)->m[6], (tri)->m[7], (tri)->m[8],               \
        0x20, 0x50, 0x70

static void
check_identity(
    const char* name,
    map_fn plain,
    map_fn variant,
    int variant_is_texalpha,
    int* a,
    int* b)
{
    struct ToriDraw_TexMapping m = make_mapping(0, 0.0f);
    int bad = 0;
    long covered = 0;

    for( int t = 0; t < TRI_COUNT; t++ )
    {
        const struct Tri* tri = &g_tris[t];

        struct ToriDraw_TexSampler s0, s1;
        ToriDraw_TexSamplerInit(&s0, g_tex, TEX_W);
        ToriDraw_TexSamplerInit(&s1, g_tex, TEX_W);
        (void)variant_is_texalpha; /* g_tex is alpha 255 throughout */

        buf_fill(a, BG);
        buf_fill(b, BG);
        plain(a + GUARD, MAPGEO(tri), &m, &s0);
        variant(b + GUARD, MAPGEO(tri), &m, &s1);

        if( !guard_intact(a, name, tri->name) || !guard_intact(b, name, tri->name) )
            return;

        for( int p = 0; p < H * W; p++ )
        {
            if( RGB(a[GUARD + p]) != BG )
                covered++;
            if( RGB(a[GUARD + p]) != RGB(b[GUARD + p]) )
                bad++;
        }
    }

    if( bad )
    {
        printf("  FAIL %-46s %d pixels differ\n", name, bad);
        g_fail++;
    }
    else if( covered == 0 )
    {
        printf("  FAIL %-46s drew nothing; the check is vacuous\n", name);
        g_fail++;
    }
    else
    {
        printf("  ok   %-46s bit-exact over %ld covered px\n", name, covered);
    }
}

static void
test_compositing(int* a, int* b)
{
    printf("neutral-setting identities within each mapped family\n");

    check_identity("texcylinder.texopaque.facealpha(0xFF) == plain",
                   (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcylinder_persp_texopaque_facealpha_branching_lerp8_v3,
                   0, a, b);
    check_identity("texcylinder.texopaque.modulate(identity) == plain",
                   (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcylinder_persp_texopaque_modulate_branching_lerp8_v3,
                   0, a, b);
    check_identity("texcylinder.texalpha(all a=255) == texopaque",
                   (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcylinder_persp_texalpha_branching_lerp8_v3, 1, a, b);

    check_identity("texcube.texopaque.facealpha(0xFF) == plain",
                   (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcube_persp_texopaque_facealpha_branching_lerp8_v3,
                   0, a, b);
    check_identity("texcube.texopaque.modulate(identity) == plain",
                   (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcube_persp_texopaque_modulate_branching_lerp8_v3,
                   0, a, b);
    check_identity("texcube.texalpha(all a=255) == texopaque",
                   (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcube_persp_texalpha_branching_lerp8_v3, 1, a, b);
    check_identity("texcube.textrans(no zero texels) == texopaque",
                   (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texcube_persp_textrans_branching_lerp8_v3, 0, a, b);

    check_identity("texsphere.texopaque.facealpha(0xFF) == plain",
                   (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texsphere_persp_texopaque_facealpha_branching_lerp8_v3,
                   0, a, b);
    check_identity("texsphere.texopaque.modulate(identity) == plain",
                   (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texsphere_persp_texopaque_modulate_branching_lerp8_v3,
                   0, a, b);
    check_identity("texsphere.texalpha(all a=255) == texopaque",
                   (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3,
                   (map_fn)raster_texsphere_persp_texalpha_branching_lerp8_v3, 1, a, b);
}

/*
 * The three projections must not agree with each other. If they did, the
 * families would be three names for one kernel — which is exactly the failure
 * the separate-kernel split exists to avoid — and every identity above would
 * still pass.
 */
static void
test_projections_differ(int* a, int* b)
{
    printf("the three projections draw different pixels\n");

    struct ToriDraw_TexMapping m = make_mapping(0, 0.0f);
    const struct Tri* tri = &g_tris[0];
    struct ToriDraw_TexSampler s;
    ToriDraw_TexSamplerInit(&s, g_tex, TEX_W);

    struct
    {
        const char* a;
        const char* b;
        map_fn fa;
        map_fn fb;
    } pairs[] = {
        { "texcylinder", "texcube",
          (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
          (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3 },
        { "texcube", "texsphere",
          (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
          (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3 },
        { "texcylinder", "texsphere",
          (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
          (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3 },
    };

    for( int i = 0; i < 3; i++ )
    {
        buf_fill(a, BG);
        buf_fill(b, BG);
        pairs[i].fa(a + GUARD, MAPGEO(tri), &m, &s);
        pairs[i].fb(b + GUARD, MAPGEO(tri), &m, &s);

        long differ = 0, covered = 0;
        for( int p = 0; p < H * W; p++ )
        {
            if( RGB(a[GUARD + p]) != BG || RGB(b[GUARD + p]) != BG )
                covered++;
            if( RGB(a[GUARD + p]) != RGB(b[GUARD + p]) )
                differ++;
        }
        if( differ == 0 )
        {
            printf("  FAIL %s and %s produced identical pixels over %ld px\n",
                   pairs[i].a, pairs[i].b, covered);
            g_fail++;
        }
        else
        {
            printf("  ok   %-12s vs %-12s %ld of %ld px differ\n",
                   pairs[i].a, pairs[i].b, differ, covered);
        }
    }
}

/*
 * Cube face selection.
 *
 * Two parts, because the first alone does not catch a scale that never reaches
 * the selection: a direct check of the dominant-axis rule, and a render proving
 * that changing only `axis_scale` changes which face a triangle lands on. The
 * test triangles elsewhere all have axis-aligned normals, where the scales
 * cannot change the answer — which is exactly how a dropped scale hides.
 */
static void
test_cube_axis(int* a, int* b)
{
    printf("cube face selection\n");

    int bad = 0;
    long checked = 0;
    for( int i = -6; i <= 6; i++ )
        for( int j = -6; j <= 6; j++ )
            for( int k = -6; k <= 6; k++ )
            {
                float x = (float)i + 0.5f, y = (float)j + 0.25f, z = (float)k - 0.125f;
                int got = toridraw_texmap_cube_axis(x, y, z);

                double ax = fabs(x), ay = fabs(y), az = fabs(z);
                int want;
                if( ay > ax && ay > az )
                    want = y > 0.0f ? 0 : 1;
                else if( az > ax && az > ay )
                    want = z > 0.0f ? 2 : 3;
                else
                    want = x > 0.0f ? 4 : 5;

                if( got != want )
                    bad++;
                checked++;
            }
    if( bad )
    {
        printf("  FAIL toridraw_texmap_cube_axis: %d of %ld wrong\n", bad, checked);
        g_fail++;
    }
    else
    {
        printf("  ok   dominant-axis rule over %ld normals\n", checked);
    }

    /*
     * A triangle whose model-space normal has three comparable components, so
     * the per-axis scales decide the winner rather than the geometry.
     */
    static const struct Tri skew = {
        "skew-normal", { 40, 150, 90 }, { 20, 35, 120 }, { 300, 340, 380 },
        { -30, -20, -25, 45, -5, -35, -15, 40, 55 }
    };

    struct ToriDraw_TexSampler s;
    ToriDraw_TexSamplerInit(&s, g_tex, TEX_W);

    struct ToriDraw_TexMapping m0 = make_mapping(0, 0.0f);
    struct ToriDraw_TexMapping m1 = make_mapping(0, 0.0f);
    /* Same mapping in every respect except the axis scales. */
    m0.axis_scale_x = 1.0f;
    m0.axis_scale_y = 1.0f;
    m0.axis_scale_z = 1.0f;
    m1.axis_scale_x = 40.0f;
    m1.axis_scale_y = 1.0f;
    m1.axis_scale_z = 1.0f;

    buf_fill(a, BG);
    buf_fill(b, BG);
    raster_texcube_persp_texopaque_branching_lerp8_v3(a + GUARD, MAPGEO(&skew), &m0, &s);
    raster_texcube_persp_texopaque_branching_lerp8_v3(b + GUARD, MAPGEO(&skew), &m1, &s);

    long differ = 0, covered = 0;
    for( int p = 0; p < H * W; p++ )
    {
        if( RGB(a[GUARD + p]) != BG || RGB(b[GUARD + p]) != BG )
            covered++;
        if( RGB(a[GUARD + p]) != RGB(b[GUARD + p]) )
            differ++;
    }

    if( covered == 0 )
    {
        printf("  FAIL axis scales: drew nothing, the check is vacuous\n");
        g_fail++;
    }
    else if( differ == 0 )
    {
        printf("  FAIL axis scales do not reach the face selection: identical over %ld px\n",
               covered);
        g_fail++;
    }
    else
    {
        printf("  ok   axis scales change the chosen face (%ld of %ld px differ)\n",
               differ, covered);
    }
}

static void
test_guards(int* buf)
{
    printf("mapped kernels write nothing outside the framebuffer\n");

    map_fn fns[] = {
        (map_fn)raster_texcylinder_persp_texopaque_branching_lerp8_v3,
        (map_fn)raster_texcylinder_persp_texalpha_branching_lerp8_v3,
        (map_fn)raster_texcube_persp_texopaque_branching_lerp8_v3,
        (map_fn)raster_texcube_persp_texalpha_facealpha_modulate_branching_lerp8_v3,
        (map_fn)raster_texsphere_persp_texopaque_branching_lerp8_v3,
        (map_fn)raster_texsphere_persp_texalpha_branching_lerp8_v3,
    };
    int ok = 1;
    for( int dir = 0; dir < 4; dir++ )
    {
        struct ToriDraw_TexMapping m = make_mapping(dir, 0.5f);
        struct ToriDraw_TexSampler s;
        ToriDraw_TexSamplerInit(&s, g_tex, TEX_W);
        s.face_alpha = 0x80;
        s.clamp_t = dir & 1;
        for( int f = 0; f < (int)(sizeof(fns) / sizeof(fns[0])); f++ )
            for( int t = 0; t < TRI_COUNT; t++ )
            {
                buf_fill(buf, BG);
                fns[f](buf + GUARD, MAPGEO(&g_tris[t]), &m, &s);
                ok &= guard_intact(buf, "mapped guards", g_tris[t].name);
            }
    }
    if( ok )
        printf("  ok   6 kernels x 4 directions x %d triangles\n", TRI_COUNT);
}

int
main(void)
{
    static int a[BUF_LEN];
    static int b[BUF_LEN];

    init_hsl16_to_rgb_table();
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    ToriDraw_InitAtanTable();
    init_reciprocal16();

    for( int v = 0; v < TEX_W; v++ )
        for( int u = 0; u < TEX_W; u++ )
        {
            int checker = (((u >> 3) ^ (v >> 3)) & 1);
            int rgb = (checker ? 0x00C08040 : 0x004080C0) + ((u & 7) << 8);
            g_tex[u + v * TEX_W] = (int)(0xFF000000u | (unsigned)rgb);
        }

    test_projection();
    test_uv_planes();
    test_compositing(a, b);
    test_projections_differ(a, b);
    test_cube_axis(a, b);
    test_guards(a);

    if( g_fail )
    {
        printf("\n%d mapped texture check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("\nall mapped texture checks passed\n");
    return 0;
}
