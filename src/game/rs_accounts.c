/*
 * The device's signed-in accounts. See rs_accounts.h.
 */
#include "game/rs_accounts.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RS_ACCOUNTS_DEFAULT_PATH "accounts.ini"

char const*
RS_Accounts_Path(void)
{
    char const* configured = getenv("TORIRS_ACCOUNTS");

    if( !configured )
        return RS_ACCOUNTS_DEFAULT_PATH;
    return *configured ? configured : NULL;
}

void
RS_Accounts_Init(struct RS_Accounts* accounts)
{
    assert(accounts);
    memset(accounts, 0, sizeof(*accounts));
}

/* The server's key for a name (torirs_server_savefile.c's file name rule):
 * lowercase alphanumerics, with '-', '_' and ' ' as '_' and the rest gone. */
static void
account_key(
    char const* name,
    char* out,
    size_t cap)
{
    size_t used = 0;

    for( ; *name && used + 1 < cap; name++ )
    {
        unsigned char ch = (unsigned char)*name;

        if( isalnum(ch) )
            out[used++] = (char)tolower(ch);
        else if( ch == '_' || ch == '-' || ch == ' ' )
            out[used++] = '_';
    }
    out[used] = '\0';
}

int
RS_Accounts_Find(
    struct RS_Accounts const* accounts,
    char const* name)
{
    char want[RS_ACCOUNTS_NAME_LEN];

    assert(accounts);
    assert(name);
    account_key(name, want, sizeof(want));
    if( !want[0] )
        return -1;
    for( int i = 0; i < accounts->count; i++ )
    {
        char have[RS_ACCOUNTS_NAME_LEN];

        account_key(accounts->list[i].name, have, sizeof(have));
        if( strcmp(have, want) == 0 )
            return i;
    }
    return -1;
}

void
RS_Accounts_Forget(
    struct RS_Accounts* accounts,
    int index)
{
    assert(accounts);
    assert(index >= 0 && index < accounts->count);
    memmove(&accounts->list[index], &accounts->list[index + 1],
            (size_t)(accounts->count - index - 1) * sizeof(accounts->list[0]));
    accounts->count--;
    memset(&accounts->list[accounts->count], 0, sizeof(accounts->list[0]));
}

void
RS_Accounts_Remember(
    struct RS_Accounts* accounts,
    char const* name,
    char const* refresh_token)
{
    int existing;

    assert(accounts);
    assert(name && name[0]);
    assert(refresh_token && refresh_token[0]);
    assert(strlen(refresh_token) < RS_ACCOUNTS_TOKEN_LEN);
    existing = RS_Accounts_Find(accounts, name);
    if( existing >= 0 )
        RS_Accounts_Forget(accounts, existing);
    else if( accounts->count == RS_ACCOUNTS_MAX )
        accounts->count--;
    memmove(&accounts->list[1], &accounts->list[0],
            (size_t)accounts->count * sizeof(accounts->list[0]));
    accounts->count++;
    snprintf(accounts->list[0].name, sizeof(accounts->list[0].name), "%s", name);
    snprintf(accounts->list[0].refresh_token, sizeof(accounts->list[0].refresh_token), "%s",
             refresh_token);
}

static void
trim(char* text)
{
    size_t len = strlen(text);
    size_t start = 0;

    while( len > 0 && isspace((unsigned char)text[len - 1]) )
        text[--len] = '\0';
    while( start < len && isspace((unsigned char)text[start]) )
        start++;
    if( start > 0 )
        memmove(text, text + start, len - start + 1);
}

/* Close the section being read: keep it when it is a whole account. */
static void
section_end(
    struct RS_Accounts* accounts,
    struct RS_Account* pending,
    int in_account)
{
    if( in_account && pending->name[0] && pending->refresh_token[0] &&
        accounts->count < RS_ACCOUNTS_MAX && RS_Accounts_Find(accounts, pending->name) < 0 )
        accounts->list[accounts->count++] = *pending;
    memset(pending, 0, sizeof(*pending));
}

void
RS_Accounts_Decode(
    struct RS_Accounts* accounts,
    void const* data,
    int size)
{
    static struct RS_Account pending;
    char line[RS_ACCOUNTS_TOKEN_LEN + 64];
    char const* text = (char const*)data;
    int in_account = 0;
    int at = 0;

    assert(accounts);
    assert(data || size == 0);
    RS_Accounts_Init(accounts);
    memset(&pending, 0, sizeof(pending));
    while( at < size )
    {
        int end = at;
        char* eq;

        while( end < size && text[end] != '\n' )
            end++;
        if( end - at < (int)sizeof(line) )
        {
            memcpy(line, text + at, (size_t)(end - at));
            line[end - at] = '\0';
            trim(line);
            if( line[0] == '[' )
            {
                section_end(accounts, &pending, in_account);
                in_account = strcmp(line, "[account]") == 0;
            }
            else if( in_account && line[0] != ';' && (eq = strchr(line, '=')) != NULL )
            {
                *eq = '\0';
                trim(line);
                trim(eq + 1);
                if( strcmp(line, "name") == 0 )
                    snprintf(pending.name, sizeof(pending.name), "%s", eq + 1);
                else if( strcmp(line, "refresh") == 0 && strlen(eq + 1) < RS_ACCOUNTS_TOKEN_LEN )
                    snprintf(pending.refresh_token, sizeof(pending.refresh_token), "%s", eq + 1);
            }
        }
        at = end + 1;
    }
    section_end(accounts, &pending, in_account);
}

void
RS_Accounts_Encode(
    struct RS_Accounts const* accounts,
    void** out_data,
    int* out_size)
{
    int cap = 256 + accounts->count * (RS_ACCOUNTS_NAME_LEN + RS_ACCOUNTS_TOKEN_LEN + 64);
    char* text;
    int used;

    assert(accounts);
    assert(out_data);
    assert(out_size);
    text = (char*)ToriMem_Alloc(ToriMem_Process(), (size_t)cap);
    assert(text);
    used = snprintf(text, (size_t)cap,
                    "; The accounts this device has signed in with, most recent first.\n"
                    "; Each refresh token signs its account in again; delete a section to\n"
                    "; forget that account here.\n");
    for( int i = 0; i < accounts->count; i++ )
        used += snprintf(text + used, (size_t)(cap - used), "\n[account]\nname = %s\nrefresh = %s\n",
                         accounts->list[i].name, accounts->list[i].refresh_token);
    assert(used < cap);
    *out_data = text;
    *out_size = used;
}
