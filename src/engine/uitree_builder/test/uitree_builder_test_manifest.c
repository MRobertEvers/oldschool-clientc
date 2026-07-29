#include "uitree_builder_manifest.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
push_field(
    struct RevConfigBuffer* buf,
    enum RevConfigFieldKind kind,
    char const* value)
{
    TEST_ASSERT(revconfig_buffer_push_field(buf, kind, value) == 0, "push_field");
}

static void
test_manifest_from_hand_pushed_fields(void)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    TEST_ASSERT(fields && items, "alloc");

    /* [sprite:invback] dat1-style */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "invback");
    push_field(fields, RCFIELD_CACHE_FORMAT, "pix8");
    push_field(fields, RCFIELD_CACHE_DATA_FILENAME, "invback.dat");
    push_field(fields, RCFIELD_CACHE_INDEX_FILENAME, "index.dat");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [sprite:cross] dat2-style */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "cross");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "297");
    push_field(fields, RCFIELD_CACHE_ATLAS_COUNT, "2");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [font:b12] */
    push_field(fields, RCFIELD_ITEMTYPE, "font");
    push_field(fields, RCFIELD_ITEMNAME, "b12");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "496");
    push_field(fields, RCFIELD_CACHE_FONT_ID, "2");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [component:sidebar] needs RS load */
    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "sidebar");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "sidebar");
    push_field(fields, RCFIELD_UICOMPONENT_COMPONENTNO, "149");
    push_field(fields, RCFIELD_UICOMPONENT_INV, "inventory");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [component:compass] builtin, no RS. Two hotkey= lines: the repeat is the
     * point — one component may advertise several effects. */
    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "compass");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "compass");
    push_field(fields, RCFIELD_UICOMPONENT_SPRITE, "cross");
    push_field(fields, RCFIELD_UICOMPONENT_WIDTH, "33");
    push_field(fields, RCFIELD_UICOMPONENT_HEIGHT, "33");
    push_field(fields, RCFIELD_UICOMPONENT_HOTKEY, "select_tab");
    push_field(fields, RCFIELD_UICOMPONENT_HOTKEY, "some_future_effect");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [layout] entries */
    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "compass_slot");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "compass");
    push_field(fields, RCFIELD_UILAYOUT_X, "10");
    push_field(fields, RCFIELD_UILAYOUT_Y, "20");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "sidebar_slot");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "sidebar");
    push_field(fields, RCFIELD_UILAYOUT_PARENT, "compass_slot");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [hotkey:f1] / [hotkey:3] — two keys onto the same component + effect,
     * plus one malformed section (no e=) that must be dropped here rather than
     * reaching the bake. */
    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "f1");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_HOTKEY_EFFECT, "select_tab");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "3");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_HOTKEY_EFFECT, "select_tab");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "f2");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [inv:inventory] */
    push_field(fields, RCFIELD_ITEMTYPE, "inv");
    push_field(fields, RCFIELD_ITEMNAME, "inventory");
    push_field(fields, RCFIELD_INV_ITEM, "1333");
    push_field(fields, RCFIELD_INV_ITEM, "4151");
    push_field(fields, RCFIELD_ITEMDONE, "");

    revconfig_items_build(fields, items);

    struct UIBuilderManifest manifest;
    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(uibuilder_manifest_from_revconfig(&manifest, items) == 0, "from_revconfig");

    TEST_ASSERT(manifest.sprite_count == 2, "sprite req count");
    TEST_ASSERT(manifest.font_count == 1, "font req count");
    TEST_ASSERT(manifest.component_count == 1, "component req count");
    TEST_ASSERT(manifest.inv_count == 1, "inv seed count");
    TEST_ASSERT(manifest.op_count == 2, "tree op count");

    TEST_ASSERT(strcmp(manifest.sprites[0].name, "invback") == 0, "sprite0 name");
    TEST_ASSERT(manifest.sprites[0].archive_id == -1, "dat1 sprite archive_id");
    TEST_ASSERT(manifest.sprites[1].archive_id == 297, "dat2 sprite archive_id");

    TEST_ASSERT(manifest.components[0].iface_id == 149, "packed iface");
    TEST_ASSERT(manifest.components[0].packed_id == (149 << 16), "packed id");

    TEST_ASSERT(manifest.ops[0].kind == UIBUILDER_OP_PUSH_BUILTIN, "compass builtin");
    TEST_ASSERT(strcmp(manifest.ops[0].type, "compass") == 0, "compass type");
    TEST_ASSERT(manifest.ops[1].kind == UIBUILDER_OP_PUSH_RS_SUBTREE, "sidebar rs");
    TEST_ASSERT(manifest.ops[1].componentno == 149, "sidebar componentno");

    /* The compass op carries the component name (the bake resolves bindings by
     * component, not by layout-entry name) and both advertised effects. */
    TEST_ASSERT(strcmp(manifest.ops[0].component_name, "compass") == 0, "op component name");
    TEST_ASSERT(manifest.ops[0].hotkey_count == 2, "advertised effect count");
    TEST_ASSERT(strcmp(manifest.ops[0].hotkeys[0], "select_tab") == 0, "advertised effect 0");

    /* Two well-formed bindings kept in order; the one missing e= is dropped. */
    TEST_ASSERT(manifest.hotkey_count == 2, "hotkey binding count");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].key_name, "f1") == 0, "binding0 key");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].component_name, "compass") == 0, "binding0 component");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].effect, "select_tab") == 0, "binding0 effect");
    TEST_ASSERT(strcmp(manifest.hotkeys[1].key_name, "3") == 0, "binding1 key");

    TEST_ASSERT(manifest.invs[0].item_count == 2, "inv items");
    TEST_ASSERT(manifest.invs[0].obj_ids[0] == 1333, "inv obj0");
    TEST_ASSERT(manifest.invs[0].obj_counts[0] == 1, "inv count default");

    TEST_ASSERT(uibuilder_pack_component_id(149) == (149 << 16), "pack small");
    TEST_ASSERT(uibuilder_pack_component_id(10616833) == 10616833, "pack already");

    uibuilder_manifest_free(&manifest);
    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

int
main(void)
{
    g_failures = 0;
    test_manifest_from_hand_pushed_fields();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_builder_test_manifest: ok\n");
    return 0;
}
