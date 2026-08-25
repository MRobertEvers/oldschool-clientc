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

/** Samples outside the main image are attributed per 64 KB page. That names
 *  the DLL, which is all that is worth knowing about them here. */
#define EIP_PAGE_SHIFT 16
#define EIP_OTHER_MAX 512

struct EipOther
{
    uint32_t page;
    uint32_t count;
};

struct EipSampler
{
    int inited;
    int enabled;
    int running;

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

    page = (uint32_t)(eip >> EIP_PAGE_SHIFT);
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

static DWORD WINAPI
eip_thread(LPVOID arg)
{
    struct EipSampler* s = (struct EipSampler*)arg;
    CONTEXT ctx;

    while( InterlockedCompareExchange(&s->stop, 0, 0) == 0 )
    {
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
            eip_record(s, (uintptr_t)ctx.Eip);
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

    path = getenv("TORIDRAW_EIP_SAMPLE_FILE");
    if( !path )
        path = "eipsample.txt";

    f = fopen(path, "w");
    assert(f);

    fprintf(f, "=== toridraw eip sample: %s ===\n", label);
    fprintf(f, "module_base 0x%08lx\n", (unsigned long)g_eip.base);
    fprintf(f, "image_size 0x%08lx\n", (unsigned long)g_eip.image_size);
    fprintf(f, "bin_bytes %d\n", 1 << EIP_BIN_SHIFT);
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
                (unsigned long)(g_eip.other[i].page << EIP_PAGE_SHIFT),
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
