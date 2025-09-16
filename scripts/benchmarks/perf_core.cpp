#include <chrono>
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

int
main()
{
    std::cout << "Thread running on: " << GetCoreType() << "\n";

    // Optional: pin thread to a CPU and re-check
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << 5); // e.g. CPU #4
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "After pinning: " << GetCoreType() << "\n";

    return 0;
}