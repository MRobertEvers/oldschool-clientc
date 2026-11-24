#ifndef CS2VM2_PROFILING_H
#define CS2VM2_PROFILING_H

/*
 * CS2VM2 PROFILING -- every measurement the CS2 interpreter and its host
 * boundary take, behind ONE compile-time switch.
 *
 * WHAT IT IS. The counters and timers the perf report (TORIRS_PERF=1,
 * src/perf/torirs_perf.c) shows for the VM -- cs2_scripts, cs2_opcodes,
 * cs2_host_ops, cs2_cycles, cs2_aborts, the cs2_vm_* pool and init/release
 * times, the cs2_frame_* pool/push/locals counts, and the cs2_host_op,
 * cs2_script and cs2_script_in stages -- plus the per-script profile and
 * opcode histogram of TORIRS_CS2_PROFILE=1. cs2vm2.c calls the functions
 * below at the points it measures; it does not include the perf header.
 *
 * WHAT IT REPLACES. cs2vm2.c called TORIRS_PERF_COUNT / _STAGE_* directly, and
 * RS_CS2Host_Exec tested `g_torirs_perf_enabled` on every request to choose a
 * measured dispatch. Each of those was a load and a branch on the path of
 * every instruction and every host request (~85,000 of them to open the
 * bank) in every build, profiled or not. The per-instruction opcode
 * histogram tested `g_cs2_profile_on` the same way.
 *
 * THE SWITCH. `CS2VM2_PROFILING` (make: `CS2VM2_PROFILING=1`, which adds
 * -DCS2VM2_PROFILING=1 to every unit, its own object-directory suffix
 * `_prof`). Unset -- the default -- every function here is an empty
 * `static inline`: no test, no counter, no load. Set, they reach the same perf
 * report as before, and still only while TORIRS_PERF=1 is on at run time (the
 * TORIRS_CS2_PROFILE profile, while that is set). A build without it says
 * "not compiled in" where the numbers would be (the perf report, and once on
 * stderr when TORIRS_CS2_PROFILE is set) instead of printing zeros.
 *
 * docs/PERF_HARNESS.md "Build knobs" documents the switch.
 */

#include <stdint.h>

#ifndef CS2VM2_PROFILING
#define CS2VM2_PROFILING 0
#endif

/* The one-line notice a build without profiling prints where a number was
 * asked for. */
#define CS2VM2_PROFILING_NOT_COMPILED_IN "not compiled in (build with CS2VM2_PROFILING=1)"

#if CS2VM2_PROFILING

#include "perf/torirs_perf.h"

#ifdef TORIRS_PERF_DISABLE
#error "CS2VM2_PROFILING=1 needs the perf report (TORIRS_PERF_DISABLE is set)"
#endif

/* ---- the host boundary: one per request the VM asks ---- */

/* Depth of nested asks: a handler may run another script (CC_CALLONOP, the
 * trigger dispatchers) and that script asks again. Only the outermost ask is
 * timed, so the cs2_host_op stage stays the wall-clock time cs2 spends inside
 * host work rather than a sum that can exceed its own parent. */
extern int g_cs2vm2_prof_host_depth;

/* Count the request, start the stage if this is the outermost ask. Returns
 * what CS2VM2_ProfHostLeave needs. */
static inline int
CS2VM2_ProfHostEnter(void)
{
    if( !g_torirs_perf_enabled )
        return 0;
    TorirsPerf_Count(TORIRS_PERF_CTR_CS2_HOST_OPS, 1);
    if( g_cs2vm2_prof_host_depth++ == 0 )
        TorirsPerf_StageBegin(TORIRS_PERF_STAGE_CS2_HOST_OP);
    return 1;
}

static inline void
CS2VM2_ProfHostLeave(int entered)
{
    if( !entered )
        return;
    if( --g_cs2vm2_prof_host_depth == 0 )
        TorirsPerf_StageEnd(TORIRS_PERF_STAGE_CS2_HOST_OP);
}

/* ---- per instruction / per run of the interpreter loop ---- */

static inline void
CS2VM2_ProfScriptBody(void)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_SCRIPTS, 1);
}

static inline void
CS2VM2_ProfOpcodes(int n)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, n);
}

static inline void
CS2VM2_ProfCycles(int n)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, n);
}

static inline void
CS2VM2_ProfAbort(void)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_ABORTS, 1);
}

/* ---- frames ---- */

static inline void
CS2VM2_ProfFrameLocalsGrown(long long bytes)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_LOCALS_BYTES, bytes);
}

static inline void
CS2VM2_ProfFramePool(int hit)
{
    if( hit )
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_POOL_HIT, 1);
    else
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_POOL_MISS, 1);
}

static inline void
CS2VM2_ProfFramePush(long long clear_bytes)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_PUSH, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_CLEAR_BYTES, clear_bytes);
}

/* ---- the VM pool: acquire and release ---- */

static inline void
CS2VM2_ProfVmAcquire(int pool_hit)
{
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_ACQUIRE, 1);
    if( pool_hit )
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_POOL_HIT, 1);
    else
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_POOL_MISS, 1);
}

/* A clock read only while the report is on: clock_gettime crosses the
 * JS/WASM boundary in a browser. 0 when off. */
uint64_t
CS2VM2_ProfClockIfOn(void);

/* `t0` from CS2VM2_ProfClockIfOn; nothing when it was 0. */
void
CS2VM2_ProfVmInitDone(uint64_t t0);
void
CS2VM2_ProfVmReleaseDone(uint64_t t0);

/* ---- one run of a script body (the cs2_script stage) ---- */

/* Begin/end the cs2_script stage around a script body. Nested bodies (a
 * script that reaches the task layer, which runs another) are not timed
 * twice. Returns what CS2VM2_ProfScriptLeave needs. */
uint64_t
CS2VM2_ProfScriptEnter(void);
void
CS2VM2_ProfScriptLeave(uint64_t token);

/* ---- TORIRS_CS2_PROFILE: the per-entry-script profile ---- */

struct CS2VM2_ProfEntry
{
    void* row;
    uint64_t begin_ns;
    uint64_t begin_creates;
    uint64_t begin_marks;
    uint64_t begin_topo;
};

/* > 0 while TORIRS_CS2_PROFILE is on (read once, then cached). */
extern int g_cs2vm2_prof_script_profile;

/* Read TORIRS_CS2_PROFILE (once). True when the profile is on. */
int
CS2VM2_ProfScriptProfileOn(void);

/* True when every instruction must reach its handler to be counted (the
 * opcode histogram). Read once per handler instruction. */
static inline int
CS2VM2_ProfObserved(void)
{
    return g_cs2vm2_prof_script_profile > 0;
}

/* Count one handler instruction in the opcode histogram. */
void
CS2VM2_ProfOpcodeSlow(int opcode);
static inline void
CS2VM2_ProfOpcode(int opcode)
{
    if( g_cs2vm2_prof_script_profile > 0 )
        CS2VM2_ProfOpcodeSlow(opcode);
}

void
CS2VM2_ProfEntryBegin(
    struct CS2VM2_ProfEntry* entry,
    int script_id);
void
CS2VM2_ProfEntryEnd(struct CS2VM2_ProfEntry* entry);

/* Once per process, at VM init: nothing to say when profiling is in. */
static inline void
CS2VM2_ProfNotice(void)
{
}

#else /* !CS2VM2_PROFILING: every function is empty. */

struct CS2VM2_ProfEntry
{
    int unused;
};

static inline int
CS2VM2_ProfHostEnter(void)
{
    return 0;
}
static inline void
CS2VM2_ProfHostLeave(int entered)
{
    (void)entered;
}
static inline void
CS2VM2_ProfScriptBody(void)
{
}
static inline void
CS2VM2_ProfOpcodes(int n)
{
    (void)n;
}
static inline void
CS2VM2_ProfCycles(int n)
{
    (void)n;
}
static inline void
CS2VM2_ProfAbort(void)
{
}
static inline void
CS2VM2_ProfFrameLocalsGrown(long long bytes)
{
    (void)bytes;
}
static inline void
CS2VM2_ProfFramePool(int hit)
{
    (void)hit;
}
static inline void
CS2VM2_ProfFramePush(long long clear_bytes)
{
    (void)clear_bytes;
}
static inline void
CS2VM2_ProfVmAcquire(int pool_hit)
{
    (void)pool_hit;
}
static inline uint64_t
CS2VM2_ProfClockIfOn(void)
{
    return 0;
}
static inline void
CS2VM2_ProfVmInitDone(uint64_t t0)
{
    (void)t0;
}
static inline void
CS2VM2_ProfVmReleaseDone(uint64_t t0)
{
    (void)t0;
}
static inline uint64_t
CS2VM2_ProfScriptEnter(void)
{
    return 0;
}
static inline void
CS2VM2_ProfScriptLeave(uint64_t token)
{
    (void)token;
}
static inline int
CS2VM2_ProfScriptProfileOn(void)
{
    return 0;
}
static inline int
CS2VM2_ProfObserved(void)
{
    return 0;
}
static inline void
CS2VM2_ProfOpcode(int opcode)
{
    (void)opcode;
}
static inline void
CS2VM2_ProfEntryBegin(
    struct CS2VM2_ProfEntry* entry,
    int script_id)
{
    (void)entry;
    (void)script_id;
}
static inline void
CS2VM2_ProfEntryEnd(struct CS2VM2_ProfEntry* entry)
{
    (void)entry;
}

/* Once per process, at VM init: TORIRS_CS2_PROFILE asks for a profile this
 * build cannot take, so say so once rather than print nothing. Not on any
 * instruction's or request's path. */
void
CS2VM2_ProfNotice(void);

#endif /* CS2VM2_PROFILING */

#endif /* CS2VM2_PROFILING_H */
