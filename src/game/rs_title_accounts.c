#include "game/rs_title_accounts.h"

#include <assert.h>

void
RS_TitleAccounts_Init(
    struct RS_TitleAccounts* accounts,
    int oauth,
    char const* path)
{
    assert(accounts);
    accounts->oauth = oauth;
    accounts->path = path;
    RS_Accounts_Init(&accounts->device);
}

int
RS_TitleAccounts_OAuth(struct RS_TitleAccounts const* accounts)
{
    assert(accounts);
    return accounts->oauth;
}

char const*
RS_TitleAccounts_Path(struct RS_TitleAccounts const* accounts)
{
    assert(accounts);
    return accounts->path;
}

int
RS_TitleAccounts_TakeDeviceLoad(struct RS_TitleAccounts* accounts)
{
    assert(accounts);
    return accounts->oauth && accounts->path && RS_AccountsLoad_TakeStart(&accounts->device_load);
}

int
RS_TitleAccounts_TakeSiteLoad(struct RS_TitleAccounts* accounts)
{
    assert(accounts);
    return accounts->oauth && RS_AccountsLoad_Done(&accounts->device_load) &&
           RS_AccountsLoad_TakeStart(&accounts->site_load);
}

void
RS_TitleAccounts_BuildCards(struct RS_TitleAccounts* accounts)
{
    int count = 0;

    assert(accounts);
    if( accounts->oauth && accounts->site.signed_in )
        for( int i = 0; i < accounts->site.count && count < RS_TITLE_ACCOUNT_CARDS; i++ )
        {
            accounts->cards[count].site = 1;
            accounts->cards[count].index = i;
            count++;
        }
    if( accounts->oauth )
        for( int i = 0; i < accounts->device.count && count < RS_TITLE_ACCOUNT_CARDS; i++ )
        {
            if( RS_SiteSession_Find(&accounts->site, accounts->device.list[i].name) >= 0 )
                continue;
            accounts->cards[count].site = 0;
            accounts->cards[count].index = i;
            count++;
        }
    accounts->card_count = count;
}

int
RS_TitleAccounts_CardCount(struct RS_TitleAccounts const* accounts)
{
    assert(accounts);
    return accounts->card_count;
}

char const*
RS_TitleAccounts_CardName(
    struct RS_TitleAccounts const* accounts,
    int card)
{
    assert(accounts);
    assert(card >= 0);
    assert(card < accounts->card_count);
    if( accounts->cards[card].site )
        return accounts->site.players[accounts->cards[card].index];
    return accounts->device.list[accounts->cards[card].index].name;
}

enum RS_AccountSignInGrant
RS_TitleAccounts_CardGrant(
    struct RS_TitleAccounts const* accounts,
    int card)
{
    assert(accounts);
    assert(card >= 0);
    assert(card < accounts->card_count);
    return accounts->cards[card].site ? RS_ACCOUNT_SIGNIN_SITE : RS_ACCOUNT_SIGNIN_REFRESH;
}

int
RS_TitleAccounts_CardIndex(
    struct RS_TitleAccounts const* accounts,
    int card)
{
    assert(accounts);
    assert(card >= 0);
    assert(card < accounts->card_count);
    return accounts->cards[card].index;
}
