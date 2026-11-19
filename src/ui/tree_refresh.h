#ifndef SRC_UI_TREE_REFRESH_H
#define SRC_UI_TREE_REFRESH_H

/*
 * A one-shot request that the UI tree be refreshed once: async work changed
 * the tree (a boot bake finished, a CS2 settle reached its fixed point, a
 * checkpoint was restored), so the frame lays it out again, re-evaluates its
 * CS1 conditions and redraws.
 *
 * Raising it twice before it is taken is one refresh. Taking it clears it, so
 * a request is answered by exactly one frame.
 *
 * This does not lay anything out, evaluate anything or draw. The caller takes
 * the request where its frame polls async completions and does the refresh
 * itself.
 */

struct UITreeRefresh
{
    /** Raised and not yet taken. */
    int pending;
};

/** Ask for one refresh. */
void
UITreeRefresh_Raise(struct UITreeRefresh* refresh);

/** Is a refresh asked for and not yet taken? Does not take it. */
int
UITreeRefresh_Pending(struct UITreeRefresh const* refresh);

/** Take the request: non-zero once per raise, clearing it. */
int
UITreeRefresh_Take(struct UITreeRefresh* refresh);

#endif
