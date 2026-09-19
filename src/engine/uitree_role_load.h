#ifndef SRC_ENGINE_UITREE_ROLE_LOAD_H
#define SRC_ENGINE_UITREE_ROLE_LOAD_H

/*
 * Translating a revision profile's `[role:…]` sections into the table the tree
 * resolves against.
 *
 * The seam exists because neither side is allowed to know the other. revconfig
 * is a leaf: it hands on `iface(logout)` and `slot(chat_buttons, report)` as
 * the strings the INI said, because resolving either would mean including the
 * ui and the ref table. The tree is a leaf too, and takes only numbers. So the
 * names are turned into numbers exactly once, here, where both are in scope --
 * the same division `[features] era=` already uses, where revconfig carries the
 * spelling and the App resolves it.
 */

#include "ui/uitree_role.h"

struct RevConfigRefs;
struct RevConfigItemBuffer;

/**
 * Fold every `[role:…]` and every component `role=` in `items` into `table`.
 *
 * `refs` resolves the `iface(<name>)` half; it must already hold the profile's
 * `[iface:…]` sections, so load it first. A matcher naming an interface this
 * revision does not declare is DROPPED with a line on stderr -- the rung can
 * never resolve, and a chain quietly one rung shorter than the profile wrote
 * is how a role ends up answering with the wrong node.
 *
 * Additive: called once per source, in the same order as the ref table, so a
 * later source extends a chain rather than restating it.
 */
void
UITreeRoleLoad_AddItems(
    struct UITreeRoleTable* table,
    struct RevConfigItemBuffer const* items,
    struct RevConfigRefs const* refs);

/**
 * Load the three profile sources into `table`. Same paths and same order as
 * RevConfigRefs_LoadSources; `inline_ini` is read in the `revconfig:` dialect.
 */
void
UITreeRoleLoad_LoadSources(
    struct UITreeRoleTable* table,
    struct RevConfigRefs const* refs,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini);

#endif /* SRC_ENGINE_UITREE_ROLE_LOAD_H */
