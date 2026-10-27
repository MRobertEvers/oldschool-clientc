#ifndef SRC_GAME_TASK_ACCOUNTS_IO_H
#define SRC_GAME_TASK_ACCOUNTS_IO_H

/*
 * The account flow's trips across the platform seam: the saved accounts file
 * (FILE_READ / FILE_WRITE, like the preferences), and the sign-in itself --
 * the deob's launcher login, as two requests to the account server:
 *
 *   auth/token   grant_type=refresh_token (a Play Now) or =password (the form)
 *                -> access_token, a rotated refresh_token, name
 *   auth/play    access_token -> {"token": one-time login}
 *
 * Each request is a SCRIPT item whose path is `auth/<endpoint>` and whose data
 * LENDS the form-encoded body -- the launch service's convention
 * (platform_x_io.c), so every executor and the browser's item ABI carry it
 * unchanged. The browser executor POSTs it to the page's own origin, where the
 * gate's cookie rides along (platform_web_io.js); the desktop one to
 * TORIRS_AUTH_URL with TORIRS_GATE_COOKIE (platform_x_io.c). The answer
 * replaces the body with the response's JSON.
 */

#include "game/rs_accounts.h"

struct ToriRS_Task;

enum RS_AccountSignInGrant
{
    /** The credential form: username and password. */
    RS_ACCOUNT_SIGNIN_PASSWORD = 0,
    /** A Play Now: the account's saved refresh token. */
    RS_ACCOUNT_SIGNIN_REFRESH,
};

/** One sign-in, owned by the app. The task fills the second half and sets
 *  `done` last. */
struct RS_AccountSignIn
{
    enum RS_AccountSignInGrant grant;
    char username[RS_ACCOUNTS_NAME_LEN];
    char password[64];
    char refresh_token[RS_ACCOUNTS_TOKEN_LEN];

    int done;
    /** 1: `name`, `otl` and `new_refresh_token` are good, dial the game. */
    int ok;
    /** The refresh token was refused (invalid_grant): forget the account. */
    int refresh_refused;
    char name[RS_ACCOUNTS_NAME_LEN];
    char otl[64];
    char new_refresh_token[RS_ACCOUNTS_TOKEN_LEN];
    /** Why it failed, for the title's message line. */
    char error[160];
};

/** Read the accounts file into `accounts` (empty when there is none) and set
 *  `*loaded` when done. */
struct ToriRS_Task*
CreateTask_AccountsLoad(
    struct RS_Accounts* accounts,
    char const* path,
    int* loaded);

/** Write a snapshot of `accounts`. */
struct ToriRS_Task*
CreateTask_AccountsSave(
    struct RS_Accounts const* accounts,
    char const* path);

/** Run `signin` (whose first half the caller filled) to its one-time login,
 *  or to an error. The caller keeps `signin` alive until `done`. */
struct ToriRS_Task*
CreateTask_AccountSignIn(struct RS_AccountSignIn* signin);

#endif
