#ifndef RSCACHE_TOOLS_CP_TEXT_H
#define RSCACHE_TOOLS_CP_TEXT_H

/*
 * The text layer: `[name]` blocks of `key=value` lines, one file per config type.
 *
 * This is LostCity's config source format, and the two halves here are its two
 * halves — `CP_Lines` is what an unpacker pushes into (engine/tools/unpack's
 * `def.push(...)`, plus its readability reordering), and `CP_ConfigFile` is what a
 * packer reads back (engine/tools/pack/config/PackShared.ts `readConfigs`).
 *
 * Keeping them in one file is deliberate: the reader and the writer have to agree
 * on escaping, on what a comment is, and on how a repeated key is spelled, and
 * every one of those agreements is invisible until a round trip loses a record.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---- writing: a record's lines ------------------------------------------ */

struct CP_Lines
{
    char** lines;
    int count;
    int capacity;
};

void
cp_lines_init(struct CP_Lines* lines);

void
cp_lines_free(struct CP_Lines* lines);

void
cp_lines_clear(struct CP_Lines* lines);

/** Append a formatted line. An allocation failure asserts: a dropped line is a
 *  missing property, which is exactly the silent loss this format exists to
 *  rule out. */
void
cp_lines_addf(
    struct CP_Lines* lines,
    const char* fmt,
    ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/**
 * Append `key=value` where `value` is a cache string.
 *
 * Cache strings are windows-1252 wire bytes (rscache is byte-transparent both
 * ways, see EXCEPTIONS.md A2) and may contain the characters this format uses
 * structurally. Newlines and carriage returns are escaped as `\n` / `\r`, and a
 * leading `[` is escaped, so a name can never be mistaken for a block header on
 * the way back in. Everything else, including `=`, passes through: only the
 * *first* `=` separates, so a value may hold as many as it likes. A value that
 * reads exactly `default` or `empty` is escaped (`\default`), so it is never
 * mistaken for a marker below.
 */
void
cp_lines_add_str(
    struct CP_Lines* lines,
    const char* key,
    const char* value);

/*
 * Every key of a record is stated, present or not.
 *
 * A field the record's opcode stream did not state is written `key=default`,
 * and the client applies its own default. A list field the stream stated with
 * no entries is written `key=empty`. A value is never omitted to mean either:
 * that is what lost 1,705 sequences' "hide the weapon" in July 2026, when the
 * unpacker and the packer silently disagreed about what an omitted line meant.
 *
 * The markers are matched on the RAW text, before unescaping, so a string that
 * genuinely reads "default" is written escaped (`name=\default`) and cannot be
 * mistaken for one.
 */
#define CP_VALUE_DEFAULT "default"
#define CP_VALUE_EMPTY "empty"

/** True if the raw (still escaped) value is the `default` marker. */
int
cp_value_is_default(const char* raw);

/** True if the raw (still escaped) value is the `empty` marker. */
int
cp_value_is_empty(const char* raw);

/** Append `key=default`: the record does not state this field. */
void
cp_lines_add_default(
    struct CP_Lines* lines,
    const char* key);

/** Append `key=empty`: the record states this list field with no entries. */
void
cp_lines_add_empty(
    struct CP_Lines* lines,
    const char* key);

/** Write the block header and every line to `out`, then a blank separator. */
void
cp_lines_write(
    const struct CP_Lines* lines,
    const char* debugname,
    FILE* out);

/* ---- reading: a whole config file --------------------------------------- */

struct CP_ConfigLine
{
    char* key;
    char* value;
    int line_no;
};

struct CP_Config
{
    char* debugname;
    struct CP_ConfigLine* lines;
    int count;
    int capacity;
};

struct CP_ConfigFile
{
    char* path;
    struct CP_Config* configs;
    int count;
    int capacity;
};

/**
 * Parse one `.npc`/`.loc`/... source file into blocks.
 *
 * Rejects a duplicate `[name]`, a line with no `=` outside a header, and an empty
 * name — the same three refusals `readConfigs` makes, and for the same reason: each
 * one silently loses a record if it is instead tolerated.
 *
 * Returns 1 on success, 0 with a message on stderr otherwise.
 */
int
cp_config_file_load(
    struct CP_ConfigFile* file,
    const char* path);

/** Write one `key=value` line: a value holding `//` is written `/\/` and a
 *  trailing blank `\ `, so the reader's comment cut and trim give it back. Every
 *  writer of config text goes through this (or cp_lines_write). */
void
cp_line_write(
    FILE* out,
    const char* line);

/** Serialise one record's block into a heap buffer. Caller frees. */
char*
cp_lines_to_string(
    const struct CP_Lines* lines,
    const char* debugname,
    size_t* out_size);

/** As cp_config_file_load, over bytes already in memory. `label` names them in
 *  error messages. Used by `verify`, which round-trips a record through the text
 *  without touching the filesystem. */
int
cp_config_file_load_memory(
    struct CP_ConfigFile* file,
    const char* text,
    size_t size,
    const char* label);

void
cp_config_file_free(struct CP_ConfigFile* file);

/** First value for `key`, or NULL. */
const char*
cp_config_get(
    const struct CP_Config* config,
    const char* key);

/** Undo cp_lines_add_str's escaping into `out`. Returns `out`. */
char*
cp_unescape(
    const char* value,
    char* out,
    int out_size);

/* ---- value helpers ------------------------------------------------------ */

/** Parse a decimal or 0x-prefixed integer. Returns 0 (and leaves `*out`) when the
 *  text is not entirely a number, which every caller reports as a bad value. */
int
cp_parse_int(
    const char* text,
    int* out);

int
cp_parse_i64(
    const char* text,
    int64_t* out);

/** `yes`/`no`/`true`/`false`/`1`/`0`, matching PackShared.isConfigBoolean. */
int
cp_parse_bool(
    const char* text,
    bool* out);

/**
 * Split `text` on commas into at most `max` fields, in place on a copy.
 *
 * Returns the field count. `scratch` must be at least strlen(text)+1 bytes; the
 * returned pointers point into it.
 */
int
cp_split(
    const char* text,
    char* scratch,
    char** fields,
    int max);

#endif
