/*
 * find_anims — discover sequences that share a framemap with an NPC's base set.
 *
 * Usage:
 *   find_anims --rev NAME <cache_dir> --npc <id> [--strict] [--json]
 *   find_anims --rev NAME <cache_dir> --model <id> [--strict] [--json]
 *   find_anims --rev NAME <cache_dir> --seq <id> [--strict] [--json]
 */

#include "anim_affinity.h"
#include "asset_access.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> --npc <id> [--strict] [--json]\n"
        "  %s --rev NAME <cache_dir> --model <id> [--strict] [--json]\n"
        "  %s --rev NAME <cache_dir> --seq <id> [--strict] [--json]\n"
        "  %s --game G --epoch E --revision N [--quirks LIST] <cache_dir> ...\n",
        argv0,
        argv0,
        argv0,
        argv0);
}

static int
int_in_list(const int* list, int count, int v)
{
    for( int i = 0; i < count; i++ )
        if( list[i] == v )
            return 1;
    return 0;
}

static int
run_dat2(
    struct Tool_Dat2Cache* c,
    int npc_id,
    int model_id,
    int seq_id,
    int strict,
    int json)
{
    struct Tool_AnimSeeds seeds;
    tool_anim_seeds_init(&seeds);

    int* model_ids = NULL;
    int model_count = 0;

    if( npc_id >= 0 )
    {
        struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(c, npc_id);
        if( !npc )
        {
            fprintf(stderr, "NPC %d not found\n", npc_id);
            return 1;
        }
        tool_dat2_npc_anim_seeds(c, npc, &seeds);
        if( npc->models && npc->models_count > 0 )
        {
            model_ids = malloc((size_t)npc->models_count * sizeof(int));
            if( model_ids )
            {
                memcpy(model_ids, npc->models, (size_t)npc->models_count * sizeof(int));
                model_count = npc->models_count;
            }
        }
        if( !json )
        {
            printf(
                "npc id=%d name=%s bas_type_id=%d models=%d\n",
                npc_id,
                npc->name ? npc->name : "(null)",
                npc->bas_type_id,
                npc->models_count);
        }
        RSCache_Dat2ConfigNpcFree(npc);
    }
    else if( seq_id >= 0 )
    {
        tool_anim_seeds_add(&seeds, seq_id);
    }
    else if( model_id >= 0 )
    {
        model_ids = malloc(sizeof(int));
        if( model_ids )
        {
            model_ids[0] = model_id;
            model_count = 1;
        }
        /* Without an NPC/seq seed we cannot discover framemaps from the model
         * alone — bone labels do not identify a framemap. Require --seq too. */
        fprintf(
            stderr,
            "error: --model alone cannot seed framemaps; pass --npc or --seq\n");
        free(model_ids);
        tool_anim_seeds_free(&seeds);
        return 1;
    }

    if( seeds.count == 0 )
    {
        fprintf(stderr, "No seed sequences found\n");
        tool_anim_seeds_free(&seeds);
        free(model_ids);
        return 1;
    }

    if( !json )
    {
        printf("seed sequences (%d):", seeds.count);
        for( int i = 0; i < seeds.count; i++ )
            printf(" %d", seeds.seq_ids[i]);
        printf("\n");
    }

    int seed_fms[64];
    int seed_fm_count = 0;
    for( int i = 0; i < seeds.count && seed_fm_count < 64; i++ )
    {
        struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(c, seeds.seq_ids[i]);
        if( !seq )
        {
            fprintf(stderr, "warn: seed seq %d not found\n", seeds.seq_ids[i]);
            continue;
        }
        int fm = tool_dat2_seq_framemap_id(c, seq, 1);
        if( fm >= 0 && !int_in_list(seed_fms, seed_fm_count, fm) )
            seed_fms[seed_fm_count++] = fm;
        RSCache_Dat2ConfigSequenceFree(seq);
    }

    if( seed_fm_count == 0 )
    {
        fprintf(stderr, "No seed framemaps resolved\n");
        tool_anim_seeds_free(&seeds);
        free(model_ids);
        return 1;
    }

    if( !json )
    {
        printf("seed framemaps (%d):", seed_fm_count);
        for( int i = 0; i < seed_fm_count; i++ )
            printf(" %d", seed_fms[i]);
        printf("\n");
    }

    struct Tool_FramemapIndex index;
    if( !tool_dat2_build_framemap_index(c, &index) )
    {
        fprintf(stderr, "Failed to build framemap index\n");
        tool_anim_seeds_free(&seeds);
        free(model_ids);
        return 1;
    }

    int* candidates = NULL;
    int candidate_count = 0;
    if( !tool_framemap_index_query(
            &index, seed_fms, seed_fm_count, &candidates, &candidate_count) )
    {
        fprintf(stderr, "Query failed\n");
        tool_framemap_index_free(&index);
        tool_anim_seeds_free(&seeds);
        free(model_ids);
        return 1;
    }

    /* Optional strict filter against model bone labels. */
    struct RSCache_Dat2Framemap** fms = NULL;
    if( strict && model_count > 0 )
    {
        fms = calloc((size_t)seed_fm_count, sizeof(*fms));
        for( int i = 0; i < seed_fm_count; i++ )
            fms[i] = tool_dat2_framemap_load(c, seed_fms[i]);

        int* filtered = NULL;
        int filtered_count = 0;
        int filtered_cap = 0;
        for( int i = 0; i < candidate_count; i++ )
        {
            int sid = candidates[i];
            int fm_id = -1;
            int frame_count = 0;
            for( int j = 0; j < index.count; j++ )
            {
                if( index.entries[j].seq_id == sid )
                {
                    fm_id = index.entries[j].framemap_id;
                    frame_count = index.entries[j].frame_count;
                    break;
                }
            }
            (void)frame_count;
            struct RSCache_Dat2Framemap* fm = NULL;
            for( int j = 0; j < seed_fm_count; j++ )
            {
                if( seed_fms[j] == fm_id )
                {
                    fm = fms[j];
                    break;
                }
            }
            if( !fm )
                continue;
            int ok = 1;
            for( int m = 0; m < model_count; m++ )
            {
                struct RSCache_Model* model = tool_dat2_model_load(c, model_ids[m]);
                if( !model )
                    continue;
                if( !tool_framemap_covers_model(fm, model) )
                    ok = 0;
                RSCache_ModelFree(model);
            }
            if( !ok )
                continue;
            if( filtered_count >= filtered_cap )
            {
                int nc = filtered_cap == 0 ? 64 : filtered_cap * 2;
                int* p = realloc(filtered, (size_t)nc * sizeof(*p));
                if( !p )
                    break;
                filtered = p;
                filtered_cap = nc;
            }
            filtered[filtered_count++] = sid;
        }
        free(candidates);
        candidates = filtered;
        candidate_count = filtered_count;
    }

    if( json )
    {
        printf("{\n");
        printf("  \"npc_id\": %d,\n", npc_id);
        printf("  \"seed_sequences\": [");
        for( int i = 0; i < seeds.count; i++ )
            printf("%s%d", i ? ", " : "", seeds.seq_ids[i]);
        printf("],\n");
        printf("  \"seed_framemaps\": [");
        for( int i = 0; i < seed_fm_count; i++ )
            printf("%s%d", i ? ", " : "", seed_fms[i]);
        printf("],\n");
        printf("  \"candidates\": [\n");
        for( int i = 0; i < candidate_count; i++ )
        {
            int sid = candidates[i];
            int fm_id = -1;
            int frame_count = 0;
            for( int j = 0; j < index.count; j++ )
            {
                if( index.entries[j].seq_id == sid )
                {
                    fm_id = index.entries[j].framemap_id;
                    frame_count = index.entries[j].frame_count;
                    break;
                }
            }
            printf(
                "    {\"seq_id\": %d, \"framemap_id\": %d, \"frame_count\": %d}%s\n",
                sid,
                fm_id,
                frame_count,
                i + 1 < candidate_count ? "," : "");
        }
        printf("  ]\n}\n");
    }
    else
    {
        printf("candidates (%d):\n", candidate_count);
        for( int i = 0; i < candidate_count; i++ )
        {
            int sid = candidates[i];
            int fm_id = -1;
            int frame_count = 0;
            for( int j = 0; j < index.count; j++ )
            {
                if( index.entries[j].seq_id == sid )
                {
                    fm_id = index.entries[j].framemap_id;
                    frame_count = index.entries[j].frame_count;
                    break;
                }
            }
            printf("  seq %d  framemap %d  frames %d\n", sid, fm_id, frame_count);
        }
    }

    if( fms )
    {
        for( int i = 0; i < seed_fm_count; i++ )
            RSCache_Dat2FramemapFree(fms[i]);
        free(fms);
    }
    free(candidates);
    tool_framemap_index_free(&index);
    tool_anim_seeds_free(&seeds);
    free(model_ids);
    return 0;
}

static int
run_dat1(
    struct Tool_Dat1Cache* c,
    int npc_id,
    int seq_id,
    int json)
{
    struct Tool_AnimSeeds seeds;
    tool_anim_seeds_init(&seeds);

    if( npc_id >= 0 )
    {
        struct RSCache_Dat1ConfigNpc* npc = tool_dat1_npc_load(c, npc_id);
        if( !npc )
        {
            fprintf(stderr, "NPC %d not found\n", npc_id);
            return 1;
        }
        tool_dat1_npc_anim_seeds(npc, &seeds);
        if( !json )
            printf("npc id=%d name=%s\n", npc_id, npc->name ? npc->name : "(null)");
        RSCache_Dat1ConfigNpcFree(npc);
    }
    else if( seq_id >= 0 )
    {
        tool_anim_seeds_add(&seeds, seq_id);
    }

    if( seeds.count == 0 )
    {
        fprintf(stderr, "No seed sequences found\n");
        tool_anim_seeds_free(&seeds);
        return 1;
    }

    struct RSCache_Dat1ConfigSeqList* seqs = tool_dat1_seq_list_load(c);
    if( !seqs )
    {
        fprintf(stderr, "Failed to load seq.dat\n");
        tool_anim_seeds_free(&seeds);
        return 1;
    }

    struct Tool_Dat1AnimAffinity affinity;
    if( !tool_dat1_build_anim_affinity(c, seqs, &affinity) )
    {
        fprintf(stderr, "Failed to build dat1 anim affinity\n");
        RSCache_Dat1ConfigSeqListFree(seqs);
        tool_anim_seeds_free(&seeds);
        return 1;
    }

    uint64_t seed_hashes[64];
    int seed_hash_count = 0;
    for( int i = 0; i < seeds.count && seed_hash_count < 64; i++ )
    {
        int sid = seeds.seq_ids[i];
        if( sid < 0 || sid >= affinity.seq_count )
            continue;
        uint64_t h = affinity.seq_base_hash[sid];
        if( h == 0 )
            continue;
        int dup = 0;
        for( int j = 0; j < seed_hash_count; j++ )
            if( seed_hashes[j] == h )
                dup = 1;
        if( !dup )
            seed_hashes[seed_hash_count++] = h;
    }

    int* candidates = NULL;
    int candidate_count = 0;
    tool_dat1_affinity_query(
        &affinity, seed_hashes, seed_hash_count, &candidates, &candidate_count);

    if( json )
    {
        printf("{\n  \"npc_id\": %d,\n  \"seed_sequences\": [", npc_id);
        for( int i = 0; i < seeds.count; i++ )
            printf("%s%d", i ? ", " : "", seeds.seq_ids[i]);
        printf("],\n  \"seed_base_hashes\": [");
        for( int i = 0; i < seed_hash_count; i++ )
            printf("%s\"%llx\"", i ? ", " : "", (unsigned long long)seed_hashes[i]);
        printf("],\n  \"candidates\": [");
        for( int i = 0; i < candidate_count; i++ )
            printf("%s%d", i ? ", " : "", candidates[i]);
        printf("]\n}\n");
    }
    else
    {
        printf("seed sequences (%d):", seeds.count);
        for( int i = 0; i < seeds.count; i++ )
            printf(" %d", seeds.seq_ids[i]);
        printf("\nseed AnimBase hashes (%d):", seed_hash_count);
        for( int i = 0; i < seed_hash_count; i++ )
            printf(" %llx", (unsigned long long)seed_hashes[i]);
        printf("\ncandidates (%d):\n", candidate_count);
        for( int i = 0; i < candidate_count; i++ )
            printf("  seq %d\n", candidates[i]);
    }

    free(candidates);
    tool_dat1_anim_affinity_free(&affinity);
    RSCache_Dat1ConfigSeqListFree(seqs);
    tool_anim_seeds_free(&seeds);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* game_name = NULL;
    const char* epoch_name = NULL;
    const char* revision_text = NULL;
    const char* quirks_list = NULL;
    const char* cache_dir = NULL;
    int npc_id = -1;
    int model_id = -1;
    int seq_id = -1;
    int strict = 0;
    int json = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--game") == 0 && i + 1 < argc )
            game_name = argv[++i];
        else if( strcmp(argv[i], "--epoch") == 0 && i + 1 < argc )
            epoch_name = argv[++i];
        else if( strcmp(argv[i], "--revision") == 0 && i + 1 < argc )
            revision_text = argv[++i];
        else if( strcmp(argv[i], "--quirks") == 0 && i + 1 < argc )
            quirks_list = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            npc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
            model_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            seq_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--strict") == 0 )
            strict = 1;
        else if( strcmp(argv[i], "--json") == 0 )
            json = 1;
        else if( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else if( argv[i][0] == '-' )
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        else if( !cache_dir )
            cache_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    int selectors = (npc_id >= 0) + (model_id >= 0) + (seq_id >= 0);
    if( !cache_dir || selectors != 1 )
    {
        usage(argv[0]);
        return 1;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(
            rev_name, game_name, epoch_name, revision_text, quirks_list, &profile) )
        return 1;

    tool_print_profile(cache_dir, &profile);

    if( profile.epoch == RSCACHE_EPOCH_DAT2 )
    {
        struct Tool_Dat2Cache cache;
        if( !tool_dat2_open(cache_dir, &profile, &cache) )
            return 1;
        int rc = run_dat2(&cache, npc_id, model_id, seq_id, strict, json);
        tool_dat2_close(&cache);
        return rc;
    }

    if( profile.epoch == RSCACHE_EPOCH_DAT1 )
    {
        if( model_id >= 0 )
        {
            fprintf(stderr, "dat1 --model seeding is not supported; use --npc or --seq\n");
            return 1;
        }
        struct Tool_Dat1Cache cache;
        if( !tool_dat1_open(cache_dir, &profile, &cache) )
            return 1;
        int rc = run_dat1(&cache, npc_id, seq_id, json);
        tool_dat1_close(&cache);
        return rc;
    }

    fprintf(stderr, "Unsupported epoch\n");
    return 1;
}
