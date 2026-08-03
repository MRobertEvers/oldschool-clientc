#include "perf/torirs_perf.h"

#ifndef TORIRS_PERF_DISABLE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_torirs_perf_enabled = 0;

#define TORIRS_PERF_RING 2048
#define TORIRS_PERF_BUDGET_NS 20000000ull /* 20 ms */
#define TORIRS_PERF_WINDOW_DEFAULT 1000

struct TorirsPerfFrame
{
    uint64_t stage_ns[TORIRS_PERF_STAGE_COUNT];
    int64_t counters[TORIRS_PERF_CTR_COUNT];
    uint64_t total_ns;
};

static struct TorirsPerfFrame g_cur;
static struct TorirsPerfFrame g_ring[TORIRS_PERF_RING];
static int g_ring_count;
static int g_ring_head;
static uint64_t g_frame_begin_ns;
static uint64_t g_stage_begin_ns[TORIRS_PERF_STAGE_COUNT];
static int g_stage_depth[TORIRS_PERF_STAGE_COUNT];
static uint64_t g_total_frames;
static uint64_t g_frames_over_budget;
static char g_csv_path[512];
static int g_window_frames;
static int g_window_index;
static FILE* g_window_csv;
static int g_window_csv_header_written;

static char const* const g_stage_names[TORIRS_PERF_STAGE_COUNT] = {
    "frame",    "async",  "logic",  "cs2",     "layout", "interact",
    "emit",     "paint",  "build",  "render",  "present", "server",
};

static char const* const g_ctr_names[TORIRS_PERF_CTR_COUNT] = {
    "uitree_find_id",
    "uitree_find_id_probes",
    "uitree_find_id_linear",
    "uitree_id_rebuild",
    "uitree_find_child",
    "uitree_find_child_hit",
    "uitree_find_child_ceil_miss",
    "uitree_walk_emit",
    "uitree_walk_emit_drag",
    "uitree_walk_hit",
    "uitree_walk_hover",
    "uitree_walk_drop",
    "uitree_emit_skip",
    "uitree_layout_resolve",
    "uitree_layout_skip",
    "uitree_layout_nodes",
    "uitree_layout_node_skip",
    "uitree_layout_depth_recompute",
    "uitree_ensure_layout",
    "uitree_cc_create",
    "uitree_cc_delete",
    "uitree_cc_deleteall",
    "uitree_cc_deleteall_rows",
    "uitree_apply_geo",
    "uitree_apply_content",
    "uitree_apply_hook",
    "uitree_apply_other",
    "uitree_key_scan",
    "uitree_key_scan_nodes",
    "uitree_components",
    "uitree_capacity",
    "uitree_free_list",
    "uitree_node_bytes",
    "cs2_scripts",
    "cs2_opcodes",
    "cs2_host_ops",
    "cs2_cycles",
    "cs2_aborts",
    "cs2_vm_acquire",
    "cs2_vm_pool_hit",
    "cs2_vm_pool_miss",
    "cs2_vm_init_ns",
    "cs2_frame_pool_hit",
    "cs2_frame_pool_miss",
    "cache_model_hit",
    "cache_model_miss",
    "cache_model_evict",
    "cache_sprite_hit",
    "cache_sprite_miss",
    "cache_sprite_evict",
    "cache_texture_hit",
    "cache_texture_miss",
    "cache_objtype_hit",
    "cache_objtype_miss",
    "cache_npctype_hit",
    "cache_npctype_miss",
    "cache_loc_hit",
    "cache_loc_miss",
    "cache_other_hit",
    "cache_other_miss",
    "cache_group_hit",
    "cache_group_miss",
    "cache_group_evict",
    "cache_archive_hit",
    "cache_archive_miss",
    "cache_archive_evict",
    "model_inst_hit",
    "model_inst_miss",
    "model_inst_evict",
    "painter_pops",
    "painter_gate_rejects",
    "painter_pushes",
    "painter_push_dedup",
    "painter_drain_events",
    "painter_commands",
    "painter_tiles_remaining_set",
    "scene_elements",
    "scene_anim_list",
    "zone_map_count",
    "zone_map_capacity",
    "npc_slot_max",
    "cache_model_size",
    "cache_sprite_size",
    "iface_open",
    "iface_close",
    "iface_bake",
    "iface_bake_reuse",
    "iface_mount_ns",
    "iface_close_ns",
    "iface_groups_resident",
    "uitree_hidden",
    "uitree_unmounted",
    "uitree_freed",
    "uitree_hook_blocks",
    "uitree_hook_bytes",
    "host_inv_hooks",
    "host_var_hooks",
    "host_stat_hooks",
    "bridge_sprite_map",
    "bridge_model_map",
    "bridge_obj_icon_map",
    "cache_clientscript_size",
    "uitree_anim_scan",
    "uitree_anim_scan_nodes",
    "uitree_anim_model_nodes",
    "uitree_wheel_scan_nodes",
    "uitree_opkey_scan_nodes",
    "uitree_hook_index_rebuild_nodes",
};

/* COUNT_SET gauges: window flush reports last sample, not a sum. */
static int
ctr_is_gauge(enum TorirsPerfCounter ctr)
{
    switch( ctr )
    {
    case TORIRS_PERF_CTR_UITREE_COMPONENTS:
    case TORIRS_PERF_CTR_UITREE_CAPACITY:
    case TORIRS_PERF_CTR_UITREE_FREE_LIST:
    case TORIRS_PERF_CTR_UITREE_NODE_BYTES:
    case TORIRS_PERF_CTR_PAINTER_COMMANDS:
    case TORIRS_PERF_CTR_PAINTER_TILES_REMAINING_SET:
    case TORIRS_PERF_CTR_SCENE_ELEMENTS:
    case TORIRS_PERF_CTR_SCENE_ANIM_LIST:
    case TORIRS_PERF_CTR_ZONE_MAP_COUNT:
    case TORIRS_PERF_CTR_ZONE_MAP_CAPACITY:
    case TORIRS_PERF_CTR_NPC_SLOT_MAX:
    case TORIRS_PERF_CTR_CACHE_MODEL_SIZE:
    case TORIRS_PERF_CTR_CACHE_SPRITE_SIZE:
    case TORIRS_PERF_CTR_IFACE_GROUPS_RESIDENT:
    case TORIRS_PERF_CTR_UITREE_HIDDEN:
    case TORIRS_PERF_CTR_UITREE_UNMOUNTED:
    case TORIRS_PERF_CTR_UITREE_FREED:
    case TORIRS_PERF_CTR_UITREE_HOOK_BLOCKS:
    case TORIRS_PERF_CTR_UITREE_HOOK_BYTES:
    case TORIRS_PERF_CTR_HOST_INV_HOOKS:
    case TORIRS_PERF_CTR_HOST_VAR_HOOKS:
    case TORIRS_PERF_CTR_HOST_STAT_HOOKS:
    case TORIRS_PERF_CTR_BRIDGE_SPRITE_MAP:
    case TORIRS_PERF_CTR_BRIDGE_MODEL_MAP:
    case TORIRS_PERF_CTR_BRIDGE_OBJ_ICON_MAP:
    case TORIRS_PERF_CTR_CACHE_CLIENTSCRIPT_SIZE:
        return 1;
    default:
        return 0;
    }
}

static uint64_t
torirs_perf_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
window_csv_open(void)
{
    char path[560];
    size_t n;

    if( !g_csv_path[0] || g_window_csv )
        return;
    n = strlen(g_csv_path);
    if( n + 16 >= sizeof(path) )
        return;
    memcpy(path, g_csv_path, n);
    memcpy(path + n, ".windows.csv", 13);
    g_window_csv = fopen(path, "w");
    if( !g_window_csv )
    {
        fprintf(stderr, "torirs_perf: failed to open window csv %s\n", path);
        return;
    }
    fprintf(
        g_window_csv,
        "kind,name,mean_ns,p50_ns,p95_ns,max_ns,total,per_frame,window\n");
    g_window_csv_header_written = 1;
    fprintf(stderr, "torirs_perf: window=%d writing %s\n", g_window_frames, path);
}

static int
cmp_u64(void const* a, void const* b)
{
    uint64_t va = *(uint64_t const*)a;
    uint64_t vb = *(uint64_t const*)b;
    return (va > vb) - (va < vb);
}

static void
percentile(uint64_t* sorted, int n, double p, uint64_t* out)
{
    int idx;
    if( n <= 0 )
    {
        *out = 0;
        return;
    }
    idx = (int)(p * (double)(n - 1) + 0.5);
    if( idx < 0 )
        idx = 0;
    if( idx >= n )
        idx = n - 1;
    *out = sorted[idx];
}

/* Stats over the most recent `n` frames in the ring (n <= g_ring_count). */
static void
stage_stats_tail(
    enum TorirsPerfStage stage,
    int n,
    uint64_t* mean_out,
    uint64_t* p50_out,
    uint64_t* p95_out,
    uint64_t* max_out)
{
    static uint64_t buf[TORIRS_PERF_RING];
    int i;
    uint64_t sum = 0;
    uint64_t mx = 0;

    if( n > g_ring_count )
        n = g_ring_count;
    if( n <= 0 )
    {
        *mean_out = *p50_out = *p95_out = *max_out = 0;
        return;
    }
    /* Ring head points at next write; last written is (head - 1). */
    for( i = 0; i < n; i++ )
    {
        int idx = g_ring_head - 1 - i;
        if( idx < 0 )
            idx += TORIRS_PERF_RING;
        {
            uint64_t v = g_ring[idx].stage_ns[stage];
            buf[i] = v;
            sum += v;
            if( v > mx )
                mx = v;
        }
    }
    qsort(buf, (size_t)n, sizeof(buf[0]), cmp_u64);
    *mean_out = sum / (uint64_t)n;
    percentile(buf, n, 0.50, p50_out);
    percentile(buf, n, 0.95, p95_out);
    *max_out = mx;
}

static void
stage_stats(
    enum TorirsPerfStage stage,
    uint64_t* mean_out,
    uint64_t* p50_out,
    uint64_t* p95_out,
    uint64_t* max_out)
{
    static uint64_t buf[TORIRS_PERF_RING];
    int i;
    int n = g_ring_count;
    uint64_t sum = 0;
    uint64_t mx = 0;

    for( i = 0; i < n; i++ )
    {
        uint64_t v = g_ring[i].stage_ns[stage];
        buf[i] = v;
        sum += v;
        if( v > mx )
            mx = v;
    }
    qsort(buf, (size_t)n, sizeof(buf[0]), cmp_u64);
    *mean_out = n ? sum / (uint64_t)n : 0;
    percentile(buf, n, 0.50, p50_out);
    percentile(buf, n, 0.95, p95_out);
    *max_out = mx;
}

static void
ctr_totals(enum TorirsPerfCounter ctr, int64_t* total_out, double* mean_out)
{
    int i;
    int n = g_ring_count;
    int64_t sum = 0;
    for( i = 0; i < n; i++ )
        sum += g_ring[i].counters[ctr];
    *total_out = sum;
    *mean_out = n ? (double)sum / (double)n : 0.0;
}

static void
ctr_window(
    enum TorirsPerfCounter ctr,
    int n,
    int64_t* total_or_last_out,
    double* per_frame_out)
{
    int i;
    int64_t sum = 0;
    int64_t last = 0;

    if( n > g_ring_count )
        n = g_ring_count;
    if( n <= 0 )
    {
        *total_or_last_out = 0;
        *per_frame_out = 0.0;
        return;
    }
    for( i = 0; i < n; i++ )
    {
        int idx = g_ring_head - 1 - i;
        if( idx < 0 )
            idx += TORIRS_PERF_RING;
        last = g_ring[idx].counters[ctr];
        sum += last;
    }
    /* For gauges the first iteration of the reverse walk is the oldest;
     * re-read the most recent frame. */
    {
        int idx = g_ring_head - 1;
        if( idx < 0 )
            idx += TORIRS_PERF_RING;
        last = g_ring[idx].counters[ctr];
    }
    if( ctr_is_gauge(ctr) )
    {
        *total_or_last_out = last;
        *per_frame_out = (double)last;
    }
    else
    {
        *total_or_last_out = sum;
        *per_frame_out = (double)sum / (double)n;
    }
}

static void
window_flush(void)
{
    int i;
    int n = g_window_frames;
    uint64_t mean, p50, p95, mx;

    if( g_window_frames <= 0 )
        return;
    if( !g_window_csv )
        window_csv_open();
    if( !g_window_csv )
        return;

    for( i = 0; i < TORIRS_PERF_STAGE_COUNT; i++ )
    {
        stage_stats_tail((enum TorirsPerfStage)i, n, &mean, &p50, &p95, &mx);
        fprintf(
            g_window_csv,
            "window_stage,%s,%llu,%llu,%llu,%llu,,,%d\n",
            g_stage_names[i],
            (unsigned long long)mean,
            (unsigned long long)p50,
            (unsigned long long)p95,
            (unsigned long long)mx,
            g_window_index);
    }
    for( i = 0; i < TORIRS_PERF_CTR_COUNT; i++ )
    {
        int64_t total;
        double per;
        char const* kind;
        ctr_window((enum TorirsPerfCounter)i, n, &total, &per);
        if( total == 0 && per == 0.0 )
            continue;
        kind = ctr_is_gauge((enum TorirsPerfCounter)i) ? "window_gauge" : "window_counter";
        fprintf(
            g_window_csv,
            "%s,%s,,,,,%lld,%.6f,%d\n",
            kind,
            g_ctr_names[i],
            (long long)total,
            per,
            g_window_index);
    }
    fflush(g_window_csv);

    /* Brief stderr slope line for interactive runs. */
    stage_stats_tail(TORIRS_PERF_STAGE_FRAME, n, &mean, &p50, &p95, &mx);
    fprintf(
        stderr,
        "torirs_perf: window=%d frame_p95=%.2fms\n",
        g_window_index,
        (double)p95 / 1e6);

    g_window_index++;
}

void
TorirsPerf_Init(int enabled)
{
    char const* env;
    char const* csv;
    char const* win;

    memset(&g_cur, 0, sizeof(g_cur));
    memset(g_ring, 0, sizeof(g_ring));
    memset(g_stage_begin_ns, 0, sizeof(g_stage_begin_ns));
    memset(g_stage_depth, 0, sizeof(g_stage_depth));
    g_ring_count = 0;
    g_ring_head = 0;
    g_total_frames = 0;
    g_frames_over_budget = 0;
    g_csv_path[0] = '\0';
    g_window_index = 0;
    g_window_csv = NULL;
    g_window_csv_header_written = 0;
    g_window_frames = TORIRS_PERF_WINDOW_DEFAULT;

    env = getenv("TORIRS_PERF");
    if( enabled || (env && env[0] != '\0' && env[0] != '0') )
        g_torirs_perf_enabled = 1;
    else
        g_torirs_perf_enabled = 0;

    csv = getenv("TORIRS_PERF_CSV");
    if( csv && csv[0] )
    {
        size_t n = strlen(csv);
        if( n >= sizeof(g_csv_path) )
            n = sizeof(g_csv_path) - 1;
        memcpy(g_csv_path, csv, n);
        g_csv_path[n] = '\0';
    }

    win = getenv("TORIRS_PERF_WINDOW");
    if( win && win[0] )
    {
        int v = atoi(win);
        if( v > 0 )
        {
            if( v > TORIRS_PERF_RING )
                v = TORIRS_PERF_RING;
            g_window_frames = v;
        }
    }

    if( g_torirs_perf_enabled )
        fprintf(
            stderr,
            "torirs_perf: enabled (ring=%d window=%d budget=20ms)%s%s\n",
            TORIRS_PERF_RING,
            g_window_frames,
            g_csv_path[0] ? " csv=" : "",
            g_csv_path[0] ? g_csv_path : "");
}

void
TorirsPerf_Shutdown(void)
{
    if( g_torirs_perf_enabled )
        TorirsPerf_Report();
    if( g_window_csv )
    {
        fclose(g_window_csv);
        g_window_csv = NULL;
    }
    g_torirs_perf_enabled = 0;
}

void
TorirsPerf_FrameBegin(void)
{
    assert(g_torirs_perf_enabled);
    memset(&g_cur, 0, sizeof(g_cur));
    memset(g_stage_depth, 0, sizeof(g_stage_depth));
    g_frame_begin_ns = torirs_perf_now_ns();
    TorirsPerf_StageBegin(TORIRS_PERF_STAGE_FRAME);
}

void
TorirsPerf_FrameEnd(void)
{
    int i;
    assert(g_torirs_perf_enabled);

    if( g_stage_depth[TORIRS_PERF_STAGE_FRAME] > 0 )
        TorirsPerf_StageEnd(TORIRS_PERF_STAGE_FRAME);

    g_cur.total_ns = torirs_perf_now_ns() - g_frame_begin_ns;
    if( g_cur.stage_ns[TORIRS_PERF_STAGE_FRAME] == 0 )
        g_cur.stage_ns[TORIRS_PERF_STAGE_FRAME] = g_cur.total_ns;

    g_ring[g_ring_head] = g_cur;
    g_ring_head = (g_ring_head + 1) % TORIRS_PERF_RING;
    if( g_ring_count < TORIRS_PERF_RING )
        g_ring_count++;
    g_total_frames++;
    if( g_cur.total_ns > TORIRS_PERF_BUDGET_NS )
        g_frames_over_budget++;

    for( i = 0; i < TORIRS_PERF_STAGE_COUNT; i++ )
        g_stage_depth[i] = 0;

    if( g_window_frames > 0 && g_csv_path[0] &&
        (g_total_frames % (uint64_t)g_window_frames) == 0 )
        window_flush();
}

void
TorirsPerf_StageBegin(enum TorirsPerfStage stage)
{
    assert(stage >= 0 && stage < TORIRS_PERF_STAGE_COUNT);
    if( !g_torirs_perf_enabled )
        return;
    g_stage_depth[stage]++;
    if( g_stage_depth[stage] == 1 )
        g_stage_begin_ns[stage] = torirs_perf_now_ns();
}

void
TorirsPerf_StageEnd(enum TorirsPerfStage stage)
{
    assert(stage >= 0 && stage < TORIRS_PERF_STAGE_COUNT);
    if( !g_torirs_perf_enabled )
        return;
    if( g_stage_depth[stage] <= 0 )
        return;
    g_stage_depth[stage]--;
    if( g_stage_depth[stage] == 0 )
        g_cur.stage_ns[stage] += torirs_perf_now_ns() - g_stage_begin_ns[stage];
}

void
TorirsPerf_Count(enum TorirsPerfCounter counter, int64_t n)
{
    assert(counter >= 0 && counter < TORIRS_PERF_CTR_COUNT);
    if( !g_torirs_perf_enabled )
        return;
    g_cur.counters[counter] += n;
}

void
TorirsPerf_CountSet(enum TorirsPerfCounter counter, int64_t n)
{
    assert(counter >= 0 && counter < TORIRS_PERF_CTR_COUNT);
    if( !g_torirs_perf_enabled )
        return;
    g_cur.counters[counter] = n;
}

void
TorirsPerf_Report(void)
{
    int i;
    uint64_t mean, p50, p95, mx;
    uint64_t frame_mean, frame_p50, frame_p95, frame_max;
    double fps;
    FILE* csv = NULL;
    uint64_t attributed = 0;

    if( g_ring_count <= 0 )
    {
        fprintf(stderr, "torirs_perf: no frames captured\n");
        return;
    }

    /* Flush a partial final window so long runs still leave a last sample. */
    if( g_window_frames > 0 && g_csv_path[0] &&
        (g_total_frames % (uint64_t)g_window_frames) != 0 && g_total_frames > 0 )
    {
        int saved = g_window_frames;
        int rem = (int)(g_total_frames % (uint64_t)g_window_frames);
        if( rem > 0 )
        {
            g_window_frames = rem;
            window_flush();
            g_window_frames = saved;
        }
    }

    stage_stats(TORIRS_PERF_STAGE_FRAME, &frame_mean, &frame_p50, &frame_p95,
                &frame_max);
    fps = frame_mean > 0 ? 1e9 / (double)frame_mean : 0.0;

    fprintf(stderr, "\n=== torirs_perf report ===\n");
    fprintf(stderr, "frames=%llu ring=%d over_20ms=%llu (%.1f%%) eff_fps=%.1f\n",
            (unsigned long long)g_total_frames, g_ring_count,
            (unsigned long long)g_frames_over_budget,
            g_total_frames
                ? 100.0 * (double)g_frames_over_budget / (double)g_total_frames
                : 0.0,
            fps);
    fprintf(stderr,
            "frame_ns mean=%llu p50=%llu p95=%llu max=%llu  (budget=20000000)\n",
            (unsigned long long)frame_mean, (unsigned long long)frame_p50,
            (unsigned long long)frame_p95, (unsigned long long)frame_max);

    fprintf(stderr, "\n%-12s %10s %10s %10s %10s\n", "stage", "mean_ns", "p50_ns",
            "p95_ns", "max_ns");
    for( i = 1; i < TORIRS_PERF_STAGE_COUNT; i++ )
    {
        stage_stats((enum TorirsPerfStage)i, &mean, &p50, &p95, &mx);
        attributed += mean;
        fprintf(stderr, "%-12s %10llu %10llu %10llu %10llu\n", g_stage_names[i],
                (unsigned long long)mean, (unsigned long long)p50,
                (unsigned long long)p95, (unsigned long long)mx);
    }
    fprintf(stderr, "%-12s %10lld  (frame_mean - attributed stages)\n",
            "residual", (long long)((int64_t)frame_mean - (int64_t)attributed));

    fprintf(stderr, "\n%-32s %12s %12s\n", "counter", "total", "per_frame");
    for( i = 0; i < TORIRS_PERF_CTR_COUNT; i++ )
    {
        int64_t total;
        double per;
        ctr_totals((enum TorirsPerfCounter)i, &total, &per);
        if( total == 0 )
            continue;
        fprintf(stderr, "%-32s %12lld %12.2f\n", g_ctr_names[i],
                (long long)total, per);
    }

    if( g_csv_path[0] )
    {
        csv = fopen(g_csv_path, "w");
        if( !csv )
        {
            fprintf(stderr, "torirs_perf: failed to open csv %s\n", g_csv_path);
        }
        else
        {
            fprintf(csv, "kind,name,mean_ns,p50_ns,p95_ns,max_ns,total,per_frame\n");
            fprintf(csv, "meta,total_frames,,,,,,%llu,\n",
                    (unsigned long long)g_total_frames);
            fprintf(csv, "meta,frames_over_20ms,,,,,,%llu,\n",
                    (unsigned long long)g_frames_over_budget);
            fprintf(csv, "meta,eff_fps,,,,,,%.3f,\n", fps);
            for( i = 0; i < TORIRS_PERF_STAGE_COUNT; i++ )
            {
                stage_stats((enum TorirsPerfStage)i, &mean, &p50, &p95, &mx);
                fprintf(csv, "stage,%s,%llu,%llu,%llu,%llu,,\n", g_stage_names[i],
                        (unsigned long long)mean, (unsigned long long)p50,
                        (unsigned long long)p95, (unsigned long long)mx);
            }
            for( i = 0; i < TORIRS_PERF_CTR_COUNT; i++ )
            {
                int64_t total;
                double per;
                ctr_totals((enum TorirsPerfCounter)i, &total, &per);
                fprintf(csv, "counter,%s,,,,,%lld,%.6f\n", g_ctr_names[i],
                        (long long)total, per);
            }
            fclose(csv);
            fprintf(stderr, "\ntorirs_perf: wrote %s\n", g_csv_path);
        }
    }
    fprintf(stderr, "=== end torirs_perf ===\n\n");
}

#endif /* TORIRS_PERF_DISABLE */
