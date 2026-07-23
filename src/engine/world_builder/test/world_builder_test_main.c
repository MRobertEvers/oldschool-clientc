#include "test_harness.h"

#include <stdio.h>

int g_failures = 0;

int
main(void)
{
    printf("== world_builder unit tests ==\n");
    test_painters_smoke();
    test_painters_tile_order();
    test_minimap_push_down();
    test_builder_lifecycle();

    printf("== world_builder cache render test ==\n");
    test_world_builder_cache_render();

    if( g_failures == 0 )
        printf("ALL TESTS PASSED\n");
    else
        printf("%d TEST(S) FAILED\n", g_failures);

    return g_failures == 0 ? 0 : 1;
}
