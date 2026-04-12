#!/usr/bin/env python3
"""Generate painters_paint1.u.c from a snapshot of painters.c (e.g. git show HEAD:...)."""
from pathlib import Path
import re

ROOT = Path("/Users/matthewevers/Documents/git_repos/3draster/src/osrs")
SRC = Path("/tmp/p_head.c")
text = SRC.read_text()
lines = text.splitlines(keepends=True)


def join(a, b):
    return "".join(lines[a - 1 : b])


# Line numbers are 1-based from /tmp/p_head.c (git HEAD)
paint1_fn = join(1257, 2046)  # painter_paint through closing brace

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

p1 = p1.replace("int_queue_push_wrap(&P1(painter)->queue", "int_queue_push_wrap(&P1(painter)->queue")  # noop

ROOT.joinpath("painters_paint1.u.c").write_text(p1)

print("Wrote paint1 to", ROOT)
