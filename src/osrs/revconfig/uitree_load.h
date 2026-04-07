#ifndef UITREE_LOAD_H
#define UITREE_LOAD_H

#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "uiscene.h"
#include "uitree.h"

struct Scene2;

void
uitree_from_revconfig_buildcachedat(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct RevConfigBuffer* revconfig_buffer);

// /** Push RS `StaticUIComponent` rows and register sprites/models in ui_scene / scene2. */
// void
// static_ui_rs_from_buildcachedat(p
//     struct UITree* ui,
//     struct UIScene* ui_scene,
//     struct Scene2* scene2,
//     struct BuildCacheDat* buildcachedat);

#endif