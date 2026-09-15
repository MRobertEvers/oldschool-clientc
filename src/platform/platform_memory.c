#include "platform/platform_memory.h"

#include <stddef.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten/heap.h>

bool
PlatformMemory_FootprintBytes(uint64_t* out_bytes)
{
    if( !out_bytes )
        return false;
    /* A browser does not expose a process RSS. The live WebAssembly heap is
     * the client-owned memory visible from this lane. */
    *out_bytes = (uint64_t)emscripten_get_heap_size();
    return true;
}

#elif defined(_WIN32)

/* Version 1 keeps the XP lane on psapi.dll's GetProcessMemoryInfo instead of
 * the Vista-era K32 alias in kernel32.dll. */
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

bool
PlatformMemory_FootprintBytes(uint64_t* out_bytes)
{
    PROCESS_MEMORY_COUNTERS info;

    if( !out_bytes )
        return false;
    if( !GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)) )
    {
        *out_bytes = 0;
        return false;
    }
    *out_bytes = (uint64_t)info.WorkingSetSize;
    return true;
}

#elif defined(__APPLE__)

#include <mach/mach.h>

bool
PlatformMemory_FootprintBytes(uint64_t* out_bytes)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    if( !out_bytes )
        return false;
    if( task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            (task_info_t)&info,
            &count) != KERN_SUCCESS )
    {
        *out_bytes = 0;
        return false;
    }
    *out_bytes = (uint64_t)info.resident_size;
    return true;
}

#elif defined(__linux__)

#include <stdio.h>
#include <unistd.h>

bool
PlatformMemory_FootprintBytes(uint64_t* out_bytes)
{
    FILE* statm;
    unsigned long total_pages;
    unsigned long resident_pages;
    long page_size;

    if( !out_bytes )
        return false;
    statm = fopen("/proc/self/statm", "r");
    if( !statm )
    {
        *out_bytes = 0;
        return false;
    }
    if( fscanf(statm, "%lu %lu", &total_pages, &resident_pages) != 2 )
    {
        fclose(statm);
        *out_bytes = 0;
        return false;
    }
    fclose(statm);
    (void)total_pages;

    page_size = sysconf(_SC_PAGESIZE);
    if( page_size <= 0 )
    {
        *out_bytes = 0;
        return false;
    }
    *out_bytes = (uint64_t)resident_pages * (uint64_t)page_size;
    return true;
}

#else

bool
PlatformMemory_FootprintBytes(uint64_t* out_bytes)
{
    if( out_bytes )
        *out_bytes = 0;
    return false;
}

#endif
