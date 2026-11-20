#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_HISCORESLINK_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_HISCORESLINK_H

/*
 * The world's line to the hiscores server (hiscoresserver/hiscores_server.h):
 * what the world's hiscores log (torirs_server_hiscoreslog.h) queued, sent on,
 * signed (service/service_auth.h), and NEVER waited for.
 *
 *   tick -> hiscores_log -> HiscoresLinkWrite -> <saves>/hiscores_spool.tsv
 *                                                   |
 *        HiscoresLinkPump, every host pass: one non-blocking step of
 *        connect / send a batch / read "acked <seq>"  -> drop what was acked
 *
 * **The tick never blocks on it.** Writing is an append of a few lines to a
 * buffered file, flushed to the operating system once per tick and never
 * synced. Sending is a non-blocking socket the host's select() watches beside
 * its players'; every step does what the socket will take now and returns.
 * A hiscores server that is down, slow or restarting costs the world nothing:
 * the records wait in the spool and go, in order, when it answers.
 *
 * **Nothing is lost or counted twice.** Each record carries a sequence number
 * that only rises; the hiscores server applies a record once and acknowledges
 * the highest it holds, so a batch resent after a lost answer is harmless. The
 * spool keeps every record until it is acknowledged, across restarts of
 * either process; a world that starts finds its unsent records there and
 * sends them first. Once everything is acknowledged a long spool is cut back
 * to its header line, which carries the next number on.
 *
 * A new spool's first number is the clock in microseconds, so a deleted spool
 * can never restart below numbers the hiscores server has already applied.
 */

#include "torirs_server_hiscoreslog.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

/* The default hiscores server, as hiscores_server.h's HISCORES_SERVER_PORT. */
#define TORIRSSERVER_HISCORESLINK_PORT 8093

struct ToriRSServerHiscoresLink;

/**
 * The world's link: `<saves>/hiscores_spool.tsv` (`account_spool.tsv` before
 * the split, renamed on first open), sending to TORIRS_HISCORES_SERVER
 * (`host:port`, default 127.0.0.1 and the port above)
 * as the source TORIRS_WORLD_NAME (default "world"). Aborts when the spool
 * cannot be opened: a world that silently kept no history would look like
 * nobody played.
 *
 * The source is NOT the world's lease name (TORIRSSERVER_WORLD): the
 * hiscores server drops a record it has seen by the source's last number,
 * and a world resends its whole spool at every start, so a world whose
 * source changed has every record it ever spooled applied again (2026-11-19:
 * the live world's adventure log listed eight quests twice). Keep it.
 */
struct ToriRSServerHiscoresLink*
ToriRSServer_HiscoresLinkOpen(void);

/** Close the socket and the spool; unsent records stay in the spool. */
void
ToriRSServer_HiscoresLinkClose(struct ToriRSServerHiscoresLink* link);

/** Append `log`'s records to the spool and empty it. */
void
ToriRSServer_HiscoresLinkWrite(
    struct ToriRSServerHiscoresLink* link,
    struct ToriRSServerHiscoresLog* log);

/** Watch the link's socket in the host's select(): adds it to the sets it is
 *  waiting on, raising `*top`. */
void
ToriRSServer_HiscoresLinkWatch(
    struct ToriRSServerHiscoresLink* link,
    fd_set* readable,
    fd_set* writable,
    int* top);

/** One non-blocking step of sending; `now_ms` the host's clock. */
void
ToriRSServer_HiscoresLinkPump(
    struct ToriRSServerHiscoresLink* link,
    long now_ms);

/** At shutdown: keep pumping, waiting on the socket, for at most
 *  `budget_ms`. What is not acknowledged by then stays in the spool. */
void
ToriRSServer_HiscoresLinkDrain(
    struct ToriRSServerHiscoresLink* link,
    long budget_ms);

/** Records written and not yet acknowledged. */
long long
ToriRSServer_HiscoresLinkPending(struct ToriRSServerHiscoresLink const* link);

#endif
