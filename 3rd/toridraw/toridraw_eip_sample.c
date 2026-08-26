#include "toridraw_eip_sample.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

/** 16 bytes per bin: fine enough to split adjacent functions, coarse enough
 *  that a 16 MB image costs a 4 MB table. */
#define EIP_BIN_SHIFT 4

/** Samples outside the main image are binned by address.
 *
 * 64 KB (shift 16) names the DLL and no more, which is all that is usually
 * worth knowing. But when a quarter of the frame is inside ntdll, "it is
 * inside ntdll" is the beginning of the question rather than the answer --
 * so the shift is tunable, and at 6 (64-byte bins) the dump carries enough
 * offset to name the function against the DLL export table.
 *
 * The slot table is a linear scan per sample, so a finer shift needs more
 * slots: one page of ntdll at 64-byte bins is up to 1024 of them. Only the
 * bins that are actually hit take a slot. */
#define EIP_PAGE_SHIFT_DEFAULT 16
#define EIP_OTHER_MAX 4096

struct EipOther
{
    uint32_t page;
    uint32_t count;
};

/* Resolved once, before the sampling thread starts. */
static unsigned int g_eip_other_shift = EIP_PAGE_SHIFT_DEFAULT;

/*
 * Call-stack capture, on top of the flat EIP histogram.
 *
 * The histogram answers "which address was executing", which is self time and
 * nothing else. It cannot say who called it, so a cost that is spread over many
 * callers -- a span fill, a memcpy, a palette lookup -- shows up as one hot
 * address with no way to tell which pass is responsible. That is exactly the
 * question a flamegraph answers, and it needs stacks.
 *
 * Walked off the frame-pointer chain, which is free here: on i386
 * CONTEXT_CONTROL already carries Ebp alongside Eip, so the suspend the
 * histogram is already paying for yields the whole walk. It does require the
 * build to keep frame pointers -- see TORIDRAW_EIP_STACKS in the makefile.
 *
 * Raw addresses are dumped and symbolised offline. Doing it in-process would
 * mean loading dbghelp and resolving symbols while the render thread is
 * suspended, which is the one thing this file is careful never to do.
 */
#define EIP_STACK_DEPTH 24
/* 30 s at ~1 kHz. Preallocated at start: the sampler thread must not allocate
 * while the target is suspended. Overflow stops recording rather than wrapping,
 * so a truncated capture is short but not a mixture of two time windows. */
#define EIP_STACK_MAX 40000

struct EipSampler
{
    int inited;
    int enabled;
    int running;
    int want_stacks;

    uint32_t* stack_buf; /* EIP_STACK_MAX * EIP_STACK_DEPTH, 0-terminated rows */
    uint32_t stack_used;
    uint32_t stack_lost;

    HANDLE target;
    HANDLE thread;
    volatile LONG stop;

    uintptr_t base;
    uint32_t image_size;
    uint32_t bins;
    uint32_t* bin;

    struct EipOther other[EIP_OTHER_MAX];
    uint32_t other_used;
    uint32_t other_lost;

    uint32_t total;
    uint32_t in_image;
    uint32_t failed;

    LARGE_INTEGER t0;
    LARGE_INTEGER t1;

    HMODULE winmm;
    UINT (WINAPI* begin_period)(UINT);
    UINT (WINAPI* end_period)(UINT);
};

static struct EipSampler g_eip;

/*
 * Called only from the sampler thread, and only while the target is
 * suspended. It allocates nothing and takes no lock -- see the header for why
 * that is a hard requirement and not a style preference.
 */
static void
eip_record(struct EipSampler* s, uintptr_t eip)
{
    uintptr_t off = eip - s->base;
    uint32_t page;
    uint32_t i;

    s->total++;

    if( eip >= s->base && off < (uintptr_t)s->image_size )
    {
        s->bin[off >> EIP_BIN_SHIFT]++;
        s->in_image++;
        return;
    }

    page = (uint32_t)(eip >> g_eip_other_shift);
    for( i = 0; i < s->other_used; i++ )
    {
        if( s->other[i].page == page )
        {
            s->other[i].count++;
            return;
        }
    }
    if( s->other_used < EIP_OTHER_MAX )
    {
        s->other[s->other_used].page = page;
        s->other[s->other_used].count = 1;
        s->other_used++;
        return;
    }
    s->other_lost++;
}

#if !defined(_WIN64)
/*
 * Walk the frame-pointer chain of the suspended target.
 *
 * Every read is bounded by the thread's own stack region, queried once per
 * sample from Esp: a frame pointer that has been clobbered, or a frame in a
 * function compiled without one, otherwise sends this straight into unmapped
 * memory. VirtualQuery gives the committed range, so a pointer inside it is
 * mapped by construction and no SEH or IsBadReadPtr guess is needed.
 *
 * The chain must walk STRICTLY upward. Stacks grow down, so a caller's frame
 * always sits at a higher address; anything else is a corrupt or hostile chain
 * and ends the walk rather than looping.
 */
static void
eip_walk_stack(struct EipSampler* s, const CONTEXT* ctx)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t lo;
    uintptr_t hi;
    uintptr_t ebp;
    uint32_t* row;
    int n = 0;

    if( s->stack_used >= EIP_STACK_MAX )
    {
        s->stack_lost++;
        return;
    }
    if( !VirtualQuery((LPCVOID)(uintptr_t)ctx->Esp, &mbi, sizeof(mbi)) )
        return;
    lo = (uintptr_t)mbi.BaseAddress;
    hi = lo + (uintptr_t)mbi.RegionSize;

    row = s->stack_buf + (size_t)s->stack_used * EIP_STACK_DEPTH;
    row[n++] = (uint32_t)ctx->Eip;

    ebp = (uintptr_t)ctx->Ebp;
    while( n < EIP_STACK_DEPTH - 1 )
    {
        uintptr_t ret;
        uintptr_t next;

        /* Both slots of the frame must be inside the region, and aligned. */
        if( ebp < lo || ebp + 8 > hi || (ebp & 3u) )
            break;
        next = *(uintptr_t const*)ebp;
        ret = *(uintptr_t const*)(ebp + sizeof(uintptr_t));
        if( ret == 0 )
            break;
        row[n++] = (uint32_t)ret;
        if( next <= ebp )
            break; /* not strictly upward: end of chain, or a bad frame */
        ebp = next;
    }
    row[n] = 0;
    s->stack_used++;
}
#endif

static DWORD WINAPI
eip_thread(LPVOID arg)
{
    struct EipSampler* s = (struct EipSampler*)arg;
    CONTEXT ctx;

    while( InterlockedCompareExchange(&s->stop, 0, 0) == 0 )
    {
        /*
         * Sleep(1), and accept what it gives.
         *
         * Under timeBeginPeriod(1) this delivers about 500 Hz on the XP target,
         * not the 1000 the interval nominally asks for -- the scheduler rounds
         * up. That is fine: 500 Hz over 40 s is 20,000 samples, which resolves
         * a function at well under a percent.
         *
         * It is deliberately NOT paced on a deadline against
         * QueryPerformanceCounter. That was tried: at 1 kHz the period is 1 ms,
         * so any wait short enough to hit the deadline degenerates into
         * Sleep(0), and on a SINGLE-CORE box a yield loop competes with the very
         * thread it is sampling. The client dropped below its frame cap and
         * never finished the run. A sampler that changes the workload is worse
         * than a slow one.
         */
        Sleep(1);

        if( SuspendThread(s->target) == (DWORD)-1 )
        {
            s->failed++;
            continue;
        }

        /*
         * CONTEXT_CONTROL is the cheapest request that carries Eip. Asking
         * for the integer or FP state as well would triple the cost of a
         * suspend that the render thread is paying for.
         */
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL;
        if( GetThreadContext(s->target, &ctx) )
        {
#if defined(_WIN64)
            eip_record(s, (uintptr_t)ctx.Rip);
#else
            eip_record(s, (uintptr_t)ctx.Eip);
            if( s->want_stacks && s->stack_buf )
                eip_walk_stack(s, &ctx);
#endif
        }
        else
            s->failed++;

        ResumeThread(s->target);
    }
    return 0;
}

static void
eip_init(void)
{
    const char* v;
    HMODULE mod;
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;

    if( g_eip.inited )
        return;
    g_eip.inited = 1;

    v = getenv("TORIDRAW_EIP_SAMPLE");
    g_eip.enabled = (v && atoi(v) != 0);
    if( !g_eip.enabled )
        return;

    /* Bin width for samples OUTSIDE the main image, as a shift. The default
     * names the DLL; a smaller value keeps enough offset to name the
     * function it landed in. Clamped so a typo cannot turn every sample
     * into its own slot and overflow the table on the first frame. */
    v = getenv("TORIDRAW_EIP_OTHER_SHIFT");
    if( v )
    {
        int shift = atoi(v);
        if( shift < 4 )
            shift = 4;
        if( shift > 16 )
            shift = 16;
        g_eip_other_shift = (unsigned int)shift;
    }

#if !defined(_WIN64)
    /* Stacks are opt-in on top of the histogram: they cost 3.7 MB of buffer
     * and are only meaningful in a build that kept frame pointers. */
    v = getenv("TORIDRAW_EIP_STACKS");
    g_eip.want_stacks = (v && atoi(v) != 0);
    if( g_eip.want_stacks )
    {
        g_eip.stack_buf = (uint32_t*)calloc(
            (size_t)EIP_STACK_MAX * EIP_STACK_DEPTH, sizeof(uint32_t));
        assert(g_eip.stack_buf);
    }
#endif

    mod = GetModuleHandleA(NULL);
    assert(mod);
    dos = (IMAGE_DOS_HEADER*)mod;
    nt = (IMAGE_NT_HEADERS*)((char*)mod + dos->e_lfanew);

    g_eip.base = (uintptr_t)mod;
    g_eip.image_size = (uint32_t)nt->OptionalHeader.SizeOfImage;
    g_eip.bins = (g_eip.image_size >> EIP_BIN_SHIFT) + 1u;
    g_eip.bin = (uint32_t*)calloc(g_eip.bins, sizeof(uint32_t));
    assert(g_eip.bin);
}

void
ToriDraw_EipSampleStart(void)
{
    DWORD tid;

    eip_init();
    if( !g_eip.enabled )
        return;
    if( g_eip.running )
        return;

    /*
     * GetCurrentThread() is a pseudo-handle that means "whichever thread is
     * asking" -- handing it to the sampler would make the sampler suspend
     * ITSELF. It has to be duplicated into a real handle first.
     */
    if( !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                         GetCurrentProcess(), &g_eip.target,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                         FALSE, 0) )
    {
        fprintf(stderr, "eip-sample: cannot duplicate the render thread "
                        "handle (%lu); sampling disabled\n",
                (unsigned long)GetLastError());
        g_eip.enabled = 0;
        return;
    }

    /*
     * Sleep(1) rounds up to the system timer tick, which is 15.6 ms out of the
     * box -- 64 samples a second, where a 37.7 ms frame needs a thousand.
     * timeBeginPeriod(1) buys the millisecond tick. Loaded dynamically so
     * this does not add -lwinmm to every link that includes the file.
     */
    g_eip.winmm = LoadLibraryA("winmm.dll");
    if( g_eip.winmm )
    {
        g_eip.begin_period = (UINT (WINAPI*)(UINT))(void*)
            GetProcAddress(g_eip.winmm, "timeBeginPeriod");
        g_eip.end_period = (UINT (WINAPI*)(UINT))(void*)
            GetProcAddress(g_eip.winmm, "timeEndPeriod");
        if( g_eip.begin_period )
            g_eip.begin_period(1);
    }

    QueryPerformanceCounter(&g_eip.t0);
    g_eip.stop = 0;
    g_eip.thread = CreateThread(NULL, 0, eip_thread, &g_eip, 0, &tid);
    assert(g_eip.thread);

    /* Above the render thread so a sample is taken promptly rather than
     * whenever the render thread happens to yield. */
    SetThreadPriority(g_eip.thread, THREAD_PRIORITY_ABOVE_NORMAL);
    g_eip.running = 1;
}

/*
 * The loaded module ranges, so the out-of-image pages above resolve to names.
 * Called only from Stop, with the sampler thread already joined -- it
 * allocates and takes kernel locks, neither of which is permissible on the
 * record path.
 */
static void
eip_dump_modules(FILE* f)
{
    HANDLE snap;
    MODULEENTRY32 me;

    assert(f);

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if( snap == INVALID_HANDLE_VALUE )
    {
        fprintf(f, "MODERR %lu\n", (unsigned long)GetLastError());
        return;
    }

    memset(&me, 0, sizeof(me));
    me.dwSize = sizeof(me);
    if( Module32First(snap, &me) )
    {
        do
        {
            fprintf(f, "MOD %08lx %08lx %s\n",
                    (unsigned long)(uintptr_t)me.modBaseAddr,
                    (unsigned long)me.modBaseSize,
                    me.szModule);
        } while( Module32Next(snap, &me) );
    }
    CloseHandle(snap);
}

void
ToriDraw_EipSampleStop(const char* label)
{
    const char* path;
    FILE* f;
    uint32_t i;
    double secs;
    LARGE_INTEGER freq;

    assert(label);

    eip_init();
    if( !g_eip.enabled )
        return;
    if( !g_eip.running )
        return;
    g_eip.running = 0;

    InterlockedExchange(&g_eip.stop, 1);
    WaitForSingleObject(g_eip.thread, 2000);
    CloseHandle(g_eip.thread);
    QueryPerformanceCounter(&g_eip.t1);

    if( g_eip.end_period )
        g_eip.end_period(1);
    if( g_eip.winmm )
        FreeLibrary(g_eip.winmm);
    CloseHandle(g_eip.target);

    QueryPerformanceFrequency(&freq);
    secs = (double)(g_eip.t1.QuadPart - g_eip.t0.QuadPart)
           / (double)freq.QuadPart;

#if !defined(_WIN64)
    /*
     * Stacks go to their own file, one sample per line, leaf first, as raw
     * hex module-relative addresses. Kept separate from the histogram dump so
     * the folded-stack conversion is a plain read rather than a parse of two
     * interleaved formats, and symbolised offline against the -g binary.
     */
    if( g_eip.want_stacks && g_eip.stack_buf )
    {
        const char* sp = getenv("TORIDRAW_EIP_STACKS_FILE");
        FILE* sf;

        if( !sp )
            sp = "eipstacks.txt";
        sf = fopen(sp, "w");
        assert(sf);
        fprintf(sf, "# base=%08lx size=%lu samples=%lu lost=%lu secs=%.3f\n",
                (unsigned long)g_eip.base, (unsigned long)g_eip.image_size,
                (unsigned long)g_eip.stack_used, (unsigned long)g_eip.stack_lost,
                secs);
        for( uint32_t i = 0; i < g_eip.stack_used; i++ )
        {
            const uint32_t* row = g_eip.stack_buf + (size_t)i * EIP_STACK_DEPTH;
            for( int j = 0; j < EIP_STACK_DEPTH && row[j]; j++ )
                fprintf(sf, "%s%08lx", j ? " " : "", (unsigned long)row[j]);
            fputc('\n', sf);
        }
        fclose(sf);
        free(g_eip.stack_buf);
        g_eip.stack_buf = NULL;
    }
#endif

    path = getenv("TORIDRAW_EIP_SAMPLE_FILE");
    if( !path )
        path = "eipsample.txt";

    f = fopen(path, "w");
    assert(f);

    fprintf(f, "=== toridraw eip sample: %s ===\n", label);
    fprintf(f, "module_base 0x%08lx\n", (unsigned long)g_eip.base);
    fprintf(f, "image_size 0x%08lx\n", (unsigned long)g_eip.image_size);
    fprintf(f, "bin_bytes %d\n", 1 << EIP_BIN_SHIFT);
    fprintf(f, "other_bin_bytes %lu\n", (unsigned long)(1ul << g_eip_other_shift));
    fprintf(f, "seconds %.3f\n", secs);
    fprintf(f, "samples_total %lu\n", (unsigned long)g_eip.total);
    fprintf(f, "samples_in_image %lu\n", (unsigned long)g_eip.in_image);
    fprintf(f, "samples_other %lu\n",
            (unsigned long)(g_eip.total - g_eip.in_image));
    fprintf(f, "suspend_failures %lu\n", (unsigned long)g_eip.failed);
    fprintf(f, "other_pages_lost %lu\n", (unsigned long)g_eip.other_lost);

    /* Emitted in address order rather than sorted: sorting here would mean
     * allocating and qsorting a few hundred thousand entries at shutdown, and
     * the off-box resolver has to walk every line anyway to attach symbols. */
    for( i = 0; i < g_eip.bins; i++ )
    {
        if( g_eip.bin[i] )
            fprintf(f, "T %08lx %lu\n",
                    (unsigned long)(i << EIP_BIN_SHIFT),
                    (unsigned long)g_eip.bin[i]);
    }
    for( i = 0; i < g_eip.other_used; i++ )
        fprintf(f, "M %08lx %lu\n",
                (unsigned long)(g_eip.other[i].page << g_eip_other_shift),
                (unsigned long)g_eip.other[i].count);

    eip_dump_modules(f);
    fclose(f);

    fprintf(stderr, "eip-sample %s: %lu samples over %.3f s (%.0f Hz), "
                    "%lu in image, %lu elsewhere -> %s\n",
            label, (unsigned long)g_eip.total, secs,
            secs > 0.0 ? (double)g_eip.total / secs : 0.0,
            (unsigned long)g_eip.in_image,
            (unsigned long)(g_eip.total - g_eip.in_image), path);
}

#else /* !_WIN32 */

void
ToriDraw_EipSampleStart(void)
{
}

void
ToriDraw_EipSampleStop(const char* label)
{
    assert(label);
    (void)label;
}

#endif
