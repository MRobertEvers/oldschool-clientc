#ifndef SRC_EDITOR_IFACE_IFACE_CS2_H
#define SRC_EDITOR_IFACE_IFACE_CS2_H

/**
 * The CS2 half of the interface editor (docs/MAPED_DESIGN.html #cs2 "CS2
 * runner"): compile, trace, watch, the `.cs2` CODE editor and the harness
 * case writer. No chrome and no App — iface_panels.c draws these,
 * app_iface_editor.c feeds the watch values and runs scripts.
 *
 *   COMPILE  `3rd/rscache/tools/cs2/cs2 compile` on the edited source, in a
 *            scratch directory of the caller's choosing (never the content
 *            tree): every line it prints is kept, and every `FAIL <file>:`
 *            line — a spelling the compiler declined — is listed as a
 *            decline, so nothing turns into a silent `.cs2b`. A clean compile
 *            leaves the encoded script, for a session hot-swap.
 *   TRACE    CS2VM2_TraceCaptureBegin/End (the same capture TORIRS_CS2_TRACE's
 *            harness uses), one line per executed op: script, pc, opcode by
 *            name, operand, stack depths.
 *   WATCH    varp / varbit / varc rows; the app reads the live value each
 *            frame and the panel shows it with a set control.
 *   CODE     a `cs2:<name>` TEXT document as lines; a CODE widget's REPLACE /
 *            SPLIT / JOIN become TEXT_SPLICE commands that name the bytes they
 *            replace; the visible lines get highlight spans.
 */

#include "editor/editor_cmd.h"

#include <stddef.h>
#include <stdint.h>

struct CS2VM2_TraceRecord;

/* ---- compile ------------------------------------------------------------- */

#define EDITOR_CS2_COMPILE_LINE_MAX 256
#define EDITOR_CS2_COMPILE_LINES 128

struct EditorCs2CompileJob
{
    /** The compiler binary ($(CS2_BIN)). */
    const char* cs2_bin;
    /** --cache: the cache the param types and db columns come from. */
    const char* cache_dir;
    /** --names (RuneStar's), or NULL. */
    const char* names_dir;
    /** --rev, NULL for "osrs241". */
    const char* revision;
    /** A scratch directory this compile owns; `src/` and `out/` go under it. */
    const char* work_dir;
    /** The script's file name without `.cs2`. */
    const char* script_name;
    const char* source;
    size_t source_length;
};

enum EditorCs2CompileStatus
{
    EDITOR_CS2_COMPILED = 0,
    /** The compiler ran and declined something (each in `declines`). */
    EDITOR_CS2_DECLINED,
    /** It could not run at all (no binary, no spawn on this platform). */
    EDITOR_CS2_NOT_RUN,
};

struct EditorCs2CompileResult
{
    enum EditorCs2CompileStatus status;
    int compiled;
    int failed;
    int exit_code;
    /** The id the header names (`// <id>`), -1 when it names none. */
    int script_id;
    /** Every line the compiler printed. */
    char lines[EDITOR_CS2_COMPILE_LINES][EDITOR_CS2_COMPILE_LINE_MAX];
    int line_count;
    /** Indices into `lines` of the `FAIL` lines. */
    int declines[EDITOR_CS2_COMPILE_LINES];
    int decline_count;
    /** The encoded script (`out/<id>`) on success, else "". */
    char output_path[1024];
};

/**
 * Run the compiler on `job`. Heap-allocate the result (it is ~33 KB).
 * Returns the status (also in `result->status`).
 */
enum EditorCs2CompileStatus
EditorCs2_Compile(
    const struct EditorCs2CompileJob* job,
    struct EditorCs2CompileResult* result);

/** Parse one line of compiler output into `result` (exposed for the test). */
void
EditorCs2_CompileTakeLine(
    struct EditorCs2CompileResult* result,
    const char* line);

/* ---- trace --------------------------------------------------------------- */

struct EditorCs2Trace
{
    /** Owned, `capacity` records. */
    struct CS2VM2_TraceRecord* records;
    int capacity;
    int count;
    int capturing;
};

void
EditorCs2Trace_Init(
    struct EditorCs2Trace* trace,
    int capacity);

/** NULL-tolerant. */
void
EditorCs2Trace_Free(struct EditorCs2Trace* trace);

/** Start capturing the VM's next instructions (stops any earlier capture). */
void
EditorCs2Trace_Begin(struct EditorCs2Trace* trace);

/** Stop; returns how many were captured. */
int
EditorCs2Trace_End(struct EditorCs2Trace* trace);

/** Record `index` as one panel line. */
void
EditorCs2Trace_Format(
    const struct EditorCs2Trace* trace,
    int index,
    char* out,
    size_t out_size);

/* ---- watch --------------------------------------------------------------- */

enum EditorCs2WatchKind
{
    EDITOR_CS2_WATCH_VARP = 0,
    EDITOR_CS2_WATCH_VARBIT,
    EDITOR_CS2_WATCH_VARC,
    EDITOR_CS2_WATCH_VARCSTR,
};

#define EDITOR_CS2_WATCH_MAX 32

struct EditorCs2WatchRow
{
    int kind;
    int id;
    int value;
    char text[64];
    /** Frames since the value last changed (0 = this frame). */
    int age;
};

struct EditorCs2Watch
{
    struct EditorCs2WatchRow rows[EDITOR_CS2_WATCH_MAX];
    int count;
};

/**
 * `varp 173`, `varbit 4153`, `varc 51`, `varcstr 335` (also `vp`, `vb`,
 * `vc`, `vcs`). Returns 0 when the text is none of those.
 */
int
EditorCs2Watch_Parse(
    const char* text,
    int* out_kind,
    int* out_id);

/** Add a row; returns its index, or -1 when full or already watched. */
int
EditorCs2Watch_Add(
    struct EditorCs2Watch* watch,
    int kind,
    int id);

void
EditorCs2Watch_Remove(
    struct EditorCs2Watch* watch,
    int index);

/** A fresh reading for row `index` (`text` for VARCSTR, else NULL). */
void
EditorCs2Watch_Set(
    struct EditorCs2Watch* watch,
    int index,
    int value,
    const char* text);

void
EditorCs2Watch_Format(
    const struct EditorCs2Watch* watch,
    int index,
    char* out,
    size_t out_size);

/* ---- the CODE editor ----------------------------------------------------- */

struct EditorCs2Code
{
    char doc_key[EDITOR_DOC_KEY_MAX];
    /** Line starts in the text it was built from; owned. line_count + 1
     *  entries, the last being the text's length. */
    size_t* starts;
    int line_count;
    int capacity;
};

void
EditorCs2Code_Init(struct EditorCs2Code* code);

/** NULL-tolerant. */
void
EditorCs2Code_Free(struct EditorCs2Code* code);

/** Index the lines of the TEXT document `doc_key`'s current `text`. */
void
EditorCs2Code_Sync(
    struct EditorCs2Code* code,
    const char* doc_key,
    const char* text,
    size_t length);

/** Line `line` without its line ending, into `out`. */
void
EditorCs2Code_Line(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    char* out,
    size_t out_size);

/**
 * The TEXT_SPLICE for a CODE event over `text` (as last synced):
 * REPLACE (line becomes `new_text`), SPLIT (a line break at `col`), JOIN (the
 * break after `line` goes). Returns 0 when the event does not fit the text or
 * the splice would not fit one command.
 */
int
EditorCs2Code_PlanReplace(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    const char* new_text,
    struct Editor_Cmd* out);

int
EditorCs2Code_PlanSplit(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    int col,
    struct Editor_Cmd* out);

int
EditorCs2Code_PlanJoin(
    const struct EditorCs2Code* code,
    const char* text,
    int line,
    struct Editor_Cmd* out);

/** Highlight kinds a span carries; the panel maps them to colours. */
enum EditorCs2Token
{
    EDITOR_CS2_TOKEN_KEYWORD = 1,
    EDITOR_CS2_TOKEN_STRING,
    EDITOR_CS2_TOKEN_COMMENT,
    EDITOR_CS2_TOKEN_NUMBER,
    EDITOR_CS2_TOKEN_LOCAL,
    EDITOR_CS2_TOKEN_VAR,
    EDITOR_CS2_TOKEN_CALL,
    EDITOR_CS2_TOKEN_HEADER,
};

struct EditorCs2Span
{
    int start;
    int length;
    int token;
};

/** Spans of one line of `.cs2` source. Returns how many (at most `max`). */
int
EditorCs2_Highlight(
    const char* line,
    struct EditorCs2Span* out,
    int max);

/* ---- harness ------------------------------------------------------------- */

/**
 * Write one TORIRS_CS2_HARNESS case file (tools/perf/cs2_cases' format):
 * `{"version": 1, "cases": [{"id", "scriptId", "intArgs", "stringArgs",
 * "screenshot", "settleMs"}]}`. Returns 1 when written.
 */
int
EditorCs2_WriteHarnessCase(
    const char* path,
    const char* case_id,
    int script_id,
    const int* int_args,
    int int_arg_count,
    const char* const* string_args,
    int string_arg_count,
    int screenshot);

#endif
