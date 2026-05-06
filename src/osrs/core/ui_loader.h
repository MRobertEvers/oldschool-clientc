#ifndef OSRS_CORE_UI_LOADER_H
#define OSRS_CORE_UI_LOADER_H

struct GGame;

enum UILoaderKind
{
    UI_LOADER_KIND_INVALID = 0,
    UI_LOADER_KIND_REV245_2 = 1,
    UI_LOADER_KIND_OS217 = 2,
    UI_LOADER_KIND_REV254 = 3,
};

struct UILoader
{
    enum UILoaderKind kind;
    void* impl;
};

const char*
ui_loader_ini_path(const struct UILoader* u);

const char*
ui_loader_cache_ini_path(const struct UILoader* u);

void
ui_loader_load(
    const struct UILoader* u,
    struct GGame* game);

#endif
