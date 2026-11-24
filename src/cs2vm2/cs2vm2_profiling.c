/*
 * CS2VM2 PROFILING -- the state and the slow halves of cs2vm2_profiling.h.
 * See that header for what this module is and what it replaces.
 *
 * Built in every configuration so every source list can name it; without
 * CS2VM2_PROFILING it holds only the one-line notice for TORIRS_CS2_PROFILE.
 */
#include "cs2vm2_profiling.h"

#include "log/torirs_log.h"

#include <stdlib.h>

#if CS2VM2_PROFILING

#include <time.h>

int g_cs2vm2_prof_host_depth = 0;
int g_cs2vm2_prof_script_profile = -1;

/*
 * TORIRS_CS2_PROFILE=1: wall time and opcode count per *entry* script.
 *
 * The frame-stage timers (TORIRS_PERF) say "the cs2 stage spiked to 22 ms";
 * they cannot say which script did it, and a stage that runs a hundred cache
 * scripts per tick needs that answer to be useful. Gosubs are inside one
 * RunScript call, so the cost lands on the script that was entered — which is
 * the unit a caller can actually do something about. Resumes after a yield
 * accumulate into the same row.
 */
#define CS2_PROFILE_ROWS 512

struct cs2_profile_row
{
    int script_id;
    uint64_t ns;
    uint32_t calls;
    /* Components this script created, from bracketing the call with
     * g_torirs_cc_create_seq. Target 7's allowlist needs to know not just which
     * scripts are expensive but which ones damage the tree every frame, because
     * those are the ones whose skip also unblocks targets 11, 12 and 14. */
    uint64_t creates;
    /* Reached dirty marks this script caused. `creates` turned out to be a
     * startup measure — the per-frame scripts create nothing — so this is the
     * column that actually identifies the script defeating target 11's gate. */
    uint64_t marks;
    /* Topology bumps (link/unlink/allocate/hide) this script caused. `marks`
     * only sees UITree_MarkNodeDirty; the topology path bumps dirty_gen without
     * going through it, which is why the script that provably defeats the gate
     * still read as zero in both columns above. */
    uint64_t topo;
    /* uitree.c line of the last topology bump this script caused. Exact when
     * the script bumps once per call, which is the shape target 7 cares about. */
    int topo_line;
    /* `calls` is zeroed by the report's selection sort; this survives it. */
    uint32_t calls_total;
};

static struct cs2_profile_row g_cs2_profile[CS2_PROFILE_ROWS];
static int g_cs2_profile_rows;

/*
 * Opcode histogram, on the same gate as the script profile above.
 *
 * Target 8 (predecode) has to choose a micro-op set, and choosing it from the
 * opcode table rather than from what this client actually executes is how a
 * predecoder ends up optimising ops that never run. The measured cost is
 * ~109 ns/opcode over 2,164 opcodes a frame; this says which opcodes those are.
 *
 * Counts, not times: timing individual ops needs two clock reads each, which at
 * this volume would cost more than the thing being measured and would move the
 * number it reports. Count x the known mean is the honest instrument here — and
 * a flat-ish distribution is itself the answer, because it would mean no small
 * micro-op set can cover the traffic and target 8 should be reconsidered.
 *
 * Sized to cover the whole opcode space including the 10xxx dialect range, and
 * indexed only after a bounds test: `opcode` is a plain int off the script and
 * a malformed cache must not scribble past the array.
 */
#define CS2_OPCODE_HIST_SIZE 16384
static uint32_t g_cs2_opcode_hist[CS2_OPCODE_HIST_SIZE];
static uint64_t g_cs2_opcode_hist_total;
static uint32_t g_cs2_opcode_hist_dropped;

static uint64_t
cs2_profile_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
cs2_profile_report(void)
{
    /* Grand total across every row, printed before the selection sort below
     * consumes the table. This is the number that decides targets 8, 9 and 13:
     * `cs2_settle` minus host ops leaves ~236 us/frame that has been *assumed*
     * to be opcode execution, and this is the only direct measurement of what
     * CS2VM2_RunScript actually costs. 17.6 calls a frame is cheap enough to
     * bracket honestly; per-opcode timing is not. */
    {
        uint64_t all_ns = 0;
        uint64_t all_calls = 0;
        for( int i = 0; i < g_cs2_profile_rows; i++ )
        {
            all_ns += g_cs2_profile[i].ns;
            all_calls += g_cs2_profile[i].calls;
        }
        TORIRS_LOG(
            "=== cs2 RunScript total: %.3f ms over %llu calls, %llu distinct scripts ===\n",
            (double)all_ns / 1e6,
            (unsigned long long)all_calls,
            (unsigned long long)g_cs2_profile_rows);
    }
    TORIRS_LOG("=== cs2 script profile (top 20 by total wall time) ===\n");
    for( int rank = 0; rank < 20 && rank < g_cs2_profile_rows; rank++ )
    {
        int best = -1;
        for( int i = 0; i < g_cs2_profile_rows; i++ )
        {
            if( g_cs2_profile[i].calls == 0 )
                continue;
            if( best < 0 || g_cs2_profile[i].ns > g_cs2_profile[best].ns )
                best = i;
        }
        if( best < 0 )
            break;
        TORIRS_LOG(
            "  script %-6d %8.3f ms total  %6u calls  %8.3f us/call  %8llu creates  %8llu "
            "marks  %8llu topo  %7.2f topo/call  uitree.c:%d\n",
            g_cs2_profile[best].script_id,
            (double)g_cs2_profile[best].ns / 1e6,
            g_cs2_profile[best].calls,
            (double)g_cs2_profile[best].ns / 1e3 / (double)g_cs2_profile[best].calls,
            (unsigned long long)g_cs2_profile[best].creates,
            (unsigned long long)g_cs2_profile[best].marks,
            (unsigned long long)g_cs2_profile[best].topo,
            (double)g_cs2_profile[best].topo / (double)g_cs2_profile[best].calls,
            g_cs2_profile[best].topo_line);
        g_cs2_profile[best].calls = 0; /* consumed: this pass is a selection sort */
    }
    /* The top-20 listing above cannot answer "is there cost outside the VM?" —
     * that needs every row. Compare this against the `cs2` stage: what the stage
     * has and this does not is task machinery, and if the gap is small there is
     * nothing there to optimize. `calls` is consumed by the selection sort
     * above, so this sums `ns`, which is not. */
    {
        double total_ms = 0.0;
        uint64_t total_calls = 0;
        for( int i = 0; i < g_cs2_profile_rows; i++ )
        {
            total_ms += (double)g_cs2_profile[i].ns / 1e6;
            total_calls += g_cs2_profile[i].calls_total;
        }
        TORIRS_LOG(
            "  ALL %d scripts: %.3f ms total, %llu calls\n",
            g_cs2_profile_rows,
            total_ms,
            (unsigned long long)total_calls);
    }
    TORIRS_LOG("=== end cs2 script profile ===\n");

    /* Cumulative share is the number target 8 is actually choosing against: it
     * says how many distinct opcodes a micro-op set has to cover to reach most
     * of the traffic. Forty rows is enough to see that curve flatten. */
    TORIRS_LOG(
        "=== cs2 opcode histogram (top 40 of %llu executed) ===\n",
        (unsigned long long)g_cs2_opcode_hist_total);
    {
        uint64_t running = 0;
        for( int rank = 0; rank < 40; rank++ )
        {
            int best = -1;
            for( int i = 0; i < CS2_OPCODE_HIST_SIZE; i++ )
            {
                if( g_cs2_opcode_hist[i] == 0 )
                    continue;
                if( best < 0 || g_cs2_opcode_hist[i] > g_cs2_opcode_hist[best] )
                    best = i;
            }
            if( best < 0 )
                break;
            running += g_cs2_opcode_hist[best];
            TORIRS_LOG(
                "  op %-6d %10u  %5.2f%%  cum %5.2f%%\n",
                best,
                g_cs2_opcode_hist[best],
                g_cs2_opcode_hist_total
                    ? 100.0 * (double)g_cs2_opcode_hist[best] / (double)g_cs2_opcode_hist_total
                    : 0.0,
                g_cs2_opcode_hist_total ? 100.0 * (double)running / (double)g_cs2_opcode_hist_total
                                        : 0.0);
            g_cs2_opcode_hist[best] = 0; /* consumed, same selection sort as above */
        }
        if( g_cs2_opcode_hist_dropped )
            TORIRS_LOG(
                "  (%u executions had an opcode outside [0,%d) and were not counted)\n",
                g_cs2_opcode_hist_dropped,
                CS2_OPCODE_HIST_SIZE);
    }
    TORIRS_LOG("=== end cs2 opcode histogram ===\n");
}

static struct cs2_profile_row*
cs2_profile_row_for(int script_id)
{
    for( int i = 0; i < g_cs2_profile_rows; i++ )
    {
        if( g_cs2_profile[i].script_id == script_id )
            return &g_cs2_profile[i];
    }
    if( g_cs2_profile_rows >= CS2_PROFILE_ROWS )
        return NULL;
    g_cs2_profile[g_cs2_profile_rows].script_id = script_id;
    return &g_cs2_profile[g_cs2_profile_rows++];
}

int
CS2VM2_ProfScriptProfileOn(void)
{
    if( g_cs2vm2_prof_script_profile < 0 )
    {
        g_cs2vm2_prof_script_profile = getenv("TORIRS_CS2_PROFILE") ? 1 : 0;
        if( g_cs2vm2_prof_script_profile )
            atexit(cs2_profile_report);
    }
    return g_cs2vm2_prof_script_profile > 0;
}

void
CS2VM2_ProfOpcodeSlow(int opcode)
{
    if( opcode >= 0 && opcode < CS2_OPCODE_HIST_SIZE )
        g_cs2_opcode_hist[opcode]++;
    else
        g_cs2_opcode_hist_dropped++;
    g_cs2_opcode_hist_total++;
}

void
CS2VM2_ProfEntryBegin(
    struct CS2VM2_ProfEntry* entry,
    int script_id)
{
    entry->row = cs2_profile_row_for(script_id);
    entry->begin_ns = cs2_profile_now_ns();
    entry->begin_creates = g_torirs_cc_create_seq;
    entry->begin_marks = g_torirs_dirty_mark_seq;
    entry->begin_topo = g_torirs_dirty_topo_seq;
}

void
CS2VM2_ProfEntryEnd(struct CS2VM2_ProfEntry* entry)
{
    struct cs2_profile_row* row = (struct cs2_profile_row*)entry->row;
    if( !row )
        return;
    row->ns += cs2_profile_now_ns() - entry->begin_ns;
    row->calls++;
    row->calls_total++;
    /* Gosubs run inside this call, so creates land on the entry script --
     * the same attribution rule the wall-time column already uses, and
     * the right one for target 7, whose allowlist is entry scripts. */
    row->creates += g_torirs_cc_create_seq - entry->begin_creates;
    row->marks += g_torirs_dirty_mark_seq - entry->begin_marks;
    if( g_torirs_dirty_topo_seq != entry->begin_topo )
    {
        row->topo += g_torirs_dirty_topo_seq - entry->begin_topo;
        row->topo_line = g_torirs_dirty_topo_line;
    }
}

/* ---- the cs2_script stage ---- */

/** Depth of nested script bodies. A script can reach the task layer, which
 *  can run another script, so only the outermost body is timed -- otherwise
 *  the child's nanoseconds land in the stage twice. */
static int g_cs2_script_depth = 0;

/* Token: 0 = not timed (nested, or the report is off); otherwise the start
 * time + 1. The script share is read by the perf harness alone
 * (TORIRS_PERF_CARRY), so the clock is too: an interface runs hundreds of
 * timer scripts a tick, and in a browser clock_gettime is a call out to
 * JavaScript (5% of an RSProx replay's seek, CDP profile 2026-11-12). */
uint64_t
CS2VM2_ProfScriptEnter(void)
{
    if( g_cs2_script_depth++ || !g_torirs_perf_enabled )
        return 0;
    TorirsPerf_StageBegin(TORIRS_PERF_STAGE_CS2_SCRIPT);
    return cs2_profile_now_ns() + 1;
}

void
CS2VM2_ProfScriptLeave(uint64_t token)
{
    g_cs2_script_depth--;
    if( !token )
        return;
    TorirsPerf_StageEnd(TORIRS_PERF_STAGE_CS2_SCRIPT);
    g_torirs_cs2_script_ns += cs2_profile_now_ns() - (token - 1);
}

/* ---- VM init / release times ---- */

uint64_t
CS2VM2_ProfClockIfOn(void)
{
    return g_torirs_perf_enabled ? cs2_profile_now_ns() + 1 : 0;
}

void
CS2VM2_ProfVmInitDone(uint64_t t0)
{
    if( t0 && g_torirs_perf_enabled )
        TorirsPerf_Count(
            TORIRS_PERF_CTR_CS2_VM_INIT_NS, (int64_t)(cs2_profile_now_ns() - (t0 - 1)));
}

void
CS2VM2_ProfVmReleaseDone(uint64_t t0)
{
    if( t0 && g_torirs_perf_enabled )
        TorirsPerf_Count(
            TORIRS_PERF_CTR_CS2_VM_RELEASE_NS, (int64_t)(cs2_profile_now_ns() - (t0 - 1)));
}

#else /* !CS2VM2_PROFILING */

void
CS2VM2_ProfNotice(void)
{
    static int said = 0;
    if( said )
        return;
    said = 1;
    if( getenv("TORIRS_CS2_PROFILE") )
        TORIRS_LOG("cs2: TORIRS_CS2_PROFILE: script profile and opcode histogram " CS2VM2_PROFILING_NOT_COMPILED_IN "\n");
}

#endif /* CS2VM2_PROFILING */
