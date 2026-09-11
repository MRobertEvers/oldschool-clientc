/*
 * ev_uv_span — the uv span a face gets, measured through the KERNEL's own path.
 *
 * ## Why this is not the survey
 *
 * rs2012_qbd_kernel_survey already reports uv spans, and reports them as sane.
 * It computes them with `ToriDraw_ComputeTextureUv`, the direct generator. The
 * kernels do not use that: they use `ToriDraw_TexMapping`, a matrix form built
 * by `ToriDraw_ModelBuildTextureMappings`, and apply it with
 * `toridraw_texmap_project_*`. Two implementations of one projection, and
 * nothing had ever compared them — so a divergence in the matrix form was
 * invisible behind a survey that said everything was fine.
 *
 * A span here is in texture tiles across one triangle. Under about 1 means the
 * face samples a patch of the texture. Much above that means the texture
 * repeats across a single triangle, which on a rock surface reads as contour
 * banding — the striping on the rs643/void634 models.
 *
 *   make -C tools/entity_viewer ev_uv_span
 *   tools/entity_viewer/ev_uv_span cache.void634 rs643 2745
 */

#include "ev_build.h"
#include "ev_textures.h"

#include "asset_access.h"
#include "tool_profile.h"
#include "toridraw.h"
#include "toridraw_render_hd.h"
#include "graphics/raster/texture/texmap_common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct EV_TextureSet g_set;

static int
have_texture(int id, void* user)
{
    (void)user;
    return ev_textures_get(&g_set, id) != NULL;
}

struct tally
{
    int n;
    double sum;
    double max;
    int over_2;
    int over_8;
};

static void
add(struct tally* t, double span)
{
    t->n++;
    t->sum += span;
    if( span > t->max )
        t->max = span;
    if( span > 2.0 )
        t->over_2++;
    if( span > 8.0 )
        t->over_8++;
}

static void
report(const char* label, const struct tally* t)
{
    if( !t->n )
        return;
    printf("  %-10s n=%-6d mean=%8.3f max=%10.3f  >2 tiles=%5.1f%%  >8 tiles=%5.1f%%\n",
           label, t->n, t->sum / t->n, t->max,
           100.0 * t->over_2 / t->n, 100.0 * t->over_8 / t->n);
}

int
main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "cache.void634";
    const char* rev = argc > 2 ? argv[2] : "rs643";
    int npc_id = argc > 3 ? atoi(argv[3]) : 2745;

    ToriDraw_Init();

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 1;
    if( !tool_dat2_open(dir, &profile, &cache) )
        return 1;
    ev_textures_load(&cache, &g_set);
    ev_build_set_texture_available(have_texture, NULL);

    struct ToriDraw_ModelHD* hd = ev_build_npc_model_hd(&cache, npc_id);
    if( !hd )
    {
        printf("npc %d has no mapped faces (nothing to measure)\n", npc_id);
        return 0;
    }
    struct ToriDraw_Model* m = &hd->base;

    struct tally cyl = { 0 }, cube = { 0 }, sph = { 0 };

    /*
     * Which cube face each triangle projects onto, and how grazing that choice
     * is. The axis is picked from the dominant component of the scaled normal;
     * a face whose normal is nearly perpendicular to its chosen axis samples the
     * texture edge-on and smears it along one direction, which reads as
     * streaking. `grazing` counts those.
     */
    int axis_hist[6] = { 0 };
    int grazing = 0;
    int seam_span = 0;
    int seam_after = 0;

    for( int f = 0; f < m->face_count; f++ )
    {
        int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
        if( coord < 0 || coord >= m->textured_face_count )
            continue;
        int type = m->texture_render_types ? (m->texture_render_types[coord] & 0xFF) : 0;
        if( type < 1 || type > 3 || !hd->texture_mappings )
            continue;

        const struct ToriDraw_TexMapping* map = &hd->texture_mappings[coord];
        int idx[3] = { m->face_indices_a[f], m->face_indices_b[f], m->face_indices_c[f] };
        float u[3], v[3];

        /* The cube axis is chosen per triangle from its scaled normal, exactly
         * as the kernel does — a fixed axis would measure a projection no face
         * actually uses. */
        int axis = 0;
        if( type == 2 )
        {
            float ax = (float)(m->vertices_x[idx[1]] - m->vertices_x[idx[0]]);
            float ay = (float)(m->vertices_y[idx[1]] - m->vertices_y[idx[0]]);
            float az = (float)(m->vertices_z[idx[1]] - m->vertices_z[idx[0]]);
            float bx = (float)(m->vertices_x[idx[2]] - m->vertices_x[idx[0]]);
            float by = (float)(m->vertices_y[idx[2]] - m->vertices_y[idx[0]]);
            float bz = (float)(m->vertices_z[idx[2]] - m->vertices_z[idx[0]]);
            float nx = ay * bz - az * by;
            float ny = az * bx - ax * bz;
            float nz = ax * by - ay * bx;
            axis = toridraw_texmap_cube_axis(nx * map->axis_scale_x, ny * map->axis_scale_y,
                                             nz * map->axis_scale_z);
            if( axis >= 0 && axis < 6 )
                axis_hist[axis]++;

            /* How dominant the winning component actually was. Near 1 means the
             * face points squarely at its cube plane; near 1/sqrt(3) means it
             * was a coin toss between axes. */
            float sx2 = nx * map->axis_scale_x, sy2 = ny * map->axis_scale_y,
                  sz2 = nz * map->axis_scale_z;
            float len = sqrtf(sx2 * sx2 + sy2 * sy2 + sz2 * sz2);
            float dom = (axis < 2) ? fabsf(sy2) : ((axis < 4) ? fabsf(sz2) : fabsf(sx2));
            if( len > 0 && dom / len < 0.60f )
                grazing++;
        }

        for( int k = 0; k < 3; k++ )
        {
            int vx = m->vertices_x[idx[k]];
            int vy = m->vertices_y[idx[k]];
            int vz = m->vertices_z[idx[k]];
            if( type == 1 )
                toridraw_texmap_project_cylinder(map, vx, vy, vz, &u[k], &v[k]);
            else if( type == 2 )
                toridraw_texmap_project_cube(map, axis, vx, vy, vz, &u[k], &v[k]);
            else
                toridraw_texmap_project_sphere(map, vx, vy, vz, &u[k], &v[k]);
        }

        double umin = u[0], umax = u[0], vmin = v[0], vmax = v[0];
        for( int k = 1; k < 3; k++ )
        {
            if( u[k] < umin ) umin = u[k];
            if( u[k] > umax ) umax = u[k];
            if( v[k] < vmin ) vmin = v[k];
            if( v[k] > vmax ) vmax = v[k];
        }
        double span = (umax - umin) > (vmax - vmin) ? (umax - umin) : (vmax - vmin);
        if( !isfinite(span) )
            span = 1e9;

        /*
         * A face whose coordinates straddle the atan2 branch cut, counted
         * before and after the unwrap. `after` must be 0: anything left is a
         * face the rasteriser will still interpolate the long way round, and
         * that is the streaking.
         */
        if( type == 1 || type == 3 )
        {
            float wrap = (type == 1 && map->scale_z > 0) ? map->scale_z : 1.0f;
            float half = wrap / 2.0f;
            float* ax = (map->direction & 1) ? v : u;

            if( fabsf(ax[1] - ax[0]) > half || fabsf(ax[2] - ax[0]) > half )
                seam_span++;

            toridraw_texmap_unwrap_seam(map->direction, wrap, u, v);

            ax = (map->direction & 1) ? v : u;
            if( fabsf(ax[1] - ax[0]) > half || fabsf(ax[2] - ax[0]) > half )
                seam_after++;
        }

        if( type == 1 )
            add(&cyl, span);
        else if( type == 2 )
            add(&cube, span);
        else
            add(&sph, span);
    }

    /* per-texture render type: which projection each texture's faces use.
     * "The purple faces streak" is only actionable once the projection those
     * faces actually take is known — a seam fix does nothing for a cube. */
    {
        int ids[32], n_ids = 0;
        for( int f = 0; f < m->face_count; f++ )
        {
            int t = m->face_textures ? m->face_textures[f] : -1;
            int dup = 0;
            if( t < 0 ) continue;
            for( int i = 0; i < n_ids; i++ ) if( ids[i] == t ) dup = 1;
            if( !dup && n_ids < 32 ) ids[n_ids++] = t;
        }
        printf("  per-texture render type (0 plane, 1 cyl, 2 cube, 3 sphere):\n");
        for( int i = 0; i < n_ids; i++ )
        {
            int rt[4] = { 0, 0, 0, 0 };
            for( int f = 0; f < m->face_count; f++ )
            {
                if( (m->face_textures ? m->face_textures[f] : -1) != ids[i] ) continue;
                int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
                int ty = 0;
                if( coord >= 0 && coord < m->textured_face_count && m->texture_render_types )
                    ty = m->texture_render_types[coord] & 0xFF;
                if( ty >= 0 && ty < 4 ) rt[ty]++;
            }
            printf("    tex %-5d plane=%-5d cyl=%-5d cube=%-5d sphere=%d\n",
                   ids[i], rt[0], rt[1], rt[2], rt[3]);
        }
    }

    /*
     * u and v span SEPARATELY, per texture.
     *
     * A max-of-the-two span hides the failure that matters here: if one axis
     * spans a normal fraction of a tile and the other spans almost nothing, the
     * texture is stretched along a line rather than mapped onto the face. That
     * is streaking, and the aggregate span looks perfectly healthy while it
     * happens. `anisotropy` is the ratio of the two.
     */
    {
        int ids[32], n_ids = 0;
        for( int f = 0; f < m->face_count; f++ )
        {
            int t = m->face_textures ? m->face_textures[f] : -1;
            int dup = 0;
            if( t < 0 ) continue;
            for( int i = 0; i < n_ids; i++ ) if( ids[i] == t ) dup = 1;
            if( !dup && n_ids < 32 ) ids[n_ids++] = t;
        }
        printf("  per-texture u/v span (anisotropy = larger/smaller):\n");
        for( int i = 0; i < n_ids; i++ )
        {
            double su = 0, sv = 0, worst = 0;
            int n = 0, bad = 0;
            /* Absolute uv range, not just the per-face span. Clamping only bites
             * where the coordinate leaves 0..1, and a healthy span says nothing
             * about where that span sits. */
            double gmin = 1e9, gmax = -1e9;
            int outside = 0;
            for( int f = 0; f < m->face_count; f++ )
            {
                if( (m->face_textures ? m->face_textures[f] : -1) != ids[i] ) continue;
                int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
                if( coord < 0 || coord >= m->textured_face_count )
                    continue;
                int ty = m->texture_render_types ? (m->texture_render_types[coord] & 0xFF) : 0;
                if( ty > 3 ) continue;
                if( ty >= 1 && !hd->texture_mappings ) continue;
                int idx[3] = { m->face_indices_a[f], m->face_indices_b[f], m->face_indices_c[f] };
                float uu[3], vv[3];
                if( ty == 0 )
                {
                    /* Type 0 through the same solve the texpmn kernel runs — the
                     * P/M/N frame projected along its normal, not the eye-ray
                     * plane walk. See toridraw_texmap_project_plane. */
                    int tp = m->textured_p_coordinate[coord], tm = m->textured_m_coordinate[coord],
                        tn = m->textured_n_coordinate[coord];
                    if( tp < 0 || tm < 0 || tn < 0 || tp >= m->vertex_count ||
                        tm >= m->vertex_count || tn >= m->vertex_count )
                        continue;
                    struct ToriDraw_TexPlaneFrame frame = {
                        m->vertices_x[tp], m->vertices_y[tp], m->vertices_z[tp],
                        m->vertices_x[tm], m->vertices_y[tm], m->vertices_z[tm],
                        m->vertices_x[tn], m->vertices_y[tn], m->vertices_z[tn],
                    };
                    toridraw_texmap_project_plane(
                        &frame,
                        m->vertices_x[idx[0]], m->vertices_y[idx[0]], m->vertices_z[idx[0]],
                        m->vertices_x[idx[1]], m->vertices_y[idx[1]], m->vertices_z[idx[1]],
                        m->vertices_x[idx[2]], m->vertices_y[idx[2]], m->vertices_z[idx[2]],
                        uu, vv);
                    goto have_uv;
                }
                const struct ToriDraw_TexMapping* mp = &hd->texture_mappings[coord];
                int axis = 0;
                if( ty == 2 )
                {
                    float ax = (float)(m->vertices_x[idx[1]] - m->vertices_x[idx[0]]);
                    float ay = (float)(m->vertices_y[idx[1]] - m->vertices_y[idx[0]]);
                    float az = (float)(m->vertices_z[idx[1]] - m->vertices_z[idx[0]]);
                    float bx = (float)(m->vertices_x[idx[2]] - m->vertices_x[idx[0]]);
                    float by = (float)(m->vertices_y[idx[2]] - m->vertices_y[idx[0]]);
                    float bz = (float)(m->vertices_z[idx[2]] - m->vertices_z[idx[0]]);
                    axis = toridraw_texmap_cube_axis(
                        (ay * bz - az * by) * mp->axis_scale_x,
                        (az * bx - ax * bz) * mp->axis_scale_y,
                        (ax * by - ay * bx) * mp->axis_scale_z);
                }
                for( int k = 0; k < 3; k++ )
                {
                    int vx = m->vertices_x[idx[k]], vy = m->vertices_y[idx[k]],
                        vz = m->vertices_z[idx[k]];
                    if( ty == 1 ) toridraw_texmap_project_cylinder(mp, vx, vy, vz, &uu[k], &vv[k]);
                    else if( ty == 2 ) toridraw_texmap_project_cube(mp, axis, vx, vy, vz, &uu[k], &vv[k]);
                    else toridraw_texmap_project_sphere(mp, vx, vy, vz, &uu[k], &vv[k]);
                }
            have_uv:;
                double ur = fmaxf(fmaxf(uu[0],uu[1]),uu[2]) - fminf(fminf(uu[0],uu[1]),uu[2]);
                double vr = fmaxf(fmaxf(vv[0],vv[1]),vv[2]) - fminf(fminf(vv[0],vv[1]),vv[2]);
                double hi = ur > vr ? ur : vr, lo = ur > vr ? vr : ur;
                double aniso = lo > 1e-6 ? hi / lo : 1e9;
                for( int k = 0; k < 3; k++ )
                {
                    if( uu[k] < gmin ) gmin = uu[k];
                    if( uu[k] > gmax ) gmax = uu[k];
                    if( vv[k] < gmin ) gmin = vv[k];
                    if( vv[k] > gmax ) gmax = vv[k];
                }
                if( uu[0] < 0 || uu[0] > 1 || uu[1] < 0 || uu[1] > 1 || uu[2] < 0 || uu[2] > 1 ||
                    vv[0] < 0 || vv[0] > 1 || vv[1] < 0 || vv[1] > 1 || vv[2] < 0 || vv[2] > 1 )
                    outside++;
                su += ur; sv += vr; n++;
                if( aniso > worst && aniso < 1e8 ) worst = aniso;
                if( aniso > 20.0 ) bad++;
            }
            if( n )
                printf("    tex %-5d n=%-5d mean u=%.4f v=%.4f  worst aniso=%.1f  "
                       "aniso>20: %d (%.1f%%)  uv range [%.2f..%.2f] outside 0..1: %d (%.1f%%)\n",
                       ids[i], n, su / n, sv / n, worst, bad, 100.0 * bad / n,
                       gmin, gmax, outside, 100.0 * outside / n);
        }
    }

    printf("%s npc %d, through the kernel's own projection:\n", dir, npc_id);
    printf("  seam-straddling faces: %d before unwrap, %d after (after must be 0)\n",
           seam_span, seam_after);
    report("cylinder", &cyl);
    report("cube", &cube);
    report("sphere", &sph);
    if( cube.n )
    {
        printf("  cube axes:");
        for( int i = 0; i < 6; i++ )
            printf(" %d=%d", i, axis_hist[i]);
        printf("   grazing (<0.60 dominance) = %d of %d (%.1f%%)\n",
               grazing, cube.n, 100.0 * grazing / cube.n);
    }

    /* The stored scale_z is the cylinder's post-atan2 u multiplier. It is also
     * folded into the basis matrix by the generator, so a value far from 1 is
     * the double application to look at. */
    int shown = 0;
    printf("  cylinder scale_z (post-atan2 u multiplier), first few:");
    for( int i = 0; i < m->textured_face_count && shown < 8; i++ )
    {
        int type = m->texture_render_types ? (m->texture_render_types[i] & 0xFF) : 0;
        if( type != 1 )
            continue;
        printf(" %.2f", hd->texture_mappings[i].scale_z);
        shown++;
    }
    printf("\n");

    ToriDraw_ModelHDFree(hd);
    ev_textures_free(&g_set);
    tool_dat2_close(&cache);
    return 0;
}
