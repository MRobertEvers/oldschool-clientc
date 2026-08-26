#include "test_harness.h"

#include <string.h>

static void
push(
    struct RevConfigBuffer* buf,
    enum RevConfigFieldKind kind,
    char const* value)
{
    TEST_ASSERT(revconfig_buffer_push_field(buf, kind, value) == 0, "push_field");
}

void
test_items_build(void)
{
    printf("TEST: items_build\n");

    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    TEST_ASSERT(fields && items, "alloc");

    /* Dat1-style sprite */
    push(fields, RCFIELD_ITEMTYPE, "sprite");
    push(fields, RCFIELD_ITEMNAME, "invback");
    push(fields, RCFIELD_CACHE_TABLE, "configs");
    push(fields, RCFIELD_CACHE_ARCHIVE, "media");
    push(fields, RCFIELD_CACHE_CONTAINER, "jagfile");
    push(fields, RCFIELD_CACHE_INDEX_FILENAME, "index.dat");
    push(fields, RCFIELD_CACHE_DATA_FILENAME, "invback.dat");
    push(fields, RCFIELD_CACHE_FORMAT, "pix8");
    push(fields, RCFIELD_CACHE_ATLAS_INDEX, "0");
    push(fields, RCFIELD_CACHE_TRANSFORM, "flip_h");
    push(fields, RCFIELD_CACHE_TRANSFORM, "flip_v");
    push(fields, RCFIELD_CACHE_CROP_X, "1");
    push(fields, RCFIELD_CACHE_CROP_Y, "2");
    push(fields, RCFIELD_CACHE_CROP_WIDTH, "3");
    push(fields, RCFIELD_CACHE_CROP_HEIGHT, "4");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Dat2-style sprite */
    push(fields, RCFIELD_ITEMTYPE, "sprite");
    push(fields, RCFIELD_ITEMNAME, "cross");
    push(fields, RCFIELD_CACHE_TABLE, "sprites");
    push(fields, RCFIELD_CACHE_ARCHIVE_ID, "297");
    push(fields, RCFIELD_CACHE_ATLAS_COUNT, "2");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Font defaults */
    push(fields, RCFIELD_ITEMTYPE, "font");
    push(fields, RCFIELD_ITEMNAME, "b12");
    push(fields, RCFIELD_CACHE_FONT_NAME, "b12");
    push(fields, RCFIELD_CACHE_FONT_ID, "2");
    push(fields, RCFIELD_CACHE_ARCHIVE_ID, "496");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Font with only begin defaults (no archive_id / cache_font_id keys) */
    push(fields, RCFIELD_ITEMTYPE, "font");
    push(fields, RCFIELD_ITEMNAME, "p11");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Component: numeric font + componentno default then override */
    push(fields, RCFIELD_ITEMTYPE, "component");
    push(fields, RCFIELD_ITEMNAME, "label");
    push(fields, RCFIELD_UICOMPONENT_TYPE, "rs_text");
    push(fields, RCFIELD_UICOMPONENT_FONT, "2");
    push(fields, RCFIELD_UICOMPONENT_TEXT, "Hello");
    push(fields, RCFIELD_UICOMPONENT_FILLED, "true");
    push(fields, RCFIELD_UICOMPONENT_CENTER, "1");
    push(fields, RCFIELD_UICOMPONENT_SHADOWED, "false");
    push(fields, RCFIELD_ITEMDONE, "");

    push(fields, RCFIELD_ITEMTYPE, "component");
    push(fields, RCFIELD_ITEMNAME, "minimenu");
    push(fields, RCFIELD_UICOMPONENT_TYPE, "sprite");
    push(fields, RCFIELD_UICOMPONENT_FONT, "b12");
    push(fields, RCFIELD_UICOMPONENT_OPTION, "Cancel");
    push(fields, RCFIELD_UICOMPONENT_OPTION_ACTION, "CANCEL");
    push(fields, RCFIELD_UICOMPONENT_BUTTON_TYPE, "close");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Layout multi-entry with group / parent / name */
    push(fields, RCFIELD_ITEMTYPE, "layout");
    push(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push(fields, RCFIELD_UILAYOUT_NAME, "root");
    push(fields, RCFIELD_UILAYOUT_COMPONENT, "label");
    push(fields, RCFIELD_UILAYOUT_X, "10");
    push(fields, RCFIELD_UILAYOUT_Y, "20");
    push(fields, RCFIELD_UILAYOUT_DIRTY, "");
    push(fields, RCFIELD_ITEMDONE, "");
    push(fields, RCFIELD_ITEMTYPE, "layout");
    push(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push(fields, RCFIELD_UILAYOUT_NAME, "child");
    push(fields, RCFIELD_UILAYOUT_PARENT, "root");
    push(fields, RCFIELD_UILAYOUT_COMPONENT, "minimenu");
    push(fields, RCFIELD_UILAYOUT_LEFT, "1");
    push(fields, RCFIELD_UILAYOUT_TOP, "2");
    push(fields, RCFIELD_ITEMDONE, "");

    /* Inv */
    push(fields, RCFIELD_ITEMTYPE, "inv");
    push(fields, RCFIELD_ITEMNAME, "backpack");
    for( int i = 0; i < 3; i++ )
    {
        char slot[16];
        snprintf(slot, sizeof(slot), "item%d", i);
        push(fields, RCFIELD_INV_ITEM, slot);
    }
    push(fields, RCFIELD_ITEMDONE, "");

    /* [features]: names are carried verbatim, numbers keep a "not stated"
     * sentinel that is distinct from every legal value. */
    push(fields, RCFIELD_ITEMTYPE, "features");
    push(fields, RCFIELD_FEATURES_ERA, "osrs");
    push(fields, RCFIELD_FEATURES_GROUND_CLICK_NEAREST, "box10_rect");
    push(fields, RCFIELD_FEATURES_MOVER, "frame");
    push(fields, RCFIELD_FEATURES_GROUND_CLICK_OFFMAP, "1");
    push(fields, RCFIELD_ITEMDONE, "");

    /* [camera]: an item carries only the keys its section stated. */
    push(fields, RCFIELD_ITEMTYPE, "camera");
    push(fields, RCFIELD_CAMERA_ZOOM, "fixed:600");
    push(fields, RCFIELD_CAMERA_CONTROLS, "arrow_keys");
    push(fields, RCFIELD_ITEMDONE, "");

    /* hotkey= repeats into a list; [hotkey:…] is its own item kind. */
    push(fields, RCFIELD_ITEMTYPE, "component");
    push(fields, RCFIELD_ITEMNAME, "tab_icon");
    push(fields, RCFIELD_UICOMPONENT_TYPE, "tab_icon");
    push(fields, RCFIELD_UICOMPONENT_HOTKEY, "select_tab");
    push(fields, RCFIELD_UICOMPONENT_HOTKEY, "another_effect");
    push(fields, RCFIELD_ITEMDONE, "");

    push(fields, RCFIELD_ITEMTYPE, "hotkey");
    push(fields, RCFIELD_ITEMNAME, "f4");
    push(fields, RCFIELD_HOTKEY_COMPONENT, "tab_icon");
    push(fields, RCFIELD_HOTKEY_EFFECT, "select_tab");
    push(fields, RCFIELD_ITEMDONE, "");

    /*
     * `table=defaults slot=N`: the graphic-defaults path, which addresses a
     * sprite by its position in the defaults record rather than by hashing a
     * name. Appended last so the indices above keep their meaning.
     */
    push(fields, RCFIELD_ITEMTYPE, "sprite");
    push(fields, RCFIELD_ITEMNAME, "compass");
    push(fields, RCFIELD_CACHE_TABLE, "defaults");
    push(fields, RCFIELD_CACHE_DEFAULTS_SLOT, "0");
    push(fields, RCFIELD_CACHE_ARCHIVE, "compass");
    push(fields, RCFIELD_ITEMDONE, "");

    revconfig_items_build(fields, items);

    TEST_ASSERT(items->item_count == 14, "item_count");

    struct RevConfigItem const* invback = &items->items[0];
    TEST_ASSERT(invback->kind == RCITEM_CACHE_SPRITE, "invback kind");
    TEST_ASSERT(strcmp(invback->u.cache.name, "invback") == 0, "invback name");
    TEST_ASSERT(strcmp(invback->u.cache.format, "pix8") == 0, "invback format");
    TEST_ASSERT(invback->u.cache.archive_id == -1, "dat1 archive_id default");
    TEST_ASSERT(invback->u.cache.transform_count == 2, "transforms");
    TEST_ASSERT(invback->u.cache.crop_width == 3, "crop");

    struct RevConfigItem const* cross = &items->items[1];
    TEST_ASSERT(cross->kind == RCITEM_CACHE_SPRITE, "cross kind");
    TEST_ASSERT(cross->u.cache.archive_id == 297, "dat2 archive_id");
    TEST_ASSERT(cross->u.cache.atlas_count == 2, "atlas_count");
    /* Absent slot= must read as "not a defaults section", not as slot 0 --
     * otherwise every name-keyed sprite would claim the compass slot. */
    TEST_ASSERT(cross->u.cache.defaults_slot == -1, "defaults_slot default");

    struct RevConfigItem const* defaults_compass = &items->items[13];
    TEST_ASSERT(defaults_compass->kind == RCITEM_CACHE_SPRITE, "defaults sprite kind");
    TEST_ASSERT(strcmp(defaults_compass->u.cache.table, "defaults") == 0, "defaults table");
    TEST_ASSERT(defaults_compass->u.cache.defaults_slot == 0, "defaults slot");
    /* archive= may sit alongside slot= as documentation of what the slot
     * resolved to; it must not disturb the slot. */
    TEST_ASSERT(
        strcmp(defaults_compass->u.cache.archive, "compass") == 0, "defaults archive kept");

    struct RevConfigItem const* b12 = &items->items[2];
    TEST_ASSERT(b12->kind == RCITEM_CACHE_FONT, "b12 kind");
    TEST_ASSERT(b12->u.font.cache_font_id == 2, "b12 cache_font_id");
    TEST_ASSERT(b12->u.font.archive_id == 496, "b12 archive_id");

    struct RevConfigItem const* p11 = &items->items[3];
    TEST_ASSERT(p11->kind == RCITEM_CACHE_FONT, "p11 kind");
    TEST_ASSERT(p11->u.font.archive_id == -1, "p11 archive_id default");
    TEST_ASSERT(p11->u.font.cache_font_id == -1, "p11 cache_font_id default");

    struct RevConfigItem const* label = &items->items[4];
    TEST_ASSERT(label->kind == RCITEM_UICOMPONENT, "label kind");
    TEST_ASSERT(label->u.uicomponent.componentno == -1, "componentno default");
    TEST_ASSERT(label->u.uicomponent.font == 2, "numeric font");
    TEST_ASSERT(label->u.uicomponent.has_font_ref == 0, "no font_ref");
    TEST_ASSERT(strcmp(label->u.uicomponent.text, "Hello") == 0, "text");
    TEST_ASSERT(label->u.uicomponent.filled == 1, "filled");
    TEST_ASSERT(label->u.uicomponent.center == 1, "center");
    TEST_ASSERT(label->u.uicomponent.shadowed == 0, "shadowed");

    struct RevConfigItem const* minimenu = &items->items[5];
    TEST_ASSERT(minimenu->u.uicomponent.has_font_ref == 1, "font_ref set");
    TEST_ASSERT(strcmp(minimenu->u.uicomponent.font_ref, "b12") == 0, "font_ref value");
    TEST_ASSERT(
        minimenu->u.uicomponent.option_action == REVCONFIG_MINIMENU_CANCEL, "option_action");
    TEST_ASSERT(
        minimenu->u.uicomponent.button_type == REVCONFIG_BUTTON_TYPE_CLOSE, "button_type");

    struct RevConfigItem const* root = &items->items[6];
    TEST_ASSERT(root->kind == RCITEM_UILAYOUT, "root layout");
    TEST_ASSERT(strcmp(root->u.uilayout.layout_group, "fixed") == 0, "layout_group");
    TEST_ASSERT(strcmp(root->u.uilayout.name, "root") == 0, "layout name");
    TEST_ASSERT(root->u.uilayout.x == 10 && root->u.uilayout.y == 20, "xy");
    TEST_ASSERT(root->u.uilayout.dirty == 1, "dirty presence");

    struct RevConfigItem const* child = &items->items[7];
    TEST_ASSERT(strcmp(child->u.uilayout.parent, "root") == 0, "parent");
    TEST_ASSERT(child->u.uilayout.left == 1 && child->u.uilayout.top == 2, "insets");

    struct RevConfigItem const* backpack = &items->items[8];
    TEST_ASSERT(backpack->kind == RCITEM_INV, "inv kind");
    TEST_ASSERT(backpack->u.inv.item_count == 3, "inv count");
    TEST_ASSERT(strcmp(backpack->u.inv.items[1], "item1") == 0, "inv item");

    struct RevConfigItem const* features = &items->items[9];
    TEST_ASSERT(features->kind == RCITEM_FEATURES, "features item kind");
    TEST_ASSERT(strcmp(features->u.features.era, "osrs") == 0, "features era");
    TEST_ASSERT(
        strcmp(features->u.features.ground_click_nearest, "box10_rect") == 0,
        "features ground_click_nearest");
    TEST_ASSERT(strcmp(features->u.features.mover, "frame") == 0, "features mover");
    TEST_ASSERT(features->u.features.ground_click_offmap == 1, "features offmap stated");
    TEST_ASSERT(
        features->u.features.ground_click_unbounded == -1, "features unbounded unstated");
    TEST_ASSERT(
        features->u.features.painter_draw_distance == 0, "features draw distance unstated");

    struct RevConfigItem const* camera = &items->items[10];
    TEST_ASSERT(camera->kind == RCITEM_CAMERA, "camera item kind");
    TEST_ASSERT(camera->u.camera.has_zoom == 1, "camera zoom stated");
    TEST_ASSERT(
        camera->u.camera.zoom_mode == REVCONFIG_CAMERA_ZOOM_FIXED, "camera zoom fixed");
    TEST_ASSERT(camera->u.camera.zoom_height == 600, "camera zoom height");
    TEST_ASSERT(camera->u.camera.has_controls == 1, "camera controls stated");
    TEST_ASSERT(
        camera->u.camera.controls == REVCONFIG_CAMERA_CONTROL_ARROW_KEYS,
        "camera controls arrow_keys only");

    struct RevConfigItem const* tab_icon = &items->items[11];
    TEST_ASSERT(tab_icon->u.uicomponent.hotkey_count == 2, "hotkey= repeats");
    TEST_ASSERT(strcmp(tab_icon->u.uicomponent.hotkeys[0], "select_tab") == 0, "hotkey 0");
    TEST_ASSERT(strcmp(tab_icon->u.uicomponent.hotkeys[1], "another_effect") == 0, "hotkey 1");

    struct RevConfigItem const* binding = &items->items[12];
    TEST_ASSERT(binding->kind == RCITEM_HOTKEY, "hotkey item kind");
    TEST_ASSERT(strcmp(binding->u.hotkey.name, "f4") == 0, "hotkey key name");
    TEST_ASSERT(strcmp(binding->u.hotkey.component, "tab_icon") == 0, "hotkey c=");
    TEST_ASSERT(strcmp(binding->u.hotkey.effect, "select_tab") == 0, "hotkey e=");

    revconfig_buffer_free(fields);
    revconfig_item_buffer_free(items);
}
