#ifndef SRC_EDITOR_BUILDER_BUILDER_SSC_H
#define SRC_EDITOR_BUILDER_BUILDER_SSC_H

/**
 * The builders' compile check (design §11 "Validation"): every regeneration
 * runs the RuneScript compiler over the tree WITH the generated files in it,
 * and the errors that name those files are shown inline.
 *
 * The content tree is read-only to the editor, and the generated text is not
 * on disk until a save, so the compile runs against a STAGE: a private copy
 * of what sscompile reads from server/scripts (the .rs2 sources, .constant,
 * .dbtable, .varp, .varbit and pack files -- never the build outputs, never a
 * lane's script directories, which a lane-less compile subtracts), with the
 * generated files written over it. --out is a sibling directory of the stage.
 * Both live under a work directory the caller names, which this module
 * refuses to put inside the content root: the work directory is the only
 * place it writes.
 *
 * The first stage copies ~40 MB; later ones re-copy only files whose size or
 * mtime moved, and drop files the tree no longer has. The compile is
 * incremental across runs (sscompile keeps <out>/ssc.state), so a re-check of
 * one edited unit answers in about a second.
 *
 *   BUILDER_SSC_COMPILE   compile into the work directory's out/ and report
 *                         errors (what the builder panel runs)
 *   BUILDER_SSC_DRY_RUN   sscompile --dry-run: which units are stale and
 *                         why; compiles nothing, so it reports no errors
 *
 * Native only: on the web and on Windows Builder_SscRun answers
 * BUILDER_SSC_UNSUPPORTED (the live editor's BuildRunner owns builds there,
 * MAPED_IMPLEMENTATION.md phase 4).
 */

#include <stddef.h>

#define BUILDER_SSC_PATH_MAX 1024
#define BUILDER_SSC_DIAGS_MAX 64
#define BUILDER_SSC_PUTS_MAX 16

enum Builder_SscMode
{
    BUILDER_SSC_COMPILE = 0,
    BUILDER_SSC_DRY_RUN,
};

enum Builder_SscStatus
{
    /** sscompile ran and exited 0. */
    BUILDER_SSC_OK = 0,
    /** sscompile ran and exited non-zero: see the diagnostics. */
    BUILDER_SSC_FAILED,
    /** It could not be run (no binary, spawn failed, stage failed). */
    BUILDER_SSC_NOT_RUN,
    BUILDER_SSC_UNSUPPORTED,
};

struct Builder_SscDiag
{
    /** Relative to server/scripts when it names a staged file. */
    char file[256];
    int line;
    int warning;
    char message[256];
};

struct Builder_SscResult
{
    enum Builder_SscStatus status;
    int exit_code;
    double seconds;
    struct Builder_SscDiag diags[BUILDER_SSC_DIAGS_MAX];
    int diag_count;
    int diags_dropped;
    /** The compiler's summary line ("3184 compiled, 0 reused, ..."). */
    char summary[256];
    /** Where the whole output went. */
    char log_path[BUILDER_SSC_PATH_MAX];
};

struct Builder_Ssc
{
    char sscompile[BUILDER_SSC_PATH_MAX];
    /** The revision content root (holds server/, pack/, configs/). */
    char content_root[BUILDER_SSC_PATH_MAX];
    char work_dir[BUILDER_SSC_PATH_MAX];
    /** work_dir/scripts, work_dir/out. */
    char stage[BUILDER_SSC_PATH_MAX];
    char out[BUILDER_SSC_PATH_MAX];
    int staged;
    /** Files written over the stage this session (relative to
     *  server/scripts); the stage's refresh keeps them. */
    char puts[BUILDER_SSC_PUTS_MAX][256];
    int put_count;
    /** Files copied / dropped by the last Stage. */
    int last_copied;
    int last_dropped;
    int last_kept;
};

/**
 * Set the runner up. Returns 0 -- and the runner stays unusable -- when
 * `work_dir` is inside `content_root` (the content tree is never written) or
 * a path does not fit. Creates nothing yet.
 */
int
Builder_SscInit(
    struct Builder_Ssc* ssc,
    const char* sscompile,
    const char* content_root,
    const char* work_dir);

/** Mirror server/scripts into the stage (see the file comment). Returns 1. */
int
Builder_SscStage(struct Builder_Ssc* ssc);

/**
 * Write `text` over the stage at `rel_path` (relative to server/scripts,
 * "edited/x/scripts/x.rs2"); `length` 0 writes an empty file. Returns 0 for
 * a path that would leave the stage.
 */
int
Builder_SscPut(
    struct Builder_Ssc* ssc,
    const char* rel_path,
    const char* text,
    size_t length);

/** Run sscompile over the stage (staging it first when it never was). */
enum Builder_SscStatus
Builder_SscRun(
    struct Builder_Ssc* ssc,
    enum Builder_SscMode mode,
    struct Builder_SscResult* result);

/** Parse compiler output into result->diags (exposed for the tests). File
 *  names under `stage` are rewritten relative to it. */
void
Builder_SscParseOutput(
    const char* stage,
    const char* output,
    size_t length,
    struct Builder_SscResult* result);

/** The errors (not warnings) about `rel_path`. */
int
Builder_SscErrorsFor(
    const struct Builder_SscResult* result,
    const char* rel_path);

#endif
