#ifndef SRC_EDITOR_BUILDER_BUILDER_SHELL_H
#define SRC_EDITOR_BUILDER_BUILDER_SHELL_H

/**
 * The multiloc / multinpc shell editor's model (docs/MAPED_DESIGN.html
 * #locbuilder "Multilocs", #npcbuilder "Multinpcs").
 *
 * A shell is a config record whose child is chosen by a var:
 *
 *   multivarbit=<varbit> | multivarp=<varp>   (the other one -1 / default)
 *   multiloc1=<child>                         (multinpc1.. for an npc)
 *   ...
 *   multilocN=<child>
 *
 * 1-based in the text: slot N is shown for var value N-1, a child of -1 is
 * HIDDEN for that value, and the LAST slot is the default for every value
 * past the list (and for a negative one) -- VarPManager_ResolveTransform's
 * rule, which Builder_ShellResolve repeats. A slot number the text skips is a
 * hidden slot too. A loc's `multidefaultnone=yes` (opcode 92 with no default
 * loc) is carried, never invented.
 *
 * The shell is read from a CFG document's text (the record's block of
 * configs/all.<loc|npc>) and written back as CFG_FIELD commands
 * (Builder_ShellCommands) -- only the fields that differ, each placed where
 * the slot belongs, so the record's other lines and an undo are byte-exact.
 *
 * PREVIEW (design §13 "Preview value"): TORIRSMAPED_STATE_VAR_PREVIEW holds
 * the var values every view resolves shells against. Layout, owned here
 * because the shell editor is what sets it:
 *
 *   values[0]           n, the number of entries (0..5)
 *   values[1 + 3i + 0]  BUILDER_PREVIEW_VARP or BUILDER_PREVIEW_VARBIT
 *   values[1 + 3i + 1]  the var's id
 *   values[1 + 3i + 2]  the value to resolve with
 *
 * 16 values, the state field's limit. A view that resolves a shell keyed on
 * a var listed here uses the listed value instead of its own varp table.
 */

#include "builder_model.h"
#include "editor/editor_cmd.h"

#include <stdint.h>

/** The tree's longest table is 257 slots (an 8-bit varbit plus the default). */
#define BUILDER_SHELL_SLOTS_MAX 520

struct Builder_ShellSlot
{
    /** The child record, or "-1" for hidden. */
    char child[BUILDER_NAME_MAX];
};

enum Builder_ShellVar
{
    BUILDER_SHELL_VAR_NONE = 0,
    BUILDER_SHELL_VAR_VARP,
    BUILDER_SHELL_VAR_VARBIT,
};

struct Builder_Shell
{
    enum Builder_Subject subject;
    char name[BUILDER_NAME_MAX];
    enum Builder_ShellVar var_kind;
    char var[BUILDER_NAME_MAX];
    struct Builder_ShellSlot slots[BUILDER_SHELL_SLOTS_MAX];
    int slot_count;
    /** Loc only: multidefaultnone=yes. */
    int default_none;
    /** The text states a slot past BUILDER_SHELL_SLOTS_MAX: the shell is
     *  shown but not edited (Builder_ShellCommands writes nothing). */
    int overflow;
};

/**
 * Read the shell out of a record's block text. Returns 1 when the record is
 * a shell (it states a var and at least one slot), 0 when it is a plain
 * record -- `out` then holds the name and no slots, ready to become one.
 */
int
Builder_ShellParse(
    struct Builder_Shell* out,
    enum Builder_Subject subject,
    const char* block_text,
    size_t length);

/** The slot a var value selects (0-based), -1 when the shell has none. */
int
Builder_ShellResolve(
    const struct Builder_Shell* shell,
    int value);

/** The child a value shows, or NULL when that value hides the shell. */
const char*
Builder_ShellResolveChild(
    const struct Builder_Shell* shell,
    int value);

/** 1 for a hidden slot. */
int
Builder_ShellSlotHidden(const struct Builder_ShellSlot* slot);

/** What the table's value column says for slot `index`: "0", "1", ... and
 *  "default (6+)" for the last. */
void
Builder_ShellSlotValueLabel(
    const struct Builder_Shell* shell,
    int index,
    char* out,
    size_t out_size);

/**
 * The visibility summary the builder shows instead of making the user count
 * ("visible at 0-5, hidden from 6"; "hidden at 0, visible from 1"). Runs over
 * the listed values with the default slot standing for "from N".
 */
void
Builder_ShellSummary(
    const struct Builder_Shell* shell,
    char* out,
    size_t out_size);

/** Insert a slot at `index` (0..slot_count), shifting later values up by
 *  one. Returns 0 when full. */
int
Builder_ShellInsertSlot(
    struct Builder_Shell* shell,
    int index,
    const char* child);

void
Builder_ShellRemoveSlot(
    struct Builder_Shell* shell,
    int index);

/**
 * The CFG_FIELD commands that turn the record `block_text` (as Parse read
 * it) into `shell`, for document (CFG, doc_key). Each command is valid for
 * the text the earlier ones leave, so the list is one stroke applied in
 * order, and its inverse in reverse order restores the text byte for byte.
 * Writes at most `max`; returns how many are needed (0: no change, or a
 * record whose table overflows the editor, which it never rewrites).
 */
int
Builder_ShellCommands(
    const char* doc_key,
    const char* block_text,
    size_t length,
    const struct Builder_Shell* shell,
    struct Editor_Cmd* out,
    int max);

/* ---- preview ------------------------------------------------------------- */

enum
{
    BUILDER_PREVIEW_VARP = 1,
    BUILDER_PREVIEW_VARBIT = 2,
    BUILDER_PREVIEW_MAX = 5,
};

struct Builder_PreviewVar
{
    int kind;
    int id;
    int value;
};

/** The id a tree-spelled var name carries ("varb346_golem_a" -> 346,
 *  kind VARBIT), or 0 when the name is not prefixed. */
int
Builder_VarNameId(
    const char* name,
    int* out_kind,
    int* out_id);

/** Encode into STATE_VAR_PREVIEW values; returns the value count. */
int
Builder_PreviewEncode(
    const struct Builder_PreviewVar* vars,
    int count,
    int32_t* out_values,
    int max_values);

/** Decode; returns the entry count (0 for a malformed field). */
int
Builder_PreviewDecode(
    const int32_t* values,
    int value_count,
    struct Builder_PreviewVar* out_vars,
    int max_vars);

/** Set (or replace) one var's preview value in a decoded list. Returns the
 *  new count; a full list drops its oldest entry. */
int
Builder_PreviewSet(
    struct Builder_PreviewVar* vars,
    int count,
    int kind,
    int id,
    int value);

#endif
