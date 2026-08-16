/*
 * seal_rig_scan — which sequence can drive which model, computed from the cache.
 *
 * The Inferno seal is three locs that have to collapse as one wall face. Two of
 * them have a named sequence; the middle does not, and every sequence tried on
 * it by hand was a no-op. Guessing from names does not converge, because a
 * sequence only moves a model when the model carries the *labels* the
 * sequence's framemap drives:
 *
 *   - transform types 0-3 (origin / translate / rotate / scale) read
 *     `vertex_bone_map`;
 *   - type 5 (alpha) reads `face_bone_map`.
 *
 * A framemap's bone groups are lists of those label ids. So "can sequence S
 * animate model M" is a set question, answerable offline: decode M's label
 * sets, decode S's framemap, and see how much of what S drives M actually has.
 * A sequence whose groups name labels the model does not carry cannot move it,
 * whatever it is called.
 *
 * Prints, for every model given, its label sets, then every candidate sequence
 * ranked by coverage. Sequences come from the frame ids in the content tree's
 * all.seq (the frame file itself carries the framemap id), so no config-group
 * decode is needed here.
 *
 *   make -C src test-seal-rig
 *   ./src/build/seal_rig_scan <cache-dir> <all.seq> <model-id>[,<model-id>...]
 */

#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/model.h"
#include "dat2disk.h"
#include "filelist.h"
#include "rscache.h"
#include "revisions/revisions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEQS 16384
#define LABELS 256

struct SeqInfo
{
    char name[128];
    int first_frame_id;
    int frame_count;
    int cycles;
    int framemap_id;
    unsigned char drives_vertex[LABELS];
    unsigned char drives_face[LABELS];
    int vertex_driven;
    int face_driven;
    int ok;
};

static struct SeqInfo g_seqs[MAX_SEQS];
static int g_seq_count;

/* all.seq is the exported config: `[name]` then `frame=<id>,<delay>` lines. The
 * frame id packs the archive group in its high 16 bits and the file in its low
 * 16, which is all that is needed to reach the framemap. */
static void
load_seqs(char const* path)
{
    FILE* f = fopen(path, "rb");
    char line[512];
    struct SeqInfo* cur = NULL;

    if( !f )
    {
        fprintf(stderr, "seal_rig_scan: cannot open %s\n", path);
        exit(1);
    }
    while( fgets(line, sizeof(line), f) )
    {
        if( line[0] == '[' )
        {
            char* end = strchr(line, ']');
            if( !end || g_seq_count >= MAX_SEQS )
            {
                cur = NULL;
                continue;
            }
            *end = 0;
            cur = &g_seqs[g_seq_count++];
            memset(cur, 0, sizeof(*cur));
            snprintf(cur->name, sizeof(cur->name), "%s", line + 1);
            cur->first_frame_id = -1;
            cur->framemap_id = -1;
        }
        else if( cur && strncmp(line, "frame=", 6) == 0 )
        {
            int id = 0;
            int delay = 1;
            if( sscanf(line + 6, "%d,%d", &id, &delay) < 1 )
                continue;
            if( cur->first_frame_id < 0 )
                cur->first_frame_id = id;
            cur->frame_count++;
            cur->cycles += delay > 0 ? delay : 1;
        }
    }
    fclose(f);
}

/* Resolve one sequence's framemap and record every label its groups drive. */
static void
resolve_seq(struct RSCache_Dat2Disk* disk, struct SeqInfo* s)
{
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* files = NULL;
    struct RSCache_Dat2Framemap* fm = NULL;
    int group;
    int file;
    int pos = -1;

    if( s->first_frame_id < 0 )
        return;
    group = s->first_frame_id >> 16;
    file = s->first_frame_id & 0xffff;

    archive = RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_TABLE_ANIMATIONS, group);
    if( !archive )
        return;
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
        goto done;
    for( int i = 0; i < archive->file_count; i++ )
        if( archive->file_ids[i] == file )
            pos = i;
    if( pos < 0 || pos >= files->file_count )
        goto done;

    s->framemap_id =
        RSCache_Dat2FramemapIdFromFrameArchive(files->files[pos], files->file_sizes[pos]);
    if( s->framemap_id < 0 )
        goto done;

    fm = RSCache_Dat2FramemapNewFromCache(disk, s->framemap_id);
    if( !fm )
        goto done;

    for( int g = 0; g < fm->length; g++ )
    {
        int type = fm->types ? fm->types[g] : -1;
        int n = fm->bone_groups_lengths ? fm->bone_groups_lengths[g] : 0;
        for( int b = 0; b < n; b++ )
        {
            int label = fm->bone_groups[g][b];
            if( label < 0 || label >= LABELS )
                continue;
            if( type >= 0 && type <= 3 )
                s->drives_vertex[label] = 1;
            else if( type == 5 )
                s->drives_face[label] = 1;
        }
    }
    for( int i = 0; i < LABELS; i++ )
    {
        s->vertex_driven += s->drives_vertex[i];
        s->face_driven += s->drives_face[i];
    }
    s->ok = 1;

done:
    if( fm )
        RSCache_Dat2FramemapFree(fm);
    if( files )
        RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

struct Ranked
{
    struct SeqInfo* s;
    int hit;
    int miss;
};

static int
rank_cmp(void const* a, void const* b)
{
    struct Ranked const* x = a;
    struct Ranked const* y = b;
    if( x->hit != y->hit )
        return y->hit - x->hit;
    return x->miss - y->miss;
}

static void
report_model(struct RSCache_Dat2Disk* disk, int model_id)
{
    struct RSCache_Model* m = RSCache_ModelNewFromCache(disk, model_id);
    unsigned char has_v[LABELS];
    unsigned char has_f[LABELS];
    int nv = 0;
    int nf = 0;
    struct Ranked* rank;
    int rank_n = 0;

    printf("\n=== model %d ===\n", model_id);
    if( !m )
    {
        printf("  DECODE FAILED\n");
        return;
    }
    memset(has_v, 0, sizeof(has_v));
    memset(has_f, 0, sizeof(has_f));

    if( m->vertex_bone_map )
        for( int i = 0; i < m->vertex_count; i++ )
            has_v[m->vertex_bone_map[i]] = 1;
    if( m->face_bone_map )
        for( int i = 0; i < m->face_count; i++ )
            has_f[m->face_bone_map[i]] = 1;
    for( int i = 0; i < LABELS; i++ )
    {
        nv += has_v[i];
        nf += has_f[i];
    }

    printf(
        "  vertices=%d faces=%d vertex_bone_map=%s face_bone_map=%s "
        "distinct_vertex_labels=%d distinct_face_labels=%d\n",
        m->vertex_count,
        m->face_count,
        m->vertex_bone_map ? "YES" : "no",
        m->face_bone_map ? "YES" : "no",
        nv,
        nf);
    if( nv )
    {
        printf("  vertex labels:");
        for( int i = 0; i < LABELS; i++ )
            if( has_v[i] )
                printf(" %d", i);
        printf("\n");
    }
    if( !m->vertex_bone_map && !m->face_bone_map )
    {
        printf("  -> no rig at all: NO sequence can move this model.\n");
        RSCache_ModelFree(m);
        return;
    }

    rank = calloc((size_t)g_seq_count, sizeof(*rank));
    for( int i = 0; i < g_seq_count; i++ )
    {
        struct SeqInfo* s = &g_seqs[i];
        int hit = 0;
        int miss = 0;
        if( !s->ok || (s->vertex_driven == 0 && s->face_driven == 0) )
            continue;
        for( int l = 0; l < LABELS; l++ )
        {
            if( s->drives_vertex[l] )
            {
                if( has_v[l] )
                    hit++;
                else
                    miss++;
            }
            if( s->drives_face[l] )
            {
                if( has_f[l] )
                    hit++;
                else
                    miss++;
            }
        }
        if( hit == 0 )
            continue;
        rank[rank_n].s = s;
        rank[rank_n].hit = hit;
        rank[rank_n].miss = miss;
        rank_n++;
    }
    qsort(rank, (size_t)rank_n, sizeof(*rank), rank_cmp);

    printf("  candidates (labels the seq drives that this model has / lacks):\n");
    for( int i = 0; i < rank_n && i < 12; i++ )
        printf(
            "    %-46s fm=%-5d frames=%-4d cycles=%-5d ticks=%5.2f  hit=%-4d miss=%d\n",
            rank[i].s->name,
            rank[i].s->framemap_id,
            rank[i].s->frame_count,
            rank[i].s->cycles,
            rank[i].s->cycles / 30.0,
            rank[i].hit,
            rank[i].miss);
    if( rank_n == 0 )
        printf("    none — no sequence in this cache drives a label this model has.\n");
    free(rank);
    RSCache_ModelFree(m);
}

/*
 * Which locs name a given model. all.loc is the *exported* table; if the port
 * dropped a record it would look unused there and still exist in the cache. So
 * ask the cache: decode every loc record and report the ones that name the
 * model. Also reports the loc's own `seq_id`, which is where a self-animating
 * loc carries its sequence.
 */
static void
find_locs_for_model(struct RSCache_Dat2Disk* disk, struct RSCache const* profile, int model_id)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int found = 0;

    printf("\n=== locs naming model %d ===\n", model_id);
    archive = RSCache_Dat2DiskArchiveNewLoad(
        disk,
        RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
        RSCACHE_DAT2_CONFIG_KIND_LOCS);
    if( !archive )
    {
        printf("  cannot load the loc config group\n");
        return;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        printf("  cannot split the loc config group\n");
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    for( int i = 0; i < files->file_count; i++ )
    {
        struct RSCache_Dat2ConfigLoc* loc;
        int hit = 0;
        if( !files->files[i] || files->file_sizes[i] <= 0 )
            continue;
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(
            profile, files->files[i], files->file_sizes[i]);
        if( !loc )
            continue;
        for( int g = 0; g < loc->shapes_and_model_count && !hit; g++ )
            for( int k = 0; k < (loc->lengths ? loc->lengths[g] : 0); k++ )
                if( loc->models && loc->models[g] && loc->models[g][k] == model_id )
                {
                    hit = 1;
                    break;
                }
        if( hit )
        {
            int loc_id = archive->file_ids ? archive->file_ids[i] : i;
            printf(
                "  loc %d  name=%s  size=%dx%d  seq=%d\n",
                loc_id,
                loc->name ? loc->name : "(none)",
                loc->size_x,
                loc->size_z,
                loc->seq_id);
            found++;
        }
        RSCache_Dat2ConfigLocFree(loc);
    }
    if( !found )
        printf("  none — no loc record in this cache names model %d\n", model_id);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

int
main(int argc, char** argv)
{
    struct RSCache_Dat2Disk* disk;
    char const* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    char const* seq_path = argc > 2 ? argv[2] : "OSRS-Content/osrs239-content/configs/all.seq";
    char const* models = argc > 3 ? argv[3] : "33033,33035,33037,33039,33040,33034,33038";
    char const* p;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "seal_rig_scan: cannot open cache %s\n", cache_dir);
        return 1;
    }
    /* Without the identity the disk answers ABSENT for every logical table, so
     * the framemap (skeleton) table cannot be resolved — app.c does the same
     * before any decode. osrs239 is the cache these ids come from. */
    {
        struct RSCache profile =
            RSCache_ProfileForIdentity(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239,
                                       0u);
        RSCache_Dat2DiskSetProfile(disk, &profile);
    }

    load_seqs(seq_path);
    printf("seal_rig_scan: %d sequences from %s\n", g_seq_count, seq_path);
    for( int i = 0; i < g_seq_count; i++ )
        resolve_seq(disk, &g_seqs[i]);
    {
        int ok = 0;
        for( int i = 0; i < g_seq_count; i++ )
            ok += g_seqs[i].ok;
        printf("seal_rig_scan: %d framemaps resolved\n", ok);
    }

    for( p = models; *p; )
    {
        char* end;
        long id = strtol(p, &end, 10);
        if( end == p )
            break;
        report_model(disk, (int)id);
        p = (*end == ',') ? end + 1 : end;
    }

    /* TORIRS_SEAL_BLOCK=lo,hi — sweep an art block: every model in the range
     * with its rig, and every loc that names it. This is the map that says
     * which pieces of a set can be animated at all, and it is derived from the
     * cache rather than from a naming convention. */
    {
        struct RSCache profile =
            RSCache_ProfileForIdentity(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, 0u);
        char const* block = getenv("TORIRS_SEAL_BLOCK");
        int lo = 0;
        int hi = -1;
        if( block && sscanf(block, "%d,%d", &lo, &hi) == 2 )
        {
            for( int id = lo; id <= hi; id++ )
            {
                struct RSCache_Model* m = RSCache_ModelNewFromCache(disk, id);
                unsigned char has_v[LABELS];
                int nv = 0;
                if( !m )
                    continue;
                memset(has_v, 0, sizeof(has_v));
                if( m->vertex_bone_map )
                    for( int i = 0; i < m->vertex_count; i++ )
                        has_v[m->vertex_bone_map[i]] = 1;
                for( int i = 0; i < LABELS; i++ )
                    nv += has_v[i];
                printf("model %d v=%-5d f=%-5d rig=%-3s labels=%d",
                       id, m->vertex_count, m->face_count,
                       m->vertex_bone_map ? "YES" : "no", nv);
                if( nv && nv < 40 )
                {
                    printf(" [");
                    for( int i = 0; i < LABELS; i++ )
                        if( has_v[i] )
                            printf("%d ", i);
                    printf("]");
                }
                printf("\n");
                RSCache_ModelFree(m);
                find_locs_for_model(disk, &profile, id);
            }
        }
        else
        {
            find_locs_for_model(disk, &profile, 33034);
            find_locs_for_model(disk, &profile, 33037);
        }
    }

    RSCache_Dat2DiskFree(disk);
    return 0;
}
