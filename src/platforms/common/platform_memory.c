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

#include <mach/mach.h>
#include <sys/resource.h>

bool
platform_get_memory_info(struct PlatformMemoryInfo* info)
{
    memset(info, 0, sizeof(*info));

    mach_task_basic_info_data_t task_info_data;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t kr =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&task_info_data, &count);
    if( kr != KERN_SUCCESS )
        return false;

    info->heap_used = (size_t)task_info_data.resident_size;
    info->heap_total = (size_t)task_info_data.virtual_size;

    struct rusage ru;
    if( getrusage(RUSAGE_SELF, &ru) == 0 )
        info->heap_peak = (size_t)ru.ru_maxrss;

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
