#ifndef UI_TREE_DRAW_H
#define UI_TREE_DRAW_H

#include "sidenav_fixture.h"
#include "ui/uitree_host.h"

#include <stdint.h>

struct UiTreeDrawContext
{
    int* pixels;
    int width;
    int height;
    struct TestSpriteSet const* sprites;
    struct UITreeHost const* host;
    int32_t hovered;
};

void
ui_tree_draw_clear(struct UiTreeDrawContext* ctx, int argb);

void
ui_tree_draw_tree(
    struct UiTreeDrawContext* ctx,
    struct UITree const* tree,
    int32_t root_index);

void
ui_tree_draw_write_bmp_cropped(
    struct UiTreeDrawContext const* ctx,
    char const* path);

int
ui_tree_draw_sample_pixel(
    struct UiTreeDrawContext const* ctx,
    int x,
    int y);

#endif
