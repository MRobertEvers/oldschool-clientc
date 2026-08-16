#ifndef TOOLS_RUNESCRIPT_LSP_DOC_H
#define TOOLS_RUNESCRIPT_LSP_DOC_H

/*
 * Open documents: their text, their tree, and the line table that converts
 * between the protocol's positions and tree-sitter's byte offsets.
 */

#include <tree_sitter/api.h>

#include <stddef.h>
#include <stdint.h>

enum RS_Syntax
{
    RS_SYNTAX_NONE = 0,
    RS_SYNTAX_SCRIPT, /**< .rs2 / .cs2 */
    RS_SYNTAX_CONFIG  /**< everything the runeconfig grammar covers */
};

struct RS_Doc
{
    char* uri;
    char* path;
    char* text;
    size_t length;
    int version;
    enum RS_Syntax syntax;
    /** `rs2`, `cs2`, `npc`, ... — the LSP keys several answers off this. */
    char extension[16];

    TSTree* tree;
    uint32_t* line_starts;
    uint32_t line_count;
};

struct RS_DocStore
{
    struct RS_Doc* docs;
    int count;
    int capacity;
    TSParser* script_parser;
    TSParser* config_parser;
};

void
RS_DocStoreInit(struct RS_DocStore* store);

void
RS_DocStoreFree(struct RS_DocStore* store);

/** Opens or replaces the document at `uri`. Never NULL. */
struct RS_Doc*
RS_DocOpen(
    struct RS_DocStore* store,
    const char* uri,
    const char* text,
    size_t length,
    int version);

void
RS_DocClose(struct RS_DocStore* store, const char* uri);

struct RS_Doc*
RS_DocFind(struct RS_DocStore* store, const char* uri);

/** Byte offset of an LSP (line, utf-16 character) position. */
uint32_t
RS_DocOffset(const struct RS_Doc* doc, uint32_t line, uint32_t character);

/** The UTF-16 column of a byte column on `line`. */
uint32_t
RS_DocUtf16Column(const struct RS_Doc* doc, uint32_t line, uint32_t byte_column);

/** The syntax a path's extension selects. */
enum RS_Syntax
RS_SyntaxForPath(const char* path);

/** The named node at a position, or a null node. */
TSNode
RS_DocNodeAt(const struct RS_Doc* doc, uint32_t line, uint32_t character);

#endif
