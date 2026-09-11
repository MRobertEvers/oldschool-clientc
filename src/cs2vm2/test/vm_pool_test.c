#include "cs2vm2/cs2vm2.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition, label)                         \
    do                                                  \
    {                                                   \
        if( condition )                                 \
            printf("  ok: %s\n", label);               \
        else                                            \
        {                                               \
            fprintf(stderr, "  FAIL: %s\n", label);    \
            failures++;                                 \
        }                                               \
    } while( 0 )

static void
test_regular_block_reuse(void)
{
    CS2VM2_PoolDrain();

    struct CS2VM2* first = CS2VM2_Acquire();
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(first);
    char* first_cell = CS2VM2_StrDup(thread, "first invocation");
    CHECK(thread->str_pool.block_count == 1, "first string allocates one regular block");
    CHECK(thread->str_pool.bytes_reserved == CS2VM2_STRPOOL_BLOCK_BYTES,
          "regular block has bounded capacity");
    CS2VM2_Release(first);

    struct CS2VM2* second = CS2VM2_Acquire();
    thread = CS2VM2_ThreadMain(second);
    CHECK(second == first, "VM allocation is recycled");
    CHECK(thread->str_pool.block_count == 1 &&
              thread->str_pool.bytes_reserved == CS2VM2_STRPOOL_BLOCK_BYTES &&
              thread->str_pool.bytes_used == 0 && thread->str_pool.alloc_count == 0 &&
              thread->str_pool.bytes_peak == 0 && thread->str_pool.reset_count == 0,
          "warm VM retains one empty regular string block");
    CHECK(CS2VM2_StrDup(thread, "second invocation") == first_cell,
          "warm invocation reuses the retained block from offset zero");
    CS2VM2_Release(second);
    CS2VM2_PoolDrain();
}

static void
test_oversize_not_retained(void)
{
    CS2VM2_PoolDrain();

    struct CS2VM2* first = CS2VM2_Acquire();
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(first);
    (void)CS2VM2_StrAlloc(thread, CS2VM2_STRPOOL_BLOCK_BYTES + 1u);
    CHECK(thread->str_pool.block_count == 1, "oversize string owns one exact-fit block");
    CS2VM2_Release(first);

    struct CS2VM2* second = CS2VM2_Acquire();
    thread = CS2VM2_ThreadMain(second);
    CHECK(second == first, "oversize VM allocation is recycled");
    CHECK(thread->str_pool.block_count == 0 && thread->str_pool.bytes_reserved == 0,
          "oversize string block is not retained");
    CS2VM2_Release(second);
    CS2VM2_PoolDrain();
}

static void
test_pool_cap_and_true_free(void)
{
    enum
    {
        POOL_CAP = 16,
        COUNT = POOL_CAP + 1,
    };
    struct CS2VM2* blocks[COUNT];

    CS2VM2_PoolDrain();
    for( int i = 0; i < COUNT; i++ )
    {
        blocks[i] = CS2VM2_Acquire();
        (void)CS2VM2_StrDup(CS2VM2_ThreadMain(blocks[i]), "pooled");
    }
    for( int i = 0; i < COUNT; i++ )
        CS2VM2_Release(blocks[i]);

    int retained = 0;
    for( int i = 0; i < COUNT; i++ )
    {
        blocks[i] = CS2VM2_Acquire();
        struct CS2VM2_StrPool const* pool = &CS2VM2_ThreadMain(blocks[i])->str_pool;
        retained += pool->block_count == 1 &&
                    pool->bytes_reserved == CS2VM2_STRPOOL_BLOCK_BYTES;
    }
    CHECK(retained == POOL_CAP, "only the bounded VM pool retains string blocks");
    for( int i = 0; i < COUNT; i++ )
        CS2VM2_Release(blocks[i]);
    CS2VM2_PoolDrain();

    struct CS2VM2* vm = CS2VM2_Acquire();
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(vm);
    (void)CS2VM2_StrDup(thread, "true free");
    CS2VM2_Free(vm);
    CHECK(thread->str_pool.block_count == 0 && thread->str_pool.bytes_reserved == 0,
          "CS2VM2_Free releases the retained regular block");
    free(vm);
}

int
main(void)
{
    printf("TEST: bounded warm VM string-pool lifecycle\n");
    test_regular_block_reuse();
    test_oversize_not_retained();
    test_pool_cap_and_true_free();

    if( failures )
    {
        fprintf(stderr, "VM pool lifecycle: %d failure(s)\n", failures);
        return 1;
    }
    printf("VM pool lifecycle: regular block reused; oversize/overflow/drain freed\n");
    return 0;
}
