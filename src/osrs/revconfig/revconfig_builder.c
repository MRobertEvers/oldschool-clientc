#include "revconfig_builder.h"

#include "osrs/revconfig/uitree.h"

#include <stdlib.h>

struct RevConfigBuilder*
revconfig_builder_new(
    struct UITree* tree,
    int32_t root_parent)
{
    struct RevConfigBuilder* b = malloc(sizeof(struct RevConfigBuilder));
    if( !b )
        return NULL;
    b->tree = tree;
    b->current_parent = root_parent;
    b->stack_top = 0;
    return b;
}

void
revconfig_builder_free(struct RevConfigBuilder* b)
{
    free(b);
}

int32_t
revconfig_builder_current_parent(struct RevConfigBuilder const* b)
{
    return b ? b->current_parent : -1;
}

void
revconfig_builder_set_parent(
    struct RevConfigBuilder* b,
    int32_t parent_index)
{
    if( !b )
        return;
    b->current_parent = parent_index;
}

int32_t
revconfig_builder_push_rs_layer_begin(
    struct RevConfigBuilder* b,
    int component_id,
    int scroll_height,
    int hide,
    int x,
    int y,
    int width,
    int height)
{
    if( !b || !b->tree )
        return -1;
    if( b->stack_top >= REVCONFIG_BUILDER_PARENT_STACK_MAX )
        return -1;

    int32_t idx = uitree_push_rs_layer(
        b->tree, b->current_parent, component_id, scroll_height, hide, x, y, width, height);
    if( idx < 0 )
        return -1;

    b->stack[b->stack_top++] = b->current_parent;
    b->current_parent = idx;
    return idx;
}

int
revconfig_builder_push_rs_layer_end(struct RevConfigBuilder* b)
{
    if( !b )
        return -1;
    if( b->stack_top <= 0 )
        return -1;
    b->current_parent = b->stack[--b->stack_top];
    return 0;
}

int32_t
revconfig_builder_push_rs_model(
    struct RevConfigBuilder* b,
    int component_id,
    int scene_id,
    int x,
    int y,
    int width,
    int height)
{
    if( !b || !b->tree )
        return -1;
    return uitree_push_rs_model(
        b->tree, b->current_parent, component_id, scene_id, x, y, width, height);
}
