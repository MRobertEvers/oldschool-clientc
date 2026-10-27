#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTLINK_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTLINK_H

/*
 * The world's line to the account server (accountserver/account_server.h):
 * what the world's account log (torirs_server_accountlog.h) queued, sent on,
 * and NEVER waited for.
 *
 *   tick -> account_log -> AccountLinkWrite -> <saves>/account_spool.tsv
 *                                                   |
 *        AccountLinkPump, every host pass: one non-blocking step of
 *        connect / send a batch / read "acked <seq>"  -> drop what was acked
 *
 * **The tick never blocks on it.** Writing is an append of a few lines to a
 * buffered file, flushed to the operating system once per tick and never
 * synced. Sending is a non-blocking socket the host's select() watches beside
 * its players'; every step does what the socket will take now and returns.
 * An account server that is down, slow or restarting costs the world nothing:
 * the records wait in the spool and go, in order, when it answers.
 *
 * **Nothing is lost or counted twice.** Each record carries a sequence number
 * that only rises; the account server applies a record once and acknowledges
 * the highest it holds, so a batch resent after a lost answer is harmless. The
 * spool keeps every record until it is acknowledged, across restarts of
 * either process; a world that starts finds its unsent records there and
 * sends them first. Once everything is acknowledged a long spool is cut back
 * to its header line, which carries the next number on.
 *
 * A new spool's first number is the clock in microseconds, so a deleted spool
 * can never restart below numbers the account server has already applied.
 */

#include "torirs_server_accountlog.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

/* The default account server, as account_server.h's ACCOUNT_SERVER_PORT. */
#define TORIRSSERVER_ACCOUNTLINK_PORT 8091

struct ToriRSServerAccountLink;

/**
 * The world's link: `<saves>/account_spool.tsv`, sending to
 * TORIRS_ACCOUNT_SERVER (`host:port`, default 127.0.0.1 and the port above)
 * as the source TORIRS_WORLD_NAME (default "world"). Aborts when the spool
 * cannot be opened: a world that silently kept no history would look like
 * nobody played.
 */
struct ToriRSServerAccountLink*
ToriRSServer_AccountLinkOpen(void);

/** Close the socket and the spool; unsent records stay in the spool. */
void
ToriRSServer_AccountLinkClose(struct ToriRSServerAccountLink* link);

/** Append `log`'s records to the spool and empty it. */
void
ToriRSServer_AccountLinkWrite(
    struct ToriRSServerAccountLink* link,
    struct ToriRSServerAccountLog* log);

/** Watch the link's socket in the host's select(): adds it to the sets it is
 *  waiting on, raising `*top`. */
void
ToriRSServer_AccountLinkWatch(
    struct ToriRSServerAccountLink* link,
    fd_set* readable,
    fd_set* writable,
    int* top);

/** One non-blocking step of sending; `now_ms` the host's clock. */
void
ToriRSServer_AccountLinkPump(
    struct ToriRSServerAccountLink* link,
    long now_ms);

/** At shutdown: keep pumping, waiting on the socket, for at most
 *  `budget_ms`. What is not acknowledged by then stays in the spool. */
void
ToriRSServer_AccountLinkDrain(
    struct ToriRSServerAccountLink* link,
    long budget_ms);

/** Records written and not yet acknowledged. */
long long
ToriRSServer_AccountLinkPending(struct ToriRSServerAccountLink const* link);

#endif
