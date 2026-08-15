/*
 * ev_export — write a subject's models and animations to a directory.
 *
 * The entity viewer can open a file off disk (Select model file…), but there
 * was no way to *get* one: every model in the tree lives inside a 461 MB cache
 * that is gitignored and obtained out of band. This produces a small, checked-in
 * set so the HD path is reproducible without one.
 *
 * ## Two formats, on purpose
 *
 * **Models are raw archive bytes** (`.ob3` and friends), exactly as the cache
 * holds them. That is the point: the viewer's upload path runs them through the
 * real `RSCache_ModelNewDecode`, so the exported file exercises format
 * detection and the complex-texture decode rather than a pre-digested form. An
 * OB3 model is the only kind that carries the cylinder/cube/sphere mappings the
 * HD kernels need.
 *
 * **Animations are ev_wire blobs** (`.eva`), not raw archives. A sequence is not
 * self-contained: it names frames, which name a framemap, which is a separate
 * archive — so "the animation file" is three lookups deep and a raw export would
 * be useless without the cache it came from. The ev_wire form carries the rig
 * and every frame in playback order, so it loads with no cache at all.
 *
 * Usage:
 *   ev_export --rev rs727 cache.rs727_preeoc --out docs/hdmodels
 *   ev_export ... --npc 2745 --name tztok_jad     (add a subject)
 *   ev_export ... --only qbd                      (one of the built-ins)
 */

#include "ev_build.h"
#include "ev_wire.h"

#include "dat2disk.h"
#include "datatypes/dat2_config_bas.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/model.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"
#include "toridraw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * The built-in subjects.
 *
 * Ids are from cache.rs727_preeoc and were found by scanning the npc table for
 * the names below; they are recorded here rather than re-scanned so an export
 * is reproducible and so a renamed or renumbered npc fails loudly instead of
 * quietly exporting something else.
 */
struct subject
{
    const char* dir;
    const char* label;
    int npc_ids[6];
};

static const struct subject g_subjects[] = {
    /* Three body models: the QBD's phases. 69766 is shared scenery on the
     * record and is exported with them because the npc names it. */
    { "qbd", "Queen Black Dragon", { 15454, 15506, 15507, 0, 0, 0 } },
    { "tztok_jad", "TzTok-Jad", { 2745, 0, 0, 0, 0, 0 } },
    { "strykewyrm", "Strykewyrms (ice / desert / jungle)", { 9463, 9465, 9467, 0, 0, 0 } },
};
#define SUBJECT_COUNT ((int)(sizeof(g_subjects) / sizeof(g_subjects[0])))

static int g_models_written;
static int g_anims_written;

static int
ensure_dir(const char* path)
{
    if( mkdir(path, 0777) == 0 )
        return 1;
    return errno == EEXIST;
}

static int
write_file(const char* path, const void* data, size_t len)
{
    FILE* f = fopen(path, "wb");
    if( !f )
        return 0;
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    return n == len;
}

/** The four-format sniff the viewer's server does, kept identical. */
static const char*
model_format_name(const unsigned char* data, size_t len)
{
    if( len < 2 )
        return "unknown";
    if( data[len - 2] == 0xFF && data[len - 1] == 0xFF )
        return "OB3";
    if( data[len - 2] == 0xFF && data[len - 1] == 0xFE )
        return "V2";
    if( data[len - 2] == 0xFF && data[len - 1] == 0xFD )
        return "V3";
    return "OB2";
}

/*
 * One model, as the cache holds it. No decode on the way out — the viewer does
 * that, which is what makes the exported file a test of the decoder rather than
 * of this tool.
 *
 * The decode still happens HERE, discarded, purely to fill the manifest: a
 * manifest that says "233 textured faces, 7 cube" is what makes a wrong export
 * visible without opening the viewer.
 */
static void
export_model(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2Disk* disk,
    int model_table,
    int model_id,
    const char* dir,
    FILE* manifest)
{
    (void)c;
    struct RSCache_Dat2DiskArchive* a =
        RSCache_Dat2DiskArchiveNewLoad(disk, model_table, model_id);
    if( !a )
    {
        fprintf(manifest, "  model %-6d MISSING\n", model_id);
        return;
    }

    const char* format = model_format_name((const unsigned char*)a->data, (size_t)a->data_size);

    char path[1024];
    snprintf(path, sizeof(path), "%s/model_%d.%s", dir, model_id,
             strcmp(format, "OB3") == 0 ? "ob3" : "model");

    if( write_file(path, a->data, (size_t)a->data_size) )
        g_models_written++;

    struct RSCache_Model* m =
        RSCache_ModelNewDecode((uint8_t*)a->data, a->data_size);
    if( m )
    {
        int rt[4] = { 0, 0, 0, 0 };
        int other = 0;
        for( int t = 0; t < m->textured_face_count; t++ )
        {
            int k = m->texture_render_types ? (m->texture_render_types[t] & 0xFF) : 0;
            if( k < 4 )
                rt[k]++;
            else
                other++;
        }
        int alpha_faces = 0;
        for( int f = 0; f < m->face_count; f++ )
            if( m->face_alphas && m->face_alphas[f] )
                alpha_faces++;

        fprintf(manifest,
                "  model %-6d %-5s %6d bytes  verts %5d  faces %5d  texcoords %4d"
                "  [plane %d cyl %d cube %d sph %d%s]  alpha faces %d\n",
                model_id, format, a->data_size, m->vertex_count, m->face_count,
                m->textured_face_count, rt[0], rt[1], rt[2], rt[3],
                other ? " +other" : "", alpha_faces);
        RSCache_ModelFree(m);
    }
    else
    {
        fprintf(manifest, "  model %-6d %-5s %6d bytes  DECODE FAILED\n",
                model_id, format, a->data_size);
    }

    RSCache_Dat2DiskArchiveFree(a);
}

static void
export_anim(
    struct Tool_Dat2Cache* c,
    int seq_id,
    const char* what,
    const char* dir,
    FILE* manifest)
{
    int framemap = -1;
    struct ToriDraw_Animation* anim = ev_build_seq_anim(c, seq_id, &framemap);
    if( !anim )
    {
        fprintf(manifest, "  anim  %-6d %-22s (no frames or no rig)\n", seq_id, what);
        return;
    }

    struct EV_WireBuf buf = { 0 };
    if( ev_wire_write_anim(&buf, anim) )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/anim_%d.eva", dir, seq_id);
        if( write_file(path, buf.data, buf.len) )
            g_anims_written++;
        fprintf(manifest, "  anim  %-6d %-22s rig %-5d frames %3d  %6zu bytes\n",
                seq_id, what, framemap, anim->frame_count, buf.len);
    }
    ev_wire_free(&buf);
    ToriDraw_AnimationFree(anim);
}

/*
 * The animations an npc record itself declares.
 *
 * Not "every sequence on the rig": that is what the catalog is for and it runs
 * to hundreds per rig. These are the ones the record positively asserts belong
 * to this npc, so an exported set is small and every file in it is known to
 * apply.
 */
static void
export_npc(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2Disk* disk,
    int model_table,
    int npc_id,
    const char* dir,
    FILE* manifest,
    int* seen,
    int* seen_count)
{
    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(c, npc_id);
    if( !npc )
    {
        fprintf(manifest, "npc %d: NOT FOUND\n", npc_id);
        return;
    }

    fprintf(manifest, "\nnpc %d \"%s\"\n", npc_id, npc->name ? npc->name : "(unnamed)");

    for( int i = 0; i < npc->models_count; i++ )
        export_model(c, disk, model_table, npc->models[i], dir, manifest);

    /*
     * Where an RS2-era npc's animations actually live.
     *
     * The per-npc `standing_animation` / `walking_animation` fields are the
     * OldSchool shape and are -1 on every subject here; RS727 moved the idle and
     * walk set into a shared BasType that the record points at with
     * `bas_type_id`. Reading only the direct fields exports zero animations and
     * says nothing about why, which is exactly what the first run of this tool
     * did.
     */
    struct RSCache_Dat2ConfigBas* bas = NULL;
    if( npc->bas_type_id >= 0 )
    {
        bas = tool_dat2_bas_load(c, npc->bas_type_id);
        if( bas )
            fprintf(manifest, "  bas   %-6d (idle %d walk %d back %d left %d right %d)\n",
                    npc->bas_type_id, bas->idle_seq_id, bas->walk_seq_id,
                    bas->walk_back_seq_id, bas->walk_left_seq_id, bas->walk_right_seq_id);
        else
            fprintf(manifest, "  bas   %-6d (failed to load)\n", npc->bas_type_id);
    }

    struct
    {
        int seq;
        const char* what;
    } anims[] = {
        { bas ? bas->idle_seq_id : -1, "bas idle" },
        { bas ? bas->walk_seq_id : -1, "bas walk" },
        { bas ? bas->walk_back_seq_id : -1, "bas walk back" },
        { bas ? bas->walk_left_seq_id : -1, "bas walk left" },
        { bas ? bas->walk_right_seq_id : -1, "bas walk right" },
        { npc->standing_animation, "standing" },
        { npc->walking_animation, "walking" },
        { npc->run_animation, "run" },
        { npc->crawl_animation, "crawl" },
        { npc->rotate180_animation, "rotate 180" },
        { npc->rotate_left_animation, "rotate left" },
        { npc->rotate_right_animation, "rotate right" },
        { npc->idle_rotate_left_animation, "idle rotate left" },
        { npc->idle_rotate_right_animation, "idle rotate right" },
    };

    /*
     * One sequence often fills several slots (idle == walk on a static npc) AND
     * several npcs of one subject share a BasType — all three QBD phases point
     * at bas 2502. Deduping per npc still wrote the same file three times and
     * reported "3 animations" for one file, so the seen-set spans the subject.
     */
    for( int i = 0; i < (int)(sizeof(anims) / sizeof(anims[0])); i++ )
    {
        int seq = anims[i].seq;
        if( seq < 0 )
            continue;
        int dup = 0;
        for( int k = 0; k < *seen_count; k++ )
            if( seen[k] == seq )
                dup = 1;
        if( dup )
        {
            fprintf(manifest, "  anim  %-6d %-22s (already exported)\n", seq, anims[i].what);
            continue;
        }
        if( *seen_count < 64 )
            seen[(*seen_count)++] = seq;
        export_anim(c, seq, anims[i].what, dir, manifest);
    }

    if( *seen_count == 0 )
        fprintf(manifest, "  (this npc declares no animations)\n");
}

int
main(int argc, char** argv)
{
    const char* rev = "rs727";
    const char* cache_dir = "cache.rs727_preeoc";
    const char* out_root = "docs/hdmodels";
    const char* only = NULL;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_root = argv[++i];
        else if( strcmp(argv[i], "--only") == 0 && i + 1 < argc )
            only = argv[++i];
        else if( argv[i][0] != '-' )
            cache_dir = argv[i];
        else
        {
            fprintf(stderr,
                    "usage: %s [--rev NAME] <cache_dir> [--out DIR] [--only SUBJECT]\n",
                    argv[0]);
            return 2;
        }
    }

    ToriDraw_Init();

    struct RSCache profile;
    if( !RSCache_ProfileByName(rev, &profile) )
    {
        fprintf(stderr, "ev_export: unknown revision profile '%s'\n", rev);
        return 1;
    }

    struct Tool_Dat2Cache c;
    if( !tool_dat2_open(cache_dir, &profile, &c) )
    {
        fprintf(stderr,
                "ev_export: cannot open %s\n"
                "  (cache.rs727_preeoc is ~461MB, gitignored, and obtained out of band)\n",
                cache_dir);
        return 1;
    }

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "ev_export: cannot reopen %s for raw archives\n", cache_dir);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    int model_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MODELS);
    if( model_table < 0 )
    {
        fprintf(stderr, "ev_export: no model table\n");
        return 1;
    }

    if( !ensure_dir(out_root) )
    {
        fprintf(stderr, "ev_export: cannot create %s\n", out_root);
        return 1;
    }

    for( int s = 0; s < SUBJECT_COUNT; s++ )
    {
        const struct subject* sub = &g_subjects[s];
        if( only && strcmp(only, sub->dir) != 0 )
            continue;

        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/%s", out_root, sub->dir);
        if( !ensure_dir(dir) )
        {
            fprintf(stderr, "ev_export: cannot create %s\n", dir);
            continue;
        }

        char mpath[1024];
        snprintf(mpath, sizeof(mpath), "%s/manifest.txt", dir);
        FILE* manifest = fopen(mpath, "w");
        if( !manifest )
        {
            fprintf(stderr, "ev_export: cannot write %s\n", mpath);
            continue;
        }

        fprintf(manifest,
                "%s\n"
                "exported by ev_export from %s (profile %s)\n"
                "\n"
                "Models are RAW cache archives: the viewer decodes them itself, so these\n"
                "exercise format detection and the complex-texture decode. Animations are\n"
                "ev_wire blobs (.eva) because a sequence is not self-contained - it names\n"
                "frames which name a framemap - so a raw export would need the cache back.\n"
                "\n"
                "An animation only moves a model that shares its rig; the rig id is below.\n",
                sub->label, cache_dir, rev);

        int before_m = g_models_written;
        int before_a = g_anims_written;
        int seen[64];
        int seen_count = 0;
        for( int i = 0; i < 6 && sub->npc_ids[i]; i++ )
            export_npc(&c, disk, model_table, sub->npc_ids[i], dir, manifest, seen,
                       &seen_count);

        fclose(manifest);
        printf("%-12s %2d models, %2d animations -> %s\n",
               sub->dir, g_models_written - before_m, g_anims_written - before_a, dir);
    }

    printf("\n%d models, %d animations written under %s\n",
           g_models_written, g_anims_written, out_root);

    RSCache_Dat2DiskFree(disk);
    tool_dat2_close(&c);
    return 0;
}
