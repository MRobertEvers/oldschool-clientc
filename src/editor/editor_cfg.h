#ifndef SRC_EDITOR_EDITOR_CFG_H
#define SRC_EDITOR_EDITOR_CFG_H

/**
 * The config text codec: `[name]` blocks of `key=value` lines
 * (docs/MAPED_DESIGN.html #documents, "Config record").
 *
 * One grammar covers every file this reads: the cache half
 * (`configs/all.<type>`, and the `id=name` member indexes beside them) and the
 * server overlays (`server/scripts/.../configs/<file>.<type>`). A file is a
 * preamble followed by blocks; a block runs from its header line to the next
 * header or the end of the file, so the blank separator and any comment that
 * precedes the next header belong to the block above it. That is the extent
 * the file layer cuts a CFG document out of (editor_host_local.c) and the
 * extent a save puts back.
 *
 * Every line is one of:
 *
 *   HEADER   first byte '[': `[name]`, plus whatever follows the `]`.
 *   FIELD    `key=value`: the key is every byte before the first '='.
 *   COMMENT  first non-blank bytes are "//".
 *   BLANK    spaces and tabs only.
 *   OTHER    anything else, carried verbatim.
 *
 * and keeps its own line ending (LF, CRLF, or none on the last line), so the
 * round trip — parse, emit from the parsed parts — is byte-exact for every
 * file, including the three in the tree that end without a newline. Keys the
 * codec does not understand are not a case: it understands none of them, it
 * only addresses them.
 *
 * Two layers, because a whole all.loc is 86 MB and an edit touches one block:
 *
 *   Editor_CfgFile    an index of block extents over BORROWED text — what a
 *                     save needs to put one block back, no copy, no parse
 *                     of the lines.
 *   Editor_CfgBlock   one block's text, OWNED and parsed into lines, with the
 *                     edit operations. An edit rewrites only the lines it
 *                     names; every other byte of the block is preserved.
 *
 * FIELD SPECS. CFG_FIELD names its field by a spec, `key[#occurrence][@line]`:
 *
 *   "name"        occurrence 0 of `name`
 *   "val#2"       the third `val=` line (a repeated key: `val=`, `data=`)
 *   "op1@3"       occurrence 0, which is (or, inserted, becomes) line 3 of the
 *                 block, the header being line 0
 *
 * The command layout carries no position, and without one the undo of a
 * removal could only put the line back at the end of the record — a save
 * after edit-then-undo would then show a diff nobody made. So a removal
 * carries its line (Editor_CfgFieldSpecFormat with the line FieldFind gave),
 * the forward apply refuses it when the field is no longer on that line, and
 * the inverse inserts it exactly there. Editor_CfgApply (editor_doc.c) reads
 * CFG_FIELD's key through Editor_CfgFieldSpecParse.
 */

#include <stddef.h>

enum Editor_CfgLineKind
{
    EDITOR_CFG_LINE_BLANK = 0,
    EDITOR_CFG_LINE_COMMENT,
    EDITOR_CFG_LINE_HEADER,
    EDITOR_CFG_LINE_FIELD,
    EDITOR_CFG_LINE_OTHER,
};

enum Editor_CfgEol
{
    EDITOR_CFG_EOL_NONE = 0,
    EDITOR_CFG_EOL_LF,
    EDITOR_CFG_EOL_CRLF,
};

/** One line, by offsets into the text that owns it. */
struct Editor_CfgLine
{
    unsigned char kind; /* enum Editor_CfgLineKind */
    unsigned char eol;  /* enum Editor_CfgEol */
    /** HEADER: 1 when the name is closed by ']'. */
    unsigned char closed;
    /** Offset of the line's first byte. */
    size_t start;
    /** Content bytes, the line ending excluded. */
    size_t length;
    /** HEADER: bytes of the name (between '[' and ']'); FIELD: bytes of the
     *  key (before the '='); otherwise 0. */
    size_t part;
};

/** Bytes of a line ending. */
static inline size_t
Editor_CfgEolLength(unsigned char eol)
{
    return eol == EDITOR_CFG_EOL_CRLF ? 2 : eol == EDITOR_CFG_EOL_LF ? 1 : 0;
}

/**
 * Parse the line starting at `at` (< length) and return the offset just past
 * its line ending. Never reads past `length`.
 */
size_t
Editor_CfgLineParse(
    const char* text,
    size_t length,
    size_t at,
    struct Editor_CfgLine* out_line);

/**
 * Write `count` lines back from their parsed parts — the header's name and
 * tail, the field's key and value, the recorded line ending — into `out`
 * (may be NULL to measure). Returns the bytes written. For lines straight
 * from Editor_CfgLineParse this is the text they were parsed from; that
 * identity is the round-trip contract editor_cfg_test holds every file to.
 */
size_t
Editor_CfgEmitLines(
    const char* text,
    const struct Editor_CfgLine* lines,
    int count,
    char* out);

/**
 * "key" -> (key, 0, -1); "key#N" -> (key, N, -1); "key@L" / "key#N@L" ->
 * (key, N, L). Returns 0 for an empty key, a key that would not survive as a
 * line ('=', '#', '@', a line break, a leading '[' or "//"), a bad number,
 * or one that does not fit `key_size`.
 */
int
Editor_CfgFieldSpecParse(
    const char* spec,
    char* out_key,
    size_t key_size,
    int* out_occurrence,
    int* out_line);

/** The spec for (key, occurrence, line); occurrence 0 and line -1 are left
 *  out. Returns 0 when it does not fit `out_size`. */
int
Editor_CfgFieldSpecFormat(
    const char* key,
    int occurrence,
    int line,
    char* out,
    size_t out_size);

/* ---- the file index ------------------------------------------------------ */

struct Editor_CfgBlockRef
{
    /** [start, end): the header line through the byte before the next
     *  header (or the end of the text). */
    size_t start;
    size_t end;
    /** The name, inside the header. */
    size_t name_start;
    size_t name_length;
};

struct Editor_CfgFile
{
    /** Borrowed: the caller keeps it alive while the index is used. */
    const char* text;
    size_t length;
    /** Bytes before the first header (the preamble): [0, preamble_end). */
    size_t preamble_end;
    /** Owned. */
    struct Editor_CfgBlockRef* blocks;
    int block_count;
    int block_capacity;
};

/** Index `text`'s blocks. Borrows `text`. */
void
Editor_CfgFileIndex(
    struct Editor_CfgFile* file,
    const char* text,
    size_t length);

/** Frees the index (never the text) and zeroes it. NULL-tolerant. */
void
Editor_CfgFileFree(struct Editor_CfgFile* file);

/** Index of the first block named `name`, or -1. */
int
Editor_CfgFileFind(
    const struct Editor_CfgFile* file,
    const char* name);

/**
 * The extent of the first block named `name` in `text`, without building an
 * index: one pass that stops at the match. Returns 0 when there is none.
 * This is the CFG document's extent (editor_host_local.c's load and save).
 */
int
Editor_CfgBlockExtent(
    const char* text,
    size_t length,
    const char* name,
    size_t* out_start,
    size_t* out_end);

/**
 * The file text with block `name` replaced by `block` (`block_length` bytes;
 * 0 = the record is gone): every other byte as it was. A block the file does
 * not hold is appended after a blank separator line. Returns an owned buffer
 * (ToriMem_Editor) of `*out_length` bytes plus a terminator.
 */
char*
Editor_CfgFileReplaceBlock(
    const char* text,
    size_t length,
    const char* name,
    const char* block,
    size_t block_length,
    size_t* out_length);

/* ---- one block, editable ------------------------------------------------- */

struct Editor_CfgBlock
{
    /** Owned, NUL-terminated, `length` bytes before the terminator. */
    char* text;
    size_t length;
    size_t capacity;
    /** Owned; offsets into `text`. Rebuilt after every edit. */
    struct Editor_CfgLine* lines;
    int line_count;
    int line_capacity;
};

void
Editor_CfgBlockInit(struct Editor_CfgBlock* block);

/** Frees and zeroes. NULL-tolerant. */
void
Editor_CfgBlockFree(struct Editor_CfgBlock* block);

/** Copy `text` in and parse it. Text before the header, or no header at
 *  all, is legal: a CFG document whose record was removed is such a text. */
void
Editor_CfgBlockSet(
    struct Editor_CfgBlock* block,
    const char* text,
    size_t length);

/** Index of the first HEADER line, or -1. */
int
Editor_CfgBlockHeader(const struct Editor_CfgBlock* block);

/** 1 when the block has a header and it names exactly `name`. */
int
Editor_CfgBlockNameIs(
    const struct Editor_CfgBlock* block,
    const char* name);

/** Occurrences of `key` among the block's FIELD lines. */
int
Editor_CfgBlockFieldCount(
    const struct Editor_CfgBlock* block,
    const char* key);

/** Line index of occurrence `occurrence` of `key`, or -1. */
int
Editor_CfgBlockFieldFind(
    const struct Editor_CfgBlock* block,
    const char* key,
    int occurrence);

/** The value of FIELD line `line_index` (not terminated; `*out_length`
 *  bytes, the line ending excluded). */
const char*
Editor_CfgBlockValue(
    const struct Editor_CfgBlock* block,
    int line_index,
    size_t* out_length);

/** 1 when occurrence `occurrence` of `key` exists and its value is exactly
 *  `value`. */
int
Editor_CfgBlockFieldIs(
    const struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    const char* value);

/**
 * The edits. Each returns 1 when the block changed, 0 when the edit does not
 * fit it (and the block is untouched):
 *
 *   FieldSet     replace the value of an existing occurrence.
 *   FieldInsert  add `key=value` as occurrence `occurrence` (0..count).
 *                With `line` >= 0 the new line becomes that line, which must
 *                be after the header and leave exactly `occurrence` earlier
 *                occurrences. Without: before the current occurrence
 *                `occurrence`, or for occurrence == count after the key's
 *                last occurrence, else after the block's last field, else
 *                after the header — never after a trailing comment, which in
 *                an all.<type> file is the next record's. Needs a header.
 *                The new line takes the header's line ending; appended to a
 *                last line that has none, the ending goes between them and
 *                the new line has none, so FieldRemove takes back exactly
 *                what this added.
 *   FieldRemove  remove an occurrence's line and its line ending (for a last
 *                line with none, the ending before it). With `line` >= 0 the
 *                occurrence must be that line.
 *   Rename       the header's name; its tail and line ending are kept.
 *   AddHeader    a block with no header gets "[name]" + LF at its start.
 *   RemoveHeader the header line goes, when it is the first line, names
 *                `name`, and no FIELD or OTHER line is left (comments and
 *                blank lines stay, so AddHeader restores the bytes).
 *
 * Values and names may not contain a line break; keys follow
 * Editor_CfgFieldSpecParse's rule.
 */
int
Editor_CfgBlockFieldSet(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    const char* value);

int
Editor_CfgBlockFieldInsert(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    int line,
    const char* value);

int
Editor_CfgBlockFieldRemove(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    int line);

int
Editor_CfgBlockRename(
    struct Editor_CfgBlock* block,
    const char* new_name);

int
Editor_CfgBlockAddHeader(
    struct Editor_CfgBlock* block,
    const char* name);

int
Editor_CfgBlockRemoveHeader(
    struct Editor_CfgBlock* block,
    const char* name);

/* ---- CFG document keys --------------------------------------------------- */

/**
 * Split a CFG document key "<type>:<record>" ("loc:newbie_door1") into its
 * halves. Returns 0 when the key has no ':' or a half does not fit.
 */
int
Editor_CfgKeySplit(
    const char* key,
    char* out_type,
    size_t type_size,
    char* out_name,
    size_t name_size);

#endif
