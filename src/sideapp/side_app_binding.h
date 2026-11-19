#ifndef SRC_SIDEAPP_SIDE_APP_BINDING_H
#define SRC_SIDEAPP_SIDE_APP_BINDING_H

/*
 * The launch configuration's key bindings, by action NAME.
 *
 * A manifest declares `[action:<source>] t=<action> a=<args>` and binds keys
 * to the sources in `[debug:hotkeys] <key>=<source>`. The loader resolves each
 * key to the action its source names and writes one SideAppBinding per key.
 * The action is a name, not a number: the side-apps that answer the names
 * register when the client starts (sideapp/side_app.h), so the loader cannot
 * know which names exist. Every `t=` the manifest declared is also listed in
 * `declared`, bound or not, so a name no side-app answers still fails the
 * boot -- as it failed the load when the loader held a table of targets.
 *
 * Types only, so the loader and the io server link none of the registry.
 */

#include "input/torirs_input.h"

#define SIDE_APP_BINDING_MAX 64
#define SIDE_APP_DECLARED_MAX 64
#define SIDE_APP_NAME_CAP 64
#define SIDE_APP_ARGS_CAP 512

/** One key bound to one action, with the action's `a=` payload. */
struct SideAppBinding
{
    enum LibToriRS_KeyCode key;
    /** The action's name: the manifest's `t=`. */
    char action[SIDE_APP_NAME_CAP];
    char args[SIDE_APP_ARGS_CAP];
};

/** One `[action:<source>] t=<action>` the manifest declared. */
struct SideAppDeclaredAction
{
    /** The section's own name, for the message that names a bad `t=`. */
    char source[SIDE_APP_NAME_CAP];
    char action[SIDE_APP_NAME_CAP];
};

/** The whole table. Zero is empty: nothing bound, nothing declared. */
struct SideAppBindingTable
{
    struct SideAppBinding bindings[SIDE_APP_BINDING_MAX];
    int binding_count;
    struct SideAppDeclaredAction declared[SIDE_APP_DECLARED_MAX];
    int declared_count;
};

#endif
