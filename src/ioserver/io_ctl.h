#ifndef SRC_IOSERVER_IO_CTL_H
#define SRC_IOSERVER_IO_CTL_H

/*
 * The game server's control link (torirsserver/torirs_server_control.h), as
 * the account site uses it for the staff player editor (io_site.c
 * /admin/players/<name>): who is online, a cheat run as a player, and a
 * player logged out. One short loopback connection per call, opened with a
 * gate ticket minted for the staff login asking -- the game refuses a HELLO
 * from anyone else (ToriRSServer_AuthLoginIsStaff) -- and closed again.
 *
 * Blocking, with a short timeout, on the handler's worker: the peer is the
 * world on this machine answering at its next tick, so a call costs about one
 * tick. The port is TORIRS_GAME_PORT, else 43594 (the deploy's game port).
 */

#include <stddef.h>

#define IO_CTL_NAME_MAX 64
#define IO_CTL_PLAYERS_MAX 256
#define IO_CTL_LINES_MAX 16
#define IO_CTL_LINE_MAX 256

struct IoCtlPlayer
{
    char account[IO_CTL_NAME_MAX];
    char display[IO_CTL_NAME_MAX];
    int x;
    int z;
    int level;
};

struct IoCtlPlayers
{
    int count;
    struct IoCtlPlayer rows[IO_CTL_PLAYERS_MAX];
};

/** What a CHEAT or LOGOUT said back: its lines, in order. */
struct IoCtlReply
{
    int count;
    char lines[IO_CTL_LINES_MAX][IO_CTL_LINE_MAX];
};

enum IoCtlResult
{
    IO_CTL_OK = 0,
    /** Nothing listens: the game server is not running. Nobody is online. */
    IO_CTL_DOWN,
    /** It answered but refused the HELLO, or broke off: say so, decide nothing. */
    IO_CTL_REFUSED,
};

/** Everyone logged in now, as the game's CTL_FACT_PLAYERS lists them. */
enum IoCtlResult
IoCtl_Players(
    char const* staff,
    struct IoCtlPlayers* out);

/** The row whose account (or display name) keys to `key`
 *  (ToriRSServer_SaveFileName), or NULL. */
struct IoCtlPlayer const*
IoCtl_Find(
    struct IoCtlPlayers const* players,
    char const* key);

/** Run `line` (no "::") as the logged-in `player` (a name the game's list
 *  gave). The game's lines, ending "cheat: ran" / "cheat: refused" /
 *  "cheat: unknown command" (or "no player named ..."), into `out`. */
enum IoCtlResult
IoCtl_Cheat(
    char const* staff,
    char const* player,
    char const* line,
    struct IoCtlReply* out);

/** Have the game write logged-in `player` as a save to
 *  `<saves>/snapshots/<key>.ini` now (CTL_SNAPSHOT): their live state. */
enum IoCtlResult
IoCtl_Snapshot(
    char const* staff,
    char const* player,
    struct IoCtlReply* out);

/** End `player`'s session as their Logout button does; the game's teardown
 *  writes the save afterwards, on its own time. */
enum IoCtlResult
IoCtl_Logout(
    char const* staff,
    char const* player,
    struct IoCtlReply* out);

#endif
