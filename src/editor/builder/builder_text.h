#ifndef SRC_EDITOR_BUILDER_BUILDER_TEXT_H
#define SRC_EDITOR_BUILDER_BUILDER_TEXT_H

/**
 * Text plumbing the builders share: a growable buffer, and the one way a
 * builder changes a file -- a stroke of TEXT_SPLICE commands submitted to the
 * authority (docs/MAPED_IMPLEMENTATION.md section 2).
 *
 * A builder never writes a file. A regeneration is the difference between
 * the document's current text and the newly emitted text, cut into splices
 * that each fit one command (before and after together <= 1024 bytes), sent
 * in order inside one stroke so it undoes as one step. The authority applies
 * each against its own text and refuses a splice whose `before` is not where
 * it says, which is how two clients regenerating at once cannot interleave
 * into a corrupt file: the second one's stroke is refused, it re-reads, and
 * regenerates from the text that won.
 */

#include "editor/editor_cmd.h"

#include <stdarg.h>
#include <stddef.h>

/* ---- buffer -------------------------------------------------------------- */

/** Owned (ToriMem_Editor), always NUL-terminated once anything was put. */
struct Builder_Buf
{
    char* data;
    size_t length;
    size_t capacity;
};

void
Builder_BufInit(struct Builder_Buf* buf);

/** Frees and zeroes. NULL-tolerant. */
void
Builder_BufFree(struct Builder_Buf* buf);

void
Builder_BufPut(
    struct Builder_Buf* buf,
    const char* data,
    size_t length);

void
Builder_BufPuts(
    struct Builder_Buf* buf,
    const char* text);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void
Builder_BufPrintf(
    struct Builder_Buf* buf,
    const char* format,
    ...);

/** The text, "" when nothing was put. */
const char*
Builder_BufText(const struct Builder_Buf* buf);

/* ---- splices ------------------------------------------------------------- */

/**
 * The TEXT_SPLICE commands that turn `old_text` into `new_text` for document
 * (kind, key), applied in order. Common prefix and suffix are kept; the
 * middle is replaced in pieces of at most EDITOR_CMD_SPLICE_MAX bytes of
 * before-plus-after text each, each piece's offset stated for the text as
 * the previous pieces left it.
 *
 * Writes at most `max` commands (NULL to count) and returns how many the
 * change needs; 0 when the texts are equal.
 */
int
Builder_TextSplices(
    enum Editor_DocKind kind,
    const char* key,
    const char* old_text,
    size_t old_length,
    const char* new_text,
    size_t new_length,
    struct Editor_Cmd* out_commands,
    int max);

/**
 * Apply one TEXT_SPLICE to a plain buffer the way Editor_CmdApply applies it
 * to a document -- for tests and for a dry run of a stroke. Returns 0 when
 * `before` is not at `offset` (and the buffer is unchanged).
 */
int
Builder_TextSpliceApply(
    struct Builder_Buf* text,
    const struct Editor_Cmd* command);

/* ---- submitting ---------------------------------------------------------- */

/**
 * Where a builder's commands go. The client wires it to Editor_Apply /
 * Editor_StrokeBegin|End (builder_panels.c); a test wires it to
 * Editor_HostMapEdCmd on an embedded authority. `current` answers a
 * document's text as the submitter sees it (the mirror's copy), or NULL
 * with *out_length 0 when the document has no text yet.
 */
struct Builder_Submit
{
    void* user_data;
    /** 1 when the authority accepted the command. */
    int (*apply)(void* user_data, const struct Editor_Cmd* command);
    void (*stroke)(void* user_data, int begin);
    /** Make sure (kind, key) is open, then return its text. */
    const char* (*current)(
        void* user_data,
        enum Editor_DocKind kind,
        const char* key,
        size_t* out_length);
};

enum Builder_SyncResult
{
    /** The document already held exactly that text. */
    BUILDER_SYNC_UNCHANGED = 0,
    BUILDER_SYNC_APPLIED,
    /** The authority refused a splice: someone else changed the text.
     *  Commands accepted before the refusal stay applied (and in the same
     *  stroke); the caller re-reads and regenerates. */
    BUILDER_SYNC_REFUSED,
};

/**
 * Make document (TEXT, key) hold `text`: read its current text through
 * `submit->current`, cut the difference into splices and submit them in
 * one stroke. `in_stroke` 1 means the caller already opened a stroke (a
 * regeneration of several files undoes as one step).
 */
enum Builder_SyncResult
Builder_TextSync(
    const struct Builder_Submit* submit,
    const char* key,
    const char* text,
    size_t length,
    int in_stroke);

#endif
