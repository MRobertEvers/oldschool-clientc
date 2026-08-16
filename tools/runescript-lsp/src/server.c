/*
 * runescript-lsp — a language server for RuneScript and the content tree.
 *
 * Two tree-sitter grammars provide the structure (tools/tree-sitter-runescript
 * for `.rs2`/`.cs2`, tools/tree-sitter-runeconfig for the declaration files),
 * and `index.c` provides the names. Everything here is the join between them:
 * what is under the cursor, what does it resolve to, and where was it declared.
 *
 * Transport is LSP over stdio. stdout carries protocol messages and nothing
 * else — every diagnostic of our own goes to stderr, which the editor shows in
 * the server's output channel.
 */

#include "doc.h"
#include "index.h"
#include "json.h"
#include "platform.h"
#include "util.h"

#include <tree_sitter/api.h>

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Server state                                                        */
/* ------------------------------------------------------------------ */

struct Settings
{
    /** Report a bare name that resolves to nothing. Off by default: a tree
     *  indexed without its cache pack would light up end to end. */
    int diagnose_unknown_names;
    /** Report `~proc`, `@label`, `^constant`, `%var` and commands that
     *  resolve to nothing. These carry a sigil, so a miss is a real miss. */
    int diagnose_unknown_sigils;
    int diagnose_unknown_locals;
    int max_reference_files;
};

static struct RS_Index g_index;
static struct RS_DocStore g_docs;
static struct Settings g_settings = { 0, 1, 1, 200000 };
static int g_shutdown_requested;
static int g_initialized;

/* ------------------------------------------------------------------ */
/* Semantic token legend                                               */
/* ------------------------------------------------------------------ */

enum
{
    TOK_NAMESPACE = 0,
    TOK_TYPE,
    TOK_CLASS,
    TOK_ENUM,
    TOK_INTERFACE,
    TOK_STRUCT,
    TOK_TYPEPARAM,
    TOK_PARAMETER,
    TOK_VARIABLE,
    TOK_PROPERTY,
    TOK_ENUMMEMBER,
    TOK_EVENT,
    TOK_FUNCTION,
    TOK_METHOD,
    TOK_MACRO,
    TOK_KEYWORD,
    TOK_MODIFIER,
    TOK_COMMENT,
    TOK_STRING,
    TOK_NUMBER,
    TOK_REGEXP,
    TOK_OPERATOR,
    TOK_DECORATOR,
    TOK_TYPE_COUNT
};

static const char* const k_token_types[TOK_TYPE_COUNT] = {
    "namespace", "type",     "class",    "enum",   "interface", "struct",
    "typeParameter", "parameter", "variable", "property", "enumMember", "event",
    "function",  "method",   "macro",    "keyword", "modifier", "comment",
    "string",    "number",   "regexp",   "operator", "decorator"
};

enum
{
    MOD_DECLARATION = 1 << 0,
    MOD_DEFINITION = 1 << 1,
    MOD_READONLY = 1 << 2,
    MOD_STATIC = 1 << 3,
    MOD_DEPRECATED = 1 << 4,
    MOD_ABSTRACT = 1 << 5,
    MOD_ASYNC = 1 << 6,
    MOD_MODIFICATION = 1 << 7,
    MOD_DOCUMENTATION = 1 << 8,
    MOD_DEFAULTLIBRARY = 1 << 9
};

static const char* const k_token_modifiers[] = {
    "declaration", "definition", "readonly", "static",        "deprecated",
    "abstract",    "async",      "modification", "documentation", "defaultLibrary"
};

/* ------------------------------------------------------------------ */
/* Transport                                                           */
/* ------------------------------------------------------------------ */

static void
log_message(const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

static void
send_raw(const char* body, size_t length)
{
    printf("Content-Length: %zu\r\n\r\n", length);
    fwrite(body, 1, length, stdout);
    fflush(stdout);
}

static void
send_buf(struct Buf* buf)
{
    send_raw(buf->data ? buf->data : "", buf->length);
    Buf_Free(buf);
}

/** Start a response envelope; the caller appends the `result` value. */
static void
begin_response(struct Buf* out, const struct JsonValue* id)
{
    Buf_AppendStr(out, "{\"jsonrpc\":\"2.0\",\"id\":");
    if( id && id->kind == JSON_STRING )
        Buf_AppendJsonString(out, id->string);
    else if( id && id->kind == JSON_NUMBER )
        Buf_Printf(out, "%.0f", id->number);
    else
        Buf_AppendStr(out, "null");
    Buf_AppendStr(out, ",\"result\":");
}

static void
send_null_result(const struct JsonValue* id)
{
    struct Buf out = { 0 };

    begin_response(&out, id);
    Buf_AppendStr(&out, "null}");
    send_buf(&out);
}

static void
begin_notification(struct Buf* out, const char* method)
{
    Buf_AppendStr(out, "{\"jsonrpc\":\"2.0\",\"method\":");
    Buf_AppendJsonString(out, method);
    Buf_AppendStr(out, ",\"params\":");
}

/** Show a line in the editor's server output channel. */
static void
notify_log(const char* text)
{
    struct Buf out = { 0 };

    begin_notification(&out, "window/logMessage");
    Buf_AppendStr(&out, "{\"type\":3,\"message\":");
    Buf_AppendJsonString(&out, text);
    Buf_AppendStr(&out, "}}");
    send_buf(&out);
}

/**
 * Read one message body. Returns a malloc'd buffer, or NULL at end of input.
 *
 * Only Content-Length is honoured; Content-Type is read and dropped, which is
 * what the specification says to do with a header you do not act on.
 */
static char*
read_message(size_t* out_length)
{
    char line[1024];
    size_t content_length = 0;
    char* body;
    size_t read = 0;

    for( ;; )
    {
        if( !fgets(line, sizeof(line), stdin) )
            return NULL;
        if( line[0] == '\r' || line[0] == '\n' )
            break;
        if( strncmp(line, "Content-Length:", 15) == 0 )
            content_length = (size_t)strtoul(line + 15, NULL, 10);
    }

    if( content_length == 0 )
        return NULL;

    body = (char*)malloc(content_length + 1);
    assert(body);
    while( read < content_length )
    {
        size_t got = fread(body + read, 1, content_length - read, stdin);

        if( got == 0 )
            break;
        read += got;
    }
    body[read] = '\0';
    *out_length = read;
    return body;
}

/* ------------------------------------------------------------------ */
/* Node helpers                                                        */
/* ------------------------------------------------------------------ */

static int
node_is(TSNode node, const char* type)
{
    if( ts_node_is_null(node) )
        return 0;
    return strcmp(ts_node_type(node), type) == 0;
}

static char*
node_text(const struct RS_Doc* doc, TSNode node)
{
    uint32_t start;
    uint32_t end;

    if( ts_node_is_null(node) )
        return Str_Dup("");
    start = ts_node_start_byte(node);
    end = ts_node_end_byte(node);
    if( end > doc->length )
        end = (uint32_t)doc->length;
    if( start >= end )
        return Str_Dup("");
    return Str_DupN(doc->text + start, end - start);
}

/** The nearest ancestor of that type, or a null node. */
static TSNode
node_ancestor(TSNode node, const char* type)
{
    TSNode current = node;

    while( !ts_node_is_null(current) )
    {
        if( strcmp(ts_node_type(current), type) == 0 )
            return current;
        current = ts_node_parent(current);
    }
    return current;
}

/* ------------------------------------------------------------------ */
/* Resolving what is under the cursor                                  */
/* ------------------------------------------------------------------ */

/** A hint that means "any of the five variable namespaces". */
#define KIND_ANY_VARIABLE ((enum RS_Kind)(RS_KIND_COUNT + 1))

struct RS_Ref
{
    int valid;
    char name[512];
    enum RS_Kind hint;
    int is_local;
    int is_property_key;
    TSNode node;
};

/** Strip a sigil and any leading dot: `~.chatnpc` -> `.chatnpc`, `%x` -> `x`. */
static void
ref_set_name(struct RS_Ref* ref, const char* text, int strip_sigil)
{
    const char* p = text;

    if( strip_sigil && *p )
        p++;
    snprintf(ref->name, sizeof(ref->name), "%s", p);
}

static struct RS_Ref
resolve_ref(const struct RS_Doc* doc, TSNode node)
{
    struct RS_Ref ref;
    const char* type;
    char* text;
    TSNode parent;

    memset(&ref, 0, sizeof(ref));
    ref.hint = RS_KIND_UNKNOWN;
    if( ts_node_is_null(node) )
        return ref;

    type = ts_node_type(node);
    text = node_text(doc, node);
    parent = ts_node_parent(node);
    ref.node = node;

    if( strcmp(type, "local") == 0 )
    {
        ref_set_name(&ref, text, 1);
        ref.is_local = 1;
        ref.hint = RS_KIND_LOCAL;
        ref.valid = 1;
    }
    else if( strcmp(type, "variable") == 0 )
    {
        ref_set_name(&ref, text, 1);
        ref.hint = KIND_ANY_VARIABLE;
        ref.valid = 1;
    }
    else if( strcmp(type, "constant") == 0 )
    {
        ref_set_name(&ref, text, 1);
        ref.hint = RS_KIND_CONSTANT;
        ref.valid = 1;
    }
    else if( strcmp(type, "proc") == 0 )
    {
        ref_set_name(&ref, text, 1);
        ref.hint = RS_KIND_PROC;
        ref.valid = 1;
    }
    else if( strcmp(type, "label") == 0 )
    {
        ref_set_name(&ref, text, 1);
        ref.hint = RS_KIND_LABEL;
        ref.valid = 1;
    }
    else if( strcmp(type, "identifier") == 0 || strcmp(type, "name") == 0 )
    {
        ref_set_name(&ref, text, 0);
        ref.valid = 1;

        if( node_is(parent, "trigger") )
        {
            ref.hint = RS_KIND_TRIGGER;
        }
        else if( node_is(parent, "type") )
        {
            ref.hint = RS_KIND_TYPE;
        }
        else if( node_is(parent, "command_call") || node_is(parent, "bare_command_call") ||
                 node_is(parent, "vararg_command_call") )
        {
            /* Only the call's own name is the command; its arguments are
             * `identifier` nodes with the same parent chain one level down. */
            TSNode name_field = ts_node_child_by_field_name(parent, "name", 4);

            if( ts_node_eq(name_field, node) )
                ref.hint = RS_KIND_COMMAND;
        }
        else if( node_is(parent, "property") )
        {
            TSNode key = ts_node_child_by_field_name(parent, "key", 3);

            if( ts_node_eq(key, node) )
                ref.is_property_key = 1;
        }
        else if( node_is(parent, "record_name") )
        {
            ref.hint = RS_KindForExtension(doc->extension);
        }

        /* A command name written with the secondary-pointer dot resolves to
         * the same command: the dot lives in the operand, not in the name. */
        if( ref.hint == RS_KIND_COMMAND && ref.name[0] == '.' )
            memmove(ref.name, ref.name + 1, strlen(ref.name));
    }
    else if( strcmp(type, "coord") == 0 || strcmp(type, "number") == 0 )
    {
        /* A coord subject is addressed by name, not by key, so it is worth
         * resolving: `[mapzone,0_38_53]` and its siblings are findable. */
        ref_set_name(&ref, text, 0);
        ref.valid = 1;
    }

    free(text);
    return ref;
}

static int
kind_matches(enum RS_Kind symbol_kind, enum RS_Kind hint)
{
    if( hint == RS_KIND_UNKNOWN )
        return 1;
    if( hint == KIND_ANY_VARIABLE )
        return RS_KindIsVariable(symbol_kind);
    if( hint == RS_KIND_COMMAND )
        return symbol_kind == RS_KIND_COMMAND;
    return symbol_kind == hint;
}

/**
 * Every symbol a reference could mean, best first.
 *
 * "Best" is the hint's own kind, then anything else the name is bound to. A
 * bare name that is both an interface and a loc really is both, and the caller
 * shows all of them rather than choosing.
 */
static int
ref_symbols(struct RS_Ref* ref, const struct RS_Symbol** out, int capacity)
{
    int first = 0;
    int count;
    int written = 0;
    int pass;
    int i;

    if( !ref->valid || ref->is_local || ref->is_property_key )
        return 0;

    count = RS_IndexFind(&g_index, ref->name, &first);
    for( pass = 0; pass < 2 && written < capacity; pass++ )
    {
        for( i = 0; i < count && written < capacity; i++ )
        {
            const struct RS_Symbol* symbol = &g_index.symbols[g_index.order[first + i]];
            int matches = kind_matches(symbol->kind, ref->hint);

            if( pass == 0 ? !matches : matches )
                continue;
            /* A trigger script named for its subject is a handler, not the
             * declaration of the name; it is offered after everything else. */
            out[written++] = symbol;
        }
        if( ref->hint == RS_KIND_UNKNOWN )
            break;
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* Locals                                                              */
/* ------------------------------------------------------------------ */

struct LocalDecl
{
    char name[128];
    uint32_t line;
    uint32_t character;
    uint32_t end_character;
    int is_parameter;
    char type[64];
};

struct LocalSet
{
    struct LocalDecl* items;
    int count;
    int capacity;
};

static void
locals_add(
    struct LocalSet* set,
    const struct RS_Doc* doc,
    TSNode name_node,
    TSNode type_node,
    int is_parameter)
{
    struct LocalDecl* decl;
    char* text;
    TSPoint point;

    if( ts_node_is_null(name_node) )
        return;
    if( set->count == set->capacity )
    {
        set->capacity = set->capacity ? set->capacity * 2 : 16;
        set->items = (struct LocalDecl*)realloc(set->items,
                                                (size_t)set->capacity * sizeof(*set->items));
        assert(set->items);
    }
    decl = &set->items[set->count++];
    memset(decl, 0, sizeof(*decl));

    text = node_text(doc, name_node);
    snprintf(decl->name, sizeof(decl->name), "%s", text[0] == '$' ? text + 1 : text);
    free(text);

    point = ts_node_start_point(name_node);
    decl->line = point.row;
    decl->character = point.column;
    decl->end_character = ts_node_end_point(name_node).column;
    decl->is_parameter = is_parameter;

    if( !ts_node_is_null(type_node) )
    {
        text = node_text(doc, type_node);
        snprintf(decl->type, sizeof(decl->type), "%s", text);
        free(text);
    }
}

/**
 * Every local a script declares.
 *
 * The whole script is one scope — `def_int` inside an if-block declares a
 * local the rest of the script reads, which is why this walks the subtree
 * rather than the enclosing blocks.
 */
static void
collect_locals(const struct RS_Doc* doc, TSNode script, struct LocalSet* set)
{
    TSTreeCursor cursor;
    int descend = 1;

    if( ts_node_is_null(script) )
        return;

    cursor = ts_tree_cursor_new(script);
    for( ;; )
    {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        const char* type = ts_node_type(node);

        if( strcmp(type, "parameter") == 0 || strcmp(type, "return_value") == 0 )
        {
            locals_add(set, doc, ts_node_child_by_field_name(node, "name", 4),
                       ts_node_child_by_field_name(node, "type", 4), 1);
        }
        else if( strcmp(type, "declaration") == 0 )
        {
            TSNode keyword = ts_node_child_by_field_name(node, "keyword", 7);
            char* keyword_text = node_text(doc, keyword);
            struct LocalDecl* decl;

            locals_add(set, doc, ts_node_child_by_field_name(node, "name", 4),
                       ts_node_child_by_field_name(node, "type", 4), 0);
            /* `def_int` names the type in the keyword rather than beside it. */
            decl = set->count ? &set->items[set->count - 1] : NULL;
            if( decl && strncmp(keyword_text, "def_", 4) == 0 )
                snprintf(decl->type, sizeof(decl->type), "%s", keyword_text + 4);
            free(keyword_text);
        }

        if( descend && ts_tree_cursor_goto_first_child(&cursor) )
            continue;
        descend = 1;
        while( !ts_tree_cursor_goto_next_sibling(&cursor) )
        {
            if( !ts_tree_cursor_goto_parent(&cursor) )
            {
                ts_tree_cursor_delete(&cursor);
                return;
            }
            if( ts_node_eq(ts_tree_cursor_current_node(&cursor), script) )
            {
                ts_tree_cursor_delete(&cursor);
                return;
            }
        }
    }
}

static const struct LocalDecl*
locals_find(const struct LocalSet* set, const char* name)
{
    int i;

    for( i = 0; i < set->count; i++ )
    {
        if( strcmp(set->items[i].name, name) == 0 )
            return &set->items[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Position formatting                                                 */
/* ------------------------------------------------------------------ */

static void
append_range(struct Buf* out, const struct RS_Doc* doc, TSNode node)
{
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);

    Buf_Printf(out, "{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}",
               start.row, RS_DocUtf16Column(doc, start.row, start.column), end.row,
               RS_DocUtf16Column(doc, end.row, end.column));
}

/**
 * A symbol's range, in a file that may not be open.
 *
 * The index stores byte columns, and the protocol wants UTF-16 ones. For an
 * open document the line table is right there; for a closed one the file is
 * read to convert the single line the symbol sits on, which is what keeps a
 * definition in a comment-heavy file from landing a few characters off.
 */
static void
append_symbol_range(struct Buf* out, const struct RS_Symbol* symbol)
{
    struct RS_Doc* doc = NULL;
    char* uri = symbol->file ? Uri_FromPath(symbol->file) : NULL;
    uint32_t start_character = symbol->character;
    uint32_t end_character = symbol->end_character;

    if( uri )
        doc = RS_DocFind(&g_docs, uri);

    if( doc )
    {
        start_character = RS_DocUtf16Column(doc, symbol->line, symbol->character);
        end_character = RS_DocUtf16Column(doc, symbol->end_line, symbol->end_character);
    }
    else if( symbol->file )
    {
        size_t length = 0;
        char* text = File_Read(symbol->file, &length);

        if( text )
        {
            uint32_t line = 0;
            size_t i = 0;
            size_t line_start = 0;

            while( i < length && line < symbol->end_line )
            {
                if( text[i] == '\n' )
                {
                    line++;
                    line_start = i + 1;
                }
                i++;
            }
            if( line == symbol->line )
                start_character =
                    Utf16_ColumnFromBytes(text + line_start, symbol->character);
            if( line == symbol->end_line )
                end_character = Utf16_ColumnFromBytes(text + line_start, symbol->end_character);
            free(text);
        }
    }

    Buf_Printf(out, "{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}",
               symbol->line, start_character, symbol->end_line, end_character);
    free(uri);
}

static void
append_location(struct Buf* out, const struct RS_Symbol* symbol)
{
    char* uri;

    if( !symbol->file )
        return;
    uri = Uri_FromPath(symbol->file);
    Buf_AppendStr(out, "{\"uri\":");
    Buf_AppendJsonString(out, uri);
    Buf_AppendStr(out, ",\"range\":");
    append_symbol_range(out, symbol);
    Buf_AppendChar(out, '}');
    free(uri);
}

/* ------------------------------------------------------------------ */
/* Hover                                                               */
/* ------------------------------------------------------------------ */

static void
append_symbol_markdown(struct Buf* out, const struct RS_Symbol* symbol)
{
    Buf_Printf(out, "**%s** `%s`", RS_KindName(symbol->kind), symbol->name);
    if( symbol->id >= 0 )
        Buf_Printf(out, " — id %d", symbol->id);
    Buf_Printf(out, "  \n_%s_", RS_OriginName(symbol->origin));
    if( symbol->file )
        Buf_Printf(out, " · `%s:%u`", Str_Basename(symbol->file), symbol->line + 1);
    if( symbol->detail && *symbol->detail )
        Buf_Printf(out, "\n\n```runescript\n%s\n```", symbol->detail);
    if( symbol->doc && *symbol->doc )
        Buf_Printf(out, "\n\n%s", symbol->doc);
}

static void
handle_hover(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc;
    TSNode node;
    struct RS_Ref ref;
    const struct RS_Symbol* symbols[16];
    int count;
    struct Buf out = { 0 };
    struct Buf markdown = { 0 };
    int i;

    doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    if( !doc )
    {
        send_null_result(id);
        return;
    }

    node = RS_DocNodeAt(doc, line, character);
    ref = resolve_ref(doc, node);
    if( !ref.valid )
    {
        send_null_result(id);
        return;
    }

    if( ref.is_local )
    {
        struct LocalSet locals = { 0 };
        const struct LocalDecl* decl;

        collect_locals(doc, node_ancestor(node, "script"), &locals);
        decl = locals_find(&locals, ref.name);
        if( decl )
            Buf_Printf(&markdown, "**%s** `$%s`  \n_%s_",
                       decl->type[0] ? decl->type : "local", decl->name,
                       decl->is_parameter ? "parameter" : "local variable");
        else
            Buf_Printf(&markdown, "`$%s`  \n_undeclared local_", ref.name);
        free(locals.items);
    }
    else if( ref.is_property_key )
    {
        Buf_Printf(&markdown, "**field** `%s`  \n_a `%s` record's property_", ref.name,
                   doc->extension);
    }
    else
    {
        count = ref_symbols(&ref, symbols, 16);
        for( i = 0; i < count; i++ )
        {
            if( i )
                Buf_AppendStr(&markdown, "\n\n---\n\n");
            append_symbol_markdown(&markdown, symbols[i]);
        }
        if( !count )
            Buf_Printf(&markdown, "`%s`  \n_no declaration in the indexed workspace_", ref.name);
    }

    begin_response(&out, id);
    Buf_AppendStr(&out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    Buf_AppendJsonString(&out, markdown.data ? markdown.data : "");
    Buf_AppendStr(&out, "},\"range\":");
    append_range(&out, doc, node);
    Buf_AppendStr(&out, "}}");
    send_buf(&out);
    Buf_Free(&markdown);
}

/* ------------------------------------------------------------------ */
/* Definition                                                          */
/* ------------------------------------------------------------------ */

static void
handle_definition(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    TSNode node;
    struct RS_Ref ref;
    const struct RS_Symbol* symbols[64];
    int count;
    int written = 0;
    int i;
    struct Buf out = { 0 };

    if( !doc )
    {
        send_null_result(id);
        return;
    }

    node = RS_DocNodeAt(doc, line, character);
    ref = resolve_ref(doc, node);
    if( !ref.valid )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    if( ref.is_local )
    {
        struct LocalSet locals = { 0 };
        const struct LocalDecl* decl;

        collect_locals(doc, node_ancestor(node, "script"), &locals);
        decl = locals_find(&locals, ref.name);
        if( decl )
        {
            Buf_AppendStr(&out, "{\"uri\":");
            Buf_AppendJsonString(&out, doc->uri);
            Buf_Printf(&out,
                       ",\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
                       "\"end\":{\"line\":%u,\"character\":%u}}}",
                       decl->line, RS_DocUtf16Column(doc, decl->line, decl->character),
                       decl->line, RS_DocUtf16Column(doc, decl->line, decl->end_character));
            written++;
        }
        free(locals.items);
    }
    else
    {
        count = ref_symbols(&ref, symbols, 64);
        for( i = 0; i < count; i++ )
        {
            if( !symbols[i]->file )
                continue;
            if( written )
                Buf_AppendChar(&out, ',');
            append_location(&out, symbols[i]);
            written++;
        }
    }

    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* References                                                          */
/* ------------------------------------------------------------------ */

/**
 * Every occurrence of `needle` across the indexed files.
 *
 * A literal scan with a name-boundary test rather than a re-parse of the
 * workspace. Names in this language are one token with no scoping, so a
 * boundary-checked substring match is exactly the set of references — and it
 * costs one read of each file rather than one parse.
 */
static void
scan_references(struct Buf* out, const char* needle, const char* sigil, int* written)
{
    size_t needle_length = strlen(needle);
    int file_index;

    for( file_index = 0; file_index < g_index.file_count; file_index++ )
    {
        const char* path = g_index.files[file_index];
        char* uri = NULL;
        struct RS_Doc* doc;
        char* text;
        size_t length = 0;
        int owns_text = 0;
        size_t i;
        uint32_t line = 0;
        size_t line_start = 0;

        uri = Uri_FromPath(path);
        doc = RS_DocFind(&g_docs, uri);
        if( doc )
        {
            text = doc->text;
            length = doc->length;
        }
        else
        {
            text = File_Read(path, &length);
            owns_text = 1;
        }
        if( !text )
        {
            free(uri);
            continue;
        }

        for( i = 0; i + needle_length <= length; i++ )
        {
            char before;
            char after;

            if( text[i] == '\n' )
            {
                line++;
                line_start = i + 1;
                continue;
            }
            if( text[i] != needle[0] || memcmp(text + i, needle, needle_length) != 0 )
                continue;

            before = i > 0 ? text[i - 1] : ' ';
            after = i + needle_length < length ? text[i + needle_length] : ' ';

            /* A name ends where a name character stops. The sigil counts as
             * part of the name it introduces, so `%foo` does not answer a
             * search for the obj `foo`, and `foobar` never answers `foo`. */
            if( isalnum((unsigned char)after) || after == '_' )
                continue;
            if( sigil && *sigil )
            {
                if( before != *sigil )
                    continue;
            }
            else if( isalnum((unsigned char)before) || before == '_' || before == '$' ||
                     before == '%' || before == '^' || before == '~' || before == '@' )
            {
                continue;
            }

            if( *written )
                Buf_AppendChar(out, ',');
            Buf_AppendStr(out, "{\"uri\":");
            Buf_AppendJsonString(out, uri);
            Buf_Printf(out, ",\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
                            "\"end\":{\"line\":%u,\"character\":%u}}}",
                       line, Utf16_ColumnFromBytes(text + line_start, i - line_start), line,
                       Utf16_ColumnFromBytes(text + line_start, i + needle_length - line_start));
            (*written)++;
        }

        if( owns_text )
            free(text);
        free(uri);
    }
}

static void
handle_references(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    TSNode node;
    struct RS_Ref ref;
    struct Buf out = { 0 };
    int written = 0;

    if( !doc )
    {
        send_null_result(id);
        return;
    }

    node = RS_DocNodeAt(doc, line, character);
    ref = resolve_ref(doc, node);
    if( !ref.valid )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    if( ref.is_local )
    {
        /* A local never leaves its script, so the search does not either. */
        TSNode script = node_ancestor(node, "script");
        TSTreeCursor cursor = ts_tree_cursor_new(script);
        int descend = 1;

        for( ;; )
        {
            TSNode current = ts_tree_cursor_current_node(&cursor);

            if( node_is(current, "local") )
            {
                char* text = node_text(doc, current);

                if( strcmp(text + 1, ref.name) == 0 )
                {
                    if( written )
                        Buf_AppendChar(&out, ',');
                    Buf_AppendStr(&out, "{\"uri\":");
                    Buf_AppendJsonString(&out, doc->uri);
                    Buf_AppendStr(&out, ",\"range\":");
                    append_range(&out, doc, current);
                    Buf_AppendChar(&out, '}');
                    written++;
                }
                free(text);
            }

            if( descend && ts_tree_cursor_goto_first_child(&cursor) )
                continue;
            descend = 1;
            while( !ts_tree_cursor_goto_next_sibling(&cursor) )
            {
                if( !ts_tree_cursor_goto_parent(&cursor) ||
                    ts_node_eq(ts_tree_cursor_current_node(&cursor), script) )
                    goto done;
            }
        }
    done:
        ts_tree_cursor_delete(&cursor);
    }
    else
    {
        const char* sigil = NULL;

        switch( ref.hint )
        {
        case RS_KIND_CONSTANT:
            sigil = "^";
            break;
        case RS_KIND_PROC:
            sigil = "~";
            break;
        case RS_KIND_LABEL:
            sigil = "@";
            break;
        default:
            if( ref.hint == KIND_ANY_VARIABLE )
                sigil = "%";
            break;
        }
        scan_references(&out, ref.name, sigil, &written);
    }

    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Semantic tokens                                                     */
/* ------------------------------------------------------------------ */

struct TokenEmitter
{
    struct Buf* out;
    const struct RS_Doc* doc;
    uint32_t previous_line;
    uint32_t previous_character;
    int written;
};

static void
emit_token(
    struct TokenEmitter* emitter,
    uint32_t line,
    uint32_t start_character,
    uint32_t length,
    int type,
    int modifiers)
{
    uint32_t delta_line;
    uint32_t delta_character;

    if( !length )
        return;

    delta_line = line - emitter->previous_line;
    delta_character = delta_line ? start_character : start_character - emitter->previous_character;

    if( emitter->written )
        Buf_AppendChar(emitter->out, ',');
    Buf_Printf(emitter->out, "%u,%u,%u,%d,%d", delta_line, delta_character, length, type,
               modifiers);

    emitter->previous_line = line;
    emitter->previous_character = start_character;
    emitter->written++;
}

/**
 * Emit one node, split at line ends.
 *
 * The protocol has no representation for a token that spans lines, and this
 * corpus has both: block comments, and message strings written across several
 * lines. A single token for those would put every following token on the wrong
 * line for the rest of the file.
 */
static void
emit_node(struct TokenEmitter* emitter, TSNode node, int type, int modifiers)
{
    const struct RS_Doc* doc = emitter->doc;
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    uint32_t line;

    if( type < 0 )
        return;

    if( start.row == end.row )
    {
        uint32_t from = RS_DocUtf16Column(doc, start.row, start.column);
        uint32_t to = RS_DocUtf16Column(doc, start.row, end.column);

        emit_token(emitter, start.row, from, to - from, type, modifiers);
        return;
    }

    for( line = start.row; line <= end.row && line < doc->line_count; line++ )
    {
        uint32_t line_start = doc->line_starts[line];
        uint32_t line_end =
            line + 1 < doc->line_count ? doc->line_starts[line + 1] : (uint32_t)doc->length;
        uint32_t from;
        uint32_t to;

        while( line_end > line_start &&
               (doc->text[line_end - 1] == '\n' || doc->text[line_end - 1] == '\r') )
            line_end--;

        from = line == start.row ? RS_DocUtf16Column(doc, line, start.column) : 0;
        to = line == end.row ? RS_DocUtf16Column(doc, line, end.column)
                             : Utf16_ColumnFromBytes(doc->text + line_start, line_end - line_start);
        if( to > from )
            emit_token(emitter, line, from, to - from, type, modifiers);
    }
}

static int
name_resolves(const char* name, enum RS_Kind hint)
{
    int first = 0;
    int count = RS_IndexFind(&g_index, name, &first);
    int i;

    for( i = 0; i < count; i++ )
    {
        if( kind_matches(g_index.symbols[g_index.order[first + i]].kind, hint) )
            return 1;
    }
    return 0;
}

/** The token class of a leaf in a script document. */
static int
classify_script_leaf(
    const struct RS_Doc* doc,
    TSNode node,
    const struct LocalSet* locals,
    int* out_modifiers)
{
    const char* type = ts_node_type(node);
    TSNode parent = ts_node_parent(node);

    *out_modifiers = 0;

    if( strcmp(type, "comment") == 0 )
        return TOK_COMMENT;
    if( strcmp(type, "string_text") == 0 || strcmp(type, "\"") == 0 )
        return TOK_STRING;
    if( strcmp(type, "interpolation_body") == 0 )
        return TOK_MACRO;
    if( strcmp(type, "number") == 0 || strcmp(type, "coord") == 0 )
        return TOK_NUMBER;
    if( strcmp(type, "def_keyword") == 0 || strcmp(type, "switch_keyword") == 0 )
        return TOK_KEYWORD;

    if( strcmp(type, "if") == 0 || strcmp(type, "else") == 0 || strcmp(type, "while") == 0 ||
        strcmp(type, "return") == 0 || strcmp(type, "case") == 0 ||
        strcmp(type, "default") == 0 || strcmp(type, "calc") == 0 ||
        strcmp(type, "true") == 0 || strcmp(type, "false") == 0 || strcmp(type, "null") == 0 )
        return TOK_KEYWORD;

    if( strcmp(type, "local") == 0 )
    {
        char* text = node_text(doc, node);
        const struct LocalDecl* decl = locals ? locals_find(locals, text + 1) : NULL;
        int is_parameter = decl && decl->is_parameter;

        free(text);
        if( node_is(parent, "declaration") || node_is(parent, "parameter") ||
            node_is(parent, "return_value") )
            *out_modifiers |= MOD_DECLARATION;
        return is_parameter ? TOK_PARAMETER : TOK_VARIABLE;
    }
    if( strcmp(type, "variable") == 0 )
        return TOK_PROPERTY;
    if( strcmp(type, "constant") == 0 )
    {
        *out_modifiers |= MOD_READONLY;
        return TOK_ENUMMEMBER;
    }
    if( strcmp(type, "proc") == 0 )
        return TOK_FUNCTION;
    if( strcmp(type, "label") == 0 )
    {
        *out_modifiers |= MOD_STATIC;
        return TOK_FUNCTION;
    }

    if( strcmp(type, "identifier") == 0 )
    {
        char* text = node_text(doc, node);
        const char* name = text[0] == '.' ? text + 1 : text;
        int result;

        if( node_is(parent, "trigger") )
        {
            *out_modifiers |= MOD_DECLARATION;
            result = TOK_KEYWORD;
        }
        else if( node_is(parent, "subject") )
        {
            *out_modifiers |= MOD_DECLARATION | MOD_DEFINITION;
            result = TOK_FUNCTION;
        }
        else if( node_is(parent, "type") )
        {
            result = TOK_TYPE;
        }
        else if( (node_is(parent, "command_call") || node_is(parent, "bare_command_call") ||
                  node_is(parent, "vararg_command_call")) &&
                 ts_node_eq(ts_node_child_by_field_name(parent, "name", 4), node) )
        {
            /* A command the engine defines is coloured as a library call; one
             * only content declares is an ordinary call. The distinction is
             * what makes a typo'd command name visible before it is compiled. */
            const struct RS_Symbol* symbol = RS_IndexFindKind(&g_index, name, RS_KIND_COMMAND);

            if( symbol && symbol->origin == RS_ORIGIN_BUILTIN )
                *out_modifiers |= MOD_DEFAULTLIBRARY;
            result = TOK_FUNCTION;
        }
        else if( name_resolves(name, RS_KIND_UNKNOWN) )
        {
            *out_modifiers |= MOD_READONLY;
            result = TOK_TYPE;
        }
        else
        {
            result = TOK_VARIABLE;
        }
        free(text);
        return result;
    }

    if( strcmp(type, "=") == 0 || strcmp(type, "!") == 0 || strcmp(type, "<") == 0 ||
        strcmp(type, ">") == 0 || strcmp(type, "<=") == 0 || strcmp(type, ">=") == 0 ||
        strcmp(type, "!=") == 0 || strcmp(type, "&") == 0 || strcmp(type, "|") == 0 ||
        strcmp(type, "+") == 0 || strcmp(type, "-") == 0 || strcmp(type, "*") == 0 ||
        strcmp(type, "/") == 0 || strcmp(type, "%") == 0 )
        return TOK_OPERATOR;

    return -1;
}

static int
classify_config_leaf(const struct RS_Doc* doc, TSNode node, int* out_modifiers)
{
    const char* type = ts_node_type(node);
    TSNode parent = ts_node_parent(node);

    *out_modifiers = 0;

    if( strcmp(type, "comment") == 0 )
        return TOK_COMMENT;
    if( strcmp(type, "section_marker") == 0 )
        return TOK_KEYWORD;
    if( strcmp(type, "text") == 0 )
        return TOK_STRING;
    if( strcmp(type, "number") == 0 || strcmp(type, "coord") == 0 )
        return TOK_NUMBER;
    if( strcmp(type, "constant") == 0 )
    {
        *out_modifiers |= MOD_DECLARATION | MOD_READONLY;
        return TOK_ENUMMEMBER;
    }
    if( strcmp(type, "=") == 0 )
        return TOK_OPERATOR;

    if( strcmp(type, "name") == 0 )
    {
        char* text = node_text(doc, node);
        int result;

        if( node_is(parent, "record_name") )
        {
            *out_modifiers |= MOD_DECLARATION | MOD_DEFINITION;
            result = TOK_TYPE;
        }
        else if( node_is(parent, "property") &&
                 ts_node_eq(ts_node_child_by_field_name(parent, "key", 3), node) )
        {
            result = TOK_PROPERTY;
        }
        else if( node_is(parent, "spawn_row") )
        {
            result = TOK_FUNCTION;
        }
        else if( name_resolves(text, RS_KIND_UNKNOWN) )
        {
            *out_modifiers |= MOD_READONLY;
            result = TOK_TYPE;
        }
        else
        {
            result = TOK_VARIABLE;
        }
        free(text);
        return result;
    }

    return -1;
}

static void
handle_semantic_tokens(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    struct Buf out = { 0 };
    struct TokenEmitter emitter = { 0 };
    struct LocalSet locals = { 0 };
    TSTreeCursor cursor;
    TSNode current_script;
    int descend = 1;

    if( !doc || !doc->tree )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendStr(&out, "{\"data\":[");

    emitter.out = &out;
    emitter.doc = doc;
    memset(&current_script, 0, sizeof(current_script));

    cursor = ts_tree_cursor_new(ts_tree_root_node(doc->tree));
    for( ;; )
    {
        TSNode node = ts_tree_cursor_current_node(&cursor);

        /* Locals are per script, and the classifier needs the set for the
         * script it is inside — recollecting it at each `script` node is what
         * keeps `$x` a parameter in one and a plain local in the next. */
        if( doc->syntax == RS_SYNTAX_SCRIPT && node_is(node, "script") )
        {
            free(locals.items);
            memset(&locals, 0, sizeof(locals));
            collect_locals(doc, node, &locals);
        }

        if( ts_node_child_count(node) == 0 )
        {
            int modifiers = 0;
            int type = doc->syntax == RS_SYNTAX_CONFIG
                           ? classify_config_leaf(doc, node, &modifiers)
                           : classify_script_leaf(doc, node, &locals, &modifiers);

            emit_node(&emitter, node, type, modifiers);
        }

        if( descend && ts_tree_cursor_goto_first_child(&cursor) )
            continue;
        descend = 1;
        while( !ts_tree_cursor_goto_next_sibling(&cursor) )
        {
            if( !ts_tree_cursor_goto_parent(&cursor) )
                goto finished;
        }
    }

finished:
    ts_tree_cursor_delete(&cursor);
    free(locals.items);

    Buf_AppendStr(&out, "]}}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void
append_diagnostic(
    struct Buf* out,
    const struct RS_Doc* doc,
    TSNode node,
    int severity,
    const char* message,
    int* written)
{
    if( *written )
        Buf_AppendChar(out, ',');
    Buf_AppendStr(out, "{\"range\":");
    append_range(out, doc, node);
    Buf_Printf(out, ",\"severity\":%d,\"source\":\"runescript\",\"message\":", severity);
    Buf_AppendJsonString(out, message);
    Buf_AppendChar(out, '}');
    (*written)++;
}

static void
publish_diagnostics(const struct RS_Doc* doc)
{
    struct Buf out = { 0 };
    struct LocalSet locals = { 0 };
    TSTreeCursor cursor;
    int descend = 1;
    int written = 0;

    begin_notification(&out, "textDocument/publishDiagnostics");
    Buf_AppendStr(&out, "{\"uri\":");
    Buf_AppendJsonString(&out, doc->uri);
    Buf_Printf(&out, ",\"version\":%d,\"diagnostics\":[", doc->version);

    if( !doc->tree )
    {
        Buf_AppendStr(&out, "]}}");
        send_buf(&out);
        return;
    }

    cursor = ts_tree_cursor_new(ts_tree_root_node(doc->tree));
    for( ;; )
    {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        const char* type = ts_node_type(node);

        if( ts_node_is_error(node) )
        {
            append_diagnostic(&out, doc, node, 1, "syntax error", &written);
            /* Everything under an error node is salvage, not structure. */
            descend = 0;
        }
        else if( ts_node_is_missing(node) )
        {
            char message[128];

            snprintf(message, sizeof(message), "missing '%s'", type);
            append_diagnostic(&out, doc, node, 1, message, &written);
        }
        else if( doc->syntax == RS_SYNTAX_SCRIPT )
        {
            if( strcmp(type, "script") == 0 )
            {
                free(locals.items);
                memset(&locals, 0, sizeof(locals));
                collect_locals(doc, node, &locals);
            }

            if( g_settings.diagnose_unknown_locals && strcmp(type, "local") == 0 )
            {
                char* text = node_text(doc, node);

                if( !locals_find(&locals, text + 1) )
                {
                    char message[256];

                    snprintf(message, sizeof(message), "'%s' names no local in scope", text);
                    append_diagnostic(&out, doc, node, 2, message, &written);
                }
                free(text);
            }
            else if( g_settings.diagnose_unknown_sigils )
            {
                struct RS_Ref ref = resolve_ref(doc, node);
                const char* what = NULL;

                if( ref.valid && !ref.is_local )
                {
                    switch( ref.hint )
                    {
                    case RS_KIND_CONSTANT:
                        what = "constant";
                        break;
                    case RS_KIND_PROC:
                        what = "proc";
                        break;
                    case RS_KIND_LABEL:
                        what = "label";
                        break;
                    case RS_KIND_TRIGGER:
                        what = "trigger";
                        break;
                    default:
                        if( ref.hint == KIND_ANY_VARIABLE )
                            what = "variable";
                        else if( ref.hint == RS_KIND_COMMAND && ref.name[0] != '_' )
                            what = "command";
                        break;
                    }
                }

                if( what && !name_resolves(ref.name, ref.hint) )
                {
                    char message[512];

                    snprintf(message, sizeof(message), "unknown %s '%s'", what, ref.name);
                    append_diagnostic(&out, doc, node, 2, message, &written);
                }
                else if( !what && g_settings.diagnose_unknown_names && ref.valid &&
                         strcmp(type, "identifier") == 0 && !name_resolves(ref.name, ref.hint) )
                {
                    char message[512];

                    snprintf(message, sizeof(message), "'%s' names nothing in the workspace",
                             ref.name);
                    append_diagnostic(&out, doc, node, 3, message, &written);
                }
            }
        }

        if( descend && ts_tree_cursor_goto_first_child(&cursor) )
            continue;
        descend = 1;
        while( !ts_tree_cursor_goto_next_sibling(&cursor) )
        {
            if( !ts_tree_cursor_goto_parent(&cursor) )
                goto finished;
        }
    }

finished:
    ts_tree_cursor_delete(&cursor);
    free(locals.items);
    Buf_AppendStr(&out, "]}}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Document symbols                                                    */
/* ------------------------------------------------------------------ */

static void
handle_document_symbol(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    struct Buf out = { 0 };
    TSNode root;
    uint32_t i;
    uint32_t count;
    int written = 0;

    if( !doc || !doc->tree )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    root = ts_tree_root_node(doc->tree);
    count = ts_node_child_count(root);
    for( i = 0; i < count; i++ )
    {
        TSNode node = ts_node_child(root, i);
        const char* type = ts_node_type(node);
        char* name = NULL;
        char* detail = NULL;
        int symbol_kind = 12; /* Function */

        if( strcmp(type, "script") == 0 )
        {
            TSNode header = ts_node_child_by_field_name(node, "header", 6);
            TSNode trigger = ts_node_child_by_field_name(header, "trigger", 7);
            TSNode subject = ts_node_child_by_field_name(header, "subject", 7);
            char* trigger_text = node_text(doc, trigger);
            char* subject_text = node_text(doc, subject);
            struct Buf label = { 0 };

            Buf_Printf(&label, "[%s,%s]", trigger_text, subject_text);
            name = label.data;
            detail = trigger_text;
            free(subject_text);
        }
        else if( strcmp(type, "record") == 0 )
        {
            name = node_text(doc, ts_node_child_by_field_name(node, "name", 4));
            detail = Str_Dup(doc->extension);
            symbol_kind = 23; /* Struct */
        }
        else if( strcmp(type, "constant_definition") == 0 )
        {
            name = node_text(doc, ts_node_child_by_field_name(node, "name", 4));
            detail = Str_Dup("constant");
            symbol_kind = 14; /* Constant */
        }
        else
        {
            continue;
        }

        if( written )
            Buf_AppendChar(&out, ',');
        Buf_AppendStr(&out, "{\"name\":");
        Buf_AppendJsonString(&out, name ? name : "");
        Buf_AppendStr(&out, ",\"detail\":");
        Buf_AppendJsonString(&out, detail ? detail : "");
        Buf_Printf(&out, ",\"kind\":%d,\"range\":", symbol_kind);
        append_range(&out, doc, node);
        Buf_AppendStr(&out, ",\"selectionRange\":");
        append_range(&out, doc, node);
        Buf_AppendChar(&out, '}');
        written++;

        free(name);
        free(detail);
    }

    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

static void
handle_workspace_symbol(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* query = Json_String(Json_Get(params, "query"), "");
    struct Buf out = { 0 };
    int written = 0;
    int i;
    size_t query_length = strlen(query);

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    for( i = 0; i < g_index.count && written < 512; i++ )
    {
        const struct RS_Symbol* symbol = &g_index.symbols[i];

        if( !symbol->file )
            continue;
        if( query_length && !strstr(symbol->name, query) )
            continue;
        /* One entry per name per kind is enough for a picker; the several
         * sites a name has are what go-to-definition is for. */
        if( symbol->origin != RS_ORIGIN_RECORD && symbol->origin != RS_ORIGIN_SCRIPT &&
            symbol->origin != RS_ORIGIN_CONSTANT )
            continue;

        if( written )
            Buf_AppendChar(&out, ',');
        Buf_AppendStr(&out, "{\"name\":");
        Buf_AppendJsonString(&out, symbol->name);
        Buf_AppendStr(&out, ",\"containerName\":");
        Buf_AppendJsonString(&out, RS_KindName(symbol->kind));
        Buf_AppendStr(&out, ",\"kind\":12,\"location\":");
        append_location(&out, symbol);
        Buf_AppendChar(&out, '}');
        written++;
    }

    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Completion                                                          */
/* ------------------------------------------------------------------ */

/** LSP CompletionItemKind for a symbol kind. */
static int
completion_kind(enum RS_Kind kind)
{
    switch( kind )
    {
    case RS_KIND_PROC:
    case RS_KIND_LABEL:
    case RS_KIND_TRIGGER_SCRIPT:
    case RS_KIND_CLIENTSCRIPT:
        return 3; /* Function */
    case RS_KIND_COMMAND:
        return 2; /* Method */
    case RS_KIND_CONSTANT:
        return 21; /* Constant */
    case RS_KIND_TRIGGER:
        return 14; /* Keyword */
    case RS_KIND_TYPE:
        return 25; /* TypeParameter */
    case RS_KIND_VARP:
    case RS_KIND_VARBIT:
    case RS_KIND_VARC:
    case RS_KIND_VARN:
    case RS_KIND_VARS:
        return 10; /* Property */
    default:
        return 22; /* Struct */
    }
}

/** The word being typed, ending at the cursor. */
static void
prefix_at(
    const struct RS_Doc* doc,
    uint32_t line,
    uint32_t character,
    char* out,
    size_t capacity,
    char* out_sigil)
{
    uint32_t offset = RS_DocOffset(doc, line, character);
    uint32_t start = offset;

    *out_sigil = '\0';
    while( start > 0 )
    {
        char c = doc->text[start - 1];

        if( isalnum((unsigned char)c) || c == '_' || c == '.' || c == ':' )
        {
            start--;
            continue;
        }
        if( c == '$' || c == '%' || c == '^' || c == '~' || c == '@' )
        {
            *out_sigil = c;
            start--;
        }
        break;
    }

    {
        size_t length = offset - start;

        if( length >= capacity )
            length = capacity - 1;
        memcpy(out, doc->text + start, length);
        out[length] = '\0';
    }
    if( *out_sigil && out[0] )
        memmove(out, out + 1, strlen(out));
}

static void
append_completion_item(
    struct Buf* out,
    const char* label,
    int kind,
    const char* detail,
    const char* documentation,
    int* written)
{
    if( *written )
        Buf_AppendChar(out, ',');
    Buf_AppendStr(out, "{\"label\":");
    Buf_AppendJsonString(out, label);
    Buf_Printf(out, ",\"kind\":%d", kind);
    if( detail && *detail )
    {
        Buf_AppendStr(out, ",\"detail\":");
        Buf_AppendJsonString(out, detail);
    }
    if( documentation && *documentation )
    {
        Buf_AppendStr(out, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        Buf_AppendJsonString(out, documentation);
        Buf_AppendStr(out, "}");
    }
    Buf_AppendChar(out, '}');
    (*written)++;
}

static void
handle_completion(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    char prefix[256];
    char sigil = '\0';
    struct Buf out = { 0 };
    int written = 0;
    enum RS_Kind wanted = RS_KIND_UNKNOWN;
    int32_t matches[2048];
    int match_count;
    int i;

    if( !doc )
    {
        send_null_result(id);
        return;
    }

    prefix_at(doc, line, character, prefix, sizeof(prefix), &sigil);

    begin_response(&out, id);
    Buf_AppendStr(&out, "{\"isIncomplete\":false,\"items\":[");

    switch( sigil )
    {
    case '~':
        wanted = RS_KIND_PROC;
        break;
    case '@':
        wanted = RS_KIND_LABEL;
        break;
    case '^':
        wanted = RS_KIND_CONSTANT;
        break;
    case '%':
        wanted = KIND_ANY_VARIABLE;
        break;
    default:
        break;
    }

    if( sigil == '$' )
    {
        /* Locals only, and only the ones this script declares. */
        TSNode node = RS_DocNodeAt(doc, line, character);
        struct LocalSet locals = { 0 };

        collect_locals(doc, node_ancestor(node, "script"), &locals);
        for( i = 0; i < locals.count; i++ )
        {
            char label[160];

            snprintf(label, sizeof(label), "$%s", locals.items[i].name);
            append_completion_item(&out, label, 6 /* Variable */, locals.items[i].type,
                                   locals.items[i].is_parameter ? "parameter" : "local", &written);
        }
        free(locals.items);
    }
    else if( doc->syntax == RS_SYNTAX_CONFIG && !sigil )
    {
        /* At the start of a line inside a config file the useful set is the
         * keys this file type actually uses, harvested from the tree itself
         * rather than from a table that would go stale. */
        uint32_t offset = RS_DocOffset(doc, line, character);
        uint32_t start = doc->line_starts[line < doc->line_count ? line : 0];
        int at_line_start = 1;
        const char* const* keys = NULL;
        int key_count;

        while( start < offset )
        {
            char c = doc->text[start];

            if( c != ' ' && c != '\t' && !isalnum((unsigned char)c) && c != '_' )
            {
                at_line_start = 0;
                break;
            }
            start++;
        }

        key_count = RS_IndexKeys(&g_index, doc->extension, &keys);
        if( at_line_start )
        {
            for( i = 0; i < key_count && written < 512; i++ )
            {
                if( prefix[0] && strncmp(keys[i], prefix, strlen(prefix)) != 0 )
                    continue;
                append_completion_item(&out, keys[i], 5 /* Field */, doc->extension, NULL,
                                       &written);
            }
        }

        match_count = RS_IndexPrefix(&g_index, prefix, matches, 2048);
        for( i = 0; i < match_count && written < 1024; i++ )
        {
            const struct RS_Symbol* symbol = &g_index.symbols[matches[i]];

            append_completion_item(&out, symbol->name, completion_kind(symbol->kind),
                                   RS_KindName(symbol->kind), symbol->doc, &written);
        }
    }
    else
    {
        char sigil_prefix[2] = { sigil, '\0' };

        match_count = RS_IndexPrefix(&g_index, prefix, matches, 2048);
        for( i = 0; i < match_count && written < 1024; i++ )
        {
            const struct RS_Symbol* symbol = &g_index.symbols[matches[i]];
            char label[544];

            if( wanted != RS_KIND_UNKNOWN && !kind_matches(symbol->kind, wanted) )
                continue;
            if( symbol->name[0] == '[' )
                continue;

            snprintf(label, sizeof(label), "%s%s", sigil_prefix, symbol->name);
            append_completion_item(&out, label, completion_kind(symbol->kind),
                                   symbol->detail ? symbol->detail : RS_KindName(symbol->kind),
                                   symbol->doc, &written);
        }
    }

    Buf_AppendStr(&out, "]}}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Signature help                                                      */
/* ------------------------------------------------------------------ */

static void
handle_signature_help(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    TSNode node;
    TSNode arguments;
    TSNode call;
    TSNode name_node;
    struct RS_Ref ref;
    const struct RS_Symbol* symbols[8];
    int count;
    int active = 0;
    uint32_t offset;
    uint32_t i;
    struct Buf out = { 0 };

    if( !doc || !doc->tree )
    {
        send_null_result(id);
        return;
    }

    node = RS_DocNodeAt(doc, line, character);
    arguments = node_ancestor(node, "argument_list");
    if( ts_node_is_null(arguments) )
    {
        send_null_result(id);
        return;
    }
    call = ts_node_parent(arguments);
    name_node = ts_node_child_by_field_name(call, "name", 4);
    if( ts_node_is_null(name_node) )
    {
        send_null_result(id);
        return;
    }

    /* Which argument the cursor is in: one per comma at the argument list's
     * own depth. Commas inside a nested call belong to that call. */
    offset = RS_DocOffset(doc, line, character);
    for( i = 0; i < ts_node_child_count(arguments); i++ )
    {
        TSNode child = ts_node_child(arguments, i);

        if( ts_node_start_byte(child) >= offset )
            break;
        if( node_is(child, ",") )
            active++;
    }

    ref = resolve_ref(doc, name_node);
    count = ref_symbols(&ref, symbols, 8);
    if( !count )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendStr(&out, "{\"signatures\":[{\"label\":");
    Buf_AppendJsonString(&out, symbols[0]->detail ? symbols[0]->detail : symbols[0]->name);
    if( symbols[0]->doc )
    {
        Buf_AppendStr(&out, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        Buf_AppendJsonString(&out, symbols[0]->doc);
        Buf_AppendChar(&out, '}');
    }
    Buf_Printf(&out, "}],\"activeSignature\":0,\"activeParameter\":%d}}", active);
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Document highlight                                                  */
/* ------------------------------------------------------------------ */

static void
handle_document_highlight(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    uint32_t line = (uint32_t)Json_Number(Json_Path(params, "position", "line", NULL), 0);
    uint32_t character =
        (uint32_t)Json_Number(Json_Path(params, "position", "character", NULL), 0);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    TSNode node;
    struct RS_Ref ref;
    char* needle;
    struct Buf out = { 0 };
    int written = 0;
    TSTreeCursor cursor;
    int descend = 1;

    if( !doc || !doc->tree )
    {
        send_null_result(id);
        return;
    }

    node = RS_DocNodeAt(doc, line, character);
    ref = resolve_ref(doc, node);
    if( !ref.valid )
    {
        send_null_result(id);
        return;
    }
    needle = node_text(doc, node);

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    cursor = ts_tree_cursor_new(ts_tree_root_node(doc->tree));
    for( ;; )
    {
        TSNode current = ts_tree_cursor_current_node(&cursor);

        if( ts_node_child_count(current) == 0 &&
            strcmp(ts_node_type(current), ts_node_type(node)) == 0 )
        {
            char* text = node_text(doc, current);

            if( strcmp(text, needle) == 0 )
            {
                if( written )
                    Buf_AppendChar(&out, ',');
                Buf_AppendStr(&out, "{\"range\":");
                append_range(&out, doc, current);
                Buf_AppendStr(&out, ",\"kind\":1}");
                written++;
            }
            free(text);
        }

        if( descend && ts_tree_cursor_goto_first_child(&cursor) )
            continue;
        descend = 1;
        while( !ts_tree_cursor_goto_next_sibling(&cursor) )
        {
            if( !ts_tree_cursor_goto_parent(&cursor) )
                goto finished;
        }
    }

finished:
    ts_tree_cursor_delete(&cursor);
    free(needle);
    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Folding                                                             */
/* ------------------------------------------------------------------ */

static void
handle_folding_range(const struct JsonValue* id, const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    struct RS_Doc* doc = uri ? RS_DocFind(&g_docs, uri) : NULL;
    struct Buf out = { 0 };
    TSTreeCursor cursor;
    int descend = 1;
    int written = 0;

    if( !doc || !doc->tree )
    {
        send_null_result(id);
        return;
    }

    begin_response(&out, id);
    Buf_AppendChar(&out, '[');

    cursor = ts_tree_cursor_new(ts_tree_root_node(doc->tree));
    for( ;; )
    {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        const char* type = ts_node_type(node);
        int foldable = strcmp(type, "script") == 0 || strcmp(type, "block") == 0 ||
                       strcmp(type, "switch_statement") == 0 ||
                       strcmp(type, "switch_case") == 0 || strcmp(type, "record") == 0;

        if( foldable )
        {
            TSPoint start = ts_node_start_point(node);
            TSPoint end = ts_node_end_point(node);

            if( end.row > start.row )
            {
                if( written )
                    Buf_AppendChar(&out, ',');
                Buf_Printf(&out, "{\"startLine\":%u,\"endLine\":%u}", start.row, end.row - 1);
                written++;
            }
        }

        if( descend && ts_tree_cursor_goto_first_child(&cursor) )
            continue;
        descend = 1;
        while( !ts_tree_cursor_goto_next_sibling(&cursor) )
        {
            if( !ts_tree_cursor_goto_parent(&cursor) )
                goto finished;
        }
    }

finished:
    ts_tree_cursor_delete(&cursor);
    Buf_AppendStr(&out, "]}");
    send_buf(&out);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void
apply_settings(const struct JsonValue* settings)
{
    const struct JsonValue* section = Json_Get(settings, "runescript");
    const struct JsonValue* diagnostics;

    if( !section )
        section = settings;
    diagnostics = Json_Get(section, "diagnostics");
    if( !diagnostics )
        return;

    g_settings.diagnose_unknown_names =
        Json_Bool(Json_Get(diagnostics, "unknownNames"), g_settings.diagnose_unknown_names);
    g_settings.diagnose_unknown_sigils =
        Json_Bool(Json_Get(diagnostics, "unknownSymbols"), g_settings.diagnose_unknown_sigils);
    g_settings.diagnose_unknown_locals =
        Json_Bool(Json_Get(diagnostics, "unknownLocals"), g_settings.diagnose_unknown_locals);
}

static void
handle_initialize(const struct JsonValue* id, const struct JsonValue* params)
{
    struct Buf out = { 0 };
    const struct JsonValue* folders = Json_Get(params, "workspaceFolders");
    const char* root_uri = Json_String(Json_Get(params, "rootUri"), NULL);
    const struct JsonValue* options = Json_Path(params, "initializationOptions", NULL);
    size_t i;
    int added = 0;

    apply_settings(options);

    RS_IndexAddBuiltins(&g_index);

    if( folders && folders->kind == JSON_ARRAY )
    {
        int folder;

        for( folder = 0; folder < folders->count; folder++ )
        {
            const char* uri = Json_String(Json_Get(folders->items[folder], "uri"), NULL);
            char* path = uri ? Uri_ToPath(uri) : NULL;

            if( path )
            {
                RS_IndexAddRoot(&g_index, path);
                added++;
                free(path);
            }
        }
    }
    if( !added && root_uri )
    {
        char* path = Uri_ToPath(root_uri);

        if( path )
        {
            RS_IndexAddRoot(&g_index, path);
            free(path);
        }
    }

    /* Extra content roots, for a tree kept outside the open folder. */
    {
        const struct JsonValue* roots = Json_Get(options, "contentRoots");

        if( roots && roots->kind == JSON_ARRAY )
        {
            for( i = 0; i < (size_t)roots->count; i++ )
            {
                const char* path = Json_String(roots->items[i], NULL);

                if( path )
                    RS_IndexAddRoot(&g_index, path);
            }
        }
    }

    begin_response(&out, id);
    Buf_AppendStr(&out, "{\"capabilities\":{");
    Buf_AppendStr(&out, "\"textDocumentSync\":{\"openClose\":true,\"change\":1,\"save\":{\"includeText\":true}},");
    Buf_AppendStr(&out, "\"hoverProvider\":true,");
    Buf_AppendStr(&out, "\"definitionProvider\":true,");
    Buf_AppendStr(&out, "\"declarationProvider\":true,");
    Buf_AppendStr(&out, "\"referencesProvider\":true,");
    Buf_AppendStr(&out, "\"documentSymbolProvider\":true,");
    Buf_AppendStr(&out, "\"workspaceSymbolProvider\":true,");
    Buf_AppendStr(&out, "\"documentHighlightProvider\":true,");
    Buf_AppendStr(&out, "\"foldingRangeProvider\":true,");
    Buf_AppendStr(&out, "\"completionProvider\":{\"triggerCharacters\":[\"~\",\"@\",\"^\",\"%\",\"$\",\",\",\"(\",\"=\"],\"resolveProvider\":false},");
    Buf_AppendStr(&out, "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},");
    Buf_AppendStr(&out, "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[");
    for( i = 0; i < TOK_TYPE_COUNT; i++ )
    {
        if( i )
            Buf_AppendChar(&out, ',');
        Buf_AppendJsonString(&out, k_token_types[i]);
    }
    Buf_AppendStr(&out, "],\"tokenModifiers\":[");
    for( i = 0; i < sizeof(k_token_modifiers) / sizeof(k_token_modifiers[0]); i++ )
    {
        if( i )
            Buf_AppendChar(&out, ',');
        Buf_AppendJsonString(&out, k_token_modifiers[i]);
    }
    Buf_AppendStr(&out, "]},\"full\":true,\"range\":false}");
    Buf_AppendStr(&out, "},\"serverInfo\":{\"name\":\"runescript-lsp\",\"version\":\"0.1.0\"}}}");
    send_buf(&out);

    {
        char message[256];

        snprintf(message, sizeof(message), "runescript-lsp: indexed %d names from %d files",
                 g_index.count, g_index.file_count);
        notify_log(message);
        log_message("%s", message);
    }
}

static void
handle_did_open(const struct JsonValue* params)
{
    const struct JsonValue* item = Json_Get(params, "textDocument");
    const char* uri = Json_String(Json_Get(item, "uri"), NULL);
    const char* text = Json_String(Json_Get(item, "text"), NULL);
    int version = (int)Json_Number(Json_Get(item, "version"), 0);
    struct RS_Doc* doc;

    if( !uri || !text )
        return;
    doc = RS_DocOpen(&g_docs, uri, text, strlen(text), version);
    publish_diagnostics(doc);
}

static void
handle_did_change(const struct JsonValue* params)
{
    const struct JsonValue* item = Json_Get(params, "textDocument");
    const char* uri = Json_String(Json_Get(item, "uri"), NULL);
    int version = (int)Json_Number(Json_Get(item, "version"), 0);
    const struct JsonValue* changes = Json_Get(params, "contentChanges");
    const char* text;
    struct RS_Doc* doc;

    /* Full sync: `textDocumentSync.change` is 1, so the last change carries
     * the whole document. */
    if( !uri || !changes || changes->count == 0 )
        return;
    text = Json_String(Json_Get(changes->items[changes->count - 1], "text"), NULL);
    if( !text )
        return;

    doc = RS_DocOpen(&g_docs, uri, text, strlen(text), version);
    publish_diagnostics(doc);
}

static void
handle_did_save(const struct JsonValue* params)
{
    const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);
    char* path;

    if( !uri )
        return;
    path = Uri_ToPath(uri);
    if( !path )
        return;

    /* A saved file may have declared or renamed something, and every other
     * open document's resolution depends on it. */
    RS_IndexReloadFile(&g_index, path);
    free(path);

    {
        int i;

        for( i = 0; i < g_docs.count; i++ )
            publish_diagnostics(&g_docs.docs[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void
dispatch(const struct JsonValue* message)
{
    const char* method = Json_String(Json_Get(message, "method"), NULL);
    const struct JsonValue* id = Json_Get(message, "id");
    const struct JsonValue* params = Json_Get(message, "params");

    if( !method )
        return;

    if( strcmp(method, "initialize") == 0 )
    {
        handle_initialize(id, params);
        return;
    }
    if( strcmp(method, "initialized") == 0 )
    {
        g_initialized = 1;
        return;
    }
    if( strcmp(method, "shutdown") == 0 )
    {
        g_shutdown_requested = 1;
        send_null_result(id);
        return;
    }
    if( strcmp(method, "exit") == 0 )
    {
        exit(g_shutdown_requested ? 0 : 1);
    }

    if( strcmp(method, "textDocument/didOpen") == 0 )
    {
        handle_did_open(params);
        return;
    }
    if( strcmp(method, "textDocument/didChange") == 0 )
    {
        handle_did_change(params);
        return;
    }
    if( strcmp(method, "textDocument/didSave") == 0 )
    {
        handle_did_save(params);
        return;
    }
    if( strcmp(method, "textDocument/didClose") == 0 )
    {
        const char* uri = Json_String(Json_Path(params, "textDocument", "uri", NULL), NULL);

        if( uri )
            RS_DocClose(&g_docs, uri);
        return;
    }
    if( strcmp(method, "workspace/didChangeConfiguration") == 0 )
    {
        apply_settings(Json_Get(params, "settings"));
        return;
    }

    if( strcmp(method, "textDocument/hover") == 0 )
    {
        handle_hover(id, params);
        return;
    }
    if( strcmp(method, "textDocument/definition") == 0 ||
        strcmp(method, "textDocument/declaration") == 0 ||
        strcmp(method, "textDocument/typeDefinition") == 0 ||
        strcmp(method, "textDocument/implementation") == 0 )
    {
        handle_definition(id, params);
        return;
    }
    if( strcmp(method, "textDocument/references") == 0 )
    {
        handle_references(id, params);
        return;
    }
    if( strcmp(method, "textDocument/documentSymbol") == 0 )
    {
        handle_document_symbol(id, params);
        return;
    }
    if( strcmp(method, "workspace/symbol") == 0 )
    {
        handle_workspace_symbol(id, params);
        return;
    }
    if( strcmp(method, "textDocument/completion") == 0 )
    {
        handle_completion(id, params);
        return;
    }
    if( strcmp(method, "textDocument/signatureHelp") == 0 )
    {
        handle_signature_help(id, params);
        return;
    }
    if( strcmp(method, "textDocument/documentHighlight") == 0 )
    {
        handle_document_highlight(id, params);
        return;
    }
    if( strcmp(method, "textDocument/foldingRange") == 0 )
    {
        handle_folding_range(id, params);
        return;
    }
    if( strcmp(method, "textDocument/semanticTokens/full") == 0 )
    {
        handle_semantic_tokens(id, params);
        return;
    }

    /* A request we do not answer still needs an answer, or the client waits
     * for one forever. A notification (no id) does not. */
    if( id )
        send_null_result(id);
}

int
main(int argc, char** argv)
{
    int i;

    /* Before anything is read or written: on Windows the C runtime would
     * otherwise translate the LF bytes inside a message body, and the message
     * would no longer be the length its own header claims. */
    Plat_UseBinaryStdio();

    RS_IndexInit(&g_index);
    RS_DocStoreInit(&g_docs);

    /* `runescript-lsp --index <root>` reports what it found and exits, which
     * is how the index is tested without an editor in the loop. */
    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--index") == 0 && i + 1 < argc )
        {
            int j;

            RS_IndexAddBuiltins(&g_index);
            RS_IndexAddRoot(&g_index, argv[++i]);
            printf("%d symbols from %d files\n", g_index.count, g_index.file_count);
            for( j = 0; j < g_index.count && j < 20; j++ )
                printf("  %-32s %-12s %-12s %s\n", g_index.symbols[j].name,
                       RS_KindName(g_index.symbols[j].kind),
                       RS_OriginName(g_index.symbols[j].origin),
                       g_index.symbols[j].file ? Str_Basename(g_index.symbols[j].file) : "");
            RS_IndexFree(&g_index);
            RS_DocStoreFree(&g_docs);
            return 0;
        }
        if( strcmp(argv[i], "--version") == 0 )
        {
            printf("runescript-lsp 0.1.0\n");
            return 0;
        }
    }

    for( ;; )
    {
        size_t length = 0;
        char* body = read_message(&length);
        struct JsonValue* message;

        if( !body )
            break;

        message = Json_Parse(body, length);
        if( message )
        {
            dispatch(message);
            Json_Free(message);
        }
        else
        {
            log_message("runescript-lsp: dropped a malformed message of %zu bytes", length);
        }
        free(body);
    }

    RS_IndexFree(&g_index);
    RS_DocStoreFree(&g_docs);
    return 0;
}
