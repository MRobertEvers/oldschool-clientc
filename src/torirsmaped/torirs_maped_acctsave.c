/*
 * The accounts module's pure pieces (torirs_maped_acctsave.h): the stat and
 * level tables, the name rules, the line-preserving save codec. Moved out of
 * torirs_maped_accounts.c unchanged; compiled into torirs_maped.c before it
 * (its static helpers are that file's too), and on its own by io_server.
 */

#include "torirs_maped_acctsave.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Replace `path` with `temp` in one step. POSIX rename does; Win32's rename
 * refuses an existing target, so MoveFileEx with REPLACE_EXISTING (as
 * platform_x_io.c and torirs_server_savefile.c do it). 0 on success. */
static int
acct_replace_file(
    const char* temp,
    const char* path)
{
#ifdef _WIN32
    return MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(temp, path);
#endif
}

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

void
ToriRSMapEd_AcctLoadoutName(
    const char* name,
    char* out,
    size_t out_size)
{
    size_t n = 0;

    assert(name);
    assert(out);
    assert(out_size > 0);
    for( const char* c = name; *c && n + 1 < out_size && n < TORIRSMAPED_ACCT_LOADOUT_NAME_MAX; c++ )
    {
        char ch = *c;
        if( ch >= 'A' && ch <= 'Z' )
            ch = (char)(ch - 'A' + 'a');
        if( ch == ' ' || ch == '-' )
            ch = '_';
        if( (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' )
            out[n++] = ch;
    }
    out[n] = '\0';
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
    if( acct_replace_file(temp, path) != 0 )
    {
        unlink(temp);
        return 0;
    }
    return 1;
}

