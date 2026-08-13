#include "ui/uitree_hook.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Slot names, positionally. Must match `struct UITreeRuntimeHooks` field for
 * field — the static assert below only catches a length mismatch, which is the
 * failure that has actually happened (four hooks added to the struct and not to
 * the table, silently relabelling everything past on_mouse_repeat, so an
 * on_target_enter read back as "on_scroll_wheel" and the spellbook looked wired
 * when it was not).
 */
static char const* const k_slot_names[] = {
    "on_click",           "on_hold",
    "on_mouse_over",      "on_mouse_leave",
    "on_mouse_repeat",    "on_click_repeat",
    "on_release",         "on_target_enter",
    "on_target_leave",    "on_drag",
    "on_drag_complete",   "on_scroll_wheel",
    "on_key",             "on_op",
    "on_timer",           "on_var_transmit",
    "on_inv_transmit",    "on_misc_transmit",
    "on_friend_transmit", "on_dialog_abort",
    "on_resize",          "on_sub_change",
};

#define UITREE_HOOK_SLOT_COUNT ((int)(sizeof(k_slot_names) / sizeof(k_slot_names[0])))

_Static_assert(
    sizeof(struct UITreeRuntimeHooks) ==
        (size_t)UITREE_HOOK_SLOT_COUNT * sizeof(struct UITreeRuntimeScriptHook),
    "the slot-name table must name every UITreeRuntimeHooks slot, in order");

/* The block is walked positionally in several places, which is only legal
 * because every member has the same type and the struct has no padding between
 * them. Both are true by construction; assert the consequence rather than
 * trusting it. */
_Static_assert(
    sizeof(struct UITreeRuntimeHooks) % sizeof(struct UITreeRuntimeScriptHook) == 0,
    "UITreeRuntimeHooks must be a dense array of slots");

/* ------------------------------------------------------------------ */
/* One slot                                                            */
/* ------------------------------------------------------------------ */

int
UITree_HookIsSet(struct UITreeRuntimeScriptHook const* hook)
{
    return hook && hook->script_id > 0;
}

int
UITree_HookArg(
    struct UITreeRuntimeScriptHook const* hook,
    int index)
{
    assert(hook);
    assert(index >= 0 && index < hook->argc && "hook argument index out of range");
    assert(hook->argv);
    return hook->argv[index];
}

int const*
UITree_HookArgs(struct UITreeRuntimeScriptHook const* hook)
{
    assert(hook);
    return hook->argc > 0 ? hook->argv : NULL;
}

char const*
UITree_HookStr(
    struct UITreeRuntimeScriptHook const* hook,
    int index)
{
    assert(hook);
    assert(index >= 0 && index < hook->str_argc && "hook string index out of range");
    assert(hook->strv && hook->strv[index]);
    return hook->strv[index];
}

void
UITree_HookStrArgv(
    struct UITreeRuntimeScriptHook const* hook,
    char const** out,
    int cap)
{
    assert(hook);
    assert(out);
    for( int i = 0; i < cap; i++ )
        out[i] = (i < hook->str_argc && hook->strv && hook->strv[i]) ? hook->strv[i] : "";
}

void
UITree_HookClear(struct UITreeRuntimeScriptHook* hook)
{
    if( !hook )
        return;
    free(hook->argv);
    if( hook->strv )
    {
        for( int i = 0; i < hook->str_argc; i++ )
            free(hook->strv[i]);
        free(hook->strv);
    }
    memset(hook, 0, sizeof(*hook));
}

/** A bounded copy of `s` into a fresh allocation; NULL becomes "". */
static char*
hook_strdup(char const* s)
{
    size_t len;
    char* out;

    if( !s )
        s = "";
    len = strlen(s);
    if( len > UITREE_HOOK_STR_ARG_LEN - 1 )
        len = UITREE_HOOK_STR_ARG_LEN - 1;
    out = (char*)malloc(len + 1);
    if( !out )
        return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

void
UITree_HookSet(
    struct UITreeRuntimeScriptHook* hook,
    int script_id,
    int const* argv,
    int argc,
    uint64_t str_mask,
    char const* const* strv,
    int str_argc)
{
    assert(hook);

    UITree_HookClear(hook);
    if( script_id <= 0 )
        return;

    if( argc > UITREE_HOOK_ARG_MAX )
        argc = UITREE_HOOK_ARG_MAX;
    if( argc < 0 || !argv )
        argc = 0;
    if( str_argc > UITREE_HOOK_STR_ARG_MAX )
        str_argc = UITREE_HOOK_STR_ARG_MAX;
    if( str_argc < 0 || !strv )
        str_argc = 0;

    hook->script_id = script_id;
    hook->str_mask = str_mask;

    if( argc > 0 )
    {
        hook->argv = (int*)malloc((size_t)argc * sizeof(int));
        if( hook->argv )
        {
            memcpy(hook->argv, argv, (size_t)argc * sizeof(int));
            hook->argc = argc;
        }
    }

    if( str_argc > 0 )
    {
        hook->strv = (char**)calloc((size_t)str_argc, sizeof(char*));
        if( hook->strv )
        {
            /* str_argc is raised as each string lands, so a mid-way OOM leaves a
             * slot whose count matches what was actually allocated — the free
             * path walks that count and must not run past it. */
            for( int i = 0; i < str_argc; i++ )
            {
                char* copy = hook_strdup(strv[i]);
                if( !copy )
                    break;
                hook->strv[i] = copy;
                hook->str_argc = i + 1;
            }
        }
    }
}

void
UITree_HookCopy(
    struct UITreeRuntimeScriptHook* dst,
    struct UITreeRuntimeScriptHook const* src)
{
    assert(dst);
    assert(src);
    if( dst == src )
        return;
    UITree_HookSet(
        dst,
        src->script_id,
        src->argv,
        src->argc,
        src->str_mask,
        (char const* const*)src->strv,
        src->str_argc);
}

/* ------------------------------------------------------------------ */
/* The block                                                           */
/* ------------------------------------------------------------------ */

struct UITreeRuntimeHooks*
UITree_HooksBlockNew(void)
{
    return (struct UITreeRuntimeHooks*)calloc(1, sizeof(struct UITreeRuntimeHooks));
}

void
UITree_HooksBlockFree(struct UITreeRuntimeHooks* hooks)
{
    if( !hooks )
        return;
    for( int i = 0; i < UITREE_HOOK_SLOT_COUNT; i++ )
        UITree_HookClear(UITree_HooksSlotAt(hooks, i));
    free(hooks);
}

void
UITree_HooksBlockCopy(
    struct UITreeRuntimeHooks* dst,
    struct UITreeRuntimeHooks const* src)
{
    assert(dst);
    assert(src);
    if( dst == src )
        return;
    for( int i = 0; i < UITREE_HOOK_SLOT_COUNT; i++ )
        UITree_HookCopy(UITree_HooksSlotAt(dst, i), UITree_HooksSlotAtConst(src, i));
}

int
UITree_HooksSlotCount(void)
{
    return UITREE_HOOK_SLOT_COUNT;
}

struct UITreeRuntimeScriptHook*
UITree_HooksSlotAt(
    struct UITreeRuntimeHooks* hooks,
    int index)
{
    assert(hooks);
    assert(index >= 0 && index < UITREE_HOOK_SLOT_COUNT && "hook slot index out of range");
    return (struct UITreeRuntimeScriptHook*)hooks + index;
}

struct UITreeRuntimeScriptHook const*
UITree_HooksSlotAtConst(
    struct UITreeRuntimeHooks const* hooks,
    int index)
{
    assert(hooks);
    assert(index >= 0 && index < UITREE_HOOK_SLOT_COUNT && "hook slot index out of range");
    return (struct UITreeRuntimeScriptHook const*)hooks + index;
}

char const*
UITree_HooksSlotName(int index)
{
    assert(index >= 0 && index < UITREE_HOOK_SLOT_COUNT && "hook slot index out of range");
    return k_slot_names[index];
}
