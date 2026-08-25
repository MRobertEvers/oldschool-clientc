/**
 * Retained-heap profiler for the Windows lanes (opt-in via -DTORIRS_MEMPROF=1).
 *
 * platform/torirs_memtrace.c answers a different question -- it streams every
 * heap event to a file so a viewer can replay the allocation timeline -- and it
 * is written against execinfo/dlfcn/pthread, none of which mingw has. This one
 * answers "what is still allocated when we exit, and who allocated it", which
 * is the question a memory-footprint investigation actually asks.
 *
 * The block carries its own bookkeeping.
 *
 * The obvious design -- a hash table from live pointer to size and call site --
 * is what the first three versions of this file did, and none of them reached a
 * world: every malloc and every free paid a lock, a hash, and a probe walk over
 * a table far larger than cache, millions of times during boot. A profiler that
 * changes the program's behaviour by two orders of magnitude measures itself.
 *
 * So each allocation is over-allocated by one header and the user pointer is
 * offset past it. free() reads size and site straight out of the header: no
 * table, no lock, no probing, two cache lines that are already hot because they
 * sit against the block being freed. A magic word makes the header
 * self-identifying, so a pointer that came from somewhere else -- the CRT's own
 * startup, before the hooks were armed -- is passed through untouched instead
 * of being mis-parsed.
 *
 * Call sites are __builtin_return_address(0), a register read, interned in a
 * direct-mapped table (one probe, never a walk). Two sites hashing to the same
 * slot merge; the report counts how often that happened so a skewed ranking
 * announces itself rather than lying quietly.
 *
 * Output: a report on stderr (or TORIRS_MEMPROF_OUT) ranking call sites by
 * retained bytes. Addresses print as link-time addresses, so
 *   addr2line -f -C -i -e src/torirs.exe <addr>...
 * names them even through LTO inlining.
 */

#if defined(TORIRS_MEMPROF)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern void* __real_malloc(size_t size);
extern void* __real_calloc(size_t nmemb, size_t size);
extern void* __real_realloc(void* ptr, size_t size);
extern void __real_free(void* ptr);
extern char* __real_strdup(const char* str);

/* 16 bytes rather than the 12 the fields need: malloc's alignment guarantee has
 * to survive the offset, and 16 is the widest this tree asks for (SSE spans). */
#define MEMPROF_HDR_BYTES 16u
#define MEMPROF_MAGIC 0x4D50524Fu /* 'MPRO' */

#define MEMPROF_SITE_BITS 19u
#define MEMPROF_SITE_CAP (1u << MEMPROF_SITE_BITS)
#define MEMPROF_REPORT_SITES 80u

/* TORIRS_MEMPROF_SITES widens the ranking. The default is a readable summary,
 * but it hides the long tail -- at one point 80 sites named 89.91 of 109.94
 * live MB, leaving 20 MB spread over sites the report never mentioned. Only
 * the printing grows; the sort already covers the whole table. */
static unsigned
memprof_report_sites(void)
{
    char const* env = getenv("TORIRS_MEMPROF_SITES");
    long n;

    if( !env || env[0] == '\0' )
        return MEMPROF_REPORT_SITES;
    n = strtol(env, NULL, 10);
    if( n <= 0 )
        return MEMPROF_REPORT_SITES;
    return (unsigned)n;
}

struct MemProf_Hdr
{
    uint32_t magic;
    uint32_t size;
    uint32_t site;
    uint32_t reserved;
};

struct MemProf_Site
{
    void* ra;
    int64_t live_bytes;
    int64_t live_count;
    int64_t total_bytes;
    int64_t total_count;
};

/*
 * Peak, not exit, is the number a footprint investigation is after: this client
 * frees most of its heap on the way down, so an exit-only report ranks the
 * teardown order rather than the load. Every time live bytes climb past the
 * last dump by this much, the report is rewritten in place -- so the file left
 * behind describes the high-water mark. The step is what keeps it affordable:
 * a dump sorts the whole site table, and doing that per allocation is the
 * mistake this file already made once. Keep it small, though: the dump only
 * describes the high-water mark to within one step, and at 16 MB that slack
 * was enough for the last dump to land 4.9 MB below the true peak -- ranking
 * a moment that was not the peak, which is worse than not ranking one.
 */
#define MEMPROF_PEAK_STEP_BYTES (1024 * 1024)

static struct MemProf_Site* g_sites;
static volatile LONG g_ready;
static int64_t g_live_bytes;
static int64_t g_peak_live_bytes;
static int64_t g_peak_dump_mark;
static int64_t g_peak_dumps;
static int64_t g_site_collisions;
static int64_t g_foreign_frees;
/* A single allocation whose size does not fit the header's 32-bit field. The
 * tree has none, but the counter is cheaper than finding out the hard way. */
static int64_t g_oversize_blocks;

static void
memprof_report(void);

static void
memprof_dump(const char* suffix, const char* label);

static uintptr_t
memprof_link_bias(void);

static void
memprof_init(void)
{
    if( InterlockedCompareExchange(&g_ready, -1, 0) != 0 )
        return;

    g_sites = (struct MemProf_Site*)__real_calloc(MEMPROF_SITE_CAP, sizeof(*g_sites));
    if( !g_sites )
    {
        /* Deliberately not an assert: losing the profiler must not take the run
         * with it, and every hook stays on its pass-through path. */
        fprintf(stderr, "memprof: site table allocation failed; profiling disabled\n");
        return;
    }

    atexit(memprof_report);
    InterlockedExchange(&g_ready, 1);
}

/*
 * TEMPORARY DIAGNOSTIC. The memory cuts introduced a fault that only fires
 * under one environment block and never under a debugger, so the fault has to
 * report itself. stderr is unbuffered; the redirected stdout buffer is not,
 * which is why the flush comes first -- without it the log loses its last 4 KB
 * and says nothing about where the process died.
 *
 * The stack scan is a poor-man's backtrace: -O2 without a frame pointer breaks
 * an EBP walk, but every return address still sits somewhere in the frame, so
 * printing the stack words that land inside the image finds them.
 */
static LONG WINAPI
memprof_fault_filter(EXCEPTION_POINTERS* info)
{
    HMODULE base = GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt;
    uintptr_t bias = memprof_link_bias();
    uintptr_t lo = (uintptr_t)base;
    uintptr_t hi;
    uintptr_t sp;
    uintptr_t stack_end;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t* p;
    int printed = 0;

    fflush(NULL);
    if( !info || !base || dos->e_magic != IMAGE_DOS_SIGNATURE )
        return EXCEPTION_CONTINUE_SEARCH;
    nt = (const IMAGE_NT_HEADERS*)((const char*)base + dos->e_lfanew);
    hi = lo + nt->OptionalHeader.SizeOfImage;

    fprintf(
        stderr,
        "FAULT: code=0x%08lx at 0x%08lx (link 0x%08lx) base=0x%08lx\n",
        (unsigned long)info->ExceptionRecord->ExceptionCode,
        (unsigned long)(uintptr_t)info->ExceptionRecord->ExceptionAddress,
        (unsigned long)((uintptr_t)info->ExceptionRecord->ExceptionAddress + bias),
        (unsigned long)lo);
    if( info->ExceptionRecord->NumberParameters >= 2 )
        fprintf(
            stderr,
            "FAULT: access %lu at 0x%08lx\n",
            (unsigned long)info->ExceptionRecord->ExceptionInformation[0],
            (unsigned long)info->ExceptionRecord->ExceptionInformation[1]);

#if defined(_WIN64)
    sp = (uintptr_t)info->ContextRecord->Rsp;
#else
    sp = (uintptr_t)info->ContextRecord->Esp;
    fprintf(
        stderr,
        "FAULT: eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx\n"
        "FAULT: ecx=%08lx edx=%08lx esi=%08lx edi=%08lx image=%08lx..%08lx\n",
        (unsigned long)info->ContextRecord->Eip,
        (unsigned long)info->ContextRecord->Esp,
        (unsigned long)info->ContextRecord->Ebp,
        (unsigned long)info->ContextRecord->Eax,
        (unsigned long)info->ContextRecord->Ebx,
        (unsigned long)info->ContextRecord->Ecx,
        (unsigned long)info->ContextRecord->Edx,
        (unsigned long)info->ContextRecord->Esi,
        (unsigned long)info->ContextRecord->Edi,
        (unsigned long)lo,
        (unsigned long)hi);
    {
        int k;
        for( k = 0; k < 16; k++ )
            fprintf(
                stderr,
                "FAULT: raw[%02d] %08lx\n",
                k,
                (unsigned long)((uintptr_t*)sp)[k]);
    }
#endif
    if( !VirtualQuery((void*)sp, &mbi, sizeof(mbi)) )
        return EXCEPTION_CONTINUE_SEARCH;
    stack_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    for( p = (uintptr_t*)sp; (uintptr_t)p < stack_end && printed < 40; p++ )
    {
        uintptr_t v = *p;
        if( v <= lo || v >= hi )
            continue;
        fprintf(
            stderr,
            "FAULT: stack[%04u] 0x%08lx link 0x%08lx\n",
            (unsigned)(((uintptr_t)p - sp) / sizeof(uintptr_t)),
            (unsigned long)v,
            (unsigned long)(v + bias));
        printed++;
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
/* The linker rewrote every malloc in the program, this file included. */
static void __attribute__((constructor)) memprof_ctor(void)
{
    SetUnhandledExceptionFilter(memprof_fault_filter);
    memprof_init();
}

/*
 * Direct-mapped, so exactly one load. Slot 0 is the "no site" bucket, which is
 * where a collision's bytes would otherwise have to go; instead a colliding
 * address simply reuses the resident slot and is counted, because the
 * alternative -- probing -- is the thing that made this file unusable before.
 */
static uint32_t
memprof_site(void* ra)
{
    uint64_t h = (uint64_t)(uintptr_t)ra;
    uint32_t slot;

    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    slot = (uint32_t)h & (MEMPROF_SITE_CAP - 1u);
    if( slot == 0 )
        slot = 1;

    if( g_sites[slot].ra == NULL )
        g_sites[slot].ra = ra;
    else if( g_sites[slot].ra != ra )
        g_sites[slot].ra = ra, g_site_collisions++;

    return slot;
}

/*
 * Hand back the user pointer for `raw`, stamping the header. `raw` is the block
 * as the CRT returned it, already MEMPROF_HDR_BYTES larger than the request.
 */
static void*
memprof_stamp(void* raw, size_t size, void* ra)
{
    struct MemProf_Hdr* hdr;
    uint32_t site;

    if( !raw )
        return NULL;

    site = memprof_site(ra);
    hdr = (struct MemProf_Hdr*)raw;
    hdr->magic = MEMPROF_MAGIC;
    hdr->size = (uint32_t)size;
    hdr->site = site;
    hdr->reserved = 0;

#if SIZE_MAX > 0xFFFFFFFFu
    if( size > 0xFFFFFFFFu )
        g_oversize_blocks++;
#endif

    g_sites[site].live_bytes += (int64_t)size;
    g_sites[site].live_count++;
    g_sites[site].total_bytes += (int64_t)size;
    g_sites[site].total_count++;
    g_live_bytes += (int64_t)size;
    if( g_live_bytes > g_peak_live_bytes )
        g_peak_live_bytes = g_live_bytes;

    if( g_live_bytes >= g_peak_dump_mark )
    {
        g_peak_dump_mark = g_live_bytes + MEMPROF_PEAK_STEP_BYTES;
        g_peak_dumps++;
        memprof_dump(".peak", "peak");
    }

    return (char*)raw + MEMPROF_HDR_BYTES;
}

/*
 * Reverse of memprof_stamp. Returns the raw block and drops it from the
 * counters, or NULL if `ptr` carries no header of ours -- which is not an
 * error: the CRT allocates before the constructor runs, and those blocks come
 * back here to be freed.
 */
static void*
memprof_unstamp(void* ptr)
{
    struct MemProf_Hdr* hdr;

    if( !ptr )
        return NULL;

    hdr = (struct MemProf_Hdr*)((char*)ptr - MEMPROF_HDR_BYTES);
    if( hdr->magic != MEMPROF_MAGIC )
    {
        g_foreign_frees++;
        return NULL;
    }

    g_sites[hdr->site].live_bytes -= (int64_t)hdr->size;
    g_sites[hdr->site].live_count--;
    g_live_bytes -= (int64_t)hdr->size;

    /* So a double free reads as foreign rather than double-counting. */
    hdr->magic = 0;
    return hdr;
}

void*
__wrap_malloc(size_t size)
{
    if( g_ready != 1 )
        return __real_malloc(size);
    return memprof_stamp(
        __real_malloc(size + MEMPROF_HDR_BYTES), size, __builtin_return_address(0));
}

void*
__wrap_calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;

    if( g_ready != 1 )
        return __real_calloc(nmemb, size);
    /* One block, not nmemb+1 elements: the header is bytes, not an element, and
     * __real_calloc zeroes the user region for us on the way past. */
    return memprof_stamp(
        __real_calloc(1, total + MEMPROF_HDR_BYTES), total, __builtin_return_address(0));
}

void*
__wrap_realloc(void* ptr, size_t size)
{
    void* raw;

    if( g_ready != 1 )
        return __real_realloc(ptr, size);
    if( !ptr )
        return memprof_stamp(
            __real_malloc(size + MEMPROF_HDR_BYTES), size, __builtin_return_address(0));

    raw = memprof_unstamp(ptr);
    if( !raw )
    {
        /* Not ours: it has no header to grow, so it must not gain one either --
         * the caller may hand the result to a free() that is not ours. */
        return __real_realloc(ptr, size);
    }
    return memprof_stamp(
        __real_realloc(raw, size + MEMPROF_HDR_BYTES), size, __builtin_return_address(0));
}

void
__wrap_free(void* ptr)
{
    void* raw;

    if( g_ready != 1 )
    {
        __real_free(ptr);
        return;
    }
    if( !ptr )
        return;

    raw = memprof_unstamp(ptr);
    __real_free(raw ? raw : ptr);
}

char*
__wrap_strdup(const char* str)
{
    size_t n;
    void* raw;

    if( g_ready != 1 )
        return __real_strdup(str);
    if( !str )
        return __real_strdup(str);

    n = strlen(str) + 1u;
    raw = __real_malloc(n + MEMPROF_HDR_BYTES);
    if( !raw )
        return NULL;
    memcpy((char*)raw + MEMPROF_HDR_BYTES, str, n);
    return (char*)memprof_stamp(raw, n, __builtin_return_address(0));
}

static int
memprof_site_cmp(const void* a, const void* b)
{
    const struct MemProf_Site* sa = *(const struct MemProf_Site* const*)a;
    const struct MemProf_Site* sb = *(const struct MemProf_Site* const*)b;

    if( sa->live_bytes < sb->live_bytes )
        return 1;
    if( sa->live_bytes > sb->live_bytes )
        return -1;
    return 0;
}

/*
 * Runtime address -> the address the linker gave the same instruction, which is
 * what addr2line wants.
 *
 * This used to read OptionalHeader.ImageBase out of the mapped header, which is
 * exactly the field the loader overwrites with wherever it decided to put the
 * image: the answer was always zero, and on a relocated i686 build that made
 * every printed call site a runtime address no symbolizer could resolve. The
 * base the image was linked for only survives on disk, so read it from there.
 */
static uintptr_t
memprof_link_bias(void)
{
    static uintptr_t cached;
    static int resolved;
    char path[MAX_PATH];
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    HMODULE base;
    FILE* f;

    if( resolved )
        return cached;
    resolved = 1;

    base = GetModuleHandleA(NULL);
    if( !base || !GetModuleFileNameA(NULL, path, sizeof(path)) )
        return 0;
    f = fopen(path, "rb");
    if( !f )
        return 0;
    if( fread(&dos, sizeof(dos), 1, f) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        fseek(f, dos.e_lfanew, SEEK_SET) == 0 && fread(&nt, sizeof(nt), 1, f) == 1 &&
        nt.Signature == IMAGE_NT_SIGNATURE )
        cached = (uintptr_t)nt.OptionalHeader.ImageBase - (uintptr_t)base;
    fclose(f);
    return cached;
}

/*
 * `suffix` is appended to TORIRS_MEMPROF_OUT so the peak snapshot and the exit
 * snapshot land in different files; with no output path set, both go to stderr.
 * Rewritten from scratch each call, so the last peak dump is the one left.
 */
static void
memprof_dump(const char* suffix, const char* label)
{
    struct MemProf_Site** ranked;
    FILE* out = stderr;
    const char* path;
    char named[512];
    uintptr_t bias;
    uint32_t i;
    uint32_t ranked_count = 0;
    int64_t reported = 0;

    if( g_ready != 1 || !g_sites )
        return;
    /* This function allocates (fopen buffers, the ranking array); disarming
     * first keeps those out of the numbers being printed, and -- since the peak
     * trigger lives inside the allocation path -- is also what stops it
     * recursing into itself. */
    InterlockedExchange(&g_ready, 2);

    path = getenv("TORIRS_MEMPROF_OUT");
    if( path && path[0] )
    {
        snprintf(named, sizeof(named), "%s%s", path, suffix);
        FILE* f = fopen(named, "w");
        if( f )
            out = f;
    }

    ranked = (struct MemProf_Site**)__real_malloc(sizeof(*ranked) * MEMPROF_SITE_CAP);
    if( !ranked )
    {
        fprintf(out, "memprof: cannot rank sites (out of memory)\n");
        if( out != stderr )
            fclose(out);
        InterlockedExchange(&g_ready, 1);
        return;
    }

    for( i = 0; i < MEMPROF_SITE_CAP; i++ )
    {
        if( g_sites[i].live_bytes > 0 )
            ranked[ranked_count++] = &g_sites[i];
    }
    qsort(ranked, ranked_count, sizeof(*ranked), memprof_site_cmp);
    bias = memprof_link_bias();

    fprintf(out, "memprof: === live heap by call site (%s) ===\n", label);
    fprintf(
        out,
        "memprof: live %.2f MB in %u sites (peak %.2f MB, %lld peak dumps)\n",
        (double)g_live_bytes / (1024.0 * 1024.0),
        ranked_count,
        (double)g_peak_live_bytes / (1024.0 * 1024.0),
        (long long)g_peak_dumps);
    fprintf(
        out,
        "memprof: site-slot collisions %lld, foreign frees %lld, oversize %lld\n",
        (long long)g_site_collisions,
        (long long)g_foreign_frees,
        (long long)g_oversize_blocks);
    /* Ground truth for the bias: nm names memprof_dump's link address, and the
     * difference from this runtime address is the exact relocation -- checkable
     * even if the PE-header arithmetic above ever lies. */
    fprintf(
        out,
        "memprof: anchor memprof_dump=0x%llx bias=0x%llx (link addr = printed)\n",
        (unsigned long long)(uintptr_t)&memprof_dump,
        (unsigned long long)bias);

    for( i = 0; i < ranked_count && i < (uint32_t)memprof_report_sites(); i++ )
    {
        const struct MemProf_Site* site = ranked[i];

        reported += site->live_bytes;
        fprintf(
            out,
            "memprof: #%-2u %9.2f MB  %8lld blocks  (%.2f MB ever, %lld allocs)  0x%llx\n",
            i,
            (double)site->live_bytes / (1024.0 * 1024.0),
            (long long)site->live_count,
            (double)site->total_bytes / (1024.0 * 1024.0),
            (long long)site->total_count,
            (unsigned long long)((uintptr_t)site->ra + bias));
    }
    fprintf(
        out,
        "memprof: top %u sites account for %.2f MB of %.2f MB live\n",
        i,
        (double)reported / (1024.0 * 1024.0),
        (double)g_live_bytes / (1024.0 * 1024.0));
    fprintf(out, "memprof: === end ===\n");

    fflush(out);
    if( out != stderr )
        fclose(out);
    __real_free(ranked);
    InterlockedExchange(&g_ready, 1);
}

static void
memprof_report(void)
{
    memprof_dump(".exit", "exit");
    /* Nothing after this should be counted, and atexit handlers that free are
     * exactly what would otherwise scribble on the numbers just written. */
    InterlockedExchange(&g_ready, 2);
}

#endif /* TORIRS_MEMPROF */
