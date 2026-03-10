#ifndef STATIC_UI_LOAD_H
#define STATIC_UI_LOAD_H

#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "static_ui.h"
#include "uiscene.h"

void
static_ui_from_revconfig_buildcachedat(
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct RevConfigBuffer* revconfig_buffer);

#endif