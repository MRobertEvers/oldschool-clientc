#!/usr/bin/env python3
"""Generate painters_*.u.c from a snapshot of painters.c (e.g. git show HEAD:...)."""
from pathlib import Path
import re

ROOT = Path("/Users/matthewevers/Documents/git_repos/3draster/src/osrs")
SRC = Path("/tmp/p_head.c")
text = SRC.read_text()
lines = text.splitlines(keepends=True)


def join(a, b):
    return "".join(lines[a - 1 : b])


# Line numbers are 1-based from /tmp/p_head.c (git HEAD)
paint1_helpers = join(264, 296)  # push_queue through painter_queue_pop
paint1_fn = join(1257, 2046)  # painter_paint through closing brace

cheb_fn = join(2048, 2769)  # painter_paint_chebyshev static + body
paint3_wrappers = join(2771, 2812)  # paint3, paint4, paint4_1

wf_helpers = join(394, 455)  # wf_queue_reset through wf_paint2_push
paint2_fn = join(2814, 3611)

dist_defs = join(20, 41)  # PAINTER_PAINT_DIST_* and PAINTER_DIST_QUEUE_BUCKETS
dist_helpers = join(294, 376)  # painter_dist_queue_reset through painter_queue_pop_dist
calc_block = join(306, 351)  # calc_distance_* through calc_distance

# --- paint1 ---
p1 = f'''#ifndef PAINTERS_PAINT1_U_C
#define PAINTERS_PAINT1_U_C

struct Paint1Ctx {{
    struct IntQueue queue;
    struct IntQueue catchup_queue;
}};

#define P1(P) ((struct Paint1Ctx*)(P)->paint1_ctx)

static int
paint1_ctx_init(struct Painter* painter)
{{
    struct Paint1Ctx* c = (struct Paint1Ctx*)calloc(1, sizeof(struct Paint1Ctx));
    if( !c )
        return -1;
    int_queue_init(&c->queue, 4096);
    int_queue_init(&c->catchup_queue, 4096);
    painter->paint1_ctx = c;
    return 0;
}}

static void
paint1_ctx_free(struct Painter* painter)
{{
    struct Paint1Ctx* c = P1(painter);
    if( !c )
        return;
    int_queue_free(&c->queue);
    int_queue_free(&c->catchup_queue);
    free(c);
    painter->paint1_ctx = NULL;
}}

static inline void
painter_push_queue(
    struct Painter* painter,
    int tile_idx,
    int prio)
{{
    struct TilePaint* tile_paint = &painter->tile_paints[tile_idx];
    tile_paint->queue_count++;

    if( prio == 0 )
        int_queue_push_wrap(&P1(painter)->queue, tile_idx);
    else
        int_queue_push_wrap_prio(&P1(painter)->catchup_queue, tile_idx, prio - 1);
}}

static inline bool
painter_queue_is_empty(struct Painter* painter)
{{
    return P1(painter)->queue.length == 0 && P1(painter)->catchup_queue.length == 0;
}}

static inline int
painter_queue_pop(struct Painter* painter)
{{
    if( P1(painter)->catchup_queue.length > 0 )
        return int_queue_pop(&P1(painter)->catchup_queue);
    else
        return int_queue_pop(&P1(painter)->queue);
}}

{re.sub(r"&painter->queue", "&P1(painter)->queue", re.sub(r"painter->catchup_queue", "P1(painter)->catchup_queue", re.sub(r"painter->queue", "P1(painter)->queue", paint1_fn)))}
#endif
'''

# Replace int_queue_push_wrap(&painter->queue in seed - already P1 in fn body from re.sub on paint1_fn - wait paint1_fn still has painter->queue in coord loop - the re.sub handles painter->queue -> P1(painter)->queue

p1 = p1.replace("int_queue_push_wrap(&P1(painter)->queue", "int_queue_push_wrap(&P1(painter)->queue")  # noop

ROOT.joinpath("painters_paint1.u.c").write_text(p1)

# --- paint2 ---
p2 = f'''#ifndef PAINTERS_PAINT2_U_C
#define PAINTERS_PAINT2_U_C

#define PAINTER_WF_WAVE_CAP 96

struct Paint2Ctx {{
    struct IntQueue wf_wave_q[PAINTER_WF_WAVE_CAP];
    int wf_min_wave;
}};

#define P2(P) ((struct Paint2Ctx*)(P)->paint2_ctx)

static int
paint2_ctx_init(struct Painter* painter)
{{
    struct Paint2Ctx* c = (struct Paint2Ctx*)calloc(1, sizeof(struct Paint2Ctx));
    if( !c )
        return -1;
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
        int_queue_init(&c->wf_wave_q[i], 4096);
    c->wf_min_wave = 0;
    painter->paint2_ctx = c;
    return 0;
}}

static void
paint2_ctx_free(struct Painter* painter)
{{
    struct Paint2Ctx* c = P2(painter);
    if( !c )
        return;
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
        int_queue_free(&c->wf_wave_q[i]);
    free(c);
    painter->paint2_ctx = NULL;
}}

{wf_helpers.replace("painter->wf_wave_q", "P2(painter)->wf_wave_q").replace("painter->wf_min_wave", "P2(painter)->wf_min_wave")}

{paint2_fn.replace("painter->wf_wave_q", "P2(painter)->wf_wave_q").replace("painter->wf_min_wave", "P2(painter)->wf_min_wave")}
#endif
'''

ROOT.joinpath("painters_paint2.u.c").write_text(p2)

# --- distancemetric ---
cheb_fn_dm = cheb_fn.replace("static int\npainter_paint_chebyshev", "static int\npainter_paint_distancemetric")
cheb_fn_dm = cheb_fn_dm.replace("painter_paint_chebyshev", "painter_paint_distancemetric")

dm = f'''#ifndef PAINTERS_DISTANCEMETRIC_U_C
#define PAINTERS_DISTANCEMETRIC_U_C

{dist_defs}

/* painter_paint_distancemetric option bits */
#define CHEB_OPT_CLEAR_BBOX_TILES (1u << 0)
#define CHEB_OPT_SCENERY_INSERTION_SORT (1u << 1)

struct DistMetricCtx {{
    struct IntQueue distance_queues[PAINTER_DIST_QUEUE_BUCKETS];
    int max_active_dist;
    int current_camera_sx;
    int current_camera_sz;
}};

#define DM(P) ((struct DistMetricCtx*)(P)->distmetric_ctx)

static int
distmetric_ctx_init(struct Painter* painter)
{{
    struct DistMetricCtx* d = (struct DistMetricCtx*)calloc(1, sizeof(struct DistMetricCtx));
    if( !d )
        return -1;
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
        int_queue_init(&d->distance_queues[i], 1024);
    d->max_active_dist = 0;
    painter->distmetric_ctx = d;
    return 0;
}}

static void
distmetric_ctx_free(struct Painter* painter)
{{
    struct DistMetricCtx* d = DM(painter);
    if( !d )
        return;
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
        int_queue_free(&d->distance_queues[i]);
    free(d);
    painter->distmetric_ctx = NULL;
}}

static void
painter_dist_queue_reset(struct Painter* painter)
{{
    struct DistMetricCtx* d = DM(painter);
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
    {{
        d->distance_queues[i].head = 0;
        d->distance_queues[i].tail = 0;
        d->distance_queues[i].length = 0;
    }}
    d->max_active_dist = 0;
}}

{calc_block}

static inline void
painter_push_queue_dist(
    struct Painter* painter,
    int tile_idx)
{{
    struct DistMetricCtx* d = DM(painter);
    struct PaintersTile* tile = &painter->tiles[tile_idx];
    int tile_sx = PAINTER_TILE_X(painter, tile);
    int tile_sz = PAINTER_TILE_Z(painter, tile);

    int dx = tile_sx - d->current_camera_sx;
    int dz = tile_sz - d->current_camera_sz;
    int dist = calc_distance(dx, dz);

    if( dist >= PAINTER_DIST_QUEUE_BUCKETS )
        dist = PAINTER_DIST_QUEUE_BUCKETS - 1;

    if( dist > d->max_active_dist )
        d->max_active_dist = dist;

    struct TilePaint* tile_paint = &painter->tile_paints[tile_idx];
    tile_paint->queue_count++;

    int_queue_push_wrap(&d->distance_queues[dist], tile_idx);
}}

static inline int
painter_queue_pop_dist(struct Painter* painter)
{{
    struct DistMetricCtx* d = DM(painter);
    int dist = d->max_active_dist;

    while( dist >= 0 && d->distance_queues[dist].length == 0 )
        dist--;

    if( dist < 0 )
    {{
        d->max_active_dist = 0;
        return -1;
    }}

    d->max_active_dist = dist;

    return int_queue_pop(&d->distance_queues[dist]) >> 8;
}}

{cheb_fn_dm.replace("painter->current_camera_sx", "DM(painter)->current_camera_sx").replace("painter->current_camera_sz", "DM(painter)->current_camera_sz").replace("painter->max_active_dist", "DM(painter)->max_active_dist").replace("painter->distance_queues", "DM(painter)->distance_queues")}

{paint3_wrappers.replace("painter_paint_chebyshev", "painter_paint_distancemetric")}
#endif
'''

ROOT.joinpath("painters_distancemetric.u.c").write_text(dm)

print("Wrote paint1, paint2, distancemetric to", ROOT)
