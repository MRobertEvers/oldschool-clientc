#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>

int g_failures = 0;

int
main(void)
{
    if( getenv("WB_BENCH") )
    {
        printf("== world_builder rebuild bench ==\n");
        test_world_builder_bench();
        return 0;
    }

    printf("== world_builder unit tests ==\n");
    test_painters_smoke();
    test_painters_tile_order();
    test_minimap_push_down();
    test_minimap_push_down_colourless_deck();
    test_builder_lifecycle();
    test_prerotate_placement();

    printf("== world_builder cache render test ==\n");
    test_world_builder_cache_render();

    if( g_failures == 0 )
        printf("ALL TESTS PASSED\n");
    else
        printf("%d TEST(S) FAILED\n", g_failures);

    return g_failures == 0 ? 0 : 1;
}
