#include "world/entity_registry.h"

#include <stdio.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

static int
test_register_and_find(void)
{
    struct EntityRegistry reg;
    entity_registry_init(&reg, 2);

    int const id = RS_ENTITY_ID(RS_ENTITY_KIND_NPC, 7);
    TEST_ASSERT(entity_registry_register(&reg, id, 100, 3), "register new");
    struct EntityRecord* rec = entity_registry_find(&reg, id);
    TEST_ASSERT(rec != NULL && rec->element_id == 100 && rec->world_index == 3, "find new");

    TEST_ASSERT(entity_registry_register(&reg, id, 200, 4), "update existing");
    TEST_ASSERT(rec->element_id == 200 && rec->world_index == 4, "updated fields");
    entity_registry_free(&reg);
    return 0;
}

static int
test_growth(void)
{
    struct EntityRegistry reg;
    entity_registry_init(&reg, 2);

    for( int i = 0; i < 5; i++ )
    {
        int const id = RS_ENTITY_ID(RS_ENTITY_KIND_PLAYER, i);
        TEST_ASSERT(entity_registry_register(&reg, id, i, i), "register grows");
    }
    TEST_ASSERT(reg.count == 5 && reg.cap >= 5, "capacity grew");
    entity_registry_free(&reg);
    return 0;
}

static int
test_kind_macros(void)
{
    int const id = RS_ENTITY_ID(RS_ENTITY_KIND_PROJECTILE, 42);
    TEST_ASSERT(RS_ENTITY_KIND_OF(id) == RS_ENTITY_KIND_PROJECTILE, "kind decode");
    TEST_ASSERT(RS_ENTITY_INDEX_OF(id) == 42, "index decode");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_register_and_find();
    failures += test_growth();
    failures += test_kind_macros();

    if( failures == 0 )
    {
        printf("All entity_registry tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
