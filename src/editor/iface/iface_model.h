#ifndef SRC_EDITOR_IFACE_IFACE_MODEL_H
#define SRC_EDITOR_IFACE_IFACE_MODEL_H

/**
 * The interface editor's model (docs/MAPED_DESIGN.html #cs2 "Interface
 * editor"): what the component TREE, the inspector and the structure and hook
 * tools show and build, over one IFACE document's text. Plain data — no
 * chrome, no App — so editor_iface_test drives it headless; iface_panels.c
 * draws it, app_iface_editor.c mounts it.
 *
 *   TREE       one node per `.if` block, children in child-id order (block
 *              order, which is child-id order when the compack is
 *              positional), expanded state kept by NAME across rebuilds so an
 *              edit that renumbers does not collapse the tree.
 *   INSPECTOR  the selected block's lines, grouped as ToriRS_Component (and
 *              the block) groups them (enum Editor_IfGroup), plus the keys of
 *              a group that are absent, for "add field".
 *   PLANS      edits as commands (editor_if.h), never as text: a field set or
 *              removal, add child (appended: the next child id, so nothing
 *              renumbers), delete (the subtree, cleared then removed, one
 *              stroke), reparent, move.
 *   HOOKS      the CS2 script catalog from `scripts/<name>.cs2` headers (id, name,
 *              typed arguments) and the hook value a pick builds, spelled as
 *              the `.if` grammar spells it (`i:` / `s:`, event placeholders).
 */

#include "editor/editor_cmd.h"
#include "editor/editor_if.h"

#include <stddef.h>
#include <stdint.h>

#define EDITOR_IFACE_NAME_MAX 64

struct EditorIfaceNode
{
    char name[EDITOR_IFACE_NAME_MAX];
    /** Child index of the parent, -1 for a root, -2 for a `layer=` the file
     *  does not resolve (another interface's id). */
    int parent;
    int depth;
    int type;
    int if3;
    int hidden;
    int expanded;
    int child_count;
};

struct EditorIfaceModel
{
    char doc_key[EDITOR_DOC_KEY_MAX];
    int interface_id;
    int count;
    /** Owned, `count` long. */
    struct EditorIfaceNode* nodes;
    /** Visible rows: node positions in tree order, collapsed subtrees left
     *  out. Owned. */
    int* rows;
    int row_count;
    /** Block position, -1 for none. Kept by name across rebuilds. */
    int selected;
    /** The compack is not positional: structure edits are refused (fields
     *  still edit). Set by the caller, which is who can read the compack. */
    int structure_locked;
    /** What the model was built from, to skip rebuilding an unchanged text. */
    uint64_t text_hash;
    size_t text_length;
    uint32_t generation;
};

void
EditorIfaceModel_Init(struct EditorIfaceModel* model);

/** NULL-tolerant. */
void
EditorIfaceModel_Free(struct EditorIfaceModel* model);

/**
 * Bring the model up to `text` (the IFACE document `doc_key`'s current
 * text). Rebuilds only when the text changed; keeps expansion and the
 * selection by block name. Returns 1 when it rebuilt.
 */
int
EditorIfaceModel_Sync(
    struct EditorIfaceModel* model,
    const char* doc_key,
    const char* text,
    size_t length);

/** Flip node `position`'s expansion and rebuild the visible rows. */
void
EditorIfaceModel_Toggle(
    struct EditorIfaceModel* model,
    int position);

/** Select `position` (-1 clears) and expand its ancestors so it shows. */
void
EditorIfaceModel_Select(
    struct EditorIfaceModel* model,
    int position);

/** The node for a full component id of this interface, or -1. */
int
EditorIfaceModel_FindComponent(
    const struct EditorIfaceModel* model,
    int component_id);

/** Full component id of node `position`. */
static inline int
EditorIfaceModel_ComponentId(
    const struct EditorIfaceModel* model,
    int position)
{
    return ((model->interface_id > 0 ? model->interface_id : 0) << 16) | position;
}

/* ---- inspector ----------------------------------------------------------- */

struct EditorIfaceField
{
    /** Vocabulary row (Editor_IfKeyAt). */
    int row;
    char key[EDITOR_CMD_NAME_MAX];
    /** Raw value (bytes after '='). */
    char raw[EDITOR_CMD_VALUE_MAX];
    int present;
    /** The value is longer than one IF_FIELD carries: shown cut, not editable. */
    int too_long;
};

/**
 * Block `position`'s lines in group `group`, canonical order; with
 * `with_absent`, then the group's absent non-indexed keys. Returns how many
 * were written (at most `max`).
 */
int
EditorIfaceModel_Fields(
    const char* text,
    size_t length,
    int position,
    int group,
    int with_absent,
    struct EditorIfaceField* out,
    int max);

/** The inspector's caption for a group. */
const char*
EditorIfaceModel_GroupName(int group);

/* ---- plans --------------------------------------------------------------- */

/** One IF_FIELD: `key` of the selected block to `raw` (NULL removes). */
int
EditorIfaceModel_PlanField(
    const struct EditorIfaceModel* model,
    const char* text,
    size_t length,
    int position,
    const char* key,
    const char* raw,
    struct Editor_Cmd* out);

/** A child appended under `parent` (-1: a root): IF_COMPONENT add at the
 *  next child id. Returns 0 when the structure is locked. */
int
EditorIfaceModel_PlanAddChild(
    const struct EditorIfaceModel* model,
    int parent,
    struct Editor_Cmd* out);

/** Delete `position` and its subtree: the commands, or -1 (locked, or a
 *  value too long to undo exactly). */
int
EditorIfaceModel_PlanDelete(
    const struct EditorIfaceModel* model,
    const char* text,
    size_t length,
    int position,
    struct Editor_Cmd* out,
    int max);

/** Reparent `position` under `parent` (-1 root), keeping its id. */
int
EditorIfaceModel_PlanReparent(
    const struct EditorIfaceModel* model,
    int position,
    int parent,
    struct Editor_Cmd* out);

/** Move `position` to child id `index` (renumbers the run between). */
int
EditorIfaceModel_PlanMove(
    const struct EditorIfaceModel* model,
    int position,
    int index,
    struct Editor_Cmd* out);

/* ---- the CS2 script catalog and hooks ------------------------------------ */

#define EDITOR_CS2_SCRIPT_NAME_MAX 96
#define EDITOR_CS2_ARGS_MAX 24
#define EDITOR_CS2_ARG_NAME_MAX 40

enum EditorCs2ArgKind
{
    /** int, boolean, obj, enum, ... — every kind the wire carries as an int. */
    EDITOR_CS2_ARG_INT = 0,
    EDITOR_CS2_ARG_STRING,
    /** A component: picked from the tree, carried as its full id. */
    EDITOR_CS2_ARG_COMPONENT,
    /** long: a hook argument cannot carry one. */
    EDITOR_CS2_ARG_LONG,
};

struct EditorCs2Script
{
    int id;
    /** 1 for `[clientscript,...]`, 0 for `[proc,...]`. */
    int clientscript;
    char name[EDITOR_CS2_SCRIPT_NAME_MAX];
    int arg_count;
    unsigned char arg_kind[EDITOR_CS2_ARGS_MAX];
    /** The argument's declared type and name, "int $op_int0". */
    char arg_decl[EDITOR_CS2_ARGS_MAX][EDITOR_CS2_ARG_NAME_MAX];
};

struct EditorCs2Catalog
{
    struct EditorCs2Script* scripts;
    int count;
    int capacity;
};

/**
 * Parse one `.cs2` source's header — `// <id>` then
 * `[clientscript|proc,<name>](<type> $<arg>, ...)[(<returns>)]` — into `out`.
 * Returns 0 when the text has no such header.
 */
int
EditorCs2_ParseHeader(
    const char* text,
    size_t length,
    struct EditorCs2Script* out);

/** Every `.cs2` header in `scripts_dir`, sorted by id. Returns the count. */
int
EditorCs2Catalog_Load(
    struct EditorCs2Catalog* catalog,
    const char* scripts_dir);

/** NULL-tolerant. */
void
EditorCs2Catalog_Free(struct EditorCs2Catalog* catalog);

const struct EditorCs2Script*
EditorCs2Catalog_Find(
    const struct EditorCs2Catalog* catalog,
    int id);

/**
 * A `--names` directory for `cs2 compile` under `out_dir`: every RuneStar
 * table from `runestar_dir` (linked; may be NULL) except `script-names.tsv`,
 * which is written from the catalog — the content tree names its scripts
 * (`openurl_raw_handle_event_5344`), RuneStar does not, and a `~call` to a
 * content name resolves only through it. Returns 1 when written.
 */
int
EditorCs2Catalog_WriteNamesDir(
    const struct EditorCs2Catalog* catalog,
    const char* runestar_dir,
    const char* out_dir);

/** Scripts whose name contains `text` (all when ""), clientscripts first;
 *  writes indices into `out`, returns how many. */
int
EditorCs2Catalog_Search(
    const struct EditorCs2Catalog* catalog,
    const char* text,
    int* out,
    int max);

/**
 * The hook for `script` with one value per argument, each as the user typed
 * it: an int, an event placeholder's name (`event_mousex`), a component as
 * `<interface>:<child>` or a full id, any text for a string. Returns 0 when a
 * value does not fit its argument.
 */
int
EditorCs2_BuildHook(
    const struct EditorCs2Script* script,
    const char* const* values,
    int value_count,
    struct Editor_IfHook* out);

/** "name(arg, ...)" for a hook's raw value, event placeholders by name and
 *  the script by name when `catalog` knows it. */
void
EditorCs2_DescribeHook(
    const struct EditorCs2Catalog* catalog,
    const char* raw,
    char* out,
    size_t out_size);

#endif
