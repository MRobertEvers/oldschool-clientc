#ifndef SIDENAV_FIXTURE_H
#define SIDENAV_FIXTURE_H

#include "test_sprites.h"
#include "ui/uitree.h"

#include <stdint.h>

enum
{
    SIDENAV_CANVAS_W = 765,
    SIDENAV_CANVAS_H = 503,
    SIDENAV_ICON_W = 32,
    SIDENAV_ICON_H = 32,
    SIDENAV_TAB_COUNT = 14,
    SIDENAV_CROP_X = 540,
    SIDENAV_CROP_Y = 155,
    SIDENAV_CROP_W = 225,
    SIDENAV_CROP_H = 335,
};

struct SidenavTabSlot
{
    int tabno;
    int x;
    int y;
    int redstone_atlas;
    int32_t redstone_index;
    int32_t icon_index;
};

struct SidenavFixture
{
    struct UITree* tree;
    struct TestSpriteSet sprites;
    int32_t root_index;
    struct SidenavTabSlot tabs[SIDENAV_TAB_COUNT];
    int32_t equipment_icon_index;
    int32_t inventory_icon_index;
    int32_t prayer_icon_index;
};

bool
sidenav_fixture_build(struct SidenavFixture* fixture);

void
sidenav_fixture_free(struct SidenavFixture* fixture);

int
sidenav_fixture_click_center_x(struct SidenavTabSlot const* slot);

int
sidenav_fixture_click_center_y(struct SidenavTabSlot const* slot);

struct SidenavTabSlot const*
sidenav_fixture_find_tab(struct SidenavFixture const* fixture, int tabno);

int
sidenav_fixture_redstone_center_x(int tabno);

int
sidenav_fixture_redstone_center_y(int tabno);

int
sidenav_fixture_redstone_sample_x(int tabno);

int
sidenav_fixture_redstone_sample_y(int tabno);

#endif
