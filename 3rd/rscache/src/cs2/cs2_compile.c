/*
 * CS2 source -> clientscript bytecode.
 *
 * Recursive descent, emitting as it parses. The only fixups are branch
 * operands, which are relative to the instruction after the branch and so are
 * patched once their destination is known.
 *
 * The control-flow shapes below are not a free choice: they are what Jagex's
 * compiler emits (read off real scripts) and therefore what the decompiler's
 * reconstruction recognises. Emitting the equivalent-but-different shape yields
 * a program that runs the same and decompiles to something else.
 *
 *   if (c) { A } else { B }     while (c) { A }        switch (e) { ... }
 *     <c>                         Ltop: <c>              <e>
 *     BRANCH_c -> Lif             BRANCH_c -> Lbody      SWITCH table
 *     <B>                         GOTO Lend              <default>
 *     GOTO Lend                 Lbody: <A>               GOTO Lend
 *   Lif: <A>                      GOTO Ltop            Lcase: <body>
 *   Lend:                       Lend:                    GOTO Lend
 *                                                      Lend:
 *
 * Note the inversion in `if`: the conditional branch targets the taken body and
 * the untaken path falls through immediately after it.
 */

#include "cs2_compile.h"

#include "../rsbuffer.h"
#include "cs2_command.h"
#include "cs2_support.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2_CC_MAX_LOCALS 512
#define CS2_CC_MAX_SWITCHES 256

/* -------------------------------------------------------------------------
 * Tokens
 * ---------------------------------------------------------------------- */

enum cs2_cc_token_kind
{
    CS2_CC_TOK_END = 0,
    CS2_CC_TOK_IDENT,
    CS2_CC_TOK_INT,
    CS2_CC_TOK_STRING,
    CS2_CC_TOK_LOCAL,    /* $name  */
    CS2_CC_TOK_GLOBAL,   /* %name  */
    CS2_CC_TOK_CONSTANT, /* ^name  */
    CS2_CC_TOK_PUNCT,
};

struct cs2_cc_token
{
    enum cs2_cc_token_kind kind;
    /* Arena-owned. For a string literal, the raw span between the quotes. */
    const char* text;
    /**
     * For a string literal, the offset of its first character in the source
     * being lexed. A hook callback needs to re-read its own span with different
     * bracket rules — see cs2_cc_hook — and that is where it starts.
     */
    int start;
    int int_value;
    int line;
    char punct;
    /** Set for `<=`, `>=`; the punct stays '<' or '>'. */
    bool punct_eq;
};

/* -------------------------------------------------------------------------
 * Emitter
 *
 * Instructions go into a growable buffer. A switch compiles its case bodies
 * into detached buffers first, because the bytecode wants the default body
 * immediately after the SWITCH while the source writes it last; the buffers are
 * then concatenated and their outward jumps patched.
 * ---------------------------------------------------------------------- */

struct cs2_cc_ops
{
    uint16_t* opcodes;
    int* int_operands;
    char** string_operands;
    int count;
    int capacity;
};

struct cs2_cc_switch
{
    int* keys;
    int* targets;
    int count;
    int capacity;
    /* Index of the SWITCH instruction itself. Case targets are stored relative
     * to it, so anything that renumbers instructions has to move both. */
    int pc;
};

struct cs2_cc_local
{
    const char* name;
    enum RSCache_CS2_Type type;
    int slot;
    bool is_string;
    bool is_array;
};

struct cs2_cc_compiler
{
    struct RSCache_CS2_Arena arena;
    const struct RSCache_CS2_CompileOptions* options;

    const char* source;
    int position;
    int line;

    struct cs2_cc_token token;
    struct cs2_cc_token ahead;
    bool has_ahead;

    struct cs2_cc_ops ops;

    struct cs2_cc_switch switches[CS2_CC_MAX_SWITCHES];
    int switch_count;

    struct cs2_cc_local locals[CS2_CC_MAX_LOCALS];
    int local_count;

    int int_local_count;
    int string_local_count;
    int int_argument_count;
    int string_argument_count;

    int script_id;
    enum RSCache_CS2_Type return_types[128];
    int return_type_count;
    bool frame_counts_override;
    int override_local_int_count;
    int override_local_string_count;
    int override_int_argument_count;
    int override_string_argument_count;
    bool return_default_override[128];
    int return_default_values[128];

    /*
     * Nesting depth inside a `calc(...)`.
     *
     * Arithmetic is only legal inside `calc`, and the generator writes the
     * wrapper once at the outside: a nested command's own arguments are printed
     * bare, as in `calc(400 - interpolate(0, invpow($int21 - 334, 2), ...))`.
     * So an int argument parsed while inside a calc has to accept operators
     * that would be a syntax error anywhere else — which is what this counts.
     */
    int calc_depth;

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_cc_fail(struct cs2_cc_compiler* cc, const char* fmt, ...)
{
    if( cc->failed )
        return;
    cc->failed = true;
    if( !cc->error || cc->error_capacity <= 0 )
        return;
    char message[400];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    snprintf(cc->error, (size_t)cc->error_capacity, "line %d: %s", cc->line, message);
}

static void
cs2_cc_ops_init(struct cs2_cc_ops* ops)
{
    memset(ops, 0, sizeof(*ops));
}

static void
cs2_cc_ops_free(struct cs2_cc_ops* ops)
{
    for( int i = 0; i < ops->count; i++ )
        free(ops->string_operands[i]);
    free(ops->opcodes);
    free(ops->int_operands);
    free(ops->string_operands);
    memset(ops, 0, sizeof(*ops));
}

static bool
cs2_cc_ops_reserve(struct cs2_cc_ops* ops, int needed)
{
    if( needed <= ops->capacity )
        return true;
    int capacity = ops->capacity ? ops->capacity * 2 : 128;
    while( capacity < needed )
        capacity *= 2;
    uint16_t* opcodes = (uint16_t*)realloc(ops->opcodes, (size_t)capacity * sizeof(uint16_t));
    int* int_operands = (int*)realloc(ops->int_operands, (size_t)capacity * sizeof(int));
    char** string_operands =
        (char**)realloc(ops->string_operands, (size_t)capacity * sizeof(char*));
    if( opcodes )
        ops->opcodes = opcodes;
    if( int_operands )
        ops->int_operands = int_operands;
    if( string_operands )
        ops->string_operands = string_operands;
    if( !opcodes || !int_operands || !string_operands )
        return false;
    for( int i = ops->capacity; i < capacity; i++ )
    {
        ops->opcodes[i] = 0;
        ops->int_operands[i] = 0;
        ops->string_operands[i] = NULL;
    }
    ops->capacity = capacity;
    return true;
}

static int
cs2_cc_emit(struct cs2_cc_compiler* cc, int opcode, int operand)
{
    if( !cs2_cc_ops_reserve(&cc->ops, cc->ops.count + 1) )
    {
        cs2_cc_fail(cc, "out of memory");
        return 0;
    }
    int index = cc->ops.count++;
    cc->ops.opcodes[index] = (uint16_t)opcode;
    cc->ops.int_operands[index] = operand;
    return index;
}

static void
cs2_cc_emit_string(struct cs2_cc_compiler* cc, const char* text)
{
    int index = cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_STRING, 0);
    if( cc->failed )
        return;
    size_t length = strlen(text ? text : "");
    char* copy = (char*)malloc(length + 1);
    if( !copy )
    {
        cs2_cc_fail(cc, "out of memory");
        return;
    }
    memcpy(copy, text ? text : "", length + 1);
    cc->ops.string_operands[index] = copy;
}

/**
 * Discard everything emitted since `mark`.
 *
 * Only used for the one place the grammar needs a speculative parse — telling
 * string interpolation from the game's markup — so it has no jump fixups to
 * undo: a `<...>` fragment is a single expression and cannot branch.
 */
static void
cs2_cc_rollback(struct cs2_cc_compiler* cc, int mark)
{
    for( int i = mark; i < cc->ops.count; i++ )
    {
        free(cc->ops.string_operands[i]);
        cc->ops.string_operands[i] = NULL;
        cc->ops.opcodes[i] = 0;
        cc->ops.int_operands[i] = 0;
    }
    cc->ops.count = mark;
    cc->failed = false;
    if( cc->error && cc->error_capacity > 0 )
        cc->error[0] = '\0';
}

/** Branch operands count from the instruction after the branch. */
static void
cs2_cc_patch(struct cs2_cc_compiler* cc, int jump_index, int target)
{
    if( jump_index >= 0 && jump_index < cc->ops.count )
        cc->ops.int_operands[jump_index] = target - jump_index - 1;
}

/*
 * A set of jump sites waiting on one target.
 *
 * Grown from the compiler's arena rather than sized up front: the count is one
 * per `else if` in a chain or one per case in a switch, and cache.osrs239 has
 * switches with several hundred cases (script 7300 is 1960 instructions over
 * one). A fixed `[256]` refused four scripts outright, in the same way the
 * decoder's switch tables had to stop being `[32]`. Arena-backed, so the early
 * returns scattered through the statement parsers cannot leak it.
 */
struct cs2_cc_jumps
{
    int* items;
    int count;
    int capacity;
};

static void
cs2_cc_jumps_add(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* jumps, int index)
{
    if( jumps->count == jumps->capacity )
    {
        int capacity = jumps->capacity ? jumps->capacity * 2 : 16;
        int* items = (int*)RSCache_CS2_ArenaAlloc(&cc->arena, (size_t)capacity * sizeof(int));
        if( !items )
        {
            cs2_cc_fail(cc, "out of memory");
            return;
        }
        if( jumps->count > 0 )
            memcpy(items, jumps->items, (size_t)jumps->count * sizeof(int));
        jumps->items = items;
        jumps->capacity = capacity;
    }
    jumps->items[jumps->count++] = index;
}

static void
cs2_cc_jumps_patch(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* jumps, int target)
{
    for( int i = 0; i < jumps->count; i++ )
        cs2_cc_patch(cc, jumps->items[i], target);
    jumps->count = 0;
}

/* -------------------------------------------------------------------------
 * Lexer
 * ---------------------------------------------------------------------- */

static bool
cs2_cc_ident_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_';
}

static void
cs2_cc_scan(struct cs2_cc_compiler* cc, struct cs2_cc_token* token)
{
    memset(token, 0, sizeof(*token));

    for( ;; )
    {
        char ch = cc->source[cc->position];
        if( ch == '\0' )
        {
            token->kind = CS2_CC_TOK_END;
            token->line = cc->line;
            return;
        }
        if( ch == '\n' )
        {
            cc->line++;
            cc->position++;
            continue;
        }
        if( isspace((unsigned char)ch) )
        {
            cc->position++;
            continue;
        }
        if( ch == '/' && cc->source[cc->position + 1] == '/' )
        {
            while( cc->source[cc->position] && cc->source[cc->position] != '\n' )
                cc->position++;
            continue;
        }
        break;
    }

    token->line = cc->line;
    char ch = cc->source[cc->position];

    if( ch == '"' )
    {
        /* Interpolation is the parser's business; the token holds the raw
         * span so `<...>` can be re-lexed in place. The scan tracks bracket
         * depth because an interpolated expression may itself contain string
         * literals — `"<~text_device("Click", "Tap")> here"` is one string,
         * not three. */
        int start = ++cc->position;
        int depth = 0;
        while( cc->source[cc->position] )
        {
            char current = cc->source[cc->position];
            if( current == '\\' &&
                (cc->source[cc->position + 1] == '"' ||
                 cc->source[cc->position + 1] == '\\') )
            {
                cc->position += 2;
                continue;
            }
            if( current == '"' && depth == 0 )
                break;
            if( current == '<' )
                depth++;
            else if( current == '>' && depth > 0 )
                depth--;
            else if( current == '\n' )
                cc->line++;
            cc->position++;
        }
        int length = cc->position - start;
        if( cc->source[cc->position] == '"' )
            cc->position++;
        token->kind = CS2_CC_TOK_STRING;
        token->start = start;
        token->text = RSCache_CS2_ArenaStrDupN(&cc->arena, cc->source + start, (size_t)length);
        return;
    }

    if( (ch == '$' || ch == '^' || ch == '%') &&
        cs2_cc_ident_char(cc->source[cc->position + 1]) )
    {
        /* `%` is the global sigil *and* the modulo operator; only a following
         * identifier character makes it a sigil. Without this, `calc($a % 3)`
         * lexes an empty global and the arithmetic falls apart. */
        cc->position++;
        int start = cc->position;
        while( cs2_cc_ident_char(cc->source[cc->position]) )
            cc->position++;
        token->kind = ch == '$' ? CS2_CC_TOK_LOCAL
                                : (ch == '%' ? CS2_CC_TOK_GLOBAL : CS2_CC_TOK_CONSTANT);
        token->text = RSCache_CS2_ArenaStrDupN(
            &cc->arena, cc->source + start, (size_t)(cc->position - start));
        return;
    }

    bool numeric = isdigit((unsigned char)ch) ||
                   (ch == '-' && isdigit((unsigned char)cc->source[cc->position + 1]));
    if( numeric || isalpha((unsigned char)ch) || ch == '_' )
    {
        int start = cc->position;
        if( ch == '-' )
            cc->position++;
        while( cs2_cc_ident_char(cc->source[cc->position]) )
            cc->position++;

        int length = cc->position - start;
        token->text =
            RSCache_CS2_ArenaStrDupN(&cc->arena, cc->source + start, (size_t)length);

        /* A plain number is an int; `0_50_50_1_2` and `coins_995` start like
         * one but carry through into identifier characters, so the whole run is
         * taken and classified afterwards. */
        if( numeric )
        {
            char* end = NULL;
            long value = strtol(token->text, &end, 0);
            if( end && *end == '\0' )
            {
                token->kind = CS2_CC_TOK_INT;
                token->int_value = (int)value;
                return;
            }
        }
        token->kind = CS2_CC_TOK_IDENT;
        return;
    }

    cc->position++;
    token->kind = CS2_CC_TOK_PUNCT;
    token->punct = ch;
    if( (ch == '<' || ch == '>') && cc->source[cc->position] == '=' )
    {
        cc->position++;
        token->punct_eq = true;
    }
}

static void
cs2_cc_next(struct cs2_cc_compiler* cc)
{
    if( cc->has_ahead )
    {
        cc->token = cc->ahead;
        cc->has_ahead = false;
        return;
    }
    cs2_cc_scan(cc, &cc->token);
}

static const struct cs2_cc_token*
cs2_cc_peek(struct cs2_cc_compiler* cc)
{
    if( !cc->has_ahead )
    {
        cs2_cc_scan(cc, &cc->ahead);
        cc->has_ahead = true;
    }
    return &cc->ahead;
}

static bool
cs2_cc_at_punct(struct cs2_cc_compiler* cc, char punct)
{
    return cc->token.kind == CS2_CC_TOK_PUNCT && cc->token.punct == punct && !cc->token.punct_eq;
}

static bool
cs2_cc_accept_punct(struct cs2_cc_compiler* cc, char punct)
{
    if( !cs2_cc_at_punct(cc, punct) )
        return false;
    cs2_cc_next(cc);
    return true;
}

static bool
cs2_cc_expect_punct(struct cs2_cc_compiler* cc, char punct)
{
    if( cs2_cc_accept_punct(cc, punct) )
        return true;
    cs2_cc_fail(cc, "expected '%c'", punct);
    return false;
}

static bool
cs2_cc_at_ident(struct cs2_cc_compiler* cc, const char* text)
{
    return cc->token.kind == CS2_CC_TOK_IDENT && strcmp(cc->token.text, text) == 0;
}

/**
 * Compile a detached fragment of source with the enclosing scope intact.
 *
 * Used for the two places the grammar nests source inside a token: `<...>`
 * interpolation, and a hook's quoted callback.
 */
struct cs2_cc_lexer_state
{
    const char* source;
    int position;
    struct cs2_cc_token token;
    struct cs2_cc_token ahead;
    bool has_ahead;
};

static void
cs2_cc_enter_fragment(struct cs2_cc_compiler* cc, const char* fragment, struct cs2_cc_lexer_state* saved)
{
    saved->source = cc->source;
    saved->position = cc->position;
    saved->token = cc->token;
    saved->ahead = cc->ahead;
    saved->has_ahead = cc->has_ahead;

    cc->source = fragment;
    cc->position = 0;
    cc->has_ahead = false;
    cs2_cc_next(cc);
}

static void
cs2_cc_leave_fragment(struct cs2_cc_compiler* cc, const struct cs2_cc_lexer_state* saved)
{
    cc->source = saved->source;
    cc->position = saved->position;
    cc->token = saved->token;
    cc->ahead = saved->ahead;
    cc->has_ahead = saved->has_ahead;
}

/* -------------------------------------------------------------------------
 * Locals
 * ---------------------------------------------------------------------- */

/**
 * Split a printed local name into its identifier and frame slot.
 *
 * The decompiler writes `<identifier><slot>`, so `$width7` is int slot 7 and
 * `$intarray0` is array slot 0. Recovering the slot from the name is what makes
 * a recompiled script use the same frame layout instead of a renumbered one. No
 * prototype identifier ends in a digit, so the split is unambiguous.
 */
static bool
cs2_cc_split_local_name(const char* name, int* out_slot, bool* out_array)
{
    size_t length = strlen(name);
    size_t digits = 0;
    while( digits < length && isdigit((unsigned char)name[length - 1 - digits]) )
        digits++;
    if( digits == 0 )
        return false;
    *out_slot = atoi(name + length - digits);
    size_t head = length - digits;
    *out_array = head >= 5 && strncmp(name + head - 5, "array", 5) == 0;
    return true;
}

static int
cs2_cc_find_local(struct cs2_cc_compiler* cc, const char* name)
{
    for( int i = 0; i < cc->local_count; i++ )
    {
        if( strcmp(cc->locals[i].name, name) == 0 )
            return i;
    }
    return -1;
}

static int
cs2_cc_declare_local(
    struct cs2_cc_compiler* cc,
    const char* name,
    enum RSCache_CS2_Type type,
    bool is_argument)
{
    int existing = cs2_cc_find_local(cc, name);
    if( existing >= 0 )
        return existing;
    if( cc->local_count == CS2_CC_MAX_LOCALS )
    {
        cs2_cc_fail(cc, "too many local variables");
        return -1;
    }

    int slot = 0;
    bool is_array = false;
    if( !cs2_cc_split_local_name(name, &slot, &is_array) )
    {
        cs2_cc_fail(cc, "local '$%s' carries no slot number", name);
        return -1;
    }

    int index = cc->local_count++;
    cc->locals[index].name = name;
    cc->locals[index].type = type;
    cc->locals[index].slot = slot;
    cc->locals[index].is_array = is_array;
    /*
     * Which bank the name lives in.
     *
     * A plain string local is the obvious case. An array *argument* is the
     * other one: at this revision the elements live in the interpreter's own
     * table (`Statics.field5246`, indexed by the array slot — see opcodes 44/45/
     * 46 in the client), and what a caller actually passes is a handle, pushed
     * with `push_string_local`. Script 465 in cache.osrs239 is the plain
     * example: `[proc,script465](intarray, int, int)` and a trailer that reads
     * `args 2i/1s`, with the recursive call pushing string local 0 first.
     *
     * A locally *defined* array is not an argument and gets no handle: it is
     * reached only through its slot, so it stays where it was.
     */
    cc->locals[index].is_string =
        is_array ? is_argument : type == RSCACHE_CS2_TYPE_STRING;

    if( cc->locals[index].is_string )
    {
        if( slot + 1 > cc->string_local_count )
            cc->string_local_count = slot + 1;
        if( is_argument )
            cc->string_argument_count++;
    }
    else
    {
        if( slot + 1 > cc->int_local_count )
            cc->int_local_count = slot + 1;
        if( is_argument )
            cc->int_argument_count++;
    }
    return index;
}

/**
 * Declare a local the source reads but never assigns.
 *
 * Legal bytecode: a script's locals start zeroed and empty, so `push_string_local
 * 0` with no prior store is an ordinary thing to write, and the decompiler prints
 * it as `$string0`. The compiler used to refuse — 44 of osrs239's own scripts
 * decompiled to source it would not take back.
 *
 * Everything needed is in the printed name. `cs2_cc_split_local_name` already
 * recovers the slot; the identifier before it names the type, which is what says
 * whether the local lives in the int bank or the string bank. Growing the frame
 * to cover the slot is correct rather than generous — the original script's
 * trailer counted it too, or the read would have been out of range.
 */
static int
cs2_cc_declare_local_from_name(struct cs2_cc_compiler* cc, const char* name)
{
    int slot = 0;
    bool is_array = false;
    if( !cs2_cc_split_local_name(name, &slot, &is_array) )
        return -1;

    char identifier[128];
    size_t length = strlen(name);
    size_t digits = 0;
    while( digits < length && isdigit((unsigned char)name[length - 1 - digits]) )
        digits++;
    size_t head = length - digits;
    if( is_array )
        head -= 5; /* drop the "array" suffix; `$intarray0` is an int array */
    if( head == 0 || head >= sizeof(identifier) )
        return -1;
    memcpy(identifier, name, head);
    identifier[head] = '\0';

    enum RSCache_CS2_Type type = RSCache_CS2_TypeOfIdentifier(identifier);
    if( type == RSCACHE_CS2_TYPE_NONE )
        type = RSCACHE_CS2_TYPE_INT;
    return cs2_cc_declare_local(cc, name, type, false);
}

/* -------------------------------------------------------------------------
 * Constant resolution
 * ---------------------------------------------------------------------- */

static const struct RSCache_CS2_Names*
cs2_cc_names(struct cs2_cc_compiler* cc)
{
    return cc->options->names;
}

/** `0_50_50_1_2` -> a packed coord. */
static bool
cs2_cc_parse_coord(const char* text, int* out)
{
    int parts[5];
    const char* cursor = text;
    for( int i = 0; i < 5; i++ )
    {
        char* end = NULL;
        long value = strtol(cursor, &end, 10);
        if( end == cursor )
            return false;
        parts[i] = (int)value;
        cursor = end;
        if( i < 4 )
        {
            if( *cursor != '_' )
                return false;
            cursor++;
        }
    }
    if( *cursor != '\0' )
        return false;
    *out = (parts[0] << 28) | (((parts[1] * 64) + parts[3]) << 14) | ((parts[2] * 64) + parts[4]);
    return true;
}

/** `coins_995` -> 995. The decompiler appends the id for every non-unique type. */
static bool
cs2_cc_parse_id_suffixed(const char* text, int* out)
{
    size_t length = strlen(text);
    size_t digits = 0;
    while( digits < length && isdigit((unsigned char)text[length - 1 - digits]) )
        digits++;
    if( digits == 0 || digits == length )
        return false;
    if( text[length - digits - 1] != '_' )
        return false;
    *out = atoi(text + length - digits);
    return true;
}

static const struct
{
    const char* name;
    int value;
} cs2_cc_colour_names[] = {
    { "red", 0xFF0000 },    { "green", 0x00FF00 },   { "blue", 0x0000FF },
    { "yellow", 0xFFFF00 }, { "magenta", 0xFF00FF }, { "cyan", 0x00FFFF },
    { "white", 0xFFFFFF },  { "black", 0x000000 },
};

/** Resolve a `^name` constant: a colour, an integer extreme, or a named family. */
static bool
cs2_cc_resolve_caret(struct cs2_cc_compiler* cc, const char* name, int* out)
{
    if( strcmp(name, "max_32bit_int") == 0 )
    {
        *out = 2147483647;
        return true;
    }
    if( strcmp(name, "min_32bit_int") == 0 )
    {
        *out = -2147483647 - 1;
        return true;
    }
    for( size_t i = 0; i < sizeof(cs2_cc_colour_names) / sizeof(cs2_cc_colour_names[0]); i++ )
    {
        if( strcmp(name, cs2_cc_colour_names[i].name) == 0 )
        {
            *out = cs2_cc_colour_names[i].value;
            return true;
        }
    }

    /* The families the decompiler writes with a `^prefix_` are searched by
     * stripping that prefix. Two pairs share a prefix (setpos, settextalign)
     * because the horizontal and vertical tables print alike. */
    static const struct
    {
        const char* prefix;
        enum RSCache_CS2_NameTable table;
    } families[] = {
        { "key_", RSCACHE_CS2_NAMES_KEY },
        { "iftype_", RSCACHE_CS2_NAMES_IFTYPE },
        { "setsize_", RSCACHE_CS2_NAMES_SETSIZE },
        { "settextalign_", RSCACHE_CS2_NAMES_SETTEXTALIGNH },
        { "settextalign_", RSCACHE_CS2_NAMES_SETTEXTALIGNV },
        { "setpos_", RSCACHE_CS2_NAMES_SETPOSH },
        { "setpos_", RSCACHE_CS2_NAMES_SETPOSV },
        { "chattype_", RSCACHE_CS2_NAMES_CHATTYPE },
        { "windowmode_", RSCACHE_CS2_NAMES_WINDOWMODE },
        { "clienttype_", RSCACHE_CS2_NAMES_CLIENTTYPE },
        { "chatfilter_", RSCACHE_CS2_NAMES_CHATFILTER },
    };
    for( size_t i = 0; i < sizeof(families) / sizeof(families[0]); i++ )
    {
        size_t prefix_length = strlen(families[i].prefix);
        if( strncmp(name, families[i].prefix, prefix_length) != 0 )
            continue;
        if( RSCache_CS2_NamesLookupId(
                cs2_cc_names(cc), families[i].table, name + prefix_length, out) )
            return true;
    }

    return RSCache_CS2_NamesLookupId(cs2_cc_names(cc), RSCACHE_CS2_NAMES_BOOLEAN, name, out);
}

/**
 * Resolve a bare word to an int under the type the context expects.
 *
 * Context matters: the same spelling names a stat, an inv or an interface
 * depending on which argument it is, and only the expected type says which
 * table to search.
 */
static bool
cs2_cc_resolve_named(
    struct cs2_cc_compiler* cc,
    const char* text,
    enum RSCache_CS2_Type type,
    int* out)
{
    if( strcmp(text, "null") == 0 )
    {
        *out = -1;
        return true;
    }

    enum RSCache_CS2_NameTable table = RSCACHE_CS2_NAMES_TABLE_COUNT;
    switch( type )
    {
    case RSCACHE_CS2_TYPE_COORD:
        return cs2_cc_parse_coord(text, out);

    case RSCACHE_CS2_TYPE_TYPE:
    {
        enum RSCache_CS2_Type referenced = RSCache_CS2_TypeOfLiteral(text);
        if( referenced == RSCACHE_CS2_TYPE_NONE )
            return false;
        *out = RSCache_CS2_TypeDesc(referenced);
        return true;
    }

    /* Exhaustively named: there is no numeric fallback spelling. */
    case RSCACHE_CS2_TYPE_BOOLEAN:
        table = RSCACHE_CS2_NAMES_BOOLEAN;
        break;
    case RSCACHE_CS2_TYPE_STAT:
        table = RSCACHE_CS2_NAMES_STAT;
        break;
    case RSCACHE_CS2_TYPE_MAPAREA:
        table = RSCACHE_CS2_NAMES_MAPAREA;
        break;
    case RSCACHE_CS2_TYPE_FONTMETRICS:
        table = RSCACHE_CS2_NAMES_FONTMETRICS;
        break;

    /* Unique ids: named if the table has one, else `<literal>_<id>`. */
    case RSCACHE_CS2_TYPE_INV:
        table = RSCACHE_CS2_NAMES_INV;
        break;
    case RSCACHE_CS2_TYPE_SYNTH:
        table = RSCACHE_CS2_NAMES_SYNTH;
        break;
    case RSCACHE_CS2_TYPE_PARAM:
        table = RSCACHE_CS2_NAMES_PARAM;
        break;
    case RSCACHE_CS2_TYPE_INTERFACE:
        table = RSCACHE_CS2_NAMES_INTERFACE;
        break;

    default:
        break;
    }

    if( table != RSCACHE_CS2_NAMES_TABLE_COUNT &&
        RSCache_CS2_NamesLookupId(cs2_cc_names(cc), table, text, out) )
        return true;

    /* An id in the spelling settles it outright. */
    if( cs2_cc_parse_id_suffixed(text, out) )
        return true;

    if( type != RSCACHE_CS2_TYPE_NONE )
        return false;

    /* No expectation to go on — a comparison's right-hand side, or a hook
     * argument. Only the tables whose names identify one record are searched,
     * longest-established first, so a bare `true` or `p11_full` resolves
     * without inventing an ambiguity between the non-unique ones. */
    static const enum RSCache_CS2_NameTable unique_tables[] = {
        RSCACHE_CS2_NAMES_BOOLEAN,     RSCACHE_CS2_NAMES_STAT,
        RSCACHE_CS2_NAMES_FONTMETRICS, RSCACHE_CS2_NAMES_MAPAREA,
        RSCACHE_CS2_NAMES_INV,         RSCACHE_CS2_NAMES_SYNTH,
        RSCACHE_CS2_NAMES_PARAM,       RSCACHE_CS2_NAMES_INTERFACE,
    };
    for( size_t i = 0; i < sizeof(unique_tables) / sizeof(unique_tables[0]); i++ )
    {
        if( RSCache_CS2_NamesLookupId(cs2_cc_names(cc), unique_tables[i], text, out) )
            return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

static void
cs2_cc_expression(struct cs2_cc_compiler* cc, enum RSCache_CS2_Type expected);

static void
cs2_cc_calc(struct cs2_cc_compiler* cc, int max_precedence);

/**
 * One argument, anywhere an argument can appear.
 *
 * The generator writes a single `calc(` at the outside of an arithmetic
 * expression and leaves everything nested inside it bare — a command's
 * arguments, a proc's, an `enum`'s, an array's index. So while a `calc` is open
 * an argument has to accept operators that are a syntax error anywhere else.
 * `cs2_cc_command_arguments` had this rule; nothing else did, and the gap
 * refused 21 scripts of cache.osrs239 on a closing parenthesis — one of them
 * `calc($intarray0(1 + $int4) - %varbit14815)`, where it is the array index
 * that is bare.
 */
static void
cs2_cc_argument(struct cs2_cc_compiler* cc, enum RSCache_CS2_Type expected)
{
    if( cc->calc_depth > 0 &&
        (expected == RSCACHE_CS2_TYPE_INT || expected == RSCACHE_CS2_TYPE_NONE) )
        cs2_cc_calc(cc, 6);
    else
        cs2_cc_expression(cc, expected);
}

static void
cs2_cc_statement(struct cs2_cc_compiler* cc);

static void
cs2_cc_block(struct cs2_cc_compiler* cc);

static void
cs2_cc_statements_until(struct cs2_cc_compiler* cc, char stop);

/** Decode a source string literal into the windows-1252 bytes the cache holds. */
static const char*
cs2_cc_decode_string(struct cs2_cc_compiler* cc, const char* utf8, int length)
{
    char* scratch = (char*)RSCache_CS2_ArenaAlloc(&cc->arena, (size_t)length + 1);
    int written = 0;
    for( int i = 0; i < length; i++ )
    {
        if( utf8[i] == '\\' && i + 1 < length &&
            (utf8[i + 1] == '"' || utf8[i + 1] == '\\') )
            i++;
        scratch[written++] = utf8[i];
    }
    scratch[written] = '\0';
    char* out = (char*)RSCache_CS2_ArenaAlloc(&cc->arena, (size_t)length + 1);
    RSCache_Utf8ToCp1252(scratch, out, length + 1);
    return out;
}

/**
 * A string literal, expanding `<expr>` interpolation into a join.
 *
 * Each literal run, each embedded expression **and each markup tag** is one
 * stack value; `join` takes the count.
 *
 * The markup tag being its own value is the part that is not obvious, and it is
 * measured. `"Blue token:<br>"` is two pushes in cache.osrs239, not one, and
 * `"<col=ff9040>Lever</col>"` is three. Script 8 builds its message from 22
 * pieces where merging the tags into their neighbouring text gives 15, which is
 * why the rebuild came out 90 instructions shorter than the cache's own.
 *
 * Treating a tag as ordinary text — which is what it *is*, semantically; the
 * client concatenates and renders the lot — was the natural reading and is
 * wrong about the bytes. 309 scripts.
 */
static void
cs2_cc_string_literal(struct cs2_cc_compiler* cc, const char* body)
{
    if( !strchr(body, '<') )
    {
        cs2_cc_emit_string(cc, cs2_cc_decode_string(cc, body, (int)strlen(body)));
        return;
    }

    int parts = 0;
    int start = 0;
    int index = 0;
    for( ;; )
    {
        char ch = body[index];
        if( ch != '\0' && ch != '<' )
        {
            index++;
            continue;
        }
        /* Marked before the pending literal run, not after: the interpolation
         * attempt below emits into the same instruction list and has to be
         * unwound without disturbing the run. Rolling back only the expression
         * left the run emitted twice, which is how `"<col=ff9040>Tutors</col>"`
         * came back with its text duplicated. */
        int mark = cc->ops.count;
        int run_length = index - start;
        int tag_start = index;
        if( run_length > 0 )
        {
            cs2_cc_emit_string(cc, cs2_cc_decode_string(cc, body + start, run_length));
            parts++;
        }
        if( ch == '\0' )
            break;

        int depth = 1;
        int expression_start = ++index;
        while( body[index] && depth > 0 )
        {
            if( body[index] == '<' )
                depth++;
            else if( body[index] == '>' && --depth == 0 )
                break;
            index++;
        }
        if( body[index] != '>' )
        {
            cs2_cc_fail(cc, "unterminated <...> in a string literal");
            return;
        }

        /* `<...>` is ambiguous in this language: it is string interpolation,
         * and it is also the game's own markup — `"Level <tostring($lvl)>"` and
         * `"red<col=ff0000>"` are both ordinary strings. The decompiler writes
         * markup through verbatim, so the only way to tell them apart is to try
         * the interpolation reading and fall back to literal text when it does
         * not parse. The attempt is rolled back so nothing it emitted survives. */
        struct cs2_cc_lexer_state saved;
        cs2_cc_enter_fragment(
            cc,
            RSCache_CS2_ArenaStrDupN(
                &cc->arena, body + expression_start, (size_t)(index - expression_start)),
            &saved);
        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
        bool parsed = !cc->failed && cc->token.kind == CS2_CC_TOK_END;
        cs2_cc_leave_fragment(cc, &saved);

        if( parsed )
        {
            parts++;
        }
        else
        {
            /* Markup. Unwind the failed expression *and* the run, then re-emit
             * the run and push the tag beside it as a second value. Two pushes
             * where the text reads as one string, because that is what the
             * cache holds. */
            cs2_cc_rollback(cc, mark);
            parts -= (run_length > 0) ? 1 : 0;
            if( run_length > 0 )
            {
                cs2_cc_emit_string(cc, cs2_cc_decode_string(cc, body + start, run_length));
                parts++;
            }
            cs2_cc_emit_string(
                cc, cs2_cc_decode_string(cc, body + tag_start, index + 1 - tag_start));
            parts++;
            index++;
            start = index;
            continue;
        }

        index++;
        start = index;
    }

    if( parts == 0 )
        cs2_cc_emit_string(cc, "");
    else if( parts > 1 )
        cs2_cc_emit(cc, RSCACHE_CS2_OP_JOIN_STRING, parts);
}

/** `%var12`, `%varbit7`, … — the prefix names the kind, the suffix the id. */
static bool
cs2_cc_global_kind(const char* name, int* out_push, int* out_pop, int* out_id)
{
    static const struct
    {
        const char* prefix;
        int push;
        int pop;
    } kinds[] = {
        /* Longest first: `varcstring` must beat `varc`, and `varbit` beat `var`. */
        { "varclansetting", RSCACHE_CS2_OP_PUSH_VARCLANSETTING, -1 },
        { "varcstring", RSCACHE_CS2_OP_PUSH_VARC_STRING, RSCACHE_CS2_OP_POP_VARC_STRING },
        { "varclan", RSCACHE_CS2_OP_PUSH_VARCLAN, -1 },
        { "varcint", RSCACHE_CS2_OP_PUSH_VARC_INT, RSCACHE_CS2_OP_POP_VARC_INT },
        { "varbit", RSCACHE_CS2_OP_PUSH_VARBIT, RSCACHE_CS2_OP_POP_VARBIT },
        { "var", RSCACHE_CS2_OP_PUSH_VAR, RSCACHE_CS2_OP_POP_VAR },
    };
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
    {
        size_t length = strlen(kinds[i].prefix);
        if( strncmp(name, kinds[i].prefix, length) != 0 )
            continue;
        const char* digits = name + length;
        if( !*digits )
            continue;
        for( const char* cursor = digits; *cursor; cursor++ )
        {
            if( !isdigit((unsigned char)*cursor) )
                return false;
        }
        *out_push = kinds[i].push;
        *out_pop = kinds[i].pop;
        *out_id = atoi(digits);
        return true;
    }
    return false;
}

/**
 * A script name at a call site: `min`, or `script1046` for an unnamed one.
 *
 * `required` matters: names repeat across triggers, so a proc call and a hook
 * callback spelled the same are different scripts.
 */
static bool
cs2_cc_resolve_script(
    struct cs2_cc_compiler* cc,
    const char* name,
    enum RSCache_CS2_Trigger required,
    int* out_id)
{
    if( strncmp(name, "script", 6) == 0 && isdigit((unsigned char)name[6]) )
    {
        *out_id = atoi(name + 6);
        return true;
    }
    return RSCache_CS2_NamesScriptId(cs2_cc_names(cc), name, required, out_id);
}

static void
cs2_cc_proc_call(struct cs2_cc_compiler* cc)
{
    if( cc->token.kind != CS2_CC_TOK_IDENT )
    {
        cs2_cc_fail(cc, "expected a procedure name after '~'");
        return;
    }
    const char* name = cc->token.text;
    cs2_cc_next(cc);

    int callee_id = -1;
    if( !cs2_cc_resolve_script(cc, name, RSCACHE_CS2_TRIGGER_PROC, &callee_id) )
    {
        cs2_cc_fail(cc, "no proc with the name '%s'", name);
        return;
    }

    if( cs2_cc_accept_punct(cc, '(') )
    {
        while( !cs2_cc_at_punct(cc, ')') && !cc->failed && cc->token.kind != CS2_CC_TOK_END )
        {
            /* Same rule as a command's arguments: the generator writes one
             * `calc(` at the outside and leaves everything nested inside it
             * bare, so `calc($a - ~proc(0, $b - $c))` reaches here with the
             * inner subtraction unwrapped. `cs2_cc_command_arguments` already
             * allowed for that; a proc call did not, and refused 18 scripts of
             * cache.osrs239 on the closing parenthesis. */
            cs2_cc_argument(cc, RSCACHE_CS2_TYPE_NONE);
            if( !cs2_cc_accept_punct(cc, ',') )
                break;
        }
        cs2_cc_expect_punct(cc, ')');
    }
    cs2_cc_emit(cc, RSCACHE_CS2_OP_GOSUB_WITH_PARAMS, callee_id);
}

static bool
cs2_cc_call_argument_word(struct cs2_cc_compiler* cc, int index, char* out, int capacity);

/**
 * The type an expression will push, as far as the source can say.
 *
 * Needed only for a hook's argument descriptor, which names each argument's
 * type and is not written out in the source. A local answers exactly (its
 * declaration carries the type); a literal answers from its spelling. Where
 * neither settles it the caller reports rather than guesses, because a wrong
 * descriptor letter changes how the *callee* reads its own arguments.
 */
static enum RSCache_CS2_Type
cs2_cc_infer_type(struct cs2_cc_compiler* cc, const char* text, enum cs2_cc_token_kind kind)
{
    switch( kind )
    {
    case CS2_CC_TOK_INT:
        return RSCACHE_CS2_TYPE_INT;
    case CS2_CC_TOK_STRING:
        return RSCACHE_CS2_TYPE_STRING;
    case CS2_CC_TOK_CONSTANT:
        return RSCACHE_CS2_TYPE_INT;
    case CS2_CC_TOK_LOCAL:
    {
        int index = cs2_cc_find_local(cc, text);
        /* A callback can be the first mention of a local — the hook is written
         * before the statement that assigns it, or the script never assigns it
         * at all. The printed name carries the type either way, which is the
         * same thing `cs2_cc_expression` relies on. */
        if( index < 0 )
            index = cs2_cc_declare_local_from_name(cc, text);
        return index >= 0 ? cc->locals[index].type : RSCACHE_CS2_TYPE_NONE;
    }
    case CS2_CC_TOK_IDENT:
    {
        /* `null` is -1 on the int stack whatever type it stands for. A string
         * argument is never spelled this way — an empty one is `""` — so there
         * is no ambiguity about which bank it comes from. */
        if( strcmp(text, "null") == 0 )
            return RSCACHE_CS2_TYPE_INT;

        /* `calc(...)` is arithmetic and arithmetic is int. It is not a command,
         * so nothing below claims it, and a callback argument written as one
         * (`script221(event_com, event_comsubid, $int3, calc(clientclock + 10))`)
         * was the single largest remaining reason a hook would not compile. */
        if( strcmp(text, "calc") == 0 )
            return RSCACHE_CS2_TYPE_INT;

        /* A command answers with what it pushes. */
        int opcode = RSCache_CS2_CommandOfName(text);
        const struct RSCache_CS2_CommandInfo* info =
            opcode >= 0 ? RSCache_CS2_CommandGet(opcode) : NULL;
        if( info &&
            (info->kind == RSCACHE_CS2_CMD_BASIC ||
             info->kind == RSCACHE_CS2_CMD_TYPED_POP) &&
            info->def_count > 0 )
        {
            const struct RSCache_CS2_Prototype* proto =
                RSCache_CS2_ProtoGet(RSCache_CS2_CommandDef(info, 0));
            /* An opcode solved by arity rather than by meaning pushes
             * `UNKNOWNINT`: no type, but a known bank, and its identifier is
             * what names that bank. A descriptor letter only records the bank
             * at this revision, so that is enough — and it is the difference
             * between `_7253` being writable in a callback and not. */
            if( proto->type == RSCACHE_CS2_TYPE_NONE && proto->identifier )
                return RSCache_CS2_TypeOfIdentifier(proto->identifier);
            return proto->type;
        }

        /* An `event_*` value has a fixed type. */
        for( int i = 0; i < RSCACHE_CS2_EVENT_COUNT; i++ )
        {
            if( strcmp(text, RSCache_CS2_EventPropertyLiteral(
                                 (enum RSCache_CS2_EventProperty)i)) == 0 )
                return RSCache_CS2_ProtoGet(
                           RSCache_CS2_EventPropertyProto((enum RSCache_CS2_EventProperty)i))
                    ->type;
        }

        /* Otherwise a named constant, if one of the tables whose names identify
         * a single record claims it. The non-unique types (obj, loc, npc, …)
         * are deliberately not searched: they all print as `<name>_<id>` and
         * would answer for each other. */
        static const struct
        {
            enum RSCache_CS2_NameTable table;
            enum RSCache_CS2_Type type;
        } unique[] = {
            { RSCACHE_CS2_NAMES_BOOLEAN, RSCACHE_CS2_TYPE_BOOLEAN },
            { RSCACHE_CS2_NAMES_STAT, RSCACHE_CS2_TYPE_STAT },
            { RSCACHE_CS2_NAMES_FONTMETRICS, RSCACHE_CS2_TYPE_FONTMETRICS },
            { RSCACHE_CS2_NAMES_MAPAREA, RSCACHE_CS2_TYPE_MAPAREA },
            { RSCACHE_CS2_NAMES_INV, RSCACHE_CS2_TYPE_INV },
            { RSCACHE_CS2_NAMES_SYNTH, RSCACHE_CS2_TYPE_SYNTH },
            { RSCACHE_CS2_NAMES_PARAM, RSCACHE_CS2_TYPE_PARAM },
        };
        int unused = 0;
        for( size_t i = 0; i < sizeof(unique) / sizeof(unique[0]); i++ )
        {
            if( RSCache_CS2_NamesLookupId(cs2_cc_names(cc), unique[i].table, text, &unused) )
                return unique[i].type;
        }
        int coord = 0;
        if( cs2_cc_parse_coord(text, &coord) )
            return RSCACHE_CS2_TYPE_COORD;

        /* `coins_995` and friends: the name is ambiguous between obj, loc, npc,
         * model, struct and seq, which is why the unique tables above skip them
         * — but every one of those is an *int*, and int is all a descriptor
         * letter has to get right. The client reads the letter to know which
         * stack an argument came off; among the int types the choice is a
         * naming hint. Saying so recovers a large share of the hooks that used
         * to refuse. */
        int suffixed = 0;
        if( cs2_cc_parse_id_suffixed(text, &suffixed) )
            return RSCACHE_CS2_TYPE_INT;
        return RSCACHE_CS2_TYPE_NONE;
    }
    case CS2_CC_TOK_GLOBAL:
    {
        int push = 0;
        int pop = 0;
        int id = 0;
        if( !cs2_cc_global_kind(text, &push, &pop, &id) )
            return RSCACHE_CS2_TYPE_NONE;
        return push == RSCACHE_CS2_OP_PUSH_VARC_STRING ? RSCACHE_CS2_TYPE_STRING
                                                       : RSCACHE_CS2_TYPE_INT;
    }
    default:
        return RSCACHE_CS2_TYPE_NONE;
    }
}

/**
 * The type of a callback argument, including the two commands whose result type
 * is a property of their own arguments rather than of the opcode.
 *
 * `cs2_cc_infer_type` answers from one token, which is enough for a local or a
 * literal and not enough for these:
 *
 *   enum(int, string, $enum6, $key)    pushes the *second* type literal
 *   struct_param($struct0, param_1279) pushes whatever param 1279 is declared
 *
 * Both are ordinary things to write in a hook — `script526(…, enum(…), …)`
 * alone accounts for six scripts in cache.osrs239 — and neither can be guessed
 * from the command name, so this reads ahead for the argument that decides and
 * rewinds. Reading ahead rather than compiling: the descriptor has to be
 * written before the argument is emitted.
 */
static enum RSCache_CS2_Type
cs2_cc_infer_call_type(struct cs2_cc_compiler* cc)
{
    if( cc->token.kind != CS2_CC_TOK_IDENT )
        return cs2_cc_infer_type(cc, cc->token.text, cc->token.kind);

    int opcode = RSCache_CS2_CommandOfName(cc->token.text);
    const struct RSCache_CS2_CommandInfo* info =
        opcode >= 0 ? RSCache_CS2_CommandGet(opcode) : NULL;
    if( !info ||
        (info->kind != RSCACHE_CS2_CMD_ENUM && info->kind != RSCACHE_CS2_CMD_PARAM &&
         info->kind != RSCACHE_CS2_CMD_ACTIVE_PARAM &&
         info->kind != RSCACHE_CS2_CMD_COMPONENT_PARAM &&
         info->kind != RSCACHE_CS2_CMD_DB_GETFIELD) )
        return cs2_cc_infer_type(cc, cc->token.text, cc->token.kind);

    int position = cc->position;
    int line = cc->line;
    struct cs2_cc_token token = cc->token;
    struct cs2_cc_token ahead = cc->ahead;
    bool has_ahead = cc->has_ahead;

    enum RSCache_CS2_Type result = RSCACHE_CS2_TYPE_NONE;
    cs2_cc_next(cc);
    if( cs2_cc_accept_punct(cc, '(') )
    {
        if( info->kind == RSCACHE_CS2_CMD_ENUM )
        {
            /* enum(<input type>, <output type>, …) — the second one. */
            if( cc->token.kind == CS2_CC_TOK_IDENT )
            {
                cs2_cc_next(cc);
                if( cs2_cc_accept_punct(cc, ',') && cc->token.kind == CS2_CC_TOK_IDENT )
                    result = RSCache_CS2_TypeOfLiteral(cc->token.text);
            }
        }
        else if( info->kind == RSCACHE_CS2_CMD_DB_GETFIELD && cc->options &&
                 cc->options->db_columns.load )
        {
            char literal[64];
            int scan_position = cc->position;
            cc->position = position;
            bool have_literal =
                cs2_cc_call_argument_word(cc, 1, literal, (int)sizeof(literal));
            cc->position = scan_position;
            if( have_literal )
            {
                char* end = NULL;
                long packed_long = strtol(literal, &end, 0);
                if( end && *end == '\0' )
                {
                    int packed = (int)packed_long;
                    int table_id = (packed >> 12) & 0xFFFFF;
                    int column_id = (packed >> 4) & 0xFF;
                    int field = (packed & 0xF) - 1;
                    enum RSCache_CS2_Type types[32];
                    int count = cc->options->db_columns.load(
                        cc->options->db_columns.user, table_id, column_id, types, 32);
                    if( field >= 0 && field < count )
                        result = types[field];
                    else if( field < 0 && count == 1 )
                        result = types[0];
                }
            }
        }
        else if( (info->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ||
                  info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM) && cc->options &&
                 cc->options->param_types.load )
        {
            /* cc_getcomponentparam(<param>) — the active component is implicit. */
            if( cc->token.kind == CS2_CC_TOK_IDENT )
            {
                int param_id = -1;
                if( RSCache_CS2_NamesLookupId(
                        cs2_cc_names(cc), RSCACHE_CS2_NAMES_PARAM, cc->token.text,
                        &param_id) ||
                    cs2_cc_parse_id_suffixed(cc->token.text, &param_id) )
                    result = cc->options->param_types.load(
                        cc->options->param_types.user, param_id);
            }
        }
        else if( cc->options && cc->options->param_types.load )
        {
            /* <receiver>_param(<receiver>, <param>) — skip to the param, which
             * may itself be a parenthesised expression. */
            int depth = 0;
            while( cc->token.kind != CS2_CC_TOK_END && !cc->failed )
            {
                if( cs2_cc_at_punct(cc, '(') )
                    depth++;
                else if( cs2_cc_at_punct(cc, ')') && depth > 0 )
                    depth--;
                else if( cs2_cc_at_punct(cc, ',') && depth == 0 )
                    break;
                cs2_cc_next(cc);
            }
            if( cs2_cc_accept_punct(cc, ',') && cc->token.kind == CS2_CC_TOK_IDENT )
            {
                int param_id = -1;
                if( RSCache_CS2_NamesLookupId(
                        cs2_cc_names(cc), RSCACHE_CS2_NAMES_PARAM, cc->token.text,
                        &param_id) ||
                    cs2_cc_parse_id_suffixed(cc->token.text, &param_id) )
                    result = cc->options->param_types.load(
                        cc->options->param_types.user, param_id);
            }
        }
    }

    cc->position = position;
    cc->line = line;
    cc->token = token;
    cc->ahead = ahead;
    cc->has_ahead = has_ahead;
    cc->failed = false;
    if( cc->error && cc->error_capacity > 0 )
        cc->error[0] = '\0';

    if( result != RSCACHE_CS2_TYPE_NONE )
        return result;
    return cs2_cc_infer_type(cc, cc->token.text, cc->token.kind);
}

static void
cs2_cc_command_arguments(
    struct cs2_cc_compiler* cc,
    const char* name,
    const struct RSCache_CS2_CommandInfo* info)
{
    if( info->arg_count == 0 )
    {
        /* Parentheses are optional on a command that takes nothing. */
        if( cs2_cc_accept_punct(cc, '(') )
            cs2_cc_expect_punct(cc, ')');
        return;
    }
    if( !cs2_cc_expect_punct(cc, '(') )
        return;

    /* Argument *slots* are fixed by the signature, but one command returning
     * several values can fill several of them, so arguments are consumed until
     * the closing parenthesis rather than counted. The declared prototype still
     * types each written argument, which is what tells a quoted graphic from a
     * quoted string. */
    int written = 0;
    while( !cc->failed && !cs2_cc_at_punct(cc, ')') && cc->token.kind != CS2_CC_TOK_END )
    {
        if( written && !cs2_cc_expect_punct(cc, ',') )
            return;
        enum RSCache_CS2_Type expected = RSCACHE_CS2_TYPE_NONE;
        if( written < info->arg_count )
        {
            enum RSCache_CS2_ProtoId proto = RSCache_CS2_CommandArg(info, written);
            if( proto != RSCACHE_CS2_PROTO_NONE )
                expected = RSCache_CS2_ProtoGet(proto)->type;
        }
        /* Only where the argument could *be* arithmetic. `cs2_cc_calc` types its
         * leaf `int`, which loses any narrower expectation — and a narrower
         * expectation is what resolves a named constant. `parawidth($string0,
         * 400, p11_full)` inside a calc took this path and then could not
         * resolve `p11_full`, because only the argument's declared
         * `fontmetrics` says which name table to search. Invisible without name
         * tables loaded, and 166 scripts with them. */
        if( cc->calc_depth > 0 &&
            (expected == RSCACHE_CS2_TYPE_INT || expected == RSCACHE_CS2_TYPE_NONE) )
            cs2_cc_calc(cc, 6);
        else
            cs2_cc_expression(cc, expected);
        written++;
    }
    if( !cc->failed && !cs2_cc_at_punct(cc, ')') )
    {
        cs2_cc_fail(cc, "'%s' arguments do not end where its signature says", name);
        return;
    }
    cs2_cc_expect_punct(cc, ')');
}

/**
 * A hook registration: `if_setonclick("name(args){triggers}", target)`.
 *
 * One quoted callback in the source becomes several stack values: the script
 * id, the arguments, the watched ids and their count, then a descriptor naming
 * each argument's type — pushed in that order, with the target component last
 * for the `if_*` family.
 *
 * The `cc_*` family carries its target in the operand exactly as the basic
 * commands do — 1 for the "." (dot) component, 0 for the active one — which is
 * how `.cc_setondrag` binds the scrollbar's drag handlers to the dragger it
 * just created rather than to the trough. `cs2_translate_clientscript` reads
 * that operand back on the same rule, so the two must agree.
 */
/**
 * The descriptor letter for one callback argument.
 *
 * `i` or `s` — the *stack* the value came off, never the type letter.
 *
 * Measured rather than assumed: over every `*_seton*` instruction in
 * cache.osrs239, the descriptors it pushes contain 27,063 `i`, 1,124 `s` and
 * 1,159 `Y`, and nothing else. Not one uppercase type letter in the whole
 * cache. Writing `I` for a component argument — which is what this did, and
 * which reads like the more informative choice — differed from the cache's own
 * bytes in 1,507 scripts.
 *
 * EXCEPTIONS G7 has the reason from the other direction: OldSchool stopped
 * putting the real type in hook descriptors and started writing `i`, which is
 * why the decompiler must not freeze a callee's parameter to `int` on the
 * strength of one. The letter's only operational job is to say which stack to
 * take the argument off, and that is all it now claims.
 *
 * A pre-237 cache does write the type letter, and there is none in this tree to
 * measure against, so no revision threshold is invented here — the same
 * position B6 takes on the trailer width. If one is added,
 * `RSCache_ClientScriptFlags`'s `RSCache_RevisionAtLeastOsrs(…, 237, …)` gate is
 * where the era switch belongs, and it should be verified against that cache's
 * bytes before it is believed.
 */
static char
cs2_cc_descriptor_letter(enum RSCache_CS2_Type type)
{
    return RSCache_CS2_TypeStackType(type) == RSCACHE_CS2_STACK_STRING ? 's' : 'i';
}

/**
 * Read the bare word that is argument `index` of the call the lexer is sitting
 * on the name of, without consuming anything.
 *
 * The lexer is one token wide, so a command whose result shape is written two
 * or three tokens ahead cannot be answered by peeking. The argument list is
 * still right there in the source; this reads it as text, which is enough for
 * the two cases that need it — both want a single identifier, `string` or
 * `param_1279`, not an expression. It gives up on anything else.
 */
static bool
cs2_cc_call_argument_word(struct cs2_cc_compiler* cc, int index, char* out, int capacity)
{
    const char* cursor = cc->source + cc->position;

    while( *cursor == ' ' || *cursor == '\t' )
        cursor++;
    if( *cursor != '(' )
        return false;
    cursor++;

    for( int skipped = 0; skipped < index; skipped++ )
    {
        int depth = 0;
        while( *cursor && (depth > 0 || (*cursor != ',' && *cursor != ')')) )
        {
            if( *cursor == '(' )
                depth++;
            else if( *cursor == ')' )
                depth--;
            cursor++;
        }
        if( *cursor != ',' )
            return false;
        cursor++;
    }

    while( *cursor == ' ' || *cursor == '\t' )
        cursor++;

    int length = 0;
    while( cs2_cc_ident_char(*cursor) && length < capacity - 1 )
        out[length++] = *cursor++;
    out[length] = '\0';
    if( length == 0 )
        return false;

    /* Only a bare word followed by an argument boundary. Anything else is an
     * expression this cannot read, and a half-read one would be worse than
     * refusing. */
    while( *cursor == ' ' || *cursor == '\t' )
        cursor++;
    return *cursor == ',' || *cursor == ')';
}

static void
cs2_cc_hook(struct cs2_cc_compiler* cc, int opcode, bool dot)
{
    if( dot && opcode >= RSCACHE_CS2_OP_IF_HOOK_BASE )
    {
        cs2_cc_fail(
            cc,
            "'%s' names its target component explicitly and has no '.' form",
            RSCache_CS2_CommandName(opcode));
        return;
    }

    if( !cs2_cc_expect_punct(cc, '(') )
        return;

    if( cs2_cc_at_ident(cc, "null") )
    {
        /* Clearing the hook rather than setting one. */
        cs2_cc_next(cc);
        cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, -1);
        cs2_cc_emit_string(cc, "");
    }
    else
    {
        if( cc->token.kind != CS2_CC_TOK_STRING )
        {
            cs2_cc_fail(cc, "expected a quoted callback");
            return;
        }
        /*
         * Re-read the callback with the bracket rule a callback needs.
         *
         * A callback's arguments can be string literals — `if_setonclick(
         * "script2470(event_com, "B", $string0)", …)` — and the reference
         * decompiler writes those inner quotes plain, with no escape (checked
         * against its expected output). So the general string lexer, which
         * stops at the first unnested `"`, cuts this one short and the parse
         * derails a few tokens later on the unbalanced '('.
         *
         * The lexer already suspends termination inside `<...>`, for exactly
         * this reason; a callback needs `(...)` counted too. It cannot have
         * that rule generally — 354 of the corpus's 33,232 string constants
         * hold an unbalanced parenthesis, `" ("` and `")"` among them — so the
         * span is re-scanned here, where the callback is the only thing it can
         * be, and the lexer is resynchronised to the true closing quote.
         */
        char* callback = NULL;
        {
            const char* text = cc->source;
            int cursor = cc->token.start;
            int depth = 0;
            while( text[cursor] )
            {
                char current = text[cursor];
                if( current == '\\' &&
                    (text[cursor + 1] == '"' || text[cursor + 1] == '\\') )
                {
                    cursor += 2;
                    continue;
                }
                if( current == '"' && depth == 0 )
                    break;
                if( current == '(' || current == '<' )
                    depth++;
                else if( (current == ')' || current == '>') && depth > 0 )
                    depth--;
                cursor++;
            }
            callback = RSCache_CS2_ArenaStrDupN(
                &cc->arena, text + cc->token.start, (size_t)(cursor - cc->token.start));
            if( text[cursor] == '"' )
                cursor++;
            cc->position = cursor;
            cc->has_ahead = false;
        }
        cs2_cc_next(cc);

        /* `name(args){triggers}` — braces and parentheses are the only
         * structure, so it is split before anything is lexed. */
        char* triggers_text = NULL;
        char* args_text = NULL;
        char* brace = strchr(callback, '{');
        if( brace )
        {
            char* brace_end = strrchr(callback, '}');
            if( !brace_end || brace_end < brace )
            {
                cs2_cc_fail(cc, "unterminated '{' in a callback");
                return;
            }
            *brace = '\0';
            *brace_end = '\0';
            triggers_text = brace + 1;
        }
        char* paren = strchr(callback, '(');
        if( paren )
        {
            char* paren_end = strrchr(callback, ')');
            if( !paren_end || paren_end < paren )
            {
                cs2_cc_fail(cc, "unterminated '(' in a callback");
                return;
            }
            *paren = '\0';
            *paren_end = '\0';
            args_text = paren + 1;
        }

        int callee_id = -1;
        if( !cs2_cc_resolve_script(
                cc, callback, RSCACHE_CS2_TRIGGER_CLIENTSCRIPT, &callee_id) )
        {
            cs2_cc_fail(cc, "no clientscript with the name '%s'", callback);
            return;
        }
        cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, callee_id);

        char descriptor[128];
        int descriptor_length = 0;

        if( args_text && *args_text )
        {
            struct cs2_cc_lexer_state saved;
            cs2_cc_enter_fragment(cc, args_text, &saved);
            while( cc->token.kind != CS2_CC_TOK_END && !cc->failed )
            {
                /* `.cc_getid` — the active-component form of a command, written
                 * as an argument. The '.' is punctuation, so the inference below
                 * sees it and gives up; what types the argument is the command
                 * after it, exactly as for the plain form. */
                if( cs2_cc_at_punct(cc, '.') && cs2_cc_peek(cc)->kind == CS2_CC_TOK_IDENT )
                {
                    enum RSCache_CS2_Type dotted = cs2_cc_infer_type(
                        cc, cs2_cc_peek(cc)->text, CS2_CC_TOK_IDENT);
                    if( dotted != RSCACHE_CS2_TYPE_NONE && RSCache_CS2_TypeDesc(dotted) != 0 )
                    {
                        if( descriptor_length >= (int)sizeof(descriptor) - 2 )
                        {
                            cs2_cc_fail(cc, "callback takes too many arguments");
                            cs2_cc_leave_fragment(cc, &saved);
                            return;
                        }
                        descriptor[descriptor_length++] = cs2_cc_descriptor_letter(dotted);
                        cs2_cc_expression(cc, dotted);
                        if( !cs2_cc_accept_punct(cc, ',') )
                            break;
                        continue;
                    }
                }

                /* `enum(int, string, …)` and `struct_param($struct0, param_1279)`
                 * as callback arguments.
                 *
                 * Both push a value the opcode does not describe: `enum`'s
                 * result type is its *second* argument, a type literal, and a
                 * `*_param` command's is the param config's. So the generated
                 * table carries no result prototype, the inference below
                 * answered "unknown", and the script was refused — 26 of them.
                 *
                 * Both answers are to hand: one in the source text a couple of
                 * tokens ahead, the other through the same `param_types`
                 * provider the decompiler uses. Reading them is the move the
                 * two cases below already make for a dotted command and for a
                 * `~proc` — the shape is knowable here, just not from the
                 * opcode. */
                if( cc->token.kind == CS2_CC_TOK_IDENT )
                {
                    int probe_opcode = RSCache_CS2_CommandOfName(cc->token.text);
                    const struct RSCache_CS2_CommandInfo* probe =
                        probe_opcode >= 0 ? RSCache_CS2_CommandGet(probe_opcode) : NULL;
                    enum RSCache_CS2_Type pushed = RSCACHE_CS2_TYPE_NONE;
                    enum RSCache_CS2_Type db_pushed[32];
                    int db_push_count = 0;
                    enum RSCache_CS2_Type static_pushed[32];
                    int static_push_count = 0;
                    char literal[64];

                    /* A fixed-signature command may itself fill several
                     * callback slots. `worldmap_getconfigbounds` is the rev-239
                     * witness: its four int results are one source expression,
                     * but four values are passed to script1713 and therefore
                     * require four descriptor letters. Treating an expression
                     * as one callback argument shortened `iiiiiiiiiiii` to
                     * `iiiiiiiii` while leaving the executable opcode stream
                     * otherwise intact. */
                    if( probe && probe->kind == RSCACHE_CS2_CMD_BASIC &&
                        probe->def_count > 1 && probe->def_count <= 32 )
                    {
                        for( int i = 0; i < probe->def_count; i++ )
                            static_pushed[static_push_count++] = RSCache_CS2_ProtoGet(
                                RSCache_CS2_CommandDef(probe, i))
                                                                    ->type;
                    }
                    else if( probe && probe->kind == RSCACHE_CS2_CMD_ENUM &&
                        cs2_cc_call_argument_word(cc, 1, literal, (int)sizeof(literal)) )
                    {
                        pushed = RSCache_CS2_TypeOfLiteral(literal);
                    }
                    else if( probe && probe->kind == RSCACHE_CS2_CMD_DB_GETFIELD )
                    {
                        if( cc->options && cc->options->db_columns.load &&
                            cs2_cc_call_argument_word(
                                cc, 1, literal, (int)sizeof(literal)) )
                        {
                            char* end = NULL;
                            long packed_long = strtol(literal, &end, 0);
                            if( end && *end == '\0' )
                            {
                                int packed = (int)packed_long;
                                int table_id = (packed >> 12) & 0xFFFFF;
                                int column_id = (packed >> 4) & 0xFF;
                                int field = (packed & 0xF) - 1;
                                enum RSCache_CS2_Type column_types[32];
                                int count = cc->options->db_columns.load(
                                    cc->options->db_columns.user,
                                    table_id,
                                    column_id,
                                    column_types,
                                    32);
                                if( field >= 0 && field < count )
                                    db_pushed[db_push_count++] = column_types[field];
                                else if( field < 0 && count > 0 )
                                {
                                    memcpy(
                                        db_pushed,
                                        column_types,
                                        (size_t)count * sizeof(column_types[0]));
                                    db_push_count = count;
                                }
                            }
                        }
                    }
                    else if(
                        probe &&
                        (probe->kind == RSCACHE_CS2_CMD_PARAM ||
                         probe->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ||
                         probe->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM) &&
                        cs2_cc_call_argument_word(
                            cc,
                            (probe->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ||
                             probe->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM) ? 0 : 1,
                            literal,
                            (int)sizeof(literal)) )
                    {
                        /* Named or `param_<id>` — the second is what a decompile
                         * without RuneStar's tables writes, and it is the whole
                         * point that the two spellings resolve the same. */
                        int param_id = -1;
                        if( !RSCache_CS2_NamesLookupId(
                                cs2_cc_names(cc), RSCACHE_CS2_NAMES_PARAM, literal, &param_id) )
                            (void)cs2_cc_parse_id_suffixed(literal, &param_id);
                        if( param_id >= 0 && cc->options->param_types.load )
                        {
                            pushed = cc->options->param_types.load(
                                cc->options->param_types.user, param_id);
                        }
                    }

                    if( static_push_count > 0 )
                    {
                        if( descriptor_length + static_push_count >=
                            (int)sizeof(descriptor) - 1 )
                        {
                            cs2_cc_fail(cc, "callback takes too many arguments");
                            cs2_cc_leave_fragment(cc, &saved);
                            return;
                        }
                        for( int i = 0; i < static_push_count; i++ )
                            descriptor[descriptor_length++] =
                                cs2_cc_descriptor_letter(static_pushed[i]);
                        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
                        if( !cs2_cc_accept_punct(cc, ',') )
                            break;
                        continue;
                    }
                    if( db_push_count > 0 )
                    {
                        if( descriptor_length + db_push_count >=
                            (int)sizeof(descriptor) - 1 )
                        {
                            cs2_cc_fail(cc, "callback takes too many arguments");
                            cs2_cc_leave_fragment(cc, &saved);
                            return;
                        }
                        for( int i = 0; i < db_push_count; i++ )
                            descriptor[descriptor_length++] =
                                cs2_cc_descriptor_letter(db_pushed[i]);
                        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
                        if( !cs2_cc_accept_punct(cc, ',') )
                            break;
                        continue;
                    }
                    if( pushed != RSCACHE_CS2_TYPE_NONE )
                    {
                        if( descriptor_length >= (int)sizeof(descriptor) - 2 )
                        {
                            cs2_cc_fail(cc, "callback takes too many arguments");
                            cs2_cc_leave_fragment(cc, &saved);
                            return;
                        }
                        descriptor[descriptor_length++] = cs2_cc_descriptor_letter(pushed);
                        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
                        if( !cs2_cc_accept_punct(cc, ',') )
                            break;
                        continue;
                    }
                }

                /* A `~proc` argument contributes one letter per value it
                 * returns, and the return types are in the callee rather than
                 * here. The callee is loadable — `options.scripts` is the same
                 * source the decompiler uses to type a proc call — and its
                 * trailing constant pushes say which stack each result lands
                 * on, which is all a descriptor letter has to record. */
                if( cs2_cc_at_punct(cc, '~') && cc->options->scripts.load )
                {
                    const struct cs2_cc_token* callee_name = cs2_cc_peek(cc);
                    int callee = -1;
                    if( callee_name->kind == CS2_CC_TOK_IDENT &&
                        cs2_cc_resolve_script(
                            cc, callee_name->text, RSCACHE_CS2_TRIGGER_PROC, &callee) )
                    {
                        const struct RSCache_CS2_Script* script =
                            cc->options->scripts.load(cc->options->scripts.user, callee);
                        enum RSCache_CS2_StackType returns[64];
                        int count =
                            script ? RSCache_CS2_ScriptReturnTypes(script, returns, 64) : 0;
                        if( count > 0 )
                        {
                            for( int i = 0; i < count; i++ )
                            {
                                if( descriptor_length >= (int)sizeof(descriptor) - 2 )
                                {
                                    cs2_cc_fail(cc, "callback takes too many arguments");
                                    cs2_cc_leave_fragment(cc, &saved);
                                    return;
                                }
                                descriptor[descriptor_length++] =
                                    returns[i] == RSCACHE_CS2_STACK_STRING ? 's' : 'i';
                            }
                            cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
                            if( !cs2_cc_accept_punct(cc, ',') )
                                break;
                            continue;
                        }
                    }
                }

                enum RSCache_CS2_Type type = cs2_cc_infer_call_type(cc);
                /* `event_*` values stand in for a value supplied when the hook
                 * fires; each has a fixed type. */
                if( cc->token.kind == CS2_CC_TOK_IDENT &&
                    strncmp(cc->token.text, "event_", 6) == 0 )
                {
                    for( int i = 0; i < RSCACHE_CS2_EVENT_COUNT; i++ )
                    {
                        if( strcmp(
                                cc->token.text,
                                RSCache_CS2_EventPropertyLiteral(
                                    (enum RSCache_CS2_EventProperty)i)) != 0 )
                            continue;
                        type = RSCache_CS2_ProtoGet(
                                   RSCache_CS2_EventPropertyProto(
                                       (enum RSCache_CS2_EventProperty)i))
                                   ->type;
                        break;
                    }
                }
                if( type == RSCACHE_CS2_TYPE_NONE ||
                    RSCache_CS2_TypeDesc(type) == 0 )
                {
                    cs2_cc_fail(
                        cc,
                        "cannot determine the type of a callback argument; a hook's "
                        "descriptor needs one letter per argument");
                    cs2_cc_leave_fragment(cc, &saved);
                    return;
                }
                if( descriptor_length >= (int)sizeof(descriptor) - 2 )
                {
                    cs2_cc_fail(cc, "callback takes too many arguments");
                    cs2_cc_leave_fragment(cc, &saved);
                    return;
                }
                descriptor[descriptor_length++] = cs2_cc_descriptor_letter(type);

                cs2_cc_expression(cc, type);
                if( !cs2_cc_accept_punct(cc, ',') )
                    break;
            }
            cs2_cc_leave_fragment(cc, &saved);
            if( cc->failed )
                return;
        }

        int trigger_count = 0;
        if( triggers_text )
        {
            struct cs2_cc_lexer_state saved;
            cs2_cc_enter_fragment(cc, triggers_text, &saved);
            while( cc->token.kind != CS2_CC_TOK_END && !cc->failed )
            {
                /* A vartransmit trigger is a varp *name*, not a value, so it is
                 * pushed as its id rather than read. */
                if( cc->token.kind == CS2_CC_TOK_GLOBAL || cc->token.kind == CS2_CC_TOK_IDENT )
                {
                    int push = 0;
                    int pop = 0;
                    int id = 0;
                    if( cs2_cc_global_kind(cc->token.text, &push, &pop, &id) )
                    {
                        cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, id);
                        cs2_cc_next(cc);
                        trigger_count++;
                        if( !cs2_cc_accept_punct(cc, ',') )
                            break;
                        continue;
                    }
                }
                cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
                trigger_count++;
                if( !cs2_cc_accept_punct(cc, ',') )
                    break;
            }
            cs2_cc_leave_fragment(cc, &saved);
            if( cc->failed )
                return;

            cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, trigger_count);
            /* The trailing 'Y' is what tells the client a trigger list follows. */
            if( descriptor_length < (int)sizeof(descriptor) - 1 )
                descriptor[descriptor_length++] = 'Y';
        }

        descriptor[descriptor_length] = '\0';
        cs2_cc_emit_string(cc, descriptor);
    }

    if( opcode >= RSCACHE_CS2_OP_IF_HOOK_BASE )
    {
        if( !cs2_cc_expect_punct(cc, ',') )
            return;
        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_COMPONENT);
    }
    cs2_cc_expect_punct(cc, ')');
    cs2_cc_emit(cc, opcode, dot ? 1 : 0);
}

static void
cs2_cc_command(struct cs2_cc_compiler* cc, const char* name, bool dot)
{
    int opcode = RSCache_CS2_CommandOfName(name);
    const struct RSCache_CS2_CommandInfo* info =
        opcode >= 0 ? RSCache_CS2_CommandGet(opcode) : NULL;
    if( !info )
    {
        cs2_cc_fail(cc, "unknown command '%s'", name);
        return;
    }

    if( info->kind == RSCACHE_CS2_CMD_CLIENTSCRIPT )
    {
        cs2_cc_hook(cc, opcode, dot);
        return;
    }
    if( info->kind == RSCACHE_CS2_CMD_DESCRIPTOR_ARGS )
    {
        /* The final source argument is the literal descriptor emitted by the
         * decompiler. Every preceding expression therefore already writes to
         * the correct physical stack; no fixed generated signature applies. */
        if( !cs2_cc_expect_punct(cc, '(') )
            return;
        int written = 0;
        while( !cc->failed && !cs2_cc_at_punct(cc, ')') &&
               cc->token.kind != CS2_CC_TOK_END )
        {
            if( written && !cs2_cc_expect_punct(cc, ',') )
                return;
            cs2_cc_argument(cc, RSCACHE_CS2_TYPE_NONE);
            written++;
        }
        if( written < 4 )
        {
            cs2_cc_fail(cc, "'%s' needs three fixed arguments and a descriptor", name);
            return;
        }
        cs2_cc_expect_punct(cc, ')');
        cs2_cc_emit(cc, opcode, 0);
        return;
    }
    if( info->kind == RSCACHE_CS2_CMD_VARIADIC_INT_RESULT )
    {
        if( !cs2_cc_expect_punct(cc, '(') )
            return;
        int written = 0;
        while( !cc->failed && !cs2_cc_at_punct(cc, ')') &&
               cc->token.kind != CS2_CC_TOK_END )
        {
            if( written++ && !cs2_cc_expect_punct(cc, ',') )
                return;
            cs2_cc_argument(cc, RSCACHE_CS2_TYPE_INT);
        }
        cs2_cc_expect_punct(cc, ')');
        cs2_cc_emit(cc, opcode, dot ? 1 : 0);
        return;
    }
    if( info->kind == RSCACHE_CS2_CMD_PARAM || info->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ||
        info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM || info->kind == RSCACHE_CS2_CMD_ENUM )
    {
        /* Both take a fixed argument list the generated table does not carry,
         * because what they push depends on an operand. */
        int arg_count = info->kind == RSCACHE_CS2_CMD_ENUM
                            ? 4
                            : (info->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM
                                   ? 1
                                   : (info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM ? 3 : 2));
        if( !cs2_cc_expect_punct(cc, '(') )
            return;
        for( int i = 0; i < arg_count && !cc->failed; i++ )
        {
            if( i && !cs2_cc_expect_punct(cc, ',') )
                return;
            enum RSCache_CS2_Type expected = RSCACHE_CS2_TYPE_NONE;
            if( info->kind == RSCACHE_CS2_CMD_ENUM && i < 2 )
                expected = RSCACHE_CS2_TYPE_TYPE;
            else if( (info->kind == RSCACHE_CS2_CMD_PARAM && i == 1) ||
                     ((info->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ||
                       info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM) && i == 0) )
                expected = RSCACHE_CS2_TYPE_PARAM;
            else if( info->kind == RSCACHE_CS2_CMD_PARAM && i == 0 )
                expected = RSCache_CS2_ProtoGet(
                               (enum RSCache_CS2_ProtoId)info->extra)
                               ->type;
            else if( info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM && i == 1 )
                expected = RSCACHE_CS2_TYPE_INTERFACE;
            else if( info->kind == RSCACHE_CS2_CMD_COMPONENT_PARAM && i == 2 )
                expected = RSCACHE_CS2_TYPE_INT;
            cs2_cc_argument(cc, expected);
        }
        cs2_cc_expect_punct(cc, ')');
        cs2_cc_emit(
            cc, opcode, info->kind == RSCACHE_CS2_CMD_ACTIVE_PARAM ? (dot ? 1 : 0) : 0);
        return;
    }
    if( info->kind == RSCACHE_CS2_CMD_DB_GETFIELD || info->kind == RSCACHE_CS2_CMD_DB_FIND )
    {
        /* Three arguments each, and no prototypes: what a db command pops and
         * pushes is a property of the dbcolumn literal rather than of the
         * opcode, so the generated table carries no signature to read here.
         * Emitting is unaffected — each argument is one expression, and the
         * search value's stack follows from how it is written. */
        if( !cs2_cc_expect_punct(cc, '(') )
            return;
        for( int i = 0; i < 3 && !cc->failed; i++ )
        {
            if( i && !cs2_cc_expect_punct(cc, ',') )
                return;
            cs2_cc_expression(
                cc,
                i == 0 && info->kind == RSCACHE_CS2_CMD_DB_GETFIELD ? RSCACHE_CS2_TYPE_DBROW
                                                                    : RSCACHE_CS2_TYPE_NONE);
        }
        cs2_cc_expect_punct(cc, ')');
        cs2_cc_emit(cc, opcode, 0);
        return;
    }
    if( info->kind != RSCACHE_CS2_CMD_BASIC && info->kind != RSCACHE_CS2_CMD_TYPED_POP )
    {
        cs2_cc_fail(cc, "'%s' cannot be written as a call", name);
        return;
    }

    cs2_cc_command_arguments(cc, name, info);
    cs2_cc_emit(cc, opcode, info->dot_capable ? (dot ? 1 : 0) : 0);
}

/**
 * `calc(...)` arithmetic, by precedence climbing. Each operator emits after its
 * operands, which is already the stack order the VM wants.
 */
static void
cs2_cc_calc(struct cs2_cc_compiler* cc, int max_precedence)
{
    if( cs2_cc_accept_punct(cc, '(') )
    {
        cs2_cc_calc(cc, 6);
        cs2_cc_expect_punct(cc, ')');
    }
    else
    {
        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_INT);
    }

    while( !cc->failed && cc->token.kind == CS2_CC_TOK_PUNCT )
    {
        int opcode;
        int precedence;
        switch( cc->token.punct )
        {
        case '*':
            opcode = RSCACHE_CS2_OP_MULTIPLY;
            precedence = 1;
            break;
        case '/':
            opcode = RSCACHE_CS2_OP_DIV;
            precedence = 1;
            break;
        case '%':
            opcode = RSCACHE_CS2_OP_MOD;
            precedence = 1;
            break;
        case '+':
            opcode = RSCACHE_CS2_OP_ADD;
            precedence = 2;
            break;
        case '-':
            opcode = RSCACHE_CS2_OP_SUB;
            precedence = 2;
            break;
        case '&':
            opcode = RSCACHE_CS2_OP_AND;
            precedence = 5;
            break;
        case '|':
            opcode = RSCACHE_CS2_OP_OR;
            precedence = 6;
            break;
        default:
            return;
        }
        if( precedence > max_precedence )
            return;
        cs2_cc_next(cc);
        cs2_cc_calc(cc, precedence - 1);
        cs2_cc_emit(cc, opcode, 0);
    }
}

static void
cs2_cc_expression(struct cs2_cc_compiler* cc, enum RSCache_CS2_Type expected)
{
    if( cc->failed )
        return;

    switch( cc->token.kind )
    {
    case CS2_CC_TOK_INT:
        cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, cc->token.int_value);
        cs2_cc_next(cc);
        return;

    case CS2_CC_TOK_STRING:
    {
        const char* body = cc->token.text;
        /* A graphic is written quoted but pushed as an id; only the expected
         * type distinguishes it from an ordinary string. */
        if( expected == RSCACHE_CS2_TYPE_GRAPHIC )
        {
            int id = -1;
            if( RSCache_CS2_NamesLookupId(
                    cs2_cc_names(cc), RSCACHE_CS2_NAMES_GRAPHIC, body, &id) ||
                cs2_cc_parse_id_suffixed(body, &id) )
            {
                cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, id);
                cs2_cc_next(cc);
                return;
            }
            cs2_cc_fail(cc, "no graphic id known for \"%s\"", body);
            return;
        }
        cs2_cc_next(cc);
        cs2_cc_string_literal(cc, body);
        return;
    }

    case CS2_CC_TOK_CONSTANT:
    {
        int value = 0;
        if( !cs2_cc_resolve_caret(cc, cc->token.text, &value) )
        {
            cs2_cc_fail(cc, "unknown constant '^%s'", cc->token.text);
            return;
        }
        cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, value);
        cs2_cc_next(cc);
        return;
    }

    case CS2_CC_TOK_LOCAL:
    {
        const char* name = cc->token.text;
        int index = cs2_cc_find_local(cc, name);
        if( index < 0 )
            index = cs2_cc_declare_local_from_name(cc, name);
        if( index < 0 )
        {
            cs2_cc_fail(cc, "'$%s' is used before it is defined", name);
            return;
        }
        cs2_cc_next(cc);
        if( cc->locals[index].is_array && cs2_cc_at_punct(cc, '(') )
        {
            cs2_cc_next(cc);
            cs2_cc_argument(cc, RSCACHE_CS2_TYPE_INT);
            cs2_cc_expect_punct(cc, ')');
            cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_ARRAY_INT, cc->locals[index].slot);
            return;
        }
        cs2_cc_emit(
            cc,
            cc->locals[index].is_string ? RSCACHE_CS2_OP_PUSH_STRING_LOCAL
                                        : RSCACHE_CS2_OP_PUSH_INT_LOCAL,
            cc->locals[index].slot);
        return;
    }

    case CS2_CC_TOK_GLOBAL:
    {
        int push = 0;
        int pop = 0;
        int id = 0;
        if( !cs2_cc_global_kind(cc->token.text, &push, &pop, &id) )
        {
            cs2_cc_fail(cc, "unknown global '%%%s'", cc->token.text);
            return;
        }
        cs2_cc_next(cc);
        cs2_cc_emit(cc, push, id);
        return;
    }

    case CS2_CC_TOK_PUNCT:
        if( cc->token.punct == '~' )
        {
            cs2_cc_next(cc);
            cs2_cc_proc_call(cc);
            return;
        }
        if( cc->token.punct == '.' )
        {
            cs2_cc_next(cc);
            if( cc->token.kind != CS2_CC_TOK_IDENT )
            {
                cs2_cc_fail(cc, "expected a command after '.'");
                return;
            }
            const char* name = cc->token.text;
            cs2_cc_next(cc);
            cs2_cc_command(cc, name, true);
            return;
        }
        cs2_cc_fail(cc, "unexpected '%c'", cc->token.punct);
        return;

    case CS2_CC_TOK_IDENT:
    {
        const char* name = cc->token.text;

        if( strcmp(name, "calc") == 0 )
        {
            cs2_cc_next(cc);
            if( !cs2_cc_expect_punct(cc, '(') )
                return;
            cc->calc_depth++;
            cs2_cc_calc(cc, 6);
            cc->calc_depth--;
            cs2_cc_expect_punct(cc, ')');
            return;
        }

        for( int i = 0; i < RSCACHE_CS2_EVENT_COUNT; i++ )
        {
            if( strcmp(
                    name,
                    RSCache_CS2_EventPropertyLiteral((enum RSCache_CS2_EventProperty)i)) != 0 )
                continue;
            /* Only meaningful inside a hook's callback, where it stands for a
             * value the client substitutes when the event fires. The magic
             * constants are the ones cs2_ir.c recognises on the way back. */
            cs2_cc_next(cc);
            if( i == RSCACHE_CS2_EVENT_OPBASE )
                cs2_cc_emit_string(cc, "event_opbase");
            else
                cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, (-2147483647 - 1) + i);
            return;
        }

        /*
         * A bare array name — `~script465(intarray0, …)`.
         *
         * An array is passed by *name*, not by `$name`: the decompiler writes it
         * as a pointer (RSCACHE_CS2_EXPR_POINTER) precisely because it is the
         * array itself and not a read of it. What goes on the stack is the
         * handle, from the bank `cs2_cc_declare_local` put the argument in.
         *
         * Checked against the declared locals rather than the spelling, so a
         * command that happened to end in a digit could never be mistaken for
         * one. Every array argument is declared by the signature before any
         * statement can name it.
         */
        {
            int array_index = cs2_cc_find_local(cc, name);
            if( array_index >= 0 && cc->locals[array_index].is_array &&
                !cs2_cc_at_punct(cc, '(') )
            {
                cs2_cc_next(cc);
                cs2_cc_emit(
                    cc,
                    cc->locals[array_index].is_string ? RSCACHE_CS2_OP_PUSH_STRING_LOCAL
                                                      : RSCACHE_CS2_OP_PUSH_INT_LOCAL,
                    cc->locals[array_index].slot);
                return;
            }
        }

        /* `interface:component` is the only two-token literal. */
        if( cs2_cc_peek(cc)->kind == CS2_CC_TOK_PUNCT && cs2_cc_peek(cc)->punct == ':' )
        {
            int interface_id = 0;
            if( !RSCache_CS2_NamesLookupId(
                    cs2_cc_names(cc), RSCACHE_CS2_NAMES_INTERFACE, name, &interface_id) &&
                !cs2_cc_parse_id_suffixed(name, &interface_id) )
            {
                cs2_cc_fail(cc, "'%s' is not a known interface", name);
                return;
            }
            cs2_cc_next(cc);
            cs2_cc_next(cc);
            if( cc->token.kind != CS2_CC_TOK_INT )
            {
                cs2_cc_fail(cc, "expected a component index after ':'");
                return;
            }
            cs2_cc_emit(
                cc,
                RSCACHE_CS2_OP_PUSH_CONSTANT_INT,
                (interface_id << 16) | (cc->token.int_value & 0xFFFF));
            cs2_cc_next(cc);
            return;
        }

        /* A type literal wins over a command of the same spelling.
         *
         * `enum(int, enum, enum_2070, $int5)` is legal source and the second
         * argument is the *type* `enum`, not a nested call — but `enum` is also
         * opcode 3408, so the command branch below claimed it and then demanded
         * the '(' that a type literal does not have. 108 scripts failed to
         * compile on that one collision. Only where a type is what the context
         * asked for, so an `enum(...)` in any other position still parses as the
         * command. */
        if( expected == RSCACHE_CS2_TYPE_TYPE &&
            RSCache_CS2_TypeOfLiteral(name) != RSCACHE_CS2_TYPE_NONE )
        {
            cs2_cc_emit(
                cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT,
                RSCache_CS2_TypeDesc(RSCache_CS2_TypeOfLiteral(name)));
            cs2_cc_next(cc);
            return;
        }

        int opcode = RSCache_CS2_CommandOfName(name);
        const struct RSCache_CS2_CommandInfo* info =
            opcode >= 0 ? RSCache_CS2_CommandGet(opcode) : NULL;
        if( info && info->kind != RSCACHE_CS2_CMD_UNKNOWN &&
            info->kind != RSCACHE_CS2_CMD_ASSIGN )
        {
            cs2_cc_next(cc);
            cs2_cc_command(cc, name, false);
            return;
        }

        int value = 0;
        if( cs2_cc_resolve_named(cc, name, expected, &value) )
        {
            cs2_cc_emit(cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, value);
            cs2_cc_next(cc);
            return;
        }
        cs2_cc_fail(cc, "'%s' is neither a command nor a resolvable constant", name);
        return;
    }

    default:
        cs2_cc_fail(cc, "expected an expression");
        return;
    }
}

/* -------------------------------------------------------------------------
 * Conditions
 *
 * `&` and `|` become branch structure rather than opcodes — the inverse of the
 * decompiler's short-circuit pass. `a | b` is two branches to the same target;
 * `a & b` is a branch over a branch.
 * ---------------------------------------------------------------------- */

static void
cs2_cc_condition(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* pass, struct cs2_cc_jumps* fail);

static void
cs2_cc_comparison(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* pass)
{
    cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
    if( cc->failed )
        return;

    if( cc->token.kind != CS2_CC_TOK_PUNCT )
    {
        cs2_cc_fail(cc, "expected a comparison operator");
        return;
    }
    int opcode;
    switch( cc->token.punct )
    {
    case '=':
        opcode = RSCACHE_CS2_OP_BRANCH_EQUALS;
        break;
    case '!':
        opcode = RSCACHE_CS2_OP_BRANCH_NOT;
        break;
    case '<':
        opcode = cc->token.punct_eq ? RSCACHE_CS2_OP_BRANCH_LESS_THAN_OR_EQUALS
                                    : RSCACHE_CS2_OP_BRANCH_LESS_THAN;
        break;
    case '>':
        opcode = cc->token.punct_eq ? RSCACHE_CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS
                                    : RSCACHE_CS2_OP_BRANCH_GREATER_THAN;
        break;
    default:
        cs2_cc_fail(cc, "'%c' is not a comparison", cc->token.punct);
        return;
    }
    cs2_cc_next(cc);
    cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
    if( cc->failed )
        return;
    cs2_cc_jumps_add(cc, pass, cs2_cc_emit(cc, opcode, 0));
}

static void
cs2_cc_condition_term(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* pass, struct cs2_cc_jumps* fail)
{
    if( cs2_cc_accept_punct(cc, '(') )
    {
        cs2_cc_condition(cc, pass, fail);
        cs2_cc_expect_punct(cc, ')');
        return;
    }
    cs2_cc_comparison(cc, pass);
}

/**
 * A run of `&`-joined terms. Passes only if every term does.
 *
 * Each term's pass jump is patched to the next term, so falling through is
 * failure; a jump to the shared fail follows each. That is the shape the
 * decompiler's short-circuit pass recognises as `&` — a branch over a branch.
 */
static void
cs2_cc_condition_and(
    struct cs2_cc_compiler* cc,
    struct cs2_cc_jumps* pass,
    struct cs2_cc_jumps* fail)
{
    for( ;; )
    {
        struct cs2_cc_jumps term_pass = { NULL, 0, 0 };
        cs2_cc_condition_term(cc, &term_pass, fail);
        if( cc->failed )
            return;
        if( !cs2_cc_at_punct(cc, '&') )
        {
            /* Last term: its pass is the whole group's pass. */
            for( int i = 0; i < term_pass.count; i++ )
                cs2_cc_jumps_add(cc, pass, term_pass.items[i]);
            return;
        }
        cs2_cc_next(cc);
        /* Passing only means "test the next term", so land there and make the
         * fall-through — failure — jump out. */
        cs2_cc_jumps_patch(cc, &term_pass, cc->ops.count + 1);
        cs2_cc_jumps_add(cc, fail, cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0));
    }
}

/**
 * A run of `|`-joined groups. Passes if any does.
 *
 * `&` binds tighter, so each alternative is a whole and-group. Getting this
 * precedence wrong does not fail to compile — it silently changes when the
 * branch is taken, which is how `a & b | c & d` came back as `a & (b | c) & d`.
 */
static void
cs2_cc_condition(struct cs2_cc_compiler* cc, struct cs2_cc_jumps* pass, struct cs2_cc_jumps* fail)
{
    for( ;; )
    {
        struct cs2_cc_jumps group_fail = { NULL, 0, 0 };
        cs2_cc_condition_and(cc, pass, &group_fail);
        if( cc->failed )
            return;
        if( !cs2_cc_at_punct(cc, '|') )
        {
            /* Last alternative: its failure is the whole condition's. */
            for( int i = 0; i < group_fail.count; i++ )
                cs2_cc_jumps_add(cc, fail, group_fail.items[i]);
            return;
        }
        cs2_cc_next(cc);
        /* This alternative failing just means "try the next one". */
        cs2_cc_jumps_patch(cc, &group_fail, cc->ops.count);
    }
}

/* -------------------------------------------------------------------------
 * Statements
 * ---------------------------------------------------------------------- */

static void
cs2_cc_if(struct cs2_cc_compiler* cc)
{
    struct cs2_cc_jumps ends = { NULL, 0, 0 };

    for( ;; )
    {
        if( !cs2_cc_expect_punct(cc, '(') )
            return;
        struct cs2_cc_jumps pass = { NULL, 0, 0 };
        struct cs2_cc_jumps fail = { NULL, 0, 0 };
        cs2_cc_condition(cc, &pass, &fail);
        if( !cs2_cc_expect_punct(cc, ')') || cc->failed )
            return;

        /* The untaken path falls through, so a jump over the body comes first
         * and the body follows it. */
        int skip = cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0);
        cs2_cc_jumps_patch(cc, &pass, cc->ops.count);
        cs2_cc_block(cc);
        if( cc->failed )
            return;
        /* The jump past the `else`. It is emitted even when the body just
         * returned and the jump cannot be reached.
         *
         * Suppressing it looks right — `if (a) { return; } else if (b) { … }`
         * is six instructions in cache.osrs239 and eight here — and measuring
         * says it is not: byte-exact went 8,291 -> 8,266 and nothing was
         * gained. The cache emits unreachable jumps of exactly this shape
         * elsewhere (script 660 has thirteen, one after each returning switch
         * case), so their absence in script 73 is not a rule about `if`.
         *
         * It is a rule about `else`. Script 73's source is two separate `if`s
         * in the cache and an `else if` chain here, because the decompiler
         * builds a chain wherever the branch structure allows one. That is the
         * shape difference EXCEPTIONS G2 records, and its fix belongs in the
         * reconstruction, not here. See CS2VM_Robustness.md D9. */
        cs2_cc_jumps_add(cc, &ends, cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0));
        cs2_cc_patch(cc, skip, cc->ops.count);
        cs2_cc_jumps_patch(cc, &fail, cc->ops.count);

        if( !cs2_cc_at_ident(cc, "else") )
            break;
        cs2_cc_next(cc);
        if( cs2_cc_at_ident(cc, "if") )
        {
            cs2_cc_next(cc);
            continue;
        }
        cs2_cc_block(cc);
        break;
    }

    cs2_cc_jumps_patch(cc, &ends, cc->ops.count);
}

static void
cs2_cc_while(struct cs2_cc_compiler* cc)
{
    int top = cc->ops.count;
    if( !cs2_cc_expect_punct(cc, '(') )
        return;
    struct cs2_cc_jumps pass = { NULL, 0, 0 };
    struct cs2_cc_jumps fail = { NULL, 0, 0 };
    cs2_cc_condition(cc, &pass, &fail);
    if( !cs2_cc_expect_punct(cc, ')') || cc->failed )
        return;

    int skip = cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0);
    cs2_cc_jumps_patch(cc, &pass, cc->ops.count);
    cs2_cc_block(cc);
    if( cc->failed )
        return;
    /* The back edge is what makes the reconstruction see a loop rather than an
     * if: it gives the test block a predecessor later in the listing. */
    cs2_cc_patch(cc, cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0), top);
    cs2_cc_patch(cc, skip, cc->ops.count);
    cs2_cc_jumps_patch(cc, &fail, cc->ops.count);
}

/**
 * A switch.
 *
 * Each body's source span is recorded on a first sweep and compiled in bytecode
 * order on a second, because the two orders differ.
 *
 * The bytecode order is the cache's, measured rather than assumed:
 *
 *     switch
 *     branch  -> D            the no-match path
 *     case body 0 ; branch -> END
 *     case body 1 ; branch -> END
 *     ...
 *  D: default body            last, and it falls through
 *  END:
 *
 * This put the default body **first** — immediately after the SWITCH, where
 * control falls through to — with a jump over it to the case bodies. That runs
 * identically and is what a reader would write, but it is not what Jagex's
 * compiler emits, and it was the largest remaining cause of a round trip coming
 * back with different bytes: 405 scripts where the instruction at the same
 * address was a `branch` in the cache and a value push in the rebuild, plus
 * most of the 582 whose jump targets disagreed.
 *
 * Script 110 is the one that settles the shape, because its no-match path and
 * its break path have different destinations: `branch +18` after the SWITCH
 * lands on the default body at 21, while every case body's terminating branch
 * lands on 23, the statement after the switch. A default body compiled first
 * cannot produce those two addresses.
 */
static void
cs2_cc_switch(struct cs2_cc_compiler* cc, enum RSCache_CS2_Type subject_type)
{
    if( cc->switch_count == CS2_CC_MAX_SWITCHES )
    {
        cs2_cc_fail(cc, "too many switch statements");
        return;
    }
    int table = cc->switch_count++;
    struct cs2_cc_switch* entries = &cc->switches[table];

    if( !cs2_cc_expect_punct(cc, '(') )
        return;
    cs2_cc_expression(cc, subject_type);
    if( !cs2_cc_expect_punct(cc, ')') || cc->failed )
        return;
    int switch_pc = cs2_cc_emit(cc, RSCACHE_CS2_OP_SWITCH, table);
    entries->pc = switch_pc;
    if( !cs2_cc_expect_punct(cc, '{') )
        return;

    struct cs2_cc_case
    {
        int keys[512];
        int key_count;
        const char* body;
    };
    /* 256 was not enough: `[proc,skill_guide_build]` and three others carry
     * more, the same way the decoder's switch tables had to stop being `[32]`
     * of `[256]`. Heap, because 1,024 of these is 2 MB of stack. */
    struct cs2_cc_case* cases = (struct cs2_cc_case*)calloc(1024, sizeof(*cases));
    if( !cases )
    {
        cs2_cc_fail(cc, "out of memory for a switch");
        return;
    }
    int case_count = 0;
    const char* default_body = NULL;

    while( !cc->failed && cs2_cc_at_ident(cc, "case") )
    {
        cs2_cc_next(cc);
        bool is_default = cs2_cc_at_ident(cc, "default");
        struct cs2_cc_case* entry = NULL;
        if( is_default )
        {
            cs2_cc_next(cc);
        }
        else
        {
            if( case_count == 1024 )
            {
                cs2_cc_fail(cc, "too many switch cases");
                goto done;
            }
            entry = &cases[case_count++];
            entry->key_count = 0;
            for( ;; )
            {
                int value = 0;
                bool ok;
                if( cc->token.kind == CS2_CC_TOK_INT )
                {
                    value = cc->token.int_value;
                    ok = true;
                }
                else if( cc->token.kind == CS2_CC_TOK_CONSTANT )
                {
                    ok = cs2_cc_resolve_caret(cc, cc->token.text, &value);
                }
                else if(
                    subject_type == RSCACHE_CS2_TYPE_COMPONENT &&
                    cc->token.kind == CS2_CC_TOK_IDENT &&
                    cs2_cc_peek(cc)->kind == CS2_CC_TOK_PUNCT &&
                    cs2_cc_peek(cc)->punct == ':' )
                {
                    /* `interface:component` — the only case label that is not a
                     * single token.
                     *
                     * Gated on the subject's type because the ':' that ends the
                     * label list looks identical one token ahead. `case
                     * obj_22731, obj_22734, obj_22743 :` sent its *last* label
                     * down this path, which consumed the terminator and then
                     * demanded a component index; 41 scripts failed on it, and
                     * only a `switch_component` can carry this form at all. */
                    int interface_id = 0;
                    ok = RSCache_CS2_NamesLookupId(
                             cs2_cc_names(cc),
                             RSCACHE_CS2_NAMES_INTERFACE,
                             cc->token.text,
                             &interface_id) ||
                         cs2_cc_parse_id_suffixed(cc->token.text, &interface_id);
                    if( ok )
                    {
                        cs2_cc_next(cc);
                        cs2_cc_next(cc);
                        ok = cc->token.kind == CS2_CC_TOK_INT;
                        if( ok )
                            value = (interface_id << 16) | (cc->token.int_value & 0xFFFF);
                    }
                }
                else
                {
                    ok = cc->token.kind == CS2_CC_TOK_IDENT &&
                         cs2_cc_resolve_named(cc, cc->token.text, subject_type, &value);
                }
                if( !ok )
                {
                    cs2_cc_fail(cc, "case label is not a resolvable constant");
                    goto done;
                }
                if( entry->key_count < (int)(sizeof(entry->keys) / sizeof(entry->keys[0])) )
                    entry->keys[entry->key_count++] = value;
                cs2_cc_next(cc);
                if( !cs2_cc_accept_punct(cc, ',') )
                    break;
            }
        }
        if( !cs2_cc_expect_punct(cc, ':') )
            goto done;

        /* Capture the body and skip past it, to be compiled on the second pass. */
        struct RSCache_CS2_StrBuf body;
        RSCache_CS2_StrBufInit(&body);
        int depth = 0;
        while( cc->token.kind != CS2_CC_TOK_END )
        {
            if( cs2_cc_at_punct(cc, '{') )
                depth++;
            else if( cs2_cc_at_punct(cc, '}') )
            {
                if( depth == 0 )
                    break;
                depth--;
            }
            else if( depth == 0 && cs2_cc_at_ident(cc, "case") )
                break;

            /* Rebuilt token by token rather than sliced from the source,
             * because the lexer's position has already moved past the token
             * it is holding. Spacing does not matter; the second pass re-lexes. */
            switch( cc->token.kind )
            {
            case CS2_CC_TOK_IDENT:
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                break;
            case CS2_CC_TOK_INT:
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                break;
            case CS2_CC_TOK_STRING:
                RSCache_CS2_StrBufAppendChar(&body, '"');
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                RSCache_CS2_StrBufAppendChar(&body, '"');
                break;
            case CS2_CC_TOK_LOCAL:
                RSCache_CS2_StrBufAppendChar(&body, '$');
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                break;
            case CS2_CC_TOK_GLOBAL:
                RSCache_CS2_StrBufAppendChar(&body, '%');
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                break;
            case CS2_CC_TOK_CONSTANT:
                RSCache_CS2_StrBufAppendChar(&body, '^');
                RSCache_CS2_StrBufAppend(&body, cc->token.text);
                break;
            default:
                RSCache_CS2_StrBufAppendChar(&body, cc->token.punct);
                if( cc->token.punct_eq )
                    RSCache_CS2_StrBufAppendChar(&body, '=');
                break;
            }
            RSCache_CS2_StrBufAppendChar(&body, ' ');
            cs2_cc_next(cc);
        }

        const char* text = RSCache_CS2_ArenaStrDup(&cc->arena, RSCache_CS2_StrBufCStr(&body));
        RSCache_CS2_StrBufFree(&body);
        if( is_default )
            default_body = text;
        else
            entry->body = text;
    }
    if( !cs2_cc_expect_punct(cc, '}') || cc->failed )
        goto done;

    struct cs2_cc_jumps ends = { NULL, 0, 0 };

    /* The no-match path, jumping over every case body to the default body. */
    int no_match = cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0);

    for( int i = 0; i < case_count && !cc->failed; i++ )
    {
        int target = cc->ops.count;
        for( int j = 0; j < cases[i].key_count; j++ )
        {
            if( entries->count == entries->capacity )
            {
                int capacity = entries->capacity ? entries->capacity * 2 : 16;
                entries->keys = (int*)realloc(entries->keys, (size_t)capacity * sizeof(int));
                entries->targets =
                    (int*)realloc(entries->targets, (size_t)capacity * sizeof(int));
                if( !entries->keys || !entries->targets )
                {
                    cs2_cc_fail(cc, "out of memory");
                    goto done;
                }
                entries->capacity = capacity;
            }
            entries->keys[entries->count] = cases[i].keys[j];
            /* Case targets are stored relative to the instruction after the
             * SWITCH, matching how the client resolves them. */
            entries->targets[entries->count] = target - switch_pc - 1;
            entries->count++;
        }

        if( cases[i].body && *cases[i].body )
        {
            struct cs2_cc_lexer_state saved;
            cs2_cc_enter_fragment(cc, cases[i].body, &saved);
            cs2_cc_statements_until(cc, 0);
            cs2_cc_leave_fragment(cc, &saved);
        }

        /* The break out of the case body, emitted after every case including
         * the last, and after a body that has already returned.
         *
         * The cache sometimes does not carry the last one. Script 660 lays out
         * thirteen case bodies with twelve breaks between them and none after
         * the thirteenth; script 8950 has one case body and no break at all.
         * Both last bodies end in `return`, so the jump is dead, and "omit the
         * last break when the body returned" is the rule those two imply.
         *
         * It is not the rule. Measured: byte-exact 8,292 -> 8,233, while
         * same-length went 8,804 -> 8,858 — so it fixed the lengths of some
         * scripts and broke the bytes of 59 others that were exact, which means
         * there are scripts whose last case body returns and whose break the
         * cache *does* carry. Something else decides, and it is not deadness
         * (660's other twelve breaks are equally dead and all present) and not
         * the presence of a default body (8950 has one).
         *
         * Left alone until there is a rule rather than two examples. See
         * CS2VM_Robustness.md D12. */
        cs2_cc_jumps_add(cc, &ends, cs2_cc_emit(cc, RSCACHE_CS2_OP_BRANCH, 0));
    }
    if( cc->failed )
        goto done;

    /* The default body, last, reached by falling out of the SWITCH. */
    cs2_cc_patch(cc, no_match, cc->ops.count);
    if( default_body && *default_body )
    {
        struct cs2_cc_lexer_state saved;
        cs2_cc_enter_fragment(cc, default_body, &saved);
        cs2_cc_statements_until(cc, 0);
        cs2_cc_leave_fragment(cc, &saved);
        if( cc->failed )
            goto done;
    }

    /* A `break` lands after the default body, not on it. */
    cs2_cc_jumps_patch(cc, &ends, cc->ops.count);
done:
    free(cases);
}

/** `return;` / `return(a, b);` */
static void
cs2_cc_return(struct cs2_cc_compiler* cc)
{
    if( cs2_cc_accept_punct(cc, '(') )
    {
        while( !cs2_cc_at_punct(cc, ')') && !cc->failed && cc->token.kind != CS2_CC_TOK_END )
        {
            cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
            if( !cs2_cc_accept_punct(cc, ',') )
                break;
        }
        cs2_cc_expect_punct(cc, ')');
    }
    cs2_cc_emit(cc, RSCACHE_CS2_OP_RETURN, 0);
}

/**
 * Type the left-hand side of an assignment wants pushed.
 *
 * Quoted graphics (`"graphic_4912"`) are ints only when the expected type says
 * so; a multi-assign must pass each target's type into its paired RHS, or the
 * string form is emitted and POP_INT_LOCAL finds an empty int stack.
 */
static enum RSCache_CS2_Type
cs2_cc_assign_target_type(struct cs2_cc_compiler* cc, const struct cs2_cc_token* target)
{
    if( target->kind == CS2_CC_TOK_LOCAL )
    {
        int index = cs2_cc_find_local(cc, target->text);
        if( index < 0 )
            index = cs2_cc_declare_local_from_name(cc, target->text);
        if( index >= 0 )
            return cc->locals[index].type;
        return RSCACHE_CS2_TYPE_NONE;
    }
    /* Globals (%varbit, %varc, …) are always ints or strings by kind; the
     * expression path does not need a graphic-style rewrite for them. */
    return RSCACHE_CS2_TYPE_NONE;
}

/** The left-hand side of an assignment, after its value is on the stack. */
static void
cs2_cc_emit_store(struct cs2_cc_compiler* cc, const struct cs2_cc_token* target)
{
    if( target->kind == CS2_CC_TOK_LOCAL )
    {
        int index = cs2_cc_find_local(cc, target->text);
        if( index < 0 )
        {
            cs2_cc_fail(cc, "'$%s' is assigned before it is defined", target->text);
            return;
        }
        cs2_cc_emit(
            cc,
            cc->locals[index].is_string ? RSCACHE_CS2_OP_POP_STRING_LOCAL
                                        : RSCACHE_CS2_OP_POP_INT_LOCAL,
            cc->locals[index].slot);
        return;
    }

    int push = 0;
    int pop = 0;
    int id = 0;
    if( !cs2_cc_global_kind(target->text, &push, &pop, &id) || pop < 0 )
    {
        cs2_cc_fail(cc, "'%%%s' cannot be assigned", target->text);
        return;
    }
    cs2_cc_emit(cc, pop, id);
}

/**
 * Discard whatever a statement-position call left on the stacks.
 *
 * `~script486($int0);` written as a statement still pushes the proc's result,
 * and CS2 has no implicit drop: the cache's own bytecode follows such a call
 * with one `pop_int_discard` / `pop_string_discard` per value. The compiler
 * emitted none, so the rebuilt script ran with a value stranded on the operand
 * stack — visible in `cs2 disassemble` as a running depth that never returns to
 * zero, and in the round trip as 491 scripts three bytes short. That is a
 * miscompile and not only a size difference: the next statement's arguments sit
 * one slot off from where they were written.
 *
 * The last instruction of the statement is its top-level call — everything
 * before it is arguments, which the call consumed — so the shape to drop is
 * that one instruction's result shape. Values come off top-first, so the
 * discards are emitted in reverse of the order the results were pushed.
 *
 * A call whose result shape is not statically known emits nothing rather than a
 * guess: a wrong discard count desynchronises the stack in the same way the
 * missing one did. The db family used to be in that category and no longer is —
 * its shape is in the dbtable config, which the caller now supplies through
 * `RSCache_CS2_CompileOptions.db_columns`, exactly as the decompiler is given
 * it. `enum` and `param` remain, their shape being an argument this cannot see
 * from the finished instruction list.
 */
/**
 * What a db call leaves on the stacks, from the `dbcolumn` literal it was given.
 *
 * The literal packs `(table << 12) | (column << 4) | (field + 1)` and field 0
 * means the whole tuple, so the same opcode on the same column pushes four
 * values or one depending on the low nibble (EXCEPTIONS G8). Returns the count,
 * or -1 when there is no provider or the column is not in this cache — a
 * refusal, not an empty column.
 *
 * `db_find` and friends push one int however wide the column is: the count of
 * matching rows, not the rows.
 */
static int
cs2_cc_db_result_stacks(
    struct cs2_cc_compiler* cc,
    enum RSCache_CS2_CommandKind kind,
    int packed,
    enum RSCache_CS2_StackType* out,
    int capacity)
{
    if( kind == RSCACHE_CS2_CMD_DB_FIND )
    {
        if( capacity < 1 )
            return -1;
        out[0] = RSCACHE_CS2_STACK_INT;
        return 1;
    }

    if( !cc->options || !cc->options->db_columns.load )
        return -1;

    int table_id = (packed >> 12) & 0xFFFFF;
    int column_id = (packed >> 4) & 0xFF;
    int field = (packed & 0xF) - 1;

    enum RSCache_CS2_Type types[32];
    int count = cc->options->db_columns.load(
        cc->options->db_columns.user, table_id, column_id, types, 32);
    if( count <= 0 )
        return -1;

    if( field < 0 )
    {
        if( count > capacity )
            return -1;
        for( int i = 0; i < count; i++ )
            out[i] = RSCache_CS2_TypeStackType(types[i]);
        return count;
    }
    if( field >= count || capacity < 1 )
        return -1;
    out[0] = RSCache_CS2_TypeStackType(types[field]);
    return 1;
}

static void
cs2_cc_emit_call_discards(struct cs2_cc_compiler* cc, int mark)
{
    if( cc->failed || cc->ops.count <= mark )
        return;

    int index = cc->ops.count - 1;
    int opcode = cc->ops.opcodes[index];
    enum RSCache_CS2_StackType stacks[64];
    int count = 0;

    if( opcode == RSCACHE_CS2_OP_GOSUB_WITH_PARAMS )
    {
        if( !cc->options || !cc->options->scripts.load )
            return;
        const struct RSCache_CS2_Script* callee =
            cc->options->scripts.load(cc->options->scripts.user, cc->ops.int_operands[index]);
        if( !callee )
            return;
        count = RSCache_CS2_ScriptReturnTypes(callee, stacks, 64);
        if( count < 0 )
            return;
    }
    else
    {
        const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
        if( !info )
            return;
        switch( info->kind )
        {
        case RSCACHE_CS2_CMD_BASIC:
        case RSCACHE_CS2_CMD_TYPED_POP:
            for( int i = 0; i < info->def_count && count < 64; i++ )
            {
                const struct RSCache_CS2_Prototype* proto =
                    RSCache_CS2_ProtoGet(RSCache_CS2_CommandDef(info, i));
                if( !proto )
                    return;
                stacks[count++] = RSCache_CS2_TypeStackType(proto->type);
            }
            break;
        case RSCACHE_CS2_CMD_JOIN_STRING:
            stacks[count++] = RSCACHE_CS2_STACK_STRING;
            break;
        case RSCACHE_CS2_CMD_PUSH_ARRAY_INT:
            stacks[count++] = RSCACHE_CS2_STACK_INT;
            break;
        case RSCACHE_CS2_CMD_DB_FIND:
            /* One int — the number of matching rows — however wide the column
             * is, so no operand needs reading. */
            count = cs2_cc_db_result_stacks(cc, info->kind, 0, stacks, 64);
            if( count < 0 )
                return;
            break;
        case RSCACHE_CS2_CMD_DB_GETFIELD:
        {
            /* The `dbcolumn` literal is the argument that says what comes back.
             * It is the second of `(dbrow, dbcolumn, index)`, and a decompiled
             * db call writes its ids as literals, so it is the instruction two
             * before the call. Anything else and the shape is unread and
             * nothing is dropped. */
            if( index - 2 < mark )
                return;
            if( cc->ops.opcodes[index - 2] != RSCACHE_CS2_OP_PUSH_CONSTANT_INT )
                return;
            count = cs2_cc_db_result_stacks(cc, info->kind, cc->ops.int_operands[index - 2],
                                            stacks, 64);
            if( count < 0 )
                return;
            break;
        }
        default:
            /* Every other kind either pushes nothing (assign, discard, branch,
             * return, switch, define_array) or has a shape only an argument
             * knows and this cannot recover (param, enum). Neither wants a
             * discard from here. */
            return;
        }
    }

    for( int i = count - 1; i >= 0; i-- )
    {
        cs2_cc_emit(cc,
                    stacks[i] == RSCACHE_CS2_STACK_STRING ? RSCACHE_CS2_OP_POP_STRING_DISCARD
                                                          : RSCACHE_CS2_OP_POP_INT_DISCARD,
                    0);
    }
}

static void
cs2_cc_statement(struct cs2_cc_compiler* cc)
{
    if( cc->failed )
        return;

    if( cs2_cc_accept_punct(cc, ';') )
        return;

    if( cs2_cc_at_ident(cc, "if") )
    {
        cs2_cc_next(cc);
        cs2_cc_if(cc);
        return;
    }
    if( cs2_cc_at_ident(cc, "while") )
    {
        cs2_cc_next(cc);
        cs2_cc_while(cc);
        return;
    }
    if( cs2_cc_at_ident(cc, "return") )
    {
        cs2_cc_next(cc);
        cs2_cc_return(cc);
        cs2_cc_accept_punct(cc, ';');
        return;
    }
    if( cc->token.kind == CS2_CC_TOK_IDENT && strncmp(cc->token.text, "switch_", 7) == 0 )
    {
        enum RSCache_CS2_Type subject = RSCache_CS2_TypeOfLiteral(cc->token.text + 7);
        if( subject == RSCACHE_CS2_TYPE_NONE )
        {
            cs2_cc_fail(cc, "'%s' does not name a type", cc->token.text);
            return;
        }
        cs2_cc_next(cc);
        cs2_cc_switch(cc, subject);
        return;
    }

    /* `def_<type> $name ...` — a declaration, and for an array also its size. */
    if( cc->token.kind == CS2_CC_TOK_IDENT && strncmp(cc->token.text, "def_", 4) == 0 )
    {
        enum RSCache_CS2_Type type = RSCache_CS2_TypeOfLiteral(cc->token.text + 4);
        if( type == RSCACHE_CS2_TYPE_NONE )
        {
            cs2_cc_fail(cc, "'%s' does not name a type", cc->token.text);
            return;
        }
        cs2_cc_next(cc);
        if( cc->token.kind != CS2_CC_TOK_LOCAL )
        {
            cs2_cc_fail(cc, "expected a local after def_");
            return;
        }
        const char* name = cc->token.text;
        cs2_cc_next(cc);
        int index = cs2_cc_declare_local(cc, name, type, false);
        if( index < 0 )
            return;

        if( cs2_cc_accept_punct(cc, '(') )
        {
            cs2_cc_expression(cc, RSCACHE_CS2_TYPE_INT);
            cs2_cc_expect_punct(cc, ')');
            /* The operand packs the slot above the element type descriptor. */
            cs2_cc_emit(
                cc,
                RSCACHE_CS2_OP_DEFINE_ARRAY,
                (cc->locals[index].slot << 16) | RSCache_CS2_TypeDesc(type));
            cs2_cc_accept_punct(cc, ';');
            return;
        }
        if( !cs2_cc_expect_punct(cc, '=') )
            return;
        cs2_cc_expression(cc, type);
        cs2_cc_emit(
            cc,
            cc->locals[index].is_string ? RSCACHE_CS2_OP_POP_STRING_LOCAL
                                        : RSCACHE_CS2_OP_POP_INT_LOCAL,
            cc->locals[index].slot);
        cs2_cc_accept_punct(cc, ';');
        return;
    }

    /* An assignment, or a command called for its effect. Both start with a
     * name, so the '=' (or ',') decides. */
    if( cc->token.kind == CS2_CC_TOK_LOCAL || cc->token.kind == CS2_CC_TOK_GLOBAL )
    {
        struct cs2_cc_token targets[64];
        int target_count = 0;
        bool array_store = false;

        for( ;; )
        {
            if( cc->token.kind != CS2_CC_TOK_LOCAL && cc->token.kind != CS2_CC_TOK_GLOBAL )
            {
                cs2_cc_fail(cc, "expected an assignment target");
                return;
            }
            if( target_count == (int)(sizeof(targets) / sizeof(targets[0])) )
            {
                cs2_cc_fail(cc, "too many assignment targets");
                return;
            }
            targets[target_count++] = cc->token;
            cs2_cc_next(cc);

            if( target_count == 1 && cs2_cc_at_punct(cc, '(') )
            {
                /* `$intarray0(i) = v;` — an element store. */
                array_store = true;
                break;
            }
            if( !cs2_cc_accept_punct(cc, ',') )
                break;
        }

        if( array_store )
        {
            /* An array outlives a `gosub`, so a proc stores into one its caller
             * defined and this script holds no `def_`. The name still says which
             * slot and which element type. */
            int index = cs2_cc_find_local(cc, targets[0].text);
            if( index < 0 )
                index = cs2_cc_declare_local_from_name(cc, targets[0].text);
            if( index < 0 || !cc->locals[index].is_array )
            {
                cs2_cc_fail(cc, "'$%s' is not an array", targets[0].text);
                return;
            }
            cs2_cc_next(cc);
            cs2_cc_argument(cc, RSCACHE_CS2_TYPE_INT);
            if( !cs2_cc_expect_punct(cc, ')') || !cs2_cc_expect_punct(cc, '=') )
                return;
            cs2_cc_expression(cc, cc->locals[index].type);
            cs2_cc_emit(cc, RSCACHE_CS2_OP_POP_ARRAY_INT, cc->locals[index].slot);
            cs2_cc_accept_punct(cc, ';');
            return;
        }

        if( !cs2_cc_expect_punct(cc, '=') )
            return;
        /* The right-hand side is itself a list: `$a, $b = $c, $d` is as legal as
         * `$a, $b = ~two_results`, and one expression may fill several slots.
         * When the RHS is a comma-list paired 1:1 with the targets, each slot's
         * type is the expected type for that expression — otherwise a quoted
         * graphic stays a string and the following POP_INT_LOCAL underflows. */
        {
            int rhs_index = 0;
            for( ;; )
            {
                enum RSCache_CS2_Type expected = RSCACHE_CS2_TYPE_NONE;
                if( rhs_index < target_count )
                    expected = cs2_cc_assign_target_type(cc, &targets[rhs_index]);
                cs2_cc_expression(cc, expected);
                rhs_index++;
                if( cc->failed || !cs2_cc_accept_punct(cc, ',') )
                    break;
            }
        }
        /* Values come off the stack top-first, so the last target stores first. */
        for( int i = target_count - 1; i >= 0 && !cc->failed; i-- )
            cs2_cc_emit_store(cc, &targets[i]);
        cs2_cc_accept_punct(cc, ';');
        return;
    }

    /* A bare command call. */
    {
        int mark = cc->ops.count;
        cs2_cc_expression(cc, RSCACHE_CS2_TYPE_NONE);
        cs2_cc_emit_call_discards(cc, mark);
    }
    cs2_cc_accept_punct(cc, ';');
}

/**
 * Run statements until `stop`, refusing to loop on one that consumes nothing.
 *
 * Every statement must advance the lexer. A parse bug that leaves the cursor
 * where it was would otherwise spin silently, so it is reported against the
 * token that could not be consumed.
 */
static void
cs2_cc_statements_until(struct cs2_cc_compiler* cc, char stop)
{
    while( !cc->failed && cc->token.kind != CS2_CC_TOK_END )
    {
        if( stop && cs2_cc_at_punct(cc, stop) )
            return;
        int before_position = cc->position;
        enum cs2_cc_token_kind before_kind = cc->token.kind;
        const char* before_text = cc->token.text;
        char before_punct = cc->token.punct;

        cs2_cc_statement(cc);

        if( cc->failed )
            return;
        if( cc->position == before_position && cc->token.kind == before_kind &&
            cc->token.text == before_text && cc->token.punct == before_punct )
        {
            cs2_cc_fail(
                cc,
                "cannot parse a statement starting at '%s'",
                before_text ? before_text : (const char[]){ before_punct, '\0' });
            return;
        }
    }
}

static void
cs2_cc_block(struct cs2_cc_compiler* cc)
{
    if( !cs2_cc_expect_punct(cc, '{') )
        return;
    cs2_cc_statements_until(cc, '}');
    cs2_cc_expect_punct(cc, '}');
}

/* -------------------------------------------------------------------------
 * Signature
 * ---------------------------------------------------------------------- */

/** `[trigger,name](type $a, …)(rettype, …)` */
static void
cs2_cc_signature(struct cs2_cc_compiler* cc)
{
    if( !cs2_cc_expect_punct(cc, '[') )
        return;
    if( cc->token.kind != CS2_CC_TOK_IDENT )
    {
        cs2_cc_fail(cc, "expected a trigger name");
        return;
    }
    if( RSCache_CS2_TriggerOfName(cc->token.text) == RSCACHE_CS2_TRIGGER_NONE )
    {
        cs2_cc_fail(cc, "'%s' is not a trigger", cc->token.text);
        return;
    }
    cs2_cc_next(cc);
    if( !cs2_cc_expect_punct(cc, ',') )
        return;
    /* The name may be a bare integer for the synthesised forms. */
    while( !cs2_cc_at_punct(cc, ']') && cc->token.kind != CS2_CC_TOK_END )
        cs2_cc_next(cc);
    if( !cs2_cc_expect_punct(cc, ']') )
        return;

    if( cs2_cc_accept_punct(cc, '(') )
    {
        while( !cs2_cc_at_punct(cc, ')') && !cc->failed && cc->token.kind != CS2_CC_TOK_END )
        {
            if( cc->token.kind != CS2_CC_TOK_IDENT )
            {
                cs2_cc_fail(cc, "expected an argument type");
                return;
            }
            /* An array argument's type is written `intarray`, not `int`. */
            const char* type_text = cc->token.text;
            size_t type_length = strlen(type_text);
            char base[64];
            if( type_length > 5 && strcmp(type_text + type_length - 5, "array") == 0 )
                type_length -= 5;
            if( type_length >= sizeof(base) )
            {
                cs2_cc_fail(cc, "argument type name is too long");
                return;
            }
            memcpy(base, type_text, type_length);
            base[type_length] = '\0';

            enum RSCache_CS2_Type type = RSCache_CS2_TypeOfLiteral(base);
            if( type == RSCACHE_CS2_TYPE_NONE )
            {
                cs2_cc_fail(cc, "'%s' does not name a type", base);
                return;
            }
            cs2_cc_next(cc);
            if( cc->token.kind != CS2_CC_TOK_LOCAL )
            {
                cs2_cc_fail(cc, "expected an argument name");
                return;
            }
            if( cs2_cc_declare_local(cc, cc->token.text, type, true) < 0 )
                return;
            cs2_cc_next(cc);
            if( !cs2_cc_accept_punct(cc, ',') )
                break;
        }
        if( !cs2_cc_expect_punct(cc, ')') )
            return;
    }

    if( cs2_cc_accept_punct(cc, '(') )
    {
        while( !cs2_cc_at_punct(cc, ')') && !cc->failed && cc->token.kind != CS2_CC_TOK_END )
        {
            if( cc->token.kind != CS2_CC_TOK_IDENT )
            {
                cs2_cc_fail(cc, "expected a return type");
                return;
            }
            enum RSCache_CS2_Type type = RSCache_CS2_TypeOfLiteral(cc->token.text);
            if( type == RSCACHE_CS2_TYPE_NONE )
            {
                cs2_cc_fail(cc, "'%s' does not name a type", cc->token.text);
                return;
            }
            if( cc->return_type_count <
                (int)(sizeof(cc->return_types) / sizeof(cc->return_types[0])) )
                cc->return_types[cc->return_type_count++] = type;
            cs2_cc_next(cc);
            if( !cs2_cc_accept_punct(cc, ',') )
                break;
        }
        cs2_cc_expect_punct(cc, ')');
    }
}

/* -------------------------------------------------------------------------
 * Peephole: unconditional branches to the next instruction
 * ---------------------------------------------------------------------- */

/** True for the seven opcodes whose operand is a relative jump target. */
static bool
cs2_cc_is_branch(int opcode)
{
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
    if( !info )
        return false;
    return info->kind == RSCACHE_CS2_CMD_BRANCH || info->kind == RSCACHE_CS2_CMD_BRANCH_COMPARE;
}

/**
 * Find the pc of the SWITCH instruction that owns table `table`.
 *
 * Case targets are stored relative to the instruction after their SWITCH, so
 * renumbering them needs that instruction's address, and the table index is the
 * SWITCH's own operand. One table is emitted per SWITCH, so the first match is
 * the only match.
 */
static int
cs2_cc_switch_pc(const struct cs2_cc_compiler* cc, int table)
{
    for( int i = 0; i < cc->ops.count; i++ )
    {
        if( cc->ops.opcodes[i] == RSCACHE_CS2_OP_SWITCH && cc->ops.int_operands[i] == table )
            return i;
    }
    return -1;
}

/**
 * Delete unconditional branches whose target is the instruction that follows
 * them, and renumber every jump and case target around the deletion.
 *
 * The compiler emits the jump over an `else` unconditionally, so an `if` with
 * no `else` ends in `branch 0` — six bytes that jump to the next instruction
 * and do nothing. Jagex's compiler does not emit it, and that one instruction
 * was the single largest cause of a round trip coming back with different
 * bytes: 2,148 scripts differed by exactly six bytes, and thousands more by a
 * multiple of six.
 *
 * Doing it as a post-pass rather than by not emitting the jump is deliberate.
 * `cs2_cc_if` cannot know whether an `else` follows until after the body is
 * parsed and the jump is already placed, and the same redundant jump falls out
 * of `while`, `switch` and every nested combination of them. One rule over the
 * finished instruction list covers all of them and cannot get the bookkeeping
 * out of step with the parser.
 *
 * Iterated to a fixed point: deleting one branch can leave the branch before it
 * pointing at what is now the next instruction.
 */
static void
cs2_cc_drop_redundant_branches(struct cs2_cc_compiler* cc)
{
    if( cc->failed || cc->ops.count <= 0 )
        return;

    int* absolute = (int*)malloc((size_t)cc->ops.count * sizeof(int));
    int* mapping = (int*)malloc((size_t)(cc->ops.count + 1) * sizeof(int));
    int* switch_pcs = NULL;
    if( cc->switch_count > 0 )
        switch_pcs = (int*)malloc((size_t)cc->switch_count * sizeof(int));
    if( !absolute || !mapping || (cc->switch_count > 0 && !switch_pcs) )
    {
        free(absolute);
        free(mapping);
        free(switch_pcs);
        return; /* Nothing is lost by skipping the pass: the code is correct. */
    }

    for( ;; )
    {
        int count = cc->ops.count;
        bool removing = false;

        for( int i = 0; i < count; i++ )
        {
            /* The trailing instruction has nowhere to fall through to, so a
             * branch there is not redundant even with an operand of 0. */
            if( cc->ops.opcodes[i] == RSCACHE_CS2_OP_BRANCH && cc->ops.int_operands[i] == 0 &&
                i + 1 < count )
            {
                removing = true;
                break;
            }
        }
        if( !removing )
            break;

        /* Relative -> absolute, so the targets survive the renumbering. */
        for( int i = 0; i < count; i++ )
            absolute[i] = cs2_cc_is_branch(cc->ops.opcodes[i]) ? i + 1 + cc->ops.int_operands[i]
                                                               : 0;
        for( int t = 0; t < cc->switch_count; t++ )
        {
            switch_pcs[t] = cs2_cc_switch_pc(cc, t);
            for( int j = 0; j < cc->switches[t].count; j++ )
            {
                if( switch_pcs[t] >= 0 )
                    cc->switches[t].targets[j] += switch_pcs[t] + 1;
            }
        }

        /* old pc -> new pc. A deleted instruction maps to whatever now sits
         * where it was, which is exactly where its branch would have gone. */
        int written = 0;
        for( int i = 0; i < count; i++ )
        {
            bool drop = cc->ops.opcodes[i] == RSCACHE_CS2_OP_BRANCH &&
                        cc->ops.int_operands[i] == 0 && i + 1 < count;
            mapping[i] = written;
            if( !drop )
            {
                if( written != i )
                {
                    cc->ops.opcodes[written] = cc->ops.opcodes[i];
                    cc->ops.int_operands[written] = cc->ops.int_operands[i];
                    cc->ops.string_operands[written] = cc->ops.string_operands[i];
                    cc->ops.string_operands[i] = NULL;
                    absolute[written] = absolute[i];
                }
                written++;
            }
            else
            {
                free(cc->ops.string_operands[i]);
                cc->ops.string_operands[i] = NULL;
            }
        }
        mapping[count] = written;
        cc->ops.count = written;

        /* Absolute -> relative, through the map. */
        for( int i = 0; i < written; i++ )
        {
            if( !cs2_cc_is_branch(cc->ops.opcodes[i]) )
                continue;
            int target = absolute[i];
            if( target < 0 )
                target = 0;
            if( target > count )
                target = count;
            cc->ops.int_operands[i] = mapping[target] - i - 1;
        }
        for( int t = 0; t < cc->switch_count; t++ )
        {
            int pc = switch_pcs[t] >= 0 ? mapping[switch_pcs[t]] : -1;
            for( int j = 0; j < cc->switches[t].count; j++ )
            {
                int target = cc->switches[t].targets[j];
                if( target < 0 )
                    target = 0;
                if( target > count )
                    target = count;
                cc->switches[t].targets[j] = pc >= 0 ? mapping[target] - pc - 1 : mapping[target];
            }
        }
    }

    free(absolute);
    free(mapping);
    free(switch_pcs);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_Compile(
    const char* source,
    const struct RSCache_CS2_CompileOptions* options,
    struct RSCache_ClientScript* out,
    char* error,
    int error_capacity)
{
    if( error && error_capacity > 0 )
        error[0] = '\0';

    struct cs2_cc_compiler cc;
    memset(&cc, 0, sizeof(cc));
    RSCache_CS2_ArenaInit(&cc.arena);
    cs2_cc_ops_init(&cc.ops);
    cc.options = options;
    cc.source = source;
    cc.line = 1;
    cc.script_id = -1;
    cc.error = error;
    cc.error_capacity = error_capacity;

    /* Generated source uses comments as a lossless side channel for serialized
     * facts the structured language cannot express. Hand-written source keeps
     * canonical behavior because these comments are optional. */
    {
        const char* frame = strstr(source, "// @rscache-frame ");
        if( frame )
        {
            int li = 0, ls = 0, ai = 0, as = 0;
            if( sscanf(frame, "// @rscache-frame %d %d %d %d", &li, &ls, &ai, &as) == 4 &&
                li >= 0 && ls >= 0 && ai >= 0 && as >= 0 )
            {
                cc.frame_counts_override = true;
                cc.override_local_int_count = li;
                cc.override_local_string_count = ls;
                cc.override_int_argument_count = ai;
                cc.override_string_argument_count = as;
            }
        }

        const char* epilogue = strstr(source, "// @rscache-epilogue ");
        if( epilogue )
        {
            epilogue += strlen("// @rscache-epilogue ");
            while( *epilogue && *epilogue != '\n' && *epilogue != '\r' )
            {
                while( *epilogue == ' ' || *epilogue == '\t' )
                    epilogue++;
                char* end = NULL;
                long slot = strtol(epilogue, &end, 10);
                if( end == epilogue || slot < 0 || slot >= 128 || *end != '=' )
                    break;
                epilogue = end + 1;
                long value = strtol(epilogue, &end, 10);
                if( end == epilogue )
                    break;
                cc.return_default_override[slot] = true;
                cc.return_default_values[slot] = (int)value;
                epilogue = end;
            }
        }
    }

    /* The leading `// <id>` comment is the only place the script's own id
     * appears; the lexer skips comments, so it is read directly. */
    {
        const char* cursor = source;
        while( *cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' )
            cursor++;
        if( cursor[0] == '/' && cursor[1] == '/' )
        {
            cursor += 2;
            while( *cursor == ' ' )
                cursor++;
            if( isdigit((unsigned char)*cursor) )
                cc.script_id = atoi(cursor);
        }
    }

    cs2_cc_next(&cc);
    cs2_cc_signature(&cc);
    cs2_cc_statements_until(&cc, 0);

    /*
     * The epilogue — one default per declared return type, then a `return`.
     *
     * Emitted for every script, unconditionally, and it is the *only* thing
     * emitted here. It has two jobs at once, which is why one suffices: it is
     * where the decoder reads the return arity and stack shape back off, and it
     * is the path a body that falls out of the bottom takes.
     *
     * This used to add a bare `return` first whenever the body did not end in
     * one. That is right for a script with no declared returns — the epilogue
     * *is* that bare return — and wrong for one that has them: the extra
     * instruction returns with an empty stack where the declared arity says
     * otherwise, which is a miscompile, and it displaced the epilogue by one so
     * every `break` in a trailing `switch` aimed at the wrong address. Script
     * 376 is the smallest example, 13 instructions in the cache against 15.
     */
    if( !cc.failed )
    {
        for( int i = 0; i < cc.return_type_count; i++ )
        {
            if( RSCache_CS2_TypeStackType(cc.return_types[i]) == RSCACHE_CS2_STACK_STRING )
                cs2_cc_emit_string(&cc, "");
            else
                cs2_cc_emit(&cc, RSCACHE_CS2_OP_PUSH_CONSTANT_INT,
                            cc.return_default_override[i]
                                ? cc.return_default_values[i]
                                : RSCache_CS2_TypeEpilogueDefault(cc.return_types[i]));
        }
        cs2_cc_emit(&cc, RSCACHE_CS2_OP_RETURN, 0);
    }

    /* After the epilogue, so the instruction list is final and the last op is
     * always a RETURN. */
    cs2_cc_drop_redundant_branches(&cc);

    bool ok = !cc.failed;
    if( ok )
    {
        memset(out, 0, sizeof(*out));
        struct RSCache_CS2_Script* script = &out->script;
        RSCache_CS2_ScriptInit(script);
        script->script_id = cc.script_id;
        script->signature = (char*)calloc(1, 1);
        script->op_count = cc.ops.count;
        script->opcodes = cc.ops.opcodes;
        script->int_operands = cc.ops.int_operands;
        script->string_operands = cc.ops.string_operands;
        script->long_operands = (int64_t*)calloc((size_t)cc.ops.count, sizeof(int64_t));
        script->int_argument_count = cc.frame_counts_override ? cc.override_int_argument_count
                                                              : cc.int_argument_count;
        script->string_argument_count = cc.frame_counts_override
                                            ? cc.override_string_argument_count
                                            : cc.string_argument_count;
        script->local_int_count = cc.frame_counts_override ? cc.override_local_int_count
                                                           : cc.int_local_count;
        script->local_string_count = cc.frame_counts_override
                                         ? cc.override_local_string_count
                                         : cc.string_local_count;

        if( RSCache_CS2_ScriptAllocSwitches(script, cc.switch_count) )
        {
            for( int i = 0; i < cc.switch_count; i++ )
            {
                if( !RSCache_CS2_ScriptAllocSwitchCases(script, i, cc.switches[i].count) )
                {
                    ok = false;
                    break;
                }
                for( int j = 0; j < cc.switches[i].count; j++ )
                {
                    script->switch_tables[i].cases[j].key = cc.switches[i].keys[j];
                    script->switch_tables[i].cases[j].target_pc = cc.switches[i].targets[j];
                }
            }
        }
        else
        {
            ok = false;
        }

        if( ok )
        {
            /* The op arrays now belong to the script. */
            memset(&cc.ops, 0, sizeof(cc.ops));
        }
        else
        {
            RSCache_CS2_ScriptFree(script);
            if( error && error_capacity > 0 && !error[0] )
                snprintf(error, (size_t)error_capacity, "out of memory");
        }
    }

    cs2_cc_ops_free(&cc.ops);
    for( int i = 0; i < cc.switch_count; i++ )
    {
        free(cc.switches[i].keys);
        free(cc.switches[i].targets);
    }
    RSCache_CS2_ArenaFree(&cc.arena);
    return ok;
}
