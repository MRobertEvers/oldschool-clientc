/*
 * The shop model, its two files and its prices (editor_shop.h). The emitter
 * is tools/gen_shop_scripts.py's render_rs2 / render_inv written again in C,
 * line for line, so a shop the panel saves is the file the generator would
 * have written for the same rows (test-editor-shop-quest compares the two).
 */
#include "editor_shop.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- names and keys ------------------------------------------------------ */

int
Editor_ShopKeyValid(const char* key)
{
    size_t length;

    assert(key);
    length = strlen(key);
    if( length == 0 || length >= EDITOR_SHOP_KEY_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = key[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

int
Editor_ShopNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_SHOP_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        if( !isalnum((unsigned char)name[i]) && name[i] != '_' )
            return 0;
    }
    return 1;
}

void
Editor_ShopDocKeys(
    const char* key,
    char* out_rs2,
    size_t rs2_size,
    char* out_inv,
    size_t inv_size)
{
    int written;

    assert(key);
    assert(out_rs2);
    assert(out_inv);
    assert(Editor_ShopKeyValid(key));

    written = snprintf(out_rs2, rs2_size, "rs2:" EDITOR_SHOP_DIR "/%s/scripts/%s.rs2", key, key);
    assert(written > 0);
    assert((size_t)written < rs2_size);
    written = snprintf(out_inv, inv_size, "rs2:" EDITOR_SHOP_DIR "/%s/configs/%s.inv", key, key);
    assert(written > 0);
    assert((size_t)written < inv_size);
    (void)written;
}

/* ---- the model ------------------------------------------------------------- */

void
Editor_ShopInit(
    struct Editor_Shop* shop,
    const char* key)
{
    assert(shop);
    assert(key);
    assert(Editor_ShopKeyValid(key));

    memset(shop, 0, sizeof(*shop));
    snprintf(shop->key, sizeof(shop->key), "%s", key);
    snprintf(shop->name, sizeof(shop->name), "%s", key);
    snprintf(shop->inv, sizeof(shop->inv), "%s", key);
    shop->op = 3;
    shop->sell = 1300;
    shop->buy = 400;
    shop->delta = 30;
    shop->origin = EDITOR_SHOP_ORIGIN_EDITOR;
}

int
Editor_ShopIsGeneral(const struct Editor_Shop* shop)
{
    static const char k_general[] = "general store";

    assert(shop);
    if( strlen(shop->special) != sizeof(k_general) - 1 )
        return 0;
    for( size_t i = 0; i < sizeof(k_general) - 1; i++ )
    {
        if( tolower((unsigned char)shop->special[i]) != k_general[i] )
            return 0;
    }
    return 1;
}

int
Editor_ShopOwnerAdd(
    struct Editor_Shop* shop,
    const char* npc)
{
    assert(shop);
    assert(npc);

    if( shop->owner_count >= EDITOR_SHOP_OWNERS_MAX )
        return 0;
    for( int i = 0; i < shop->owner_count; i++ )
    {
        if( strcmp(shop->owners[i].npc, npc) == 0 )
            return 0;
    }
    snprintf(shop->owners[shop->owner_count].npc, sizeof(shop->owners[0].npc), "%s", npc);
    shop->owners[shop->owner_count].declared = 1;
    shop->owner_count++;
    return 1;
}

int
Editor_ShopStockAdd(
    struct Editor_Shop* shop,
    const char* obj,
    int count,
    int rate)
{
    struct Editor_ShopStock* row;

    assert(shop);
    assert(obj);

    if( shop->stock_count >= EDITOR_SHOP_STOCK_MAX )
        return -1;
    row = &shop->stock[shop->stock_count];
    snprintf(row->obj, sizeof(row->obj), "%s", obj);
    row->count = count;
    row->rate = rate;
    return shop->stock_count++;
}

void
Editor_ShopStockRemove(
    struct Editor_Shop* shop,
    int index)
{
    assert(shop);
    assert(index >= 0);
    assert(index < shop->stock_count);

    memmove(&shop->stock[index], &shop->stock[index + 1],
        (size_t)(shop->stock_count - index - 1) * sizeof(shop->stock[0]));
    shop->stock_count--;
}

/* ---- prices ----------------------------------------------------------------- */

int
Editor_ShopValue(
    int multiplier,
    int haggle,
    int stock,
    int base,
    int cost)
{
    long long moved = (long long)(stock - base) * (long long)haggle;
    long long rate;

    if( moved < -5000 )
        moved = -5000;
    if( moved > 1000 )
        moved = 1000;
    rate = (long long)multiplier - moved;
    if( rate < 100 )
        rate = 100;
    /* RuneScript's scale(a, b, c): c * a / b, truncated toward zero. */
    return (int)((long long)cost * rate / 1000);
}

int
Editor_ShopSellsAt(
    const struct Editor_Shop* shop,
    int index,
    int cost)
{
    int value;

    assert(shop);
    assert(index >= 0);
    assert(index < shop->stock_count);

    value = Editor_ShopValue(shop->sell, shop->delta, shop->stock[index].count, shop->stock[index].count, cost);
    return value < 1 ? 1 : value;
}

int
Editor_ShopBuysAt(
    const struct Editor_Shop* shop,
    int index,
    int cost)
{
    assert(shop);
    assert(index >= 0);
    assert(index < shop->stock_count);

    return Editor_ShopValue(shop->buy, shop->delta, shop->stock[index].count, shop->stock[index].count, cost);
}

/* ---- emitting ---------------------------------------------------------------- */

struct shop_out
{
    char* data;
    size_t capacity;
    size_t length;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void
shop_put(
    struct shop_out* out,
    const char* format,
    ...)
{
    va_list args;
    int written;
    size_t room = out->length < out->capacity ? out->capacity - out->length : 0;

    va_start(args, format);
    written = vsnprintf(room ? out->data + out->length : NULL, room, format, args);
    va_end(args);
    assert(written >= 0);
    out->length += (size_t)written;
}

static size_t
shop_out_end(struct shop_out* out)
{
    if( out->capacity > 0 )
        out->data[out->length < out->capacity ? out->length : out->capacity - 1] = '\0';
    return out->length;
}

/** The citation lines both files carry (render_rs2 / render_inv's second to
 *  fourth lines, or the authored line). */
static void
shop_put_citation(
    struct shop_out* out,
    const struct Editor_Shop* shop)
{
    shop_put(out, "// %s\n", shop->name);
    if( !shop->page_title[0] )
    {
        shop_put(out, "%s\n", EDITOR_SHOP_AUTHORED_LINE);
        return;
    }
    shop_put(out, "// https://oldschool.runescape.wiki/w/");
    for( const char* at = shop->page_title; *at; at++ )
        shop_put(out, "%c", *at == ' ' ? '_' : *at);
    shop_put(out, "\n// Wiki revision %s, scraped %s.\n", shop->revid, shop->fetch_date);
}

static int
shop_name_compare(
    const void* a,
    const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

size_t
Editor_ShopEmitRs2(
    const struct Editor_Shop* shop,
    const char* mark,
    char* out,
    size_t out_capacity)
{
    struct shop_out o = { out, out_capacity, 0 };
    const char* patched[EDITOR_SHOP_OWNERS_MAX];
    int patched_count = 0;
    int declared_count = 0;

    assert(shop);
    assert(mark);
    assert(out || out_capacity == 0);

    shop_put(&o, "%s\n", mark);
    shop_put_citation(&o, shop);
    /* f"// {owner_note} {'Members. ' if members}{special}." */
    if( shop->owner_count > 0 )
    {
        shop_put(&o, "// Owner: ");
        for( int i = 0; i < shop->owner_count; i++ )
            shop_put(&o, "%s%s", i ? ", " : "", shop->owners[i].npc);
        shop_put(&o, ".");
    }
    else
        shop_put(&o, "// Owner access: quest-gated by hand-authored dialogue.");
    shop_put(&o, " %s%s.\n", shop->members ? "Members. " : "", shop->special);

    /* The patched note: sorted(set(owners) - set(fresh)), or a blank line. */
    for( int i = 0; i < shop->owner_count; i++ )
    {
        if( shop->owners[i].declared )
            declared_count++;
        else
            patched[patched_count++] = shop->owners[i].npc;
    }
    if( patched_count > 0 )
    {
        qsort(patched, (size_t)patched_count, sizeof(patched[0]), shop_name_compare);
        shop_put(&o, "// ");
        for( int i = 0; i < patched_count; i++ )
            shop_put(&o, "%s%s", i ? ", " : "", patched[i]);
        shop_put(&o, " already had an [opnpc3] trade stub \xe2\x80\x94 that trigger now calls @%s_open in place.",
            shop->key);
    }
    shop_put(&o, "\n\n");

    /* The triggers, one element of the generator's list: a blank line when
     * no owner is declared here. */
    if( declared_count == 0 )
        shop_put(&o, "\n");
    for( int i = 0; i < shop->owner_count; i++ )
    {
        if( shop->owners[i].declared )
            shop_put(&o, "[opnpc%d,%s] @%s_open;\n", shop->op, shop->owners[i].npc, shop->key);
    }
    shop_put(&o, "\n[label,%s_open]\n~openshop(%s, %d, %d, %d, \"", shop->key, shop->inv, shop->buy, shop->sell,
        shop->delta);
    for( const char* at = shop->name; *at; at++ )
        shop_put(&o, "%c", *at == '"' ? '\'' : *at);
    shop_put(&o, "\");\n");
    return shop_out_end(&o);
}

size_t
Editor_ShopEmitInv(
    const struct Editor_Shop* shop,
    const char* mark,
    char* out,
    size_t out_capacity)
{
    struct shop_out o = { out, out_capacity, 0 };

    assert(shop);
    assert(mark);
    assert(out || out_capacity == 0);

    shop_put(&o, "%s\n", mark);
    shop_put_citation(&o, shop);
    shop_put(&o, "[%s]\nscope=shared\nrestock=yes\nstackall=yes\n", shop->inv);
    if( Editor_ShopIsGeneral(shop) )
        shop_put(&o, "allstock=yes\n");
    for( int i = 0; i < shop->stock_count; i++ )
        shop_put(&o, "stock%d=%s,%d,%d\n", i + 1, shop->stock[i].obj, shop->stock[i].count, shop->stock[i].rate);
    return shop_out_end(&o);
}

const char*
Editor_ShopSaveRefusal(const struct Editor_Shop* shop)
{
    assert(shop);
    if( shop->origin == EDITOR_SHOP_ORIGIN_GENERATOR )
        return "it is tools/gen_shop_scripts.py's output: change the wiki catalogue and regenerate";
    return NULL;
}

/* ---- parsing ----------------------------------------------------------------- */

struct shop_lines
{
    const char* text;
    size_t length;
    size_t at;
    int number;
};

/** The next line without its newline into `out`; 0 at the end of the text.
 *  A line that does not fit is 0 too, with `*out_long` set. */
static int
shop_next_line(
    struct shop_lines* lines,
    char* out,
    size_t out_size,
    int* out_long)
{
    size_t start = lines->at;
    size_t stop;

    *out_long = 0;
    if( lines->at >= lines->length )
        return 0;
    stop = start;
    while( stop < lines->length && lines->text[stop] != '\n' )
        stop++;
    lines->at = stop < lines->length ? stop + 1 : stop;
    lines->number++;
    if( stop - start >= out_size )
    {
        *out_long = 1;
        return 0;
    }
    memcpy(out, lines->text + start, stop - start);
    out[stop - start] = '\0';
    return 1;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static int
shop_refuse(
    char* why,
    size_t why_size,
    const char* format,
    ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(why, why_size, format, args);
    va_end(args);
    return 0;
}

static int
shop_starts(
    const char* text,
    const char* prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/** "<text>" ending in `suffix`: the part before it into `out`. */
static int
shop_strip_suffix(
    const char* text,
    const char* suffix,
    char* out,
    size_t out_size)
{
    size_t const length = strlen(text);
    size_t const suffix_length = strlen(suffix);

    if( length < suffix_length || strcmp(text + length - suffix_length, suffix) != 0 )
        return 0;
    if( length - suffix_length >= out_size )
        return 0;
    memcpy(out, text, length - suffix_length);
    out[length - suffix_length] = '\0';
    return 1;
}

/** The mark, the name and the citation: shared by both files. Returns 1
 *  with the shop's fields set (or compared, when `compare`). */
static int
shop_parse_head(
    struct shop_lines* lines,
    struct Editor_Shop* shop,
    int compare,
    const char* file,
    char* why,
    size_t why_size)
{
    char line[512];
    int too_long;
    enum Editor_ShopOrigin origin;
    char name[EDITOR_SHOP_TEXT_MAX];
    char page[EDITOR_SHOP_TEXT_MAX] = "";
    char revid[24] = "";
    char date[24] = "";

    if( !shop_next_line(lines, line, sizeof(line), &too_long) )
        return shop_refuse(why, why_size, "%s: empty", file);
    if( strcmp(line, EDITOR_SHOP_MARK) == 0 )
        origin = EDITOR_SHOP_ORIGIN_EDITOR;
    else if( strcmp(line, EDITOR_SHOP_GENERATOR_MARK) == 0 )
        origin = EDITOR_SHOP_ORIGIN_GENERATOR;
    else
        return shop_refuse(why, why_size, "%s line 1: not the shop builder's or gen_shop_scripts.py's mark", file);

    if( !shop_next_line(lines, line, sizeof(line), &too_long) || !shop_starts(line, "// ")
        || strlen(line + 3) >= sizeof(name) )
        return shop_refuse(why, why_size, "%s line 2: not \"// <shop name>\"", file);
    snprintf(name, sizeof(name), "%s", line + 3);

    if( !shop_next_line(lines, line, sizeof(line), &too_long) )
        return shop_refuse(why, why_size, "%s line 3: no citation", file);
    if( shop_starts(line, "// https://oldschool.runescape.wiki/w/") )
    {
        const char* title = line + strlen("// https://oldschool.runescape.wiki/w/");
        char* comma;
        if( strlen(title) >= sizeof(page) )
            return shop_refuse(why, why_size, "%s line 3: the page title is too long", file);
        /* The URL spells the title's spaces as '_'; the title is kept so. */
        snprintf(page, sizeof(page), "%s", title);
        if( !shop_next_line(lines, line, sizeof(line), &too_long) || !shop_starts(line, "// Wiki revision ") )
            return shop_refuse(why, why_size, "%s line 4: not \"// Wiki revision <n>, scraped <date>.\"", file);
        comma = strstr(line, ", scraped ");
        if( !comma || line[strlen(line) - 1] != '.' )
            return shop_refuse(why, why_size, "%s line 4: not \"// Wiki revision <n>, scraped <date>.\"", file);
        *comma = '\0';
        /* The date without its closing '.'. */
        comma[10 + strlen(comma + 10) - 1] = '\0';
        if( strlen(line + 17) >= sizeof(revid) || strlen(comma + 10) >= sizeof(date) )
            return shop_refuse(why, why_size, "%s line 4: the revision or the date is too long", file);
        snprintf(revid, sizeof(revid), "%s", line + 17);
        snprintf(date, sizeof(date), "%s", comma + 10);
    }
    else if( strcmp(line, EDITOR_SHOP_AUTHORED_LINE) != 0 )
        return shop_refuse(why, why_size, "%s line 3: neither a wiki citation nor the authored line", file);

    if( compare )
    {
        if( origin != shop->origin || strcmp(name, shop->name) != 0 || strcmp(page, shop->page_title) != 0
            || strcmp(revid, shop->revid) != 0 || strcmp(date, shop->fetch_date) != 0 )
            return shop_refuse(why, why_size, "%s: its header differs from the .rs2's", file);
        return 1;
    }
    shop->origin = origin;
    snprintf(shop->name, sizeof(shop->name), "%s", name);
    snprintf(shop->page_title, sizeof(shop->page_title), "%s", page);
    snprintf(shop->revid, sizeof(shop->revid), "%s", revid);
    snprintf(shop->fetch_date, sizeof(shop->fetch_date), "%s", date);
    return 1;
}

/** "a, b, c" into the owners (declared until a patched note says not). */
static int
shop_parse_owner_list(
    struct Editor_Shop* shop,
    char* list)
{
    char* at = list;

    for( ;; )
    {
        char* comma = strstr(at, ", ");
        if( comma )
            *comma = '\0';
        if( !Editor_ShopNameValid(at) || !Editor_ShopOwnerAdd(shop, at) )
            return 0;
        if( !comma )
            return 1;
        at = comma + 2;
    }
}

static int
shop_owner_index(
    const struct Editor_Shop* shop,
    const char* npc)
{
    for( int i = 0; i < shop->owner_count; i++ )
    {
        if( strcmp(shop->owners[i].npc, npc) == 0 )
            return i;
    }
    return -1;
}

static int
shop_parse_int(
    const char* text,
    int* out)
{
    char* end;
    long value;

    if( !text[0] )
        return 0;
    value = strtol(text, &end, 10);
    if( *end != '\0' || value < -1000000000L || value > 1000000000L )
        return 0;
    *out = (int)value;
    return 1;
}

static int
shop_parse_rs2(
    struct Editor_Shop* shop,
    const char* rs2,
    size_t rs2_length,
    char* why,
    size_t why_size)
{
    struct shop_lines lines = { rs2, rs2_length, 0, 0 };
    char line[1024];
    char key[EDITOR_SHOP_KEY_MAX];
    int too_long;
    int declared = 0;
    int op = 0;

    if( !shop_parse_head(&lines, shop, 0, ".rs2", why, why_size) )
        return 0;

    /* The owner line. */
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) )
        return shop_refuse(why, why_size, ".rs2 line %d: no owner line", lines.number);
    {
        static const char k_gated[] = "// Owner access: quest-gated by hand-authored dialogue. ";
        char* rest;
        if( shop_starts(line, k_gated) )
            rest = line + sizeof(k_gated) - 1;
        else if( shop_starts(line, "// Owner: ") )
        {
            char* stop = strstr(line + 10, ". ");
            if( !stop )
                return shop_refuse(why, why_size, ".rs2 line %d: the owner list does not end in \". \"",
                    lines.number);
            *stop = '\0';
            if( !shop_parse_owner_list(shop, line + 10) )
                return shop_refuse(why, why_size, ".rs2 line %d: an owner is not an npc name, or is listed twice",
                    lines.number);
            rest = stop + 2;
        }
        else
            return shop_refuse(why, why_size, ".rs2 line %d: not \"// Owner: <npc>, ...\"", lines.number);
        if( shop_starts(rest, "Members. ") )
        {
            shop->members = 1;
            rest += 9;
        }
        if( !rest[0] || rest[strlen(rest) - 1] != '.' || strlen(rest) > sizeof(shop->special) )
            return shop_refuse(why, why_size, ".rs2 line %d: the owner line does not end in \"<special>.\"",
                lines.number);
        rest[strlen(rest) - 1] = '\0';
        snprintf(shop->special, sizeof(shop->special), "%s", rest);
    }

    /* The patched note, or the blank line in its place. */
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) )
        return shop_refuse(why, why_size, ".rs2 line %d: missing", lines.number);
    if( line[0] )
    {
        char* note = strstr(line, " already had an [opnpc3] trade stub");
        char* at;
        if( !shop_starts(line, "// ") || !note )
            return shop_refuse(why, why_size, ".rs2 line %d: not the generator's patched-stub note", lines.number);
        *note = '\0';
        at = line + 3;
        for( ;; )
        {
            char* comma = strstr(at, ", ");
            int index;
            if( comma )
                *comma = '\0';
            index = shop_owner_index(shop, at);
            if( index < 0 )
                return shop_refuse(why, why_size, ".rs2 line %d: %s is patched but not an owner", lines.number, at);
            shop->owners[index].declared = 0;
            if( !comma )
                break;
            at = comma + 2;
        }
    }
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) || line[0] )
        return shop_refuse(why, why_size, ".rs2 line %d: not blank", lines.number);

    /* The triggers until the blank before the label; a lone blank when none. */
    for( int expected = 0; expected < shop->owner_count; expected++ )
    {
        if( shop->owners[expected].declared )
            declared++;
    }
    if( declared == 0 )
    {
        if( !shop_next_line(&lines, line, sizeof(line), &too_long) || line[0] )
            return shop_refuse(why, why_size, ".rs2 line %d: not blank (no owner declares a trigger)", lines.number);
    }
    for( int i = 0; i < shop->owner_count; i++ )
    {
        char npc[EDITOR_SHOP_NAME_MAX];
        char label[EDITOR_SHOP_KEY_MAX];
        int this_op;
        char* comma;
        char* close;

        if( !shop->owners[i].declared )
            continue;
        if( !shop_next_line(&lines, line, sizeof(line), &too_long) || !shop_starts(line, "[opnpc") )
            return shop_refuse(why, why_size, ".rs2 line %d: not a [opnpc<N>,<npc>] @<key>_open; trigger",
                lines.number);
        comma = strchr(line, ',');
        close = strstr(line, "] @");
        if( !comma || !close || close < comma || comma - line != 7 || line[6] < '1' || line[6] > '5' )
            return shop_refuse(why, why_size, ".rs2 line %d: not a [opnpc<N>,<npc>] @<key>_open; trigger",
                lines.number);
        this_op = line[6] - '0';
        if( (size_t)(close - comma - 1) >= sizeof(npc) )
            return shop_refuse(why, why_size, ".rs2 line %d: the npc name is too long", lines.number);
        memcpy(npc, comma + 1, (size_t)(close - comma - 1));
        npc[close - comma - 1] = '\0';
        if( strcmp(npc, shop->owners[i].npc) != 0 )
            return shop_refuse(why, why_size, ".rs2 line %d: the trigger is %s's, the owner line's next is %s",
                lines.number, npc, shop->owners[i].npc);
        if( op && this_op != op )
            return shop_refuse(why, why_size, ".rs2 line %d: the owners open the shop on different ops",
                lines.number);
        op = this_op;
        if( !shop_strip_suffix(close + 3, "_open;", label, sizeof(label)) || !Editor_ShopKeyValid(label) )
            return shop_refuse(why, why_size, ".rs2 line %d: the trigger does not call @<key>_open;", lines.number);
        if( shop->key[0] == '\0' )
            snprintf(shop->key, sizeof(shop->key), "%s", label);
        else if( strcmp(label, shop->key) != 0 )
            return shop_refuse(why, why_size, ".rs2 line %d: the owners call different labels", lines.number);
    }
    shop->op = op ? op : 3;

    if( !shop_next_line(&lines, line, sizeof(line), &too_long) || line[0] )
        return shop_refuse(why, why_size, ".rs2 line %d: not blank", lines.number);
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) || !shop_starts(line, "[label,")
        || !shop_strip_suffix(line + 7, "_open]", key, sizeof(key)) || !Editor_ShopKeyValid(key) )
        return shop_refuse(why, why_size, ".rs2 line %d: not [label,<key>_open]", lines.number);
    if( shop->key[0] && strcmp(key, shop->key) != 0 )
        return shop_refuse(why, why_size, ".rs2 line %d: the label is not the one the triggers call", lines.number);
    snprintf(shop->key, sizeof(shop->key), "%s", key);

    /* ~openshop(<inv>, <buy>, <sell>, <delta>, "<title>"); */
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) || !shop_starts(line, "~openshop(") )
        return shop_refuse(why, why_size, ".rs2 line %d: not ~openshop(...)", lines.number);
    {
        char* fields[4];
        char* at = line + 10;
        char title[EDITOR_SHOP_TEXT_MAX];
        char want[EDITOR_SHOP_TEXT_MAX];

        for( int f = 0; f < 4; f++ )
        {
            char* comma = strstr(at, ", ");
            if( !comma )
                return shop_refuse(why, why_size, ".rs2 line %d: ~openshop takes five arguments", lines.number);
            *comma = '\0';
            fields[f] = at;
            at = comma + 2;
        }
        if( !Editor_ShopNameValid(fields[0]) || strlen(fields[0]) >= sizeof(shop->inv) )
            return shop_refuse(why, why_size, ".rs2 line %d: the inv is not a name", lines.number);
        snprintf(shop->inv, sizeof(shop->inv), "%s", fields[0]);
        if( !shop_parse_int(fields[1], &shop->buy) || !shop_parse_int(fields[2], &shop->sell)
            || !shop_parse_int(fields[3], &shop->delta) )
            return shop_refuse(why, why_size, ".rs2 line %d: a multiplier is not a number", lines.number);
        if( at[0] != '"' || !shop_strip_suffix(at + 1, "\");", title, sizeof(title)) )
            return shop_refuse(why, why_size, ".rs2 line %d: the title is not a \"string\"", lines.number);
        snprintf(want, sizeof(want), "%s", shop->name);
        for( char* p = want; *p; p++ )
        {
            if( *p == '"' )
                *p = '\'';
        }
        if( strcmp(title, want) != 0 )
            return shop_refuse(why, why_size, ".rs2 line %d: the title is not the header's name", lines.number);
    }
    if( lines.at != lines.length )
        return shop_refuse(why, why_size, ".rs2 line %d: text after ~openshop", lines.number + 1);
    return 1;
}

static int
shop_parse_inv(
    struct Editor_Shop* shop,
    const char* inv,
    size_t inv_length,
    char* why,
    size_t why_size)
{
    struct shop_lines lines = { inv, inv_length, 0, 0 };
    char line[512];
    char want[EDITOR_SHOP_NAME_MAX + 4];
    int too_long;
    int allstock = 0;
    static const char* const k_fixed[] = { "scope=shared", "restock=yes", "stackall=yes" };

    if( !shop_parse_head(&lines, shop, 1, ".inv", why, why_size) )
        return 0;
    snprintf(want, sizeof(want), "[%s]", shop->inv);
    if( !shop_next_line(&lines, line, sizeof(line), &too_long) || strcmp(line, want) != 0 )
        return shop_refuse(why, why_size, ".inv line %d: not %s, the inv ~openshop names", lines.number, want);
    for( int i = 0; i < 3; i++ )
    {
        if( !shop_next_line(&lines, line, sizeof(line), &too_long) || strcmp(line, k_fixed[i]) != 0 )
            return shop_refuse(why, why_size, ".inv line %d: not %s", lines.number, k_fixed[i]);
    }
    while( shop_next_line(&lines, line, sizeof(line), &too_long) )
    {
        char* equals;
        char* first;
        char* second;
        int n = 0;

        if( !allstock && shop->stock_count == 0 && strcmp(line, "allstock=yes") == 0 )
        {
            allstock = 1;
            continue;
        }
        equals = strchr(line, '=');
        if( !shop_starts(line, "stock") || !equals )
            return shop_refuse(why, why_size, ".inv line %d: not stock<N>=<obj>,<count>,<rate>", lines.number);
        *equals = '\0';
        if( !shop_parse_int(line + 5, &n) || n != shop->stock_count + 1 )
            return shop_refuse(why, why_size, ".inv line %d: stock%d where stock%d comes next", lines.number,
                n, shop->stock_count + 1);
        first = strchr(equals + 1, ',');
        second = first ? strchr(first + 1, ',') : NULL;
        if( !second )
            return shop_refuse(why, why_size, ".inv line %d: not <obj>,<count>,<rate>", lines.number);
        *first = '\0';
        *second = '\0';
        if( !Editor_ShopNameValid(equals + 1) )
            return shop_refuse(why, why_size, ".inv line %d: the obj is not a name", lines.number);
        if( shop->stock_count >= EDITOR_SHOP_STOCK_MAX )
            return shop_refuse(why, why_size, ".inv line %d: more than %d stock rows", lines.number,
                EDITOR_SHOP_STOCK_MAX);
        {
            struct Editor_ShopStock* row = &shop->stock[shop->stock_count];
            snprintf(row->obj, sizeof(row->obj), "%s", equals + 1);
            if( !shop_parse_int(first + 1, &row->count) || !shop_parse_int(second + 1, &row->rate) )
                return shop_refuse(why, why_size, ".inv line %d: the count or the rate is not a number",
                    lines.number);
        }
        shop->stock_count++;
    }
    if( too_long )
        return shop_refuse(why, why_size, ".inv line %d: too long", lines.number);
    if( allstock != Editor_ShopIsGeneral(shop) )
        return shop_refuse(why, why_size, ".inv: allstock=yes is %s but the shop is %sa general store",
            allstock ? "there" : "missing", allstock ? "not " : "");
    if( inv_length == 0 || inv[inv_length - 1] != '\n' )
        return shop_refuse(why, why_size, ".inv: no final newline");
    return 1;
}

int
Editor_ShopParse(
    struct Editor_Shop* shop,
    const char* rs2,
    size_t rs2_length,
    const char* inv,
    size_t inv_length,
    char* why,
    size_t why_size)
{
    assert(shop);
    assert(rs2 || rs2_length == 0);
    assert(inv || inv_length == 0);
    assert(why);
    assert(why_size > 0);

    memset(shop, 0, sizeof(*shop));
    why[0] = '\0';
    if( !shop_parse_rs2(shop, rs2 ? rs2 : "", rs2_length, why, why_size) )
        return 0;
    if( !shop_parse_inv(shop, inv ? inv : "", inv_length, why, why_size) )
        return 0;
    return 1;
}

/* ---- checking ---------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static void
shop_issue(
    char (*out)[EDITOR_SHOP_TEXT_MAX],
    int max,
    int* count,
    const char* format,
    ...)
{
    va_list args;

    if( *count >= max )
        return;
    va_start(args, format);
    vsnprintf(out[*count], EDITOR_SHOP_TEXT_MAX, format, args);
    va_end(args);
    (*count)++;
}

int
Editor_ShopCheck(
    const struct Editor_Shop* shop,
    char (*out)[EDITOR_SHOP_TEXT_MAX],
    int max)
{
    int count = 0;

    assert(shop);
    assert(out || max == 0);

    if( !Editor_ShopKeyValid(shop->key) )
        shop_issue(out, max, &count, "The shop key '%s' is not 1-47 of a-z, 0-9 and _.", shop->key);
    if( shop->owner_count == 0 )
        shop_issue(out, max, &count, "No owner npc: nobody opens the shop (pick one).");
    for( int i = 0; i < shop->owner_count; i++ )
    {
        if( !Editor_ShopNameValid(shop->owners[i].npc) )
            shop_issue(out, max, &count, "Owner '%s' is not an npc name.", shop->owners[i].npc);
    }
    if( shop->op < 1 || shop->op > 5 )
        shop_issue(out, max, &count, "op%d is not an npc op (1 to 5).", shop->op);
    if( !Editor_ShopNameValid(shop->inv) )
        shop_issue(out, max, &count, "The inv '%s' is not a name.", shop->inv);
    if( shop->sell < 1 || shop->sell > 100000 )
        shop_issue(out, max, &count, "Sell %d per mille is outside 1 to 100000.", shop->sell);
    if( shop->buy < 0 || shop->buy > 100000 )
        shop_issue(out, max, &count, "Buy %d per mille is outside 0 to 100000.", shop->buy);
    if( shop->delta < 0 || shop->delta > 1000 )
        shop_issue(out, max, &count, "Delta %d per mille is outside 0 to 1000.", shop->delta);
    if( shop->buy > shop->sell )
        shop_issue(out, max, &count, "Buy %d is above sell %d: a player could sell back for profit.", shop->buy,
            shop->sell);
    if( !shop->name[0] || strchr(shop->name, '\n') )
        shop_issue(out, max, &count, "The shop needs a one-line title.");
    if( shop->stock_count == 0 )
        shop_issue(out, max, &count, "No stock: add a row (+ stock) or read the wiki catalogue.");
    for( int i = 0; i < shop->stock_count; i++ )
    {
        const struct Editor_ShopStock* row = &shop->stock[i];
        if( !Editor_ShopNameValid(row->obj) )
            shop_issue(out, max, &count, "stock%d: '%s' is not an obj name.", i + 1, row->obj);
        if( row->count < 1 )
            shop_issue(out, max, &count, "stock%d (%s): count %d; a shop line holds at least 1.", i + 1, row->obj,
                row->count);
        if( row->rate < 1 )
            shop_issue(out, max, &count, "stock%d (%s): rate %d; the restock tick needs at least 1.", i + 1,
                row->obj, row->rate);
        for( int j = 0; j < i; j++ )
        {
            if( strcmp(shop->stock[j].obj, row->obj) == 0 )
                shop_issue(out, max, &count, "stock%d repeats %s (stock%d).", i + 1, row->obj, j + 1);
        }
    }
    return count;
}

/* ---- the wiki catalogue -------------------------------------------------------- */

/** One CSV record's fields (RFC 4180) into `fields` as offsets into a
 *  scratch copy; returns the field count, the record's end in *inout_at. */
static int
shop_csv_record(
    const char* csv,
    size_t length,
    size_t* inout_at,
    char* scratch,
    size_t scratch_size,
    const char** fields,
    int max_fields)
{
    size_t at = *inout_at;
    size_t used = 0;
    int count = 0;
    int quoted = 0;

    if( max_fields <= 0 || scratch_size == 0 )
        return 0;
    fields[count++] = scratch;
    while( at < length )
    {
        char const c = csv[at];
        if( quoted )
        {
            if( c == '"' && at + 1 < length && csv[at + 1] == '"' )
            {
                if( used + 1 < scratch_size )
                    scratch[used++] = '"';
                at += 2;
                continue;
            }
            if( c == '"' )
            {
                quoted = 0;
                at++;
                continue;
            }
            if( used + 1 < scratch_size )
                scratch[used++] = c;
            at++;
            continue;
        }
        if( c == '"' )
        {
            quoted = 1;
            at++;
            continue;
        }
        if( c == ',' )
        {
            if( used + 1 < scratch_size )
                scratch[used++] = '\0';
            if( count < max_fields )
                fields[count++] = scratch + used;
            at++;
            continue;
        }
        if( c == '\r' )
        {
            at++;
            continue;
        }
        if( c == '\n' )
        {
            at++;
            break;
        }
        if( used + 1 < scratch_size )
            scratch[used++] = c;
        at++;
    }
    scratch[used < scratch_size ? used : scratch_size - 1] = '\0';
    *inout_at = at;
    return count;
}

int
Editor_ShopWikiStock(
    const char* csv,
    size_t length,
    const char* key,
    struct Editor_ShopStock* out,
    int max,
    int* out_skipped)
{
    enum
    {
        CSV_FIELDS = 32
    };
    char scratch[2048];
    const char* fields[CSV_FIELDS];
    size_t at = 0;
    int field_count;
    int col_key = -1;
    int col_obj = -1;
    int col_stock = -1;
    int col_restock = -1;
    int col_review = -1;
    int count = 0;

    assert(csv || length == 0);
    assert(key);
    assert(out || max == 0);
    assert(out_skipped);

    *out_skipped = 0;
    field_count = shop_csv_record(csv, length, &at, scratch, sizeof(scratch), fields, CSV_FIELDS);
    for( int f = 0; f < field_count; f++ )
    {
        if( strcmp(fields[f], "shop_key") == 0 )
            col_key = f;
        else if( strcmp(fields[f], "obj_gameval") == 0 )
            col_obj = f;
        else if( strcmp(fields[f], "stock") == 0 )
            col_stock = f;
        else if( strcmp(fields[f], "restock") == 0 )
            col_restock = f;
        else if( strcmp(fields[f], "review") == 0 )
            col_review = f;
    }
    if( col_key < 0 || col_obj < 0 || col_stock < 0 || col_restock < 0 )
        return -1;
    while( at < length )
    {
        int number;
        int rate;
        field_count = shop_csv_record(csv, length, &at, scratch, sizeof(scratch), fields, CSV_FIELDS);
        if( field_count <= col_key || strcmp(fields[col_key], key) != 0 )
            continue;
        if( field_count <= col_restock || field_count <= col_obj || field_count <= col_stock || !fields[col_obj][0]
            || (col_review >= 0 && field_count > col_review && fields[col_review][0])
            || !Editor_ShopNameValid(fields[col_obj]) || !shop_parse_int(fields[col_stock], &number)
            || !shop_parse_int(fields[col_restock], &rate) )
        {
            (*out_skipped)++;
            continue;
        }
        if( count >= max )
        {
            (*out_skipped)++;
            continue;
        }
        snprintf(out[count].obj, sizeof(out[count].obj), "%s", fields[col_obj]);
        out[count].count = number;
        out[count].rate = rate;
        count++;
    }
    return count;
}
