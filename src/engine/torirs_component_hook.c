#include "engine/torirs_component_hook.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>

static char const* const k_hook_names[] = {
    "on_load",         "on_click",        "on_op",
    "on_mouse_over",   "on_mouse_leave",  "on_drag",
    "on_drag_complete", "on_hold",        "on_mouse_repeat",
    "on_scroll_wheel", "on_timer",        "on_click_repeat",
    "on_release",      "on_target_enter", "on_target_leave",
    "on_varp_transmit", "on_inv_transmit", "on_stat_transmit",
};

_Static_assert(
    sizeof(k_hook_names) / sizeof(k_hook_names[0]) == TORIRS_COMPONENT_HOOK_COUNT,
    "hook name table must name every ToriRS_ComponentHookKind, in order");

int
ToriRS_ComponentHookIsSet(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind)
{
    assert(kind >= 0 && kind < TORIRS_COMPONENT_HOOK_COUNT);
    return component && component->hooks[kind] != NULL;
}

struct ToriRS_ScriptHook const*
ToriRS_ComponentHookPeek(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind)
{
    assert(kind >= 0 && kind < TORIRS_COMPONENT_HOOK_COUNT);
    return component ? component->hooks[kind] : NULL;
}

struct ToriRS_ScriptHook const*
ToriRS_ComponentHookGet(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind)
{
    assert(component);
    assert(kind >= 0 && kind < TORIRS_COMPONENT_HOOK_COUNT);
    assert(component->hooks[kind] && "component hook read before it was set");
    return component->hooks[kind];
}

struct ToriRS_ScriptHook*
ToriRS_ComponentHookInit(
    struct ToriRS_Component* component,
    enum ToriRS_ComponentHookKind kind)
{
    assert(component);
    assert(kind >= 0 && kind < TORIRS_COMPONENT_HOOK_COUNT);
    if( !component->hooks[kind] )
        component->hooks[kind] =
            (struct ToriRS_ScriptHook*)calloc(1, sizeof(struct ToriRS_ScriptHook));
    return component->hooks[kind];
}

void
ToriRS_ComponentHooksFree(struct ToriRS_Component* component)
{
    if( !component )
        return;
    for( int i = 0; i < TORIRS_COMPONENT_HOOK_COUNT; i++ )
    {
        free(component->hooks[i]);
        component->hooks[i] = NULL;
    }
}

int
ToriRS_ComponentHasAnyHook(struct ToriRS_Component const* component)
{
    assert(component);
    for( int i = 0; i < TORIRS_COMPONENT_HOOK_COUNT; i++ )
        if( component->hooks[i] )
            return 1;
    return 0;
}

char const*
ToriRS_ComponentHookName(enum ToriRS_ComponentHookKind kind)
{
    assert(kind >= 0 && kind < TORIRS_COMPONENT_HOOK_COUNT);
    return k_hook_names[kind];
}
