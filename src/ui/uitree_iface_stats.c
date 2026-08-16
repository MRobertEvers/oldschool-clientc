#include "ui/uitree_iface_stats.h"

#include "perf/torirs_perf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IFACE_STATS_MAX 128

struct IfaceGroupStat
{
    int group_id;
    int opens;
    int closes;
    int bakes;
    int bake_reuses;
    uint32_t live;
    uint32_t hidden;
    uint32_t hook_blocks;
    uint32_t hook_bytes;
};

static int g_enabled = -1;
static struct IfaceGroupStat g_stats[IFACE_STATS_MAX];
static int g_stat_count;

static int
iface_stats_enabled(void)
{
    if( g_enabled < 0 )
        g_enabled = getenv("TORIRS_IFACE_STATS") != NULL;
    return g_enabled;
}

static struct IfaceGroupStat*
iface_stats_find_or_add(int group_id)
{
    int i;
    assert(group_id > 0);
    for( i = 0; i < g_stat_count; i++ )
    {
        if( g_stats[i].group_id == group_id )
            return &g_stats[i];
    }
    if( g_stat_count >= IFACE_STATS_MAX )
        return NULL;
    memset(&g_stats[g_stat_count], 0, sizeof(g_stats[0]));
    g_stats[g_stat_count].group_id = group_id;
    return &g_stats[g_stat_count++];
}

void
UITreeIfaceStats_NoteOpen(int group_id)
{
    struct IfaceGroupStat* s;
    if( !iface_stats_enabled() || group_id <= 0 )
        return;
    s = iface_stats_find_or_add(group_id);
    if( s )
        s->opens++;
}

void
UITreeIfaceStats_NoteClose(int group_id)
{
    struct IfaceGroupStat* s;
    if( !iface_stats_enabled() || group_id <= 0 )
        return;
    s = iface_stats_find_or_add(group_id);
    if( s )
        s->closes++;
}

void
UITreeIfaceStats_NoteBake(int group_id)
{
    struct IfaceGroupStat* s;
    if( !iface_stats_enabled() || group_id <= 0 )
        return;
    s = iface_stats_find_or_add(group_id);
    if( s )
        s->bakes++;
}

void
UITreeIfaceStats_NoteBakeReuse(int group_id)
{
    struct IfaceGroupStat* s;
    if( !iface_stats_enabled() || group_id <= 0 )
        return;
    s = iface_stats_find_or_add(group_id);
    if( s )
        s->bake_reuses++;
}

void
UITreeIfaceStats_SampleGauges(struct UITree const* tree)
{
    uint32_t hidden = 0;
    uint32_t unmounted = 0;
    uint32_t freed = 0;
    uint32_t hook_blocks = 0;
    uint32_t groups_seen[IFACE_STATS_MAX];
    int groups_n = 0;
    uint32_t i;

    assert(tree);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int group;
        int g;
        if( c->freed )
        {
            freed++;
            continue;
        }
        if( c->component_id < 0 )
            continue;
        if( c->behavior.hide )
            hidden++;
        if( c->behavior.hide_unmounted )
            unmounted++;
        if( c->runtime_hooks )
            hook_blocks++;
        group = (c->component_id >> 16) & 0xffff;
        if( group <= 0 )
            continue;
        for( g = 0; g < groups_n; g++ )
        {
            if( groups_seen[g] == (uint32_t)group )
                break;
        }
        if( g == groups_n && groups_n < IFACE_STATS_MAX )
            groups_seen[groups_n++] = (uint32_t)group;
    }

    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_IFACE_GROUPS_RESIDENT, groups_n);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_HIDDEN, (int64_t)hidden);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_UNMOUNTED, (int64_t)unmounted);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_FREED, (int64_t)freed);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_HOOK_BLOCKS, (int64_t)hook_blocks);
    TORIRS_PERF_COUNT_SET(
        TORIRS_PERF_CTR_UITREE_HOOK_BYTES,
        (int64_t)hook_blocks * (int64_t)sizeof(struct UITreeRuntimeHooks));
}

void
UITreeIfaceStats_Tick(
    struct UITree const* tree,
    int tick)
{
    uint32_t i;
    int g;

    assert(tree);
    if( !iface_stats_enabled() )
        return;
    if( tick % 250 != 0 )
        return;

    for( g = 0; g < g_stat_count; g++ )
    {
        g_stats[g].live = 0;
        g_stats[g].hidden = 0;
        g_stats[g].hook_blocks = 0;
        g_stats[g].hook_bytes = 0;
    }

    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int group;
        struct IfaceGroupStat* s;
        if( c->freed || c->component_id < 0 )
            continue;
        group = (c->component_id >> 16) & 0xffff;
        if( group <= 0 )
            continue;
        s = iface_stats_find_or_add(group);
        if( !s )
            continue;
        s->live++;
        if( c->behavior.hide )
            s->hidden++;
        if( c->runtime_hooks )
        {
            s->hook_blocks++;
            s->hook_bytes += (uint32_t)sizeof(struct UITreeRuntimeHooks);
        }
    }

    fprintf(stderr, "torirs_iface_stats: tick=%d groups=%d\n", tick, g_stat_count);
    for( g = 0; g < g_stat_count; g++ )
    {
        struct IfaceGroupStat const* s = &g_stats[g];
        fprintf(
            stderr,
            "  group=%d opens=%d closes=%d bakes=%d reuse=%d "
            "live=%u hidden=%u hooks=%u hook_bytes=%u\n",
            s->group_id,
            s->opens,
            s->closes,
            s->bakes,
            s->bake_reuses,
            s->live,
            s->hidden,
            s->hook_blocks,
            s->hook_bytes);
    }
}
