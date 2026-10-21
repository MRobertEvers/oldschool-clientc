#ifndef SRC_UI_UITREE_IF_STORE_H
#define SRC_UI_UITREE_IF_STORE_H

/**
 * The persistent IF_SET* stores: what the server said about a component,
 * remembered until there is a node to say it to.
 *
 * The server sends IF_SETTEXT, IF_SETHIDE and IF_SETCOLOUR for components of
 * an interface that may not be mounted yet -- the journal and bonus texts of a
 * quest panel arrive before the panel does -- and the reference keeps them on
 * the shared IfType.list for exactly that reason. So does this: the value is
 * stored, applied now if the node exists, and RE-applied whenever the tree's
 * generation moves, because a mount or a bake replaces the nodes underneath.
 *
 * Three stores, one shape: keyed by component id, one value per component,
 * last write wins. Two of the three carry an int and share a type; the text
 * store owns its strings, which is the only difference that matters and the
 * reason it is separate rather than an int store holding pointers.
 *
 * Keyed rather than appended because a component's value is a STATE, not an
 * event. Appending would re-apply an old text after a new one on the next
 * generation bump, which is the sort of thing that shows as a quest panel
 * reverting one line to what it said an hour ago.
 */

#include <stdbool.h>

struct UIIfIntEntry
{
    int com_id;
    int value;
};

/** IF_SETHIDE and IF_SETCOLOUR. */
struct UIIfIntStore
{
    struct UIIfIntEntry* entries;
    int count;
    int cap;
    /** Tree generation these were last applied against; a mismatch means the
     *  nodes have been replaced and every entry has to be applied again. */
    unsigned int applied_generation;
};

/** Store `value` for `com_id`, replacing whatever was there. */
void
UIIfIntStore_Set(
    struct UIIfIntStore* store,
    int com_id,
    int value);

/**
 * Replace the value of a component the store ALREADY holds; false, and nothing
 * stored, when it holds none.
 *
 * For a client script's write to the same field. On the reference IF_SETHIDE
 * and `if_sethide` set one `IfType.list` field, so whichever came last is what
 * shows. Here the script writes the node and the store keeps the server's
 * value, and a generation bump re-applies the stale one over it: the Fractured
 * Archive's fade hid `hpbar_hud:hp`, `hp_hud_open` showed it, and the next
 * mount anywhere hid the boss bar again. A component the server never set has
 * nothing to re-apply, so nothing is added for it.
 */
bool
UIIfIntStore_Update(
    struct UIIfIntStore* store,
    int com_id,
    int value);

/** The stored value, or `fallback` when the component has none. */
int
UIIfIntStore_Get(
    struct UIIfIntStore const* store,
    int com_id,
    int fallback);

void
UIIfIntStore_Free(struct UIIfIntStore* store);

struct UIIfTextEntry
{
    int com_id;
    /** Owned. Never NULL for a live entry -- an unset text is the empty
     *  string, because that is what the server means by clearing one. */
    char* text;
};

/** IF_SETTEXT. */
struct UIIfTextStore
{
    struct UIIfTextEntry* entries;
    int count;
    int cap;
    unsigned int applied_generation;
};

/**
 * Store `text` for `com_id`, replacing and freeing whatever was there.
 *
 * NULL is stored as the empty string rather than as an absent entry: the
 * server clearing a caption is a value, and forgetting the entry would let the
 * cache's authored text come back on the next generation bump.
 */
void
UIIfTextStore_Set(
    struct UIIfTextStore* store,
    int com_id,
    char const* text);

/** UIIfIntStore_Update's twin: a script's `if_settext` over a server text. */
bool
UIIfTextStore_Update(
    struct UIIfTextStore* store,
    int com_id,
    char const* text);

/** The stored text, or NULL when the component has none. */
char const*
UIIfTextStore_Get(
    struct UIIfTextStore const* store,
    int com_id);

/** Release the entries AND every string they own. */
void
UIIfTextStore_Free(struct UIIfTextStore* store);

#endif /* SRC_UI_UITREE_IF_STORE_H */
