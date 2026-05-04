#include "osrs/core/ui_loader.h"

#include <assert.h>

struct GGame;

const char*
ui_loader_ini_path(const struct UILoader* u)
{
    if( !u )
        return "";
    switch( u->kind )
    {
    case UI_LOADER_KIND_REV245_2:
        return "rev_245_2/rev_245_2_ui.ini";
    case UI_LOADER_KIND_REV254:
        return "rev_lc254/rev_lc254_ui.ini";
    case UI_LOADER_KIND_OS217:
        assert(0 && "ui_loader_ini_path: OS217 not wired");
        return "";
    case UI_LOADER_KIND_INVALID:
    default:
        assert(0 && "ui_loader_ini_path: invalid UILoaderKind");
        return "";
    }
}

const char*
ui_loader_cache_ini_path(const struct UILoader* u)
{
    if( !u )
        return "";
    switch( u->kind )
    {
    case UI_LOADER_KIND_REV245_2:
        return "rev_245_2/rev_245_2_cache.ini";
    case UI_LOADER_KIND_REV254:
        return "rev_lc254/rev_lc254_cache.ini";
    case UI_LOADER_KIND_OS217:
        assert(0 && "ui_loader_cache_ini_path: OS217 not wired");
        return "";
    case UI_LOADER_KIND_INVALID:
    default:
        assert(0 && "ui_loader_cache_ini_path: invalid UILoaderKind");
        return "";
    }
}

void
ui_loader_load(const struct UILoader* u, struct GGame* game)
{
    (void)game;
    if( !u )
        return;
    switch( u->kind )
    {
    case UI_LOADER_KIND_REV245_2:
    case UI_LOADER_KIND_REV254:
        /* Actual UI load is still driven from Lua init_ui; seam only. */
        return;
    case UI_LOADER_KIND_OS217:
        assert(0 && "ui_loader_load: OS217 not wired");
        return;
    case UI_LOADER_KIND_INVALID:
    default:
        assert(0 && "ui_loader_load: invalid UILoaderKind");
        return;
    }
}
