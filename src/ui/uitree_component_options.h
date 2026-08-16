#ifndef SRC_UI_UITREE_COMPONENT_OPTIONS_H
#define SRC_UI_UITREE_COMPONENT_OPTIONS_H

struct UITreeComponent;
struct UITreeMenuOptions;
struct UITreeOpKeys;

/*
 * The two blocks a component only sometimes needs: its right-click menu options
 * and its op key bindings.
 *
 * Inline they were 888 + 480 = **1,368 of `UITreeComponent`'s 2,104 bytes, 65%**,
 * carried by all ~7,100 live nodes — roughly 9 MB — to hold text and keys that
 * only a small minority of components have. (The `submenus` block inside
 * `UITreeMenuOptions` was made lazy for exactly this reason already; this is the
 * same argument one level out.)
 *
 * Reading is NULL-free: the accessors hand back a shared zeroed singleton when
 * a component has no block, so `UITree_MenuOptions(c)->ops[i]` reads "" for a
 * component with no options, which is what every call site already expected.
 * Writing goes through the `Mut` accessors, which allocate. The `Has` tests are
 * for the walks that want to skip a node entirely.
 *
 * The singletons are `const` and shared. Never write through a `Mut` result you
 * obtained from a component you did not intend to give a block to — that is why
 * the read and write accessors are different functions rather than one.
 */

/** The component's menu options, or a shared empty block. Never NULL. */
struct UITreeMenuOptions const*
UITree_MenuOptions(struct UITreeComponent const* component);

/** Allocate the block if absent and return it for writing. NULL only on OOM. */
struct UITreeMenuOptions*
UITree_MenuOptionsMut(struct UITreeComponent* component);

/** Does this component carry a menu-options block at all? */
int
UITree_HasMenuOptions(struct UITreeComponent const* component);

/** Copy `src` onto the component, allocating only when `src` says something.
 *  Deep for the submenu block, which is owned per component. `src` may be NULL
 *  or empty, in which case the component's block is released. */
void
UITree_MenuOptionsSet(
    struct UITreeComponent* component,
    struct UITreeMenuOptions const* src);

/** Release the block (and the submenus it owns). */
void
UITree_MenuOptionsFree(struct UITreeComponent* component);

/** The component's op-key bindings, or a shared empty block. Never NULL. */
struct UITreeOpKeys const*
UITree_OpKeys(struct UITreeComponent const* component);

/** Allocate the bindings if absent and return them for writing. */
struct UITreeOpKeys*
UITree_OpKeysMut(struct UITreeComponent* component);

/** Does this component carry op-key bindings at all? Cheaper than reading
 *  `has_bindings` through the singleton, and the match pass runs it per node. */
int
UITree_HasOpKeys(struct UITreeComponent const* component);

/** Copy `src` onto the component, allocating only when it binds something. */
void
UITree_OpKeysSet(
    struct UITreeComponent* component,
    struct UITreeOpKeys const* src);

/** Release the bindings. */
void
UITree_OpKeysFree(struct UITreeComponent* component);

#endif
