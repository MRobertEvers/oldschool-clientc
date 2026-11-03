/*
 * Accounts: the lane's player saves managed from the editor
 * (torirs_maped_accounts.h has the wire and the rules). Compiled into
 * torirs_maped.c after torirs_maped_live.c: it reads the jobs' player list
 * (who is online) and sends cheats on the control link.
 *
 * Three layers, bottom up:
 *   - pure pieces: the stat table, the save codec, the quest arms parser
 *     (exported, tested by accounts_test.c);
 *   - the registry and the per-lane state: `<live>/accounts.ini`, the
 *     snapshots a `::checkpoint` writes, the login watch;
 *   - the request handlers and the service pass.
 */

#include "torirs_maped_accounts.h"
#include "torirs_maped_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define acct_mkdir(path) _mkdir(path)
#else
#define acct_mkdir(path) mkdir(path, 0755)
#endif

/* ------------------------------------------------------------------ */
/* Pure pieces                                                         */
/* ------------------------------------------------------------------ */

int
ToriRSMapEd_AcctStatFind(const char* name)
{
    char* end = NULL;
    long numeric;

    assert(name);
    if( !name[0] )
        return -1;
    numeric = strtol(name, &end, 10);
    if( end && end != name && *end == '\0' )
        return numeric >= 0 && numeric < TORIRSMAPED_ACCT_STATS ? (int)numeric : -1;
    for( int i = 0; i < TORIRSMAPED_ACCT_STATS; i++ )
        if( strcasecmp(ToriRSMapEd_AcctStatName(i), name) == 0 )
            return i;
    return -1;
}

static int g_acct_xp_table[99];
static int g_acct_xp_table_built;

static void
acct_xp_table(void)
{
    long long accumulated = 0;

    if( g_acct_xp_table_built )
        return;
    for( int i = 0; i < 99; i++ )
    {
        int const level = i + 1;
        accumulated += (long long)floor((double)level + pow(2.0, (double)level / 7.0) * 300.0);
        g_acct_xp_table[i] = (int)(accumulated / 4);
    }
    g_acct_xp_table_built = 1;
}

int
ToriRSMapEd_AcctXpForLevel(int level)
{
    if( level <= 1 )
        return 0;
    if( level > 99 )
        level = 99;
    acct_xp_table();
    return g_acct_xp_table[level - 2];
}

int
ToriRSMapEd_AcctLevelForXp(int xp)
{
    acct_xp_table();
    /* table[i] is where level i + 2 starts; 99 is the ceiling. */
    for( int i = 97; i >= 0; i-- )
        if( xp >= g_acct_xp_table[i] )
            return i + 2;
    return 1;
}

void
ToriRSMapEd_AcctFileName(
    const char* name,
    char* out,
    size_t out_size)
{
    size_t written = 0;

    assert(name);
    assert(out);
    assert(out_size > 0);
    for( const char* scan = name; *scan && written + 1 < out_size; scan++ )
    {
        unsigned char const ch = (unsigned char)*scan;
        if( isalnum(ch) )
            out[written++] = (char)tolower(ch);
        else if( ch == '_' || ch == '-' || ch == ' ' )
            out[written++] = '_';
    }
    out[written] = '\0';
}

const char*
ToriRSMapEd_AcctCredentialsProblem(
    const char* name,
    const char* password)
{
    size_t const name_len = strlen(name);
    size_t const pass_len = strlen(password);
    int letters = 0;
    int digits = 0;
    char lower_name[TORIRSMAPED_ACCT_NAME_MAX + 1];
    char lower_pass[64];

    assert(name);
    assert(password);
    if( name_len < 1 || name_len > TORIRSMAPED_ACCT_NAME_MAX )
        return "names are 1 to 12 characters long";
    for( size_t i = 0; i < name_len; i++ )
    {
        unsigned char const ch = (unsigned char)name[i];
        if( !isalnum(ch) && ch != '_' && ch != ' ' && ch != '-' )
            return "a name holds letters, digits, spaces, '-' and '_' only";
    }
    if( pass_len < 8 || pass_len > 20 )
        return "passwords are 8 to 20 characters long";
    for( size_t i = 0; i < pass_len; i++ )
    {
        unsigned char const ch = (unsigned char)password[i];
        if( ch < 0x20 || ch > 0x7e )
            return "a password holds printable ASCII only";
        letters += isalpha(ch) != 0;
        digits += isdigit(ch) != 0;
    }
    if( !letters || !digits )
        return "a password needs a letter and a digit";
    for( size_t i = 0; i <= name_len; i++ )
        lower_name[i] = (char)tolower((unsigned char)name[i]);
    for( size_t i = 0; i <= pass_len && i < sizeof(lower_pass) - 1; i++ )
        lower_pass[i] = (char)tolower((unsigned char)password[i]);
    lower_pass[sizeof(lower_pass) - 1] = '\0';
    if( strstr(lower_pass, lower_name) )
        return "a password must not contain the name";
    return NULL;
}

/* ---- the save codec ---------------------------------------------------------- */

void
ToriRSMapEd_AcctSaveInit(struct ToriRSMapEdAcctSave* save)
{
    assert(save);
    memset(save, 0, sizeof(*save));
    save->final_newline = 1;
}

void
ToriRSMapEd_AcctSaveFree(struct ToriRSMapEdAcctSave* save)
{
    if( !save )
        return;
    for( int i = 0; i < save->count; i++ )
        free(save->lines[i]);
    free(save->lines);
    memset(save, 0, sizeof(*save));
    save->final_newline = 1;
}

static char*
acct_strdup_n(
    const char* text,
    size_t length)
{
    char* copy = malloc(length + 1);
    assert(copy);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void
acct_save_insert(
    struct ToriRSMapEdAcctSave* save,
    int at,
    const char* text)
{
    assert(at >= 0);
    assert(at <= save->count);
    if( save->count == save->capacity )
    {
        save->capacity = save->capacity ? save->capacity * 2 : 64;
        save->lines = realloc(save->lines, (size_t)save->capacity * sizeof(*save->lines));
        assert(save->lines);
    }
    memmove(&save->lines[at + 1], &save->lines[at], (size_t)(save->count - at) * sizeof(*save->lines));
    save->lines[at] = acct_strdup_n(text, strlen(text));
    save->count++;
}

static void
acct_save_delete(
    struct ToriRSMapEdAcctSave* save,
    int at)
{
    assert(at >= 0);
    assert(at < save->count);
    free(save->lines[at]);
    memmove(&save->lines[at], &save->lines[at + 1], (size_t)(save->count - at - 1) * sizeof(*save->lines));
    save->count--;
}

void
ToriRSMapEd_AcctSaveParse(
    struct ToriRSMapEdAcctSave* save,
    const char* text,
    size_t length)
{
    size_t at = 0;

    assert(save);
    assert(text || length == 0);
    ToriRSMapEd_AcctSaveFree(save);
    save->crlf = 0;
    save->final_newline = 1;
    while( at < length )
    {
        size_t end = at;
        size_t line_end;
        while( end < length && text[end] != '\n' )
            end++;
        line_end = end;
        if( line_end > at && text[line_end - 1] == '\r' )
        {
            line_end--;
            save->crlf = 1;
        }
        if( save->count == save->capacity )
        {
            save->capacity = save->capacity ? save->capacity * 2 : 64;
            save->lines = realloc(save->lines, (size_t)save->capacity * sizeof(*save->lines));
            assert(save->lines);
        }
        save->lines[save->count++] = acct_strdup_n(text + at, line_end - at);
        if( end >= length )
        {
            save->final_newline = 0;
            break;
        }
        at = end + 1;
    }
}

size_t
ToriRSMapEd_AcctSaveEmit(
    const struct ToriRSMapEdAcctSave* save,
    char* out,
    size_t capacity)
{
    size_t total = 0;
    const char* const newline = save->crlf ? "\r\n" : "\n";
    size_t const newline_len = save->crlf ? 2 : 1;

    assert(save);
    assert(out || capacity == 0);
    for( int i = 0; i < save->count; i++ )
    {
        size_t const len = strlen(save->lines[i]);
        int const last = i == save->count - 1;
        if( out && total + len < capacity )
            memcpy(out + total, save->lines[i], len);
        total += len;
        if( !last || save->final_newline )
        {
            if( out && total + newline_len < capacity )
                memcpy(out + total, newline, newline_len);
            total += newline_len;
        }
    }
    if( out && capacity > 0 )
        out[total < capacity ? total : capacity - 1] = '\0';
    return total;
}

/** The trimmed span of `text`. */
static void
acct_trim(
    const char* text,
    size_t length,
    const char** out_start,
    size_t* out_length)
{
    while( length > 0 && isspace((unsigned char)text[0]) )
    {
        text++;
        length--;
    }
    while( length > 0 && isspace((unsigned char)text[length - 1]) )
        length--;
    *out_start = text;
    *out_length = length;
}

/** `[name]`: 1 with the trimmed name. */
static int
acct_line_section(
    const char* line,
    char* out,
    size_t out_size)
{
    const char* start;
    size_t length;
    const char* close;

    acct_trim(line, strlen(line), &start, &length);
    if( length < 2 || start[0] != '[' )
        return 0;
    close = memchr(start, ']', length);
    if( !close )
        return 0;
    acct_trim(start + 1, (size_t)(close - start - 1), &start, &length);
    if( length + 1 > out_size )
        length = out_size - 1;
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

/** `key = value`: 1 with both trimmed. Comments (';', '#', "//"), blank
 *  lines and headers are 0. */
static int
acct_line_keyval(
    const char* line,
    char* out_key,
    size_t key_size,
    char* out_value,
    size_t value_size)
{
    const char* start;
    size_t length;
    const char* eq;
    const char* k;
    size_t k_len;
    const char* v;
    size_t v_len;

    acct_trim(line, strlen(line), &start, &length);
    if( length == 0 || start[0] == ';' || start[0] == '#' || start[0] == '[' )
        return 0;
    if( length >= 2 && start[0] == '/' && start[1] == '/' )
        return 0;
    eq = memchr(start, '=', length);
    if( !eq )
        return 0;
    acct_trim(start, (size_t)(eq - start), &k, &k_len);
    acct_trim(eq + 1, length - (size_t)(eq - start) - 1, &v, &v_len);
    if( k_len == 0 )
        return 0;
    if( k_len + 1 > key_size )
        k_len = key_size - 1;
    if( v_len + 1 > value_size )
        v_len = value_size - 1;
    memcpy(out_key, k, k_len);
    out_key[k_len] = '\0';
    memcpy(out_value, v, v_len);
    out_value[v_len] = '\0';
    return 1;
}

/** The next block of `[section]` at or after line `from`: *out_start the
 *  first line after its header, *out_end one past its last. Returns the
 *  header's line, or -1 when there is none. A section may be stated more
 *  than once (the server's reader folds them; a hand edit may append a
 *  second [inv]): callers walk every block. */
static int
acct_save_section_from(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    int from,
    int* out_start,
    int* out_end)
{
    char name[128];
    int header = -1;

    for( int i = from; i < save->count; i++ )
    {
        if( !acct_line_section(save->lines[i], name, sizeof(name)) )
            continue;
        if( header >= 0 )
        {
            *out_end = i;
            return header;
        }
        if( strcmp(name, section) == 0 )
        {
            header = i;
            *out_start = i + 1;
        }
    }
    if( header >= 0 )
        *out_end = save->count;
    return header;
}

/** The LAST block of `[section]` (where a new key goes); -1 when none. */
static int
acct_save_section(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    int* out_start,
    int* out_end)
{
    int header = -1;
    int from = 0;
    int start;
    int end;
    int h;

    while( (h = acct_save_section_from(save, section, from, &start, &end)) >= 0 )
    {
        header = h;
        *out_start = start;
        *out_end = end;
        from = end;
    }
    return header;
}

/** The line of `key` in any block of `[section]`, or -1. */
static int
acct_save_find_key(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key)
{
    char k[128];
    char v[512];
    int from = 0;
    int start;
    int end;

    while( acct_save_section_from(save, section, from, &start, &end) >= 0 )
    {
        for( int i = start; i < end; i++ )
            if( acct_line_keyval(save->lines[i], k, sizeof(k), v, sizeof(v)) && strcmp(k, key) == 0 )
                return i;
        from = end;
    }
    return -1;
}

int
ToriRSMapEd_AcctSaveGet(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key,
    char* out,
    size_t out_size)
{
    int line;
    char k[128];

    assert(save);
    assert(section);
    assert(key);
    assert(out);
    line = acct_save_find_key(save, section, key);
    if( line < 0 )
        return 0;
    return acct_line_keyval(save->lines[line], k, sizeof(k), out, out_size);
}

int
ToriRSMapEd_AcctSaveSet(
    struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key,
    const char* value)
{
    int start = 0;
    int end = 0;
    int header;
    int line;
    char text[768];

    assert(save);
    assert(section);
    assert(key);
    assert(value);
    snprintf(text, sizeof(text), "%s = %s", key, value);
    line = acct_save_find_key(save, section, key);
    if( line >= 0 )
    {
        if( strcmp(save->lines[line], text) == 0 )
            return 0;
        free(save->lines[line]);
        save->lines[line] = acct_strdup_n(text, strlen(text));
        return 1;
    }
    header = acct_save_section(save, section, &start, &end);
    if( header < 0 )
    {
        char head[136];
        /* A blank line before a new section, as the server writes them. */
        if( save->count > 0 && save->lines[save->count - 1][0] != '\0' )
            acct_save_insert(save, save->count, "");
        snprintf(head, sizeof(head), "[%s]", section);
        acct_save_insert(save, save->count, head);
        acct_save_insert(save, save->count, text);
        save->final_newline = 1;
        return 1;
    }
    /* Before the section's trailing blank lines. */
    while( end > start && save->lines[end - 1][0] == '\0' )
        end--;
    acct_save_insert(save, end, text);
    if( end == save->count - 1 )
        save->final_newline = 1;
    return 1;
}

int
ToriRSMapEd_AcctSaveRemove(
    struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key)
{
    int line;

    assert(save);
    assert(section);
    assert(key);
    line = acct_save_find_key(save, section, key);
    if( line < 0 )
        return 0;
    acct_save_delete(save, line);
    return 1;
}

int
ToriRSMapEd_AcctSaveNth(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    int index,
    char* out_key,
    size_t key_size,
    char* out_value,
    size_t value_size)
{
    int from = 0;
    int start;
    int end;

    assert(save);
    assert(section);
    assert(index >= 0);
    assert(out_key);
    assert(out_value);
    while( acct_save_section_from(save, section, from, &start, &end) >= 0 )
    {
        for( int i = start; i < end; i++ )
            if( acct_line_keyval(save->lines[i], out_key, key_size, out_value, value_size) && index-- == 0 )
                return 1;
        from = end;
    }
    return 0;
}

/** The whole file, NUL-terminated; NULL when it cannot be read. */
static char*
acct_read_file(
    const char* path,
    size_t* out_length)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    *out_length = 0;
    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 )
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = malloc((size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_length = (size_t)size;
    return data;
}

int
ToriRSMapEd_AcctSaveLoad(
    struct ToriRSMapEdAcctSave* save,
    const char* path)
{
    size_t length;
    char* text;

    assert(save);
    assert(path);
    text = acct_read_file(path, &length);
    if( !text )
    {
        ToriRSMapEd_AcctSaveFree(save);
        return 0;
    }
    ToriRSMapEd_AcctSaveParse(save, text, length);
    free(text);
    return 1;
}

int
ToriRSMapEd_AcctSaveWrite(
    const struct ToriRSMapEdAcctSave* save,
    const char* path)
{
    size_t length;
    char* text;
    char temp[1200];
    FILE* f;

    assert(save);
    assert(path);
    length = ToriRSMapEd_AcctSaveEmit(save, NULL, 0);
    text = malloc(length + 1);
    assert(text);
    ToriRSMapEd_AcctSaveEmit(save, text, length + 1);
    snprintf(temp, sizeof(temp), "%s.tmp%ld", path, (long)getpid());
    f = fopen(temp, "wb");
    if( !f )
    {
        free(text);
        return 0;
    }
    if( fwrite(text, 1, length, f) != length || fclose(f) != 0 )
    {
        free(text);
        unlink(temp);
        return 0;
    }
    free(text);
    if( rename(temp, path) != 0 )
    {
        unlink(temp);
        return 0;
    }
    return 1;
}

/* ---- the quest index ---------------------------------------------------------- */

/** `%varp29_cookquest` -> (1, 29); `%varb3293_peng_quest` -> (2, 3293). */
static int
acct_var_token(
    const char* token,
    int* out_kind,
    int* out_id)
{
    const char* digits;
    char* end = NULL;
    long id;

    if( strncmp(token, "varp", 4) == 0 )
    {
        *out_kind = 1;
        digits = token + 4;
    }
    else if( strncmp(token, "varb", 4) == 0 )
    {
        *out_kind = 2;
        digits = token + 4;
    }
    else
        return 0;
    id = strtol(digits, &end, 10);
    if( !end || end == digits || id < 0 )
        return 0;
    *out_id = (int)id;
    return 1;
}

/** Copy the identifier at `at` ([A-Za-z0-9_]) into `out`; the length. */
static size_t
acct_ident(
    const char* at,
    char* out,
    size_t out_size)
{
    size_t n = 0;
    while( at[n] && (isalnum((unsigned char)at[n]) || at[n] == '_') )
        n++;
    if( n + 1 > out_size )
        n = out_size - 1;
    memcpy(out, at, n);
    out[n] = '\0';
    return n;
}

int
ToriRSMapEd_AcctQuestArms(
    const char* text,
    size_t length,
    struct ToriRSMapEdAcctQuest* out,
    int max)
{
    size_t at = 0;
    int count = 0;
    struct ToriRSMapEdAcctQuest* current = NULL;
    int need_test = 0;

    assert(text || length == 0);
    assert(out || max == 0);
    while( at < length )
    {
        size_t end = at;
        const char* line;
        const char* arm;
        while( end < length && text[end] != '\n' )
            end++;
        line = text + at;
        {
            char buf[512];
            size_t n = end - at;
            if( n >= sizeof(buf) )
                n = sizeof(buf) - 1;
            memcpy(buf, line, n);
            buf[n] = '\0';
            arm = strstr(buf, "if ($row = ");
            if( arm )
            {
                if( count < max )
                {
                    current = &out[count++];
                    memset(current, 0, sizeof(*current));
                    acct_ident(arm + 11, current->dbrow, sizeof(current->dbrow));
                    current->complete_value = -1;
                    need_test = 1;
                }
                else
                    current = NULL;
            }
            else if( current && need_test )
            {
                const char* caret = strchr(buf, '^');
                const char* percent = strchr(buf, '%');
                if( caret && (strstr(buf, ">=") || strstr(buf, "= ^")) && strstr(buf, "if (") )
                {
                    acct_ident(caret + 1, current->constant, sizeof(current->constant));
                    if( percent )
                    {
                        char token[96];
                        acct_ident(percent + 1, token, sizeof(token));
                        if( !acct_var_token(token, &current->var_kind, &current->var_id) )
                            current->var_kind = 0;
                    }
                    need_test = 0;
                }
            }
        }
        at = end + 1;
    }
    return count;
}

int
ToriRSMapEd_AcctQuestRows(
    const char* text,
    size_t length,
    struct ToriRSMapEdAcctQuest* out,
    int max)
{
    size_t at = 0;
    int count = 0;
    char block[64] = "";
    int in_quest = 0;
    char display[64] = "";

    assert(text || length == 0);
    assert(out || max == 0);
    while( at <= length )
    {
        size_t end = at;
        char buf[512];
        size_t n;
        while( end < length && text[end] != '\n' )
            end++;
        n = end - at;
        if( n >= sizeof(buf) )
            n = sizeof(buf) - 1;
        memcpy(buf, text + at, n);
        buf[n] = '\0';
        if( n > 0 && buf[n - 1] == '\r' )
            buf[--n] = '\0';
        if( buf[0] == '[' || end >= length )
        {
            if( in_quest && block[0] && count < max )
            {
                struct ToriRSMapEdAcctQuest* row = &out[count++];
                memset(row, 0, sizeof(*row));
                snprintf(row->dbrow, sizeof(row->dbrow), "%s", block);
                snprintf(row->display, sizeof(row->display), "%s", display[0] ? display : block);
                row->complete_value = -1;
            }
            in_quest = 0;
            display[0] = '\0';
            block[0] = '\0';
            if( buf[0] == '[' )
            {
                char* close = strchr(buf, ']');
                if( close )
                {
                    *close = '\0';
                    snprintf(block, sizeof(block), "%s", buf + 1);
                }
            }
        }
        else if( strcmp(buf, "table=quest") == 0 )
            in_quest = 1;
        else if( strncmp(buf, "data=displayname,", 17) == 0 )
            snprintf(display, sizeof(display), "%s", buf + 17);
        if( end >= length )
            break;
        at = end + 1;
    }
    return count;
}

int
ToriRSMapEd_AcctConstantFind(
    const char* text,
    size_t length,
    const char* name,
    int* out_value)
{
    size_t at = 0;
    size_t const name_len = strlen(name);

    assert(text || length == 0);
    assert(name);
    assert(out_value);
    while( at < length )
    {
        size_t end = at;
        const char* line = text + at;
        while( end < length && text[end] != '\n' )
            end++;
        if( end - at > name_len + 1 && line[0] == '^' && strncmp(line + 1, name, name_len) == 0
            && !(isalnum((unsigned char)line[1 + name_len]) || line[1 + name_len] == '_') )
        {
            const char* eq = memchr(line, '=', end - at);
            if( eq )
            {
                char* stop = NULL;
                long value = strtol(eq + 1, &stop, 10);
                if( stop && stop != eq + 1 )
                {
                    *out_value = (int)value;
                    return 1;
                }
            }
        }
        at = end + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The per-server state                                                */
/* ------------------------------------------------------------------ */

#define ACCT_PATH_MAX (TORIRSMAPED_LANE_PATH_MAX + 256)
#define ACCT_SNAPSHOTS_MAX 32
#define ACCT_SEEN_MAX 32
#define ACCT_QUESTS_MAX 320
#define ACCT_NAMES_MAX 128
/** Seconds after a login before its queued cheats go: the server's login
 *  settles its scene first. */
#define ACCT_FLUSH_DELAY_S 3
/** Seconds a `::checkpoint` may take to land before the view falls back. */
#define ACCT_REFRESH_TIMEOUT_S 4
/** The first checkpoint leg this module uses (the quest harness counts from 1). */
#define ACCT_LEG_BASE 90001

/** One account's server checkpoint, asked for by `refresh`. */
struct acct_snapshot
{
    char file[64];
    int leg;
    /** A checkpoint is out; `sent` says since when. */
    int pending;
    time_t sent;
    /** The checkpoint file holds a snapshot the view may show. */
    int have;
};

/** One player the attached server lists, and since when this module saw it. */
struct acct_seen
{
    char account[64];
    uint32_t slot;
    uint32_t generation;
    time_t since;
    int flushed;
};

struct acct_state
{
    struct ToriRSMapEd* maped;
    int registry_loaded;
    char registry_path[ACCT_PATH_MAX];
    struct ToriRSMapEdAcctSave registry;
    struct acct_snapshot snapshots[ACCT_SNAPSHOTS_MAX];
    int snapshot_count;
    struct acct_seen seen[ACCT_SEEN_MAX];
    int seen_count;
    /** The quest index, built on the first request that needs it. */
    int quests_loaded;
    struct ToriRSMapEdAcctQuest quests[ACCT_QUESTS_MAX];
    int quest_count;
};

static struct acct_state* g_acct_states[4];

static struct acct_state*
acct_state_for(struct ToriRSMapEd* maped)
{
    struct acct_state* state;

    assert(maped);
    for( int i = 0; i < 4; i++ )
        if( g_acct_states[i] && g_acct_states[i]->maped == maped )
            return g_acct_states[i];
    state = calloc(1, sizeof(*state));
    assert(state);
    state->maped = maped;
    ToriRSMapEd_AcctSaveInit(&state->registry);
    for( int i = 0; i < 4; i++ )
        if( !g_acct_states[i] )
        {
            g_acct_states[i] = state;
            return state;
        }
    /* Four servers in one process is the embed test's ceiling. */
    assert(!"more accounts states than servers");
    return state;
}

/** The server closed: drop its state (ToriRSMapEd_Close). */
static void
acct_state_drop(struct ToriRSMapEd* maped)
{
    for( int i = 0; i < 4; i++ )
        if( g_acct_states[i] && g_acct_states[i]->maped == maped )
        {
            ToriRSMapEd_AcctSaveFree(&g_acct_states[i]->registry);
            free(g_acct_states[i]);
            g_acct_states[i] = NULL;
        }
}

/** The lane's live dir, resolved; 0 when no lane is bound. */
static int
acct_live_dir(
    struct ToriRSMapEd* maped,
    char* out,
    size_t out_size)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    assert(jobs);
    if( !jobs->have_lane || !jobs->lane.live_dir[0] )
    {
        out[0] = '\0';
        return 0;
    }
    build_resolve(jobs->lane.live_dir, out, out_size);
    return 1;
}

static void
acct_mkdirs(const char* path)
{
    char buf[ACCT_PATH_MAX];
    size_t used = 0;

    for( const char* scan = path; *scan && used + 1 < sizeof(buf); scan++ )
    {
        if( *scan == '/' && used > 0 )
        {
            buf[used] = '\0';
            (void)acct_mkdir(buf);
        }
        buf[used++] = *scan;
    }
    buf[used] = '\0';
    if( used > 0 )
        (void)acct_mkdir(buf);
}

static void
acct_save_path(
    const char* live,
    const char* file,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/saves/%s.ini", live, file);
}

static void
acct_record_path(
    const char* live,
    const char* file,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/saves/accounts/%s.ini", live, file);
}

/* ---- the registry ------------------------------------------------------------- */

static void
acct_registry_load(
    struct acct_state* state,
    const char* live)
{
    snprintf(state->registry_path, sizeof(state->registry_path), "%s/accounts.ini", live);
    if( !ToriRSMapEd_AcctSaveLoad(&state->registry, state->registry_path) )
    {
        static const char header[] =
            "; ToriRSMapEd accounts (torirs_maped_accounts.h): the lane's play account,\n"
            "; the passwords of accounts made in the editor, and the cheats queued for\n"
            "; accounts that were offline when they were asked (flushed at their login).\n";
        ToriRSMapEd_AcctSaveParse(&state->registry, header, sizeof(header) - 1);
    }
    state->registry_loaded = 1;
}

static int
acct_registry_write(struct acct_state* state)
{
    char dir[ACCT_PATH_MAX];
    char* slash;

    snprintf(dir, sizeof(dir), "%s", state->registry_path);
    slash = strrchr(dir, '/');
    if( slash )
    {
        *slash = '\0';
        acct_mkdirs(dir);
    }
    if( !ToriRSMapEd_AcctSaveWrite(&state->registry, state->registry_path) )
    {
        TORIRS_LOG("torirsmaped: accounts: cannot write %s: %s\n", state->registry_path, strerror(errno));
        return 0;
    }
    return 1;
}

/** The password the editor knows for `file`: made here, or the lane's
 *  default play account's. */
static int
acct_password_for(
    struct acct_state* state,
    const char* file,
    char* out,
    size_t out_size)
{
    struct ToriRSMapEdJobs* jobs = state->maped->jobs;
    struct ToriRSMapEdLane plain;
    char user[16];
    char pass[24];

    if( ToriRSMapEd_AcctSaveGet(&state->registry, "passwords", file, out, out_size) && out[0] )
        return 1;
    if( !jobs->have_lane )
        return 0;
    plain = jobs->lane;
    plain.play_user[0] = '\0';
    plain.play_pass[0] = '\0';
    ToriRSMapEd_LanePlayAccount(&plain, user, sizeof(user), pass, sizeof(pass));
    if( live_name_equal(user, file) )
    {
        snprintf(out, out_size, "%s", pass);
        return 1;
    }
    return 0;
}

/** The queued cheat lines of `file`, in order. */
static int
acct_queue_count(
    struct acct_state* state,
    const char* file)
{
    char key[128];
    char value[512];
    int count = 0;
    size_t const file_len = strlen(file);

    for( int i = 0; ToriRSMapEd_AcctSaveNth(&state->registry, "queue", i, key, sizeof(key), value, sizeof(value)); i++ )
        if( strncmp(key, file, file_len) == 0 && key[file_len] == '.' )
            count++;
    return count;
}

static int
acct_queue_nth(
    struct acct_state* state,
    const char* file,
    int index,
    char* out_key,
    size_t key_size,
    char* out_line,
    size_t line_size)
{
    size_t const file_len = strlen(file);

    for( int i = 0; ToriRSMapEd_AcctSaveNth(&state->registry, "queue", i, out_key, key_size, out_line, line_size); i++ )
        if( strncmp(out_key, file, file_len) == 0 && out_key[file_len] == '.' && index-- == 0 )
            return 1;
    return 0;
}

static int
acct_queue_add(
    struct acct_state* state,
    const char* file,
    const char* line)
{
    char key[128];
    char value[512];
    int next = 0;
    size_t const file_len = strlen(file);

    if( acct_queue_count(state, file) >= TORIRSMAPED_ACCT_QUEUE_MAX )
        return 0;
    for( int i = 0; ToriRSMapEd_AcctSaveNth(&state->registry, "queue", i, key, sizeof(key), value, sizeof(value)); i++ )
        if( strncmp(key, file, file_len) == 0 && key[file_len] == '.' )
        {
            int n = atoi(key + file_len + 1);
            if( n >= next )
                next = n + 1;
        }
    snprintf(key, sizeof(key), "%s.%d", file, next);
    ToriRSMapEd_AcctSaveSet(&state->registry, "queue", key, line);
    return 1;
}

static void
acct_queue_clear(
    struct acct_state* state,
    const char* file)
{
    char key[128];
    char line[512];

    while( acct_queue_nth(state, file, 0, key, sizeof(key), line, sizeof(line)) )
        ToriRSMapEd_AcctSaveRemove(&state->registry, "queue", key);
}

/* ---- who is online -------------------------------------------------------------- */

static const struct ToriRSMapEdJobsPlayer*
acct_online_row(
    struct ToriRSMapEd* maped,
    const char* file)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    if( jobs->live_state != TORIRSMAPED_LIVE_ATTACHED || !jobs->players_known )
        return NULL;
    for( int i = 0; i < jobs->player_row_count; i++ )
    {
        const struct ToriRSMapEdJobsPlayer* row = &jobs->player_rows[i];
        if( live_name_equal(row->account, file) || live_name_equal(row->display, file) )
            return row;
    }
    return NULL;
}

/** The lane's play account as a save file name. */
static void
acct_play_file(
    struct ToriRSMapEd* maped,
    char* out,
    size_t out_size)
{
    char user[16];
    char pass[24];

    jobs_play_account(maped->jobs, user, sizeof(user), pass, sizeof(pass));
    ToriRSMapEd_AcctFileName(user, out, out_size);
}

/* ---- snapshots ---------------------------------------------------------------- */

static struct acct_snapshot*
acct_snapshot_for(
    struct acct_state* state,
    const char* file,
    int make)
{
    for( int i = 0; i < state->snapshot_count; i++ )
        if( strcmp(state->snapshots[i].file, file) == 0 )
            return &state->snapshots[i];
    if( !make )
        return NULL;
    if( state->snapshot_count == ACCT_SNAPSHOTS_MAX )
    {
        /* The oldest slot is reused; its checkpoint file is left behind. */
        memmove(&state->snapshots[0], &state->snapshots[1], (size_t)(ACCT_SNAPSHOTS_MAX - 1) * sizeof(state->snapshots[0]));
        state->snapshot_count--;
    }
    {
        struct acct_snapshot* snap = &state->snapshots[state->snapshot_count++];
        int leg = ACCT_LEG_BASE;
        int taken;
        do
        {
            taken = 0;
            for( int i = 0; i < state->snapshot_count - 1; i++ )
                if( state->snapshots[i].leg == leg )
                    taken = 1;
            if( taken )
                leg++;
        } while( taken );
        memset(snap, 0, sizeof(*snap));
        snprintf(snap->file, sizeof(snap->file), "%s", file);
        snap->leg = leg;
        return snap;
    }
}

static void
acct_snapshot_path(
    const char* live,
    const struct acct_snapshot* snap,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/saves/checkpoints/%d.ini", live, snap->leg);
}

/** Ask the attached server for `file`'s checkpoint; 1 when sent. */
static int
acct_refresh_request(
    struct acct_state* state,
    const char* live,
    const char* file,
    const struct ToriRSMapEdJobsPlayer* row)
{
    struct acct_snapshot* snap = acct_snapshot_for(state, file, 1);
    struct ToriRSMapEdBuf payload;
    char path[ACCT_PATH_MAX];
    char line[64];

    assert(row);
    acct_snapshot_path(live, snap, path, sizeof(path));
    (void)unlink(path);
    snap->have = 0;
    memset(&payload, 0, sizeof(payload));
    snprintf(line, sizeof(line), "checkpoint %d", snap->leg);
    ToriRSMapEd_PutString(&payload, row->account);
    ToriRSMapEd_PutString(&payload, line);
    if( !live_send(state->maped, TORIRSMAPED_CTL_CHEAT, &payload) )
    {
        ToriRSMapEd_BufFree(&payload);
        return 0;
    }
    ToriRSMapEd_BufFree(&payload);
    snap->pending = 1;
    snap->sent = time(NULL);
    return 1;
}

/* ---- the catalog's names ----------------------------------------------------------- */

/** The catalog's record of `kind` named or numbered `text` (exact), or -1. */
static int
acct_catalog_find(
    struct ToriRSMapEd* maped,
    uint32_t kind,
    const char* text,
    struct ToriRSMapEdCatalogEntry* out_entry)
{
    int records[8];
    int total;

    if( !maped->catalog || !ToriRSMapEdCatalog_Built(maped->catalog) )
        return -1;
    total = ToriRSMapEdCatalog_Query(maped->catalog, kind, text, TORIRSMAPED_CATALOG_EXACT, records, 8);
    for( int i = 0; i < total && i < 8; i++ )
    {
        struct ToriRSMapEdCatalogEntry entry;
        char* end = NULL;
        long numeric = strtol(text, &end, 10);
        int const by_id = end && end != text && *end == '\0';
        ToriRSMapEdCatalog_Entry(maped->catalog, records[i], &entry);
        if( entry.kind != kind )
            continue;
        if( by_id ? entry.id == (int32_t)numeric : (strcmp(entry.name, text) == 0 || strcmp(entry.display, text) == 0) )
        {
            *out_entry = entry;
            return records[i];
        }
    }
    return -1;
}

/** The pack name of var `id` of `kind` ("" when the catalog has none yet). */
static void
acct_var_name(
    struct ToriRSMapEd* maped,
    int kind,
    int id,
    char* out,
    size_t out_size)
{
    struct ToriRSMapEdCatalogEntry entry;
    char text[16];

    out[0] = '\0';
    snprintf(text, sizeof(text), "%d", id);
    if( acct_catalog_find(maped, kind == 2 ? TORIRSMAPED_CATALOG_VARBIT : TORIRSMAPED_CATALOG_VARP, text, &entry) >= 0 )
        snprintf(out, out_size, "%s", entry.name);
}

/** A varbit's base varp and bit range from the catalog; 0 when unknown. */
static int
acct_varbit_def(
    struct ToriRSMapEd* maped,
    int id,
    int* out_varp,
    int* out_start,
    int* out_end)
{
    struct ToriRSMapEdCatalogEntry entry;
    char text[16];
    char field[96];
    int record;

    snprintf(text, sizeof(text), "%d", id);
    record = acct_catalog_find(maped, TORIRSMAPED_CATALOG_VARBIT, text, &entry);
    if( record < 0 )
        return 0;
    if( !ToriRSMapEdCatalog_Field(maped->catalog, record, "basevar", field, sizeof(field)) )
        return 0;
    {
        int kind;
        int varp;
        if( !acct_var_token(field, &kind, &varp) || kind != 1 )
        {
            /* A bare id. */
            char* end = NULL;
            long numeric = strtol(field, &end, 10);
            if( !end || end == field )
                return 0;
            varp = (int)numeric;
        }
        *out_varp = varp;
    }
    if( !ToriRSMapEdCatalog_Field(maped->catalog, record, "startbit", field, sizeof(field)) )
        return 0;
    *out_start = atoi(field);
    if( !ToriRSMapEdCatalog_Field(maped->catalog, record, "endbit", field, sizeof(field)) )
        return 0;
    *out_end = atoi(field);
    return *out_start >= 0 && *out_end >= *out_start && *out_end < 32;
}

/** `name` as (kind 1 varp / 2 varbit, id): a decimal id with `kind_hint`,
 *  else the catalog's exact varbit, then varp. */
static int
acct_var_find(
    struct ToriRSMapEd* maped,
    int kind_hint,
    const char* name,
    int* out_kind,
    int* out_id)
{
    struct ToriRSMapEdCatalogEntry entry;
    char* end = NULL;
    long numeric = strtol(name, &end, 10);

    if( end && end != name && *end == '\0' && kind_hint )
    {
        *out_kind = kind_hint;
        *out_id = (int)numeric;
        return 1;
    }
    if( acct_var_token(name, out_kind, out_id) && (kind_hint == 0 || kind_hint == *out_kind) )
        return 1;
    if( kind_hint != 1 && acct_catalog_find(maped, TORIRSMAPED_CATALOG_VARBIT, name, &entry) >= 0 )
    {
        *out_kind = 2;
        *out_id = entry.id;
        return 1;
    }
    if( kind_hint != 2 && acct_catalog_find(maped, TORIRSMAPED_CATALOG_VARP, name, &entry) >= 0 )
    {
        *out_kind = 1;
        *out_id = entry.id;
        return 1;
    }
    return 0;
}

/** An obj by id or exact name; the name is filled when the catalog knows it. */
static int
acct_obj_find(
    struct ToriRSMapEd* maped,
    const char* text,
    int* out_id,
    char* out_name,
    size_t name_size)
{
    struct ToriRSMapEdCatalogEntry entry;
    char* end = NULL;
    long numeric = strtol(text, &end, 10);

    out_name[0] = '\0';
    if( acct_catalog_find(maped, TORIRSMAPED_CATALOG_OBJ, text, &entry) >= 0 )
    {
        *out_id = entry.id;
        snprintf(out_name, name_size, "%s", entry.name);
        return 1;
    }
    if( end && end != text && *end == '\0' && numeric >= 0 )
    {
        *out_id = (int)numeric;
        return 1;
    }
    return 0;
}

/* ---- the quest index -------------------------------------------------------------- */

/** Every .constant file under `dir`, each handed to `visit`. */
static void
acct_walk_constants(
    const char* dir,
    int depth,
    void (*visit)(void* user, const char* path),
    void* user)
{
    DIR* d = opendir(dir);
    struct dirent* entry;

    if( !d || depth > 12 )
    {
        if( d )
            closedir(d);
        return;
    }
    while( (entry = readdir(d)) )
    {
        char path[ACCT_PATH_MAX];
        struct stat st;
        size_t len;
        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            /* The pack outputs hold no sources. */
            if( strncmp(entry->d_name, "build", 5) == 0 )
                continue;
            acct_walk_constants(path, depth + 1, visit, user);
            continue;
        }
        len = strlen(entry->d_name);
        if( len > 9 && strcmp(entry->d_name + len - 9, ".constant") == 0 )
            visit(user, path);
    }
    closedir(d);
}

static void
acct_constants_visit(
    void* user,
    const char* path)
{
    struct acct_state* state = user;
    size_t length;
    char* text = acct_read_file(path, &length);

    if( !text )
        return;
    for( int i = 0; i < state->quest_count; i++ )
    {
        struct ToriRSMapEdAcctQuest* quest = &state->quests[i];
        if( quest->complete_value >= 0 || !quest->constant[0] )
            continue;
        (void)ToriRSMapEd_AcctConstantFind(text, length, quest->constant, &quest->complete_value);
    }
    free(text);
}

static void
acct_quests_load(struct acct_state* state)
{
    struct ToriRSMapEd* maped = state->maped;
    char path[ACCT_PATH_MAX];
    size_t length;
    char* text;
    static struct ToriRSMapEdAcctQuest arms[ACCT_QUESTS_MAX];
    int arm_count = 0;

    if( state->quests_loaded )
        return;
    state->quests_loaded = 1;
    state->quest_count = 0;
    snprintf(path, sizeof(path), "%s/configs/all.dbrow", maped->content_dir);
    text = acct_read_file(path, &length);
    if( text )
    {
        state->quest_count = ToriRSMapEd_AcctQuestRows(text, length, state->quests, ACCT_QUESTS_MAX);
        free(text);
    }
    snprintf(path, sizeof(path), "%s/server/scripts/quests/scripts/quest_cheat.rs2", maped->content_dir);
    text = acct_read_file(path, &length);
    if( text )
    {
        arm_count = ToriRSMapEd_AcctQuestArms(text, length, arms, ACCT_QUESTS_MAX);
        free(text);
    }
    for( int a = 0; a < arm_count; a++ )
    {
        struct ToriRSMapEdAcctQuest* row = NULL;
        for( int i = 0; i < state->quest_count && !row; i++ )
            if( strcmp(state->quests[i].dbrow, arms[a].dbrow) == 0 )
                row = &state->quests[i];
        if( !row )
        {
            if( state->quest_count == ACCT_QUESTS_MAX )
                continue;
            row = &state->quests[state->quest_count++];
            *row = arms[a];
            snprintf(row->display, sizeof(row->display), "%s", arms[a].dbrow);
            continue;
        }
        row->var_kind = arms[a].var_kind;
        row->var_id = arms[a].var_id;
        snprintf(row->constant, sizeof(row->constant), "%s", arms[a].constant);
        row->complete_value = -1;
    }
    snprintf(path, sizeof(path), "%s/server/scripts", maped->content_dir);
    acct_walk_constants(path, 0, acct_constants_visit, state);
    TORIRS_LOG("torirsmaped: accounts: %d quest row(s), %d with a ::complete arm\n", state->quest_count, arm_count);
}

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

/** One account's values as the view shows them: the save, or the server's
 *  checkpoint while the account is online and one landed. */
struct acct_values
{
    struct ToriRSMapEdAcctSave save;
    int from_snapshot;
    int have;
};

static int
acct_values_load(
    struct acct_state* state,
    const char* live,
    const char* file,
    const struct ToriRSMapEdJobsPlayer* row,
    struct acct_values* out)
{
    char path[ACCT_PATH_MAX];

    memset(out, 0, sizeof(*out));
    ToriRSMapEd_AcctSaveInit(&out->save);
    if( row )
    {
        struct acct_snapshot* snap = acct_snapshot_for(state, file, 0);
        if( snap && snap->have )
        {
            acct_snapshot_path(live, snap, path, sizeof(path));
            if( ToriRSMapEd_AcctSaveLoad(&out->save, path) )
            {
                out->from_snapshot = 1;
                out->have = 1;
                return 1;
            }
        }
    }
    acct_save_path(live, file, path, sizeof(path));
    out->have = ToriRSMapEd_AcctSaveLoad(&out->save, path);
    return out->have;
}

static int
acct_values_int(
    const struct acct_values* values,
    const char* section,
    const char* key,
    int fallback)
{
    char text[64];
    if( !ToriRSMapEd_AcctSaveGet(&values->save, section, key, text, sizeof(text)) )
        return fallback;
    return atoi(text);
}

/** The varp's value in the values (0 when absent, as the server reads it). */
static int
acct_values_varp(
    const struct acct_values* values,
    int varp)
{
    char key[16];
    snprintf(key, sizeof(key), "%d", varp);
    return acct_values_int(values, "varps", key, 0);
}

/** boosted and xp_tenths of `stat` (the fresh character's where absent). */
static void
acct_values_stat(
    const struct acct_values* values,
    int stat,
    int* out_boosted,
    int* out_xp_tenths)
{
    char key[16];
    char text[64];
    int a = 1;
    int b = 1;
    int c = 0;
    int n;

    *out_boosted = stat == 3 ? 10 : 1;
    *out_xp_tenths = stat == 3 ? ToriRSMapEd_AcctXpForLevel(10) * 10 : 0;
    snprintf(key, sizeof(key), "%d", stat);
    if( !ToriRSMapEd_AcctSaveGet(&values->save, "stats", key, text, sizeof(text)) )
        return;
    n = sscanf(text, "%d %d %d", &a, &b, &c);
    if( n == 3 )
    {
        *out_boosted = b;
        *out_xp_tenths = c;
    }
    else if( n == 2 )
    {
        *out_boosted = a;
        *out_xp_tenths = b;
    }
}

/** A quest's completion state from the values, through the catalog's
 *  varbit definitions. */
static uint32_t
acct_quest_state(
    struct acct_state* state,
    const struct acct_values* values,
    const struct ToriRSMapEdAcctQuest* quest,
    int32_t* out_value)
{
    int value;

    *out_value = 0;
    if( quest->var_kind == 0 || quest->complete_value < 0 )
        return TORIRSMAPED_QUEST_UNKNOWN;
    if( quest->var_kind == 1 )
        value = acct_values_varp(values, quest->var_id);
    else
    {
        int varp;
        int start;
        int end;
        if( !acct_varbit_def(state->maped, quest->var_id, &varp, &start, &end) )
            return TORIRSMAPED_QUEST_UNKNOWN;
        value = (int)(((uint32_t)acct_values_varp(values, varp) >> start) & ((end - start >= 31) ? 0xffffffffu : ((1u << (end - start + 1)) - 1u)));
    }
    *out_value = value;
    return value >= quest->complete_value ? TORIRSMAPED_QUEST_COMPLETE : TORIRSMAPED_QUEST_NOT_COMPLETE;
}

/* ------------------------------------------------------------------ */
/* Facts                                                               */
/* ------------------------------------------------------------------ */

static void
acct_send_or_broadcast(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    if( session )
        send_buf(session, type, payload);
    else
        jobs_broadcast(maped, type, payload);
}

static void
acct_status(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    enum EditorHost_Status status,
    const char* account,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;

static void
acct_status(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    enum EditorHost_Status status,
    const char* account,
    const char* format,
    ...)
{
    struct ToriRSMapEdBuf payload;
    char message[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, request);
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    ToriRSMapEd_PutString(&payload, account);
    ToriRSMapEd_PutString(&payload, message);
    acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNT_STATUS, &payload);
    ToriRSMapEd_BufFree(&payload);
    TORIRS_LOG("torirsmaped: accounts: %s%s%s\n", account[0] ? account : "", account[0] ? ": " : "", message);
}

/** The save files under `<live>/saves`, sorted, as file names. */
static int
acct_list_files(
    const char* live,
    char names[][64],
    int max)
{
    char dir[ACCT_PATH_MAX];
    DIR* d;
    struct dirent* entry;
    int count = 0;

    snprintf(dir, sizeof(dir), "%s/saves", live);
    d = opendir(dir);
    if( !d )
        return 0;
    while( (entry = readdir(d)) && count < max )
    {
        size_t const len = strlen(entry->d_name);
        if( len < 5 || strcmp(entry->d_name + len - 4, ".ini") != 0 || entry->d_name[0] == '.' )
            continue;
        if( len - 4 >= 64 )
            continue;
        memcpy(names[count], entry->d_name, len - 4);
        names[count][len - 4] = '\0';
        count++;
    }
    closedir(d);
    /* Insertion sort: a handful of accounts. */
    for( int i = 1; i < count; i++ )
        for( int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j-- )
        {
            char swap[64];
            memcpy(swap, names[j], sizeof(swap));
            memcpy(names[j], names[j - 1], sizeof(swap));
            memcpy(names[j - 1], swap, sizeof(swap));
        }
    return count;
}

static uint32_t
acct_flags(
    struct acct_state* state,
    const char* live,
    const char* file,
    const struct ToriRSMapEdJobsPlayer* row)
{
    uint32_t flags = 0;
    char play[64];
    char pass[64];
    char path[ACCT_PATH_MAX];
    struct acct_snapshot* snap;

    if( row )
        flags |= TORIRSMAPED_ACCT_ONLINE;
    acct_play_file(state->maped, play, sizeof(play));
    if( strcmp(play, file) == 0 )
        flags |= TORIRSMAPED_ACCT_PLAY;
    if( acct_password_for(state, file, pass, sizeof(pass)) )
        flags |= TORIRSMAPED_ACCT_PASSWORD;
    acct_save_path(live, file, path, sizeof(path));
    if( access(path, F_OK) == 0 )
        flags |= TORIRSMAPED_ACCT_SAVE;
    snap = acct_snapshot_for(state, file, 0);
    if( row && snap && snap->have )
        flags |= TORIRSMAPED_ACCT_SNAPSHOT;
    if( snap && snap->pending )
        flags |= TORIRSMAPED_ACCT_REFRESHING;
    return flags;
}

static void
acct_broadcast_list(
    struct acct_state* state,
    struct ToriRSMapEdSession* session)
{
    struct ToriRSMapEd* maped = state->maped;
    struct ToriRSMapEdBuf payload;
    char live[ACCT_PATH_MAX];
    static char names[ACCT_NAMES_MAX][64];
    int count;
    char play[64];

    memset(&payload, 0, sizeof(payload));
    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        ToriRSMapEd_PutU32(&payload, EDITOR_HOST_UNSUPPORTED);
        ToriRSMapEd_PutString(&payload, "");
        ToriRSMapEd_PutString(&payload, "");
        ToriRSMapEd_PutU32(&payload, 0);
        ToriRSMapEd_PutU32(&payload, 0);
        acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNTS, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }
    count = acct_list_files(live, names, ACCT_NAMES_MAX);
    acct_play_file(maped, play, sizeof(play));
    ToriRSMapEd_PutU32(&payload, EDITOR_HOST_OK);
    ToriRSMapEd_PutString(&payload, live);
    ToriRSMapEd_PutString(&payload, play);
    ToriRSMapEd_PutU32(&payload, maped->jobs->live_state == TORIRSMAPED_LIVE_ATTACHED);
    ToriRSMapEd_PutU32(&payload, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSMapEdJobsPlayer* row = acct_online_row(maped, names[i]);
        int x = -1;
        int z = -1;
        int level = -1;
        ToriRSMapEd_PutString(&payload, names[i]);
        ToriRSMapEd_PutU32(&payload, acct_flags(state, live, names[i], row));
        if( row )
        {
            x = row->x;
            z = row->z;
            level = row->level;
        }
        else
        {
            struct acct_values values;
            if( acct_values_load(state, live, names[i], NULL, &values) )
            {
                x = acct_values_int(&values, "player", "x", -1);
                z = acct_values_int(&values, "player", "z", -1);
                level = acct_values_int(&values, "player", "level", 0);
            }
            ToriRSMapEd_AcctSaveFree(&values.save);
        }
        ToriRSMapEd_PutU32(&payload, (uint32_t)x);
        ToriRSMapEd_PutU32(&payload, (uint32_t)z);
        ToriRSMapEd_PutU32(&payload, (uint32_t)level);
        ToriRSMapEd_PutU32(&payload, (uint32_t)acct_queue_count(state, names[i]));
    }
    acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNTS, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
acct_put_items(
    struct ToriRSMapEdBuf* payload,
    const struct acct_values* values,
    const char* section,
    int slot_max)
{
    char key[64];
    char value[128];
    int count = 0;
    int at = (int)ToriRSMapEd_BufAvailable(payload);

    ToriRSMapEd_PutU32(payload, 0);
    for( int i = 0; ToriRSMapEd_AcctSaveNth(&values->save, section, i, key, sizeof(key), value, sizeof(value)); i++ )
    {
        int obj = -1;
        int n = 0;
        int const slot = atoi(key);
        if( slot < 0 || slot >= slot_max || sscanf(value, "%d %d", &obj, &n) != 2 || obj < 0 || n <= 0 )
            continue;
        ToriRSMapEd_PutU32(payload, (uint32_t)slot);
        ToriRSMapEd_PutU32(payload, (uint32_t)obj);
        ToriRSMapEd_PutU32(payload, (uint32_t)n);
        count++;
    }
    ToriRSMapEd_WriteU32(payload->data + payload->head + at, (uint32_t)count);
}

/** FACT_ACCOUNT for `file`, to `session` or everyone. */
static void
acct_broadcast_view(
    struct acct_state* state,
    struct ToriRSMapEdSession* session,
    const char* file)
{
    struct ToriRSMapEd* maped = state->maped;
    struct ToriRSMapEdBuf payload;
    char live[ACCT_PATH_MAX];
    const struct ToriRSMapEdJobsPlayer* row;
    struct acct_values values;
    uint32_t flags;
    char text[256];
    char display[64];
    int count_at;
    int count;

    memset(&payload, 0, sizeof(payload));
    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        ToriRSMapEd_PutU32(&payload, EDITOR_HOST_UNSUPPORTED);
        ToriRSMapEd_PutString(&payload, file);
        acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNT, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }
    row = acct_online_row(maped, file);
    if( !acct_values_load(state, live, file, row, &values) )
    {
        ToriRSMapEd_AcctSaveFree(&values.save);
        ToriRSMapEd_PutU32(&payload, EDITOR_HOST_ABSENT);
        ToriRSMapEd_PutString(&payload, file);
        acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNT, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }
    flags = acct_flags(state, live, file, row);
    ToriRSMapEd_PutU32(&payload, EDITOR_HOST_OK);
    ToriRSMapEd_PutString(&payload, file);
    ToriRSMapEd_PutU32(&payload, flags);
    if( row )
    {
        struct acct_snapshot* snap = acct_snapshot_for(state, file, 0);
        snprintf(text, sizeof(text), "online at %d,%d level %d: edits are cheats; values %s", row->x, row->z,
            row->level,
            values.from_snapshot ? "from the server's checkpoint"
            : snap && snap->pending ? "from the last save (a checkpoint is on its way)"
                                    : "from the last save (Refresh asks the server)");
    }
    else
        snprintf(text, sizeof(text), "offline: edits write the save");
    ToriRSMapEd_PutString(&payload, text);
    if( !ToriRSMapEd_AcctSaveGet(&values.save, "player", "display_name", display, sizeof(display)) )
        if( !ToriRSMapEd_AcctSaveGet(&values.save, "player", "name", display, sizeof(display)) )
            snprintf(display, sizeof(display), "%s", file);
    ToriRSMapEd_PutString(&payload, display);
    ToriRSMapEd_PutU32(&payload, (uint32_t)(row ? row->x : acct_values_int(&values, "player", "x", -1)));
    ToriRSMapEd_PutU32(&payload, (uint32_t)(row ? row->z : acct_values_int(&values, "player", "z", -1)));
    ToriRSMapEd_PutU32(&payload, (uint32_t)(row ? row->level : acct_values_int(&values, "player", "level", 0)));
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_ACCT_STATS);
    for( int stat = 0; stat < TORIRSMAPED_ACCT_STATS; stat++ )
    {
        int boosted;
        int xp_tenths;
        acct_values_stat(&values, stat, &boosted, &xp_tenths);
        ToriRSMapEd_PutU32(&payload, (uint32_t)boosted);
        ToriRSMapEd_PutU32(&payload, (uint32_t)xp_tenths);
    }
    /* Varps. */
    count_at = ToriRSMapEd_BufAvailable(&payload);
    ToriRSMapEd_PutU32(&payload, 0);
    count = 0;
    {
        char key[64];
        char value[64];
        for( int i = 0; ToriRSMapEd_AcctSaveNth(&values.save, "varps", i, key, sizeof(key), value, sizeof(value)); i++ )
        {
            char name[96];
            char* end = NULL;
            long id = strtol(key, &end, 10);
            if( !end || end == key || *end != '\0' || id < 0 )
                continue;
            acct_var_name(maped, 1, (int)id, name, sizeof(name));
            ToriRSMapEd_PutU32(&payload, (uint32_t)id);
            ToriRSMapEd_PutU32(&payload, (uint32_t)atoi(value));
            ToriRSMapEd_PutString(&payload, name);
            count++;
        }
    }
    ToriRSMapEd_WriteU32(payload.data + payload.head + count_at, (uint32_t)count);
    acct_put_items(&payload, &values, "inv", TORIRSMAPED_ACCT_INV_SLOTS);
    acct_put_items(&payload, &values, "worn", TORIRSMAPED_ACCT_WORN_SLOTS);
    acct_put_items(&payload, &values, "bank", TORIRSMAPED_ACCT_BANK_SLOTS);
    /* The queue. */
    count = acct_queue_count(state, file);
    ToriRSMapEd_PutU32(&payload, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        char key[128];
        char line[512];
        if( acct_queue_nth(state, file, i, key, sizeof(key), line, sizeof(line)) )
            ToriRSMapEd_PutString(&payload, line);
        else
            ToriRSMapEd_PutString(&payload, "");
    }
    /* Quests. */
    acct_quests_load(state);
    ToriRSMapEd_PutU32(&payload, (uint32_t)state->quest_count);
    for( int i = 0; i < state->quest_count; i++ )
    {
        int32_t value;
        uint32_t quest_state = acct_quest_state(state, &values, &state->quests[i], &value);
        ToriRSMapEd_PutString(&payload, state->quests[i].dbrow);
        ToriRSMapEd_PutU32(&payload, quest_state);
        ToriRSMapEd_PutU32(&payload, (uint32_t)value);
    }
    ToriRSMapEd_AcctSaveFree(&values.save);
    acct_send_or_broadcast(maped, session, TORIRSMAPED_FACT_ACCOUNT, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
acct_send_quests(
    struct acct_state* state,
    struct ToriRSMapEdSession* session)
{
    struct ToriRSMapEdBuf payload;

    acct_quests_load(state);
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)state->quest_count);
    for( int i = 0; i < state->quest_count; i++ )
    {
        const struct ToriRSMapEdAcctQuest* quest = &state->quests[i];
        char var[32];
        if( quest->var_kind == 1 )
            snprintf(var, sizeof(var), "varp %d", quest->var_id);
        else if( quest->var_kind == 2 )
            snprintf(var, sizeof(var), "varbit %d", quest->var_id);
        else if( quest->constant[0] )
            snprintf(var, sizeof(var), "proc");
        else
            var[0] = '\0';
        ToriRSMapEd_PutString(&payload, quest->dbrow);
        ToriRSMapEd_PutString(&payload, quest->display);
        ToriRSMapEd_PutString(&payload, var);
        ToriRSMapEd_PutU32(&payload, (uint32_t)quest->complete_value);
    }
    send_buf(session, TORIRSMAPED_FACT_QUESTS, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/* ------------------------------------------------------------------ */
/* Edits                                                               */
/* ------------------------------------------------------------------ */

#define ACCT_EDIT_CHEATS_MAX 400

/** What one SET resolved to: cheat lines (online), the save edited
 *  (offline), lines to queue (no offline form). */
struct acct_edit
{
    char (*cheats)[200];
    int cheat_count;
    int queue_only;
    int save_changed;
    char what[256];
    char why[256];
};

static void
acct_edit_cheat(
    struct acct_edit* edit,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void
acct_edit_cheat(
    struct acct_edit* edit,
    const char* format,
    ...)
{
    va_list args;

    if( edit->cheat_count >= ACCT_EDIT_CHEATS_MAX )
        return;
    va_start(args, format);
    vsnprintf(edit->cheats[edit->cheat_count], sizeof(edit->cheats[0]), format, args);
    va_end(args);
    edit->cheat_count++;
}

static int
acct_edit_refuse(
    struct acct_edit* edit,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static int
acct_edit_refuse(
    struct acct_edit* edit,
    const char* format,
    ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(edit->why, sizeof(edit->why), format, args);
    va_end(args);
    return 0;
}

/** Up to three space-separated integers out of `value`; the count. */
static int
acct_ints(
    const char* value,
    int* a,
    int* b,
    int* c)
{
    char rest[8] = "";
    int n = sscanf(value, "%d %d %d %7s", a, b, c, rest);
    if( rest[0] )
        return -1;
    return n;
}

/** The position: x z level into the save or a ::tele. */
static int
acct_edit_position(
    struct acct_edit* edit,
    struct acct_values* values,
    int online,
    int x,
    int z,
    int level)
{
    if( x < 0 || x >= 16384 || z < 0 || z >= 16384 || level < 0 || level > 3 )
        return acct_edit_refuse(edit, "a position is 0 <= x,z < 16384 and level 0..3 (got %d,%d,%d)", x, z, level);
    snprintf(edit->what, sizeof(edit->what), "position %d,%d level %d", x, z, level);
    if( online )
    {
        acct_edit_cheat(edit, "tele %d_%d_%d_%d_%d", level, x >> 6, z >> 6, x & 63, z & 63);
        return 1;
    }
    {
        char text[16];
        snprintf(text, sizeof(text), "%d", x);
        edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "player", "x", text);
        snprintf(text, sizeof(text), "%d", z);
        edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "player", "z", text);
        snprintf(text, sizeof(text), "%d", level);
        edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "player", "level", text);
    }
    return 1;
}

static void
acct_save_stat(
    struct acct_values* values,
    struct acct_edit* edit,
    int stat,
    int boosted,
    int xp_tenths)
{
    char key[16];
    char text[48];
    snprintf(key, sizeof(key), "%d", stat);
    snprintf(text, sizeof(text), "%d %d", boosted, xp_tenths);
    edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "stats", key, text);
}

static int
acct_edit_stats(
    struct acct_edit* edit,
    struct acct_values* values,
    int online,
    const char* field,
    const char* value)
{
    char name[64];
    const char* dot;
    int xp_form = 0;
    int stat;
    int a = 0;
    int b = 0;
    int c = 0;

    dot = strchr(field, '.');
    if( dot )
    {
        snprintf(name, sizeof(name), "%.*s", (int)(dot - field), field);
        if( strcmp(dot + 1, "xp") != 0 )
            return acct_edit_refuse(edit, "stats.<stat>.xp is the only suffix (got '%s')", dot + 1);
        xp_form = 1;
    }
    else
        snprintf(name, sizeof(name), "%s", field);
    if( strcmp(name, "all") == 0 )
    {
        if( xp_form )
            return acct_edit_refuse(edit, "stats.all takes max or reset");
        if( strcmp(value, "max") == 0 )
        {
            snprintf(edit->what, sizeof(edit->what), "every stat 99");
            if( online )
                acct_edit_cheat(edit, "maxstats");
            else
                for( stat = 0; stat < TORIRSMAPED_ACCT_STATS; stat++ )
                    acct_save_stat(values, edit, stat, 99, ToriRSMapEd_AcctXpForLevel(99) * 10);
            return 1;
        }
        if( strcmp(value, "reset") == 0 )
        {
            snprintf(edit->what, sizeof(edit->what), "every stat back to a fresh character's");
            for( stat = 0; stat < TORIRSMAPED_ACCT_STATS; stat++ )
            {
                if( online )
                    acct_edit_cheat(edit, "setlevel %d %d", stat, stat == 3 ? 10 : 1);
                else
                {
                    char key[16];
                    snprintf(key, sizeof(key), "%d", stat);
                    edit->save_changed |= ToriRSMapEd_AcctSaveRemove(&values->save, "stats", key);
                }
            }
            return 1;
        }
        return acct_edit_refuse(edit, "stats.all takes max or reset (got '%s')", value);
    }
    stat = ToriRSMapEd_AcctStatFind(name);
    if( stat < 0 )
        return acct_edit_refuse(edit, "no stat is named '%s'", name);
    if( acct_ints(value, &a, &b, &c) != 1 )
        return acct_edit_refuse(edit, "stats.%s takes one number (got '%s')", name, value);
    if( xp_form )
    {
        int current_boosted;
        int current_xp_tenths;
        if( a < 0 || a > 200000000 )
            return acct_edit_refuse(edit, "xp is 0..200000000 (got %d)", a);
        snprintf(edit->what, sizeof(edit->what), "%s xp %d (level %d)", ToriRSMapEd_AcctStatName(stat), a,
            ToriRSMapEd_AcctLevelForXp(a));
        if( online )
        {
            acct_values_stat(values, stat, &current_boosted, &current_xp_tenths);
            if( !values->have )
                return acct_edit_refuse(edit, "online, xp only grows (::xp adds): refresh first, or set the level");
            if( a * 10 < current_xp_tenths )
                return acct_edit_refuse(edit, "online, xp only grows (::xp adds): %s has %d, set the level instead",
                    ToriRSMapEd_AcctStatName(stat), current_xp_tenths / 10);
            if( a * 10 == current_xp_tenths )
                return acct_edit_refuse(edit, "%s already has %d xp", ToriRSMapEd_AcctStatName(stat), a);
            acct_edit_cheat(edit, "xp %s %d", ToriRSMapEd_AcctStatName(stat), a - current_xp_tenths / 10);
            return 1;
        }
        acct_save_stat(values, edit, stat, ToriRSMapEd_AcctLevelForXp(a), a * 10);
        return 1;
    }
    if( a < 1 || a > 99 )
        return acct_edit_refuse(edit, "a level is 1..99 (got %d)", a);
    snprintf(edit->what, sizeof(edit->what), "%s level %d", ToriRSMapEd_AcctStatName(stat), a);
    if( online )
        acct_edit_cheat(edit, "setlevel %s %d", ToriRSMapEd_AcctStatName(stat), a);
    else
        acct_save_stat(values, edit, stat, a, ToriRSMapEd_AcctXpForLevel(a) * 10);
    return 1;
}

static int
acct_edit_var(
    struct acct_state* state,
    struct acct_edit* edit,
    struct acct_values* values,
    int online,
    int kind_hint,
    const char* name,
    const char* value)
{
    struct ToriRSMapEd* maped = state->maped;
    int kind;
    int id;
    int a = 0;
    int b = 0;
    int c = 0;
    char var_name[96];

    if( !acct_var_find(maped, kind_hint, name, &kind, &id) )
    {
        if( maped->catalog && !ToriRSMapEdCatalog_Built(maped->catalog) )
            return acct_edit_refuse(edit, "the catalog is still indexing the content: name '%s' cannot be resolved yet", name);
        return acct_edit_refuse(edit, "no %s is named '%s'", kind_hint == 1 ? "varp" : kind_hint == 2 ? "varbit" : "varp or varbit", name);
    }
    if( acct_ints(value, &a, &b, &c) != 1 )
        return acct_edit_refuse(edit, "a var takes one integer (got '%s')", value);
    acct_var_name(maped, kind, id, var_name, sizeof(var_name));
    snprintf(edit->what, sizeof(edit->what), "%s %d%s%s = %d", kind == 2 ? "varbit" : "varp", id,
        var_name[0] ? " " : "", var_name, a);
    if( online )
    {
        if( !var_name[0] )
            return acct_edit_refuse(edit, "::setvar takes a name and the catalog has none for %s %d",
                kind == 2 ? "varbit" : "varp", id);
        acct_edit_cheat(edit, "setvar %s %d", var_name, a);
        return 1;
    }
    if( kind == 1 )
    {
        char key[16];
        char text[16];
        snprintf(key, sizeof(key), "%d", id);
        snprintf(text, sizeof(text), "%d", a);
        if( a == 0 )
            edit->save_changed |= ToriRSMapEd_AcctSaveRemove(&values->save, "varps", key);
        else
            edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "varps", key, text);
        return 1;
    }
    {
        int varp;
        int start;
        int end;
        uint32_t mask;
        uint32_t old;
        uint32_t fresh;
        char key[16];
        char text[16];
        if( !acct_varbit_def(maped, id, &varp, &start, &end) )
            return acct_edit_refuse(edit, "varbit %d: the catalog has no basevar/startbit/endbit for it (indexing?)", id);
        mask = (end - start >= 31) ? 0xffffffffu : ((1u << (end - start + 1)) - 1u);
        if( (uint32_t)a > mask )
            return acct_edit_refuse(edit, "varbit %d holds %d bit(s): at most %u (got %d)", id, end - start + 1, mask, a);
        old = (uint32_t)acct_values_varp(values, varp);
        fresh = (old & ~(mask << start)) | (((uint32_t)a & mask) << start);
        snprintf(key, sizeof(key), "%d", varp);
        snprintf(text, sizeof(text), "%d", (int)fresh);
        if( fresh == 0 )
            edit->save_changed |= ToriRSMapEd_AcctSaveRemove(&values->save, "varps", key);
        else
            edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, "varps", key, text);
        snprintf(edit->what + strlen(edit->what), sizeof(edit->what) - strlen(edit->what), " (varp %d bits %d..%d)",
            varp, start, end);
        return 1;
    }
}

static int
acct_edit_items(
    struct acct_state* state,
    struct acct_edit* edit,
    struct acct_values* values,
    int online,
    const char* section,
    const char* field,
    const char* value)
{
    int const slot_max = strcmp(section, "inv") == 0 ? TORIRSMAPED_ACCT_INV_SLOTS
                         : strcmp(section, "worn") == 0 ? TORIRSMAPED_ACCT_WORN_SLOTS
                                                        : TORIRSMAPED_ACCT_BANK_SLOTS;
    char* end = NULL;
    long slot;
    char key[16];

    if( strcmp(field, "clear") == 0 )
    {
        if( strcmp(section, "inv") != 0 )
            return acct_edit_refuse(edit, "only inv.clear exists (the server's ::clearinv)");
        snprintf(edit->what, sizeof(edit->what), "backpack emptied");
        if( online )
        {
            acct_edit_cheat(edit, "clearinv");
            return 1;
        }
        {
            char k[64];
            char v[128];
            while( ToriRSMapEd_AcctSaveNth(&values->save, "inv", 0, k, sizeof(k), v, sizeof(v)) )
            {
                ToriRSMapEd_AcctSaveRemove(&values->save, "inv", k);
                edit->save_changed = 1;
            }
            while( ToriRSMapEd_AcctSaveNth(&values->save, "inv_var", 0, k, sizeof(k), v, sizeof(v)) )
                ToriRSMapEd_AcctSaveRemove(&values->save, "inv_var", k);
        }
        return 1;
    }
    slot = strtol(field, &end, 10);
    if( !end || end == field || *end != '\0' || slot < 0 || slot >= slot_max )
        return acct_edit_refuse(edit, "%s.<slot> is 0..%d (got '%s')", section, slot_max - 1, field);
    snprintf(key, sizeof(key), "%ld", slot);
    if( strcmp(value, "-") == 0 )
    {
        snprintf(edit->what, sizeof(edit->what), "%s slot %ld emptied", section, slot);
        if( online )
            return acct_edit_refuse(edit, "no cheat empties one slot online: drop it in the client, or edit when offline");
        edit->save_changed |= ToriRSMapEd_AcctSaveRemove(&values->save, section, key);
        return 1;
    }
    {
        char obj_text[96] = "";
        int count = 1;
        char rest[8] = "";
        int obj;
        char obj_name[96];
        int n = sscanf(value, "%95s %d %7s", obj_text, &count, rest);
        if( n < 1 || rest[0] )
            return acct_edit_refuse(edit, "%s.<slot> takes <obj id|name> [count] or - (got '%s')", section, value);
        if( count < 1 )
            count = 1;
        if( !acct_obj_find(state->maped, obj_text, &obj, obj_name, sizeof(obj_name)) )
        {
            if( state->maped->catalog && !ToriRSMapEdCatalog_Built(state->maped->catalog) )
                return acct_edit_refuse(edit, "the catalog is still indexing: name '%s' cannot be resolved yet (an id works)", obj_text);
            return acct_edit_refuse(edit, "no obj is named '%s'", obj_text);
        }
        snprintf(edit->what, sizeof(edit->what), "%s slot %ld = %s%s%d x%d", section, slot, obj_name, obj_name[0] ? " " : "",
            obj, count);
        if( online )
        {
            if( strcmp(section, "bank") == 0 )
                return acct_edit_refuse(edit, "no cheat banks an item online: edit the bank when the account is offline");
            acct_edit_cheat(edit, "item %d %d", obj, count);
            if( strcmp(section, "worn") == 0 )
                acct_edit_cheat(edit, "wield %d", obj);
            snprintf(edit->what + strlen(edit->what), sizeof(edit->what) - strlen(edit->what),
                " (online: into the first free backpack slot%s)", strcmp(section, "worn") == 0 ? ", then wielded" : "");
            return 1;
        }
        {
            char text[48];
            snprintf(text, sizeof(text), "%d %d", obj, count);
            edit->save_changed |= ToriRSMapEd_AcctSaveSet(&values->save, section, key, text);
        }
        return 1;
    }
}

static int
acct_edit_quests(
    struct acct_state* state,
    struct acct_edit* edit,
    const char* field,
    const char* value)
{
    int mark;

    acct_quests_load(state);
    if( strcmp(value, "complete") == 0 )
        mark = 0;
    else if( strcmp(value, "mark") == 0 )
        mark = 1;
    else
        return acct_edit_refuse(edit, "quests.<dbrow> takes complete or mark (got '%s')", value);
    edit->queue_only = 1;
    if( strcmp(field, "all") == 0 )
    {
        int n = 0;
        if( !mark )
            return acct_edit_refuse(edit, "quests.all takes mark only (::complete pays quest points per quest)");
        for( int i = 0; i < state->quest_count; i++ )
            if( state->quests[i].constant[0] )
            {
                acct_edit_cheat(edit, "questmark %s", state->quests[i].dbrow);
                n++;
            }
        snprintf(edit->what, sizeof(edit->what), "%d quest(s) marked complete", n);
        return n > 0 ? 1 : acct_edit_refuse(edit, "the quest index has no ::complete arms (no quest_cheat.rs2?)");
    }
    for( int i = 0; i < state->quest_count; i++ )
        if( strcmp(state->quests[i].dbrow, field) == 0 )
        {
            if( !state->quests[i].constant[0] )
                return acct_edit_refuse(edit, "%s has no ::complete arm in quest_cheat.rs2", field);
            acct_edit_cheat(edit, "%s %s", mark ? "questmark" : "complete", field);
            snprintf(edit->what, sizeof(edit->what), "%s %s", state->quests[i].display,
                mark ? "marked complete" : "completed (quest points paid)");
            return 1;
        }
    return acct_edit_refuse(edit, "no quest row is named '%s'", field);
}

/** Resolve one SET into `edit`. 0 with edit->why on a refusal. */
static int
acct_edit_resolve(
    struct acct_state* state,
    struct acct_edit* edit,
    struct acct_values* values,
    int online,
    const char* key,
    const char* value)
{
    char section[32];
    const char* field;
    const char* dot = strchr(key, '.');

    if( dot )
    {
        snprintf(section, sizeof(section), "%.*s", (int)(dot - key), key);
        field = dot + 1;
    }
    else
    {
        snprintf(section, sizeof(section), "%s", key);
        field = "";
    }
    if( strcmp(section, "player") == 0 )
    {
        int x = acct_values_int(values, "player", "x", 0);
        int z = acct_values_int(values, "player", "z", 0);
        int level = acct_values_int(values, "player", "level", 0);
        int a = 0;
        int b = 0;
        int c = 0;
        int n = acct_ints(value, &a, &b, &c);
        if( strcmp(field, "pos") == 0 )
        {
            if( n != 3 )
                return acct_edit_refuse(edit, "player.pos takes <x> <z> <level> (got '%s')", value);
            return acct_edit_position(edit, values, online, a, b, c);
        }
        if( n != 1 )
            return acct_edit_refuse(edit, "player.%s takes one number (got '%s')", field, value);
        if( strcmp(field, "x") == 0 )
            x = a;
        else if( strcmp(field, "z") == 0 )
            z = a;
        else if( strcmp(field, "level") == 0 )
            level = a;
        else
            return acct_edit_refuse(edit, "player.<x|z|level|pos> (got '%s')", field);
        return acct_edit_position(edit, values, online, x, z, level);
    }
    if( strcmp(section, "stats") == 0 )
        return acct_edit_stats(edit, values, online, field, value);
    if( strcmp(section, "varps") == 0 )
        return acct_edit_var(state, edit, values, online, 1, field, value);
    if( strcmp(section, "varbits") == 0 )
        return acct_edit_var(state, edit, values, online, 2, field, value);
    if( strcmp(section, "vars") == 0 )
        return acct_edit_var(state, edit, values, online, 0, field, value);
    if( strcmp(section, "inv") == 0 || strcmp(section, "worn") == 0 || strcmp(section, "bank") == 0 )
        return acct_edit_items(state, edit, values, online, section, field, value);
    if( strcmp(section, "quests") == 0 )
        return acct_edit_quests(state, edit, field, value);
    if( strcmp(section, "cheat") == 0 )
    {
        const char* line = value;
        while( *line == ' ' )
            line++;
        if( line[0] == ':' && line[1] == ':' )
            line += 2;
        if( !line[0] )
            return acct_edit_refuse(edit, "cheat takes the line to run");
        edit->queue_only = 1;
        acct_edit_cheat(edit, "%s", line);
        snprintf(edit->what, sizeof(edit->what), "::%s", line);
        return 1;
    }
    if( strcmp(section, "refresh") == 0 )
    {
        snprintf(edit->what, sizeof(edit->what), "refresh");
        return 1;
    }
    return acct_edit_refuse(edit,
        "unknown key '%s' (player, stats, varps, varbits, vars, inv, worn, bank, quests, cheat, refresh)", key);
}

/** Send one cheat for `row` on the control link; 1 when it went. */
static int
acct_send_cheat(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdJobsPlayer* row,
    const char* line)
{
    struct ToriRSMapEdBuf payload;
    int ok;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, row->account);
    ToriRSMapEd_PutString(&payload, line);
    ok = live_send(maped, TORIRSMAPED_CTL_CHEAT, &payload);
    ToriRSMapEd_BufFree(&payload);
    return ok;
}

/** ACCOUNT_SET / ACCOUNT_PLACE. */
static void
acct_handle_set(
    struct acct_state* state,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    const char* name,
    const char* key,
    const char* value)
{
    struct ToriRSMapEd* maped = state->maped;
    char live[ACCT_PATH_MAX];
    char file[64];
    const struct ToriRSMapEdJobsPlayer* row;
    struct acct_values values;
    struct acct_edit edit;
    static char cheats[ACCT_EDIT_CHEATS_MAX][200];
    int online;

    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_UNSUPPORTED, name, "no lane is bound (torirsmaped --live-dir)");
        return;
    }
    ToriRSMapEd_AcctFileName(name, file, sizeof(file));
    if( !file[0] )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, name, "'%s' is not an account name", name);
        return;
    }
    row = acct_online_row(maped, file);
    online = row != NULL;
    if( !acct_values_load(state, live, file, row, &values) && !online )
    {
        ToriRSMapEd_AcctSaveFree(&values.save);
        acct_status(maped, session, request, EDITOR_HOST_ABSENT, file, "no save %s/saves/%s.ini", live, file);
        return;
    }
    memset(&edit, 0, sizeof(edit));
    edit.cheats = cheats;
    if( !acct_edit_resolve(state, &edit, &values, online, key, value) )
    {
        ToriRSMapEd_AcctSaveFree(&values.save);
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, file, "%s", edit.why);
        return;
    }
    if( online )
    {
        int sent = 0;
        for( int i = 0; i < edit.cheat_count; i++ )
            sent += acct_send_cheat(maped, row, edit.cheats[i]);
        if( edit.cheat_count > 0 && sent < edit.cheat_count )
        {
            ToriRSMapEd_AcctSaveFree(&values.save);
            acct_status(maped, session, request, EDITOR_HOST_IO_ERROR, file, "the control link dropped: %d of %d cheat(s) sent",
                sent, edit.cheat_count);
            return;
        }
        (void)acct_refresh_request(state, live, file, row);
        if( edit.cheat_count == 0 )
            acct_status(maped, session, request, EDITOR_HOST_OK, file, "online: asked %s for a checkpoint", row->account);
        else if( edit.cheat_count == 1 )
            acct_status(maped, session, request, EDITOR_HOST_OK, file, "online: ::%s for %s (%s)", edit.cheats[0],
                row->account, edit.what);
        else
            acct_status(maped, session, request, EDITOR_HOST_OK, file, "online: ::%s and %d more for %s (%s)",
                edit.cheats[0], edit.cheat_count - 1, row->account, edit.what);
    }
    else if( edit.queue_only )
    {
        int queued = 0;
        for( int i = 0; i < edit.cheat_count; i++ )
            queued += acct_queue_add(state, file, edit.cheats[i]);
        if( queued > 0 )
            acct_registry_write(state);
        if( queued < edit.cheat_count )
            acct_status(maped, session, request, EDITOR_HOST_REFUSED, file,
                "the queue holds %d lines; %d of %d queued", TORIRSMAPED_ACCT_QUEUE_MAX, queued, edit.cheat_count);
        else
            acct_status(maped, session, request, EDITOR_HOST_OK, file, "queued for %s's next login: %s (%d line%s)",
                file, edit.what, queued, queued == 1 ? "" : "s");
    }
    else
    {
        char path[ACCT_PATH_MAX];
        acct_save_path(live, file, path, sizeof(path));
        if( edit.save_changed && !ToriRSMapEd_AcctSaveWrite(&values.save, path) )
        {
            ToriRSMapEd_AcctSaveFree(&values.save);
            acct_status(maped, session, request, EDITOR_HOST_IO_ERROR, file, "cannot write %s: %s", path, strerror(errno));
            return;
        }
        acct_status(maped, session, request, EDITOR_HOST_OK, file, "offline: %s saves/%s.ini (%s)",
            edit.save_changed ? "wrote" : "no change to", file, edit.what);
    }
    ToriRSMapEd_AcctSaveFree(&values.save);
    acct_broadcast_view(state, NULL, file);
    acct_broadcast_list(state, NULL);
}

/* ---- create, duplicate, delete, use -------------------------------------------- */

/** Write `file`'s save from `source` with `name = <display>`; drop any
 *  stale password record so the first login claims `password`. */
static int
acct_make_save(
    struct acct_state* state,
    const char* live,
    const char* source,
    const char* file,
    const char* display,
    const char* password,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdAcctSave save;
    char path[ACCT_PATH_MAX];
    char dir[ACCT_PATH_MAX];

    ToriRSMapEd_AcctSaveInit(&save);
    if( source[0] && !ToriRSMapEd_AcctSaveLoad(&save, source) )
    {
        snprintf(why, why_size, "cannot read %s: %s", source, strerror(errno));
        return 0;
    }
    if( !source[0] )
    {
        static const char fresh[] = "; ToriRSMapEd account (a new character).\n";
        ToriRSMapEd_AcctSaveParse(&save, fresh, sizeof(fresh) - 1);
        ToriRSMapEd_AcctSaveSet(&save, "player", "version", "1");
    }
    ToriRSMapEd_AcctSaveSet(&save, "player", "name", display);
    ToriRSMapEd_AcctSaveRemove(&save, "player", "display_name");
    snprintf(dir, sizeof(dir), "%s/saves", live);
    acct_mkdirs(dir);
    acct_save_path(live, file, path, sizeof(path));
    if( !ToriRSMapEd_AcctSaveWrite(&save, path) )
    {
        ToriRSMapEd_AcctSaveFree(&save);
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    ToriRSMapEd_AcctSaveFree(&save);
    acct_record_path(live, file, path, sizeof(path));
    (void)unlink(path);
    ToriRSMapEd_AcctSaveSet(&state->registry, "passwords", file, password);
    acct_registry_write(state);
    return 1;
}

static void
acct_handle_create(
    struct acct_state* state,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    const char* name,
    const char* password,
    const char* source_name,
    int duplicate)
{
    struct ToriRSMapEd* maped = state->maped;
    char live[ACCT_PATH_MAX];
    char file[64];
    char path[ACCT_PATH_MAX];
    char source[ACCT_PATH_MAX];
    char why[512];
    const char* problem;

    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_UNSUPPORTED, name, "no lane is bound (torirsmaped --live-dir)");
        return;
    }
    problem = ToriRSMapEd_AcctCredentialsProblem(name, password);
    if( problem )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, name, "%s", problem);
        return;
    }
    ToriRSMapEd_AcctFileName(name, file, sizeof(file));
    acct_save_path(live, file, path, sizeof(path));
    if( access(path, F_OK) == 0 )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, file, "there is an account %s already", file);
        return;
    }
    if( duplicate )
    {
        char source_file[64];
        ToriRSMapEd_AcctFileName(source_name, source_file, sizeof(source_file));
        acct_save_path(live, source_file, source, sizeof(source));
        if( !source_file[0] || access(source, F_OK) != 0 )
        {
            acct_status(maped, session, request, EDITOR_HOST_ABSENT, file, "no account %s to duplicate", source_name);
            return;
        }
    }
    else if( source_name[0] )
        snprintf(source, sizeof(source), "%s", source_name);
    else
        snprintf(source, sizeof(source), "%s", maped->jobs->lane.play_fixture);
    if( !acct_make_save(state, live, source, file, name, password, why, sizeof(why)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_IO_ERROR, file, "%s", why);
        return;
    }
    acct_status(maped, session, request, EDITOR_HOST_OK, file, "%s %s from %s (password set at its first login)",
        duplicate ? "duplicated as" : "created", file, source[0] ? source : "a new character");
    acct_broadcast_list(state, NULL);
    acct_broadcast_view(state, NULL, file);
}

static void
acct_handle_delete(
    struct acct_state* state,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    const char* name)
{
    struct ToriRSMapEd* maped = state->maped;
    char live[ACCT_PATH_MAX];
    char file[64];
    char path[ACCT_PATH_MAX];
    char play[64];

    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_UNSUPPORTED, name, "no lane is bound (torirsmaped --live-dir)");
        return;
    }
    ToriRSMapEd_AcctFileName(name, file, sizeof(file));
    if( !file[0] )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, name, "'%s' is not an account name", name);
        return;
    }
    if( acct_online_row(maped, file) )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, file, "%s is logged in: log it out first", file);
        return;
    }
    acct_save_path(live, file, path, sizeof(path));
    if( access(path, F_OK) != 0 )
    {
        acct_status(maped, session, request, EDITOR_HOST_ABSENT, file, "no account %s", file);
        return;
    }
    if( unlink(path) != 0 )
    {
        acct_status(maped, session, request, EDITOR_HOST_IO_ERROR, file, "cannot remove %s: %s", path, strerror(errno));
        return;
    }
    acct_record_path(live, file, path, sizeof(path));
    (void)unlink(path);
    ToriRSMapEd_AcctSaveRemove(&state->registry, "passwords", file);
    acct_queue_clear(state, file);
    acct_play_file(maped, play, sizeof(play));
    if( strcmp(play, file) == 0 && maped->jobs->lane.play_user[0] )
    {
        maped->jobs->lane.play_user[0] = '\0';
        maped->jobs->lane.play_pass[0] = '\0';
        ToriRSMapEd_AcctSaveRemove(&state->registry, "lane", "play_user");
        ToriRSMapEd_AcctSaveRemove(&state->registry, "lane", "play_pass");
        jobs_live_broadcast_play(maped);
    }
    acct_registry_write(state);
    acct_status(maped, session, request, EDITOR_HOST_OK, file, "deleted %s (its save, password record and queue)", file);
    acct_broadcast_list(state, NULL);
}

static void
acct_handle_use(
    struct acct_state* state,
    struct ToriRSMapEdSession* session,
    uint32_t request,
    const char* name)
{
    struct ToriRSMapEd* maped = state->maped;
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char live[ACCT_PATH_MAX];
    char file[64];
    char path[ACCT_PATH_MAX];
    char pass[64];

    if( !acct_live_dir(maped, live, sizeof(live)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_UNSUPPORTED, name, "no lane is bound (torirsmaped --live-dir)");
        return;
    }
    ToriRSMapEd_AcctFileName(name, file, sizeof(file));
    acct_save_path(live, file, path, sizeof(path));
    if( !file[0] || access(path, F_OK) != 0 )
    {
        acct_status(maped, session, request, EDITOR_HOST_ABSENT, file, "no account %s", name);
        return;
    }
    if( strlen(file) >= sizeof(jobs->lane.play_user) )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, file, "the play account name is at most %d characters",
            (int)sizeof(jobs->lane.play_user) - 1);
        return;
    }
    if( !acct_password_for(state, file, pass, sizeof(pass)) )
    {
        acct_status(maped, session, request, EDITOR_HOST_REFUSED, file,
            "the editor knows no password for %s: make the account here (New account) to use it for RUN", file);
        return;
    }
    snprintf(jobs->lane.play_user, sizeof(jobs->lane.play_user), "%s", file);
    snprintf(jobs->lane.play_pass, sizeof(jobs->lane.play_pass), "%s", pass);
    ToriRSMapEd_AcctSaveSet(&state->registry, "lane", "play_user", file);
    ToriRSMapEd_AcctSaveSet(&state->registry, "lane", "play_pass", pass);
    acct_registry_write(state);
    acct_status(maped, session, request, EDITOR_HOST_OK, file, "RUN and Play log in as %s from now on (a running play client keeps its login)", file);
    jobs_live_broadcast_play(maped);
    acct_broadcast_list(state, NULL);
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void
handle_accounts(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct acct_state* state = acct_state_for(maped);
    struct ToriRSMapEdReader reader;
    char name[64];
    char a[512];
    char b[512];

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_AccountsService(maped);
    if( type == TORIRSMAPED_REQ_ACCOUNTS_LIST )
    {
        if( length != 0 )
        {
            session->alive = 0;
            return;
        }
        acct_broadcast_list(state, session);
        return;
    }
    if( type == TORIRSMAPED_REQ_ACCOUNTS_QUESTS )
    {
        if( length != 0 )
        {
            session->alive = 0;
            return;
        }
        acct_send_quests(state, session);
        return;
    }
    ToriRSMapEd_GetString(&reader, name, sizeof(name));
    if( type == TORIRSMAPED_REQ_ACCOUNT_VIEW || type == TORIRSMAPED_REQ_ACCOUNT_DELETE
        || type == TORIRSMAPED_REQ_ACCOUNT_USE )
    {
        char file[64];
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            return;
        }
        if( type == TORIRSMAPED_REQ_ACCOUNT_VIEW )
        {
            ToriRSMapEd_AcctFileName(name, file, sizeof(file));
            acct_broadcast_view(state, session, file[0] ? file : name);
        }
        else if( type == TORIRSMAPED_REQ_ACCOUNT_DELETE )
            acct_handle_delete(state, session, type, name);
        else
            acct_handle_use(state, session, type, name);
        return;
    }
    if( type == TORIRSMAPED_REQ_ACCOUNT_CREATE || type == TORIRSMAPED_REQ_ACCOUNT_DUPLICATE )
    {
        ToriRSMapEd_GetString(&reader, a, sizeof(a));
        ToriRSMapEd_GetString(&reader, b, sizeof(b));
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            return;
        }
        if( type == TORIRSMAPED_REQ_ACCOUNT_CREATE )
            acct_handle_create(state, session, type, name, a, b, 0);
        else
            acct_handle_create(state, session, type, a, b, name, 1);
        return;
    }
    if( type == TORIRSMAPED_REQ_ACCOUNT_SET )
    {
        ToriRSMapEd_GetString(&reader, a, sizeof(a));
        ToriRSMapEd_GetString(&reader, b, sizeof(b));
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            return;
        }
        acct_handle_set(state, session, type, name, a, b);
        return;
    }
    if( type == TORIRSMAPED_REQ_ACCOUNT_PLACE )
    {
        int32_t x = (int32_t)ToriRSMapEd_GetU32(&reader);
        int32_t z = (int32_t)ToriRSMapEd_GetU32(&reader);
        int32_t level = (int32_t)ToriRSMapEd_GetU32(&reader);
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            return;
        }
        snprintf(a, sizeof(a), "%d %d %d", x, z, level);
        acct_handle_set(state, session, type, name, "player.pos", a);
        return;
    }
    TORIRS_ERR("torirsmaped: accounts: unknown request %u -- closing\n", type);
    session->alive = 0;
}

/* ------------------------------------------------------------------ */
/* Service                                                             */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_AccountsService(struct ToriRSMapEd* maped)
{
    struct acct_state* state;
    struct ToriRSMapEdJobs* jobs;
    char live[ACCT_PATH_MAX];
    time_t const now = time(NULL);
    int list_changed = 0;

    assert(maped);
    assert(maped->open);
    jobs = maped->jobs;
    assert(jobs);
    if( !acct_live_dir(maped, live, sizeof(live)) )
        return;
    state = acct_state_for(maped);
    if( !state->registry_loaded )
        acct_registry_load(state, live);

    /* The remembered play account, onto the lane (again, if the lane was
     * rebound under it). */
    {
        char user[64];
        char pass[64];
        if( ToriRSMapEd_AcctSaveGet(&state->registry, "lane", "play_user", user, sizeof(user)) && user[0]
            && ToriRSMapEd_AcctSaveGet(&state->registry, "lane", "play_pass", pass, sizeof(pass)) && pass[0]
            && strlen(user) < sizeof(jobs->lane.play_user) && strlen(pass) < sizeof(jobs->lane.play_pass)
            && (strcmp(jobs->lane.play_user, user) != 0 || strcmp(jobs->lane.play_pass, pass) != 0) )
        {
            snprintf(jobs->lane.play_user, sizeof(jobs->lane.play_user), "%s", user);
            snprintf(jobs->lane.play_pass, sizeof(jobs->lane.play_pass), "%s", pass);
            TORIRS_LOG("torirsmaped: accounts: the lane's play account is %s (accounts.ini)\n", user);
        }
    }

    /* Logins and logouts: the server's list against what was seen. */
    {
        struct acct_seen fresh[ACCT_SEEN_MAX];
        int fresh_count = 0;
        int const attached = jobs->live_state == TORIRSMAPED_LIVE_ATTACHED && jobs->players_known;
        for( int i = 0; attached && i < jobs->player_row_count && fresh_count < ACCT_SEEN_MAX; i++ )
        {
            const struct ToriRSMapEdJobsPlayer* row = &jobs->player_rows[i];
            struct acct_seen* seen = &fresh[fresh_count++];
            int known = 0;
            memset(seen, 0, sizeof(*seen));
            snprintf(seen->account, sizeof(seen->account), "%s", row->account);
            seen->slot = row->slot;
            seen->generation = row->generation;
            seen->since = now;
            for( int j = 0; j < state->seen_count && !known; j++ )
                if( strcmp(state->seen[j].account, row->account) == 0 && state->seen[j].slot == row->slot
                    && state->seen[j].generation == row->generation )
                {
                    seen->since = state->seen[j].since;
                    seen->flushed = state->seen[j].flushed;
                    known = 1;
                }
            if( !known )
                list_changed = 1;
        }
        if( fresh_count != state->seen_count )
            list_changed = 1;
        memcpy(state->seen, fresh, sizeof(fresh));
        state->seen_count = fresh_count;
    }

    /* Queued cheats go a few seconds after the login. */
    for( int i = 0; i < state->seen_count; i++ )
    {
        struct acct_seen* seen = &state->seen[i];
        char file[64];
        char key[128];
        char line[512];
        int sent = 0;
        const struct ToriRSMapEdJobsPlayer* row;
        if( seen->flushed || now - seen->since < ACCT_FLUSH_DELAY_S )
            continue;
        seen->flushed = 1;
        ToriRSMapEd_AcctFileName(seen->account, file, sizeof(file));
        if( !file[0] || acct_queue_count(state, file) == 0 )
            continue;
        row = acct_online_row(maped, file);
        if( !row )
            continue;
        while( acct_queue_nth(state, file, 0, key, sizeof(key), line, sizeof(line)) )
        {
            if( !acct_send_cheat(maped, row, line) )
                break;
            ToriRSMapEd_AcctSaveRemove(&state->registry, "queue", key);
            sent++;
        }
        acct_registry_write(state);
        acct_status(maped, NULL, 0, EDITOR_HOST_OK, file, "%s logged in: %d queued cheat(s) sent", file, sent);
        (void)acct_refresh_request(state, live, file, row);
        list_changed = 1;
    }

    /* Checkpoints that landed, or did not. */
    for( int i = 0; i < state->snapshot_count; i++ )
    {
        struct acct_snapshot* snap = &state->snapshots[i];
        char path[ACCT_PATH_MAX];
        if( !snap->pending )
            continue;
        acct_snapshot_path(live, snap, path, sizeof(path));
        if( access(path, F_OK) == 0 )
        {
            snap->pending = 0;
            snap->have = 1;
            acct_broadcast_view(state, NULL, snap->file);
            continue;
        }
        if( now - snap->sent > ACCT_REFRESH_TIMEOUT_S )
        {
            snap->pending = 0;
            acct_status(maped, NULL, 0, EDITOR_HOST_REFUSED, snap->file,
                "%s wrote no checkpoint (in a dialogue or a fight? see the Live log): showing the last save",
                snap->file);
            acct_broadcast_view(state, NULL, snap->file);
        }
    }

    if( list_changed )
        acct_broadcast_list(state, NULL);
}
