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

#include "editor_cmd.h"

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

/**
 * OVERLAY KEYS. "<type>:<name>@<overlay>" addresses the record's `[name]`
 * block in a SERVER OVERLAY file -- `<overlay>` is a path under
 * server/scripts ending in `.<type>` ("npc:giantspider1@areas/lumbridge/
 * configs/lumbridge.npc", "npc:giantspider1@_explicit/configs/
 * server_fields.generated.npc") -- instead of in configs/all.<type>. The
 * document is still just that block (a 10 MB generated overlay is never
 * loaded whole), saved back over it with every other byte kept. A key with
 * no '@' means configs/all.<type>, exactly as before.
 *
 * Editor_CfgKeyOverlay: the overlay path of a key, or NULL for an all.<type>
 * key. Editor_CfgOverlayKey: the key for (type, name, overlay or NULL/"");
 * 0 when it does not fit EDITOR_DOC_KEY_MAX (128).
 */
const char*
Editor_CfgKeyOverlay(const char* key);

int
Editor_CfgOverlayKey(
    char out[128],
    const char* type,
    const char* name,
    const char* overlay);

/* ---- a record across its layers (the record editor) ------------------------
 *
 * An npc's (loc's, obj's) fields are not one block. The pack the server reads
 * is merged by cachepack (3rd/rscache/tools/cachepack/cp_merge.c
 * cp_merge_rank_for; the server reads only that pack -- torirs_server_content.c
 * walk_configs is the old text loader's sorted walk, kept for spawns):
 *
 *   rank 0   configs/all.<type>                    the cache half, every key
 *   rank 1   a server/scripts file whose basename holds `.generated.`
 *            (and an included lane's ported/<lane>/configs): generated
 *   rank 2   every other `.<type>` file under server/scripts: authored
 *
 * A higher rank wins; within a rank the later file in sorted path order
 * (cp_walk.c sorts the walk) wins. `param=<name>,<value>` lines merge PER
 * PARAM NAME (cp_merge.c slot_for), so each param is its own field here.
 *
 * Editor_CfgRecord holds one block per layer (each a CFG document: the
 * all.<type> key, or an overlay key "<type>:<name>@<overlay>"), resolves
 * every field to the layer that WINS it, adds the field register's keys
 * (fields/<type>.ini) the record states nowhere, and turns an edit into the
 * CFG_BLOCK / CFG_FIELD commands that make it -- in the file that actually
 * holds the value, or ("override in <layer>") in another layer.
 */

#define EDITOR_CFG_RECORD_LAYERS_MAX 16
#define EDITOR_CFG_REGISTER_MAX 256

/** One field of the register (fields/<type>.ini, every `[<type>.<key>]`
 *  section merged). */
struct Editor_CfgRegisterField
{
    char key[EDITOR_CMD_NAME_MAX];
    /** "server" (the default) or "client". */
    char scope[16];
    /** `text = param`: spelled `param=<key>,<value>`. */
    int text_param;
    /** `text = indexed`: spelled `<key>1..N=` (patrol1, patrol2, ...). */
    int text_indexed;
    /** `values = a:0, b:1`, verbatim ("" when none). */
    char values[EDITOR_CMD_VALUE_MAX];
    /* Workbench 6.6 (design section 9, the typed rows): the rest of the
     * register's grammar (3rd/rscache/src/rscache_register.h), verbatim. */
    /** `type = int` / `boolean` / `obj` / `coord,obj,int` ("" when none). */
    char type[64];
    /** `ref = <config type>`: the namespace a symbolic value names. */
    char ref[32];
    /** `client = drop | native | error | param:<p>` ("" = drop). */
    char client[48];
    /** `server = opcode:<n>:<u1|u2|u4|string|list> | drop` ("" when none). */
    char server[48];
    /** `param = <p>`: its runtime param binding. */
    char param[48];
};

/**
 * What a row of the Record panel is edited with (design section 9: "a number
 * gets a field with step buttons, a bool a checkbox, a name a catalog-backed
 * picker, a param a two-column row"), from the register row:
 *
 *   ENUM    `values = ...` lists the words: a dropdown of them.
 *   BOOL    `type = boolean`.
 *   NAME    `ref = <type>`, or a `type` that is a config type (obj, npc,
 *           loc, seq, ...): a picker over the catalog's records of that kind.
 *   NUMBER  `type = int`, or a band home of width u1 / u2 / u4: step buttons,
 *           the range from the width.
 *   TEXT    anything else (`type = string`, a list's tuple, no type).
 *
 * A `text = param` field is spelled `param=<key>,<value>`: the row is the
 * two-column PARAM row (param name, value) and its VALUE has the kind above.
 * A key the register does not list is typed by its value (an integer is a
 * NUMBER with no range, yes / no / true / false a BOOL, else TEXT): the cache
 * half states dozens of opcodes the register never names.
 */
enum Editor_CfgFieldKind
{
    EDITOR_CFG_FIELD_TEXT = 0,
    EDITOR_CFG_FIELD_NUMBER,
    EDITOR_CFG_FIELD_BOOL,
    EDITOR_CFG_FIELD_NAME,
    EDITOR_CFG_FIELD_ENUM,
    EDITOR_CFG_FIELD_PARAM,
};

/** "text", "number", "bool", "name", "enum", "param". */
const char*
Editor_CfgFieldKindName(int kind);

/** The register field for `key`, or -1. */
int
Editor_CfgRegisterFind(
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* key);

/** The VALUE kind of a register field (never PARAM: a param field's value
 *  has its own kind). */
int
Editor_CfgRegisterValueKind(const struct Editor_CfgRegisterField* field);

/** The catalog kind name a NAME field picks from ("obj"); "" for any other
 *  kind. */
const char*
Editor_CfgRegisterRefType(const struct Editor_CfgRegisterField* field);

/** A NUMBER field's range: 1 with [*out_min, *out_max] when the register
 *  bounds it (u1 0..255, u2 0..65535, u4 0..4294967295, int the int32
 *  range), 0 when it says nothing. */
int
Editor_CfgRegisterRange(
    const struct Editor_CfgRegisterField* field,
    long long* out_min,
    long long* out_max);

/** An ENUM field's words, in register order, into `out` (each at most 31
 *  bytes); returns the count (at most `max`). */
int
Editor_CfgRegisterWords(
    const struct Editor_CfgRegisterField* field,
    char out[][32],
    int max);

/** 1 when changing the field changes what the CLIENT reads: `scope =
 *  client`, `client = native` or `client = param:<p>` (cachepack writes it
 *  into the client cache), so a RUN needs a client pack. */
int
Editor_CfgRegisterClientSide(const struct Editor_CfgRegisterField* field);

/**
 * What is wrong with a row, as the Record panel marks it and the Problems
 * panel lists it (design section 9: "a field unknown to the register is
 * marked and listed in Problems"):
 *
 *   UNKNOWN_KEY    a SERVER OVERLAY states a key fields/<type>.ini does not
 *                  list (the cache half's own opcodes are never this: the
 *                  register declares what lies beyond them)
 *   UNKNOWN_PARAM  an overlay's `param=<p>,...` names a param the register
 *                  does not list and the caller's known params do not hold
 *   NOT_NUMBER     a NUMBER row whose value is not an integer
 *   OUT_OF_RANGE   a NUMBER row outside its width's range
 *   NOT_BOOL       a BOOL row that is not yes / no / true / false / 1 / 0
 *   NOT_WORD       an ENUM row that is none of its words (nor a number)
 */
enum Editor_CfgRowIssue
{
    EDITOR_CFG_ISSUE_NONE = 0,
    EDITOR_CFG_ISSUE_UNKNOWN_KEY,
    EDITOR_CFG_ISSUE_UNKNOWN_PARAM,
    EDITOR_CFG_ISSUE_NOT_NUMBER,
    EDITOR_CFG_ISSUE_OUT_OF_RANGE,
    EDITOR_CFG_ISSUE_NOT_BOOL,
    EDITOR_CFG_ISSUE_NOT_WORD,
};

/** "unknown key", "unknown param", "not a number", "out of range",
 *  "not yes or no", "not one of its words"; "" for NONE. */
const char*
Editor_CfgRowIssueText(int issue);

/** The issue `value` has as a value of `kind` under its register row
 *  `field` (never NULL: a key the register lacks has no value issue, and
 *  the caller, who knows whether it has a row, does not ask). NONE for TEXT
 *  and NAME (a name is the catalog's to say). */
int
Editor_CfgValueIssue(
    const struct Editor_CfgRegisterField* field,
    int kind,
    const char* value);

/** Parse a field register for `type` ("npc"): every `[<type>.<key>]`
 *  section, merged by key in first-seen order. Returns the field count
 *  (at most `max`). */
int
Editor_CfgRegisterParse(
    const char* type,
    const char* text,
    size_t length,
    struct Editor_CfgRegisterField* out,
    int max);

struct Editor_CfgRecordLayer
{
    /** The CFG document key: "npc:x" or "npc:x@<overlay>". */
    char doc_key[EDITOR_DOC_KEY_MAX];
    /** "configs/all.npc" or the overlay path under server/scripts. */
    char label[EDITOR_DOC_KEY_MAX];
    /** 0, 1 or 2 (above). */
    int rank;
    /** The layer holds a `[name]` block. */
    int present;
    struct Editor_CfgBlock block;
};

struct Editor_CfgRecordRow
{
    /** The line key ("respawnrate", "param", "patrol2"). */
    char key[EDITOR_CMD_NAME_MAX];
    /** A param's name ("huntrange"), else "". */
    char sub[EDITOR_CMD_NAME_MAX];
    /** Which statement of a key a layer repeats (a list key), 0 mostly. */
    int occurrence;
    /** The winning value, without a param's "<name>," and without a
     *  trailing `// comment`. */
    char value[EDITOR_CMD_VALUE_MAX];
    /** The layer that wins it, -1 when no layer states it. */
    int layer;
    /** Other layers stating it (beneath the winner). */
    int shadowed;
    /** From the register: its scope ("" when the register has no row). */
    char scope[16];
    /* Workbench 6.6: the typed row (Editor_CfgRecordResolve fills these). */
    /** The register field it is (Editor_CfgRegisterFind), -1 for none. For
     *  an indexed key (patrol2) the `patrol` field. */
    int reg;
    /** enum Editor_CfgFieldKind of the row (PARAM for a param row), and of
     *  its value. */
    unsigned char kind;
    unsigned char value_kind;
    /** Changing it needs a client pack: the winning layer is the cache half,
     *  or the register says the client reads it. */
    unsigned char client_side;
    /** Some server overlay (rank 1 or 2) states it. */
    unsigned char in_overlay;
    /** enum Editor_CfgRowIssue. */
    unsigned char issue;
};

struct Editor_CfgRecord
{
    char type[32];
    char name[EDITOR_CMD_NAME_MAX];
    struct Editor_CfgRecordLayer layers[EDITOR_CFG_RECORD_LAYERS_MAX];
    int layer_count;
    /** Owned. */
    struct Editor_CfgRecordRow* rows;
    int row_count;
    int row_capacity;
};

void
Editor_CfgRecordInit(
    struct Editor_CfgRecord* record,
    const char* type,
    const char* name);

/** NULL-tolerant. */
void
Editor_CfgRecordFree(struct Editor_CfgRecord* record);

/** The rank of a layer: NULL / "" (configs/all.<type>) 0, a `.generated.`
 *  basename or a ported lane 1, else 2. */
int
Editor_CfgLayerRank(const char* overlay);

/**
 * Add a layer: `overlay` NULL for configs/all.<type>, else the path under
 * server/scripts; `text` its CFG document's text (the block, or empty when
 * the file has no block for the record yet). Layers are kept in merge order
 * (rank, then path), so an index can move when a layer is added; find one
 * again by doc_key. Returns 1, 0 when full or the key does not fit.
 */
int
Editor_CfgRecordAddLayer(
    struct Editor_CfgRecord* record,
    const char* overlay,
    const char* text,
    size_t length);

/** Index of the layer with `doc_key`, or -1. */
int
Editor_CfgRecordFindLayer(
    const struct Editor_CfgRecord* record,
    const char* doc_key);

/** Rebuild the rows from the layers, plus every register field the record
 *  states nowhere (`fields` may be NULL with count 0). */
void
Editor_CfgRecordResolve(
    struct Editor_CfgRecord* record,
    const struct Editor_CfgRegisterField* fields,
    int field_count);

/**
 * The typed half of a resolve: every row's register field, kinds, client
 * side and issue. Editor_CfgRecordResolve calls it with no known params;
 * call it again with the params the catalog knows (`known_params`, which
 * may be NULL with count 0) so an overlay's `param=<p>` that names a real
 * param is not UNKNOWN_PARAM. Returns the rows with an issue.
 */
int
Editor_CfgRecordType(
    struct Editor_CfgRecord* record,
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* const* known_params,
    int known_count);

/** The field name a row is addressed by: "param:<p>", "key#N" or "key". */
void
Editor_CfgRecordRowName(
    const struct Editor_CfgRecordRow* row,
    char* out,
    size_t out_size);

/**
 * The search inside a record (design section 9: "a loc record can hold forty
 * lines"): 1 when every whitespace-separated term of `query` is found,
 * case-insensitively, in the row's field name, its value, its layer's file
 * name or its kind's name. An empty query matches every row.
 */
int
Editor_CfgRecordRowMatches(
    const struct Editor_CfgRecord* record,
    int row,
    const char* query);

/**
 * "Changed only": 1 when row `row` of `record` differs from what `base` (the
 * same record resolved earlier -- when it was opened, or last clean) says
 * for the same (key, sub, occurrence): another value, another winning layer
 * (compared by document key), stated now and not then, or the reverse.
 */
int
Editor_CfgRecordRowChanged(
    const struct Editor_CfgRecord* record,
    int row,
    const struct Editor_CfgRecord* base);

/** Fields `base` states that `record` no longer has a row for (a removed
 *  field the register does not list): what "dirty, N fields" adds. */
int
Editor_CfgRecordRemovedSince(
    const struct Editor_CfgRecord* record,
    const struct Editor_CfgRecord* base);

/** Does layer `layer` state row `row`'s field? Its line in the block (the
 *  header is 0) and its value (as the row's value is spelled: no param
 *  name, no trailing comment) into `out_value`; -1 when it does not. What a
 *  layer's tab lists, and the line a Problems row opens. */
int
Editor_CfgRecordLayerStates(
    const struct Editor_CfgRecord* record,
    int layer,
    int row,
    char* out_value,
    size_t value_size);

/** The row for (key, sub, occurrence), or -1. */
int
Editor_CfgRecordFindRow(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub,
    int occurrence);

/** Where a field nobody states is added: the highest-precedence layer that
 *  holds the record's block (the one whose statement would win), else 0. */
int
Editor_CfgRecordAddLayerFor(const struct Editor_CfgRecord* record);

/**
 * The commands that set field (key, sub, occurrence) to `value` ("" removes
 * it) in layer `layer` (-1: the layer that wins it now, or for a field no
 * layer states, Editor_CfgRecordAddLayerFor). A layer with no block for the
 * record gets a CFG_BLOCK add first. A value keeps the old line's trailing
 * `// comment`. Returns the commands written, 0 when nothing changes, -1 when
 * the edit cannot be expressed (a key or value the codec refuses, a layer
 * out of range, `max` too small).
 */
int
Editor_CfgRecordFieldCommands(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub,
    int occurrence,
    const char* value,
    int layer,
    struct Editor_Cmd* out,
    int max);

#endif
