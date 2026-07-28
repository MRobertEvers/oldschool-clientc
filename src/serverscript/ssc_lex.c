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

/** Read a name, allowing the `interface:component` qualified form. */
static void
read_ident(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    size_t start = lexer->pos;
    size_t length;

    while( lexer->pos < lexer->length && is_ident_char(lexer->source[lexer->pos]) )
        lexer->pos++;

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

static void
read_string(struct SSC_Lexer* lexer, struct SSC_Token* token)
{
    size_t out = 0;

    lexer->pos++; /* opening quote */
    while( lexer->pos < lexer->length && lexer->source[lexer->pos] != '"' )
    {
        char c = lexer->source[lexer->pos];

        if( c == '\n' )
            lexer->line++;

        /* Angle brackets are content, not syntax: `<$name>` marks an
         * interpolation the parser expands later, and `<br>` / `<col=...>` are
         * markup the client renders. Both pass through untouched. */
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

        lexer->pos++;
        read_ident(lexer, token);
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
