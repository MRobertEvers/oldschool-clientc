/* Real-engine painter benchmark: world3d vs bucket on seeded and cache scenes.
 *
 * Usage:
 *   ./bench_real [start] [count] [iters]          seeded scenes (default build)
 *   ./bench_real cache [dir] [iters]              dat2 world (FUZZ_WITH_CACHE build)
 */
#include "graphics/shared_tables.h"
#include "painter_bench_real.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(
    int argc,
    char** argv)
{
    init_sin_table();
    init_cos_table();

#ifdef FUZZ_WITH_CACHE
    if( argc >= 2 && strcmp(argv[1], "cache") == 0 )
    {
        const char* cache_dir = NULL;
        if( argc >= 3 && argv[2][0] != '\0' )
        {
            if( strcmp(argv[2], "kronos") == 0 )
                cache_dir = painter_bench_find_kronos_cache_dir();
            else
                cache_dir = argv[2];
        }
        else
        {
            cache_dir = painter_bench_find_cache_dir();
        }
        int iters = (argc >= 4) ? (int)strtol(argv[3], NULL, 10) : 200;
        if( !cache_dir )
        {
            fprintf(
                stderr,
                "Usage: bench_real cache [dir] [iters]\n"
                "Could not auto-detect cache directory.\n");
            return 1;
        }
        return painter_bench_cache(cache_dir, iters);
    }
#else
    if( argc >= 2 && strcmp(argv[1], "cache") == 0 )
    {
        fprintf(stderr, "cache bench requires bench_real_cache build (make bench_real_cache)\n");
        return 1;
    }
#endif

    uint32_t start = 1u;
    uint32_t count = 500u;
    int iters = 200;

    if( argc >= 2 )
        start = (uint32_t)strtoul(argv[1], NULL, 10);
    if( argc >= 3 )
        count = (uint32_t)strtoul(argv[2], NULL, 10);
    if( argc >= 4 )
        iters = (int)strtol(argv[3], NULL, 10);

    return painter_bench_seeded(start, count, iters);
}
