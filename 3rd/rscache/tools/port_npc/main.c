/*
 * port_npc — port an NPC and its asset closure between cache revisions.
 *
 * Dry-run by default; pass --apply to write. Prefer --out DIR so the destination
 * cache is copied first (Dat2DiskWriteArchive appends and orphans).
 */

#include "anim_affinity.h"
#include "asset_access.h"
#include "cache_write.h"
#include "port_plan.h"
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
        "  %s --from-rev A <src_dir> --to-rev B <dst_dir> --npc ID\n"
        "     [--out DIR] [--apply] [--include-related-anims] [--emit-bas]\n"
        "     [--json report.json]\n",
        argv0);
}

static int
is_osrs239(const struct RSCache* p)
{
    return p->game == RSCACHE_GAME_OLDSCHOOL && p->revision == 239;
}

static int
add_unique_int(int** arr, int* count, int* cap, int v)
{
    for( int i = 0; i < *count; i++ )
        if( (*arr)[i] == v )
            return 1;
    if( *count >= *cap )
    {
        int nc = *cap == 0 ? 16 : (*cap) * 2;
        int* n = realloc(*arr, (size_t)nc * sizeof(int));
        if( !n )
            return 0;
        *arr = n;
        *cap = nc;
    }
    (*arr)[(*count)++] = v;
    return 1;
}

struct alloc_ctx
{
    struct Tool_Dat2Cache* dst;
    int (*base_free)(struct Tool_Dat2Cache* c, int id);
    int* reserved;
    int reserved_count;
};

static int
alloc_free(int id, void* ud)
{
    struct alloc_ctx* ctx = ud;
    for( int i = 0; i < ctx->reserved_count; i++ )
    {
        if( ctx->reserved[i] == id )
            return 0;
    }
    return ctx->base_free(ctx->dst, id);
}

static int
alloc_reserve(
    struct alloc_ctx* ctx,
    int source_id,
    int** reserved,
    int* reserved_count,
    int* reserved_cap)
{
    ctx->reserved = *reserved;
    ctx->reserved_count = *reserved_count;
    int did = tool_alloc_id(source_id, alloc_free, ctx);
    if( did < 0 )
        return -1;
    add_unique_int(reserved, reserved_count, reserved_cap, did);
    return did;
}

static int
free_npc_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_npc_id_free(c, id);
}
static int
free_model_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_model_id_free(c, id);
}
static int
free_seq_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_seq_id_free(c, id);
}
static int
free_fm_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_framemap_id_free(c, id);
}
static int
free_arch_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_anim_archive_id_free(c, id);
}
static int
free_bas_base(struct Tool_Dat2Cache* c, int id)
{
    return tool_dat2_bas_id_free(c, id);
}

static int
build_dat2_plan(
    struct Tool_Dat2Cache* src,
    struct Tool_Dat2Cache* dst,
    int npc_id,
    int include_related,
    int emit_bas,
    struct Tool_PortPlan* plan)
{
    memset(plan, 0, sizeof(*plan));
    tool_id_map_init(&plan->model_map);
    tool_id_map_init(&plan->chathead_map);
    tool_id_map_init(&plan->seq_map);
    tool_id_map_init(&plan->framemap_map);
    tool_id_map_init(&plan->frame_archive_map);
    tool_id_map_init(&plan->frame_id_map);
    tool_id_map_init(&plan->bas_map);

    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(src, npc_id);
    if( !npc )
    {
        fprintf(stderr, "Source NPC %d not found\n", npc_id);
        return 1;
    }
    if( !tool_neutral_npc_from_dat2(src, npc, npc_id, &plan->npc) )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return 1;
    }
    RSCache_Dat2ConfigNpcFree(npc);

    int* seqs = NULL;
    int seq_count = 0;
    int seq_cap = 0;
    for( int i = 0; i < TOOL_ANIM_SLOT_COUNT; i++ )
    {
        if( plan->npc.anim_present[i] && plan->npc.anim[i] >= 0 )
            add_unique_int(&seqs, &seq_count, &seq_cap, plan->npc.anim[i]);
    }

    if( include_related )
    {
        struct Tool_AnimSeeds seeds;
        struct RSCache_Dat2ConfigNpc* n2 = tool_dat2_npc_load(src, npc_id);
        if( n2 )
        {
            tool_dat2_npc_anim_seeds(src, n2, &seeds);
            RSCache_Dat2ConfigNpcFree(n2);
            int seed_fms[64];
            int seed_fm_count = 0;
            for( int i = 0; i < seeds.count && seed_fm_count < 64; i++ )
            {
                struct RSCache_Dat2ConfigSequence* s = tool_dat2_seq_load(src, seeds.seq_ids[i]);
                if( !s )
                    continue;
                int fm = tool_dat2_seq_framemap_id(src, s, 0);
                RSCache_Dat2ConfigSequenceFree(s);
                if( fm < 0 )
                    continue;
                int dup = 0;
                for( int j = 0; j < seed_fm_count; j++ )
                    if( seed_fms[j] == fm )
                        dup = 1;
                if( !dup )
                    seed_fms[seed_fm_count++] = fm;
            }
            struct Tool_FramemapIndex index;
            if( tool_dat2_build_framemap_index(src, &index) )
            {
                int* cands = NULL;
                int cand_count = 0;
                tool_framemap_index_query(
                    &index, seed_fms, seed_fm_count, &cands, &cand_count);
                for( int i = 0; i < cand_count; i++ )
                    add_unique_int(&seqs, &seq_count, &seq_cap, cands[i]);
                free(cands);
                tool_framemap_index_free(&index);
            }
            tool_anim_seeds_free(&seeds);
        }
    }

    plan->related_seq_ids = seqs;
    plan->related_seq_count = seq_count;

    int* reserved = NULL;
    int reserved_count = 0;
    int reserved_cap = 0;
    struct alloc_ctx actx = { .dst = dst };

    actx.base_free = free_npc_base;
    int dest_npc = alloc_reserve(&actx, npc_id, &reserved, &reserved_count, &reserved_cap);
    if( dest_npc < 0 )
    {
        fprintf(stderr, "Failed to allocate destination NPC id\n");
        free(reserved);
        return 1;
    }
    if( dest_npc != npc_id )
        tool_port_plan_add_warning(plan, "npc id remapped %d -> %d", npc_id, dest_npc);
    plan->npc.source_id = dest_npc;

    actx.base_free = free_model_base;
    for( int i = 0; i < plan->npc.models_count; i++ )
    {
        int sid = plan->npc.models[i];
        int did = alloc_reserve(&actx, sid, &reserved, &reserved_count, &reserved_cap);
        if( did < 0 )
            continue;
        tool_id_map_put(&plan->model_map, sid, did);
        if( did != sid )
            tool_port_plan_add_warning(plan, "model id remapped %d -> %d", sid, did);
    }
    for( int i = 0; i < plan->npc.chathead_models_count; i++ )
    {
        int sid = plan->npc.chathead_models[i];
        int existing;
        if( tool_id_map_lookup(&plan->model_map, sid, &existing) )
        {
            tool_id_map_put(&plan->chathead_map, sid, existing);
            continue;
        }
        int did = alloc_reserve(&actx, sid, &reserved, &reserved_count, &reserved_cap);
        if( did < 0 )
            continue;
        tool_id_map_put(&plan->chathead_map, sid, did);
    }

    actx.base_free = free_seq_base;
    for( int i = 0; i < seq_count; i++ )
    {
        int sid = seqs[i];
        int did = alloc_reserve(&actx, sid, &reserved, &reserved_count, &reserved_cap);
        if( did < 0 )
            continue;
        tool_id_map_put(&plan->seq_map, sid, did);
        if( did != sid )
            tool_port_plan_add_warning(plan, "seq id remapped %d -> %d", sid, did);

        struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(src, sid);
        if( !seq )
            continue;
        int fm = tool_dat2_seq_framemap_id(src, seq, 0);
        if( fm >= 0 )
        {
            int existing;
            if( !tool_id_map_lookup(&plan->framemap_map, fm, &existing) )
            {
                actx.base_free = free_fm_base;
                int dfm = alloc_reserve(&actx, fm, &reserved, &reserved_count, &reserved_cap);
                actx.base_free = free_seq_base;
                if( dfm >= 0 )
                    tool_id_map_put(&plan->framemap_map, fm, dfm);
            }
        }
        if( seq->frame_ids && seq->frame_count > 0 )
        {
            int arch = (seq->frame_ids[0] >> 16) & 0xFFFF;
            int existing;
            if( !tool_id_map_lookup(&plan->frame_archive_map, arch, &existing) )
            {
                actx.base_free = free_arch_base;
                int da = alloc_reserve(&actx, arch, &reserved, &reserved_count, &reserved_cap);
                actx.base_free = free_seq_base;
                if( da >= 0 )
                    tool_id_map_put(&plan->frame_archive_map, arch, da);
            }
        }
        RSCache_Dat2ConfigSequenceFree(seq);
    }

    if( emit_bas && RSCache_IsRs2Dat2(&dst->profile) )
    {
        int bas_src = plan->npc.bas_present ? plan->npc.bas_type_id : dest_npc;
        actx.base_free = free_bas_base;
        int bas_dst = alloc_reserve(&actx, bas_src, &reserved, &reserved_count, &reserved_cap);
        if( bas_dst >= 0 )
            tool_id_map_put(&plan->bas_map, bas_src, bas_dst);
    }
    else if( emit_bas )
    {
        tool_port_plan_add_warning(plan, "--emit-bas ignored: destination is not RS2 dat2");
    }

    if( plan->npc.retexture_count > 0 )
        tool_port_plan_add_warning(
            plan, "retexture ids are cache-local and may not map across revisions");

    free(reserved);
    return 0;
}

static int
free_dat1_model_base(struct Tool_Dat1Cache* c, int id)
{
    return tool_dat1_model_id_free(c, id);
}

static int
free_dat1_anim_base(struct Tool_Dat1Cache* c, int id)
{
    return tool_dat1_anim_archive_id_free(c, id);
}

struct alloc_ctx_dat1
{
    struct Tool_Dat1Cache* dst;
    int (*base_free)(struct Tool_Dat1Cache* c, int id);
    int* reserved;
    int reserved_count;
};

static int
alloc_free_dat1(int id, void* ud)
{
    struct alloc_ctx_dat1* ctx = ud;
    for( int i = 0; i < ctx->reserved_count; i++ )
    {
        if( ctx->reserved[i] == id )
            return 0;
    }
    return ctx->base_free(ctx->dst, id);
}

static int
alloc_reserve_dat1(
    struct alloc_ctx_dat1* ctx,
    int source_id,
    int** reserved,
    int* reserved_count,
    int* reserved_cap)
{
    ctx->reserved = *reserved;
    ctx->reserved_count = *reserved_count;
    int did = tool_alloc_id(source_id, alloc_free_dat1, ctx);
    if( did < 0 )
        return -1;
    add_unique_int(reserved, reserved_count, reserved_cap, did);
    return did;
}

static int
build_dat1_plan(
    struct Tool_Dat2Cache* src,
    struct Tool_Dat1Cache* dst,
    int npc_id,
    int include_related,
    struct Tool_PortPlan* plan)
{
    memset(plan, 0, sizeof(*plan));
    tool_id_map_init(&plan->model_map);
    tool_id_map_init(&plan->chathead_map);
    tool_id_map_init(&plan->seq_map);
    tool_id_map_init(&plan->framemap_map);
    tool_id_map_init(&plan->frame_archive_map);
    tool_id_map_init(&plan->frame_id_map);
    tool_id_map_init(&plan->bas_map);

    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(src, npc_id);
    if( !npc )
    {
        fprintf(stderr, "Source NPC %d not found\n", npc_id);
        return 1;
    }
    if( !tool_neutral_npc_from_dat2(src, npc, npc_id, &plan->npc) )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return 1;
    }
    RSCache_Dat2ConfigNpcFree(npc);

    int* seqs = NULL;
    int seq_count = 0;
    int seq_cap = 0;
    for( int i = 0; i < TOOL_ANIM_SLOT_COUNT; i++ )
    {
        if( plan->npc.anim_present[i] && plan->npc.anim[i] >= 0 )
            add_unique_int(&seqs, &seq_count, &seq_cap, plan->npc.anim[i]);
    }

    if( include_related )
    {
        struct Tool_AnimSeeds seeds;
        struct RSCache_Dat2ConfigNpc* n2 = tool_dat2_npc_load(src, npc_id);
        if( n2 )
        {
            tool_dat2_npc_anim_seeds(src, n2, &seeds);
            RSCache_Dat2ConfigNpcFree(n2);
            int seed_fms[64];
            int seed_fm_count = 0;
            for( int i = 0; i < seeds.count && seed_fm_count < 64; i++ )
            {
                struct RSCache_Dat2ConfigSequence* s = tool_dat2_seq_load(src, seeds.seq_ids[i]);
                if( !s )
                    continue;
                int fm = tool_dat2_seq_framemap_id(src, s, 0);
                RSCache_Dat2ConfigSequenceFree(s);
                if( fm < 0 )
                    continue;
                int dup = 0;
                for( int j = 0; j < seed_fm_count; j++ )
                    if( seed_fms[j] == fm )
                        dup = 1;
                if( !dup )
                    seed_fms[seed_fm_count++] = fm;
            }
            struct Tool_FramemapIndex index;
            if( tool_dat2_build_framemap_index(src, &index) )
            {
                int* cands = NULL;
                int cand_count = 0;
                tool_framemap_index_query(
                    &index, seed_fms, seed_fm_count, &cands, &cand_count);
                for( int i = 0; i < cand_count; i++ )
                    add_unique_int(&seqs, &seq_count, &seq_cap, cands[i]);
                free(cands);
                tool_framemap_index_free(&index);
            }
            tool_anim_seeds_free(&seeds);
        }
    }

    plan->related_seq_ids = seqs;
    plan->related_seq_count = seq_count;

    int* reserved = NULL;
    int reserved_count = 0;
    int reserved_cap = 0;

    /* Dense npc.dat / seq.dat: always append at current count. */
    int next_npc = tool_dat1_npc_count(dst);
    int dest_npc = next_npc;
    add_unique_int(&reserved, &reserved_count, &reserved_cap, dest_npc);
    if( dest_npc != npc_id )
        tool_port_plan_add_warning(plan, "npc id remapped %d -> %d (dat1 append)", npc_id, dest_npc);
    plan->npc.source_id = dest_npc;

    struct alloc_ctx_dat1 actx = { .dst = dst };
    actx.base_free = free_dat1_model_base;
    for( int i = 0; i < plan->npc.models_count; i++ )
    {
        int sid = plan->npc.models[i];
        int did = alloc_reserve_dat1(&actx, sid, &reserved, &reserved_count, &reserved_cap);
        if( did < 0 )
            continue;
        tool_id_map_put(&plan->model_map, sid, did);
        if( did != sid )
            tool_port_plan_add_warning(plan, "model id remapped %d -> %d", sid, did);
    }
    for( int i = 0; i < plan->npc.chathead_models_count; i++ )
    {
        int sid = plan->npc.chathead_models[i];
        int existing;
        if( tool_id_map_lookup(&plan->model_map, sid, &existing) )
        {
            tool_id_map_put(&plan->chathead_map, sid, existing);
            continue;
        }
        int did = alloc_reserve_dat1(&actx, sid, &reserved, &reserved_count, &reserved_cap);
        if( did < 0 )
            continue;
        tool_id_map_put(&plan->chathead_map, sid, did);
        if( did != sid )
            tool_port_plan_add_warning(plan, "chathead model id remapped %d -> %d", sid, did);
    }

    int next_seq = tool_dat1_seq_count(dst);
    int next_frame = tool_dat1_max_frame_id(dst) + 1;
    if( next_frame < 0 )
        next_frame = 0;

    for( int i = 0; i < seq_count; i++ )
    {
        int sid = seqs[i];
        int did = next_seq++;
        add_unique_int(&reserved, &reserved_count, &reserved_cap, did);
        tool_id_map_put(&plan->seq_map, sid, did);
        if( did != sid )
            tool_port_plan_add_warning(plan, "seq id remapped %d -> %d (dat1 append)", sid, did);

        struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(src, sid);
        if( !seq )
            continue;
        int fm = tool_dat2_seq_framemap_id(src, seq, 0);
        if( fm >= 0 )
        {
            int existing;
            if( !tool_id_map_lookup(&plan->framemap_map, fm, &existing) )
            {
                /* Append at the first free anim-archive slot. Preferring the
                 * source framemap id (often 1000+) pads anim_version and makes
                 * the client sweep every prior archive before reaching ours. */
                actx.base_free = free_dat1_anim_base;
                int da = alloc_reserve_dat1(
                    &actx, 0, &reserved, &reserved_count, &reserved_cap);
                actx.base_free = free_dat1_model_base;
                if( da >= 0 )
                {
                    tool_id_map_put(&plan->framemap_map, fm, da);
                    if( da != fm )
                        tool_port_plan_add_warning(
                            plan, "anim archive id remapped framemap %d -> %d", fm, da);
                }
            }
        }
        if( seq->frame_ids && seq->frame_count > 0 )
        {
            for( int f = 0; f < seq->frame_count; f++ )
            {
                int composite = seq->frame_ids[f];
                int existing;
                if( tool_id_map_lookup(&plan->frame_id_map, composite, &existing) )
                    continue;
                if( next_frame > 65535 )
                {
                    tool_port_plan_add_warning(
                        plan,
                        "dat1 frame id space exhausted (>65535); remaining frames skipped");
                    break;
                }
                tool_id_map_put(&plan->frame_id_map, composite, next_frame++);
            }
        }
        RSCache_Dat2ConfigSequenceFree(seq);
    }

    if( plan->npc.retexture_count > 0 )
        tool_port_plan_add_warning(
            plan, "retexture ids are cache-local and may not map across revisions");

    free(reserved);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* from_rev = NULL;
    const char* to_rev = NULL;
    const char* src_dir = NULL;
    const char* dst_dir = NULL;
    const char* out_dir = NULL;
    const char* json_path = NULL;
    int npc_id = -1;
    int apply = 0;
    int include_related = 0;
    int emit_bas = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--from-rev") == 0 && i + 1 < argc )
            from_rev = argv[++i];
        else if( strcmp(argv[i], "--to-rev") == 0 && i + 1 < argc )
            to_rev = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            npc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_dir = argv[++i];
        else if( strcmp(argv[i], "--json") == 0 && i + 1 < argc )
            json_path = argv[++i];
        else if( strcmp(argv[i], "--apply") == 0 )
            apply = 1;
        else if( strcmp(argv[i], "--include-related-anims") == 0 )
            include_related = 1;
        else if( strcmp(argv[i], "--emit-bas") == 0 )
            emit_bas = 1;
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
        else if( !src_dir )
            src_dir = argv[i];
        else if( !dst_dir )
            dst_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    if( !from_rev || !to_rev || !src_dir || !dst_dir || npc_id < 0 )
    {
        usage(argv[0]);
        return 1;
    }

    struct RSCache from_profile;
    struct RSCache to_profile;
    if( !tool_resolve_profile(from_rev, NULL, NULL, NULL, NULL, &from_profile) )
        return 1;
    if( !tool_resolve_profile(to_rev, NULL, NULL, NULL, NULL, &to_profile) )
        return 1;

    if( is_osrs239(&from_profile) || is_osrs239(&to_profile) )
    {
        fprintf(
            stderr,
            "error: osrs239 NPC records do not decode exactly; refuse to port\n"
            "(see 3rd/rscache/README.md known decode gaps)\n");
        return 1;
    }

    tool_print_profile(src_dir, &from_profile);
    tool_print_profile(dst_dir, &to_profile);

    if( from_profile.epoch != RSCACHE_EPOCH_DAT2 )
    {
        fprintf(stderr, "error: source must be dat2 for this build of port_npc planning\n");
        fprintf(stderr, "(dat1 source support uses the same commit path when planned)\n");
        /* Allow dat1 source with limited support */
    }

    if( from_profile.epoch == RSCACHE_EPOCH_DAT2 && to_profile.epoch == RSCACHE_EPOCH_DAT2 )
    {
        struct Tool_Dat2Cache src;
        struct Tool_Dat2Cache dst;
        if( !tool_dat2_open(src_dir, &from_profile, &src) )
            return 1;
        if( !tool_dat2_open(dst_dir, &to_profile, &dst) )
        {
            tool_dat2_close(&src);
            return 1;
        }

        struct Tool_PortPlan plan;
        if( build_dat2_plan(&src, &dst, npc_id, include_related, emit_bas, &plan) != 0 )
        {
            tool_dat2_close(&src);
            tool_dat2_close(&dst);
            return 1;
        }

        tool_port_plan_print(&plan);
        if( json_path )
            tool_port_plan_write_json(&plan, json_path);

        if( !apply )
        {
            printf("# dry-run: pass --apply to write (prefer --out DIR)\n");
            tool_port_plan_free(&plan);
            tool_dat2_close(&src);
            tool_dat2_close(&dst);
            return 0;
        }

        const char* write_dir = dst_dir;
        char tmp_opened = 0;
        struct Tool_Dat2Cache write_cache = dst;
        if( out_dir )
        {
            if( tool_copy_cache_dir(dst_dir, out_dir) != 0 )
            {
                fprintf(stderr, "Failed to copy cache to %s\n", out_dir);
                tool_port_plan_free(&plan);
                tool_dat2_close(&src);
                tool_dat2_close(&dst);
                return 1;
            }
            tool_dat2_close(&dst);
            if( !tool_dat2_open(out_dir, &to_profile, &write_cache) )
            {
                tool_port_plan_free(&plan);
                tool_dat2_close(&src);
                return 1;
            }
            write_dir = out_dir;
            tmp_opened = 1;
        }

        int rc = tool_port_commit_dat2(&src, &write_cache, write_dir, &plan, emit_bas);
        if( rc == 0 )
            printf("Wrote NPC %d -> %d into %s\n", npc_id, plan.npc.source_id, write_dir);
        else
            fprintf(stderr, "Commit failed\n");

        tool_port_plan_free(&plan);
        tool_dat2_close(&src);
        if( tmp_opened )
            tool_dat2_close(&write_cache);
        else
            tool_dat2_close(&dst);
        return rc;
    }

    if( from_profile.epoch == RSCACHE_EPOCH_DAT2 && to_profile.epoch == RSCACHE_EPOCH_DAT1 )
    {
        struct Tool_Dat2Cache src;
        struct Tool_Dat1Cache dst;
        if( !tool_dat2_open(src_dir, &from_profile, &src) )
            return 1;
        if( !tool_dat1_open(dst_dir, &to_profile, &dst) )
        {
            tool_dat2_close(&src);
            return 1;
        }

        struct Tool_PortPlan plan;
        if( build_dat1_plan(&src, &dst, npc_id, include_related, &plan) != 0 )
        {
            tool_dat2_close(&src);
            tool_dat1_close(&dst);
            return 1;
        }

        tool_port_plan_print(&plan);
        if( json_path )
            tool_port_plan_write_json(&plan, json_path);

        if( !apply )
        {
            printf("# dry-run: pass --apply to write (prefer --out DIR)\n");
            tool_port_plan_free(&plan);
            tool_dat2_close(&src);
            tool_dat1_close(&dst);
            return 0;
        }

        const char* write_dir = dst_dir;
        if( out_dir )
        {
            if( tool_copy_cache_dir(dst_dir, out_dir) != 0 )
            {
                fprintf(stderr, "Failed to copy cache to %s\n", out_dir);
                tool_port_plan_free(&plan);
                tool_dat2_close(&src);
                tool_dat1_close(&dst);
                return 1;
            }
            tool_dat1_close(&dst);
            if( !tool_dat1_open(out_dir, &to_profile, &dst) )
            {
                tool_port_plan_free(&plan);
                tool_dat2_close(&src);
                return 1;
            }
            write_dir = out_dir;
        }

        int rc = tool_port_commit_dat1(&src, NULL, &dst, write_dir, &plan);
        if( rc == 0 )
            printf(
                "Wrote NPC %d -> %d into dat1 cache %s\n",
                npc_id,
                plan.npc.source_id,
                write_dir);
        else
            fprintf(stderr, "Commit failed\n");
        tool_port_plan_free(&plan);
        tool_dat2_close(&src);
        tool_dat1_close(&dst);
        return rc;
    }

    fprintf(
        stderr,
        "Unsupported epoch pair: source=%s dest=%s\n",
        RSCache_EpochName(from_profile.epoch),
        RSCache_EpochName(to_profile.epoch));
    return 1;
}
