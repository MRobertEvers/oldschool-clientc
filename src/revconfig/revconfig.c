#include "revconfig.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static void
revconfig_strncpy_trimmed(
    char* dest,
    const char* src,
    size_t n)
{
    assert(dest);
    assert(src);
    assert(n > 0);

    while( *src == ' ' || *src == '\t' || *src == '\r' || *src == '\n' )
        src++;

    size_t len = strlen(src);
    while( len > 0 &&
           (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' ||
            src[len - 1] == '\n') )
        len--;

    /*
     * A quoted value keeps its inner whitespace.
     *
     * Trimming is right for almost everything a profile writes -- a stray
     * space after `w=360` must not become part of the number -- but a
     * DISPLAY string is different: the reference's login label is
     * "Username: ", space included, and a trimmed one draws the value hard
     * against the colon. Quotes are how an INI has always said "this
     * whitespace is content", and no shipped profile had a quoted value
     * before this, so nothing changes meaning by our reading them.
     */
    if( len >= 2 && src[0] == '"' && src[len - 1] == '"' )
    {
        src++;
        len -= 2;
    }

    if( len >= n )
        len = n - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}
/* ---------------------------------------------------------------------------
 * Numbers
 *
 * A profile spells ids, masks and colours the way the reference does, so the
 * value of a key is a small integer EXPRESSION rather than a decimal run:
 *
 *   hex          0x1088      1088h      0FFh
 *   binary       0b1010_1010
 *   grouping     0x1000_0000            (underscores between digits)
 *   colours      rgb(255, 0, 0)         rgba(0, 0, 0, 128)    #FF0000
 *   palette      hsl16(0, 7, 64)        -- hue, saturation, lightness
 *   uids         if(1088, 255)          -- (interface << 16) | component
 *   arithmetic   (1088 << 16) | 0xFF
 *
 * Precedence is C's, lowest binding first:
 *
 *   |    ^    &    << >>    + -    * / %    unary + - ~    primary
 *
 * A primary is a literal, a parenthesised expression, or one of the three
 * named forms above. Everything is evaluated at 64 bits and the result must
 * land in [INT32_MIN, UINT32_MAX]; the 32-bit pattern is what the caller gets,
 * so `rgba(255,255,255,255)` (0xFFFFFFFF) reaches an `int` field as -1, which
 * is the same word the client would have blitted.
 * ------------------------------------------------------------------------- */

/** Advance past spaces and tabs. */
static char const*
revconfig_skip_space(char const* p)
{
    assert(p);
    while( *p == ' ' || *p == '\t' )
        p++;
    return p;
}

struct RevConfigNumCursor
{
    char const* p;
    /** Cleared by the first thing that does not parse; nothing after it runs. */
    int ok;
    /** Why, for the one message the caller prints. NULL until something fails. */
    char const* error;
};

static void
revconfig_num_fail(struct RevConfigNumCursor* c, char const* reason)
{
    assert(c);
    assert(reason);
    if( c->ok )
    {
        c->ok = 0;
        c->error = reason;
    }
}

static void
revconfig_num_skip_space(struct RevConfigNumCursor* c)
{
    assert(c);
    c->p = revconfig_skip_space(c->p);
}

/** Value of `ch` as a hex digit, or -1. */
static int
revconfig_num_hex_digit(char ch)
{
    if( ch >= '0' && ch <= '9' )
        return ch - '0';
    if( ch >= 'a' && ch <= 'f' )
        return ch - 'a' + 10;
    if( ch >= 'A' && ch <= 'F' )
        return ch - 'A' + 10;
    return -1;
}

static int
revconfig_num_ident_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

static int
revconfig_num_ident_start(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static int64_t
revconfig_num_expr(struct RevConfigNumCursor* c);

/**
 * One literal.
 *
 * The `h` suffix has to be found before the first digit is read -- it is what
 * says the digits are hex -- so the run is scanned twice: once for the suffix,
 * once for the value.
 */
static int64_t
revconfig_num_literal(struct RevConfigNumCursor* c)
{
    char const* p;
    char const* scan;
    int base = 10;
    int digits = 0;
    int suffixed = 0;
    uint64_t value = 0;

    assert(c);

    p = c->p;
    scan = p;
    while( revconfig_num_hex_digit(*scan) >= 0 || *scan == '_' )
        scan++;
    if( scan != p && (*scan == 'h' || *scan == 'H') && !revconfig_num_ident_char(scan[1]) )
    {
        base = 16;
        suffixed = 1;
    }
    else if( p[0] == '0' && (p[1] == 'x' || p[1] == 'X') )
    {
        base = 16;
        p += 2;
    }
    /* `#RRGGBB`, the spelling a wiki page, a stylesheet and this tree's own
     * plugin_prefs.ini all carry. It marks the digits as hex and says nothing
     * about how many there are, so `#FFF` and `#FF0000FF` are both numbers --
     * a colour's WIDTH is the field's business, not the literal's. */
    else if( p[0] == '#' )
    {
        base = 16;
        p += 1;
    }
    else if( p[0] == '0' && (p[1] == 'b' || p[1] == 'B') )
    {
        base = 2;
        p += 2;
    }

    for( ; *p; p++ )
    {
        int digit;

        if( *p == '_' )
            continue;
        digit = revconfig_num_hex_digit(*p);
        if( digit < 0 || digit >= base )
            break;
        if( value > (UINT64_C(0x7FFFFFFFFFFFFFFF) - (uint64_t)digit) / (uint64_t)base )
        {
            revconfig_num_fail(c, "number is too large");
            return 0;
        }
        value = value * (uint64_t)base + (uint64_t)digit;
        digits++;
    }
    if( suffixed && (*p == 'h' || *p == 'H') )
        p++;

    if( digits == 0 )
    {
        revconfig_num_fail(c, "expected a number");
        return 0;
    }
    /* `12abc` and `0b12` are typos, not a number followed by something. */
    if( revconfig_num_ident_char(*p) )
    {
        revconfig_num_fail(c, "unexpected character after a number");
        return 0;
    }

    c->p = p;
    return (int64_t)value;
}

/** Comma-separated arguments up to the closing ')', which is consumed. */
static int
revconfig_num_args(
    struct RevConfigNumCursor* c,
    int64_t* out_args,
    int max_args)
{
    int count = 0;

    assert(c);
    assert(out_args);
    assert(max_args > 0);

    revconfig_num_skip_space(c);
    if( *c->p == ')' )
    {
        c->p++;
        return 0;
    }
    for( ;; )
    {
        int64_t arg = revconfig_num_expr(c);
        if( !c->ok )
            return count;
        if( count >= max_args )
        {
            revconfig_num_fail(c, "too many arguments");
            return count;
        }
        out_args[count++] = arg;

        revconfig_num_skip_space(c);
        if( *c->p == ',' )
        {
            c->p++;
            continue;
        }
        if( *c->p == ')' )
        {
            c->p++;
            return count;
        }
        revconfig_num_fail(c, "expected ',' or ')'");
        return count;
    }
}

/** 0..255, the range every rgb()/rgba() channel has to be in. */
static int
revconfig_num_channel(struct RevConfigNumCursor* c, int64_t v)
{
    assert(c);
    if( v < 0 || v > 255 )
    {
        revconfig_num_fail(c, "rgb()/rgba() channels are 0..255");
        return 0;
    }
    return (int)v;
}

/**
 * One hsl16() axis, each with its own width.
 *
 * Separate from the rgb() channel check because the widths differ and the
 * message has to name the axis: "0..7" on a saturation somebody wrote as 255
 * is the whole answer.
 */
static int
revconfig_num_hsl_axis(
    struct RevConfigNumCursor* c,
    int64_t v,
    int64_t limit,
    char const* reason)
{
    assert(c);
    assert(reason);
    if( v < 0 || v > limit )
    {
        revconfig_num_fail(c, reason);
        return 0;
    }
    return (int)v;
}

/** 0..65535, or -1 for the "no component" half of a uid. */
static int
revconfig_num_uid_half(struct RevConfigNumCursor* c, int64_t v)
{
    assert(c);
    if( v == -1 )
        return 0xFFFF;
    if( v < 0 || v > 0xFFFF )
    {
        revconfig_num_fail(c, "if() halves are 0..65535 (or -1)");
        return 0;
    }
    return (int)v;
}

/** rgb(), rgba(), hsl16(), if() -- the spellings worth having a name for. */
static int64_t
revconfig_num_call(struct RevConfigNumCursor* c, char const* name)
{
    int64_t args[4];
    int count;

    assert(c);
    assert(name);

    count = revconfig_num_args(c, args, (int)(sizeof(args) / sizeof(args[0])));
    if( !c->ok )
        return 0;

    if( strcasecmp(name, "rgb") == 0 && count == 3 )
        return ((int64_t)revconfig_num_channel(c, args[0]) << 16) |
               ((int64_t)revconfig_num_channel(c, args[1]) << 8) |
               (int64_t)revconfig_num_channel(c, args[2]);
    /* ARGB, the word the client blits. */
    if( strcasecmp(name, "rgba") == 0 && count == 4 )
        return ((int64_t)revconfig_num_channel(c, args[3]) << 24) |
               ((int64_t)revconfig_num_channel(c, args[0]) << 16) |
               ((int64_t)revconfig_num_channel(c, args[1]) << 8) |
               (int64_t)revconfig_num_channel(c, args[2]);
    /* The client's own colour unit: a packed palette index, hue 0..63,
     * saturation 0..7, lightness 0..127. What a model recolour, a face colour
     * and a text tint are addressed in, and a number no rgb() can spell --
     * the palette is not a cube, so there is no exact RGB for most entries. */
    if( strcasecmp(name, "hsl16") == 0 && count == 3 )
        return ((int64_t)revconfig_num_hsl_axis(c, args[0], 63, "hsl16() hue is 0..63") << 10) |
               ((int64_t)revconfig_num_hsl_axis(c, args[1], 7, "hsl16() saturation is 0..7")
                << 7) |
               (int64_t)revconfig_num_hsl_axis(c, args[2], 127, "hsl16() lightness is 0..127");
    if( strcasecmp(name, "if") == 0 && count == 2 )
        return ((int64_t)revconfig_num_uid_half(c, args[0]) << 16) |
               (int64_t)revconfig_num_uid_half(c, args[1]);

    revconfig_num_fail(c, "unknown function, or wrong argument count");
    return 0;
}

static int64_t
revconfig_num_primary(struct RevConfigNumCursor* c)
{
    assert(c);

    revconfig_num_skip_space(c);
    if( *c->p == '(' )
    {
        int64_t v;

        c->p++;
        v = revconfig_num_expr(c);
        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        if( *c->p != ')' )
        {
            revconfig_num_fail(c, "expected ')'");
            return 0;
        }
        c->p++;
        return v;
    }
    if( revconfig_num_ident_start(*c->p) )
    {
        char name[16];
        size_t len = 0;

        while( revconfig_num_ident_char(c->p[len]) )
            len++;
        if( len >= sizeof(name) )
        {
            revconfig_num_fail(c, "unknown function");
            return 0;
        }
        memcpy(name, c->p, len);
        name[len] = '\0';
        c->p += len;

        revconfig_num_skip_space(c);
        if( *c->p != '(' )
        {
            revconfig_num_fail(c, "not a number");
            return 0;
        }
        c->p++;
        return revconfig_num_call(c, name);
    }
    return revconfig_num_literal(c);
}

static int64_t
revconfig_num_unary(struct RevConfigNumCursor* c)
{
    assert(c);

    revconfig_num_skip_space(c);
    if( *c->p == '-' )
    {
        c->p++;
        return -revconfig_num_unary(c);
    }
    if( *c->p == '+' )
    {
        c->p++;
        return revconfig_num_unary(c);
    }
    if( *c->p == '~' )
    {
        c->p++;
        return ~revconfig_num_unary(c);
    }
    return revconfig_num_primary(c);
}

/** Multiplication overflows 64 bits only for absurd input; refuse it there. */
static int64_t
revconfig_num_mul(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_unary(c);
    for( ;; )
    {
        char op;
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        op = *c->p;
        if( op != '*' && op != '/' && op != '%' )
            return lhs;
        c->p++;
        rhs = revconfig_num_unary(c);
        if( !c->ok )
            return 0;

        if( op == '*' )
        {
            if( lhs > INT64_C(0xFFFFFFFFFF) || lhs < -INT64_C(0xFFFFFFFFFF) ||
                rhs > INT64_C(0xFFFFFFFFFF) || rhs < -INT64_C(0xFFFFFFFFFF) )
            {
                revconfig_num_fail(c, "number is too large");
                return 0;
            }
            lhs = lhs * rhs;
        }
        else if( rhs == 0 )
        {
            revconfig_num_fail(c, "division by zero");
            return 0;
        }
        else if( op == '/' )
            lhs = lhs / rhs;
        else
            lhs = lhs % rhs;
    }
}

static int64_t
revconfig_num_add(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_mul(c);
    for( ;; )
    {
        char op;
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        op = *c->p;
        if( op != '+' && op != '-' )
            return lhs;
        c->p++;
        rhs = revconfig_num_mul(c);
        if( !c->ok )
            return 0;
        lhs = (op == '+') ? lhs + rhs : lhs - rhs;
    }
}

static int64_t
revconfig_num_shift(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_add(c);
    for( ;; )
    {
        char op;
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        op = *c->p;
        if( (op != '<' && op != '>') || c->p[1] != op )
            return lhs;
        c->p += 2;
        rhs = revconfig_num_add(c);
        if( !c->ok )
            return 0;
        if( rhs < 0 || rhs > 63 )
        {
            revconfig_num_fail(c, "shift count is 0..63");
            return 0;
        }
        /* Shifting is a bit move, not an arithmetic one: the value is carried
         * through unsigned so a set top bit is neither undefined nor smeared. */
        if( op == '<' )
            lhs = (int64_t)((uint64_t)lhs << (unsigned)rhs);
        else
            lhs = (int64_t)((uint64_t)lhs >> (unsigned)rhs);
    }
}

static int64_t
revconfig_num_and(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_shift(c);
    for( ;; )
    {
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        /* `&&` is not a spelling this grammar has; leave it to the caller's
         * trailing-text check to complain about. */
        if( *c->p != '&' || c->p[1] == '&' )
            return lhs;
        c->p++;
        rhs = revconfig_num_shift(c);
        if( !c->ok )
            return 0;
        lhs = lhs & rhs;
    }
}

static int64_t
revconfig_num_xor(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_and(c);
    for( ;; )
    {
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        if( *c->p != '^' )
            return lhs;
        c->p++;
        rhs = revconfig_num_and(c);
        if( !c->ok )
            return 0;
        lhs = lhs ^ rhs;
    }
}

static int64_t
revconfig_num_expr(struct RevConfigNumCursor* c)
{
    int64_t lhs;

    assert(c);

    lhs = revconfig_num_xor(c);
    for( ;; )
    {
        int64_t rhs;

        if( !c->ok )
            return 0;
        revconfig_num_skip_space(c);
        if( *c->p != '|' || c->p[1] == '|' )
            return lhs;
        c->p++;
        rhs = revconfig_num_xor(c);
        if( !c->ok )
            return 0;
        lhs = lhs | rhs;
    }
}

static int
revconfig_num_parse(
    char const* str,
    char const** out_end,
    int* out_value,
    char const** out_error)
{
    struct RevConfigNumCursor c;
    int64_t v;

    assert(str);
    assert(out_value);

    c.p = str;
    c.ok = 1;
    c.error = NULL;

    v = revconfig_num_expr(&c);
    if( !c.ok )
    {
        if( out_error )
            *out_error = c.error ? c.error : "not a number";
        return 0;
    }
    /* One 32-bit word, signed or unsigned as the author spelled it. */
    if( v > INT64_C(0xFFFFFFFF) || v < INT32_MIN )
    {
        if( out_error )
            *out_error = "number does not fit in 32 bits";
        return 0;
    }
    if( out_end )
        *out_end = c.p;
    *out_value = (int)(int32_t)(uint32_t)(uint64_t)v;
    return 1;
}

int
revconfig_parse_int_expr(
    char const* str,
    char const** out_end,
    int* out_value)
{
    assert(str);
    assert(out_value);
    return revconfig_num_parse(str, out_end, out_value, NULL);
}

int
revconfig_parse_int(char const* str)
{
    char const* end = NULL;
    char const* error = NULL;
    int value = 0;

    assert(str);

    /* An unstated key is not a malformed one. */
    if( *revconfig_skip_space(str) == '\0' )
        return 0;

    if( !revconfig_num_parse(str, &end, &value, &error) )
    {
        TORIRS_ERR("revconfig: '%s' is not a number: %s\n", str, error);
        return 0;
    }
    if( *revconfig_skip_space(end) != '\0' )
    {
        TORIRS_LOG("revconfig: '%s' is not a number: trailing text '%s'\n", str, end);
        return 0;
    }
    return value;
}


char const*
revconfig_field_kind_str(enum RevConfigFieldKind kind)
{
    switch( kind )
    {
    case RCFIELD_NONE:
        return "RCFIELD_NONE";
    case RCFIELD_ITEMTYPE:
        return "RCFIELD_ITEMTYPE";
    case RCFIELD_ITEMNAME:
        return "RCFIELD_ITEMNAME";
    case RCFIELD_ITEMDONE:
        return "RCFIELD_ITEMDONE";
    case RCFIELD_CACHE_TABLE:
        return "RCFIELD_CACHE_TABLE";
    case RCFIELD_CACHE_ARCHIVE:
        return "RCFIELD_CACHE_ARCHIVE";
    case RCFIELD_CACHE_ARCHIVE_ID:
        return "RCFIELD_CACHE_ARCHIVE_ID";
    case RCFIELD_CACHE_DEFAULTS_SLOT:
        return "RCFIELD_CACHE_DEFAULTS_SLOT";
    case RCFIELD_CACHE_GROUP:
        return "RCFIELD_CACHE_GROUP";
    case RCFIELD_CACHE_CONTAINER:
        return "RCFIELD_CACHE_CONTAINER";
    case RCFIELD_CACHE_INDEX_FILENAME:
        return "RCFIELD_CACHE_INDEX_FILENAME";
    case RCFIELD_CACHE_DATA_FILENAME:
        return "RCFIELD_CACHE_DATA_FILENAME";
    case RCFIELD_CACHE_FORMAT:
        return "RCFIELD_CACHE_FORMAT";
    case RCFIELD_CACHE_ATLAS_INDEX:
        return "RCFIELD_CACHE_ATLAS_INDEX";
    case RCFIELD_CACHE_ATLAS_COUNT:
        return "RCFIELD_CACHE_ATLAS_COUNT";
    case RCFIELD_CACHE_TRANSFORM:
        return "RCFIELD_CACHE_TRANSFORM";
    case RCFIELD_CACHE_CROP_X:
        return "RCFIELD_CACHE_CROP_X";
    case RCFIELD_CACHE_CROP_Y:
        return "RCFIELD_CACHE_CROP_Y";
    case RCFIELD_CACHE_CROP_WIDTH:
        return "RCFIELD_CACHE_CROP_WIDTH";
    case RCFIELD_CACHE_CROP_HEIGHT:
        return "RCFIELD_CACHE_CROP_HEIGHT";
    case RCFIELD_CACHE_FONT_NAME:
        return "RCFIELD_CACHE_FONT_NAME";
    case RCFIELD_CACHE_FONT_ID:
        return "RCFIELD_CACHE_FONT_ID";
    case RCFIELD_CACHEREF_ID:
        return "RCFIELD_CACHEREF_ID";
    case RCFIELD_UICOMPONENT_TYPE:
        return "RCFIELD_UICOMPONENT_TYPE";
    case RCFIELD_UICOMPONENT_SPRITE:
        return "RCFIELD_UICOMPONENT_SPRITE";
    case RCFIELD_UICOMPONENT_WIDTH:
        return "RCFIELD_UICOMPONENT_WIDTH";
    case RCFIELD_UICOMPONENT_HEIGHT:
        return "RCFIELD_UICOMPONENT_HEIGHT";
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        return "RCFIELD_UICOMPONENT_ANCHOR_X";
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        return "RCFIELD_UICOMPONENT_ANCHOR_Y";
    case RCFIELD_UICOMPONENT_TABNO:
        return "RCFIELD_UICOMPONENT_TABNO";
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        return "RCFIELD_UICOMPONENT_SPRITE_ACTIVE";
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        return "RCFIELD_UICOMPONENT_COMPONENTNO";
    case RCFIELD_UICOMPONENT_INV:
        return "RCFIELD_UICOMPONENT_INV";
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        return "RCFIELD_UICOMPONENT_PAINT_LEVELS";
    case RCFIELD_UICOMPONENT_HOTKEY:
        return "RCFIELD_UICOMPONENT_HOTKEY";
    case RCFIELD_HOTKEY_COMPONENT:
        return "RCFIELD_HOTKEY_COMPONENT";
    case RCFIELD_HOTKEY_EFFECT:
        return "RCFIELD_HOTKEY_EFFECT";
    case RCFIELD_UICOMPONENT_COLOR:
        return "RCFIELD_UICOMPONENT_COLOR";
    case RCFIELD_UICOMPONENT_FILLED:
        return "RCFIELD_UICOMPONENT_FILLED";
    case RCFIELD_UICOMPONENT_TILED:
        return "RCFIELD_UICOMPONENT_TILED";
    case RCFIELD_UICOMPONENT_FONT:
        return "RCFIELD_UICOMPONENT_FONT";
    case RCFIELD_UICOMPONENT_CENTER:
        return "RCFIELD_UICOMPONENT_CENTER";
    case RCFIELD_UICOMPONENT_VALIGN:
        return "RCFIELD_UICOMPONENT_VALIGN";
    case RCFIELD_UICOMPONENT_OVER_COLOR:
        return "RCFIELD_UICOMPONENT_OVER_COLOR";
    case RCFIELD_UICOMPONENT_SHADOWED:
        return "RCFIELD_UICOMPONENT_SHADOWED";
    case RCFIELD_UICOMPONENT_TEXT:
        return "RCFIELD_UICOMPONENT_TEXT";
    case RCFIELD_UICOMPONENT_TITLE_FIELD:
        return "RCFIELD_UICOMPONENT_TITLE_FIELD";
    case RCFIELD_UICOMPONENT_TITLE_PREFIX:
        return "RCFIELD_UICOMPONENT_TITLE_PREFIX";
    case RCFIELD_UICOMPONENT_TITLE_CARET:
        return "RCFIELD_UICOMPONENT_TITLE_CARET";
    case RCFIELD_UICOMPONENT_TITLE_CARET_BLINK:
        return "RCFIELD_UICOMPONENT_TITLE_CARET_BLINK";
    case RCFIELD_UICOMPONENT_TITLE_MASK:
        return "RCFIELD_UICOMPONENT_TITLE_MASK";
    case RCFIELD_UICOMPONENT_TITLE_MAXLEN:
        return "RCFIELD_UICOMPONENT_TITLE_MAXLEN";
    case RCFIELD_UICOMPONENT_TITLE_CHARSET:
        return "RCFIELD_UICOMPONENT_TITLE_CHARSET";
    case RCFIELD_UICOMPONENT_TITLE_ACTION:
        return "RCFIELD_UICOMPONENT_TITLE_ACTION";
    case RCFIELD_UICOMPONENT_TITLE_MESSAGE_INDEX:
        return "RCFIELD_UICOMPONENT_TITLE_MESSAGE_INDEX";
    case RCFIELD_UICOMPONENT_TITLE_PX_PER_PERCENT:
        return "RCFIELD_UICOMPONENT_TITLE_PX_PER_PERCENT";
    case RCFIELD_UICOMPONENT_FLAME_BIAS:
        return "RCFIELD_UICOMPONENT_FLAME_BIAS";
    case RCFIELD_UICOMPONENT_FLAME_SWAY:
        return "RCFIELD_UICOMPONENT_FLAME_SWAY";
    case RCFIELD_UICOMPONENT_FLAME_RUN:
        return "RCFIELD_UICOMPONENT_FLAME_RUN";
    case RCFIELD_UICOMPONENT_FLAME_ROW:
        return "RCFIELD_UICOMPONENT_FLAME_ROW";
    case RCFIELD_UICOMPONENT_FLAME_BLUR:
        return "RCFIELD_UICOMPONENT_FLAME_BLUR";
    case RCFIELD_UICOMPONENT_TEXT_BASELINE:
        return "RCFIELD_UICOMPONENT_TEXT_BASELINE";
    case RCFIELD_UICOMPONENT_OPTION:
        return "RCFIELD_UICOMPONENT_OPTION";
    case RCFIELD_UICOMPONENT_OPTION_ACTION:
        return "RCFIELD_UICOMPONENT_OPTION_ACTION";
    case RCFIELD_UICOMPONENT_OP0:
        return "RCFIELD_UICOMPONENT_OP0";
    case RCFIELD_UICOMPONENT_OP1:
        return "RCFIELD_UICOMPONENT_OP1";
    case RCFIELD_UICOMPONENT_OP2:
        return "RCFIELD_UICOMPONENT_OP2";
    case RCFIELD_UICOMPONENT_OP3:
        return "RCFIELD_UICOMPONENT_OP3";
    case RCFIELD_UICOMPONENT_OP4:
        return "RCFIELD_UICOMPONENT_OP4";
    case RCFIELD_UICOMPONENT_OP0_ACTION:
        return "RCFIELD_UICOMPONENT_OP0_ACTION";
    case RCFIELD_UICOMPONENT_OP1_ACTION:
        return "RCFIELD_UICOMPONENT_OP1_ACTION";
    case RCFIELD_UICOMPONENT_OP2_ACTION:
        return "RCFIELD_UICOMPONENT_OP2_ACTION";
    case RCFIELD_UICOMPONENT_OP3_ACTION:
        return "RCFIELD_UICOMPONENT_OP3_ACTION";
    case RCFIELD_UICOMPONENT_OP4_ACTION:
        return "RCFIELD_UICOMPONENT_OP4_ACTION";
    case RCFIELD_UICOMPONENT_BUTTON_TYPE:
        return "RCFIELD_UICOMPONENT_BUTTON_TYPE";
    case RCFIELD_UICOMPONENT_CLIENT_CODE:
        return "RCFIELD_UICOMPONENT_CLIENT_CODE";
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE";
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR";
    case RCFIELD_INV_ITEM:
        return "RCFIELD_INV_ITEM";
    case RCFIELD_UILAYOUT_COMPONENT:
        return "RCFIELD_UILAYOUT_COMPONENT";
    case RCFIELD_UILAYOUT_X:
        return "RCFIELD_UILAYOUT_X";
    case RCFIELD_UILAYOUT_Y:
        return "RCFIELD_UILAYOUT_Y";
    case RCFIELD_UILAYOUT_WIDTH:
        return "RCFIELD_UILAYOUT_WIDTH";
    case RCFIELD_UILAYOUT_HEIGHT:
        return "RCFIELD_UILAYOUT_HEIGHT";
    case RCFIELD_UILAYOUT_ANCHOR_X:
        return "RCFIELD_UILAYOUT_ANCHOR_X";
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        return "RCFIELD_UILAYOUT_ANCHOR_Y";
    case RCFIELD_UILAYOUT_TOP:
        return "RCFIELD_UILAYOUT_TOP";
    case RCFIELD_UILAYOUT_LEFT:
        return "RCFIELD_UILAYOUT_LEFT";
    case RCFIELD_UILAYOUT_BOTTOM:
        return "RCFIELD_UILAYOUT_BOTTOM";
    case RCFIELD_UILAYOUT_RIGHT:
        return "RCFIELD_UILAYOUT_RIGHT";
    case RCFIELD_UILAYOUT_DIRTY:
        return "RCFIELD_UILAYOUT_DIRTY";
    case RCFIELD_UILAYOUT_XALIGN:
        return "RCFIELD_UILAYOUT_XALIGN";
    case RCFIELD_UILAYOUT_PARENT:
        return "RCFIELD_UILAYOUT_PARENT";
    case RCFIELD_UILAYOUT_NAME:
        return "RCFIELD_UILAYOUT_NAME";
    case RCFIELD_FEATURES_ERA:
        return "RCFIELD_FEATURES_ERA";
    case RCFIELD_FEATURES_GROUND_CLICK_NEAREST:
        return "RCFIELD_FEATURES_GROUND_CLICK_NEAREST";
    case RCFIELD_FEATURES_GROUND_CLICK_UNBOUNDED:
        return "RCFIELD_FEATURES_GROUND_CLICK_UNBOUNDED";
    case RCFIELD_FEATURES_GROUND_CLICK_OFFMAP:
        return "RCFIELD_FEATURES_GROUND_CLICK_OFFMAP";
    case RCFIELD_FEATURES_MOVER:
        return "RCFIELD_FEATURES_MOVER";
    case RCFIELD_FEATURES_PAINTER_DRAW_DISTANCE:
        return "RCFIELD_FEATURES_PAINTER_DRAW_DISTANCE";
    case RCFIELD_CAMERA_ZOOM:
        return "RCFIELD_CAMERA_ZOOM";
    case RCFIELD_CAMERA_CONTROLS:
        return "RCFIELD_CAMERA_CONTROLS";
    case RCFIELD_CAMERA_WHEEL_STEP:
        return "RCFIELD_CAMERA_WHEEL_STEP";
    case RCFIELD_CHROME_PLUGIN_IFACE:
        return "RCFIELD_CHROME_PLUGIN_IFACE";
    case RCFIELD_CHROME_PLUGIN_BUTTON_PARENT:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_PARENT";
    case RCFIELD_CHROME_PLUGIN_BUTTON_X:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_X";
    case RCFIELD_CHROME_PLUGIN_BUTTON_Y:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_Y";
    case RCFIELD_CHROME_PLUGIN_BUTTON_W:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_W";
    case RCFIELD_CHROME_PLUGIN_BUTTON_H:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_H";
    case RCFIELD_CHROME_PLUGIN_BUTTON_OP:
        return "RCFIELD_CHROME_PLUGIN_BUTTON_OP";
    case RCFIELD_ROLE_MATCH:
        return "RCFIELD_ROLE_MATCH";
    case RCFIELD_UICOMPONENT_ROLE:
        return "RCFIELD_UICOMPONENT_ROLE";
    default:
        return "UNKNOWN";
    }
}

struct RevConfigBuffer*
revconfig_buffer_new(uint32_t hint)
{
    struct RevConfigBuffer* buffer = malloc(sizeof(struct RevConfigBuffer));
    assert(buffer);
    memset(buffer, 0, sizeof(struct RevConfigBuffer));

    if( hint > 0 )
    {
        buffer->fields = malloc(sizeof(struct RevConfigField) * hint);
        assert(buffer->fields);
        buffer->field_capacity = hint;
    }

    return buffer;
}

void
revconfig_buffer_free(struct RevConfigBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->fields);
    free(buffer);
}

int
revconfig_buffer_push_field(
    struct RevConfigBuffer* buffer,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(buffer);

    if( buffer->field_count >= buffer->field_capacity )
    {
        uint32_t new_capacity = buffer->field_capacity == 0 ? 16 : buffer->field_capacity * 2;
        struct RevConfigField* new_fields =
            realloc(buffer->fields, sizeof(struct RevConfigField) * new_capacity);
        if( !new_fields )
            return -1;
        buffer->fields = new_fields;
        buffer->field_capacity = new_capacity;
    }

    struct RevConfigField* field = &buffer->fields[buffer->field_count++];
    field->kind = kind;
    assert(value);
    revconfig_strncpy_trimmed(field->value, value, sizeof(field->value));
    return 0;
}

struct RevConfigItemBuffer*
revconfig_item_buffer_new(uint32_t hint)
{
    struct RevConfigItemBuffer* buffer = malloc(sizeof(struct RevConfigItemBuffer));
    assert(buffer);
    memset(buffer, 0, sizeof(struct RevConfigItemBuffer));

    if( hint > 0 )
    {
        buffer->items = malloc(sizeof(struct RevConfigItem) * hint);
        assert(buffer->items);
        buffer->item_capacity = hint;
    }

    return buffer;
}

void
revconfig_item_buffer_free(struct RevConfigItemBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->items);
    free(buffer);
}

struct RevConfigItem*
revconfig_item_buffer_push(struct RevConfigItemBuffer* buffer)
{
    assert(buffer);

    if( buffer->item_count >= buffer->item_capacity )
    {
        uint32_t new_capacity = buffer->item_capacity == 0 ? 16 : buffer->item_capacity * 2;
        struct RevConfigItem* new_items =
            realloc(buffer->items, sizeof(struct RevConfigItem) * new_capacity);
        if( !new_items )
            return NULL;
        buffer->items = new_items;
        buffer->item_capacity = new_capacity;
    }

    struct RevConfigItem* item = &buffer->items[buffer->item_count++];
    memset(item, 0, sizeof(*item));
    return item;
}

static void
revconfig_item_set_name(
    struct RevConfigItem* item,
    const char* value)
{
    assert(item);
    assert(value);

    switch( item->kind )
    {
    case RCITEM_CACHE_SPRITE:
        strncpy(item->u.cache.name, value, sizeof(item->u.cache.name) - 1);
        break;
    case RCITEM_CACHE_FONT:
        strncpy(item->u.font.name, value, sizeof(item->u.font.name) - 1);
        break;
    case RCITEM_UICOMPONENT:
        strncpy(item->u.uicomponent.name, value, sizeof(item->u.uicomponent.name) - 1);
        break;
    case RCITEM_UILAYOUT:
        strncpy(item->u.uilayout.name, value, sizeof(item->u.uilayout.name) - 1);
        break;
    case RCITEM_INV:
        strncpy(item->u.inv.name, value, sizeof(item->u.inv.name) - 1);
        break;
    case RCITEM_HOTKEY:
        strncpy(item->u.hotkey.name, value, sizeof(item->u.hotkey.name) - 1);
        break;
    case RCITEM_CACHE_REF:
        strncpy(item->u.cacheref.name, value, sizeof(item->u.cacheref.name) - 1);
        break;
    case RCITEM_ROLE:
        strncpy(item->u.role.name, value, sizeof(item->u.role.name) - 1);
        break;
    case RCITEM_STRING:
        strncpy(item->u.string.name, value, sizeof(item->u.string.name) - 1);
        break;
    case RCITEM_PRELOAD:
        strncpy(item->u.preload.name, value, sizeof(item->u.preload.name) - 1);
        break;
    case RCITEM_LOGIN_REPLY:
        /* The section name IS the code, except for the two names that stand
         * for cases the protocol has no byte for. */
        if( strcmp(value, REVCONFIG_LOGIN_REPLY_DEFAULT_NAME) == 0 )
            item->u.login_reply.code = REVCONFIG_LOGIN_REPLY_CODE_DEFAULT;
        else if( strcmp(value, REVCONFIG_LOGIN_REPLY_CONNECT_FAILED_NAME) == 0 )
            item->u.login_reply.code = REVCONFIG_LOGIN_REPLY_CODE_CONNECT_FAILED;
        else
            item->u.login_reply.code = revconfig_parse_int(value);
        break;
    default:
        break;
    }
}

/** True when `type_value` names one of REVCONFIG_CACHEREF_KINDS. */
static int
revconfig_type_is_cacheref(const char* type_value)
{
    static char const* const kinds[] = { REVCONFIG_CACHEREF_KINDS };
    assert(type_value);
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
    {
        if( strcmp(type_value, kinds[i]) == 0 )
            return 1;
    }
    return 0;
}

static void
revconfig_item_begin(
    struct RevConfigItem* item,
    const char* type_value)
{
    memset(item, 0, sizeof(*item));

    if( strcmp(type_value, "sprite") == 0 )
    {
        item->kind = RCITEM_CACHE_SPRITE;
        item->u.cache.archive_id = -1;
        item->u.cache.defaults_slot = -1;
    }
    else if( strcmp(type_value, "font") == 0 )
    {
        item->kind = RCITEM_CACHE_FONT;
        item->u.font.archive_id = -1;
        item->u.font.cache_font_id = -1;
    }
    else if( strcmp(type_value, "component") == 0 )
    {
        item->kind = RCITEM_UICOMPONENT;
        item->u.uicomponent.componentno = -1;
    }
    else if( strcmp(type_value, "layout") == 0 )
        item->kind = RCITEM_UILAYOUT;
    else if( strcmp(type_value, "role") == 0 )
        item->kind = RCITEM_ROLE;
    else if( strcmp(type_value, "string") == 0 )
        item->kind = RCITEM_STRING;
    else if( strcmp(type_value, "preload") == 0 )
    {
        item->kind = RCITEM_PRELOAD;
        /* Unstated is not zero for these two: id 0 is a real cache index,
         * and a step with no percent must leave the bar where it was. */
        item->u.preload.id = -1;
        item->u.preload.percent = -1;
    }
    else if( strcmp(type_value, "login_reply") == 0 )
    {
        item->kind = RCITEM_LOGIN_REPLY;
        /* -1 = "leave the screen alone", which is what most codes want: the
         * generic error page is already up by the time the lines are read. */
        item->u.login_reply.screen = -1;
    }
    else if( strcmp(type_value, "inv") == 0 )
        item->kind = RCITEM_INV;
    else if( strcmp(type_value, "hotkey") == 0 )
        item->kind = RCITEM_HOTKEY;
    else if( strcmp(type_value, "features") == 0 )
    {
        item->kind = RCITEM_FEATURES;
        /* Unstated has to be distinguishable from every legal value: 0 is a
         * real answer for both permissive extensions, and 25 (not 0) is the
         * painter's smallest real radius. */
        item->u.features.ground_click_unbounded = -1;
        item->u.features.ground_click_offmap = -1;
    }
    else if( strcmp(type_value, "camera") == 0 )
    {
        item->kind = RCITEM_CAMERA;
        /* Neither key is defaulted here. An item carries only what its section
         * SAID -- has_zoom / has_controls -- so that a later source overriding
         * `controls=` cannot silently reset `zoom=` back to a default the
         * earlier source had deliberately moved. RevConfigProfile owns the
         * defaults, once, for the whole boot. */
    }
    else if( strcmp(type_value, "chrome") == 0 )
    {
        item->kind = RCITEM_CHROME;
        /* -1 for every number, the same reason the cache-ref id is: 0 is a real
         * child component, a real column slot and a real pixel size, so a key
         * this section did not state has to read as something no INI can spell.
         * The merge below keys off exactly that. */
        item->u.chrome.plugin_button_parent = -1;
        item->u.chrome.plugin_button_x = -1;
        item->u.chrome.plugin_button_y = -1;
        item->u.chrome.plugin_button_w = -1;
        item->u.chrome.plugin_button_h = -1;
    }
    else if( revconfig_type_is_cacheref(type_value) )
    {
        item->kind = RCITEM_CACHE_REF;
        /* -1, not 0: 0 is a real script/iface/seq/varbit id, so a section that
         * forgot its id= would otherwise read as a binding to whatever thing
         * happens to be numbered zero. */
        item->u.cacheref.id = -1;
        strncpy(item->u.cacheref.kind, type_value, sizeof(item->u.cacheref.kind) - 1);
    }
    else
        item->kind = RCITEM_NONE;
}

static void
revconfig_item_apply_cache_field(
    struct RevConfigCacheItem* cache,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(cache->table, value, sizeof(cache->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(cache->archive, value, sizeof(cache->archive) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE_ID:
        cache->archive_id = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_DEFAULTS_SLOT:
        cache->defaults_slot = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_GROUP:
        strncpy(cache->group, value, sizeof(cache->group) - 1);
        break;
    case RCFIELD_CACHE_CONTAINER:
        strncpy(cache->container, value, sizeof(cache->container) - 1);
        break;
    case RCFIELD_CACHE_INDEX_FILENAME:
        strncpy(cache->index_filename, value, sizeof(cache->index_filename) - 1);
        break;
    case RCFIELD_CACHE_DATA_FILENAME:
        strncpy(cache->data_filename, value, sizeof(cache->data_filename) - 1);
        break;
    case RCFIELD_CACHE_FORMAT:
        strncpy(cache->format, value, sizeof(cache->format) - 1);
        break;
    case RCFIELD_CACHE_ATLAS_INDEX:
        cache->atlas_index = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_ATLAS_COUNT:
        cache->atlas_count = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_TRANSFORM:
        if( cache->transform_count < 4 )
        {
            strncpy(
                cache->transform[cache->transform_count],
                value,
                sizeof(cache->transform[cache->transform_count]) - 1);
            cache->transform_count++;
        }
        break;
    case RCFIELD_CACHE_CROP_X:
        cache->crop_x = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_CROP_Y:
        cache->crop_y = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_CROP_WIDTH:
        cache->crop_width = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_CROP_HEIGHT:
        cache->crop_height = revconfig_parse_int(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_font_field(
    struct RevConfigFontItem* font,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(font->table, value, sizeof(font->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(font->archive, value, sizeof(font->archive) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE_ID:
        font->archive_id = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_FONT_NAME:
        strncpy(font->font_name, value, sizeof(font->font_name) - 1);
        break;
    case RCFIELD_CACHE_FONT_ID:
        font->cache_font_id = revconfig_parse_int(value);
        break;
    case RCFIELD_CACHE_GROUP:
        strncpy(font->group, value, sizeof(font->group) - 1);
        break;
    default:
        break;
    }
}

/**
 * Does `font=` name a font id, or a font in the profile?
 *
 * "Parses as a number" is the whole test, so a hex or expression id is a font
 * id like any other, and the named forms this has to leave alone -- `b12`,
 * `chrome:bold` -- are exactly the ones that do not parse.
 */
static int
revconfig_font_field_is_numeric(const char* value)
{
    char const* end = NULL;
    int parsed = 0;

    assert(value);
    if( value[0] == '\0' )
        return 0;
    if( !revconfig_parse_int_expr(value, &end, &parsed) )
        return 0;
    return *revconfig_skip_space(end) == '\0';
}

static int
revconfig_minimenu_action_from_symbol(char const* sym)
{
    assert(sym);
    if( sym[0] == '\0' )
        return 0;

#define MAP_ACTION(name)                                                                           \
    if( strcasecmp(sym, #name) == 0 )                                                              \
        return REVCONFIG_MINIMENU_##name;

    MAP_ACTION(CANCEL)
    MAP_ACTION(WALK)
    MAP_ACTION(IF_BUTTON)
    MAP_ACTION(IF_BUTTON_TOGGLE)
    MAP_ACTION(IF_BUTTON_SELECT)
    MAP_ACTION(RESUME_PAUSEBUTTON)
    MAP_ACTION(CLOSE_MODAL)
    MAP_ACTION(INV_BUTTON1)
    MAP_ACTION(INV_BUTTON2)
    MAP_ACTION(INV_BUTTON3)
    MAP_ACTION(INV_BUTTON4)
    MAP_ACTION(INV_BUTTON5)
    MAP_ACTION(FRIENDLIST_ADD)
    MAP_ACTION(IGNORELIST_ADD)
    MAP_ACTION(FRIENDLIST_DEL)
    MAP_ACTION(IGNORELIST_DEL)
    MAP_ACTION(MESSAGE_PRIVATE)
    MAP_ACTION(REPORT_ABUSE)
    MAP_ACTION(OPHELD1)
    MAP_ACTION(OPHELD2)
    MAP_ACTION(OPHELD3)
    MAP_ACTION(OPHELD4)
    MAP_ACTION(OPHELD5)
    MAP_ACTION(OPHELD6)
    /* The client's own. @see REVCONFIG_MINIMENU_PLUGIN_PANEL. */
    MAP_ACTION(PLUGIN_PANEL)
    /* Client.ts aliases */
    if( strcasecmp(sym, "CLOSE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_CLOSE_MODAL;
    if( strcasecmp(sym, "TOGGLE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE;
    if( strcasecmp(sym, "SELECT_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_IF_BUTTON_SELECT;
    if( strcasecmp(sym, "PAUSE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON;
    if( strcasecmp(sym, "ABUSE_REPORT") == 0 )
        return REVCONFIG_MINIMENU_REPORT_ABUSE;
    if( strcasecmp(sym, "ACCEPT_TRADEREQ") == 0 )
        return REVCONFIG_MINIMENU_OPPLAYER_TRADEREQ;
    if( strcasecmp(sym, "ACCEPT_DUELREQ") == 0 )
        return REVCONFIG_MINIMENU_OPPLAYER_DUELREQ;

#undef MAP_ACTION
    return 0;
}

int
revconfig_parse_minimenu_action(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0;

    int sym = revconfig_minimenu_action_from_symbol(str);
    if( sym != 0 )
        return sym;

    char const* end = NULL;
    int v = 0;
    if( revconfig_parse_int_expr(str, &end, &v) && *revconfig_skip_space(end) == '\0' && v > 0 )
        return v;

    TORIRS_ERR("revconfig_parse_minimenu_action: unknown action '%s'\n", str);
    assert(false && "unknown minimenu action in revconfig");
    return 0;
}

static int
revconfig_parse_chat_button_filter(char const* value)
{
    assert(value);
    if( value[0] == '\0' )
        return -1;
    if( strcasecmp(value, "public") == 0 )
        return 0;
    if( strcasecmp(value, "private") == 0 )
        return 1;
    if( strcasecmp(value, "trade") == 0 )
        return 2;
    if( strcasecmp(value, "report") == 0 )
        return 3;
    return revconfig_parse_int(value);
}

int
revconfig_parse_button_type(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0;

    if( strcasecmp(str, "ok") == 0 )
        return REVCONFIG_BUTTON_TYPE_OK;
    if( strcasecmp(str, "target") == 0 )
        return REVCONFIG_BUTTON_TYPE_TARGET;
    if( strcasecmp(str, "close") == 0 )
        return REVCONFIG_BUTTON_TYPE_CLOSE;
    if( strcasecmp(str, "toggle") == 0 )
        return REVCONFIG_BUTTON_TYPE_TOGGLE;
    if( strcasecmp(str, "select") == 0 )
        return REVCONFIG_BUTTON_TYPE_SELECT;
    if( strcasecmp(str, "continue") == 0 )
        return REVCONFIG_BUTTON_TYPE_CONTINUE;

    return revconfig_parse_int(str);
}

static void
revconfig_item_apply_uicomponent_field(
    struct RevConfigUIComponentItem* comp,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UICOMPONENT_TYPE:
        strncpy(comp->type, value, sizeof(comp->type) - 1);
        break;
    case RCFIELD_UICOMPONENT_SPRITE:
        strncpy(comp->sprite, value, sizeof(comp->sprite) - 1);
        break;
    case RCFIELD_UICOMPONENT_WIDTH:
        comp->width = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_HEIGHT:
        comp->height = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        comp->anchor_x = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        comp->anchor_y = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_TABNO:
        comp->tabno = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_SELECTED:
        comp->selected = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_SLOT:
        strncpy(comp->slot, value, sizeof(comp->slot) - 1);
        comp->slot[sizeof(comp->slot) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_ROLE:
        strncpy(comp->role, value, sizeof(comp->role) - 1);
        comp->role[sizeof(comp->role) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        strncpy(comp->sprite_active, value, sizeof(comp->sprite_active) - 1);
        break;
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        comp->componentno = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_INV:
        strncpy(comp->inv, value, sizeof(comp->inv) - 1);
        break;
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        strncpy(comp->paint_levels, value, sizeof(comp->paint_levels) - 1);
        comp->paint_levels[sizeof(comp->paint_levels) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_HOTKEY:
        /* Repeatable, like transform= and inv item=: each line appends. */
        if( comp->hotkey_count < REVCONFIG_COMPONENT_HOTKEY_MAX )
        {
            strncpy(
                comp->hotkeys[comp->hotkey_count],
                value,
                sizeof(comp->hotkeys[comp->hotkey_count]) - 1);
            comp->hotkeys[comp->hotkey_count][sizeof(comp->hotkeys[0]) - 1] = '\0';
            comp->hotkey_count++;
        }
        break;
    case RCFIELD_UICOMPONENT_COLOR:
        comp->color = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FILLED:
        comp->filled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_TILED:
        comp->tiled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_FONT:
        if( revconfig_font_field_is_numeric(value) )
        {
            comp->font = revconfig_parse_int(value);
            comp->has_font_ref = 0;
            comp->font_ref[0] = '\0';
        }
        else
        {
            strncpy(comp->font_ref, value, sizeof(comp->font_ref) - 1);
            comp->font_ref[sizeof(comp->font_ref) - 1] = '\0';
            comp->has_font_ref = 1;
        }
        break;
    case RCFIELD_UICOMPONENT_CENTER:
        comp->center = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_VALIGN:
        comp->valign = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_OVER_COLOR:
        comp->over_color = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_SHADOWED:
        comp->shadowed = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_TEXT:
        strncpy(comp->text, value, sizeof(comp->text) - 1);
        comp->text[sizeof(comp->text) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_FIELD:
        strncpy(comp->title_field, value, sizeof(comp->title_field) - 1);
        comp->title_field[sizeof(comp->title_field) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_PREFIX:
        strncpy(comp->title_prefix, value, sizeof(comp->title_prefix) - 1);
        comp->title_prefix[sizeof(comp->title_prefix) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_CARET:
        strncpy(comp->title_caret, value, sizeof(comp->title_caret) - 1);
        comp->title_caret[sizeof(comp->title_caret) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_CARET_BLINK:
        comp->title_caret_blink = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_TITLE_MASK:
        strncpy(comp->title_mask, value, sizeof(comp->title_mask) - 1);
        comp->title_mask[sizeof(comp->title_mask) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_MAXLEN:
        comp->title_maxlen = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_TITLE_CHARSET:
        strncpy(comp->title_charset, value, sizeof(comp->title_charset) - 1);
        comp->title_charset[sizeof(comp->title_charset) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_ACTION:
        strncpy(comp->title_action, value, sizeof(comp->title_action) - 1);
        comp->title_action[sizeof(comp->title_action) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_TITLE_MESSAGE_INDEX:
        comp->title_message_index = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_TITLE_PX_PER_PERCENT:
        comp->title_px_per_percent = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FLAME_BIAS:
        comp->flame_bias = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FLAME_SWAY:
        comp->flame_sway = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FLAME_RUN:
        comp->flame_run = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FLAME_ROW:
        comp->flame_row = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_FLAME_BLUR:
        strncpy(comp->flame_blur, value, sizeof(comp->flame_blur) - 1);
        break;
    case RCFIELD_UICOMPONENT_TEXT_BASELINE:
        comp->text_baseline = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_OPTION:
        strncpy(comp->option, value, sizeof(comp->option) - 1);
        comp->option[sizeof(comp->option) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OPTION_ACTION:
        comp->option_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP0:
        strncpy(comp->ops[0], value, sizeof(comp->ops[0]) - 1);
        comp->ops[0][sizeof(comp->ops[0]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP1:
        strncpy(comp->ops[1], value, sizeof(comp->ops[1]) - 1);
        comp->ops[1][sizeof(comp->ops[1]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP2:
        strncpy(comp->ops[2], value, sizeof(comp->ops[2]) - 1);
        comp->ops[2][sizeof(comp->ops[2]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP3:
        strncpy(comp->ops[3], value, sizeof(comp->ops[3]) - 1);
        comp->ops[3][sizeof(comp->ops[3]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP4:
        strncpy(comp->ops[4], value, sizeof(comp->ops[4]) - 1);
        comp->ops[4][sizeof(comp->ops[4]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP0_ACTION:
        comp->op_actions[0] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP1_ACTION:
        comp->op_actions[1] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP2_ACTION:
        comp->op_actions[2] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP3_ACTION:
        comp->op_actions[3] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP4_ACTION:
        comp->op_actions[4] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_BUTTON_TYPE:
        comp->button_type = revconfig_parse_button_type(value);
        break;
    case RCFIELD_UICOMPONENT_CLIENT_CODE:
        comp->client_code = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE:
        strncpy(comp->chat_op_report_abuse, value, sizeof(comp->chat_op_report_abuse) - 1);
        comp->chat_op_report_abuse[sizeof(comp->chat_op_report_abuse) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION:
        comp->chat_op_report_abuse_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE:
        strncpy(comp->chat_op_add_ignore, value, sizeof(comp->chat_op_add_ignore) - 1);
        comp->chat_op_add_ignore[sizeof(comp->chat_op_add_ignore) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION:
        comp->chat_op_add_ignore_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND:
        strncpy(comp->chat_op_add_friend, value, sizeof(comp->chat_op_add_friend) - 1);
        comp->chat_op_add_friend[sizeof(comp->chat_op_add_friend) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION:
        comp->chat_op_add_friend_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE:
        strncpy(comp->chat_op_accept_trade, value, sizeof(comp->chat_op_accept_trade) - 1);
        comp->chat_op_accept_trade[sizeof(comp->chat_op_accept_trade) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION:
        comp->chat_op_accept_trade_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL:
        strncpy(comp->chat_op_accept_duel, value, sizeof(comp->chat_op_accept_duel) - 1);
        comp->chat_op_accept_duel[sizeof(comp->chat_op_accept_duel) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION:
        comp->chat_op_accept_duel_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER:
        comp->chat_button_filter = revconfig_parse_chat_button_filter(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL:
        strncpy(comp->chat_button_label, value, sizeof(comp->chat_button_label) - 1);
        comp->chat_button_label[sizeof(comp->chat_button_label) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y:
        comp->chat_button_label_y = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y:
        comp->chat_button_mode_y = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0:
        strncpy(comp->chat_button_mode_label[0], value, sizeof(comp->chat_button_mode_label[0]) - 1);
        comp->chat_button_mode_label[0][sizeof(comp->chat_button_mode_label[0]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1:
        strncpy(comp->chat_button_mode_label[1], value, sizeof(comp->chat_button_mode_label[1]) - 1);
        comp->chat_button_mode_label[1][sizeof(comp->chat_button_mode_label[1]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2:
        strncpy(comp->chat_button_mode_label[2], value, sizeof(comp->chat_button_mode_label[2]) - 1);
        comp->chat_button_mode_label[2][sizeof(comp->chat_button_mode_label[2]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3:
        strncpy(comp->chat_button_mode_label[3], value, sizeof(comp->chat_button_mode_label[3]) - 1);
        comp->chat_button_mode_label[3][sizeof(comp->chat_button_mode_label[3]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR:
        comp->chat_button_mode_color[0] = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR:
        comp->chat_button_mode_color[1] = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR:
        comp->chat_button_mode_color[2] = revconfig_parse_int(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR:
        comp->chat_button_mode_color[3] = revconfig_parse_int(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_uilayout_field(
    struct RevConfigUILayoutItem* layout,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UILAYOUT_COMPONENT:
        strncpy(layout->component, value, sizeof(layout->component) - 1);
        break;
    case RCFIELD_UILAYOUT_X:
        layout->x = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_Y:
        layout->y = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_WIDTH:
        layout->width = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_HEIGHT:
        layout->height = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_ANCHOR_X:
        layout->anchor_x = revconfig_parse_int(value);
        layout->has_anchor = 1;
        break;
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        layout->anchor_y = revconfig_parse_int(value);
        layout->has_anchor = 1;
        break;
    case RCFIELD_UILAYOUT_TOP:
        layout->top = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_LEFT:
        layout->left = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_BOTTOM:
        layout->bottom = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_RIGHT:
        layout->right = revconfig_parse_int(value);
        break;
    case RCFIELD_UILAYOUT_DIRTY:
        layout->dirty = 1;
        break;
    case RCFIELD_UILAYOUT_XALIGN:
        layout->xalign_center = strcmp(value, "center") == 0 || strcmp(value, "centre") == 0;
        break;
    case RCFIELD_UILAYOUT_PARENT:
        strncpy(layout->parent, value, sizeof(layout->parent) - 1);
        break;
    case RCFIELD_UILAYOUT_NAME:
        strncpy(layout->name, value, sizeof(layout->name) - 1);
        break;
    case RCFIELD_UILAYOUT_GROUP:
        strncpy(layout->layout_group, value, sizeof(layout->layout_group) - 1);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_features_field(
    struct RevConfigFeaturesItem* features,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(features);
    assert(value);

    switch( kind )
    {
    case RCFIELD_FEATURES_ERA:
        strncpy(features->era, value, sizeof(features->era) - 1);
        features->era[sizeof(features->era) - 1] = '\0';
        break;
    case RCFIELD_FEATURES_GROUND_CLICK_NEAREST:
        strncpy(
            features->ground_click_nearest,
            value,
            sizeof(features->ground_click_nearest) - 1);
        features->ground_click_nearest[sizeof(features->ground_click_nearest) - 1] = '\0';
        break;
    case RCFIELD_FEATURES_MOVER:
        strncpy(features->mover, value, sizeof(features->mover) - 1);
        features->mover[sizeof(features->mover) - 1] = '\0';
        break;
    case RCFIELD_FEATURES_GROUND_CLICK_UNBOUNDED:
        features->ground_click_unbounded = revconfig_parse_int(value) ? 1 : 0;
        break;
    case RCFIELD_FEATURES_GROUND_CLICK_OFFMAP:
        features->ground_click_offmap = revconfig_parse_int(value) ? 1 : 0;
        break;
    case RCFIELD_FEATURES_PAINTER_DRAW_DISTANCE:
        features->painter_draw_distance = revconfig_parse_int(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_camera_field(
    struct RevConfigCameraItem* camera,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(camera);
    assert(value);

    switch( kind )
    {
    case RCFIELD_CAMERA_ZOOM:
        if( revconfig_parse_camera_zoom(value, camera) )
            camera->has_zoom = 1;
        else
            TORIRS_ERR("revconfig: [camera] zoom must be fixed:<height> or "
                "clamped:[<min>,<max>], got '%s'\n",
                value);
        break;
    case RCFIELD_CAMERA_CONTROLS:
    {
        int controls = revconfig_parse_camera_controls(value);
        if( controls >= 0 )
        {
            camera->controls = controls;
            camera->has_controls = 1;
        }
        else
            TORIRS_LOG("revconfig: [camera] controls must be a comma list of "
                "mmb|arrow_keys, got '%s'\n",
                value);
        break;
    }
    case RCFIELD_CAMERA_WHEEL_STEP:
    {
        int step = revconfig_parse_int(value);
        if( step > 0 )
        {
            camera->wheel_step = step;
            camera->has_wheel_step = 1;
        }
        else
            TORIRS_LOG("revconfig: [camera] wheel_step must be a positive eye-height "
                "step, got '%s'\n",
                value);
        break;
    }
    default:
        break;
    }
}

/** The INI spelling of one `[chrome]` key, for the complaints below: the reader
 *  of a bad profile is holding the INI, not this enum. */
static char const*
revconfig_chrome_key_str(enum RevConfigFieldKind kind)
{
    switch( kind )
    {
    case RCFIELD_CHROME_PLUGIN_BUTTON_PARENT:
        return "plugin_button_parent";
    case RCFIELD_CHROME_PLUGIN_BUTTON_X:
        return "plugin_button_x";
    case RCFIELD_CHROME_PLUGIN_BUTTON_Y:
        return "plugin_button_y";
    case RCFIELD_CHROME_PLUGIN_BUTTON_W:
        return "plugin_button_w";
    case RCFIELD_CHROME_PLUGIN_BUTTON_H:
        return "plugin_button_h";
    default:
        return revconfig_field_kind_str(kind);
    }
}

/**
 * One `[chrome]` key.
 *
 * A number that does not parse is REPORTED and left unstated, rather than
 * applied as the 0 atoi() hands back: a plate zero pixels tall is an invisible
 * button in the middle of the logout tab, which is a far worse answer than the
 * client simply not building one on this revision.
 */
static void
revconfig_item_apply_chrome_field(
    struct RevConfigChromeItem* chrome,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(chrome);
    assert(value);

    switch( kind )
    {
    case RCFIELD_CHROME_PLUGIN_IFACE:
        strncpy(chrome->plugin_iface, value, sizeof(chrome->plugin_iface) - 1);
        chrome->plugin_iface[sizeof(chrome->plugin_iface) - 1] = '\0';
        break;
    case RCFIELD_CHROME_PLUGIN_BUTTON_OP:
        strncpy(chrome->plugin_button_op, value, sizeof(chrome->plugin_button_op) - 1);
        chrome->plugin_button_op[sizeof(chrome->plugin_button_op) - 1] = '\0';
        break;
    case RCFIELD_CHROME_PLUGIN_BUTTON_PARENT:
    case RCFIELD_CHROME_PLUGIN_BUTTON_X:
    case RCFIELD_CHROME_PLUGIN_BUTTON_Y:
    case RCFIELD_CHROME_PLUGIN_BUTTON_W:
    case RCFIELD_CHROME_PLUGIN_BUTTON_H:
    {
        int const n = revconfig_parse_int(value);
        /* A geometry of nothing is not a smaller button, it is an invisible
         * one; a child or a position cannot be negative. */
        int const floor_value =
            (kind == RCFIELD_CHROME_PLUGIN_BUTTON_W ||
             kind == RCFIELD_CHROME_PLUGIN_BUTTON_H)
                ? 1
                : 0;
        if( n < floor_value )
        {
            TORIRS_LOG("revconfig: [chrome] %s must be >= %d, got '%s'\n",
                revconfig_chrome_key_str(kind),
                floor_value,
                value);
            break;
        }
        if( kind == RCFIELD_CHROME_PLUGIN_BUTTON_PARENT )
            chrome->plugin_button_parent = n;
        else if( kind == RCFIELD_CHROME_PLUGIN_BUTTON_X )
            chrome->plugin_button_x = n;
        else if( kind == RCFIELD_CHROME_PLUGIN_BUTTON_Y )
            chrome->plugin_button_y = n;
        else if( kind == RCFIELD_CHROME_PLUGIN_BUTTON_W )
            chrome->plugin_button_w = n;
        else
            chrome->plugin_button_h = n;
        break;
    }
    default:
        break;
    }
}

static void
revconfig_item_apply_field(
    struct RevConfigItem* item,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(item);
    if( item->kind == RCITEM_NONE )
        return;

    switch( item->kind )
    {
    case RCITEM_CACHE_SPRITE:
        revconfig_item_apply_cache_field(&item->u.cache, kind, value);
        break;
    case RCITEM_CACHE_FONT:
        revconfig_item_apply_font_field(&item->u.font, kind, value);
        break;
    case RCITEM_UICOMPONENT:
        revconfig_item_apply_uicomponent_field(&item->u.uicomponent, kind, value);
        break;
    case RCITEM_UILAYOUT:
        revconfig_item_apply_uilayout_field(&item->u.uilayout, kind, value);
        break;
    case RCITEM_INV:
        if( kind == RCFIELD_INV_ITEM && item->u.inv.item_count < REVCONFIG_INV_MAX_ITEMS )
        {
            strncpy(
                item->u.inv.items[item->u.inv.item_count],
                value,
                sizeof(item->u.inv.items[item->u.inv.item_count]) - 1);
            item->u.inv.item_count++;
        }
        break;
    case RCITEM_CACHE_REF:
        if( kind == RCFIELD_CACHEREF_ID )
            item->u.cacheref.id = revconfig_parse_int(value);
        break;
    case RCITEM_STRING:
        if( kind == RCFIELD_STRING_TEXT )
        {
            strncpy(item->u.string.text, value, sizeof(item->u.string.text) - 1);
            item->u.string.text[sizeof(item->u.string.text) - 1] = '\0';
        }
        break;
    case RCITEM_PRELOAD:
        switch( kind )
        {
        case RCFIELD_PRELOAD_KIND:
            strncpy(item->u.preload.kind, value, sizeof(item->u.preload.kind) - 1);
            break;
        case RCFIELD_PRELOAD_ARCHIVE:
            strncpy(item->u.preload.archive, value, sizeof(item->u.preload.archive) - 1);
            break;
        case RCFIELD_PRELOAD_SAY:
            strncpy(item->u.preload.say, value, sizeof(item->u.preload.say) - 1);
            break;
        case RCFIELD_PRELOAD_ID:
            item->u.preload.id = revconfig_parse_int(value);
            break;
        case RCFIELD_PRELOAD_PERCENT:
            item->u.preload.percent = revconfig_parse_int(value);
            break;
        case RCFIELD_PRELOAD_WEIGHT:
            item->u.preload.weight = revconfig_parse_int(value);
            break;
        case RCFIELD_PRELOAD_ORDER:
            item->u.preload.order = revconfig_parse_int(value);
            break;
        case RCFIELD_PRELOAD_RENDER:
            item->u.preload.render = (strcmp(value, "true") == 0 ||
                                      strcmp(value, "yes") == 0 || strcmp(value, "1") == 0)
                                         ? 1
                                         : 0;
            break;
        default:
            break;
        }
        break;
    case RCITEM_LOGIN_REPLY:
        if( kind == RCFIELD_LOGIN_REPLY_SCREEN )
        {
            item->u.login_reply.screen = revconfig_parse_int(value);
        }
        else if(
            kind == RCFIELD_LOGIN_REPLY_LINE1 || kind == RCFIELD_LOGIN_REPLY_LINE2 ||
            kind == RCFIELD_LOGIN_REPLY_LINE3 )
        {
            int line = kind == RCFIELD_LOGIN_REPLY_LINE1   ? 0
                       : kind == RCFIELD_LOGIN_REPLY_LINE2 ? 1
                                                           : 2;
            strncpy(
                item->u.login_reply.line[line],
                value,
                sizeof(item->u.login_reply.line[line]) - 1);
            item->u.login_reply.line[line][sizeof(item->u.login_reply.line[line]) - 1] = '\0';
        }
        break;
    case RCITEM_ROLE:
        if( kind == RCFIELD_ROLE_MATCH &&
            item->u.role.matcher_count < REVCONFIG_ROLE_MAX_MATCHERS )
        {
            /* A line that does not parse has already been reported; dropping
             * it keeps the rungs that DID parse working. */
            if( revconfig_parse_role_matcher(
                    value, &item->u.role.matchers[item->u.role.matcher_count]) )
                item->u.role.matcher_count++;
        }
        break;
    case RCITEM_FEATURES:
        revconfig_item_apply_features_field(&item->u.features, kind, value);
        break;
    case RCITEM_CAMERA:
        revconfig_item_apply_camera_field(&item->u.camera, kind, value);
        break;
    case RCITEM_CHROME:
        revconfig_item_apply_chrome_field(&item->u.chrome, kind, value);
        break;
    case RCITEM_HOTKEY:
        if( kind == RCFIELD_HOTKEY_COMPONENT )
        {
            strncpy(item->u.hotkey.component, value, sizeof(item->u.hotkey.component) - 1);
            item->u.hotkey.component[sizeof(item->u.hotkey.component) - 1] = '\0';
        }
        else if( kind == RCFIELD_HOTKEY_EFFECT )
        {
            strncpy(item->u.hotkey.effect, value, sizeof(item->u.hotkey.effect) - 1);
            item->u.hotkey.effect[sizeof(item->u.hotkey.effect) - 1] = '\0';
        }
        break;
    default:
        break;
    }
}

static void
revconfig_item_finish(
    struct RevConfigItem* pending,
    struct RevConfigItemBuffer* out)
{
    assert(pending);
    assert(out);
    if( pending->kind == RCITEM_NONE )
        return;

    struct RevConfigItem* item = revconfig_item_buffer_push(out);
    if( !item )
        return;

    *item = *pending;
    pending->kind = RCITEM_NONE;
}

void
revconfig_items_build(
    const struct RevConfigBuffer* fields,
    struct RevConfigItemBuffer* out)
{
    assert(fields);
    assert(out);

    struct RevConfigItem pending = { 0 };

    for( uint32_t i = 0; i < fields->field_count; i++ )
    {
        const struct RevConfigField* field = &fields->fields[i];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            revconfig_item_begin(&pending, field->value);
            break;
        case RCFIELD_ITEMNAME:
            revconfig_item_set_name(&pending, field->value);
            break;
        case RCFIELD_ITEMDONE:
            revconfig_item_finish(&pending, out);
            break;
        default:
            revconfig_item_apply_field(&pending, field->kind, field->value);
            break;
        }
    }
}

int
revconfig_parse_camera_zoom(
    char const* str,
    struct RevConfigCameraItem* out)
{
    char const* p;

    assert(str);
    assert(out);

    p = revconfig_skip_space(str);
    if( strncmp(p, "fixed:", 6) == 0 )
    {
        int height;

        if( !revconfig_parse_int_expr(p + 6, &p, &height) )
            return 0;
        if( *revconfig_skip_space(p) != '\0' )
            return 0;
        if( height <= 0 )
            return 0;
        out->zoom_mode = REVCONFIG_CAMERA_ZOOM_FIXED;
        out->zoom_height = height;
        /* Stated on the fixed branch too, so a reader of the resolved struct
         * never has to know which branch filled it in: the band is the point. */
        out->zoom_min = height;
        out->zoom_max = height;
        return 1;
    }
    if( strncmp(p, "clamped:", 8) == 0 )
    {
        int lo;
        int hi;

        p = revconfig_skip_space(p + 8);
        if( *p != '[' )
            return 0;
        /* The bounds are expressions, so the separator is where the first one
         * stopped -- not the first comma in the line, which may well be inside
         * one of them. */
        if( !revconfig_parse_int_expr(p + 1, &p, &lo) )
            return 0;
        p = revconfig_skip_space(p);
        if( *p != ',' )
            return 0;
        if( !revconfig_parse_int_expr(p + 1, &p, &hi) )
            return 0;
        p = revconfig_skip_space(p);
        if( *p != ']' )
            return 0;
        if( lo <= 0 || hi < lo )
            return 0;
        out->zoom_mode = REVCONFIG_CAMERA_ZOOM_CLAMPED;
        out->zoom_min = lo;
        out->zoom_max = hi;
        /* Rest position: the reference height when the band contains it, and
         * the nearest end when it does not. */
        out->zoom_height = REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT;
        if( out->zoom_height < lo )
            out->zoom_height = lo;
        if( out->zoom_height > hi )
            out->zoom_height = hi;
        return 1;
    }
    return 0;
}

int
revconfig_parse_camera_controls(char const* str)
{
    int controls = 0;
    char const* p;

    assert(str);

    p = revconfig_skip_space(str);
    while( *p )
    {
        char name[32];
        char const* end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        while( len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t') )
            len--;
        if( len >= sizeof(name) )
            return -1;
        memcpy(name, p, len);
        name[len] = '\0';

        if( strcmp(name, "mmb") == 0 )
            controls |= REVCONFIG_CAMERA_CONTROL_MMB;
        else if( strcmp(name, "arrow_keys") == 0 )
            controls |= REVCONFIG_CAMERA_CONTROL_ARROW_KEYS;
        else if( name[0] != '\0' )
            return -1;

        if( !end )
            break;
        p = revconfig_skip_space(end + 1);
    }
    return controls;
}

/*
 * ---------------------------------------------------------------------------
 * [role:…] matcher parsing
 * ---------------------------------------------------------------------------
 *
 * Every form is `<head>(<args>)`, so the whole grammar is one split plus a
 * per-head reading of the arguments. Nested calls are ordinary here -- both
 * `id(if(553, 0))` and `cc(iface(xpdrop), 4)` carry a call inside an argument
 * -- so argument splitting counts parenthesis depth rather than taking the
 * first comma it sees.
 */

/** Copy `src[0..len)` into `dst` with the ends trimmed. 0 when it will not fit. */
static int
revconfig_role_copy_trimmed(char* dst, size_t cap, char const* src, size_t len)
{
    assert(dst);
    assert(src);

    while( len > 0 && (*src == ' ' || *src == '\t') )
    {
        src++;
        len--;
    }
    while( len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t') )
        len--;

    if( len >= cap )
        return 0;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

/**
 * Split `str` as `<head>(<body>)`, with nothing but space after the close.
 *
 * Returns 1 and points `out_body`/`out_body_len` at the inside of the parens.
 */
static int
revconfig_role_split_call(
    char const* str,
    char* out_head,
    size_t head_cap,
    char const** out_body,
    size_t* out_body_len)
{
    char const* open;
    char const* p;
    int depth;

    assert(str);
    assert(out_head);
    assert(out_body);
    assert(out_body_len);

    open = strchr(str, '(');
    if( !open )
        return 0;
    if( !revconfig_role_copy_trimmed(out_head, head_cap, str, (size_t)(open - str)) )
        return 0;
    if( out_head[0] == '\0' )
        return 0;

    depth = 0;
    for( p = open; *p; p++ )
    {
        if( *p == '(' )
            depth++;
        else if( *p == ')' )
        {
            depth--;
            if( depth == 0 )
                break;
        }
    }
    if( depth != 0 || *p != ')' )
        return 0;

    /* A trailing tail -- `id(4) junk` -- is a malformed line, not a matcher
     * with something ignorable after it. */
    if( *revconfig_skip_space(p + 1) != '\0' )
        return 0;

    *out_body = open + 1;
    *out_body_len = (size_t)(p - (open + 1));
    return 1;
}

/**
 * Offset of the comma separating the first argument of `s[0..len)` from the
 * rest, skipping any nested call's own commas. -1 when there is only one.
 */
static long
revconfig_role_arg_split(char const* s, size_t len)
{
    int depth = 0;

    assert(s);

    for( size_t i = 0; i < len; i++ )
    {
        if( s[i] == '(' )
            depth++;
        else if( s[i] == ')' )
            depth--;
        else if( s[i] == ',' && depth == 0 )
            return (long)i;
    }
    return -1;
}

/** Parse `s[0..len)` as one whole integer expression. */
static int
revconfig_role_parse_int(char const* s, size_t len, int* out_value)
{
    char buf[64];
    char const* end;

    assert(s);
    assert(out_value);

    if( !revconfig_role_copy_trimmed(buf, sizeof(buf), s, len) )
        return 0;
    if( buf[0] == '\0' )
        return 0;
    if( !revconfig_parse_int_expr(buf, &end, out_value) )
        return 0;
    return *revconfig_skip_space(end) == '\0';
}

/** Parse an `id(<expr>)` or `iface(<name>[, <child>])` reference. */
static int
revconfig_role_parse_ref(char const* s, size_t len, struct RevConfigRoleRef* out)
{
    char text[64];
    char head[32];
    char const* body;
    size_t body_len;
    long comma;

    assert(s);
    assert(out);

    if( !revconfig_role_copy_trimmed(text, sizeof(text), s, len) )
        return 0;
    if( !revconfig_role_split_call(text, head, sizeof(head), &body, &body_len) )
        return 0;

    memset(out, 0, sizeof(*out));

    if( strcmp(head, "id") == 0 )
    {
        if( !revconfig_role_parse_int(body, body_len, &out->value) )
            return 0;
        out->kind = REVCONFIG_ROLE_MATCH_ID;
        return 1;
    }

    if( strcmp(head, "iface") == 0 )
    {
        comma = revconfig_role_arg_split(body, body_len);
        if( comma < 0 )
        {
            if( !revconfig_role_copy_trimmed(
                    out->name, sizeof(out->name), body, body_len) )
                return 0;
            /* Child 0 is the group's own root, which is what naming a group
             * with no child means. */
            out->value = 0;
        }
        else
        {
            if( !revconfig_role_copy_trimmed(
                    out->name, sizeof(out->name), body, (size_t)comma) )
                return 0;
            if( !revconfig_role_parse_int(
                    body + comma + 1, body_len - (size_t)comma - 1, &out->value) )
                return 0;
        }
        if( out->name[0] == '\0' )
            return 0;
        out->kind = REVCONFIG_ROLE_MATCH_IFACE;
        return 1;
    }

    return 0;
}

int
revconfig_parse_role_matcher(char const* str, struct RevConfigRoleMatcher* out)
{
    struct RevConfigRoleMatcher matcher;
    char head[32];
    char const* body;
    size_t body_len;
    long comma;

    assert(str);
    assert(out);

    memset(&matcher, 0, sizeof(matcher));
    matcher.value = -1;

    if( !revconfig_role_split_call(str, head, sizeof(head), &body, &body_len) )
        goto malformed;

    if( strcmp(head, "slot") == 0 )
    {
        comma = revconfig_role_arg_split(body, body_len);
        if( comma < 0 )
        {
            if( !revconfig_role_copy_trimmed(
                    matcher.slot, sizeof(matcher.slot), body, body_len) )
                goto malformed;
        }
        else
        {
            if( !revconfig_role_copy_trimmed(
                    matcher.slot, sizeof(matcher.slot), body, (size_t)comma) )
                goto malformed;
            /* Kept verbatim: which numbering a member is in belongs to the
             * role -- a chat button's filter has names, a sidebar mount's
             * tabno does not -- and that is the ui layer's to know. */
            if( !revconfig_role_copy_trimmed(
                    matcher.member,
                    sizeof(matcher.member),
                    body + comma + 1,
                    body_len - (size_t)comma - 1) )
                goto malformed;
            if( matcher.member[0] == '\0' )
                goto malformed;
        }
        if( matcher.slot[0] == '\0' )
            goto malformed;
        matcher.kind = REVCONFIG_ROLE_MATCH_SLOT;
    }
    else if( strcmp(head, "clientcode") == 0 )
    {
        if( !revconfig_role_parse_int(body, body_len, &matcher.value) )
            goto malformed;
        matcher.kind = REVCONFIG_ROLE_MATCH_CLIENTCODE;
    }
    else if( strcmp(head, "cc") == 0 )
    {
        comma = revconfig_role_arg_split(body, body_len);
        if( comma < 0 )
            goto malformed;
        if( !revconfig_role_parse_ref(body, (size_t)comma, &matcher.ref) )
            goto malformed;
        if( !revconfig_role_parse_int(
                body + comma + 1, body_len - (size_t)comma - 1, &matcher.value) )
            goto malformed;
        matcher.kind = REVCONFIG_ROLE_MATCH_CC;
    }
    else
    {
        /* id() and iface() are references in their own right. */
        if( !revconfig_role_parse_ref(str, strlen(str), &matcher.ref) )
            goto malformed;
        matcher.kind = matcher.ref.kind;
    }

    *out = matcher;
    return 1;

malformed:
    TORIRS_LOG("revconfig: unrecognised role matcher '%s'\n", str);
    return 0;
}
