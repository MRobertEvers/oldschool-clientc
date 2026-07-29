#include "test_harness.h"

#include <string.h>

static uint32_t
count_kind(
    struct RevConfigBuffer const* buf,
    enum RevConfigFieldKind kind)
{
    uint32_t n = 0;
    for( uint32_t i = 0; i < buf->field_count; i++ )
    {
        if( buf->fields[i].kind == kind )
            n++;
    }
    return n;
}

void
test_load(void)
{
    printf("TEST: load\n");

    struct RevConfigBuffer* fields = revconfig_buffer_new(128);
    TEST_ASSERT(fields != NULL, "alloc");

    revconfig_load_fields_from_ini_bytes(NULL, 0, fields);
    TEST_ASSERT(fields->field_count == 0, "null data no-op");

    revconfig_load_fields_from_ini_bytes((const uint8_t*)"", 0, fields);
    TEST_ASSERT(fields->field_count == 0, "empty size no-op");

    static char const ini[] =
        "[sprite:invback]\n"
        "table=configs\n"
        "archive=media\n"
        "filename=invback.dat\n"
        "format=pix8\n"
        "\n"
        "[component:compass]\n"
        "type=compass\n"
        "sprite=compass\n"
        "w=33\n"
        "h=33\n"
        "\n"
        "[component:world]\n"
        "type=world\n"
        "mmb_rotate=true\n"
        "wheel_zoom=false\n"
        "hotkey=select_tab\n"
        "hotkey=another_effect\n"
        "\n"
        "[hotkey:f4]\n"
        "c=world\n"
        "e=select_tab\n"
        "\n"
        "[layout:fixed]\n"
        "n=compass_slot\n"
        "c=compass\n"
        "x=10\n"
        "y=20\n"
        "w=40\n"
        "h=40\n"
        "=\n"
        "n=child\n"
        "c=compass\n"
        "p=compass_slot\n"
        "left=1\n"
        "\n"
        "[inv:backpack]\n"
        "item=bronze_dagger\n"
        "item=coins\n";

    revconfig_load_fields_from_ini_bytes((const uint8_t*)ini, (uint32_t)strlen(ini), fields);

    TEST_ASSERT(count_kind(fields, RCFIELD_ITEMTYPE) >= 4, "item types");
    TEST_ASSERT(count_kind(fields, RCFIELD_ITEMDONE) >= 4, "item dones");
    TEST_ASSERT(count_kind(fields, RCFIELD_UICOMPONENT_WIDTH) == 1, "component w");
    TEST_ASSERT(count_kind(fields, RCFIELD_UILAYOUT_WIDTH) == 1, "layout w");
    TEST_ASSERT(count_kind(fields, RCFIELD_UILAYOUT_GROUP) >= 2, "layout group re-emit");
    TEST_ASSERT(count_kind(fields, RCFIELD_INV_ITEM) == 2, "inv items");
    /* Repeated component key, and `c=` disambiguated by section: it must land
     * as a hotkey component here, not as a layout component. */
    TEST_ASSERT(count_kind(fields, RCFIELD_UICOMPONENT_HOTKEY) == 2, "hotkey= repeats");
    TEST_ASSERT(count_kind(fields, RCFIELD_HOTKEY_COMPONENT) == 1, "hotkey c=");
    TEST_ASSERT(count_kind(fields, RCFIELD_HOTKEY_EFFECT) == 1, "hotkey e=");
    TEST_ASSERT(count_kind(fields, RCFIELD_UILAYOUT_COMPONENT) == 2, "layout c= unaffected");

    /* Trailing ITEMDONE always present */
    TEST_ASSERT(
        fields->fields[fields->field_count - 1].kind == RCFIELD_ITEMDONE, "trailing ITEMDONE");

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    revconfig_items_build(fields, items);

    int sprites = 0, components = 0, layouts = 0, invs = 0;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        switch( items->items[i].kind )
        {
        case RCITEM_CACHE_SPRITE:
            sprites++;
            TEST_ASSERT(strcmp(items->items[i].u.cache.name, "invback") == 0, "sprite name");
            break;
        case RCITEM_UICOMPONENT:
            components++;
            if( strcmp(items->items[i].u.uicomponent.name, "compass") == 0 )
                TEST_ASSERT(items->items[i].u.uicomponent.width == 33, "component w value");
            else
            {
                TEST_ASSERT(items->items[i].u.uicomponent.mmb_rotate == 1, "mmb_rotate=true");
                TEST_ASSERT(items->items[i].u.uicomponent.wheel_zoom == 0, "wheel_zoom=false");
            }
            break;
        case RCITEM_UILAYOUT:
            layouts++;
            break;
        case RCITEM_INV:
            invs++;
            TEST_ASSERT(items->items[i].u.inv.item_count == 2, "inv count");
            break;
        default:
            break;
        }
    }

    TEST_ASSERT(sprites == 1, "one sprite");
    TEST_ASSERT(components == 2, "two components");
    TEST_ASSERT(layouts == 2, "two layouts from bare =");
    TEST_ASSERT(invs == 1, "one inv");

    struct RevConfigItem const* first_layout = NULL;
    struct RevConfigItem const* second_layout = NULL;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind != RCITEM_UILAYOUT )
            continue;
        if( !first_layout )
            first_layout = &items->items[i];
        else
            second_layout = &items->items[i];
    }
    TEST_ASSERT(first_layout && second_layout, "both layouts");
    TEST_ASSERT(strcmp(first_layout->u.uilayout.layout_group, "fixed") == 0, "group");
    TEST_ASSERT(strcmp(first_layout->u.uilayout.name, "compass_slot") == 0, "n=");
    TEST_ASSERT(first_layout->u.uilayout.width == 40, "layout size override");
    TEST_ASSERT(strcmp(second_layout->u.uilayout.parent, "compass_slot") == 0, "p=");

    revconfig_buffer_free(fields);
    revconfig_item_buffer_free(items);
}
