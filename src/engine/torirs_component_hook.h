#ifndef SRC_ENGINE_TORIRS_COMPONENT_HOOK_H
#define SRC_ENGINE_TORIRS_COMPONENT_HOOK_H

#include <stdint.h>

struct ToriRS_Component;
struct ToriRS_ScriptHook;

/*
 * The script hooks a decoded cache component carries, lazily.
 *
 * `struct ToriRS_Component` used to hold all eighteen inline. A
 * `ToriRS_ScriptHook` is 600 bytes (`argv[64]` + `strv[4][80]`, sized for the
 * widest hook in the cache), so eighteen of them were **7,800 of the struct's
 * 15,312 bytes — 51%** — carried by every component whether or not it declared
 * a single one, and most declare none.
 *
 * Now the component holds one pointer per kind and a hook is allocated the
 * first time something writes it. A component with no hooks pays the pointer
 * table; one with the usual one or two pays that plus what it uses.
 *
 * Reading goes through the accessors on purpose: `Peek` for "is there one",
 * `Get` when the caller has already established there is (it asserts). The old
 * shape let `component->on_load.argc` read a zeroed inline struct whether or
 * not the cache had said anything, so "no hook" and "hook with no arguments"
 * were the same value and nothing could tell them apart.
 */

enum ToriRS_ComponentHookKind
{
    TORIRS_COMPONENT_HOOK_LOAD = 0,
    TORIRS_COMPONENT_HOOK_CLICK,
    TORIRS_COMPONENT_HOOK_OP,
    TORIRS_COMPONENT_HOOK_MOUSE_OVER,
    TORIRS_COMPONENT_HOOK_MOUSE_LEAVE,
    TORIRS_COMPONENT_HOOK_DRAG,
    TORIRS_COMPONENT_HOOK_DRAG_COMPLETE,
    TORIRS_COMPONENT_HOOK_HOLD,
    TORIRS_COMPONENT_HOOK_MOUSE_REPEAT,
    TORIRS_COMPONENT_HOOK_SCROLL_WHEEL,
    TORIRS_COMPONENT_HOOK_TIMER,
    TORIRS_COMPONENT_HOOK_CLICK_REPEAT,
    TORIRS_COMPONENT_HOOK_RELEASE,
    TORIRS_COMPONENT_HOOK_TARGET_ENTER,
    TORIRS_COMPONENT_HOOK_TARGET_LEAVE,
    TORIRS_COMPONENT_HOOK_VARP_TRANSMIT,
    TORIRS_COMPONENT_HOOK_INV_TRANSMIT,
    TORIRS_COMPONENT_HOOK_STAT_TRANSMIT,
    TORIRS_COMPONENT_HOOK_COUNT
};

/** Is this kind present on the component? The one question to ask first. */
int
ToriRS_ComponentHookIsSet(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind);

/** The hook, or NULL when the component does not declare this kind. */
struct ToriRS_ScriptHook const*
ToriRS_ComponentHookPeek(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind);

/** The hook, asserting it is present — for a caller that has already tested
 *  `IsSet` (or reached here because `Peek` was non-NULL). */
struct ToriRS_ScriptHook const*
ToriRS_ComponentHookGet(
    struct ToriRS_Component const* component,
    enum ToriRS_ComponentHookKind kind);

/** Allocate this kind (zeroed) if absent and return it for the caller to fill.
 *  NULL only on OOM. The single write path — the decoder's. */
struct ToriRS_ScriptHook*
ToriRS_ComponentHookInit(
    struct ToriRS_Component* component,
    enum ToriRS_ComponentHookKind kind);

/** Release every hook the component allocated. */
void
ToriRS_ComponentHooksFree(struct ToriRS_Component* component);

/** Does the component declare ANY hook? What decides `script_kind`, and what a
 *  "does this need the CS2 path at all" test wants. */
int
ToriRS_ComponentHasAnyHook(struct ToriRS_Component const* component);

/** Field name of `kind` ("on_load", …), for diagnostics. */
char const*
ToriRS_ComponentHookName(enum ToriRS_ComponentHookKind kind);

#endif
