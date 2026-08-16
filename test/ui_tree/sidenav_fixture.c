#include "sidenav_fixture.h"

#include <stdlib.h>
#include <string.h>

/* Kronos 14-tab layout: top row matches legacy fixed coords; bottom row from cache dump. */
static struct SidenavTabSlot const k_tab_layout[SIDENAV_TAB_COUNT] = {
    { 0, 545, 172, TEST_REDSTONE_1, -1, -1 },
    { 1, 569, 170, TEST_REDSTONE_2, -1, -1 },
    { 2, 598, 170, TEST_REDSTONE_2, -1, -1 },
    { 3, 631, 172, TEST_REDSTONE_3, -1, -1 },
    { 4, 669, 173, TEST_REDSTONE_2H, -1, -1 },
    { 5, 696, 171, TEST_REDSTONE_2H, -1, -1 },
    { 6, 724, 173, TEST_REDSTONE_1H, -1, -1 },
    { 7, 529, 470, TEST_REDSTONE_2V, -1, -1 },
    { 8, 596, 470, TEST_REDSTONE_2V, -1, -1 },
    { 9, 570, 468, TEST_REDSTONE_3V, -1, -1 },
    { 10, 633, 470, TEST_REDSTONE_2HV, -1, -1 },
    { 11, 662, 468, TEST_REDSTONE_2HV, -1, -1 },
    { 12, 695, 468, TEST_REDSTONE_1HV, -1, -1 },
    { 13, 729, 470, TEST_REDSTONE_1HV, -1, -1 },
};

static int
redstone_y_for_tab(int tabno)
{
    return tabno <= 6 ? 169 : 465;
}

static int
redstone_x_for_tab(int tabno)
{
    switch( tabno )
    {
    case 0:
        return 538;
    case 1:
        return 570;
    case 2:
        return 598;
    case 3:
        return 626;
    case 4:
        return 669;
    case 5:
        return 696;
    case 6:
        return 724;
    case 7:
        return 522;
    case 8:
        return 593;
    case 9:
        return 570;
    case 10:
        return 625;
    case 11:
        return 659;
    case 12:
        return 697;
    case 13:
        return 762;
    default:
        return 0;
    }
}

bool
sidenav_fixture_build(struct SidenavFixture* fixture)
{
    if( !fixture )
        return false;
    memset(fixture, 0, sizeof(*fixture));

    if( !test_sprites_create(&fixture->sprites) )
        return false;

    fixture->tree = uitree_new(96);
    if( !fixture->tree )
    {
        test_sprites_free(&fixture->sprites);
        return false;
    }

    struct UINodeSpec root_spec = { 0 };
    root_spec.type = UIELEM_RS_LAYER;
    root_spec.component_id = -1;
    root_spec.x = 0;
    root_spec.y = 0;
    root_spec.width = SIDENAV_CANVAS_W;
    root_spec.height = SIDENAV_CANVAS_H;
    fixture->root_index = uitree_push(fixture->tree, -1, &root_spec);
    if( fixture->root_index < 0 )
    {
        sidenav_fixture_free(fixture);
        return false;
    }

    memcpy(fixture->tabs, k_tab_layout, sizeof(k_tab_layout));

    for( int i = 0; i < SIDENAV_TAB_COUNT; i++ )
    {
        struct SidenavTabSlot* slot = &fixture->tabs[i];
        int const rx = redstone_x_for_tab(slot->tabno);
        int const ry = redstone_y_for_tab(slot->tabno);

        struct UINodeSpec redstone_spec = { 0 };
        redstone_spec.type = UIELEM_BUILTIN_REDSTONE_TAB;
        redstone_spec.x = rx;
        redstone_spec.y = ry;
        redstone_spec.width = SIDENAV_ICON_W;
        redstone_spec.height = SIDENAV_ICON_H;
        redstone_spec.u.redstone_tab.tabno = slot->tabno;
        redstone_spec.u.redstone_tab.scene_id = -1;
        redstone_spec.u.redstone_tab.atlas_index = 0;
        redstone_spec.u.redstone_tab.scene_id_active = TEST_SCENE_REDSTONE;
        redstone_spec.u.redstone_tab.atlas_index_active = slot->redstone_atlas;
        slot->redstone_index = uitree_push(fixture->tree, fixture->root_index, &redstone_spec);

        struct UINodeSpec icon_spec = { 0 };
        icon_spec.type = UIELEM_BUILTIN_TAB_ICONS;
        icon_spec.x = slot->x;
        icon_spec.y = slot->y;
        icon_spec.width = SIDENAV_ICON_W;
        icon_spec.height = SIDENAV_ICON_H;
        icon_spec.u.tab_icon.tabno = slot->tabno;
        icon_spec.u.tab_icon.scene_id = TEST_SCENE_SIDEICONS;
        icon_spec.u.tab_icon.atlas_index = slot->tabno;
        slot->icon_index = uitree_push(fixture->tree, fixture->root_index, &icon_spec);
    }

    fixture->equipment_icon_index = fixture->tabs[4].icon_index;
    fixture->inventory_icon_index = fixture->tabs[3].icon_index;
    fixture->prayer_icon_index = fixture->tabs[5].icon_index;
    return true;
}

void
sidenav_fixture_free(struct SidenavFixture* fixture)
{
    if( !fixture )
        return;
    if( fixture->tree )
        uitree_free(fixture->tree);
    test_sprites_free(&fixture->sprites);
    memset(fixture, 0, sizeof(*fixture));
}

int
sidenav_fixture_click_center_x(struct SidenavTabSlot const* slot)
{
    return slot ? slot->x + SIDENAV_ICON_W / 2 : 0;
}

int
sidenav_fixture_click_center_y(struct SidenavTabSlot const* slot)
{
    return slot ? slot->y + SIDENAV_ICON_H / 2 : 0;
}

struct SidenavTabSlot const*
sidenav_fixture_find_tab(struct SidenavFixture const* fixture, int tabno)
{
    if( !fixture )
        return NULL;
    for( int i = 0; i < SIDENAV_TAB_COUNT; i++ )
    {
        if( fixture->tabs[i].tabno == tabno )
            return &fixture->tabs[i];
    }
    return NULL;
}

int
sidenav_fixture_redstone_center_x(int tabno)
{
    return redstone_x_for_tab(tabno) + SIDENAV_ICON_W / 2;
}

int
sidenav_fixture_redstone_center_y(int tabno)
{
    return redstone_y_for_tab(tabno) + SIDENAV_ICON_H / 2;
}

int
sidenav_fixture_redstone_sample_x(int tabno)
{
    return redstone_x_for_tab(tabno) + 2;
}

int
sidenav_fixture_redstone_sample_y(int tabno)
{
    return redstone_y_for_tab(tabno) + 2;
}
