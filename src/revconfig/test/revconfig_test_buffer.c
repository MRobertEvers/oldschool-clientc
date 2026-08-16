#include "test_harness.h"

#include <string.h>

void
test_buffers(void)
{
    printf("TEST: buffers\n");

    revconfig_buffer_free(NULL);
    revconfig_item_buffer_free(NULL);

    struct RevConfigBuffer* fields = revconfig_buffer_new(0);
    TEST_ASSERT(fields != NULL, "buffer_new");
    TEST_ASSERT(fields->field_count == 0, "empty field_count");
    TEST_ASSERT(fields->fields == NULL, "null fields until push");

    TEST_ASSERT(revconfig_buffer_push_field(fields, RCFIELD_ITEMTYPE, "  sprite  ") == 0, "push");
    TEST_ASSERT(fields->field_count == 1, "field_count after push");
    TEST_ASSERT(strcmp(fields->fields[0].value, "sprite") == 0, "trim whitespace");
    TEST_ASSERT(fields->fields[0].kind == RCFIELD_ITEMTYPE, "kind");

    for( int i = 0; i < 32; i++ )
        TEST_ASSERT(revconfig_buffer_push_field(fields, RCFIELD_ITEMNAME, "x") == 0, "grow push");
    TEST_ASSERT(fields->field_count == 33, "grew past initial capacity");
    TEST_ASSERT(fields->field_capacity >= 33, "capacity grew");

    TEST_ASSERT(
        strcmp(revconfig_field_kind_str(RCFIELD_CACHE_ARCHIVE_ID), "RCFIELD_CACHE_ARCHIVE_ID") == 0,
        "field_kind_str");

    revconfig_buffer_free(fields);

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(2);
    TEST_ASSERT(items != NULL, "item_buffer_new");
    struct RevConfigItem* a = revconfig_item_buffer_push(items);
    struct RevConfigItem* b = revconfig_item_buffer_push(items);
    struct RevConfigItem* c = revconfig_item_buffer_push(items);
    TEST_ASSERT(a && b && c, "item push");
    TEST_ASSERT(items->item_count == 3, "item_count");
    TEST_ASSERT(items->item_capacity >= 3, "item capacity grew");
    revconfig_item_buffer_free(items);
}
