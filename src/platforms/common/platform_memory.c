#include "platform_memory.h"
#include <string.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten/heap.h>
#include <malloc.h>

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));
    info->heap_total = emscripten_get_heap_size();
    struct mallinfo mi = mallinfo();
    info->heap_used = (size_t)mi.uordblks;
    return true;
}

#elif defined(__APPLE__)

#include <malloc/malloc.h>

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));

    /*
     * Do not use mach_task_basic_info resident vs virtual here: virtual_size is
     * process address space (often huge due to mmap), so used/total looked empty.
     * malloc_zone_statistics (NULL aggregates all zones) matches Linux mallinfo
     * / Emscripten heap semantics for the ImGui bar.
     */
    malloc_statistics_t stats;
    malloc_zone_statistics(NULL, &stats);
    info->heap_used = (size_t)stats.size_in_use;
    info->heap_total = (size_t)stats.size_allocated;
    info->heap_peak = (size_t)stats.max_size_in_use;
    return true;
}

#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));

    PROCESS_MEMORY_COUNTERS pmc;
    if( !GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) )
        return false;

    info->heap_used = (size_t)pmc.WorkingSetSize;
    info->heap_total = (size_t)pmc.PagefileUsage;
    info->heap_peak = (size_t)pmc.PeakWorkingSetSize;
    return true;
}

#elif defined(__linux__)

#include <malloc.h>
#include <stdio.h>

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    struct mallinfo2 mi = mallinfo2();
    info->heap_used = mi.uordblks + mi.hblkhd;
    info->heap_total = mi.arena + mi.hblkhd;
#else
    struct mallinfo mi = mallinfo();
    info->heap_used = (size_t)(unsigned int)mi.uordblks + (size_t)(unsigned int)mi.hblkhd;
    info->heap_total = (size_t)(unsigned int)mi.arena + (size_t)(unsigned int)mi.hblkhd;
#endif

    FILE* f = fopen("/proc/self/status", "r");
    if( f )
    {
        char line[256];
        while( fgets(line, sizeof(line), f) )
        {
            unsigned long val = 0;
            if( sscanf(line, "VmPeak: %lu kB", &val) == 1 )
            {
                info->heap_peak = (size_t)val * 1024;
                break;
            }
        }
        fclose(f);
    }

    return true;
}

#else

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));
    return false;
}

#endif
