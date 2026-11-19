#ifndef SRC_HISCORESSERVER_HISCORES_CLIENT_H
#define SRC_HISCORESSERVER_HISCORES_CLIENT_H

/*
 * Asking the hiscores server (hiscores_server.h): the website's pages, and
 * `torirsserver --account admin`. Blocking, so for a page load or an
 * operator's command, never a world's tick, which only sends
 * (torirs_server_hiscoreslink.h).
 *
 * The server is TORIRS_HISCORES_SERVER (`host:port`), else 127.0.0.1 on
 * HISCORES_SERVER_PORT, signed (service/service_link.h); or in this process,
 * HiscoresClient_SetLocal. When it does not answer, each call says so (-1),
 * so a page can say "the hiscores are not answering" instead of "no such
 * player".
 */

#include "hiscores_proto.h"
#include "service/http_server.h"

#include <stddef.h>

/** The server is in this process: every call is `handler`. NULL goes back to
 *  HTTP. */
void
HiscoresClient_SetLocal(HttpHandler handler);

/** 1 set, -1 no answer. */
int
HiscoresClient_SetRanked(
    const char* display_name,
    int ranked);

/** The page view (hiscores_proto.h); `history` 1 for the owner's. 1 found, 0
 *  no such player, -1 no answer. */
int
HiscoresClient_PlayerView(
    const char* key,
    int history,
    struct HiscoresPlayerView* out);

/** Ranks `first`.. of `category`, at most `max`, into `rows`; how many the
 *  category ranks into `*out_ranked`. The rows' count, or -1 no answer. */
int
HiscoresClient_Hiscores(
    int category,
    int first,
    int max,
    struct HiscoresRow* rows,
    int* out_ranked);

#endif
