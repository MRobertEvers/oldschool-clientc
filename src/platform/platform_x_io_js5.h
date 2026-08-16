#ifndef SRC_PLATFORM_PLATFORM_X_IO_JS5_H
#define SRC_PLATFORM_PLATFORM_X_IO_JS5_H

#include "js5/js5.h"

#include <stdbool.h>
#include <stdint.h>

struct PlatformX_IO;

/*
 * Executor-only JS5 attachment for the native cache backend.
 *
 * Keeping this out of platform_x_io.h is intentional: App and TaskRunner keep
 * seeing the same IO interface whether the cache was complete at startup or is
 * being populated by the executor.
 */
int
PlatformXIO_Js5Enable(
    struct PlatformX_IO* px,
    const struct Js5Config* config);

/*
 * Do bounded non-blocking network/storage work and publish completed urgent
 * reads into their original ToriRS_IO slots.
 *
 * Returns -1 after a terminal JS5 failure, 0 while metadata is still being
 * primed, and 1 once all enabled reference tables are ready. Group downloads
 * continue in the background after the return value becomes 1.
 */
int
PlatformXIO_Js5Pump(
    struct PlatformX_IO* px,
    uint64_t now_ms);

bool
PlatformXIO_Js5MetadataReady(const struct PlatformX_IO* px);

enum Js5Error
PlatformXIO_Js5LastError(const struct PlatformX_IO* px);

void
PlatformXIO_Js5GetProgress(
    const struct PlatformX_IO* px,
    struct Js5Progress* progress);

#endif
