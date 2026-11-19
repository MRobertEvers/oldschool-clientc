/*
 * The title's account flow (game/rs_title_accounts.h): when the device's file
 * and the site session are asked for, and which accounts become the Play Now
 * cards, in which order.
 *
 * Run: make -C src test-rs-title-accounts
 */

#include "game/rs_title_accounts.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
fresh(
    struct RS_TitleAccounts* accounts,
    int oauth,
    char const* path)
{
    memset(accounts, 0, sizeof(*accounts));
    RS_TitleAccounts_Init(accounts, oauth, path);
}

static void
link_player(
    struct RS_SiteSession* site,
    char const* name)
{
    site->signed_in = 1;
    RS_SiteSession_Add(site, name);
}

/* Off an account-server world, or with nowhere to keep it, nothing is read. */
static void
test_device_load_needs_oauth_and_a_path(void)
{
    struct RS_TitleAccounts accounts;

    fresh(&accounts, 0, "accounts.ini");
    assert(!RS_TitleAccounts_OAuth(&accounts));
    assert(!RS_TitleAccounts_TakeDeviceLoad(&accounts));
    assert(!accounts.device_load.started);

    fresh(&accounts, 1, NULL);
    assert(RS_TitleAccounts_Path(&accounts) == NULL);
    assert(!RS_TitleAccounts_TakeDeviceLoad(&accounts));

    fresh(&accounts, 1, "accounts.ini");
    assert(strcmp(RS_TitleAccounts_Path(&accounts), "accounts.ini") == 0);
    assert(RS_TitleAccounts_TakeDeviceLoad(&accounts));
    assert(!RS_TitleAccounts_TakeDeviceLoad(&accounts));
}

/* The site session waits for the device's file, then is asked once. */
static void
test_site_load_waits_for_the_device_file(void)
{
    struct RS_TitleAccounts accounts;

    fresh(&accounts, 1, "accounts.ini");
    assert(RS_TitleAccounts_TakeDeviceLoad(&accounts));
    assert(!RS_TitleAccounts_TakeSiteLoad(&accounts));
    *RS_AccountsLoad_DoneFlag(&accounts.device_load) = 1;
    assert(RS_TitleAccounts_TakeSiteLoad(&accounts));
    assert(!RS_TitleAccounts_TakeSiteLoad(&accounts));

    /* Off an account-server world, not even a landed file asks. */
    fresh(&accounts, 0, "accounts.ini");
    accounts.device_load.done = 1;
    assert(!RS_TitleAccounts_TakeSiteLoad(&accounts));
}

/* Linked players first, then the device's own not among them. */
static void
test_cards_linked_first_without_repeats(void)
{
    struct RS_TitleAccounts accounts;

    fresh(&accounts, 1, "accounts.ini");
    RS_Accounts_Remember(&accounts.device, "Carol", "rt-c");
    RS_Accounts_Remember(&accounts.device, "Alice", "rt-a");
    link_player(&accounts.site, "alice");
    link_player(&accounts.site, "Bob");
    RS_TitleAccounts_BuildCards(&accounts);
    assert(RS_TitleAccounts_CardCount(&accounts) == 3);
    assert(strcmp(RS_TitleAccounts_CardName(&accounts, 0), "alice") == 0);
    assert(RS_TitleAccounts_CardGrant(&accounts, 0) == RS_ACCOUNT_SIGNIN_SITE);
    assert(RS_TitleAccounts_CardIndex(&accounts, 0) == 0);
    assert(strcmp(RS_TitleAccounts_CardName(&accounts, 1), "Bob") == 0);
    assert(strcmp(RS_TitleAccounts_CardName(&accounts, 2), "Carol") == 0);
    assert(RS_TitleAccounts_CardGrant(&accounts, 2) == RS_ACCOUNT_SIGNIN_REFRESH);
    assert(RS_TitleAccounts_CardIndex(&accounts, 2) == 1);
}

/* A site that is not signed in contributes no cards, though it names players. */
static void
test_cards_ignore_a_signed_out_site(void)
{
    struct RS_TitleAccounts accounts;

    fresh(&accounts, 1, "accounts.ini");
    RS_Accounts_Remember(&accounts.device, "Dave", "rt-d");
    link_player(&accounts.site, "Erin");
    accounts.site.signed_in = 0;
    RS_TitleAccounts_BuildCards(&accounts);
    assert(RS_TitleAccounts_CardCount(&accounts) == 1);
    assert(strcmp(RS_TitleAccounts_CardName(&accounts, 0), "Dave") == 0);
}

/* At most four, and none off an account-server world. */
static void
test_cards_capped_and_oauth_only(void)
{
    struct RS_TitleAccounts accounts;
    char name[16];

    fresh(&accounts, 1, "accounts.ini");
    for( int i = 0; i < 6; i++ )
    {
        snprintf(name, sizeof(name), "Player%d", i);
        RS_Accounts_Remember(&accounts.device, name, "rt");
    }
    RS_TitleAccounts_BuildCards(&accounts);
    assert(RS_TitleAccounts_CardCount(&accounts) == RS_TITLE_ACCOUNT_CARDS);
    assert(strcmp(RS_TitleAccounts_CardName(&accounts, 0), "Player5") == 0);

    accounts.oauth = 0;
    RS_TitleAccounts_BuildCards(&accounts);
    assert(RS_TitleAccounts_CardCount(&accounts) == 0);
}

int
main(void)
{
    test_device_load_needs_oauth_and_a_path();
    test_site_load_waits_for_the_device_file();
    test_cards_linked_first_without_repeats();
    test_cards_ignore_a_signed_out_site();
    test_cards_capped_and_oauth_only();
    printf("rs_title_accounts_test: ok\n");
    return 0;
}
