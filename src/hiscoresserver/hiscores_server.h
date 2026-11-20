#ifndef SRC_HISCORESSERVER_HISCORES_SERVER_H
#define SRC_HISCORESSERVER_HISCORES_SERVER_H

/*
 * The hiscores server: the characters' records -- stats, the hiscores, the
 * experience history, the sessions, the adventure log -- and the one process
 * that opens their database (hiscores_db.h).
 *
 * Each world SENDS its records here and never waits for an answer
 * (torirs_server_hiscoreslink.h); the website reads the pages
 * (hiscores_client.h), and the website and `torirsserver --account admin` set
 * whether a player is ranked (staff are not). Nothing here is on the login
 * path: a hiscores server that is down costs a world nothing, its records
 * waiting in the world's spool, and the site says the hiscores are not
 * answering.
 *
 * Its routes, under `/svc/hiscores` on a ServiceHost (service/service_host.h),
 * signed (service/service_auth.h); in process, the handler directly. Every
 * answer is lines of tab-separated fields (hiscores_proto.h).
 *
 *   POST /events?source=<world>   records, one per line -> acked <seq>
 *   POST /player/ranked  name, ranked              -> result 1
 *   GET  /view/player?key=&history=0|1             -> hiscores_proto.h's view
 *   GET  /view/hiscores?category=&first=&max=      -> ranked <n>, row ...
 *
 * A body that is not what the route takes is a 400 with `error <why>`.
 */

#include "service/http_server.h"

#define HISCORES_SERVER_PORT 8093
/* The most records one POST /events applies. */
#define HISCORES_SERVER_BATCH_MAX 512

/** The server's HttpHandler (`user` unused). Opens the database on first
 *  use. One request at a time: its callers' HttpServer serves one, and a
 *  process hosting it in its own does too. */
void
HiscoresServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
