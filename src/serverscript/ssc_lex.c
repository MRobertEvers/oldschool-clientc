#include "ssc_lex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident_char(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int
is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int
is_letter(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/**
 * Is `c` a character that can only ever close a name, never open a right
 * operand? `read_ident`'s trailing-sign rule needs this for cache names that
 * end the sign run right at the name's own end — `weapon_poison+`,
 * `antidote++`, `unfinished_weapon_poison++` — where nothing ident-shaped
 * follows the last sign at all. The same whitespace-around-operators
 * invariant `read_ident`'s header already relies on makes this safe: a
 * `calc()` operator immediately followed by one of these, with no space, is
 * not a token this corpus ever writes, because it would leave the operator
 * without a right-hand side.
 */
static int
is_name_boundary(int c)
{
    return c == ')' || c == ']' || c == ',' || c == ':' ||
           c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
}

/**
 * Does the run starting at `pos` spell a name rather than a number?
 *
 * Names may begin with digits — `3dose1strength`, `2_handedsign` — and some
 * begin with something a coord literal is indistinguishable from:
 * `0_41_53_compofishspot` opens exactly like `0_41_53_9_20`. Consuming digits
 * and underscores and then looking for a letter is what tells them apart, and
 * it has to scan the whole run: stopping at the first underscore would read
 * `0_41_53` as a coord and leave a stray identifier behind.
 */
static int
looks_like_identifier(const struct SSC_Lexer* lexer, size_t pos)
{
    /* Hex literals open with a digit followed by a letter, which is exactly the
     * shape this function otherwise calls a name — so 0x has to be settled
     * first or `0xFFFFFFFF` lexes as an identifier. */
    if( pos + 1 < lexer->length && lexer->source[pos] == '0' &&
        (lexer->source[pos + 1] == 'x' || lexer->source[pos + 1] == 'X') )
        return 0;

    while( pos < lexer->length && (is_digit(lexer->source[pos]) || lexer->source[pos] == '_') )
        pos++;
    return pos < lexer->length && is_letter(lexer->source[pos]);
}

void
SSC_LexInit(
    struct SSC_Lexer* lexer,
    const char* source,
    size_t length,
    const char* file)
{
    memset(lexer, 0, sizeof(*lexer));
    lexer->source = source;
    lexer->length = length;
    lexer->line = 1;
    snprintf(lexer->file, sizeof(lexer->file), "%s", file ? file : "");
}

static void
skip_trivia(struct SSC_Lexer* lexer)
{
    while( lexer->pos < lexer->length )
    {
        char c = lexer->source[lexer->pos];

        if( c == '\n' )
        {
            lexer->line++;
            lexer->pos++;
        }
        else if( c == ' ' || c == '\t' || c == '\r' )
        {
            lexer->pos++;
        }
        else if( c == '/' && lexer->pos + 1 < lexer->length &&
                 lexer->source[lexer->pos + 1] == '/' )
        {
            while( lexer->pos < lexer->length && lexer->source[lexer->pos] != '\n' )
                lexer->pos++;
        }
        else if( c == '/' && lexer->pos + 1 < lexer->length &&
                 lexer->source[lexer->pos + 1] == '*' )
        {
            lexer->pos += 2;
            while( lexer->pos + 1 < lexer->length &&
                   !(lexer->source[lexer->pos] == '*' && lexer->source[lexer->pos + 1] == '/') )
            {
                if( lexer->source[lexer->pos] == '\n' )
                    lexer->line++;
                lexer->pos++;
            }
            lexer->pos += 2;
            if( lexer->pos > lexer->length )
                lexer->pos = lexer->length;
        }
        else
        {
            break;
        }
    }
}

/**
 * Read a name, allowing the `interface:component` qualified form.
 *
 * Names may also contain `+` and `-`: 21 obj names look like
 * `premade_cheese+tom_batta`, and godsword shards are `godwars_godsword_blade1+2`
 * (digit after the sign). One midi is `music_Jolly-R`. Those are the
 * same characters `calc()` uses as operators, so the two are told apart by
 * spacing — a sign continues a name only when it is tight against a letter or
 * digit on the far side. That is safe because no calc() expression in the whole
 * corpus writes an operator without surrounding whitespace, and a `$local`
 * after the sign never merges either way.
 *
 * Some names end the sign run at the name's own end rather than opening a
 * further letter/digit run — `weapon_poison+`, `antidote++`,
 * `unfinished_weapon_poison++` are real cache bracket names, and the second
 * one has TWO consecutive signs with nothing ident-shaped after either. The
 * same whitespace invariant covers this: a sign tight against a name and
 * immediately followed by a boundary character (`)`, `]`, `,`, whitespace,
 * …) or another sign can only be part of the name, never an operator — a
 * real operator there would have nothing to its right. `is_name_boundary`
 * is that second check; it only fires once at least one ident/sign
 * character has already been consumed (`lexer->pos > start`), so a bare
 * leading `+`/`-` token is untouched.
 */
static void
read_ident(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    size_t start = lexer->pos;
    size_t length;

    for( ;; )
    {
        while( lexer->pos < lexer->length && is_ident_char(lexer->source[lexer->pos]) )
            lexer->pos++;

        if( lexer->pos + 1 < lexer->length &&
            (lexer->source[lexer->pos] == '+' || lexer->source[lexer->pos] == '-') &&
            (is_ident_start(lexer->source[lexer->pos + 1]) ||
             is_digit(lexer->source[lexer->pos + 1])) )
        {
            lexer->pos++;
            continue;
        }

        if( lexer->pos > start && lexer->pos < lexer->length &&
            (lexer->source[lexer->pos] == '+' || lexer->source[lexer->pos] == '-') &&
            (lexer->pos + 1 >= lexer->length ||
             lexer->source[lexer->pos + 1] == '+' ||
             lexer->source[lexer->pos + 1] == '-' ||
             is_name_boundary(lexer->source[lexer->pos + 1])) )
        {
            lexer->pos++;
            continue;
        }
        break;
    }

    /* A single colon joins two halves of one name (`multi2:com_1`). A colon
     * followed by anything else is punctuation — switch cases end with one. */
    if( lexer->pos + 1 < lexer->length && lexer->source[lexer->pos] == ':' &&
        is_ident_start(lexer->source[lexer->pos + 1]) )
    {
        lexer->pos++;
        while( lexer->pos < lexer->length && is_ident_char(lexer->source[lexer->pos]) )
            lexer->pos++;
    }

    length = lexer->pos - start;
    if( length >= sizeof(token->text) )
        length = sizeof(token->text) - 1;
    memcpy(token->text, lexer->source + start, length);
    token->text[length] = '\0';
}

/**
 * Read a number, or a coord literal.
 *
 * `0_49_50_3_11` is level_mx_mz_lx_lz packed into one int the same way the
 * reference's compiler packs it. It starts like an integer and only reveals
 * itself at the first underscore, so the two cases share an entry point.
 */
static void
read_number(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    size_t start = lexer->pos;
    int parts[5];
    int part_count = 0;
    int negative = 0;

    if( lexer->source[lexer->pos] == '-' )
    {
        negative = 1;
        lexer->pos++;
    }

    if( lexer->pos + 1 < lexer->length && lexer->source[lexer->pos] == '0' &&
        (lexer->source[lexer->pos + 1] == 'x' || lexer->source[lexer->pos + 1] == 'X') )
    {
        long value;
        char* end = NULL;

        value = strtol(lexer->source + lexer->pos, &end, 16);
        lexer->pos = (size_t)(end - lexer->source);
        token->kind = SSC_TOK_INT;
        token->value = (int32_t)(negative ? -value : value);
        snprintf(token->text, sizeof(token->text), "%d", token->value);
        return;
    }

    while( part_count < 5 )
    {
        int value = 0;
        int digits = 0;

        while( lexer->pos < lexer->length && is_digit(lexer->source[lexer->pos]) )
        {
            value = value * 10 + (lexer->source[lexer->pos] - '0');
            lexer->pos++;
            digits++;
        }
        if( !digits )
            break;
        parts[part_count++] = value;

        if( lexer->pos < lexer->length && lexer->source[lexer->pos] == '_' &&
            lexer->pos + 1 < lexer->length && is_digit(lexer->source[lexer->pos + 1]) )
        {
            lexer->pos++;
            continue;
        }
        break;
    }

    token->kind = SSC_TOK_INT;
    if( part_count == 5 )
    {
        /* level << 28 | mx << 20 | mz << 6 ... is the reference's packing:
         * (level << 28) | ((mx * 64 + lx) << 14) | (mz * 64 + lz). */
        token->value = (int32_t)(((uint32_t)parts[0] << 28) |
                                 ((uint32_t)(parts[1] * 64 + parts[3]) << 14) |
                                 (uint32_t)(parts[2] * 64 + parts[4]));
    }
    else
    {
        token->value = negative ? -parts[0] : parts[0];
    }

    {
        size_t length = lexer->pos - start;

        if( length >= sizeof(token->text) )
            length = sizeof(token->text) - 1;
        memcpy(token->text, lexer->source + start, length);
        token->text[length] = '\0';
    }
}

/**
 * Read a string literal, including any `<...>` it carries.
 *
 * Angle brackets are content rather than syntax at this level: `<$name>` and
 * `<tostring($level)>` are interpolations the parser expands later, `<br>` and
 * `<p,happy>` are markup the client renders. Both pass through untouched.
 *
 * The catch is that an interpolation can contain a *quoted string of its own* —
 * `<text_gender("man", "woman")>` — so a bare `"` only ends the literal at
 * bracket depth zero. Terminating on the first quote regardless truncates the
 * literal mid-interpolation and then lexes the remainder as code.
 */
static void
read_string(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    size_t out = 0;
    int depth = 0;

    lexer->pos++; /* opening quote */
    while( lexer->pos < lexer->length )
    {
        char c = lexer->source[lexer->pos];

        if( c == '"' && depth == 0 )
            break;
        if( c == '<' )
            depth++;
        else if( c == '>' && depth > 0 )
            depth--;

        if( c == '\n' )
            lexer->line++;

        if( out + 1 < sizeof(token->text) )
            token->text[out++] = c;
        lexer->pos++;
    }
    if( lexer->pos < lexer->length )
        lexer->pos++; /* closing quote */

    token->text[out] = '\0';
    token->kind = SSC_TOK_STRING;
}

static void
scan(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    char c;

    memset(token, 0, sizeof(*token));
    skip_trivia(lexer);
    token->line = lexer->line;

    if( lexer->pos >= lexer->length )
    {
        token->kind = SSC_TOK_EOF;
        return;
    }

    c = lexer->source[lexer->pos];

    if( c == '"' )
    {
        read_string(lexer, token);
        return;
    }

    if( is_digit(c) )
    {
        if( looks_like_identifier(lexer, lexer->pos) )
        {
            read_ident(lexer, token);
            token->kind = SSC_TOK_IDENT;
            return;
        }
        read_number(lexer, token);
        return;
    }

    /* A minus tight against a digit is a negative literal, not the operator —
     * content writes `queue(script, 3, -1)`. read_number consumes the sign. */
    if( c == '-' && lexer->pos + 1 < lexer->length && is_digit(lexer->source[lexer->pos + 1]) )
    {
        read_number(lexer, token);
        return;
    }

    if( c == '$' || c == '%' || c == '^' || c == '~' || c == '@' )
    {
        static const struct
        {
            char sigil;
            enum SSC_TokenKind kind;
        } k_sigils[] = {
            { '$', SSC_TOK_LOCAL }, { '%', SSC_TOK_VAR },   { '^', SSC_TOK_CONSTANT },
            { '~', SSC_TOK_PROC },  { '@', SSC_TOK_LABEL },
        };
        size_t i;
        int dotted = 0;

        lexer->pos++;
        /* `~.chatnpc` is not a dot-modified call — it names the script
         * `[proc,.chatnpc]`, one of 47 whose name genuinely starts with a dot.
         * The dot is part of the name and has to survive into the token. */
        if( lexer->pos < lexer->length && lexer->source[lexer->pos] == '.' )
        {
            dotted = 1;
            lexer->pos++;
        }
        read_ident(lexer, token);
        if( dotted )
        {
            memmove(token->text + 1, token->text, strlen(token->text) + 1);
            token->text[0] = '.';
        }
        token->kind = SSC_TOK_IDENT;
        for( i = 0; i < sizeof(k_sigils) / sizeof(k_sigils[0]); i++ )
        {
            if( k_sigils[i].sigil == c )
                token->kind = k_sigils[i].kind;
        }
        return;
    }

    if( is_ident_start(c) )
    {
        read_ident(lexer, token);
        token->kind = SSC_TOK_IDENT;
        return;
    }

    /* A leading '.' on a command name selects the secondary pointer
     * (`.npc_say`), so it has to reach the parser as a distinct token. */
    if( c == '.' && lexer->pos + 1 < lexer->length && is_ident_start(lexer->source[lexer->pos + 1]) )
    {
        lexer->pos++;
        read_ident(lexer, token);
        token->kind = SSC_TOK_IDENT;
        memmove(token->text + 1, token->text, strlen(token->text) + 1);
        token->text[0] = '.';
        return;
    }

    token->kind = SSC_TOK_PUNCT;
    if( (c == '<' || c == '>' || c == '!' || c == '=') && lexer->pos + 1 < lexer->length &&
        lexer->source[lexer->pos + 1] == '=' )
    {
        token->text[0] = c;
        token->text[1] = '=';
        token->text[2] = '\0';
        lexer->pos += 2;
        return;
    }
    token->text[0] = c;
    token->text[1] = '\0';
    lexer->pos++;
}

const struct SSC_Token*
SSC_LexNext(struct SSC_Lexer* lexer)
{
    if( lexer->has_lookahead )
    {
        lexer->current = lexer->lookahead;
        lexer->has_lookahead = 0;
    }
    else
    {
        scan(lexer, &lexer->current);
    }
    return &lexer->current;
}

const struct SSC_Token*
SSC_LexPeek(struct SSC_Lexer* lexer)
{
    if( !lexer->has_lookahead )
    {
        scan(lexer, &lexer->lookahead);
        lexer->has_lookahead = 1;
    }
    return &lexer->lookahead;
}

int
SSC_LexIsPunct(
    const struct SSC_Lexer* lexer,
    const char* punct)
{
    return lexer->current.kind == SSC_TOK_PUNCT && strcmp(lexer->current.text, punct) == 0;
}
