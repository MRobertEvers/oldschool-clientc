/*
 * Port an imported RS2012 model to OB_TORI, choosing a span kernel PER FACE.
 *
 * The lane's materials already carry flags saying how they should be drawn
 * (alpha / modulate / detail — see docs/HD_KERNELS.md), but those are
 * properties of the TEXTURE and a texture is shared. The same mask is used on
 * this lane both as a cutout card, where the empty region has to show the scene
 * behind it, and as a decal lying on a solid surface, where it must not. 13 of
 * the 39 masks are used both ways, so no per-material flag can be right for all
 * of their faces.
 *
 * Which one a face is, is a question about the geometry:
 *
 *   A face on the OUTSIDE of a closed surface has all three of its edges shared
 *   with another face of the same model. It is part of a skin, so its texture is
 *   a decal on that skin -> the detail kernel, opaque, no holes and no
 *   dependence on draw order.
 *
 *   A face with a FREE edge is the rim of a card - a frill, a fringe, a tuft
 *   modelled as a couple of triangles sticking out of the body. Its alpha is a
 *   silhouette and must cut -> the alpha or modulate kernel.
 *
 * That test needs the whole mesh, so it belongs here at port time rather than in
 * the raster. The result is written to a NON-STOCK container, because OB3 has
 * nowhere to put a per-face kernel id (src/engine/proctex/obtori.h).
 *
 * Build and run:
 *   make -C src rs2012-qbd-obtori
 *   src/build_win64_opt/rs2012_qbd_obtori --tree OSRS-Content/osrs239-content \
 *       --model 70260 --model 69766 --out build/qbd_obtori
 *
 * Writes <out>/rs2012_model_<id>.obtori next to a report of what it chose. It
 * never writes the lane.
 */

#include "datatypes/model_obtori.h"

#include "datatypes/model.h"

#include <rscache.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS 16
#define MAX_TEXTURES 4096

struct MaterialFlags
{
    bool present;
    bool alpha;
    bool modulate;
    bool detail;
};

static struct MaterialFlags g_material[MAX_TEXTURES];

/* --- the lane's texture records ------------------------------------------ */

static char*
read_file(const char* path, long* out_size)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* text;

    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    text = size >= 0 ? (char*)malloc((size_t)size + 1) : NULL;
    if( !text || fread(text, 1, (size_t)size, file) != (size_t)size )
    {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    if( out_size )
        *out_size = size;
    return text;
}

/**
 * Which destination textures carry which kernel flags, from the bake's own
 * output. Reading the records rather than re-deriving the classification means
 * this tool and the renderer cannot disagree about what a material is.
 */
static bool
load_material_flags(const char* tree)
{
    char path[2048];
    char* records;
    char* index;
    char* line;

    snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/textures/texture_0.texture", tree);
    records = read_file(path, NULL);
    snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/textures/texture_0.compack", tree);
    index = read_file(path, NULL);
    if( !records || !index )
    {
        fprintf(stderr, "rs2012_qbd_obtori: no texture records under %s\n", tree);
        free(records);
        free(index);
        return false;
    }

    for( line = strtok(index, "\r\n"); line; line = strtok(NULL, "\r\n") )
    {
        char name[128], head[160];
        int id;
        char* block;
        char* end;

        if( sscanf(line, "%d=%127s", &id, name) != 2 )
            continue;
        if( id < 0 || id >= MAX_TEXTURES )
            continue;
        snprintf(head, sizeof(head), "[%s]\n", name);
        block = strstr(records, head);
        if( !block )
            continue;
        block += strlen(head);
        end = strstr(block, "\n[");

        g_material[id].present = true;
        /* Bounded to this record: the next block starts at `end`, and a search
         * past it would find every flag on every texture. */
        for( char* p = block; p && *p && (!end || p < end); )
        {
            char* eol = strchr(p, '\n');
            size_t len = eol ? (size_t)(eol - p) : strlen(p);
            if( len >= 9 && strncmp(p, "alpha=yes", 9) == 0 )
                g_material[id].alpha = true;
            if( len >= 12 && strncmp(p, "modulate=yes", 12) == 0 )
                g_material[id].modulate = true;
            if( len >= 10 && strncmp(p, "detail=yes", 10) == 0 )
                g_material[id].detail = true;
            p = eol ? eol + 1 : NULL;
        }
    }
    free(records);
    free(index);
    return true;
}

/* --- the geometric test --------------------------------------------------- */

/**
 * Mark every face that has at least one edge no other face shares.
 *
 * O(faces^2) would be 81 million compares on the QBD, so edges go into an open
 * hash keyed on the ordered vertex pair. A shared edge is the SAME pair of
 * vertex indices in either direction; models that duplicate vertices along a
 * seam would read as free there, which is a conservative error - it routes a
 * face to the cutting kernel, the same one it had before this tool existed.
 */
static void
mark_faces_with_free_edges(const struct RSCache_Model* model, uint8_t* out_free)
{
    int const face_count = model->face_count;
    int const slots = face_count * 8 + 16;
    int64_t* keys = (int64_t*)malloc((size_t)slots * sizeof(*keys));
    int* counts = (int*)calloc((size_t)slots, sizeof(*counts));

    if( !keys || !counts )
    {
        free(keys);
        free(counts);
        /* Cannot decide; leave every face on its material's own routing. */
        return;
    }
    for( int i = 0; i < slots; i++ )
        keys[i] = -1;

    for( int pass = 0; pass < 2; pass++ )
    {
        for( int f = 0; f < face_count; f++ )
        {
            int const v[3] = {
                model->face_indices_a[f], model->face_indices_b[f], model->face_indices_c[f]
            };
            for( int e = 0; e < 3; e++ )
            {
                int lo = v[e];
                int hi = v[(e + 1) % 3];
                int64_t key;
                size_t slot;

                if( lo > hi )
                {
                    int t = lo;
                    lo = hi;
                    hi = t;
                }
                key = ((int64_t)lo << 32) | (uint32_t)hi;
                slot = (size_t)((uint64_t)key * 0x9E3779B97F4A7C15ull >> 40) % (size_t)slots;
                while( keys[slot] != -1 && keys[slot] != key )
                    slot = (slot + 1) % (size_t)slots;

                if( pass == 0 )
                {
                    keys[slot] = key;
                    counts[slot]++;
                }
                else if( counts[slot] < 2 )
                {
                    out_free[f] = 1;
                }
            }
        }
    }
    free(keys);
    free(counts);
}

/* --- porting -------------------------------------------------------------- */

/**
 * Add `<id + offset>=<name>_obtori` to the lane's model pack, so the packer
 * gives the container its own group instead of overwriting the stock model's.
 * Idempotent: a row that is already there is left alone.
 */
static bool
register_variant_id(const char* tree, int model_id)
{
    char path[2048];
    char row[256];
    char* pack;
    FILE* file;
    bool ok;

    snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/pack/7_models.pack", tree);
    snprintf(row, sizeof(row), "%d=ported/rs2012_qbd_td/rs2012_model_%d_obtori\n",
             model_id + OBTORI_MODEL_ID_OFFSET, model_id);

    pack = read_file(path, NULL);
    if( !pack )
    {
        fprintf(stderr, "rs2012_qbd_obtori: cannot read %s\n", path);
        return false;
    }
    if( strstr(pack, row) )
    {
        free(pack);
        return true;
    }
    free(pack);

    file = fopen(path, "ab");
    if( !file )
    {
        fprintf(stderr, "rs2012_qbd_obtori: cannot append to %s\n", path);
        return false;
    }
    ok = fputs(row, file) >= 0;
    return fclose(file) == 0 && ok;
}

static bool
port_model(const char* tree, const char* out_dir, int model_id, bool verbose)
{
    char path[2048];
    char* raw;
    long raw_size = 0;
    struct RSCache_Model* model;
    struct RSCache_ModelProvenance* provenance = NULL;
    uint8_t* free_edge = NULL;
    uint8_t* kernels = NULL;
    uint8_t* encoded = NULL;
    int counts[OBTORI_KERNEL_COUNT] = { 0 };
    int written;
    bool ok = false;
    FILE* out;

    snprintf(path, sizeof(path), "%s/models/ported/rs2012_qbd_td/rs2012_model_%d.ob3",
             tree, model_id);
    raw = read_file(path, &raw_size);
    if( !raw )
    {
        fprintf(stderr, "rs2012_qbd_obtori: cannot read %s\n", path);
        return false;
    }
    model = RSCache_ModelNewDecodeProvenance((uint8_t*)raw, (int)raw_size, &provenance);
    if( !model )
    {
        fprintf(stderr, "rs2012_qbd_obtori: cannot decode model %d\n", model_id);
        free(raw);
        return false;
    }

    free_edge = (uint8_t*)calloc((size_t)model->face_count, 1);
    kernels = (uint8_t*)calloc((size_t)model->face_count, 1);
    if( !free_edge || !kernels )
        goto done;

    mark_faces_with_free_edges(model, free_edge);

    for( int f = 0; f < model->face_count; f++ )
    {
        int const texture = model->face_textures ? model->face_textures[f] : -1;
        struct MaterialFlags const* material;

        if( texture < 0 || texture >= MAX_TEXTURES || !g_material[texture].present )
        {
            kernels[f] = OBTORI_KERNEL_DEFAULT;
            counts[OBTORI_KERNEL_DEFAULT]++;
            continue;
        }
        material = &g_material[texture];

        if( material->detail )
        {
            /* An HD program is never a silhouette: it has no coverage to cut
             * with. It is a detail map wherever it appears. */
            kernels[f] = OBTORI_KERNEL_DETAIL;
        }
        else if( material->alpha && free_edge[f] )
        {
            /* A card's rim. The coverage is the shape, so it has to cut. */
            kernels[f] = material->modulate ? OBTORI_KERNEL_MODULATE : OBTORI_KERNEL_ALPHA;
        }
        else if( material->alpha )
        {
            /* Enclosed by its neighbours, so it is skin: the mask is a decal on
             * it and the face should stay opaque. This is the case the texture
             * flags get wrong, and the reason this file exists. */
            kernels[f] = OBTORI_KERNEL_DETAIL;
        }
        else
        {
            kernels[f] = OBTORI_KERNEL_DEFAULT;
        }
        counts[kernels[f]]++;
    }

    {
        int const bound = ObTori_EncodeBound((int)raw_size, model->face_count, 1);
        encoded = (uint8_t*)malloc((size_t)bound);
        if( !encoded )
            goto done;
        written = ObTori_Encode(
            raw, (int)raw_size, model->face_count, kernels, NULL, NULL, encoded, bound);
        if( written <= 0 )
        {
            fprintf(stderr, "rs2012_qbd_obtori: encode failed for %d\n", model_id);
            goto done;
        }
    }

    /* Named for the variant, not for the model: the stock <name>.ob3 keeps its
      * own asset slot and its own id, and this one is registered separately. */
    snprintf(path, sizeof(path), "%s/rs2012_model_%d_obtori.obtori", out_dir, model_id);
    out = fopen(path, "wb");
    if( !out )
    {
        fprintf(stderr, "rs2012_qbd_obtori: cannot write %s\n", path);
        goto done;
    }
    ok = fwrite(encoded, 1, (size_t)written, out) == (size_t)written;
    ok = fclose(out) == 0 && ok;

    if( ok )
        ok = register_variant_id(tree, model_id);

    if( verbose )
    {
        printf("model %d -> %d: %d faces -> %s\n", model_id,
               model_id + OBTORI_MODEL_ID_OFFSET, model->face_count, path);
        for( int k = 0; k < OBTORI_KERNEL_COUNT; k++ )
            if( counts[k] )
                printf("    %-12s %6d\n", ObTori_KernelName(k), counts[k]);
    }

done:
    free(encoded);
    free(kernels);
    free(free_edge);
    RSCache_ModelFree(model);
    RSCache_ModelProvenanceFree(provenance);
    free(raw);
    return ok;
}

static void
usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--tree DIR] [--out DIR] --model ID [--model ID ...]\n"
            "  Ports imported RS2012 models to OB_TORI with a per-face kernel\n"
            "  chosen from the mesh. Never writes the lane.\n",
            argv0);
}

int
main(int argc, char** argv)
{
    const char* tree = "OSRS-Content/osrs239-content";
    const char* out_dir = "build/qbd_obtori";
    int models[MAX_MODELS];
    int model_count = 0;
    int failures = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--tree") == 0 && i + 1 < argc )
            tree = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_dir = argv[++i];
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc && model_count < MAX_MODELS )
            models[model_count++] = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if( model_count == 0 )
    {
        usage(argv[0]);
        return 1;
    }
    if( !load_material_flags(tree) )
        return 1;

    for( int i = 0; i < model_count; i++ )
        if( !port_model(tree, out_dir, models[i], true) )
            failures++;

    if( failures )
        fprintf(stderr, "rs2012_qbd_obtori: %d model(s) failed\n", failures);
    return failures ? 1 : 0;
}
