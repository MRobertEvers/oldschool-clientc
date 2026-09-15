/*
 * Plugin-path performance counters, compiled in only under
 * -DTORIRS_PERF_AUDIT_BUILD=1 (see the `torirs_perfaudit` recipe in the
 * makefile). They measured the anchor reorder pass, the input hit walks, the
 * frame bind and the retained setters; the gates that keep those honest cite
 * their names, so they stay in the tree as the instrument rather than being
 * re-derived each time. A default build defines nothing: every PA_ macro is
 * empty and `struct PerfAudit` is not even declared.
 *
 * Never TORIRS_PERF: that facility measures something else and is documented
 * as useless for this path.
 *
 * With the build flag on, every counter is bumped unconditionally (a few ns)
 * and PRINTED only when TORIRS_PERF_AUDIT=1 in the environment, between the
 * frames named by TORIRS_PERF_AUDIT_LO and _HI.
 */
#ifndef TORIRS_PERF_AUDIT_H
#define TORIRS_PERF_AUDIT_H

#include <stdint.h>

#if !defined(TORIRS_PERF_AUDIT_BUILD) || TORIRS_PERF_AUDIT_BUILD == 0

#include <stddef.h>

/* The argument is consumed so a timing local declared beside the counter does
 * not become an unused variable in a default build. */
#define PA_ADD(field, by)  do { (void)(by); } while(0)
#define PA_INC(field)      ((void)0)
static inline uint64_t PerfAudit_Now(void) { return 0; }
static inline int PerfAudit_On(void) { return 0; }
static inline void PerfAudit_EndFrame(uint64_t work_us) { (void)work_us; }
extern int g_pa_site;

#else

struct PerfAudit
{
    /* 1. anchor reorder */
    uint64_t reorder_calls;          /* UITree_FrameReorder entries */
    uint64_t reorder_bodies;         /* got past the anchor_count==0 early-out */
    uint64_t reorder_records;        /* sum of `count` */
    uint64_t reorder_n;              /* sum of component_count */
    uint64_t reorder_units;          /* sum of `anchored` */
    uint64_t reorder_init_iters;     /* the n-wide init loop */
    uint64_t reorder_treefirst_calls;
    uint64_t reorder_treefirst_iters;/* the n-wide scan inside anchor_tree_first */
    uint64_t reorder_writetree_iters;/* the n-wide child scan inside anchor_write_tree */
    uint64_t reorder_root_scan_iters;/* the (count+1) x n root scan */
    uint64_t reorder_unitof_iters;
    uint64_t reorder_bytes;          /* malloc/calloc bytes */
    uint64_t reorder_ns;
    uint64_t reorder_site_emit, reorder_site_input, reorder_site_hover;
    uint64_t reorder_rec_emit, reorder_rec_input, reorder_rec_hover;
    uint64_t reorder_ns_emit, reorder_ns_input, reorder_ns_hover;
    uint64_t input_events_calls, input_events_bytes, hover_items_calls, hover_items_bytes;
    /* 2. owned creates */
    uint64_t create_calls;
    uint64_t create_hits;            /* idempotent: returned an existing child */
    uint64_t create_sibling_iters;
    uint64_t create_cap_iters;       /* the component_count cap scan */
    uint64_t create_ns;
    /* 3. retained setters */
    uint64_t set_geometry_calls, set_geometry_same, set_geometry_slot_iters, set_geometry_allocs;
    uint64_t set_hidden_calls, set_hidden_same, set_hidden_slot_iters;
    uint64_t set_anchor_calls, set_anchor_same;
    uint64_t set_skin_calls, set_skin_same;
    uint64_t set_graphic_calls, set_transparency_calls, set_text_calls;
    uint64_t widget_edit_allocs, widget_edit_bytes;
    uint64_t note_mutation_calls;    /* mutations the setters raised */
    uint64_t setter_ns;
    /* 4. layout position override */
    uint64_t override_calls, override_iters, override_hits, override_ns;
    /* 5. frame reassert / apply */
    uint64_t reassert_calls, frame_apply_calls, frame_apply_equal;
    uint64_t collect_slots_calls, collect_slots_iters;
    uint64_t collect_chrome_calls, collect_chrome_iters, collect_chrome_bytes;
    uint64_t stretch_calls, stretch_iters;
    uint64_t frame_apply_ns;
    /* 6. layout tick */
    uint64_t layout_tick_calls, layout_tick_ns;
    uint64_t frame_bind_calls, frame_bind_ns, frame_bind_stamp_calls;
    uint64_t slots_stale_calls, slots_stale_bytes, slots_stale_ns, slots_stale_true;
    uint64_t widgets_changed_calls, widgets_changed_ns;
    uint64_t plugin_host_layout_calls, plugin_host_layout_ns;
    /* 7. role resolution */
    uint64_t role_node_calls, role_memo_hits, role_memo_misses;
    uint64_t role_find_calls, role_find_iters;
    uint64_t role_authored_iters, role_matcher_calls;
    uint64_t role_ns;
    /* 8. find_all */
    uint64_t find_all_calls, find_all_iters, find_all_ns;
    uint64_t widget_request_calls;
    /* 9. emit / draw */
    uint64_t emit_walk_calls, emit_cmds;
    uint64_t draw_canvas_calls, draw_world_calls, draw_canvas_ns;
    /* 10. plugin-path heap */
    uint64_t plugin_mallocs, plugin_malloc_bytes;
    /* 11. frame */
    uint64_t frames, frame_work_us_total;
    uint64_t getenv_calls;
};

/* Weak so a test binary that links only part of the engine still links;
 * uitree.c carries the strong definition in the client. */
struct PerfAudit g_pa __attribute__((weak));      /* accumulates within one frame */
extern struct PerfAudit g_pa_sum;  /* accumulates over the sampled window */
extern int g_pa_on;                /* -1 unknown, 0 off, 1 on */
extern uint64_t g_pa_frame;
extern int g_pa_site; /* 0 emit, 1 input, 2 hover */

int PerfAudit_On(void);
void PerfAudit_EndFrame(uint64_t work_us);
#include <time.h>
static inline uint64_t PerfAudit_Now(void) /* monotonic ns */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define PA_ADD(field, by)  do { g_pa.field += (uint64_t)(by); } while(0)
#define PA_INC(field)      PA_ADD(field, 1)

#endif /* TORIRS_PERF_AUDIT_BUILD */

#endif /* TORIRS_PERF_AUDIT_H */
