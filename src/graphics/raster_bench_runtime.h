#ifndef RASTER_BENCH_RUNTIME_H
#define RASTER_BENCH_RUNTIME_H

#include <stdint.h>

struct DashRasterBenchRuntime
{
    uint32_t packed;
    int active;
};

extern struct DashRasterBenchRuntime g_raster_bench;

#endif /* RASTER_BENCH_RUNTIME_H */
