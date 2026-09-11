/*
 * Score gouraud span-fill kernels against a recorded trace of real spans.
 *
 *   make -C src test-span-bench                 # correctness, then timings
 *   make -C src test-span-bench TRACE=<file>    # against a captured trace
 *
 * WHY A REPLAY AND NOT A FRAME TIME
 *
 * The scene bench (`./launch bench osrs239-bench`) reports `render` p50 to
 * about +/-1-3% across repeats, and the whole gouraud span fill is a single
 * digit percentage of that. A kernel that wins 20% of the fill therefore moves
 * `render` by about as much as the harness's own spread, and no number of
 * repeats separates the two. Replaying the exact spans the rasterizer issued,
 * with nothing else in the loop, does.
 *
 * The trace is captured by building the client with -DTORIDRAW_SPAN_TRACE=1
 * (graphics/raster/span_census.h) and running a bench scene; every 8th span's
 * post-prologue argument tuple is written out. Without one this bench
 * synthesizes a distribution from the published census and says so -- a
 * synthetic distribution is enough to catch a correctness bug and NOT enough
 * to rank two kernels, because the thing that separates them is how often the
 * palette index actually changes, which is a property of the geometry.
 *
 * HOW THE MEMORY IS SHAPED
 *
 * Spans are drawn into a real-sized framebuffer at a cursor that walks it and
 * wraps, rather than into one hot line. A span fill is mostly stores, its cost
 * is mostly what those stores do to the cache, and a kernel timed against a
 * 4 KB buffer resident in L1 is measuring the instruction count of something
 * whose real cost is the write traffic. The trace's recorded 16-byte phase is
 * reproduced at the cursor, because whether the left end is aligned is what
 * decides between an aligned store and an unaligned one.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"

int g_toridraw_raster_scanline = 0;

// clang-format off
#include "graphics/shared_tables.c"
#include "graphics/raster/gouraudhsllightness/gouraud_span_fill.h"
// clang-format on

/* A frame's worth of pixels, so the store traffic is the real thing. */
#define BENCH_WIDTH 765
#define BENCH_HEIGHT 503
#define BENCH_PIXELS (BENCH_WIDTH * BENCH_HEIGHT)
/* Room for the longest span past the cursor, plus a guard band each side. */
#define BENCH_GUARD 4096
#define BENCH_ALLOC (BENCH_PIXELS + 2 * BENCH_GUARD)
#define BENCH_GUARD_BYTE 0x5A

struct SpanRec
{
    int stride;
    int color_hsl16_ish8;
    int color_step_hsl16_ish8;
    short align;
    short clipped;
};

typedef void (*span_fill_fn)(toripixel_t* RESTRICT, int, int, int, int);

struct Variant
{
    char const* name;
    span_fill_fn fn;
    /* A probe that deliberately draws the wrong thing, to put a floor under
     * the others. Excluded from the bit-exactness check for that reason --
     * see fill_none. */
    int is_probe;
    char const* note;
};

static void
fill_ref(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    toridraw_gouraud_span_fill_ref(buf, offset, stride, color, step);
}

static void
fill_sse2(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    toridraw_gouraud_span_fill_sse2(buf, offset, stride, color, step);
}

static void
fill_run(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    toridraw_gouraud_span_fill_run(buf, offset, stride, color, step);
}

static void
fill_edge(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    toridraw_gouraud_span_fill_edge(buf, offset, stride, color, step);
}

static void
fill_short(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    toridraw_gouraud_span_fill_short(buf, offset, stride, color, step);
}

/*
 * Draws nothing. This is the floor: replay loop, indirect call, argument
 * setup and the trace's own cache misses, with no fill under it. Any variant
 * whose time is close to this one is not being limited by its own code, and
 * no amount of kernel work will move it.
 */
static void
fill_none(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    (void)buf;
    (void)offset;
    (void)stride;
    (void)color;
    (void)step;
}

/*
 * Writes one pixel per span instead of `stride` of them. Together with `none`
 * this splits the reference's time three ways -- replay overhead, per-span
 * prologue, and the write traffic itself -- which is the split that decides
 * whether a span kernel is worth writing at all.
 */
static void
fill_first(toripixel_t* RESTRICT buf, int offset, int stride, int color, int step)
{
    (void)step;
    if( stride > 0 )
        buf[offset] = ToriDraw_Hsl16Ish8ToPixel(color);
}

static struct Variant const g_variants[] = {
    { "ref", fill_ref, 0, "today's shipping loop" },
    { "sse2", fill_sse2, 0, "one movdqu per 4-pixel block" },
    { "run", fill_run, 0, "one palette load per run of equal indices" },
    { "edge", fill_edge, 0, "branchless 1-3 px tail, reference block loop" },
    { "short", fill_short, 0, "that, plus stride<4 lifted out of the loop" },
    { "none", fill_none, 1, "PROBE: draws nothing -- replay overhead floor" },
    { "first", fill_first, 1, "PROBE: one pixel per span -- no write traffic" },
};
#define VARIANT_COUNT ((int)(sizeof(g_variants) / sizeof(g_variants[0])))

/* ------------------------------------------------------------------ trace */

/*
 * The published census from a 400-frame XP capture: 45.95% of spans are one
 * pixel, 73.19% are four or fewer, 99.28% are under 64, and the mean is 6.08
 * pixels. This reproduces that shape when no trace is available.
 */
static struct SpanRec*
synth_trace(int count)
{
    /* Cumulative percent of spans at each exact length 1..8, then the tail. */
    static double const cum[] = { 45.95, 61.78, 68.98, 73.19,
                                  75.60, 77.60, 79.60, 81.90 };
    struct SpanRec* recs = malloc((size_t)count * sizeof(*recs));
    unsigned int rng = 12345u;

    assert(recs);

    for( int i = 0; i < count; i++ )
    {
        double roll;
        int len;

        rng = rng * 1103515245u + 12345u;
        roll = 100.0 * (double)((rng >> 8) & 0xFFFFu) / 65536.0;

        len = 96;
        for( int b = 0; b < 8; b++ )
            if( roll < cum[b] )
            {
                len = b + 1;
                break;
            }
        if( len == 96 )
        {
            /* The tail: 81.9%..100% spread over 8..~700, log-uniform. */
            rng = rng * 1103515245u + 12345u;
            len = 8 + (int)((rng >> 8) % 700u);
        }

        rng = rng * 1103515245u + 12345u;
        recs[i].stride = len;
        recs[i].align = (short)((rng >> 8) & 3u);
        recs[i].clipped = 0;
        rng = rng * 1103515245u + 12345u;
        recs[i].color_hsl16_ish8 = (int)((rng >> 8) % (0xFFFFu << 8));
        rng = rng * 1103515245u + 12345u;
        /* Small steps dominate: a lit surface's colour ramp is gentle. */
        recs[i].color_step_hsl16_ish8 = (int)((rng >> 8) % 512u) - 256;
    }
    return recs;
}

static struct SpanRec*
load_trace(char const* path, int* out_count)
{
    struct SpanRec* recs;
    long bytes;
    size_t got;
    FILE* f = fopen(path, "rb");

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    bytes = ftell(f);
    fseek(f, 0, SEEK_SET);

    if( bytes <= 0 || (size_t)bytes % sizeof(*recs) != 0 )
    {
        fprintf(stderr, "span-bench: %s is %ld bytes, not a whole number of "
                        "records -- ignoring it\n", path, bytes);
        fclose(f);
        return NULL;
    }

    recs = malloc((size_t)bytes);
    assert(recs);
    got = fread(recs, sizeof(*recs), (size_t)bytes / sizeof(*recs), f);
    fclose(f);

    *out_count = (int)got;
    return recs;
}

/* ------------------------------------------------------------- correctness */

/*
 * Bit-exact or it does not ship. The 4-pixel colour quantization is visible
 * output pinned by toridraw_scanline_parity_test against the reference client,
 * so "close enough" is a rendering change wearing an optimization's clothes.
 *
 * Guard bands on both sides catch the other failure a span kernel has: a
 * vector body that rounds the count up and writes past the span's right end
 * produces a correct-looking span and corrupts the next one.
 */
static int
check_variant(struct Variant const* v, struct SpanRec const* recs, int count)
{
    size_t const bytes = BENCH_ALLOC * sizeof(toripixel_t);
    toripixel_t* a = malloc(bytes);
    toripixel_t* b = malloc(bytes);
    int failures = 0;

    assert(a);
    assert(b);

    for( int i = 0; i < count; i++ )
    {
        int const align = recs[i].align & 3;
        int const offset = BENCH_GUARD + align;

        memset(a, BENCH_GUARD_BYTE, bytes);
        memset(b, BENCH_GUARD_BYTE, bytes);

        fill_ref(
            a, offset, recs[i].stride, recs[i].color_hsl16_ish8,
            recs[i].color_step_hsl16_ish8);
        v->fn(
            b, offset, recs[i].stride, recs[i].color_hsl16_ish8,
            recs[i].color_step_hsl16_ish8);

        if( memcmp(a, b, bytes) != 0 )
        {
            if( failures < 5 )
            {
                int at = 0;
                while( at < BENCH_ALLOC && a[at] == b[at] )
                    at++;
                fprintf(
                    stderr,
                    "span-bench: %s MISMATCH span %d (stride=%d align=%d "
                    "color=%d step=%d) first at pixel %d of span: "
                    "ref=%08x got=%08x\n",
                    v->name, i, recs[i].stride, align,
                    recs[i].color_hsl16_ish8, recs[i].color_step_hsl16_ish8,
                    at - offset, (unsigned)a[at], (unsigned)b[at]);
            }
            failures++;
        }
    }

    free(a);
    free(b);
    return failures;
}

/* ------------------------------------------------------------------ timing */

static double
run_variant(struct Variant const* v, struct SpanRec const* recs, int count,
            int passes, toripixel_t* buf, double* out_pixels)
{
    clock_t const t0 = clock();
    double pixels = 0.0;

    for( int p = 0; p < passes; p++ )
    {
        int cursor = 0;

        for( int i = 0; i < count; i++ )
        {
            int const stride = recs[i].stride;
            int offset;

            if( cursor + stride + 8 >= BENCH_PIXELS )
                cursor = 0;
            /* Reproduce the recorded 16-byte phase at the cursor. */
            offset = BENCH_GUARD + ((cursor & ~3) | (recs[i].align & 3));

            v->fn(
                buf, offset, stride, recs[i].color_hsl16_ish8,
                recs[i].color_step_hsl16_ish8);

            cursor += stride + 1;
            pixels += (double)stride;
        }
    }

    *out_pixels = pixels;
    return (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
}

int
main(int argc, char** argv)
{
    char const* path = argc > 1 ? argv[1] : getenv("TORIDRAW_SPAN_TRACE_FILE");
    struct SpanRec* recs;
    toripixel_t* buf;
    int count = 0;
    int synthetic = 0;
    int failures = 0;
    int passes;
    double ref_ns_per_px = 0.0;

    init_hsl16_to_pixel_table();

    if( !path )
        path = "span_trace.bin";
    recs = load_trace(path, &count);
    if( !recs )
    {
        count = 200000;
        recs = synth_trace(count);
        synthetic = 1;
        fprintf(
            stderr,
            "span-bench: no trace at %s -- using a SYNTHETIC distribution.\n"
            "            Good enough to prove correctness, not to rank\n"
            "            kernels. Capture one with -DTORIDRAW_SPAN_TRACE=1.\n",
            path);
    }
    printf(
        "span-bench: %d spans (%s)\n", count, synthetic ? "synthetic" : path);

    /* What the trace actually is. Every conclusion below is a claim about
     * THIS distribution, so print it rather than making the reader trust a
     * percentage quoted from an older capture. */
    {
        double pixels = 0.0;
        int le4 = 0;
        int ge16 = 0;
        int aligned = 0;
        int longest = 0;

        for( int i = 0; i < count; i++ )
        {
            pixels += (double)recs[i].stride;
            if( recs[i].stride <= 4 )
                le4++;
            if( recs[i].stride >= 16 )
                ge16++;
            if( (recs[i].align & 3) == 0 )
                aligned++;
            if( recs[i].stride > longest )
                longest = recs[i].stride;
        }
        printf(
            "            %.0f px, %.2f px/span, longest %d\n"
            "            %.1f%% are <=4 px, %.1f%% are >=16 px, "
            "%.1f%% start 16B aligned\n",
            pixels, pixels / count, longest,
            100.0 * le4 / count, 100.0 * ge16 / count, 100.0 * aligned / count);
    }

    /* Correctness first, over a bounded prefix -- every span is memset and
     * memcmp'd over the whole buffer, so this is far slower per span than the
     * fill it is checking. */
    {
        int const check_count = count < 20000 ? count : 20000;

        for( int v = 1; v < VARIANT_COUNT; v++ )
        {
            int bad;

            if( g_variants[v].is_probe )
                continue;
            bad = check_variant(&g_variants[v], recs, check_count);

            printf(
                "  compare %-6s vs ref over %d spans: %s\n",
                g_variants[v].name, check_count,
                bad == 0 ? "bit-exact" : "MISMATCH");
            failures += bad;
        }
    }
    if( failures )
    {
        fprintf(stderr, "span-bench: %d mismatches, not timing them\n", failures);
        return 1;
    }

    buf = malloc(BENCH_ALLOC * sizeof(toripixel_t));
    assert(buf);
    memset(buf, 0, BENCH_ALLOC * sizeof(toripixel_t));

    /* clock() ticks at ~1 ms here, so a run that lands at 0.08 s is only 80
     * ticks of resolution -- too coarse to separate kernels a few percent
     * apart. Size the pass count for ~0.5 s of work instead of a fixed one. */
    passes = (int)(60000000.0 / (double)count);
    if( passes < 1 )
        passes = 1;
    if( passes > 400 )
        passes = 400;
    printf("\n  %-6s %10s %12s %12s  %s\n",
           "kernel", "seconds", "ns/span", "ns/pixel", "note");

    for( int v = 0; v < VARIANT_COUNT; v++ )
    {
        double pixels = 0.0;
        double secs;
        double ns_per_px;

        /* One untimed pass so every variant meets the same warm buffer. */
        run_variant(&g_variants[v], recs, count, 1, buf, &pixels);
        secs = run_variant(&g_variants[v], recs, count, passes, buf, &pixels);

        ns_per_px = secs * 1e9 / pixels;
        if( v == 0 )
            ref_ns_per_px = ns_per_px;

        printf(
            "  %-6s %10.3f %12.3f %12.3f  %s%s\n",
            g_variants[v].name, secs,
            secs * 1e9 / ((double)count * passes), ns_per_px,
            g_variants[v].note, "");
        if( v > 0 )
            printf(
                "  %-6s %10s %12s %11.1f%%  vs ref\n", "", "", "",
                100.0 * (ns_per_px - ref_ns_per_px) / ref_ns_per_px);
    }

    free(buf);
    free(recs);
    return 0;
}
