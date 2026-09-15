#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_EMBED_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_EMBED_H

/*
 * ToriRSMapEd hosted inside another process, with no socket anywhere — the
 * same shape as torirs_server_embed.h, because it earns the same property:
 * an embedded session exercises the real wire protocol, not a shortcut
 * around it. The bytes crossing these calls are byte-for-byte what the
 * socket daemon would carry.
 *
 *      client request  --->  ToriRSMapEd_EmbedWrite   (client -> server)
 *      client fact     <---  ToriRSMapEd_EmbedRead    (server -> client)
 *
 * with ToriRSMapEd_EmbedPump in between to let the server act.
 *
 * Unlike the game server there is no process-wide "one embed" rule: the game
 * server's cache and content tables are file-scope, ToriRSMapEd's state is
 * all inside the handle. Two embeds over one content tree behave like two
 * daemons over it — the second finds the session lock held and runs
 * read-only, which is the correct answer rather than a hazard.
 *
 * Threading: none. Every call from one thread; the server acts only inside
 * ToriRSMapEd_EmbedPump. There is no tick — the map editor's server has no
 * simulation, so pumping is purely request-driven.
 */

#include <stdint.h>

enum
{
    /**
     * Connections one embedded server can hold. A client is one stateful
     * connection and a process may hold several — a viewer connection, a
     * controller connection, a spare for tooling — which is the point of the
     * array. At or below TORIRSMAPED_SESSION_MAX so a connect that succeeds
     * always gets a session slot.
     */
    TORIRSMAPED_EMBED_CLIENT_MAX = 4,
};

struct ToriRSMapEdEmbed;

/**
 * Open one embedded server over a content tree, with client 0 already
 * connected. `repo_root` NULL disables baking. Returns NULL only when the
 * process is out of memory — a bad tree serves errors instead of refusing
 * to start, exactly like the daemon.
 */
struct ToriRSMapEdEmbed*
ToriRSMapEd_EmbedStart(
    const char* content_dir,
    const char* repo_root);

/** Open another connection to the same server. Returns its client id, or -1
 *  when every slot is taken. */
int
ToriRSMapEd_EmbedConnect(struct ToriRSMapEdEmbed* embed);

/** Close one client. Returns 1 if a client was closed. */
int
ToriRSMapEd_EmbedDisconnect(
    struct ToriRSMapEdEmbed* embed,
    int client_id);

void
ToriRSMapEd_EmbedStop(struct ToriRSMapEdEmbed* embed);

/** Client -> server. Returns `len`, or -1 once the session is gone. */
int
ToriRSMapEd_EmbedWrite(
    struct ToriRSMapEdEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len);

/** Server -> client. Returns the byte count, 0 when nothing is pending, or
 *  -1 once the session is gone and drained. */
int
ToriRSMapEd_EmbedRead(
    struct ToriRSMapEdEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max);

/** Bytes waiting to be read. */
int
ToriRSMapEd_EmbedPending(
    const struct ToriRSMapEdEmbed* embed,
    int client_id);

/** Let the server act on whatever every client wrote. Returns the number of
 *  sessions still alive. */
int
ToriRSMapEd_EmbedPump(struct ToriRSMapEdEmbed* embed);

#endif
