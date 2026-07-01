#include "ui_tree_draw.h"

#include "bmp.h"
#include "ui/uitree_layout.h"
#include "toridraw/toridraw_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fill_rect(
    int* pixels,
    int stride,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb)
{
    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > width )
        x1 = width;
    if( y1 > height )
        y1 = height;
    for( int y = y0; y < y1; y++ )
    {
        for( int x = x0; x < x1; x++ )
            pixels[y * stride + x] = argb;
    }
}

static void
blit_sprite(
    struct UiTreeDrawContext* ctx,
    struct ToriDraw_Sprite* sprite,
    int x,
    int y)
{
    if( !sprite )
        return;

    struct ToriDraw_ViewPort vp = { 0 };
    vp.stride = ctx->width;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = ctx->width;
    vp.clip_bottom = ctx->height;
    ToriDraw2D_BlitSprite(sprite, &vp, x, y, ctx->pixels);
}

static void
emit_component(
    struct UiTreeDrawContext* ctx,
    struct UITree const* tree,
    int32_t index)
{
    if( !ctx || !tree || index < 0 || (uint32_t)index >= tree->component_count )
        return;

    struct StaticUIComponent const* component = &tree->components[index];
    if( !uitree_component_visible_host(component, ctx->hovered, ctx->host) )
        return;
    if( !uitree_component_should_emit(component, ctx->host) )
        return;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);

    switch( component->type )
    {
    case UIELEM_BUILTIN_TAB_ICONS:
    {
        struct ToriDraw_Sprite* sprite = test_sprites_get(
            ctx->sprites,
            component->u.tab_icon.scene_id,
            component->u.tab_icon.atlas_index);
        blit_sprite(ctx, sprite, bx, by);
        break;
    }
    case UIELEM_BUILTIN_REDSTONE_TAB:
    {
        int scene_id = component->u.redstone_tab.scene_id_active;
        int atlas_index = component->u.redstone_tab.atlas_index_active;
        if( scene_id < 0 )
        {
            scene_id = component->u.redstone_tab.scene_id;
            atlas_index = component->u.redstone_tab.atlas_index;
        }
        struct ToriDraw_Sprite* sprite = test_sprites_get(ctx->sprites, scene_id, atlas_index);
        blit_sprite(ctx, sprite, bx, by);
        break;
    }
    case UIELEM_RS_LAYER:
        break;
    case UIELEM_RS_RECT:
    {
        int color = component->u.rs_rect.color;
        if( color != 0 )
            fill_rect(ctx->pixels, ctx->width, ctx->width, ctx->height, bx, by, bx + bw, by + bh, color);
        break;
    }
    default:
        break;
    }
}

static void
draw_node(
    struct UiTreeDrawContext* ctx,
    struct UITree const* tree,
    int32_t index)
{
    if( index < 0 || (uint32_t)index >= tree->component_count )
        return;

    emit_component(ctx, tree, index);

    struct StaticUIComponent const* component = &tree->components[index];
    draw_node(ctx, tree, component->first_child);
    draw_node(ctx, tree, component->next_sibling);
}

void
ui_tree_draw_clear(struct UiTreeDrawContext* ctx, int argb)
{
    if( !ctx || !ctx->pixels )
        return;
    for( int i = 0; i < ctx->width * ctx->height; i++ )
        ctx->pixels[i] = argb;
}

void
ui_tree_draw_tree(
    struct UiTreeDrawContext* ctx,
    struct UITree const* tree,
    int32_t root_index)
{
    if( !ctx || !tree )
        return;
    draw_node(ctx, tree, root_index);
}

void
ui_tree_draw_write_bmp_cropped(
    struct UiTreeDrawContext const* ctx,
    char const* path)
{
    if( !ctx || !ctx->pixels || !path )
        return;

    int* cropped = malloc((size_t)SIDENAV_CROP_W * (size_t)SIDENAV_CROP_H * sizeof(int));
    if( !cropped )
    {
        fprintf(stderr, "Failed to allocate crop buffer for %s\n", path);
        return;
    }

    for( int y = 0; y < SIDENAV_CROP_H; y++ )
    {
        int src_y = SIDENAV_CROP_Y + y;
        for( int x = 0; x < SIDENAV_CROP_W; x++ )
        {
            int src_x = SIDENAV_CROP_X + x;
            cropped[y * SIDENAV_CROP_W + x] = ctx->pixels[src_y * ctx->width + src_x];
        }
    }

    bmp_write_file(path, cropped, SIDENAV_CROP_W, SIDENAV_CROP_H);
    free(cropped);
}

int
ui_tree_draw_sample_pixel(
    struct UiTreeDrawContext const* ctx,
    int x,
    int y)
{
    if( !ctx || !ctx->pixels || x < 0 || y < 0 || x >= ctx->width || y >= ctx->height )
        return 0;
    return ctx->pixels[y * ctx->width + x];
}
