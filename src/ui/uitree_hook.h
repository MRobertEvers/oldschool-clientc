#ifndef SRC_UI_UITREE_HOOK_H
#define SRC_UI_UITREE_HOOK_H

#include <stdint.h>

/*
 * A component's runtime script hooks — the "when X happens, run script N with
 * these arguments" bindings, whether the cache declared them or a CS2
 * `if_seton*` armed them at runtime.
 *
 * ------------------------------------------------------------------
 * Why the arguments are on the heap
 * ------------------------------------------------------------------
 *
 * A slot used to be a flat 600-byte struct: `int argv[64]` and
 * `char strv[4][80]` inline, sized for the widest hook in the cache rather than
 * for the hook in hand. Twenty-two slots of that made the per-component block
 * **13,200 bytes**, allocated whole the moment a component acquired its first
 * hook. Measured across the rev-230 gameframe and all fourteen sidebar tabs:
 * 855 blocks, 11.7 MB, and an average of **two hooks set per component of the
 * twenty-two** — about a 9% fill, with 576 of every 600 bytes being argument
 * capacity nothing had asked for.
 *
 * So the tails are sized to what was actually passed. A slot is now 40 bytes,
 * a block 880, and a typical hook's arguments are a few dozen bytes beside it.
 *
 * The caps below survive as *validation bounds* — the widest `if_seton*` in
 * cache.osrs239 really does carry 44 arguments, so they are not arbitrary — but
 * they no longer decide what is allocated. Nothing outside this file may reach
 * into `argv`/`strv`: they are owned pointers with a lifetime, and the whole
 * point of the accessors is that an index is checked rather than assumed.
 */

/* Widest argument list a hook may carry. A bound, not an allocation size. */
#define UITREE_HOOK_ARG_MAX 64
/* Script 150's nine operation labels are the widest currently known hook;
 * keep headroom so valid signatures are not silently truncated. */
#define UITREE_HOOK_STR_ARG_MAX 16
/* Longest string argument kept, including the terminator. */
#define UITREE_HOOK_STR_ARG_LEN 80

struct UITreeRuntimeScriptHook
{
    /** The clientscript to run. <= 0 means "no hook here"; every other field is
     *  then zero and both tails are NULL. */
    int script_id;
    /** Number of ints in `argv`. */
    int argc;
    /** Number of strings in `strv`. */
    int str_argc;
    /** Bit i set = argument position i is a string; strings fill `strv` in
     *  position order (k-th set bit -> strv[k]) and `argv[i]` is unused there. */
    uint64_t str_mask;

    /* Owned tails, exactly `argc` / `str_argc` long, NULL when the count is 0.
     * Private — go through the accessors below. */
    int* argv;
    char** strv;
};

/*
 * The slots, in the order `UITree_HooksSlotAt` and `UITree_HooksSlotName`
 * report them. Adding one here means adding its name to the table in
 * uitree_hook.c; a static assert there keeps the two the same length, because
 * the positional walk is what the debug dump and the clear/keep policy both
 * read and a silent misalignment relabels every slot past the new one.
 */
struct UITreeRuntimeHooks
{
    struct UITreeRuntimeScriptHook on_click;
    struct UITreeRuntimeScriptHook on_hold;
    struct UITreeRuntimeScriptHook on_mouse_over;
    struct UITreeRuntimeScriptHook on_mouse_leave;
    struct UITreeRuntimeScriptHook on_mouse_repeat;
    struct UITreeRuntimeScriptHook on_click_repeat;
    struct UITreeRuntimeScriptHook on_release;
    /* BUTTON_TARGET selection lifecycle. Spellbook icons use these to swap
     * their outline when target mode is armed/cancelled. */
    struct UITreeRuntimeScriptHook on_target_enter;
    struct UITreeRuntimeScriptHook on_target_leave;
    struct UITreeRuntimeScriptHook on_drag;
    struct UITreeRuntimeScriptHook on_drag_complete;
    struct UITreeRuntimeScriptHook on_scroll_wheel;
    struct UITreeRuntimeScriptHook on_key;
    /* Key-DOWN and key-UP, distinct from on_key (which is "key typed" and
     * carries a character as well as a code). CS2 opcodes 1430/2430 and
     * 1431/2431 — the vendor opcode table calls those SETONITEMONITEM and
     * SETONCLANSETTINGS, but the rev-239 client hands them the pressed and
     * released key lists (class415, fields 4154 and 4060), one dispatch per
     * key code, and script6007 registers them as exactly that: the pair that
     * rebuilds the inventory when shift goes down and comes back up. */
    struct UITreeRuntimeScriptHook on_key_down;
    struct UITreeRuntimeScriptHook on_key_up;
    struct UITreeRuntimeScriptHook on_op;
    struct UITreeRuntimeScriptHook on_timer;
    struct UITreeRuntimeScriptHook on_var_transmit;
    struct UITreeRuntimeScriptHook on_inv_transmit;
    struct UITreeRuntimeScriptHook on_misc_transmit;
    /** CC/IF_SETONFRIENDTRANSMIT. Like on_misc_transmit it carries no trigger
     *  list: every registered hook re-runs when the friend store changes. */
    struct UITreeRuntimeScriptHook on_friend_transmit;
    /** CC/IF_SETONCHATTRANSMIT. No trigger list either: the reference bumps
     *  one chat stamp per message and every registered hook re-runs against
     *  it. The chatbox's own hook (`chat_onchattransmit`, registered by
     *  `[clientscript,chatbox_init]`) is what redraws the 500 line
     *  components -- the client writes none of them itself. */
    struct UITreeRuntimeScriptHook on_chat_transmit;
    struct UITreeRuntimeScriptHook on_dialog_abort;
    struct UITreeRuntimeScriptHook on_resize;
    struct UITreeRuntimeScriptHook on_sub_change;
};

/* ------------------------------------------------------------------ */
/* One slot                                                            */
/* ------------------------------------------------------------------ */

/** Is anything bound here? The one test callers should make before reading. */
int
UITree_HookIsSet(struct UITreeRuntimeScriptHook const* hook);

/** Argument `index`. Asserts `0 <= index < argc` — an out-of-range read is a
 *  caller bug, and it used to be a silent zero out of the padding. */
int
UITree_HookArg(
    struct UITreeRuntimeScriptHook const* hook,
    int index);

/** The argument block for a bulk read (`argc` ints), or NULL when argc == 0.
 *  For handing straight to a dispatch call; index with UITree_HookArg. */
int const*
UITree_HookArgs(struct UITreeRuntimeScriptHook const* hook);

/** String argument `index`. Asserts `0 <= index < str_argc`. */
char const*
UITree_HookStr(
    struct UITreeRuntimeScriptHook const* hook,
    int index);

/** Fill `out[0..cap)` with the string arguments, "" past `str_argc`. The
 *  dispatch tasks take a fixed-width array of pointers; this is that shape
 *  without the caller indexing a tail it does not own. */
void
UITree_HookStrArgv(
    struct UITreeRuntimeScriptHook const* hook,
    char const** out,
    int cap);

/** Bind `script_id` with these arguments, replacing whatever was here (and
 *  releasing its tails). `argc`/`str_argc` are clamped to the caps above.
 *  A `script_id <= 0` clears instead. */
void
UITree_HookSet(
    struct UITreeRuntimeScriptHook* hook,
    int script_id,
    int const* argv,
    int argc,
    uint64_t str_mask,
    char const* const* strv,
    int str_argc);

/** Release the tails and zero the slot. The ONLY correct way to blank one —
 *  a memset leaks both tails, which is why the raw struct is no longer safe to
 *  clear by hand. */
void
UITree_HookClear(struct UITreeRuntimeScriptHook* hook);

/** Deep copy into an UNINITIALISED slot — a local snapshot, typically. Zeroes
 *  `dst` first, so it never frees the caller's stack garbage; release it with
 *  UITree_HookClear when the snapshot goes out of scope. */
void
UITree_HookInitCopy(
    struct UITreeRuntimeScriptHook* dst,
    struct UITreeRuntimeScriptHook const* src);

/** Deep copy: `dst` ends up owning its own tails. Self-copy is safe. */
void
UITree_HookCopy(
    struct UITreeRuntimeScriptHook* dst,
    struct UITreeRuntimeScriptHook const* src);

/* ------------------------------------------------------------------ */
/* The block                                                           */
/* ------------------------------------------------------------------ */

/** A zeroed block, or NULL on OOM. */
struct UITreeRuntimeHooks*
UITree_HooksBlockNew(void);

/** Clear every slot (releasing tails) and free the block. NULL is a no-op. */
void
UITree_HooksBlockFree(struct UITreeRuntimeHooks* hooks);

/** Deep copy every slot. `dst` must be a live block; its old tails are freed. */
void
UITree_HooksBlockCopy(
    struct UITreeRuntimeHooks* dst,
    struct UITreeRuntimeHooks const* src);

/** How many slots a block holds. */
int
UITree_HooksSlotCount(void);

/** Slot `index` positionally, for a walk that must cover every kind. Asserts
 *  the range. */
struct UITreeRuntimeScriptHook*
UITree_HooksSlotAt(
    struct UITreeRuntimeHooks* hooks,
    int index);

struct UITreeRuntimeScriptHook const*
UITree_HooksSlotAtConst(
    struct UITreeRuntimeHooks const* hooks,
    int index);

/** The field name of slot `index` ("on_click", …), for diagnostics. */
char const*
UITree_HooksSlotName(int index);

#endif
