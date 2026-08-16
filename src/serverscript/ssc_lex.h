#ifndef SRC_SERVERSCRIPT_SSC_LEX_H
#define SRC_SERVERSCRIPT_SSC_LEX_H

/*
 * RuneScript lexer.
 *
 * The awkward parts of the language are all lexical, which is why this is worth
 * separating from the parser:
 *
 *   - five sigils that bind to the following identifier ($local, %varp,
 *     ^constant, ~proc, @label) and change what it means;
 *   - qualified names with an embedded colon (`multi2:com_1`), which look like
 *     a label until you reach the second half;
 *   - coord literals (`0_49_50_3_11`), which start like an integer;
 *   - string literals carrying <$interpolation> and <tag> markup, where the
 *     angle brackets are content, not syntax.
 */

#include <stddef.h>
#include <stdint.h>

enum SSC_TokenKind
{
    SSC_TOK_EOF = 0,
    SSC_TOK_IDENT,   /**< bare name, possibly qualified with ':' */
    SSC_TOK_INT,
    SSC_TOK_STRING,
    SSC_TOK_LOCAL,    /**< $name */
    SSC_TOK_VAR,      /**< %name */
    SSC_TOK_CONSTANT, /**< ^name */
    SSC_TOK_PROC,     /**< ~name */
    SSC_TOK_LABEL,    /**< @name */
    SSC_TOK_PUNCT,    /**< one of ( ) { } [ ] , ; : . and the operators */
};

struct SSC_Token
{
    enum SSC_TokenKind kind;
    /** Identifier / string text, or the punctuation itself. */
    char text[512];
    int32_t value; /**< SSC_TOK_INT only */
    int line;
};

struct SSC_Lexer
{
    const char* source;
    size_t length;
    size_t pos;
    int line;
    char file[256];

    struct SSC_Token current;
    struct SSC_Token lookahead;
    int has_lookahead;

    char error[256];
};

/** `source` must outlive the lexer; it is not copied. */
void
SSC_LexInit(
    struct SSC_Lexer* lexer,
    const char* source,
    size_t length,
    const char* file);

/** Advances and returns the new current token. */
const struct SSC_Token*
SSC_LexNext(struct SSC_Lexer* lexer);

/** The next token without consuming it. */
const struct SSC_Token*
SSC_LexPeek(struct SSC_Lexer* lexer);

/** True when the current token is this punctuation. */
int
SSC_LexIsPunct(
    const struct SSC_Lexer* lexer,
    const char* punct);

#endif
