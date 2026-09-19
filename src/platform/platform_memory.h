#ifndef TORIRS_PLATFORM_MEMORY_H
#define TORIRS_PLATFORM_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

/* Current memory footprint of this client process. */
bool PlatformMemory_FootprintBytes(uint64_t* out_bytes);

#endif /* TORIRS_PLATFORM_MEMORY_H */
