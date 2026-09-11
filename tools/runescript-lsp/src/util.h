#ifndef TOOLS_RUNESCRIPT_LSP_UTIL_H
#define TOOLS_RUNESCRIPT_LSP_UTIL_H

/*
 * Growable buffers, path helpers, and the UTF-8/UTF-16 conversion the protocol
 * forces on us.
 *
 * LSP positions are counted in UTF-16 code units, tree-sitter's in UTF-8 bytes.
 * The content tree is full of em dashes and typographic quotes in comments, so
 * the two disagree on any line carrying one — a mismatch shows up as a
 * highlight sliding sideways for the rest of the line, which is why the
 * conversion is a named function here rather than an `int` cast at each site.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Buffers                                                             */
/* ------------------------------------------------------------------ */

struct Buf
{
    char* data;
    size_t length;
    size_t capacity;
};

void
Buf_Free(struct Buf* buf);

void
Buf_Reset(struct Buf* buf);

void
Buf_Append(struct Buf* buf, const char* text, size_t length);

void
Buf_AppendStr(struct Buf* buf, const char* text);

void
Buf_AppendChar(struct Buf* buf, char c);

void
Buf_Printf(struct Buf* buf, const char* fmt, ...);

/** Append `text` as a quoted, escaped JSON string. */
void
Buf_AppendJsonString(struct Buf* buf, const char* text);

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

char*
Str_Dup(const char* text);

char*
Str_DupN(const char* text, size_t length);

/** Case-insensitive compare, ASCII only. */
int
Str_CaseCmp(const char* a, const char* b);

/** True when `text` ends with `suffix` (case-insensitive). */
int
Str_HasSuffix(const char* text, const char* suffix);

/** The extension without its dot, or "" — `foo/bar.rs2` -> `rs2`. */
const char*
Str_Extension(const char* path);

/** The last path component. Never NULL. */
const char*
Str_Basename(const char* path);

/* ------------------------------------------------------------------ */
/* URIs                                                                */
/* ------------------------------------------------------------------ */

/** `file:///a/b%20c` -> `/a/b c`. Returns a malloc'd path, or NULL. */
char*
Uri_ToPath(const char* uri);

/** `/a/b c` -> `file:///a/b%20c`. Returns a malloc'd uri. */
char*
Uri_FromPath(const char* path);

/* ------------------------------------------------------------------ */
/* UTF-16 columns                                                      */
/* ------------------------------------------------------------------ */

/**
 * The UTF-16 code-unit count of the first `bytes` bytes of `line`.
 *
 * This is the LSP's idea of a column. Malformed UTF-8 counts as one unit per
 * byte, which keeps a mis-encoded file editable instead of collapsing its
 * columns onto each other.
 */
uint32_t
Utf16_ColumnFromBytes(const char* line, size_t bytes);

/** The inverse: how many bytes into `line` a UTF-16 column lands. */
size_t
Utf16_BytesFromColumn(const char* line, size_t line_bytes, uint32_t column);

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

/** Whole file as a NUL-terminated malloc'd buffer, or NULL. */
char*
File_Read(const char* path, size_t* out_length);

#endif
