#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_SAVEFILE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_SAVEFILE_H

/*
 * The save directory and how a file in it is named and written.
 *
 * Split out of torirs_server_save.c so that a process with no world can keep
 * the same files: io_server's sign-in gate reads and writes the account
 * records (torirs_server_auth.h) under the same names, in the same directory,
 * with the same atomic write. Nothing here knows what a player is.
 *
 *   <TORIRSSERVER_SAVES, default "saves">/
 *     <name>.ini                 the character (torirs_server_save.h)
 *     accounts/<name>.ini        the player account: password and privileges
 *     accounts.sqlite            web logins for io_server's sign-in gate (a
 *                                separate account, not a player), invites,
 *                                hiscores, history (torirs_server_accounts.h)
 *     gate/<name>.ini            before accounts.sqlite: a web login, and
 *     gate/invites/<code>.ini    an invite; imported once, read no more
 *     gate/gate.key              the key the gate's ticket is signed with
 */

#include <stddef.h>

/** TORIRSSERVER_SAVES, or "saves". */
const char*
ToriRSServer_SaveDir(void);

/**
 * The file name a login name saves under: `[a-z0-9_]`, or "" when nothing is
 * left. Uppercase folds, `-` and space become `_`, everything else is dropped.
 * The character save and the account record share it, so one name is one
 * character is one password -- and two spellings that sanitise alike ARE the
 * same account.
 */
void
ToriRSServer_SaveFileName(
    const char* display_name,
    char* name,
    size_t cap);

/**
 * Where a player's save lives. Returns a pointer to static storage.
 *
 * The name is sanitised to `[a-z0-9_]` — it comes off the login screen, so it
 * is attacker-controlled, and a name of `../../etc/passwd` must not become a
 * path. An empty result means the name was unusable and nothing should be
 * written.
 */
const char*
ToriRSServer_SavePath(const char* display_name);

/**
 * Where a login name's account record lives: `<save dir>/accounts/<name>.ini`,
 * the same sanitised name ToriRSServer_SavePath uses, so a password and a
 * character are keyed identically. Static storage; "" for an unusable name.
 * See torirs_server_auth.h.
 */
const char*
ToriRSServer_AccountPath(const char* display_name);

/**
 * Where a web login for the sign-in gate lived before the account database:
 * `<save dir>/gate/<name>.ini`, sanitised the same way. Only the database's
 * one-time import reads it (torirs_server_accounts.h). Static storage; "" for
 * an unusable name.
 */
const char*
ToriRSServer_GateAccountPath(const char* name);

/**
 * Replace `path` with `text`, the way a save is written: the directory is
 * created, the text goes to `<path>.tmp`, and the temp is renamed over the
 * file, so a crash leaves the old file or the new one and never half of
 * either. Returns 1 on success; a failure is reported on stderr.
 */
int
ToriRSServer_SaveWriteText(
    const char* path,
    const char* text);

/** `mkdir -p` the directory `path` is in; the save directory when it names none. */
void
ToriRSServer_SaveMkdirParent(const char* path);

/** Rename `temp` over `path`, replacing it atomically (MoveFileEx on Windows,
 *  whose rename() refuses an existing destination). 0 on success. */
int
ToriRSServer_SaveReplace(
    const char* temp,
    const char* path);

#endif
