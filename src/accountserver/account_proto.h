#ifndef SRC_ACCOUNTSERVER_ACCOUNT_PROTO_H
#define SRC_ACCOUNTSERVER_ACCOUNT_PROTO_H

/*
 * What the account server (account_server.h) and its clients
 * (account_client.h) agree on: its sizes, and an account as its listings
 * answer it. The wire is the servers' shared shape (service/service_form.h).
 *
 * The account server is the LOGIN server: a PLAYER account is a character --
 * its password, its admin and record flags, which web login it is linked to,
 * its save, its display name -- and, since there may be more than one world,
 * the one place that says which world a character is logged in to. A USER
 * account is a web login, the website's (websiteserver/website_users_db.h): a
 * bucket player accounts are linked into, nothing a world logs in with.
 */

#include "service/service_form.h"

#include <stddef.h>

/** Its routes are /svc/account on a ServiceHost; alone, it serves them on
 *  127.0.0.1 here. */
#define ACCOUNT_SERVER_PORT 8091

/**
 * A lease: one world's hold on one character, and with it the character's
 * save. Granted at the login's check; held, with the save on the world's own
 * disk, until the world hands the save back at the logout (`/logout`). It does
 * not lapse -- see account_lease.h.
 */
/** A lease's token: 16 random bytes as hex. */
#define ACCOUNT_LEASE_TOKEN_HEX 32
/** A world's name: TORIRSSERVER_WORLD, or "<host>:<port>". */
#define ACCOUNT_WORLD_MAX 64
/** A world's boot: 8 random bytes as hex, new each time it starts, so its
 *  `/hello` -- which may arrive late, after a retry -- lets go of only what an
 *  EARLIER run of it held. */
#define ACCOUNT_BOOT_HEX 16
/** A character as the save is keyed (ToriRSServer_SaveFileName). */
#define ACCOUNT_KEY_MAX 64
/** The largest save the server takes: a save is a few KB; a full bank, a
 *  house and every varp stay well under this. */
#define ACCOUNT_SAVE_MAX (4 * 1024 * 1024)

/** One player account, as GET /account and GET /accounts answer it. */
struct AccountPlayerInfo
{
    char key[ACCOUNT_KEY_MAX];
    int admin;
    int record;
    /** ToriRSServer_AuthManagedBy's: 1 linked to `owner`, 0 not, -1 the
     *  record could not be read. */
    int linked;
    char owner[ACCOUNT_KEY_MAX];
    /** The world that has the character, and its save, now; "" when none. */
    char world[ACCOUNT_WORLD_MAX];
};

#endif
