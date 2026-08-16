/**
 * Cross-platform malloc instrumentation (opt-in via -DTORIRS_MEMTRACE=1).
 *
 * Output: binary trace file (default memtrace.bin, override TORIRS_MEMTRACE_OUT).
 * Decode with tools/memtrace/decode_memtrace.py and view in viewer.html.
 */

#if defined(TORIRS_MEMTRACE)

#include "torirs_memtrace.h"
#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#include <emscripten.h>
#include <malloc.h>

extern void* __real_malloc(size_t size);
extern void* __real_calloc(size_t nmemb, size_t size);
extern void* __real_realloc(void* ptr, size_t size);
extern void __real_free(void* ptr);
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#include <malloc/malloc.h>
#include <pthread.h>
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/time.h>
#endif

#if !defined(__EMSCRIPTEN__)
#include <execinfo.h>
#endif

/* ------------------------------------------------------------------------- */
/* Binary on-disk format (version 1)                                         */
/* ------------------------------------------------------------------------- */

#define TORIRS_MEMTRACE_MAGIC "TRMT"
#define TORIRS_MEMTRACE_VERSION 1u

#define TORIRS_MEMTRACE_MAX_STACK 32u
#define TORIRS_MEMTRACE_RING_CAP 4096u
#define TORIRS_MEMTRACE_DEFAULT_PATH "memtrace.bin"

#if defined(__EMSCRIPTEN__)
#define TORIRS_MEMTRACE_WASM_STACK_TABLE 4096u
#define TORIRS_MEMTRACE_WASM_STACK_MAX 512u
#endif

struct TorIRS_MemTrace_FileHeader
{
    char magic[4];
    uint32_t version;
    uint32_t platform;
    uint32_t mod_count;
    uint32_t wasm_stack_count;
} __attribute__((packed));

struct TorIRS_MemTrace_ModEntry
{
    uint64_t base;
    uint64_t size;
    uint32_t path_len;
} __attribute__((packed));

struct TorIRS_MemTrace_EventRecord
{
    uint8_t kind;
    uint8_t stack_count;
    uint16_t reserved;
    uint64_t timestamp_ns;
    uint64_t thread_id;
    uint64_t req_size;
    uint64_t usable_size;
    uint64_t ptr;
    uint64_t old_ptr;
    uint64_t live_bytes;
    uint64_t heap_total;
    uint64_t stack[TORIRS_MEMTRACE_MAX_STACK];
} __attribute__((packed));

/* ------------------------------------------------------------------------- */
/* Runtime state                                                             */
/* ------------------------------------------------------------------------- */

static int g_trace_fd = -1;
static char g_trace_path[512];
static pthread_mutex_t g_trace_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_live_bytes;
static _Atomic int g_initialized;
static _Atomic int g_closed;

static struct TorIRS_MemTrace_EventRecord g_ring[TORIRS_MEMTRACE_RING_CAP];
static uint32_t g_ring_count;

/* 324 bytes land on disk per event and a client boot allocates millions of
 * times, so an unbounded trace is measured in gigabytes. TORIRS_MEMTRACE_MAX
 * caps the number of *recorded* events; live-byte accounting keeps running
 * past the cap, so the totals stay right even once the stream stops. */
static uint64_t g_max_events;
static _Atomic uint64_t g_event_count;
static _Atomic uint64_t g_recorded_count;
static _Atomic uint64_t g_peak_bytes;

static __thread int tls_in_hook;

#if defined(__EMSCRIPTEN__)

struct TorIRS_MemTrace_WasmStackEntry
{
    uint32_t hash;
    uint16_t len;
    char text[TORIRS_MEMTRACE_WASM_STACK_MAX];
};

static struct TorIRS_MemTrace_WasmStackEntry
    g_wasm_stacks[TORIRS_MEMTRACE_WASM_STACK_TABLE];
static uint32_t g_wasm_stack_count;

#endif

/* Real allocator function pointers (macOS interpose / lazy init). */
typedef void* (*torirs_real_malloc_fn)(size_t);
typedef void* (*torirs_real_calloc_fn)(size_t, size_t);
typedef void* (*torirs_real_realloc_fn)(void*, size_t);
typedef void (*torirs_real_free_fn)(void*);
typedef void* (*torirs_real_reallocf_fn)(void*, size_t);
typedef int (*torirs_real_posix_memalign_fn)(void**, size_t, size_t);
typedef char* (*torirs_real_strdup_fn)(const char*);

static torirs_real_malloc_fn g_real_malloc;
static torirs_real_calloc_fn g_real_calloc;
static torirs_real_realloc_fn g_real_realloc;
static torirs_real_free_fn g_real_free;
static torirs_real_reallocf_fn g_real_reallocf;
static torirs_real_posix_memalign_fn g_real_posix_memalign;
static torirs_real_strdup_fn g_real_strdup;

static void
torirs_memtrace_resolve_real(void)
{
    if( g_real_malloc )
        return;

#if defined(__APPLE__)
    g_real_malloc = (torirs_real_malloc_fn)dlsym(RTLD_NEXT, "malloc");
    g_real_calloc = (torirs_real_calloc_fn)dlsym(RTLD_NEXT, "calloc");
    g_real_realloc = (torirs_real_realloc_fn)dlsym(RTLD_NEXT, "realloc");
    g_real_free = (torirs_real_free_fn)dlsym(RTLD_NEXT, "free");
    g_real_reallocf = (torirs_real_reallocf_fn)dlsym(RTLD_NEXT, "reallocf");
    g_real_posix_memalign =
        (torirs_real_posix_memalign_fn)dlsym(RTLD_NEXT, "posix_memalign");
    g_real_strdup = (torirs_real_strdup_fn)dlsym(RTLD_NEXT, "strdup");
#elif defined(__EMSCRIPTEN__)
    g_real_malloc = __real_malloc;
    g_real_calloc = __real_calloc;
    g_real_realloc = __real_realloc;
    g_real_free = __real_free;
#else
    g_real_malloc = (torirs_real_malloc_fn)dlsym(RTLD_DEFAULT, "__real_malloc");
    g_real_calloc = (torirs_real_calloc_fn)dlsym(RTLD_DEFAULT, "__real_calloc");
    g_real_realloc = (torirs_real_realloc_fn)dlsym(RTLD_DEFAULT, "__real_realloc");
    g_real_free = (torirs_real_free_fn)dlsym(RTLD_DEFAULT, "__real_free");
    g_real_reallocf =
        (torirs_real_reallocf_fn)dlsym(RTLD_DEFAULT, "__real_reallocf");
    g_real_posix_memalign = (torirs_real_posix_memalign_fn)dlsym(
        RTLD_DEFAULT, "__real_posix_memalign");
    g_real_strdup = (torirs_real_strdup_fn)dlsym(RTLD_DEFAULT, "__real_strdup");

    if( !g_real_malloc )
        g_real_malloc = (torirs_real_malloc_fn)dlsym(RTLD_NEXT, "malloc");
    if( !g_real_calloc )
        g_real_calloc = (torirs_real_calloc_fn)dlsym(RTLD_NEXT, "calloc");
    if( !g_real_realloc )
        g_real_realloc = (torirs_real_realloc_fn)dlsym(RTLD_NEXT, "realloc");
    if( !g_real_free )
        g_real_free = (torirs_real_free_fn)dlsym(RTLD_NEXT, "free");
    if( !g_real_reallocf )
        g_real_reallocf = (torirs_real_reallocf_fn)dlsym(RTLD_NEXT, "reallocf");
    if( !g_real_posix_memalign )
        g_real_posix_memalign =
            (torirs_real_posix_memalign_fn)dlsym(RTLD_NEXT, "posix_memalign");
    if( !g_real_strdup )
        g_real_strdup = (torirs_real_strdup_fn)dlsym(RTLD_NEXT, "strdup");
#endif
}

static uint64_t
torirs_memtrace_now_ns(void)
{
#if defined(__EMSCRIPTEN__)
    return (uint64_t)(emscripten_get_now() * 1000000.0);
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    if( tb.denom == 0 )
        mach_timebase_info(&tb);
    return mach_absolute_time() * (uint64_t)tb.numer / (uint64_t)tb.denom;
#else
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static uint64_t
torirs_memtrace_thread_id(void)
{
#if defined(__EMSCRIPTEN__)
    return 1ull;
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}

static uint64_t
torirs_memtrace_heap_total(void)
{
#if defined(__EMSCRIPTEN__)
    return (uint64_t)__builtin_wasm_memory_size(0) * 65536u;
#elif defined(__APPLE__)
    return 0ull;
#else
    void* brk = sbrk(0);
    return brk ? (uint64_t)(uintptr_t)brk : 0ull;
#endif
}

static size_t
torirs_memtrace_usable_size(void* ptr)
{
    assert(ptr);
#if defined(__APPLE__)
    return (size_t)malloc_size(ptr);
#elif defined(__EMSCRIPTEN__)
    return (size_t)malloc_usable_size(ptr);
#else
    return (size_t)malloc_usable_size(ptr);
#endif
}

static int
torirs_memtrace_write_all(int fd, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t off = 0;
    while( off < len )
    {
        ssize_t n = write(fd, p + off, len - off);
        if( n < 0 )
        {
            if( errno == EINTR )
                continue;
            return -1;
        }
        if( n == 0 )
            return -1;
        off += (size_t)n;
    }
    return 0;
}

static void
torirs_memtrace_write_records_locked(void)
{
    if( g_trace_fd < 0 || g_ring_count == 0 )
        return;
    (void)torirs_memtrace_write_all(
        g_trace_fd,
        g_ring,
        (size_t)g_ring_count * sizeof(g_ring[0]));
    g_ring_count = 0;
}

static void
torirs_memtrace_push_record(const struct TorIRS_MemTrace_EventRecord* rec)
{
    if( g_trace_fd < 0 || atomic_load(&g_closed) )
        return;

    pthread_mutex_lock(&g_trace_mutex);
    if( g_ring_count < TORIRS_MEMTRACE_RING_CAP )
        g_ring[g_ring_count++] = *rec;
    else
    {
        torirs_memtrace_write_records_locked();
        g_ring[g_ring_count++] = *rec;
    }
    if( g_ring_count >= TORIRS_MEMTRACE_RING_CAP / 2u )
        torirs_memtrace_write_records_locked();
    pthread_mutex_unlock(&g_trace_mutex);
}

#if defined(__EMSCRIPTEN__)

static uint32_t
torirs_memtrace_fnv1a(const char* s, size_t len)
{
    uint32_t h = 2166136261u;
    for( size_t i = 0; i < len; ++i )
    {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t
torirs_memtrace_wasm_stack_id(const char* stack_text)
{
    assert(stack_text);
    if( !stack_text[0] )
        return 0;

    const size_t len = strnlen(stack_text, TORIRS_MEMTRACE_WASM_STACK_MAX - 1u);
    const uint32_t hash = torirs_memtrace_fnv1a(stack_text, len);

    for( uint32_t i = 0; i < g_wasm_stack_count; ++i )
    {
        if( g_wasm_stacks[i].hash == hash
            && g_wasm_stacks[i].len == (uint16_t)len
            && memcmp(g_wasm_stacks[i].text, stack_text, len) == 0 )
            return i + 1u;
    }

    if( g_wasm_stack_count >= TORIRS_MEMTRACE_WASM_STACK_TABLE )
        return 0;

    const uint32_t id = g_wasm_stack_count;
    g_wasm_stacks[id].hash = hash;
    g_wasm_stacks[id].len = (uint16_t)len;
    memcpy(g_wasm_stacks[id].text, stack_text, len);
    g_wasm_stacks[id].text[len] = '\0';
    g_wasm_stack_count++;
    return id + 1u;
}

static void
torirs_memtrace_capture_stack(struct TorIRS_MemTrace_EventRecord* rec)
{
    char stack_buf[TORIRS_MEMTRACE_WASM_STACK_MAX];
    emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_JS_STACK, stack_buf, sizeof(stack_buf));
    const uint32_t stack_id = torirs_memtrace_wasm_stack_id(stack_buf);
    memset(rec->stack, 0, sizeof(rec->stack));
    rec->stack[0] = (uint64_t)stack_id;
    rec->stack_count = stack_id ? 1u : 0u;
}

#else

static void
torirs_memtrace_capture_stack(struct TorIRS_MemTrace_EventRecord* rec)
{
    void* frames[TORIRS_MEMTRACE_MAX_STACK];
    const int n = backtrace(frames, (int)TORIRS_MEMTRACE_MAX_STACK);
    memset(rec->stack, 0, sizeof(rec->stack));
    rec->stack_count = 0;
    if( n <= 0 )
        return;
    const int skip = 4; /* hook internals + allocator wrapper */
    for( int i = skip; i < n && rec->stack_count < TORIRS_MEMTRACE_MAX_STACK; ++i )
        rec->stack[rec->stack_count++] = (uint64_t)(uintptr_t)frames[i];
}

#endif

static void
torirs_memtrace_record_event(
    uint8_t kind,
    size_t req_size,
    void* ptr,
    void* old_ptr)
{
    if( tls_in_hook || !atomic_load(&g_initialized) || atomic_load(&g_closed) )
        return;
    const uint64_t seq = atomic_fetch_add(&g_event_count, 1ull);
    if( g_max_events && seq >= g_max_events )
        return;

    tls_in_hook = 1;
    atomic_fetch_add(&g_recorded_count, 1ull);

    struct TorIRS_MemTrace_EventRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.kind = kind;
    rec.timestamp_ns = torirs_memtrace_now_ns();
    rec.thread_id = torirs_memtrace_thread_id();
    rec.req_size = (uint64_t)req_size;
    rec.ptr = (uint64_t)(uintptr_t)ptr;
    rec.old_ptr = (uint64_t)(uintptr_t)old_ptr;
    rec.usable_size = (uint64_t)torirs_memtrace_usable_size(ptr);
    rec.live_bytes = atomic_load(&g_live_bytes);
    rec.heap_total = torirs_memtrace_heap_total();
    torirs_memtrace_capture_stack(&rec);
    torirs_memtrace_push_record(&rec);

    tls_in_hook = 0;
}

static void
torirs_memtrace_account_alloc(void* ptr, size_t req_size)
{
    assert(ptr);
    const size_t usable = torirs_memtrace_usable_size(ptr);
    (void)req_size;
    const uint64_t live =
        atomic_fetch_add(&g_live_bytes, (uint64_t)usable) + (uint64_t)usable;
    uint64_t peak = atomic_load(&g_peak_bytes);
    while( live > peak
           && !atomic_compare_exchange_weak(&g_peak_bytes, &peak, live) )
        ;
}

static void
torirs_memtrace_account_free(void* ptr)
{
    if( !ptr )
        return;
    const size_t usable = torirs_memtrace_usable_size(ptr);
    atomic_fetch_sub(&g_live_bytes, (uint64_t)usable);
}

/* ------------------------------------------------------------------------- */
/* Module map (written into file header at init)                             */
/* ------------------------------------------------------------------------- */

#if defined(__APPLE__)

struct TorIRS_MemTrace_ModScratch
{
    struct TorIRS_MemTrace_ModEntry* entries;
    char* path_blob;
    size_t path_blob_cap;
    size_t path_blob_len;
    uint32_t count;
    uint32_t cap;
};

static void
torirs_memtrace_mod_scratch_grow(struct TorIRS_MemTrace_ModScratch* scratch)
{
    if( scratch->count < scratch->cap )
        return;
    const uint32_t new_cap = scratch->cap ? scratch->cap * 2u : 32u;
    scratch->entries = (struct TorIRS_MemTrace_ModEntry*)realloc(
        scratch->entries, (size_t)new_cap * sizeof(*scratch->entries));
    scratch->cap = new_cap;
}

static void
torirs_memtrace_mod_add(
    struct TorIRS_MemTrace_ModScratch* scratch,
    uint64_t base,
    uint64_t size,
    const char* path)
{
    assert(scratch);
    assert(path);
    const size_t plen = strlen(path);
    if( scratch->path_blob_len + plen > scratch->path_blob_cap )
    {
        size_t new_cap = scratch->path_blob_cap ? scratch->path_blob_cap * 2u : 4096u;
        while( new_cap < scratch->path_blob_len + plen )
            new_cap *= 2u;
        scratch->path_blob = (char*)realloc(scratch->path_blob, new_cap);
        scratch->path_blob_cap = new_cap;
    }
    torirs_memtrace_mod_scratch_grow(scratch);
    struct TorIRS_MemTrace_ModEntry* e = &scratch->entries[scratch->count++];
    e->base = base;
    e->size = size;
    e->path_len = (uint32_t)plen;
    memcpy(scratch->path_blob + scratch->path_blob_len, path, plen);
    scratch->path_blob_len += plen;
}

static int
torirs_memtrace_collect_modmap(struct TorIRS_MemTrace_ModScratch* scratch)
{
    const uint32_t n = _dyld_image_count();
    for( uint32_t i = 0; i < n; ++i )
    {
        const struct mach_header* hdr = _dyld_get_image_header(i);
        const char* name = _dyld_get_image_name(i);
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        (void)slide;
        if( !hdr || !name )
            continue;
        const uint64_t base = (uint64_t)(uintptr_t)hdr;
        torirs_memtrace_mod_add(scratch, base, 0ull, name);
    }
    return 0;
}

#elif !defined(__EMSCRIPTEN__)

struct TorIRS_MemTrace_ModScratch
{
    struct TorIRS_MemTrace_ModEntry* entries;
    char* path_blob;
    size_t path_blob_cap;
    size_t path_blob_len;
    uint32_t count;
    uint32_t cap;
};

static void
torirs_memtrace_mod_scratch_grow(struct TorIRS_MemTrace_ModScratch* scratch)
{
    if( scratch->count < scratch->cap )
        return;
    const uint32_t new_cap = scratch->cap ? scratch->cap * 2u : 32u;
    scratch->entries = (struct TorIRS_MemTrace_ModEntry*)realloc(
        scratch->entries, (size_t)new_cap * sizeof(*scratch->entries));
    scratch->cap = new_cap;
}

static void
torirs_memtrace_mod_add(
    struct TorIRS_MemTrace_ModScratch* scratch,
    uint64_t base,
    uint64_t size,
    const char* path)
{
    assert(scratch);
    assert(path);
    const size_t plen = strlen(path);
    if( scratch->path_blob_len + plen > scratch->path_blob_cap )
    {
        size_t new_cap = scratch->path_blob_cap ? scratch->path_blob_cap * 2u : 4096u;
        while( new_cap < scratch->path_blob_len + plen )
            new_cap *= 2u;
        scratch->path_blob = (char*)realloc(scratch->path_blob, new_cap);
        scratch->path_blob_cap = new_cap;
    }
    torirs_memtrace_mod_scratch_grow(scratch);
    struct TorIRS_MemTrace_ModEntry* e = &scratch->entries[scratch->count++];
    e->base = base;
    e->size = size;
    e->path_len = (uint32_t)plen;
    memcpy(scratch->path_blob + scratch->path_blob_len, path, plen);
    scratch->path_blob_len += plen;
}

struct TorIRS_MemTrace_PhdrCtx
{
    struct TorIRS_MemTrace_ModScratch* scratch;
};

static int
torirs_memtrace_phdr_cb(
    struct dl_phdr_info* info,
    size_t size,
    void* data)
{
    (void)size;
    struct TorIRS_MemTrace_PhdrCtx* ctx = (struct TorIRS_MemTrace_PhdrCtx*)data;
    if( !ctx || !ctx->scratch )
        return 0;
    assert(info);

    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    for( int i = 0; i < info->dlpi_phnum; ++i )
    {
        if( info->dlpi_phdr[i].p_type != PT_LOAD )
            continue;
        const uint64_t start =
            (uint64_t)info->dlpi_addr + (uint64_t)info->dlpi_phdr[i].p_vaddr;
        const uint64_t end = start + (uint64_t)info->dlpi_phdr[i].p_memsz;
        if( start < min_vaddr )
            min_vaddr = start;
        if( end > max_vaddr )
            max_vaddr = end;
    }
    if( min_vaddr == UINT64_MAX )
        return 0;

    const char* path = info->dlpi_name && info->dlpi_name[0] ? info->dlpi_name : "[main]";
    torirs_memtrace_mod_add(
        ctx->scratch,
        min_vaddr,
        max_vaddr - min_vaddr,
        path);
    return 0;
}

static int
torirs_memtrace_collect_modmap(struct TorIRS_MemTrace_ModScratch* scratch)
{
    struct TorIRS_MemTrace_PhdrCtx ctx = { scratch };
    return dl_iterate_phdr(torirs_memtrace_phdr_cb, &ctx);
}

#endif

#define TORIRS_MEMTRACE_FOOTER_MAGIC "TRMS"

struct TorIRS_MemTrace_StackFooter
{
    char magic[4];
    uint32_t stack_count;
};

static int
torirs_memtrace_write_header_and_modmap(int fd)
{
    const int prev_hook = tls_in_hook;
    tls_in_hook = 1;

    struct TorIRS_MemTrace_FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, TORIRS_MEMTRACE_MAGIC, 4);
    hdr.version = TORIRS_MEMTRACE_VERSION;
#if defined(__EMSCRIPTEN__)
    hdr.platform = TORIRS_MEMTRACE_PLATFORM_WASM;
#elif defined(__APPLE__)
    hdr.platform = TORIRS_MEMTRACE_PLATFORM_MACOS;
#else
    hdr.platform = TORIRS_MEMTRACE_PLATFORM_LINUX;
#endif

#if defined(__EMSCRIPTEN__)
    hdr.mod_count = 0;
    hdr.wasm_stack_count = 0;
    if( torirs_memtrace_write_all(fd, &hdr, sizeof(hdr)) != 0 )
    {
        tls_in_hook = prev_hook;
        return -1;
    }
    tls_in_hook = prev_hook;
    return 0;
#else
    struct TorIRS_MemTrace_ModScratch scratch;
    memset(&scratch, 0, sizeof(scratch));
    if( torirs_memtrace_collect_modmap(&scratch) != 0 )
    {
        tls_in_hook = prev_hook;
        return -1;
    }

    hdr.mod_count = scratch.count;
    hdr.wasm_stack_count = 0;
    if( torirs_memtrace_write_all(fd, &hdr, sizeof(hdr)) != 0 )
        goto fail;

    size_t path_off = 0;
    for( uint32_t i = 0; i < scratch.count; ++i )
    {
        const struct TorIRS_MemTrace_ModEntry* e = &scratch.entries[i];
        if( torirs_memtrace_write_all(fd, e, sizeof(*e)) != 0 )
            goto fail;
        if( e->path_len > 0 )
        {
            if( torirs_memtrace_write_all(
                    fd, scratch.path_blob + path_off, e->path_len)
                != 0 )
                goto fail;
            path_off += e->path_len;
        }
    }

    free(scratch.entries);
    free(scratch.path_blob);
    tls_in_hook = prev_hook;
    return 0;

fail:
    free(scratch.entries);
    free(scratch.path_blob);
    tls_in_hook = prev_hook;
    return -1;
#endif
}

static void
torirs_memtrace_write_wasm_footer(int fd)
{
#if defined(__EMSCRIPTEN__)
    struct TorIRS_MemTrace_StackFooter footer;
    memcpy(footer.magic, TORIRS_MEMTRACE_FOOTER_MAGIC, 4);
    footer.stack_count = g_wasm_stack_count;
    if( torirs_memtrace_write_all(fd, &footer, sizeof(footer)) != 0 )
        return;
    for( uint32_t i = 0; i < g_wasm_stack_count; ++i )
    {
        const uint32_t len = (uint32_t)g_wasm_stacks[i].len;
        if( torirs_memtrace_write_all(fd, &len, sizeof(len)) != 0 )
            return;
        if( len > 0
            && torirs_memtrace_write_all(fd, g_wasm_stacks[i].text, len) != 0 )
            return;
    }
    const off_t count_off = (off_t)offsetof(struct TorIRS_MemTrace_FileHeader, wasm_stack_count);
    if( lseek(fd, count_off, SEEK_SET) >= 0 )
    {
        const uint32_t count = g_wasm_stack_count;
        (void)write(fd, &count, sizeof(count));
    }
#else
    (void)fd;
#endif
}

static void
torirs_memtrace_atexit(void);

static void
torirs_memtrace_on_signal(int sig)
{
    (void)sig;
    torirs_memtrace_flush();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void
torirs_memtrace_install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = torirs_memtrace_on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

static void
torirs_memtrace_init_once(void)
{
    if( atomic_exchange(&g_initialized, 1) )
        return;

    torirs_memtrace_resolve_real();

    const char* max_events = getenv("TORIRS_MEMTRACE_MAX");
    if( max_events && max_events[0] )
        g_max_events = strtoull(max_events, NULL, 0);

    const char* path = getenv("TORIRS_MEMTRACE_OUT");
    if( !path || !path[0] )
        path = TORIRS_MEMTRACE_DEFAULT_PATH;

    strncpy(g_trace_path, path, sizeof(g_trace_path) - 1u);
    g_trace_path[sizeof(g_trace_path) - 1u] = '\0';

#if defined(__EMSCRIPTEN__)
    g_trace_fd = open(g_trace_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
#else
    g_trace_fd = open(g_trace_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
#endif
    if( g_trace_fd < 0 )
        return;

    if( torirs_memtrace_write_header_and_modmap(g_trace_fd) != 0 )
    {
        close(g_trace_fd);
        g_trace_fd = -1;
        return;
    }

    atexit(torirs_memtrace_atexit);
    torirs_memtrace_install_signals();
}

void
torirs_memtrace_flush(void)
{
    if( g_trace_fd < 0 )
        return;
    if( atomic_exchange(&g_closed, 1) )
        return;

    pthread_mutex_lock(&g_trace_mutex);
    torirs_memtrace_write_records_locked();
    torirs_memtrace_write_wasm_footer(g_trace_fd);
    pthread_mutex_unlock(&g_trace_mutex);

    close(g_trace_fd);
    g_trace_fd = -1;

    /* The totals stay correct past TORIRS_MEMTRACE_MAX (only the recording
     * stops), so this line answers "how big is the heap, and did it come
     * back down" without decoding the trace at all. */
    const int prev_hook = tls_in_hook;
    tls_in_hook = 1;
    fprintf(
        stderr,
        "memtrace: %llu events (%llu recorded), peak live %.1f MB, "
        "still live at exit %.1f MB -> %s\n",
        (unsigned long long)atomic_load(&g_event_count),
        (unsigned long long)atomic_load(&g_recorded_count),
        (double)atomic_load(&g_peak_bytes) / (1024.0 * 1024.0),
        (double)atomic_load(&g_live_bytes) / (1024.0 * 1024.0),
        g_trace_path);
    tls_in_hook = prev_hook;
}

static void
torirs_memtrace_atexit(void)
{
    torirs_memtrace_flush();
}

void
torirs_memtrace_export_path(char* out_path, size_t out_cap)
{
    if( out_cap == 0 )
        return;
    assert(out_path);
    strncpy(out_path, g_trace_path, out_cap - 1u);
    out_path[out_cap - 1u] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Allocator hooks                                                           */
/* ------------------------------------------------------------------------- */

static void*
torirs_hook_malloc(size_t size)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    void* p = g_real_malloc ? g_real_malloc(size) : NULL;
    if( !tls_in_hook && p )
    {
        torirs_memtrace_account_alloc(p, size);
        torirs_memtrace_record_event(TORIRS_MEMTRACE_MALLOC, size, p, NULL);
    }
    return p;
}

static void*
torirs_hook_calloc(size_t nmemb, size_t size)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    void* p = g_real_calloc ? g_real_calloc(nmemb, size) : NULL;
    if( !tls_in_hook && p )
    {
        torirs_memtrace_account_alloc(p, nmemb * size);
        torirs_memtrace_record_event(TORIRS_MEMTRACE_CALLOC, nmemb * size, p, NULL);
    }
    return p;
}

static void*
torirs_hook_realloc(void* ptr, size_t size)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    void* old = ptr;
    if( !tls_in_hook && old )
        torirs_memtrace_account_free(old);
    void* p = g_real_realloc ? g_real_realloc(ptr, size) : NULL;
    if( !tls_in_hook )
    {
        if( p )
            torirs_memtrace_account_alloc(p, size);
        torirs_memtrace_record_event(TORIRS_MEMTRACE_REALLOC, size, p, old);
    }
    return p;
}

static void
torirs_hook_free(void* ptr)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    if( !tls_in_hook && ptr )
    {
        torirs_memtrace_record_event(TORIRS_MEMTRACE_FREE, 0, ptr, NULL);
        torirs_memtrace_account_free(ptr);
    }
    if( g_real_free )
        g_real_free(ptr);
}

static void*
torirs_hook_reallocf(void* ptr, size_t size)
{
    void* p = torirs_hook_realloc(ptr, size);
    if( !p && ptr )
        torirs_hook_free(ptr);
    return p;
}

static int
torirs_hook_posix_memalign(void** memptr, size_t alignment, size_t size)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    if( !g_real_posix_memalign )
        return ENOSYS;
    const int rc = g_real_posix_memalign(memptr, alignment, size);
    if( rc == 0 && memptr && *memptr && !tls_in_hook )
    {
        torirs_memtrace_account_alloc(*memptr, size);
        torirs_memtrace_record_event(TORIRS_MEMTRACE_POSIX_MEMALIGN, size, *memptr, NULL);
    }
    return rc;
}

static char*
torirs_hook_strdup(const char* s)
{
    torirs_memtrace_init_once();
    torirs_memtrace_resolve_real();
    char* p = g_real_strdup ? g_real_strdup(s) : NULL;
    if( !tls_in_hook && p )
    {
        const size_t n = s ? strlen(s) + 1u : 0u;
        torirs_memtrace_account_alloc(p, n);
        torirs_memtrace_record_event(TORIRS_MEMTRACE_STRDUP, n, p, NULL);
    }
    return p;
}

#if defined(__APPLE__)

extern void* malloc(size_t);
extern void* calloc(size_t, size_t);
extern void* realloc(void*, size_t);
extern void free(void*);
extern void* reallocf(void*, size_t);
extern int posix_memalign(void**, size_t, size_t);
extern char* strdup(const char*);

void*
malloc(size_t size)
{
    return torirs_hook_malloc(size);
}

void*
calloc(size_t nmemb, size_t size)
{
    return torirs_hook_calloc(nmemb, size);
}

void*
realloc(void* ptr, size_t size)
{
    return torirs_hook_realloc(ptr, size);
}

void
free(void* ptr)
{
    torirs_hook_free(ptr);
}

void*
reallocf(void* ptr, size_t size)
{
    return torirs_hook_reallocf(ptr, size);
}

int
posix_memalign(void** memptr, size_t alignment, size_t size)
{
    return torirs_hook_posix_memalign(memptr, alignment, size);
}

char*
strdup(const char* s)
{
    return torirs_hook_strdup(s);
}

#else /* Linux / WASM: linker --wrap */

void*
__wrap_malloc(size_t size)
{
    return torirs_hook_malloc(size);
}

void*
__wrap_calloc(size_t nmemb, size_t size)
{
    return torirs_hook_calloc(nmemb, size);
}

void*
__wrap_realloc(void* ptr, size_t size)
{
    return torirs_hook_realloc(ptr, size);
}

void
__wrap_free(void* ptr)
{
    torirs_hook_free(ptr);
}

void*
__wrap_reallocf(void* ptr, size_t size)
{
    return torirs_hook_reallocf(ptr, size);
}

int
__wrap_posix_memalign(void** memptr, size_t alignment, size_t size)
{
    return torirs_hook_posix_memalign(memptr, alignment, size);
}

char*
__wrap_strdup(const char* s)
{
    return torirs_hook_strdup(s);
}

#endif

#if defined(__EMSCRIPTEN__)
/* The page has no exit to flush on: it calls these to close the trace and find
 * it in MEMFS. Flushing ends recording for the rest of the session. */
EMSCRIPTEN_KEEPALIVE
void
torirs_memtrace_web_flush(void)
{
    torirs_memtrace_flush();
}

EMSCRIPTEN_KEEPALIVE
const char*
torirs_memtrace_web_path(void)
{
    return g_trace_path;
}
#endif

/* Init on first hooked allocation; avoid a global constructor on Emscripten. */
#if !defined(__EMSCRIPTEN__)
__attribute__((constructor)) static void
torirs_memtrace_ctor(void)
{
    torirs_memtrace_init_once();
}
#endif

#endif /* TORIRS_MEMTRACE */
