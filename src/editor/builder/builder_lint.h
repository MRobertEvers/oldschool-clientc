#ifndef SRC_EDITOR_BUILDER_BUILDER_LINT_H
#define SRC_EDITOR_BUILDER_BUILDER_LINT_H

/**
 * The dialogue's lint (design section 10, "Problems lists lint findings ...
 * with a click-to-node"): what compiles but ships wrong, one row per
 * finding, each naming the node (or the trigger step) it is about so a click
 * on the row selects it.
 *
 *   PLAYER_NAME   a literal player name in player-facing text: the U+00A0 a
 *                 recorded display name arrives with, or the session's own
 *                 account name (case-insensitive, a whole word) -- write
 *                 {player}
 *   ORPHAN        a node the start cannot reach: it is emitted as nothing
 *   ONE_ARM       a choice with fewer than two options (~p_choice2 is the
 *                 smallest the tree has)
 *   UNKNOWN_VAR   a var no varp / varbit record names (a branch, a quest
 *                 start, an if / set step)
 *   UNKNOWN_PROC  a CALL step's proc no [proc,<name>] declares
 *
 * Builder_Validate (builder_emit.h) is the emitter's own check -- what cannot
 * compile; these are the reader's. The two var / proc rows need the catalog
 * (struct Builder_LintNames' callbacks); without one they are not checked and
 * Builder_Lint says so in `unchecked`.
 */

#include "builder_model.h"

#define BUILDER_LINT_ROWS_MAX 64

enum Builder_LintKind
{
    BUILDER_LINT_PLAYER_NAME = 0,
    BUILDER_LINT_ORPHAN,
    BUILDER_LINT_ONE_ARM,
    BUILDER_LINT_UNKNOWN_VAR,
    BUILDER_LINT_UNKNOWN_PROC,
    BUILDER_LINT_KIND_COUNT
};

struct Builder_LintRow
{
    enum Builder_LintKind kind;
    /** 1 = the dialogue cannot work as written; 0 = a warning. */
    int error;
    /** What a click selects: a node (index), else a trigger's step. */
    int node;
    int trigger;
    int step;
    /** "node n4", "op1 step 2". */
    char where[64];
    char message[192];
};

struct Builder_LintRows
{
    struct Builder_LintRow rows[BUILDER_LINT_ROWS_MAX];
    int count;
    /** Rows past the cap, counted, not kept. */
    int dropped;
    /** Kinds not checked (bit 1 << kind): no callback to ask. */
    unsigned unchecked;
};

/** What the lint asks the tree. Each callback may be NULL. */
struct Builder_LintNames
{
    void* user_data;
    /** 1 when a varp or varbit record is named `name` (as the tree spells
     *  it: "varp29_...", "varb346_..."), 0 when none is, -1 when it cannot
     *  say (no catalog yet: the kind is then `unchecked`, not a row). */
    int (*var_known)(void* user_data, const char* name);
    /** The same for `[proc,<name>]` declared somewhere in the tree. */
    int (*proc_known)(void* user_data, const char* name);
    /** The session's account display name; NULL or "" for none. */
    const char* player_name;
};

/** "Literal player name", ... */
const char*
Builder_LintKindLabel(enum Builder_LintKind kind);

/** Lint `model` into `out` (cleared first). Returns out->count. */
int
Builder_Lint(
    const struct Builder_Model* model,
    const struct Builder_LintNames* names,
    struct Builder_LintRows* out);

#endif
