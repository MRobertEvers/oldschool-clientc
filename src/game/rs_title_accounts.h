#ifndef SRC_GAME_RS_TITLE_ACCOUNTS_H
#define SRC_GAME_RS_TITLE_ACCOUNTS_H

/*
 * The account flow as the title screen holds it, on a world whose manifest
 * says `[net:boot] auth=oauth`: the accounts this device has signed in with,
 * the site session and the players linked under it, the one sign-in in
 * flight, and the Play Now cards those make.
 *
 * It decides three things. The device's file is read once, and only on an
 * account-server world with somewhere to keep it; the site session is asked
 * once, and only after the device's file has landed, because answering it
 * edits that file. And the cards: the site's linked players first, in its
 * order, then the device's own that are not among them, at most
 * RS_TITLE_ACCOUNT_CARDS, and none at all off an account-server world.
 *
 * `oauth` 0 leaves the password form exactly as it was: no loads, no cards.
 *
 * This does not queue a load, run a sign-in, lay out a card or know which
 * screen is up. The caller asks at its own moments, queues the tasks with the
 * records it is handed, rebuilds the title when a latch says so, and places
 * the cards it is told about.
 */

#include "game/rs_accounts.h"

/* Play Now cards the title has room for; the device may hold more
 * (RS_ACCOUNTS_MAX), and the most recently played are the ones shown. */
#define RS_TITLE_ACCOUNT_CARDS 4

/** What one Play Now card plays: a linked account (`site` 1, an index into
 *  the site session's players) or one of the device's (`site` 0, into the
 *  device's list). */
struct RS_TitleCard
{
    int site;
    int index;
};

struct RS_TitleAccounts
{
    int oauth;
    /** Where the device's accounts are kept; NULL keeps nothing
     *  (TORIRS_ACCOUNTS=""). */
    char const* path;
    struct RS_Accounts device;
    struct RS_AccountsLoad device_load;
    /** The gate's web login and its linked players, asked of the account
     *  server once at boot. */
    struct RS_SiteSession site;
    struct RS_AccountsLoad site_load;
    struct RS_AccountSignIn signin;
    /** Built by RS_TitleAccounts_BuildCards, in card order. */
    struct RS_TitleCard cards[RS_TITLE_ACCOUNT_CARDS];
    int card_count;
};

/** Arm the flow: whether this world signs in through the account server, and
 *  where the device's accounts are kept (NULL for nowhere). The device's list
 *  starts empty; nothing else is touched. */
void
RS_TitleAccounts_Init(
    struct RS_TitleAccounts* accounts,
    int oauth,
    char const* path);

int
RS_TitleAccounts_OAuth(struct RS_TitleAccounts const* accounts);

char const*
RS_TitleAccounts_Path(struct RS_TitleAccounts const* accounts);

/** 1 exactly once, on an account-server world with a path: read the device's
 *  file now (into `device`, setting RS_AccountsLoad_DoneFlag(&device_load)). */
int
RS_TitleAccounts_TakeDeviceLoad(struct RS_TitleAccounts* accounts);

/** 1 exactly once, on an account-server world, once the device's file has
 *  landed: ask for the site session now. */
int
RS_TitleAccounts_TakeSiteLoad(struct RS_TitleAccounts* accounts);

/** Rebuild the cards from the device's list and the site session. */
void
RS_TitleAccounts_BuildCards(struct RS_TitleAccounts* accounts);

int
RS_TitleAccounts_CardCount(struct RS_TitleAccounts const* accounts);

/** The name on card `card` (< the card count). */
char const*
RS_TitleAccounts_CardName(
    struct RS_TitleAccounts const* accounts,
    int card);

/** The grant that plays card `card`: SITE for a linked account, REFRESH for
 *  one of the device's. */
enum RS_AccountSignInGrant
RS_TitleAccounts_CardGrant(
    struct RS_TitleAccounts const* accounts,
    int card);

/** The index card `card` plays, in the list its grant names. */
int
RS_TitleAccounts_CardIndex(
    struct RS_TitleAccounts const* accounts,
    int card);

#endif
