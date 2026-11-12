#ifndef SRC_EDITOR_EDITOR_SCRIPT_H
#define SRC_EDITOR_EDITOR_SCRIPT_H

/**
 * RuneScript triggers as the editor sees them (the Triggers panel, the
 * catalog's "Open in editor"): the pure half, no session, no chrome, so
 * editor_script_test can hold every rule. The scripts themselves are edited
 * in the user's own editor (editor_open_cmd.h).
 *
 *   triggers     every `[kind,subject]` header of a `.rs2` text with its
 *                1-based line -- the same rule the server's catalog indexes
 *                TRIGGER contributions by (torirs_maped_catalog.c
 *                cat_parse_rs2): a line that starts with '[' and has a ','
 *                before its ']'.
 *   kinds        the op / ap triggers a loc, npc or obj can carry, named the
 *                way the builders' emitter composes them (builder_emit.c:
 *                "op"/"ap" + "loc"/"npc" + 1..5 or "u") plus the obj's
 *                held-inventory kinds; and which record field each one's
 *                menu entry is (op<N>; ifop<N> for opheld<N>).
 *   template     the trigger a new hook starts as: a body sscompile accepts
 *                ("Nothing interesting happens." for an op / ap, the tree's
 *                default use-on message for a `u` kind -- the builders'
 *                fallbacks).
 *   add plan     "add trigger <kind> to <subject>": which field the record
 *                needs (when the menu entry is missing), which file the
 *                trigger goes into (the subject's edited script,
 *                server/scripts/edited/<s>/scripts/<s>.rs2, the builders'
 *                path), the text appended and the line its header lands on.
 *   diagnostics  sscompile's located errors (builder_ssc.h) mapped onto the
 *                lines of one document.
 *
 * Contract violations assert (CLAUDE.md).
 */

#include "editor/builder/builder_ssc.h"

#include <stddef.h>

#define EDITOR_SCRIPT_KIND_MAX 24
#define EDITOR_SCRIPT_SUBJECT_MAX 128
/** Every op / ap kind of the widest type (obj: 24). */
#define EDITOR_SCRIPT_KINDS_MAX 32
/** A relative script path ("edited/x/scripts/x.rs2"); the TEXT key is
 *  "rs2:" + it and must fit EDITOR_DOC_KEY_MAX (128). */
#define EDITOR_SCRIPT_PATH_MAX 124
#define EDITOR_SCRIPT_INSERT_MAX 512

struct Editor_ScriptTrigger
{
    char kind[EDITOR_SCRIPT_KIND_MAX];
    char subject[EDITOR_SCRIPT_SUBJECT_MAX];
    /** 1-based, as sscompile and the catalog count. */
    int line;
};

/**
 * The trigger headers of `text`, in order. Writes at most `max` (out may be
 * NULL with max 0) and returns how many the text has.
 */
int
Editor_ScriptParseTriggers(
    const char* text,
    size_t length,
    struct Editor_ScriptTrigger* out,
    int max);

/** The trigger kinds a record of `type` ("loc", "npc", "obj") can carry, in
 *  menu order (op1..5, opu, ap1..5, apu; then opheld1..5, opheldu for an
 *  obj). Writes at most `max` names (static strings); returns the count, 0
 *  for any other type. */
int
Editor_ScriptTriggerKinds(
    const char* type,
    const char** out,
    int max);

/** 1 when `kind` is one of `type`'s kinds. */
int
Editor_ScriptKindValid(
    const char* type,
    const char* kind);

/**
 * The record field holding the menu text the trigger answers: "op3" for
 * oploc3 / aploc3 / opnpc3 / apnpc3 / opobj3 / apobj3, "ifop3" for opheld3.
 * Returns 0 (and writes "") for a use-on kind, which has no menu entry.
 */
int
Editor_ScriptKindOpField(
    const char* kind,
    char* out,
    size_t out_size);

/** 1 when a resolved field value names a menu entry: not NULL, "",
 *  "default" or "null" (the cache dump's spellings of no entry). "hidden"
 *  counts as stated: the entry exists, the client hides it. */
int
Editor_ScriptOpStated(const char* value);

/** "edited/<subject>/scripts/<subject>.rs2" (Builder_FilePath's script). 0
 *  when it does not fit. */
int
Editor_ScriptEditedPath(
    const char* subject,
    char* out,
    size_t out_size);

/** A relative `.rs2` path the TEXT document "rs2:<path>" can be: no
 *  leading '/', no "..", no backslash, ends ".rs2", fits. */
int
Editor_ScriptPathValid(const char* rel_path);

/** The same rule for ANY file or folder under server/scripts -- a quest's
 *  `.varp` and `.constant`, the folder they are in: what Open in editor and
 *  Show in Explorer are handed. No trailing '/'. */
int
Editor_ScriptFileValid(const char* rel_path);

/** "server/scripts/<rel>.rs2" (a catalog source path) -> "<rel>.rs2".
 *  0 when it is not a script under server/scripts. */
int
Editor_ScriptRelFromContentPath(
    const char* content_path,
    char* out,
    size_t out_size);

/** `[kind,subject]` and its starter body, newline-terminated. Returns the
 *  length written, 0 when it does not fit. */
size_t
Editor_ScriptTemplate(
    const char* kind,
    const char* subject,
    char* out,
    size_t out_size);

enum Editor_ScriptPlanStatus
{
    EDITOR_SCRIPT_PLAN_OK = 0,
    /** The kind is not one of the type's (or the type is unknown). */
    EDITOR_SCRIPT_PLAN_BAD_KIND,
    /** The subject is not a record name ([a-z0-9_], fits). */
    EDITOR_SCRIPT_PLAN_BAD_SUBJECT,
    /** The edited script is a builder's output (its header): the builder
     *  regenerates it, so a hand-written trigger would be lost. */
    EDITOR_SCRIPT_PLAN_GENERATED,
    /** The edited script already declares this trigger. */
    EDITOR_SCRIPT_PLAN_DUPLICATE,
    /** The record has no such menu entry and no text was given for it. */
    EDITOR_SCRIPT_PLAN_NO_LABEL,
    /** The menu text cannot be a config value (empty after trim, '\n', too
     *  long) or the template does not fit one splice. */
    EDITOR_SCRIPT_PLAN_BAD_TEXT,
};

struct Editor_ScriptAddPlan
{
    /** "edited/<s>/scripts/<s>.rs2" and its TEXT key "rs2:" + that. */
    char rel_path[EDITOR_SCRIPT_PATH_MAX];
    char doc_key[EDITOR_SCRIPT_PATH_MAX + 8];
    /** The field to write ("op3", "ifop2"), "" when none is needed. */
    char field[16];
    char field_value[64];
    /** The splice: `insert` goes in at `offset` (the end of the text). */
    size_t offset;
    char insert[EDITOR_SCRIPT_INSERT_MAX];
    size_t insert_length;
    /** The new header's 1-based line in the text after the splice. */
    int header_line;
};

/**
 * Plan "add trigger `kind` to `subject` (a `type` record)".
 *   `op_value`   the record's resolved value of the kind's menu field (NULL
 *                when the record states it nowhere);
 *   `op_label`   the menu text to write when the entry is missing (may be
 *                NULL or "" when the record has it);
 *   `script`     the edited script's current text ("" for a new file).
 * Fills `out` and returns EDITOR_SCRIPT_PLAN_OK, else a status naming why.
 */
enum Editor_ScriptPlanStatus
Editor_ScriptPlanAddTrigger(
    const char* type,
    const char* subject,
    const char* kind,
    const char* op_value,
    const char* op_label,
    const char* script,
    size_t script_length,
    struct Editor_ScriptAddPlan* out);

/** What a status means, for the console and the panel. */
const char*
Editor_ScriptPlanStatusText(enum Editor_ScriptPlanStatus status);

/**
 * The 0-based lines of `rel_path` sscompile reported an ERROR on (warnings
 * left out), ascending, each once. Writes at most `max`; returns how many.
 */
int
Editor_ScriptDiagLines(
    const struct Builder_SscResult* result,
    const char* rel_path,
    int* out_lines,
    int max);

/** "file:line: message" ("warning: " before a warning's message). */
void
Editor_ScriptDiagFormat(
    const struct Builder_SscDiag* diag,
    char* out,
    size_t out_size);

#endif
