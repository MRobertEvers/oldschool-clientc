#ifndef TORIRS_MEMTRACE_H
#define TORIRS_MEMTRACE_H

#include <stddef.h>
#include <stdint.h>

#if defined(TORIRS_MEMTRACE)

enum TorIRS_MemTrace_Kind
{
    TORIRS_MEMTRACE_MALLOC = 1,
    TORIRS_MEMTRACE_CALLOC = 2,
    TORIRS_MEMTRACE_REALLOC = 3,
    TORIRS_MEMTRACE_FREE = 4,
    TORIRS_MEMTRACE_POSIX_MEMALIGN = 5,
    TORIRS_MEMTRACE_STRDUP = 6,
};

enum TorIRS_MemTrace_Platform
{
    TORIRS_MEMTRACE_PLATFORM_LINUX = 0,
    TORIRS_MEMTRACE_PLATFORM_MACOS = 1,
    TORIRS_MEMTRACE_PLATFORM_WASM = 2,
};

/* Flush buffered events and close the trace file (safe to call multiple times). */
void
torirs_memtrace_flush(void);

/* Emscripten: export for JS download helper. */
void
torirs_memtrace_export_path(char* out_path, size_t out_cap);

#endif /* TORIRS_MEMTRACE */

#endif /* TORIRS_MEMTRACE_H */
