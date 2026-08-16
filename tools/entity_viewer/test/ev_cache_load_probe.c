/*
 * Does the viewer's cache loader actually load this cache?
 *
 * ## Why a probe and not a unit test
 *
 * Every failure this covers is silent. A wrong revision profile does not error
 * — it decodes records at the wrong field widths, so the viewer comes up, lists
 * npcs, and shows nonsense. "16292 npcs" in the log reads as success whether
 * the names are real or the byte soup of a misaligned stream. So the questions
 * here are the ones a count cannot answer:
 *
 *   1. Which revision does detection choose, unprompted?
 *   2. Under that revision, do the config records consume exactly? A record ends
 *      with opcode 0 at exactly its file length, so a wrong width anywhere makes
 *      one miss its terminator.
 *   3. Do the things the viewer draws — npc models, sequence frames — actually
 *      decode to geometry, or merely to non-NULL?
 *
 *   make -C tools/entity_viewer ev_cache_load_probe
 *   tools/entity_viewer/ev_cache_load_probe cache.void634 [cache.rs643 ...]
 *
 * With no arguments it walks every `cache.*` beside the repo root.
 */

#include "ev_build.h"
#include "ev_caches.h"

#include "asset_access.h"
#include "tool_profile.h"

#include "datatypes/dat2_config_npc.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include "toridraw_model.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PROBE_NPC_SAMPLE 40

static int g_failures = 0;

static void
check(bool ok, const char* what)
{
    if( !ok )
    {
        printf("    FAIL %s\n", what);
        g_failures++;
    }
}

/**
 * Load `sample` npcs and report how many yield a model with geometry.
 *
 * Deliberately counts vertices rather than pointers: a model that decoded to
 * zero vertices is the exact shape of a wrong profile, and it is not NULL.
 */
static void
probe_npc_models(
    struct Tool_Dat2Cache* cache,
    const struct EV_Index* index,
    int* out_built,
    int* out_tried,
    int* out_vertices)
{
    int built = 0;
    int tried = 0;
    int vertices = 0;

    int step = index->npc_count > PROBE_NPC_SAMPLE ? index->npc_count / PROBE_NPC_SAMPLE : 1;
    for( int i = 0; i < index->npc_count && tried < PROBE_NPC_SAMPLE; i += step )
    {
        tried++;
        struct ToriDraw_Model* model = ev_build_npc_model(cache, index->npcs[i].id);
        if( !model )
            continue;
        if( model->vertex_count > 0 && model->face_count > 0 )
        {
            built++;
            vertices += model->vertex_count;
        }
        ToriDraw_ModelFree(model);
    }

    *out_built = built;
    *out_tried = tried;
    *out_vertices = vertices;
}

/** How many of the first `limit` npc records consume exactly. */
static void
probe_npc_exact(
    struct Tool_Dat2Cache* cache,
    const struct EV_Index* index,
    int limit,
    int* out_seen,
    int* out_exact)
{
    int seen = 0;
    int exact = 0;
    for( int i = 0; i < index->npc_count && seen < limit; i++ )
    {
        int record_exact = 0;
        struct RSCache_Dat2ConfigNpc* npc =
            tool_dat2_npc_load_checked(cache, index->npcs[i].id, &record_exact);
        if( !npc )
            continue;
        seen++;
        exact += record_exact ? 1 : 0;
        RSCache_Dat2ConfigNpcFree(npc);
    }
    *out_seen = seen;
    *out_exact = exact;
}

/** The registry's own gate: a dat1 cache has `main_file_cache.dat`, no `dat2`. */
static bool
has_dat2(const char* path)
{
    char probe[1024];
    snprintf(probe, sizeof(probe), "%s/main_file_cache.dat2", path);
    struct stat st;
    return stat(probe, &st) == 0 && S_ISREG(st.st_mode);
}

static void
probe_cache(const char* path)
{
    printf("== %s\n", path);

    /* Not a failure, and not silently skipped either: ev_caches_add refuses a
     * directory with no dat2, so a dat1 cache never reaches detection. Saying so
     * keeps a legitimate skip from reading as a pass. */
    if( !has_dat2(path) )
    {
        printf("    skip: dat1 cache (no main_file_cache.dat2)\n");
        return;
    }

    char detected[32] = { 0 };
    if( !ev_cache_detect_rev(path, detected, (int)sizeof(detected)) )
    {
        printf("    detect: FAILED — nothing decoded\n");
        g_failures++;
        return;
    }
    printf("    detect: %s\n", detected);

    struct RSCache profile;
    check(RSCache_ProfileByName(detected, &profile), "detected revision resolves");
    if( !RSCache_ProfileByName(detected, &profile) )
        return;

    struct Tool_Dat2Cache cache;
    if( !tool_dat2_open(path, &profile, &cache) )
    {
        printf("    open: FAILED\n");
        g_failures++;
        return;
    }

    struct EV_Index index;
    if( !ev_index_build(&cache, &profile, path, detected, &index) )
    {
        printf("    index: FAILED\n");
        g_failures++;
        tool_dat2_close(&cache);
        return;
    }

    int named = 0;
    for( int i = 0; i < index.npc_count; i++ )
        if( index.npcs[i].name && index.npcs[i].name[0] )
            named++;

    printf("    index:  %d npcs (%d named), %d sequences, %d models\n",
           index.npc_count, named, index.seq_count, index.model_count);
    check(index.npc_count > 0, "index has npcs");
    check(index.model_count > 0, "index has models");
    /* A misaligned npc stream loses the name opcode long before it loses the
     * record, so a low named ratio is the loudest signal a count can carry. */
    check(index.npc_count == 0 || named * 2 > index.npc_count, "most npcs are named");

    int seen = 0;
    int exact = 0;
    probe_npc_exact(&cache, &index, 400, &seen, &exact);
    printf("    decode: %d/%d npc records consume exactly\n", exact, seen);
    check(seen == 0 || exact == seen, "every sampled npc record consumes exactly");

    int built = 0;
    int tried = 0;
    int vertices = 0;
    probe_npc_models(&cache, &index, &built, &tried, &vertices);
    printf("    models: %d/%d sampled npcs built geometry (%d vertices)\n", built, tried,
           vertices);
    check(tried == 0 || built > 0, "at least one npc model has geometry");

    ev_index_free(&index);
    tool_dat2_close(&cache);
}

int
main(int argc, char** argv)
{
    if( argc > 1 )
    {
        for( int i = 1; i < argc; i++ )
            probe_cache(argv[i]);
    }
    else
    {
        DIR* d = opendir(".");
        if( !d )
        {
            fprintf(stderr, "cannot read the current directory\n");
            return 2;
        }
        struct dirent* ent;
        while( (ent = readdir(d)) != NULL )
        {
            if( strncmp(ent->d_name, "cache.", 6) != 0 )
                continue;
            struct stat st;
            if( stat(ent->d_name, &st) != 0 || !S_ISDIR(st.st_mode) )
                continue;
            probe_cache(ent->d_name);
        }
        closedir(d);
    }

    printf("\nev_cache_load_probe: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
