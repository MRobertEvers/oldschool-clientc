/*
 * Is the software rasterizer bound by memory, or by instructions?
 *
 * Two hand-written kernels -- the whole gouraud triangle and the whole
 * perspective-textured triangle -- have now each measured neutral on the XP
 * box despite removing real work (a call per scanline, fifteen pushes, twelve
 * redundant invariant stores, a width branch per row). Neutral twice in a row
 * is itself a result: it says the thing being removed was not what the frame
 * was waiting on. The obvious suspect is memory, and this measures it rather
 * than assuming it.
 *
 * WHY NOT A PROFILER
 *
 * A sampling profiler attributes cycles to instructions, and every one of the
 * cycles in question would be attributed to the load or the store it is
 * stalled on -- which is where they already appear to be. It cannot tell a
 * load that is slow because the bus is saturated from a load that is slow
 * because it missed a cache that a better access pattern would have hit. The
 * experiments below can, because each one holds the instruction stream fixed
 * and varies only the memory being touched.
 *
 * THE THREE EXPERIMENTS
 *
 * A. The machine's ceiling. Sequential read, sequential write, and
 *    sequential write with non-temporal stores, over a buffer far larger than
 *    L2. This is the roofline's horizontal axis, and the movntdq number is
 *    also a proposal: the framebuffer is write-only in the opaque path, so a
 *    normal store pays a read-for-ownership -- the line is fetched from memory
 *    purely to be overwritten. If NT stores are much faster here, that is a
 *    real optimisation available to SPANBODY, not just a diagnostic.
 *
 * B. Texel fetch against working-set size. The SAME gather loop, the same
 *    instruction count, the same number of fetches, over a texture atlas that
 *    grows from 16 KB to 4 MB. Only the footprint changes. If throughput is
 *    flat, texel fetch is instruction-bound and hand-written addressing can
 *    help it; if it collapses past L2, no instruction scheduling will.
 *
 * C. Span fill against working-set size. The same, with the store side
 *    included: fill spans into a framebuffer-sized target, reading texels from
 *    an atlas that is L2-resident in one arm and far larger in the other.
 *    This is B plus write traffic, which is the shape of the real kernel.
 *
 * The demand side comes from the face census over the osrs239 lumbridge bench:
 * 57,509,312 textured pixels, each costing at minimum one 4-byte texel read
 * and one 4-byte pixel write. Divide by the measured frame time and compare
 * against A. If demand is within a small factor of the ceiling, the rasterizer
 * is bandwidth-bound and the remaining work is to move fewer bytes -- not to
 * issue fewer instructions.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emmintrin.h>

#if defined(_WIN32)
#include <windows.h>
static double
now_seconds(void)
{
    LARGE_INTEGER f;
    LARGE_INTEGER t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double
now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* Big enough that L2 is irrelevant on any P4 (max 2 MB on Extreme Edition). */
#define STREAM_BYTES (16 * 1024 * 1024)

/* One measurement is repeated until it has run for at least this long, so a
 * 1 ms QueryPerformanceCounter quantum cannot be a meaningful share of it. */
#define MIN_SECONDS 0.35

static void*
alloc_aligned(size_t bytes)
{
    /* 64-byte aligned: a P4 cache line, so a "sequential" walk really is one
     * line at a time and not two half-lines. */
    void* raw = malloc(bytes + 64);
    assert(raw);
    uintptr_t p = ((uintptr_t)raw + 63u) & ~(uintptr_t)63u;
    return (void*)p;
}

/* ---------------------------------------------------------------- A ------ */

static uint32_t
stream_read(const void* src, size_t bytes)
{
    const __m128i* p = (const __m128i*)src;
    size_t n = bytes / 16u;
    __m128i acc = _mm_setzero_si128();
    for( size_t i = 0; i < n; i += 4 )
    {
        acc = _mm_add_epi32(acc, _mm_load_si128(p + i + 0));
        acc = _mm_add_epi32(acc, _mm_load_si128(p + i + 1));
        acc = _mm_add_epi32(acc, _mm_load_si128(p + i + 2));
        acc = _mm_add_epi32(acc, _mm_load_si128(p + i + 3));
    }
    return (uint32_t)_mm_cvtsi128_si32(acc);
}

static void
stream_write(void* dst, size_t bytes, uint32_t value)
{
    __m128i* p = (__m128i*)dst;
    size_t n = bytes / 16u;
    __m128i v = _mm_set1_epi32((int)value);
    for( size_t i = 0; i < n; i += 4 )
    {
        _mm_store_si128(p + i + 0, v);
        _mm_store_si128(p + i + 1, v);
        _mm_store_si128(p + i + 2, v);
        _mm_store_si128(p + i + 3, v);
    }
}

static void
stream_write_nt(void* dst, size_t bytes, uint32_t value)
{
    __m128i* p = (__m128i*)dst;
    size_t n = bytes / 16u;
    __m128i v = _mm_set1_epi32((int)value);
    for( size_t i = 0; i < n; i += 4 )
    {
        _mm_stream_si128(p + i + 0, v);
        _mm_stream_si128(p + i + 1, v);
        _mm_stream_si128(p + i + 2, v);
        _mm_stream_si128(p + i + 3, v);
    }
    _mm_sfence();
}

/* ---------------------------------------------------------------- B ------ */

/*
 * The texel address the real kernel forms, reduced to its memory behaviour:
 * a row index and a column index combined by a shift, where consecutive
 * fetches move by a fractional step in both. The steps are deliberately not
 * powers of two -- a texture walk that advanced by exactly one texel per pixel
 * would be a sequential read and would measure the prefetcher, not the gather.
 *
 * atlas_rows must be a power of two. Every arm of the sweep executes this
 * identical loop with an identical fetch count; only atlas_rows differs, so
 * the only thing being varied is how much memory the same instructions touch.
 */
static uint32_t
gather_pass(const uint32_t* atlas, uint32_t atlas_rows, int fetches)
{
    const uint32_t row_mask = atlas_rows - 1u;
    const uint32_t col_mask = 63u; /* a 64-texel row, as in the 64-wide case */
    uint32_t acc = 0;
    uint32_t u = 0x0013579bu;
    uint32_t v = 0x02468acdu;

    for( int i = 0; i < fetches; ++i )
    {
        uint32_t idx = (((v >> 16) & row_mask) << 6) | ((u >> 16) & col_mask);
        acc += atlas[idx];
        u += 0x00013b71u;
        v += 0x0000a3d7u;
    }
    return acc;
}

/* ---------------------------------------------------------------- C ------ */

/*
 * B with the store side attached: the same gather, writing each fetched texel
 * to a framebuffer-sized destination that is walked with a wrapping cursor.
 * This is the shape of the real fill -- one texel read and one pixel written
 * per iteration -- so its collapse point, or lack of one, is the one that
 * speaks about the shipped kernel.
 */
static void
fill_pass(
    uint32_t* dst, size_t dst_pixels, const uint32_t* atlas,
    uint32_t atlas_rows, int pixels)
{
    const uint32_t row_mask = atlas_rows - 1u;
    const uint32_t col_mask = 63u;
    uint32_t u = 0x0013579bu;
    uint32_t v = 0x02468acdu;
    size_t cursor = 0;

    for( int i = 0; i < pixels; ++i )
    {
        uint32_t idx = (((v >> 16) & row_mask) << 6) | ((u >> 16) & col_mask);
        dst[cursor] = atlas[idx];
        cursor++;
        if( cursor == dst_pixels )
            cursor = 0;
        u += 0x00013b71u;
        v += 0x0000a3d7u;
    }
}

/* ---------------------------------------------------------------- E ------ */

/*
 * Does a non-temporal store still pay at SPAN length?
 *
 * Section A measured movntdq over a 16 MB contiguous stream and found it 2.28x
 * a normal store, because a normal store to a write-only buffer pays a
 * read-for-ownership: the line is dragged in from RAM purely to be overwritten.
 * That is a real effect, but it is measured on the friendliest possible shape.
 *
 * The rasterizer writes nothing like it. The census says 7.24 px per span --
 * 29 bytes, less than two cache lines -- at a scattered destination, one short
 * run per row. A non-temporal store fills a write-combine buffer, and the P4
 * has only a handful of them; a buffer evicted before it is full goes to RAM as
 * a PARTIAL line write, which is the slowest transaction the memory controller
 * performs. Short scattered runs are exactly the shape that leaves buffers
 * partial.
 *
 * So the two effects pull opposite ways and the answer is a length: below some
 * run length the partial-line penalty dominates the saved ownership read, and
 * above it the trade flips. This finds that length instead of assuming it, by
 * writing the SAME total pixel count into the SAME framebuffer with the same
 * addressing, varying only the store instruction and the run length.
 */
static void
span_store_normal(uint32_t* fb, size_t fb_pixels, int spans, int run, uint32_t v)
{
    /* Advance by a prime stride so successive spans land on unrelated lines,
     * the way successive rows of a triangle do, rather than walking forward
     * into the hardware prefetcher's arms. */
    size_t at = 0;
    __m128i vv = _mm_set1_epi32((int)v);
    for( int s = 0; s < spans; ++s )
    {
        uint32_t* p = fb + at;
        int i = 0;
        for( ; i + 4 <= run; i += 4 )
            _mm_storeu_si128((__m128i*)(p + i), vv);
        for( ; i < run; ++i )
            p[i] = v;
        at += 1021u;
        if( at + (size_t)run >= fb_pixels )
            at = 0;
    }
}

static void
span_store_nt(uint32_t* fb, size_t fb_pixels, int spans, int run, uint32_t v)
{
    /*
     * movntdq requires 16-byte alignment, so the head and tail that fall
     * outside an aligned block must stay normal stores -- which is precisely
     * what a real kernel would have to do, and part of what is being measured.
     * At 7.24 px/span most spans are ALL head and tail.
     */
    size_t at = 0;
    __m128i vv = _mm_set1_epi32((int)v);
    for( int s = 0; s < spans; ++s )
    {
        uint32_t* p = fb + at;
        int i = 0;
        while( i < run && (((uintptr_t)(p + i)) & 15u) != 0 )
            p[i++] = v;
        for( ; i + 4 <= run; i += 4 )
            _mm_stream_si128((__m128i*)(p + i), vv);
        for( ; i < run; ++i )
            p[i] = v;
        at += 1021u;
        if( at + (size_t)run >= fb_pixels )
            at = 0;
    }
    _mm_sfence();
}

struct SpanCtx
{
    uint32_t* fb;
    size_t fb_pixels;
    int spans;
    int run;
    int nt;
};

static double
span_once(void* p)
{
    struct SpanCtx* c = (struct SpanCtx*)p;
    if( c->nt )
        span_store_nt(c->fb, c->fb_pixels, c->spans, c->run, 0x00ff00ffu);
    else
        span_store_normal(c->fb, c->fb_pixels, c->spans, c->run, 0x00ff00ffu);
    return 0.0;
}

/* -------------------------------------------------------------- driver --- */

static double
timed(double (*once)(void*), void* ctx)
{
    /* Warm, then repeat until the clock quantum is negligible. */
    once(ctx);
    int reps = 1;
    for( ;; )
    {
        double t0 = now_seconds();
        for( int i = 0; i < reps; ++i )
            once(ctx);
        double dt = now_seconds() - t0;
        if( dt >= MIN_SECONDS )
            return dt / (double)reps;
        reps *= 2;
    }
}

struct StreamCtx
{
    void* buf;
    size_t bytes;
    int mode; /* 0 read, 1 write, 2 nt write */
    uint32_t sink;
};

static double
stream_once(void* p)
{
    struct StreamCtx* c = (struct StreamCtx*)p;
    if( c->mode == 0 )
        c->sink += stream_read(c->buf, c->bytes);
    else if( c->mode == 1 )
        stream_write(c->buf, c->bytes, c->sink | 1u);
    else
        stream_write_nt(c->buf, c->bytes, c->sink | 1u);
    return 0.0;
}

struct GatherCtx
{
    const uint32_t* atlas;
    uint32_t rows;
    int fetches;
    uint32_t sink;
};

static double
gather_once(void* p)
{
    struct GatherCtx* c = (struct GatherCtx*)p;
    c->sink += gather_pass(c->atlas, c->rows, c->fetches);
    return 0.0;
}

struct FillCtx
{
    uint32_t* dst;
    size_t dst_pixels;
    const uint32_t* atlas;
    uint32_t rows;
    int pixels;
};

static double
fill_once(void* p)
{
    struct FillCtx* c = (struct FillCtx*)p;
    fill_pass(c->dst, c->dst_pixels, c->atlas, c->rows, c->pixels);
    return 0.0;
}

/*
 * The census, so the demand side is printed next to the ceiling instead of
 * being worked out by hand afterwards and getting it wrong.
 *
 * MEASURED, not quoted. The 57,509,312-pixel figure in tex_tri_asm.h is a
 * total over a frame count nobody wrote down, and dividing it by a guess is
 * how a bandwidth argument gets made against the wrong denominator -- the
 * first version of this file assumed 1500 frames and was out by 3.75x. These
 * come from a census build run over an explicitly pinned 500 frames, on the
 * same camera as the A/B.
 *
 * The span count is here because it, not the pixel count, is what the texture
 * path's cost tracks: 7.24 px/span and 69.8% of spans never running a single
 * 8-pixel block means the fill is dominated by per-span SETUP. A kernel change
 * that speeds up the fill body is optimising the smaller half.
 */
#define CENSUS_TEXTURED_PIXELS 71930448.0
#define CENSUS_FRAMES 500.0
#define CENSUS_SPANS 9938358.0
#define CENSUS_BYTES_PER_PIXEL 8.0 /* one 4-byte texel in, one 4-byte pixel out */

int
main(int argc, char** argv)
{
    /* Seconds of the measured frame that the rasterizer is thought to occupy;
     * only used to turn the census into a bandwidth demand. */
    double frame_seconds = (argc > 1) ? atof(argv[1]) : 0.0377;

    printf("toridraw memory-bandwidth probe\n");
    printf("  stream buffer %d MB, min %.2f s per measurement\n\n",
           STREAM_BYTES / (1024 * 1024), MIN_SECONDS);

    /* ---- A ---- */
    void* big = alloc_aligned(STREAM_BYTES);
    memset(big, 0x5A, STREAM_BYTES);

    printf("A. machine ceiling\n");
    static const char* names[3] = { "read      ", "write     ",
                                    "write (nt)" };
    double wr = 0.0;
    double ntwr = 0.0;
    for( int mode = 0; mode < 3; ++mode )
    {
        struct StreamCtx c;
        c.buf = big;
        c.bytes = STREAM_BYTES;
        c.mode = mode;
        c.sink = 1u;
        double s = timed(stream_once, &c);
        double gbs = (double)STREAM_BYTES / s / (1024.0 * 1024.0 * 1024.0);
        printf("   %s  %8.3f ms   %6.3f GB/s\n", names[mode], s * 1e3, gbs);
        if( mode == 1 )
            wr = gbs;
        if( mode == 2 )
            ntwr = gbs;
    }
    if( wr > 0.0 )
        printf("   non-temporal stores are %.2fx the normal-store rate\n",
               ntwr / wr);

    /* ---- B ---- */
    printf("\nB. texel gather vs working set (identical instruction stream)\n");
    const int FETCHES = 4 * 1000 * 1000;
    uint32_t* atlas = (uint32_t*)alloc_aligned(4u * 1024u * 1024u);
    for( size_t i = 0; i < (4u * 1024u * 1024u) / 4u; ++i )
        atlas[i] = (uint32_t)(i * 2654435761u);

    double base_rate = 0.0;
    for( uint32_t rows = 64; rows <= 16384; rows *= 4 )
    {
        struct GatherCtx c;
        c.atlas = atlas;
        c.rows = rows;
        c.fetches = FETCHES;
        c.sink = 0;
        double s = timed(gather_once, &c);
        double kb = (double)rows * 64.0 * 4.0 / 1024.0;
        double mfetch = (double)FETCHES / s / 1e6;
        if( base_rate == 0.0 )
            base_rate = mfetch;
        printf("   %7.0f KB   %8.3f ms   %7.2f Mfetch/s   %.2fx of L2-resident\n",
               kb, s * 1e3, mfetch, mfetch / base_rate);
    }

    /* ---- C ---- */
    printf("\nC. gather + store vs working set (the shape of the real fill)\n");
    const int PIXELS = 2 * 1000 * 1000;
    const size_t FB_PIXELS = 765u * 503u;
    uint32_t* fb = (uint32_t*)alloc_aligned(FB_PIXELS * 4u + 64u);
    memset(fb, 0, FB_PIXELS * 4u);

    base_rate = 0.0;
    for( uint32_t rows = 64; rows <= 16384; rows *= 4 )
    {
        struct FillCtx c;
        c.dst = fb;
        c.dst_pixels = FB_PIXELS;
        c.atlas = atlas;
        c.rows = rows;
        c.pixels = PIXELS;
        double s = timed(fill_once, &c);
        double kb = (double)rows * 64.0 * 4.0 / 1024.0;
        double mpx = (double)PIXELS / s / 1e6;
        double gbs = (double)PIXELS * CENSUS_BYTES_PER_PIXEL / s
                     / (1024.0 * 1024.0 * 1024.0);
        if( base_rate == 0.0 )
            base_rate = mpx;
        printf("   %7.0f KB   %8.3f ms   %7.2f Mpx/s  %6.3f GB/s  %.2fx\n",
               kb, s * 1e3, mpx, gbs, mpx / base_rate);
    }

    /* ---- demand ---- */
    printf("\nD. what the rasterizer actually asks for\n");
    double px_per_frame = CENSUS_TEXTURED_PIXELS / CENSUS_FRAMES;
    double bytes_per_frame = px_per_frame * CENSUS_BYTES_PER_PIXEL;
    printf("   census: %.0f textured px/frame, >= %.2f MB/frame of texel+pixel\n",
           px_per_frame, bytes_per_frame / (1024.0 * 1024.0));
    printf("   at a %.1f ms frame that is %.3f GB/s of demand\n",
           frame_seconds * 1e3,
           bytes_per_frame / frame_seconds / (1024.0 * 1024.0 * 1024.0));
    printf("   compare against A's write rate and C's achieved rate above.\n");
    printf("   %.0f spans/frame at %.2f px/span: the texture path's cost is\n"
           "   span setup, not fill body, so that is where a kernel wins.\n",
           CENSUS_SPANS / CENSUS_FRAMES,
           CENSUS_TEXTURED_PIXELS / CENSUS_SPANS);

    /* ---- E ---- */
    printf("\nE. normal vs non-temporal store at SPAN length\n");
    printf("   (same pixels written, same framebuffer, only the store differs)\n");
    {
        const int SPAN_PIXELS = 4 * 1000 * 1000;
        static const int runs[6] = { 7, 16, 32, 64, 256, 765 };
        for( int r = 0; r < 6; ++r )
        {
            int run = runs[r];
            int spans = SPAN_PIXELS / run;
            struct SpanCtx c;
            double s_norm;
            double s_nt;
            c.fb = fb;
            c.fb_pixels = FB_PIXELS;
            c.spans = spans;
            c.run = run;
            c.nt = 0;
            s_norm = timed(span_once, &c);
            c.nt = 1;
            s_nt = timed(span_once, &c);
            printf("   run %4d px   normal %7.2f Mpx/s   nt %7.2f Mpx/s   "
                   "nt is %.2fx\n",
                   run,
                   (double)(spans * run) / s_norm / 1e6,
                   (double)(spans * run) / s_nt / 1e6,
                   s_norm / s_nt);
        }
        printf("   the census run length is %.2f px -- read the row nearest it,\n"
               "   not the long ones, when deciding whether to mirror a kernel.\n",
               CENSUS_TEXTURED_PIXELS / CENSUS_SPANS);
    }

    return 0;
}
