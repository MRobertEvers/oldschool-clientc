/*
 * Per-frame phase clocks for the editor, off by default.
 *
 * `TORIRS_EDITOR_FRAME_STATS=1` arms them (`TORIRS_EDITOR_FRAME_STATS_FROM=<n>`
 * samples only frames after the n-th, to isolate a scenario;
 * `TORIRS_EDITOR_FRAME_STATS_SLOW=<ms>` prints every loop iteration longer
 * than that -- the frame's phases and the time outside App_RunOnce+App_Render,
 * which is the present, the event poll and the pacer's sleep -- the moment
 * it ends, so a hitch names its frame and its phase); at exit the client prints to
 * stderr the number of frames sampled and the p50 / p95 / max of each phase
 * in milliseconds, plus the startup marks (time since the process started and
 * the resident set at the first frame that drew the world and at the moment
 * the catalog's warm build finished). Sampling starts at the first drawn
 * world frame so the boot's loading frames do not pollute the steady state.
 *
 * Phases nest: EDITOR_UI contains CHROME_BUILD, RENDER contains WORLD_DRAW,
 * FRAME contains everything but PRESENT. A phase is one `FrameStats_Add` per
 * frame at most -- several spans in one frame add up.
 *
 * Which lane marks what: the app's phases (ui, pump, logic, flush, interact)
 * are marked on every lane, and so is the world-drawn mark that starts the
 * sampling (App_BuildFrame). RENDER, the hover pick and FRAME's render half
 * are marked by the software lane (App_Render) and by D3D9 (main.c); PRESENT
 * by D3D9 only; WORLD_DRAW by the software rasteriser only. On another GPU
 * lane a slow frame is still printed, with its render time under "outside
 * frame".
 *
 * Header-only on purpose: the renderer (platform/) and the app (app/) both
 * mark phases, and a .c would have to join every link line that carries
 * either. The state is one weak global, the pattern perf_audit.h uses.
 *
 * Never TORIRS_PERF: that facility measures something else and is documented
 * as useless for frame work.
 */
#ifndef TORIRS_FRAME_STATS_H
#define TORIRS_FRAME_STATS_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

enum FrameStatsPhase
{
    FRAME_STATS_FRAME = 0,   /* App_RunOnce + App_Render */
    FRAME_STATS_EDITOR_UI,   /* chrome tick + overlay/editor panel ticks (chrome build inside) */
    FRAME_STATS_CHROME_BUILD,/* ToriRSChrome_Build (the retained display list rebuild) */
    FRAME_STATS_HOVER_GHOSTS,/* hover pick finish + ghost / preview / palette updates */
    FRAME_STATS_PUMP,        /* App_PumpAsync: IO, the embedded editor server (catalog slices) */
    FRAME_STATS_LOGIC,       /* logic ticks + app_world_frame (movers) */
    FRAME_STATS_EDIT_FLUSH,  /* app_map_editor_drain: the edit layout's remesh flush */
    FRAME_STATS_INTERACT,    /* UITree_InteractFrame (the game tree's input pass) */
    FRAME_STATS_RENDER,      /* Soft3D_RenderFrame: world + 2D raster */
    FRAME_STATS_WORLD_DRAW,  /* the DRAW_MODEL commands inside RENDER (the world) */
    FRAME_STATS_PRESENT,     /* the swap (D3D9 Present): outside FRAME, inside the loop interval */
    FRAME_STATS_PHASE_COUNT
};

enum FrameStatsCounter
{
    FRAME_STATS_CTR_CHROME_REBUILDS = 0, /* frames on which ToriRSChrome_Build rebuilt */
    FRAME_STATS_CTR_PICK_FRAMES,         /* frames that ran the world hover pick */
    FRAME_STATS_CTR_REMESH_TILES,        /* tiles remeshed by the edit layout */
    FRAME_STATS_CTR_PREVIEW_RASTERS,     /* Preview well rasters */
    FRAME_STATS_CTR_COUNT
};

enum FrameStatsMark
{
    FRAME_STATS_MARK_WORLD_DRAWN = 0, /* first App_Render that drew the world */
    FRAME_STATS_MARK_CATALOG_READY,   /* the catalog's warm build finished */
    FRAME_STATS_MARK_COUNT
};

#define FRAME_STATS_SAMPLES_MAX 65536

struct FrameStats
{
    int enabled;          /* 0 not yet read, 1 on, 2 off */
    int sampling;         /* 1 once the first world frame was drawn */
    int sample_from;      /* TORIRS_EDITOR_FRAME_STATS_FROM: no sample before this frame */
    int slow_us;          /* TORIRS_EDITOR_FRAME_STATS_SLOW in microseconds, 0 = no trace */
    uint64_t last_end_ns; /* the clock at the previous FrameEnd: the loop interval */
    uint64_t start_ns;    /* the clock at the first call (close to process start) */
    uint64_t accum_ns[FRAME_STATS_PHASE_COUNT]; /* this frame */
    uint64_t counters[FRAME_STATS_CTR_COUNT];
    uint32_t* samples;    /* [sample_count][PHASE_COUNT] microseconds */
    int sample_count;
    int frames_total;
    double mark_s[FRAME_STATS_MARK_COUNT];
    long mark_rss_kb[FRAME_STATS_MARK_COUNT];
    int mark_frame[FRAME_STATS_MARK_COUNT];
    int mark_set[FRAME_STATS_MARK_COUNT];
};

/* Weak so every TU may define it; the linker keeps one. */
struct FrameStats g_frame_stats __attribute__((weak));

static inline uint64_t
FrameStats_ClockNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline int
FrameStats_Enabled(void)
{
    if( g_frame_stats.enabled == 0 )
    {
        char const* env = getenv("TORIRS_EDITOR_FRAME_STATS");
        g_frame_stats.start_ns = FrameStats_ClockNs();
        g_frame_stats.enabled = env && env[0] == '1' ? 1 : 2;
        if( g_frame_stats.enabled == 1 )
        {
            char const* from = getenv("TORIRS_EDITOR_FRAME_STATS_FROM");
            char const* slow = getenv("TORIRS_EDITOR_FRAME_STATS_SLOW");
            g_frame_stats.sample_from = from ? atoi(from) : 0;
            g_frame_stats.slow_us = slow ? atoi(slow) * 1000 : 0;
            g_frame_stats.samples =
                calloc((size_t)FRAME_STATS_SAMPLES_MAX * FRAME_STATS_PHASE_COUNT, sizeof(uint32_t));
            assert(g_frame_stats.samples);
        }
    }
    return g_frame_stats.enabled == 1;
}

/** 0 when the stats are off, so a span costs one predictable branch. */
static inline uint64_t
FrameStats_Now(void)
{
    return FrameStats_Enabled() ? FrameStats_ClockNs() : 0;
}

static inline void
FrameStats_Add(enum FrameStatsPhase phase, uint64_t ns)
{
    assert(phase >= 0 && phase < FRAME_STATS_PHASE_COUNT);
    if( !FrameStats_Enabled() )
        return;
    g_frame_stats.accum_ns[phase] += ns;
}

/** Add the span from `t0` (a FrameStats_Now) to now. */
static inline void
FrameStats_Span(enum FrameStatsPhase phase, uint64_t t0)
{
    if( t0 == 0 )
        return;
    FrameStats_Add(phase, FrameStats_ClockNs() - t0);
}

static inline void
FrameStats_Count(enum FrameStatsCounter counter, uint64_t by)
{
    assert(counter >= 0 && counter < FRAME_STATS_CTR_COUNT);
    if( !FrameStats_Enabled() )
        return;
    g_frame_stats.counters[counter] += by;
}

static inline long
FrameStats_ResidentKb(void)
{
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if( task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS )
        return -1;
    return (long)(info.resident_size / 1024);
#else
    FILE* f = fopen("/proc/self/statm", "r");
    long pages = 0;
    long resident = 0;
    if( !f )
        return -1;
    if( fscanf(f, "%ld %ld", &pages, &resident) != 2 )
        resident = -1;
    fclose(f);
    return resident < 0 ? -1 : resident * 4;
#endif
}

/** A one-off startup mark: the first call wins. */
static inline void
FrameStats_Mark(enum FrameStatsMark mark)
{
    assert(mark >= 0 && mark < FRAME_STATS_MARK_COUNT);
    if( !FrameStats_Enabled() || g_frame_stats.mark_set[mark] )
        return;
    g_frame_stats.mark_set[mark] = 1;
    g_frame_stats.mark_s[mark] = (double)(FrameStats_ClockNs() - g_frame_stats.start_ns) / 1e9;
    g_frame_stats.mark_rss_kb[mark] = FrameStats_ResidentKb();
    g_frame_stats.mark_frame[mark] = g_frame_stats.frames_total;
    if( mark == FRAME_STATS_MARK_WORLD_DRAWN )
        g_frame_stats.sampling = 1;
}

/** Close the frame: push this frame's phase sums as one sample. Called once
 *  per frame, at the top of the next App_RunOnce. */
static inline void
FrameStats_FrameEnd(void)
{
    if( !FrameStats_Enabled() )
        return;
    g_frame_stats.frames_total++;
    if( g_frame_stats.slow_us > 0 )
    {
        uint64_t const now = FrameStats_ClockNs();
        uint64_t const loop_us = g_frame_stats.last_end_ns ? (now - g_frame_stats.last_end_ns) / 1000u : 0;
        uint64_t const frame_us = g_frame_stats.accum_ns[FRAME_STATS_FRAME] / 1000u;
        g_frame_stats.last_end_ns = now;
        if( g_frame_stats.sampling && (int64_t)loop_us > g_frame_stats.slow_us )
        {
            /* One line per slow iteration: the phases that nest inside the
             * frame, then what the frame did not account for, then the time
             * outside the frame (present + events + pace). Milliseconds. */
            fprintf(stderr,
                "frame_stats: slow frame %d: loop %.1f ms, frame %.1f (ui %.1f [chrome %.1f] "
                "hover %.1f pump %.1f logic %.1f flush %.1f interact %.1f render %.1f "
                "[world %.1f] other %.1f), outside frame %.1f [present %.1f]\n",
                g_frame_stats.frames_total, loop_us / 1000.0, frame_us / 1000.0,
                g_frame_stats.accum_ns[FRAME_STATS_EDITOR_UI] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_CHROME_BUILD] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_HOVER_GHOSTS] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_PUMP] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_LOGIC] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_EDIT_FLUSH] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_INTERACT] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_RENDER] / 1e6,
                g_frame_stats.accum_ns[FRAME_STATS_WORLD_DRAW] / 1e6,
                ((double)g_frame_stats.accum_ns[FRAME_STATS_FRAME]
                 - (double)(g_frame_stats.accum_ns[FRAME_STATS_EDITOR_UI]
                            + g_frame_stats.accum_ns[FRAME_STATS_PUMP]
                            + g_frame_stats.accum_ns[FRAME_STATS_LOGIC]
                            + g_frame_stats.accum_ns[FRAME_STATS_EDIT_FLUSH]
                            + g_frame_stats.accum_ns[FRAME_STATS_INTERACT]
                            + g_frame_stats.accum_ns[FRAME_STATS_RENDER]))
                    / 1e6,
                (loop_us > frame_us ? loop_us - frame_us : 0) / 1000.0,
                g_frame_stats.accum_ns[FRAME_STATS_PRESENT] / 1e6);
            fflush(stderr);
        }
    }
    if( g_frame_stats.sampling && g_frame_stats.frames_total > g_frame_stats.sample_from
        && g_frame_stats.sample_count < FRAME_STATS_SAMPLES_MAX )
    {
        uint32_t* row = g_frame_stats.samples + (size_t)g_frame_stats.sample_count * FRAME_STATS_PHASE_COUNT;
        for( int i = 0; i < FRAME_STATS_PHASE_COUNT; i++ )
            row[i] = (uint32_t)(g_frame_stats.accum_ns[i] / 1000u);
        g_frame_stats.sample_count++;
    }
    memset(g_frame_stats.accum_ns, 0, sizeof(g_frame_stats.accum_ns));
}

static inline int
frame_stats_cmp_u32(void const* a, void const* b)
{
    uint32_t const x = *(uint32_t const*)a;
    uint32_t const y = *(uint32_t const*)b;
    return x < y ? -1 : x > y;
}

/** The report, to stderr. Register with atexit from the frame loop. */
static inline void
FrameStats_Report(void)
{
    static char const* const phase_names[FRAME_STATS_PHASE_COUNT] = {
        "frame (RunOnce+Render)", "editor ui tick (+chrome build)", "  chrome build",
        "hover pick + ghosts",     "async pump (io, maped server)",  "logic + movers",
        "edit flush (remesh)",     "tree interact",                  "render (world + 2D)",
        "  world draw (models)",    "present (swap)",
    };
    static char const* const counter_names[FRAME_STATS_CTR_COUNT] = {
        "chrome rebuilds", "pick frames", "remesh tiles", "preview rasters",
    };
    static char const* const mark_names[FRAME_STATS_MARK_COUNT] = { "world drawn", "catalog ready" };
    struct FrameStats* s = &g_frame_stats;
    uint32_t* column;
    int n = s->sample_count;

    if( s->enabled != 1 )
        return;
    fprintf(stderr, "frame_stats: %d frames, %d sampled (from the first drawn world frame%s)\n",
        s->frames_total, n, s->sample_from > 0 ? ", TORIRS_EDITOR_FRAME_STATS_FROM" : "");
    fprintf(stderr, "frame_stats: %-32s %9s %9s %9s %9s\n", "phase", "p50 ms", "p95 ms", "max ms", "mean ms");
    column = malloc((size_t)(n > 0 ? n : 1) * sizeof(uint32_t));
    assert(column);
    for( int p = 0; p < FRAME_STATS_PHASE_COUNT; p++ )
    {
        double sum = 0.0;
        uint32_t p50 = 0;
        uint32_t p95 = 0;
        uint32_t max = 0;
        for( int i = 0; i < n; i++ )
        {
            column[i] = s->samples[(size_t)i * FRAME_STATS_PHASE_COUNT + p];
            sum += column[i];
        }
        if( n > 0 )
        {
            qsort(column, (size_t)n, sizeof(uint32_t), frame_stats_cmp_u32);
            p50 = column[n / 2];
            p95 = column[(int)((double)(n - 1) * 0.95)];
            max = column[n - 1];
        }
        fprintf(stderr, "frame_stats: %-32s %9.3f %9.3f %9.3f %9.3f\n", phase_names[p], p50 / 1000.0,
            p95 / 1000.0, max / 1000.0, n > 0 ? sum / n / 1000.0 : 0.0);
    }
    free(column);
    for( int c = 0; c < FRAME_STATS_CTR_COUNT; c++ )
        fprintf(stderr, "frame_stats: counter %-24s %llu\n", counter_names[c],
            (unsigned long long)s->counters[c]);
    for( int m = 0; m < FRAME_STATS_MARK_COUNT; m++ )
    {
        if( s->mark_set[m] )
            fprintf(stderr, "frame_stats: mark %-16s %.3f s from start, frame %d, rss %ld MB\n",
                mark_names[m], s->mark_s[m], s->mark_frame[m], s->mark_rss_kb[m] / 1024);
        else
            fprintf(stderr, "frame_stats: mark %-16s (never)\n", mark_names[m]);
    }
    fprintf(stderr, "frame_stats: exit rss %ld MB\n", FrameStats_ResidentKb() / 1024);
}

#endif /* TORIRS_FRAME_STATS_H */
