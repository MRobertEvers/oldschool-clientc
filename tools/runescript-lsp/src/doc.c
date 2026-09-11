#include "doc.h"

#include "index.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TSLanguage*
tree_sitter_runescript(void);

const TSLanguage*
tree_sitter_runeconfig(void);

enum RS_Syntax
RS_SyntaxForPath(const char* path)
{
    const char* extension;

    if( !path )
        return RS_SYNTAX_NONE;

    extension = Str_Extension(path);
    if( strcmp(extension, "rs2") == 0 || strcmp(extension, "cs2") == 0 )
        return RS_SYNTAX_SCRIPT;
    /* The same list the index reads, asked of the index: an editor that
     * opens a file the index skipped would have no names for it anyway. */
    if( RS_IsConfigExtension(extension) )
        return RS_SYNTAX_CONFIG;
    return RS_SYNTAX_NONE;
}

void
RS_DocStoreInit(struct RS_DocStore* store)
{
    assert(store);
    memset(store, 0, sizeof(*store));

    store->script_parser = ts_parser_new();
    assert(store->script_parser);
    ts_parser_set_language(store->script_parser, tree_sitter_runescript());

    store->config_parser = ts_parser_new();
    assert(store->config_parser);
    ts_parser_set_language(store->config_parser, tree_sitter_runeconfig());
}

static void
doc_free(struct RS_Doc* doc)
{
    free(doc->uri);
    free(doc->path);
    free(doc->text);
    free(doc->line_starts);
    if( doc->tree )
        ts_tree_delete(doc->tree);
    memset(doc, 0, sizeof(*doc));
}

void
RS_DocStoreFree(struct RS_DocStore* store)
{
    int i;

    if( !store )
        return;
    for( i = 0; i < store->count; i++ )
        doc_free(&store->docs[i]);
    free(store->docs);
    if( store->script_parser )
        ts_parser_delete(store->script_parser);
    if( store->config_parser )
        ts_parser_delete(store->config_parser);
    memset(store, 0, sizeof(*store));
}

static void
compute_lines(struct RS_Doc* doc)
{
    uint32_t capacity = 256;
    uint32_t count = 0;
    size_t i;

    free(doc->line_starts);
    doc->line_starts = (uint32_t*)malloc(capacity * sizeof(*doc->line_starts));
    assert(doc->line_starts);

    doc->line_starts[count++] = 0;
    for( i = 0; i < doc->length; i++ )
    {
        if( doc->text[i] != '\n' )
            continue;
        if( count == capacity )
        {
            capacity *= 2;
            doc->line_starts =
                (uint32_t*)realloc(doc->line_starts, capacity * sizeof(*doc->line_starts));
            assert(doc->line_starts);
        }
        doc->line_starts[count++] = (uint32_t)(i + 1);
    }
    doc->line_count = count;
}

static void
reparse(struct RS_DocStore* store, struct RS_Doc* doc)
{
    TSParser* parser = doc->syntax == RS_SYNTAX_CONFIG ? store->config_parser
                                                       : store->script_parser;

    if( doc->tree )
    {
        ts_tree_delete(doc->tree);
        doc->tree = NULL;
    }
    if( doc->syntax == RS_SYNTAX_NONE )
        return;
    doc->tree = ts_parser_parse_string(parser, NULL, doc->text, (uint32_t)doc->length);
}

struct RS_Doc*
RS_DocOpen(
    struct RS_DocStore* store,
    const char* uri,
    const char* text,
    size_t length,
    int version)
{
    struct RS_Doc* doc;

    assert(store);
    assert(uri);
    assert(text);

    doc = RS_DocFind(store, uri);
    if( !doc )
    {
        if( store->count == store->capacity )
        {
            store->capacity = store->capacity ? store->capacity * 2 : 8;
            store->docs =
                (struct RS_Doc*)realloc(store->docs, (size_t)store->capacity * sizeof(*store->docs));
            assert(store->docs);
        }
        doc = &store->docs[store->count++];
        memset(doc, 0, sizeof(*doc));
        doc->uri = Str_Dup(uri);
        doc->path = Uri_ToPath(uri);
        doc->syntax = RS_SyntaxForPath(doc->path ? doc->path : uri);
        snprintf(doc->extension, sizeof(doc->extension), "%.15s",
                 Str_Extension(doc->path ? doc->path : uri));
    }

    free(doc->text);
    doc->text = Str_DupN(text, length);
    doc->length = length;
    doc->version = version;

    compute_lines(doc);
    reparse(store, doc);
    return doc;
}

void
RS_DocClose(struct RS_DocStore* store, const char* uri)
{
    int i;

    assert(store);
    assert(uri);
    for( i = 0; i < store->count; i++ )
    {
        if( strcmp(store->docs[i].uri, uri) != 0 )
            continue;
        doc_free(&store->docs[i]);
        store->docs[i] = store->docs[store->count - 1];
        store->count--;
        return;
    }
}

struct RS_Doc*
RS_DocFind(struct RS_DocStore* store, const char* uri)
{
    int i;

    assert(store);
    assert(uri);
    for( i = 0; i < store->count; i++ )
    {
        if( strcmp(store->docs[i].uri, uri) == 0 )
            return &store->docs[i];
    }
    return NULL;
}

uint32_t
RS_DocOffset(const struct RS_Doc* doc, uint32_t line, uint32_t character)
{
    uint32_t start;
    uint32_t end;

    assert(doc);
    if( line >= doc->line_count )
        return (uint32_t)doc->length;

    start = doc->line_starts[line];
    end = line + 1 < doc->line_count ? doc->line_starts[line + 1] : (uint32_t)doc->length;

    return start + (uint32_t)Utf16_BytesFromColumn(doc->text + start, end - start, character);
}

uint32_t
RS_DocUtf16Column(const struct RS_Doc* doc, uint32_t line, uint32_t byte_column)
{
    uint32_t start;

    assert(doc);
    if( line >= doc->line_count )
        return byte_column;
    start = doc->line_starts[line];
    return Utf16_ColumnFromBytes(doc->text + start, byte_column);
}

TSNode
RS_DocNodeAt(const struct RS_Doc* doc, uint32_t line, uint32_t character)
{
    TSNode root;
    uint32_t offset;

    assert(doc);
    if( !doc->tree )
    {
        TSNode null_node;

        memset(&null_node, 0, sizeof(null_node));
        return null_node;
    }

    root = ts_tree_root_node(doc->tree);
    offset = RS_DocOffset(doc, line, character);

    /* A cursor sitting just past the end of a name still means that name —
     * which is where it is when you finish typing one and ask for its
     * definition. `ts_node_descendant_for_byte_range` with a zero-width range
     * at `offset` picks the node starting there instead, so the range is
     * widened backwards by one byte when the character before the cursor is
     * part of a name and the one under it is not. */
    {
        TSNode node = ts_node_named_descendant_for_byte_range(root, offset, offset);

        if( !ts_node_is_null(node) && ts_node_start_byte(node) == offset && offset > 0 )
        {
            TSNode before = ts_node_named_descendant_for_byte_range(root, offset - 1, offset - 1);

            if( !ts_node_is_null(before) && ts_node_end_byte(before) == offset )
                return before;
        }
        return node;
    }
}
