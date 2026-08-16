#include "test_harness.h"

int g_failures;

int
main(void)
{
    test_buffers();
    test_items_build();
    test_load();
    test_parse();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All revconfig tests passed.\n");
    return 0;
}
