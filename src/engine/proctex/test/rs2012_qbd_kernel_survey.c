/*
 * rs2012_qbd_kernel_survey — what a native software renderer would have to draw
 * to render the RS727 Queen Black Dragon *from source*, rather than the
 * backported lane copy.
 *
 * This answers one question and nothing else: which raster kernels does the
 * source model demand? It reads the RS727 cache directly and reports, per
 * model, the face populations that select a kernel:
 *
 *   - texture render type per textured face (0 simple / 1 cylinder / 2 cube /
 *     3 sphere). Anything but 0 needs generated uv, which the decoder skips
 *     today, so a kernel alone would have nothing to sample with.
 *   - face_infos shading type (gouraud / flat / hidden) and face alpha
 *   - per material: the render-side properties that decide compositing -
 *     alpha_mode, combine_mode, repeat_s/t, anim_u/v, mipmap, valid, small
 *
 * Build:  make -C src rs2012-qbd-kernel-survey
 * Run:    src/build/rs2012_qbd_kernel_survey [--cache DIR] [--model ID]...
 *
 * Defaults to the three QBD body models named in RS2012_QBD_TD.md §8.4.
 */

#include "datatypes/dat2_proctexture.h"
#include "datatypes/model.h"
#include "toridraw_texture_uv.h"
#include "graphics/shared_tables.h"
#include "dat2disk.h"
#include "revisions/revisions.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS 64
#define MAX_MATERIALS 4096

/* The QBD body models. RS2012_QBD_TD.md §8.4 records 233/233/241 textured
 * faces for these three. */
static const int g_default_models[] = { 70260, 70267, 70268 };

struct material_use
{
    bool used;
    int faces;
    int render_type_faces[4];
    int other_render_type_faces;
};

static struct material_use g_material_use[MAX_MATERIALS];

/* The uv generator lives in ToriDraw (it needs the arctangent table) and takes
 * plain arrays, so the cache model is described to it rather than passed. */
static void
uv_source_from_model(const struct RSCache_Model* m, struct ToriDraw_TextureUvSource* s)
{
    memset(s, 0, sizeof(*s));
    s->vertices_x = m->vertices_x;
    s->vertices_y = m->vertices_y;
    s->vertices_z = m->vertices_z;
    s->vertex_count = m->vertex_count;
    s->face_indices_a = m->face_indices_a;
    s->face_indices_b = m->face_indices_b;
    s->face_indices_c = m->face_indices_c;
    s->face_textures = m->face_textures;
    s->face_texture_coords = m->face_texture_coords;
    s->face_count = m->face_count;
    s->textured_face_count = m->textured_face_count;
    s->textured_p = m->textured_p_coordinate;
    s->textured_m = m->textured_m_coordinate;
    s->textured_n = m->textured_n_coordinate;
    s->render_types = m->texture_render_types;
    s->scale_x = m->texture_scale_x;
    s->scale_y = m->texture_scale_y;
    s->scale_z = m->texture_scale_z;
    s->rotation = m->texture_rotation;
    s->direction = m->texture_direction;
    s->speed = m->texture_speed;
    s->trans_u = m->texture_trans_u;
    s->trans_v = m->texture_trans_v;
}

static void
survey_model(
    struct RSCache_Dat2Disk* disk,
    int model_table,
    int model_id,
    const struct RSCache_Dat2MaterialTable* materials)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, model_table, model_id);
    if( !archive )
    {
        printf("model %d: NOT PRESENT in this cache\n", model_id);
        return;
    }

    struct RSCache_Model* m =
        RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    if( !m )
    {
        printf("model %d: decode FAILED\n", model_id);
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    int shading[8] = { 0 };
    int textured_faces = 0;
    int alpha_faces = 0;
    int alpha_hist[5] = { 0 }; /* 0, 1-63, 64-127, 128-254, 255 */
    int render_types[4] = { 0 };
    int render_type_other = 0;

    for( int f = 0; f < m->face_count; f++ )
    {
        int info = m->face_infos ? (m->face_infos[f] & 0xFF) : 0;
        shading[info & 7]++;

        int tex = m->face_textures ? m->face_textures[f] : -1;
        if( tex >= 0 )
        {
            textured_faces++;
            if( tex < MAX_MATERIALS )
            {
                g_material_use[tex].used = true;
                g_material_use[tex].faces++;
            }
        }

        int a = m->face_alphas ? (m->face_alphas[f] & 0xFF) : 0;
        if( a != 0 )
            alpha_faces++;
        if( a == 0 )
            alpha_hist[0]++;
        else if( a < 64 )
            alpha_hist[1]++;
        else if( a < 128 )
            alpha_hist[2]++;
        else if( a < 255 )
            alpha_hist[3]++;
        else
            alpha_hist[4]++;
    }

    for( int t = 0; t < m->textured_face_count; t++ )
    {
        int rt = m->texture_render_types ? (m->texture_render_types[t] & 0xFF) : 0;
        if( rt < 4 )
            render_types[rt]++;
        else
            render_type_other++;
    }

    printf("\n=== model %d ===\n", model_id);
    printf("  vertices %d  faces %d  textured faces %d  texture-coord entries %d\n",
           m->vertex_count, m->face_count, textured_faces, m->textured_face_count);
    printf("  face shading:      gouraud %d  flat %d  hidden %d  type3 %d\n",
           shading[0], shading[1], shading[2], shading[3]);
    printf("  texture render types: simple(0) %d  cylinder(1) %d  cube(2) %d  sphere(3) %d"
           "  other %d\n",
           render_types[0], render_types[1], render_types[2], render_types[3],
           render_type_other);
    printf("  face alpha:        opaque(0) %d  1-63 %d  64-127 %d  128-254 %d  255 %d"
           "   (non-zero %d)\n",
           alpha_hist[0], alpha_hist[1], alpha_hist[2], alpha_hist[3], alpha_hist[4],
           alpha_faces);

    /* Draw order is a rendering requirement too: per-face priorities mean the
     * depth-bucketed face sort has to honour them, and their absence means a
     * model this size is relying on something else to resolve overlap. */
    if( m->face_priorities )
    {
        int prio[16] = { 0 };
        for( int f = 0; f < m->face_count; f++ )
            prio[m->face_priorities[f] & 15]++;
        printf("  face priorities:   present -");
        for( int p = 0; p < 16; p++ )
            if( prio[p] )
                printf(" p%d=%d", p, prio[p]);
        printf("\n");
    }
    else
    {
        printf("  face priorities:   absent (uniform)\n");
    }

    /* The complex mapping parameters, now that the decoder reads them. Printed
     * per complex face because there are only a handful, and because "decoded
     * without asserting" is not the same as "decoded to something sane" - a
     * mis-sized scale block still consumes the right total and yields garbage. */
    if( render_types[1] || render_types[2] || render_types[3] )
    {
        printf("  complex texture mapping (type / axis p,m,n / scale x,y,z / rot / dir /"
               " speed / transUV):\n");
        for( int t = 0; t < m->textured_face_count; t++ )
        {
            int rt = m->texture_render_types ? (m->texture_render_types[t] & 0xFF) : 0;
            if( rt < 1 || rt > 3 )
                continue;
            printf("    [%3d] type=%d  axis=(%6d,%6d,%6d)  scale=(%7d,%7d,%7d)"
                   "  rot=%4d dir=%d speed=%4d trans=(%4d,%4d)\n",
                   t,
                   rt,
                   (int)(int16_t)m->textured_p_coordinate[t],
                   (int)(int16_t)m->textured_m_coordinate[t],
                   (int)(int16_t)m->textured_n_coordinate[t],
                   m->texture_scale_x ? m->texture_scale_x[t] : 0,
                   m->texture_scale_y ? m->texture_scale_y[t] : 0,
                   m->texture_scale_z ? m->texture_scale_z[t] : 0,
                   m->texture_rotation ? m->texture_rotation[t] : 0,
                   m->texture_direction ? m->texture_direction[t] : 0,
                   m->texture_speed ? m->texture_speed[t] : 0,
                   m->texture_trans_u ? m->texture_trans_u[t] : 0,
                   m->texture_trans_v ? m->texture_trans_v[t] : 0);
        }
    }

    /*
     * Generated uv, per render type. The decoder now supplies the complex
     * parameters, so this is the first point at which a cube face has a texture
     * coordinate at all. Reported as a span (max - min across the triangle)
     * because that is the number that says whether the mapping is sane: a
     * correct projection puts a face's three vertices within a fraction of a
     * tile of each other, while a mis-scaled or mis-centred one produces spans
     * of hundreds and a face that samples the whole texture per pixel.
     */
    {
        float* uv = (float*)malloc((size_t)m->face_count * 6 * sizeof(float));
        struct ToriDraw_TextureUvSource uvsrc;
            uv_source_from_model(m, &uvsrc);
            if( uv && ToriDraw_ComputeTextureUv(&uvsrc, uv) )
        {
            double span_sum[4] = { 0 };
            long span_n[4] = { 0 };
            double span_max[4] = { 0 };
            long nonfinite = 0;

            for( int f = 0; f < m->face_count; f++ )
            {
                if( !m->face_textures || m->face_textures[f] < 0 )
                    continue;
                int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
                int rt = 0;
                if( coord >= 0 && coord < m->textured_face_count && m->texture_render_types )
                    rt = m->texture_render_types[coord] & 0xFF;
                if( rt > 3 )
                    continue;

                const float* q = uv + (size_t)f * 6;
                int bad = 0;
                for( int k = 0; k < 6; k++ )
                    if( !isfinite(q[k]) )
                        bad = 1;
                if( bad )
                {
                    nonfinite++;
                    continue;
                }

                float ulo = q[0], uhi = q[0], vlo = q[1], vhi = q[1];
                for( int k = 1; k < 3; k++ )
                {
                    if( q[k * 2] < ulo )
                        ulo = q[k * 2];
                    if( q[k * 2] > uhi )
                        uhi = q[k * 2];
                    if( q[k * 2 + 1] < vlo )
                        vlo = q[k * 2 + 1];
                    if( q[k * 2 + 1] > vhi )
                        vhi = q[k * 2 + 1];
                }
                double span = (double)(uhi - ulo) + (double)(vhi - vlo);
                span_sum[rt] += span;
                span_n[rt]++;
                if( span > span_max[rt] )
                    span_max[rt] = span;
            }

            printf("  generated uv: ");
            for( int rt = 0; rt < 4; rt++ )
            {
                if( !span_n[rt] )
                    continue;
                printf("type%d n=%ld mean-span=%.4f max-span=%.4f   ",
                       rt, span_n[rt], span_sum[rt] / (double)span_n[rt], span_max[rt]);
            }
            printf("%s\n", nonfinite ? "" : "(all finite)");
            if( nonfinite )
                printf("    WARNING: %ld faces produced non-finite uv\n", nonfinite);
        }
        free(uv);
    }

    /* Attribute this model's textured faces to render types, so the material
     * report can say which materials need generated uv. */
    for( int f = 0; f < m->face_count; f++ )
    {
        int tex = m->face_textures ? m->face_textures[f] : -1;
        if( tex < 0 || tex >= MAX_MATERIALS )
            continue;
        int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
        int rt = 0;
        if( coord >= 0 && coord < m->textured_face_count && m->texture_render_types )
            rt = m->texture_render_types[coord] & 0xFF;
        if( rt < 4 )
            g_material_use[tex].render_type_faces[rt]++;
        else
            g_material_use[tex].other_render_type_faces++;
    }

    (void)materials;
    RSCache_ModelFree(m);
    RSCache_Dat2DiskArchiveFree(archive);
}

static void
report_materials(const struct RSCache_Dat2MaterialTable* materials)
{
    printf("\n=== materials referenced by the surveyed models ===\n");
    printf("%6s %7s  %-5s %-5s %-6s %-5s %-6s %-6s %-6s %-7s %s\n",
           "mat", "faces", "valid", "small", "alpha", "combi", "repeat", "animUV", "mipmap",
           "shader", "render types (simple/cyl/cube/sph)");

    int total = 0;
    int alpha_mode_hist[8] = { 0 };
    int animated = 0;
    int non_repeat = 0;
    int invalid = 0;
    int complex_uv = 0;

    for( int i = 0; i < MAX_MATERIALS; i++ )
    {
        if( !g_material_use[i].used )
            continue;
        total++;

        const struct RSCache_Dat2Material* mat =
            (materials && i < materials->count) ? &materials->materials[i] : NULL;

        if( mat )
        {
            if( mat->alpha_mode < 8 )
                alpha_mode_hist[mat->alpha_mode]++;
            if( mat->anim_u || mat->anim_v )
                animated++;
            if( !mat->repeat_s || !mat->repeat_t )
                non_repeat++;
            if( !mat->valid )
                invalid++;
        }

        const struct material_use* u = &g_material_use[i];
        if( u->render_type_faces[1] || u->render_type_faces[2] || u->render_type_faces[3] ||
            u->other_render_type_faces )
            complex_uv++;

        printf("%6d %7d  %-5s %-5s %-6d %-5d %-6s %3d/%-2d %-6d %-7d %d/%d/%d/%d\n",
               i,
               u->faces,
               mat ? (mat->valid ? "yes" : "no") : "?",
               mat ? (mat->small ? "yes" : "no") : "?",
               mat ? mat->alpha_mode : -1,
               mat ? mat->combine_mode : -1,
               mat ? ((mat->repeat_s && mat->repeat_t) ? "both"
                                                       : (mat->repeat_s ? "s" : (mat->repeat_t ? "t" : "none")))
                   : "?",
               mat ? mat->anim_u : 0,
               mat ? mat->anim_v : 0,
               mat ? mat->mipmap : -1,
               mat ? mat->shader_id : -1,
               u->render_type_faces[0],
               u->render_type_faces[1],
               u->render_type_faces[2],
               u->render_type_faces[3]);
    }

    printf("\n  %d distinct materials\n", total);
    printf("  alpha_mode: 0(opaque)=%d  1(cutout)=%d  2(blend)=%d  3+=%d\n",
           alpha_mode_hist[0], alpha_mode_hist[1], alpha_mode_hist[2],
           alpha_mode_hist[3] + alpha_mode_hist[4] + alpha_mode_hist[5] +
               alpha_mode_hist[6] + alpha_mode_hist[7]);
    printf("  animated (anim_u|anim_v non-zero): %d\n", animated);
    printf("  not repeat-wrapped on both axes:   %d\n", non_repeat);
    printf("  valid == false (isGroundMesh):     %d\n", invalid);
    printf("  materials with non-simple uv faces: %d\n", complex_uv);
}

/*
 * Decode every model in the cache and report what the complex-texture sections
 * contained. This is the broad check on the decoder change: the per-section
 * cursor asserts in decode_ob3 fire on any mis-sized block, so a clean sweep
 * over tens of thousands of models is a much stronger statement than three
 * hand-picked ones — and it is the only way to reach render types 1 and 3,
 * which the QBD itself does not use.
 */
static int
sweep_models(struct RSCache_Dat2Disk* disk, int model_table, int limit)
{
    int decoded = 0, absent = 0, failed = 0;
    int with_complex = 0;
    int type_faces[4] = { 0 };
    int params_present = 0;
    int nonzero_rotation = 0, nonzero_direction = 0, nonzero_speed = 0, nonzero_trans = 0;
    long uv_faces[4] = { 0 }, uv_nonfinite[4] = { 0 };
    double uv_span_sum[4] = { 0 }, uv_span_max[4] = { 0 };
    int scale_min = 0x7FFFFFFF, scale_max = -0x7FFFFFFF;
    long scale_high_half = 0, scale_near_top = 0, scale_total = 0;

    for( int id = 0; id < limit; id++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, model_table, id);
        if( !archive )
        {
            absent++;
            continue;
        }

        struct RSCache_Model* m =
            RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
        if( !m )
        {
            failed++;
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        decoded++;

        int complex_here = 0;
        for( int t = 0; t < m->textured_face_count; t++ )
        {
            int rt = m->texture_render_types ? (m->texture_render_types[t] & 0xFF) : 0;
            if( rt < 1 || rt > 3 )
                continue;
            type_faces[rt]++;
            complex_here++;

            if( m->texture_scale_x )
            {
                params_present++;
                scale_total += 3;
                int s[3] = { m->texture_scale_x[t], m->texture_scale_y[t],
                             m->texture_scale_z[t] };
                for( int k = 0; k < 3; k++ )
                {
                    if( s[k] < scale_min )
                        scale_min = s[k];
                    if( s[k] > scale_max )
                        scale_max = s[k];
                    /* Distribution across the sign bit of a hypothetical signed
                     * 24-bit field. The reference's readMedium is unsigned (three
                     * bytes OR'd, no sign extension) and this decoder matches it,
                     * but calculateTextureScales tests `scaleX <= 0`, which only
                     * means anything if the field can go negative. If the high
                     * half is a thin band hugging 0xFFFFFF it is small negatives
                     * and the reference is lossy; if it is spread, unsigned is
                     * right. Measured, not assumed. */
                    if( (unsigned)s[k] >= 0x800000u )
                    {
                        scale_high_half++;
                        if( (unsigned)s[k] >= 0xFFF000u )
                            scale_near_top++;
                    }
                }
                if( m->texture_rotation[t] )
                    nonzero_rotation++;
                if( m->texture_direction[t] )
                    nonzero_direction++;
                if( m->texture_speed[t] )
                    nonzero_speed++;
                if( m->texture_trans_u[t] || m->texture_trans_v[t] )
                    nonzero_trans++;
            }
        }
        if( complex_here )
            with_complex++;

        /* Generated uv over every model in the cache. Non-finite uv is the
         * failure mode that matters: a NaN from a degenerate basis poisons a
         * whole triangle rather than one texel, and it is invisible until
         * something rasterizes it. */
        {
            float* uv = (float*)malloc((size_t)m->face_count * 6 * sizeof(float));
            struct ToriDraw_TextureUvSource uvsrc;
            uv_source_from_model(m, &uvsrc);
            if( uv && ToriDraw_ComputeTextureUv(&uvsrc, uv) )
            {
                for( int f = 0; f < m->face_count; f++ )
                {
                    if( !m->face_textures || m->face_textures[f] < 0 )
                        continue;
                    int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
                    int rt = 0;
                    if( coord >= 0 && coord < m->textured_face_count &&
                        m->texture_render_types )
                        rt = m->texture_render_types[coord] & 0xFF;
                    if( rt > 3 )
                        continue;
                    const float* q = uv + (size_t)f * 6;
                    int bad = 0;
                    for( int k = 0; k < 6; k++ )
                        if( !isfinite(q[k]) )
                            bad = 1;
                    uv_faces[rt]++;
                    if( bad )
                    {
                        uv_nonfinite[rt]++;
                        continue;
                    }
                    float ulo = q[0], uhi = q[0], vlo = q[1], vhi = q[1];
                    for( int k = 1; k < 3; k++ )
                    {
                        if( q[k*2] < ulo ) ulo = q[k*2];
                        if( q[k*2] > uhi ) uhi = q[k*2];
                        if( q[k*2+1] < vlo ) vlo = q[k*2+1];
                        if( q[k*2+1] > vhi ) vhi = q[k*2+1];
                    }
                    double span = (double)(uhi-ulo) + (double)(vhi-vlo);
                    uv_span_sum[rt] += span;
                    if( span > uv_span_max[rt] ) uv_span_max[rt] = span;
                }
            }
            free(uv);
        }

        RSCache_ModelFree(m);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    printf("\n=== model sweep (ids 0..%d) ===\n", limit - 1);
    printf("  decoded %d, absent %d, decode failures %d\n", decoded, absent, failed);
    printf("  models with complex texture faces: %d\n", with_complex);
    printf("  complex faces: cylinder(1) %d  cube(2) %d  sphere(3) %d\n",
           type_faces[1], type_faces[2], type_faces[3]);
    printf("  complex faces with parameters decoded: %d\n", params_present);
    if( params_present )
    {
        printf("  scale range [%d, %d] over %ld samples\n", scale_min, scale_max, scale_total);
        printf("  scale >= 0x800000: %ld (%.3f%%)   of which >= 0xFFF000: %ld\n",
               scale_high_half, 100.0 * (double)scale_high_half / (double)(scale_total ? scale_total : 1),
               scale_near_top);
        printf("  non-zero: rotation %d  direction %d  speed %d  cube-translation %d\n",
               nonzero_rotation, nonzero_direction, nonzero_speed, nonzero_trans);
    }
    printf("  generated uv:\n");
    long total_nonfinite = 0;
    for( int rt = 0; rt < 4; rt++ )
    {
        if( !uv_faces[rt] )
            continue;
        total_nonfinite += uv_nonfinite[rt];
        printf("    type%d  faces %-9ld mean-span %8.4f  max-span %10.4f  non-finite %ld\n",
               rt, uv_faces[rt],
               uv_span_sum[rt] / (double)(uv_faces[rt] - uv_nonfinite[rt] ?
                                          uv_faces[rt] - uv_nonfinite[rt] : 1),
               uv_span_max[rt], uv_nonfinite[rt]);
    }

    return (failed == 0 && total_nonfinite == 0) ? 0 : 1;
}

int
main(int argc, char** argv)
{
    const char* cache_path = getenv("RS2012_SRC_CACHE");
    if( !cache_path )
        cache_path = "cache.rs727_preeoc";

    int models[MAX_MODELS];
    int model_count = 0;
    int sweep_limit = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_path = argv[++i];
        else if( strcmp(argv[i], "--sweep") == 0 && i + 1 < argc )
            sweep_limit = atoi(argv[++i]);
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
        {
            if( model_count < MAX_MODELS )
                models[model_count++] = atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr,
                    "usage: %s [--cache DIR] [--model ID]... [--sweep MAX_ID]\n",
                    argv[0]);
            return 2;
        }
    }

    if( model_count == 0 )
    {
        for( int i = 0; i < (int)(sizeof(g_default_models) / sizeof(g_default_models[0])); i++ )
            models[model_count++] = g_default_models[i];
    }

    /* The generator's angles come from the arctangent table. */
    ToriDraw_InitAtanTable();

    struct RSCache profile;
    if( !RSCache_ProfileByName("rs727", &profile) )
    {
        fprintf(stderr, "rs2012_qbd_kernel_survey: rs727 profile is unavailable\n");
        return 1;
    }

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_path);
    if( !disk )
    {
        fprintf(stderr,
                "rs2012_qbd_kernel_survey: cannot open source cache %s\n"
                "  (it is ~461MB, gitignored, and must be obtained out of band;\n"
                "   point RS2012_SRC_CACHE at it if it is not at the repo root)\n",
                cache_path);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    int model_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MODELS);
    int material_table_id = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MATERIALS);
    if( model_table < 0 || material_table_id < 0 )
    {
        fprintf(stderr, "rs2012_qbd_kernel_survey: source cache lacks a required table\n");
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    uint32_t flags = RSCache_Dat2ProcTextureFlags(&profile);
    struct RSCache_Dat2MaterialTable* materials = NULL;
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, material_table_id, 0);
        if( archive )
        {
            materials = RSCache_Dat2MaterialTableNewDecode(
                archive->data, archive->data_size, flags);
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }
    if( !materials )
        fprintf(stderr, "  warning: material table did not decode; property columns will be '?'\n");
    else
        printf("material table: %d rows, extended block %s\n",
               materials->count, materials->has_extended ? "present" : "absent");

    int rc = 0;
    if( sweep_limit > 0 )
    {
        rc = sweep_models(disk, model_table, sweep_limit);
        RSCache_Dat2MaterialTableFree(materials);
        RSCache_Dat2DiskFree(disk);
        return rc;
    }

    for( int i = 0; i < model_count; i++ )
        survey_model(disk, model_table, models[i], materials);

    report_materials(materials);

    RSCache_Dat2MaterialTableFree(materials);
    RSCache_Dat2DiskFree(disk);
    return 0;
}
