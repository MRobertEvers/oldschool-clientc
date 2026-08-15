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
#include "dat2disk.h"
#include "revisions/revisions.h"

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

int
main(int argc, char** argv)
{
    const char* cache_path = getenv("RS2012_SRC_CACHE");
    if( !cache_path )
        cache_path = "cache.rs727_preeoc";

    int models[MAX_MODELS];
    int model_count = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_path = argv[++i];
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
        {
            if( model_count < MAX_MODELS )
                models[model_count++] = atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: %s [--cache DIR] [--model ID]...\n", argv[0]);
            return 2;
        }
    }

    if( model_count == 0 )
    {
        for( int i = 0; i < (int)(sizeof(g_default_models) / sizeof(g_default_models[0])); i++ )
            models[model_count++] = g_default_models[i];
    }

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

    for( int i = 0; i < model_count; i++ )
        survey_model(disk, model_table, models[i], materials);

    report_materials(materials);

    RSCache_Dat2MaterialTableFree(materials);
    RSCache_Dat2DiskFree(disk);
    return 0;
}
