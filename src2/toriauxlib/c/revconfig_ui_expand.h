#ifndef REVCONFIG_UI_EXPAND_H
#define REVCONFIG_UI_EXPAND_H

#include "revconfig_ui_build.h"
#include "ui/uitree.h"

#include <stdint.h>

void
revconfig_ui_preload_interface_sprites(struct RevConfigUIBuildState* state);

void
revconfig_ui_expand_sidebar(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    int32_t sidebar_idx,
    int componentno,
    int inv_index,
    int sidebar_x,
    int sidebar_y);

#endif
