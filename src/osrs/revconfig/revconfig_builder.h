#ifndef REVCONFIG_BUILDER_H
#define REVCONFIG_BUILDER_H

#include <stdint.h>

struct UITree;

/** Independent of UIFRAME_LAYER_STACK depth; build-time nesting may be deeper. */
#define REVCONFIG_BUILDER_PARENT_STACK_MAX 64

struct RevConfigBuilder
{
    struct UITree* tree;
    int32_t current_parent;
    int32_t stack[REVCONFIG_BUILDER_PARENT_STACK_MAX];
    int stack_top;
};

struct RevConfigBuilder*
revconfig_builder_new(
    struct UITree* tree,
    int32_t root_parent);

void
revconfig_builder_free(struct RevConfigBuilder* b);

int32_t
revconfig_builder_current_parent(struct RevConfigBuilder const* b);

void
revconfig_builder_set_parent(
    struct RevConfigBuilder* b,
    int32_t parent_index);

int32_t
revconfig_builder_push_rs_layer_begin(
    struct RevConfigBuilder* b,
    int component_id,
    int scroll_height,
    int hide,
    int x,
    int y,
    int width,
    int height);

int
revconfig_builder_push_rs_layer_end(struct RevConfigBuilder* b);

int32_t
revconfig_builder_push_rs_model(
    struct RevConfigBuilder* b,
    int component_id,
    int scene_id,
    int x,
    int y,
    int width,
    int height);

#endif
