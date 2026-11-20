#ifndef SRC_WEBSITESERVER_WEBSITE_USERS_H
#define SRC_WEBSITESERVER_WEBSITE_USERS_H

/*
 * The website's user accounts, for the processes outside it: `torirsserver
 * --account login, list, revoke, invite` (website_client.h). Under
 * `/svc/users` on the website's port, signed (service/service_auth.h); the
 * website's own pages call website_users_db.h directly.
 *
 *   POST /login          name, password            -> result <auth result>
 *   POST /create         name, password            -> result <create> <problem>
 *   POST /revoke         name                      -> result <0|1>
 *   GET  /list                                     -> login <name> ...
 *   POST /invite/create  [name]                    -> code <code>
 *   GET  /invite?code=                             -> invite <0|1> <name>
 *   POST /invite/redeem  code, name, password      -> result <invite> <problem>
 */

#include "service/http_server.h"

/** The website's port: browsers on it, and these routes under /svc/users. */
#define WEBSITE_SERVER_PORT 8088

void
WebsiteUsers_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
