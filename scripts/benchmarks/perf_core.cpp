#include "../../src/graphics/gouraud_simd.u.c"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <windows.h>

// Get core type using GetLogicalProcessorInformationEx
std::string
GetCoreType()
{
    // Get current processor index
    DWORD currentCpuIndex = GetCurrentProcessorNumber();

    // First, get the required buffer size
    DWORD bufferSize = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize);

    if( bufferSize == 0 )
    {
        return "Failed to get processor info - CPU " + std::to_string(currentCpuIndex);
    }

    // Allocate buffer and get processor information
    std::vector<BYTE> buffer(bufferSize);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* procInfo =
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

    if( !GetLogicalProcessorInformationEx(RelationProcessorCore, procInfo, &bufferSize) )
    {
        return "Failed to retrieve processor information - CPU " + std::to_string(currentCpuIndex);
    }

    // Walk through the processor information
    BYTE* ptr = buffer.data();
    BYTE* end = ptr + bufferSize;

    while( ptr < end )
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info =
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);

        if( info->Relationship == RelationProcessorCore )
        {
            // Check if current CPU is in this core's group
            for( int i = 0; i < info->Processor.GroupCount; i++ )
            {
                GROUP_AFFINITY groupAffinity = info->Processor.GroupMask[i];

                // Check if our current CPU index matches any of the logical processors in this core
                for( int bit = 0; bit < 64; bit++ )
                {
                    if( groupAffinity.Mask & (1ULL << bit) )
                    {
                        DWORD logicalProcessorIndex = groupAffinity.Group * 64 + bit;

                        if( logicalProcessorIndex == currentCpuIndex )
                        {
                            // Found our core! Check efficiency class
                            WORD efficiencyClass = info->Processor.EfficiencyClass;

                            if( efficiencyClass == 0 )
                            {
                                return "E-core (Efficiency) - CPU " +
                                       std::to_string(currentCpuIndex);
                            }
                            else
                            {
                                return "P-core (Performance) - CPU " +
                                       std::to_string(currentCpuIndex) +
                                       " (EfficiencyClass: " + std::to_string(efficiencyClass) +
                                       ")";
                            }
                        }
                    }
                }
            }
        }

        ptr += info->Size;
    }

    return "Unknown core type - CPU " + std::to_string(currentCpuIndex);
}

static int pixel_buffer[1024 * 1024];

static inline void
raster_linear_alpha_s4_baseline(uint32_t* pixel_buffer, int offset, int rgb_color, int alpha)
{
    for( int i = 0; i < 4; i++ )
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    }
}

int
main()
{
    std::cout << "Thread running on: " << GetCoreType() << "\n";

    // Optional: pin thread to a CPU and re-check
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << 6); // e.g. CPU #4
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "After pinning: " << GetCoreType() << "\n";

    int random_value = rand() % 256;
    int random_r = rand() % 256;
    int random_g = rand() % 256;
    int random_b = rand() % 256;
    int random_rgb = (random_r << 16) | (random_g << 8) | random_b;
    std::cout << "Random value between 0 and 255: " << random_value << std::endl;

    // Clear pixel buffer to ensure consistent starting state
    memset(pixel_buffer, 0, sizeof(pixel_buffer));

    // Benchmark SIMD version - process contiguous memory blocks
    std::cout << "\nBenchmarking SIMD version (contiguous memory)...\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    const int iterations = 10;
    const int pixels_per_call = 4;
    const int total_pixels = iterations * pixels_per_call;

    for( int i = 0; i < iterations; i++ )
    {
        raster_linear_alpha_s4(
            (uint32_t*)pixel_buffer, i * pixels_per_call, random_rgb, random_value);

        random_rgb += 1;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto simd_duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    std::cout << "SIMD version - Total time: " << simd_duration.count() << " nanoseconds\n";
    std::cout << "SIMD version - Time per pixel: " << (double)simd_duration.count() / total_pixels
              << " nanoseconds\n";

    // Clear pixel buffer again for fair comparison
    memset(pixel_buffer, 0, sizeof(pixel_buffer));

    // Benchmark baseline version - same conditions
    std::cout << "\nBenchmarking baseline version (same conditions)...\n";

    start_time = std::chrono::high_resolution_clock::now();

    for( int i = 0; i < iterations; i++ )
    {
        raster_linear_alpha_s4_baseline(
            (uint32_t*)pixel_buffer, i * pixels_per_call, random_rgb, random_value);
        random_rgb += 1;
    }

    end_time = std::chrono::high_resolution_clock::now();
    auto baseline_duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    std::cout << "Baseline version - Total time: " << baseline_duration.count() << " nanoseconds\n";
    std::cout << "Baseline version - Time per pixel: "
              << (double)baseline_duration.count() / total_pixels << " nanoseconds\n";

    // Calculate speedup
    double speedup = (double)baseline_duration.count() / (double)simd_duration.count();
    std::cout << "\nSpeedup: " << speedup << "x faster\n";

    // Additional test: Large contiguous block to minimize function call overhead
    std::cout << "\nTesting large contiguous block performance...\n";

    memset(pixel_buffer, 0, sizeof(pixel_buffer));

    start_time = std::chrono::high_resolution_clock::now();

    // Process a large contiguous block
    for( int i = 0; i < 1024 * 1024 / 4; i++ )
    {
        raster_linear_alpha_s4((uint32_t*)pixel_buffer, i * 4, random_rgb, random_value);
        random_rgb += 1;
    }

    end_time = std::chrono::high_resolution_clock::now();
    auto large_simd_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    memset(pixel_buffer, 0, sizeof(pixel_buffer));

    start_time = std::chrono::high_resolution_clock::now();

    for( int i = 0; i < 1024 * 1024 / 4; i++ )
    {
        raster_linear_alpha_s4_baseline((uint32_t*)pixel_buffer, i * 4, random_rgb, random_value);
        random_rgb += 1;
    }

    end_time = std::chrono::high_resolution_clock::now();
    auto large_baseline_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    std::cout << "Large block SIMD: " << large_simd_duration.count() << " microseconds\n";
    std::cout << "Large block Baseline: " << large_baseline_duration.count() << " microseconds\n";
    std::cout << "Large block speedup: "
              << (double)large_baseline_duration.count() / (double)large_simd_duration.count()
              << "x\n";

    return 0;
}

// mrobe@MatthewLenovo MINGW64 ~/benchmarks
// $ g++ -g -O3 -fno-inline -mavx2 perf_core.cpp

// mrobe@MatthewLenovo MINGW64 ~/benchmarks
// $ ./a.exe
// Thread running on: P-core (Performance) - CPU 2 (EfficiencyClass: 1)
// After pinning: E-core (Efficiency) - CPU 5
// Random value between 0 and 255: 41

// Benchmarking SIMD version (contiguous memory)...
// SIMD version - Total time: 2800 nanoseconds
// SIMD version - Time per pixel: 0.7 nanoseconds

// Benchmarking baseline version (same conditions)...
// Baseline version - Total time: 7400 nanoseconds
// Baseline version - Time per pixel: 1.85 nanoseconds

// Speedup: 2.64286x faster

// Testing large contiguous block performance...
// Large block SIMD: 581 microseconds
// Large block Baseline: 1887 microseconds
// Large block speedup: 3.24785x