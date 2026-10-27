#ifndef SRC_IOSERVER_IO_GATE_LINKS_H
#define SRC_IOSERVER_IO_GATE_LINKS_H

/*
 * The web logins' links database: which player accounts each web login has
 * linked, and the link token each link was minted with
 * (torirs_server_auth.h "Linked player accounts").
 *
 *   <saves>/gate/links.sqlite
 *     links(player PRIMARY KEY, user, display, token, linked_at)
 *
 * Keyed by the PLAYER, so a player account has one row and one owner however
 * often it is linked. The token is a bearer secret for that one account and
 * the file is written owner-only; the player's record keeps only the token's
 * hash, and is the authority: a row the record does not agree with (an
 * unlink, a newer link) is dropped the next time the login's list is read.
 */

#include <stddef.h>

#include "torirsserver/torirs_server_auth.h"

#define IO_GATE_LINKS_NAME_MAX 64
/* The most one /account/players answer lists. */
#define IO_GATE_LINKS_LIST_MAX 16

struct IoGateLinks;

/** Open (creating) the database at `path`. Aborts when it cannot: a gate whose
 *  links silently failed to open would show every linked player as gone. */
struct IoGateLinks*
IoGateLinks_Open(char const* path);

/** Record that the web login `user` linked the player `display_name` with
 *  `token`, replacing any row the player had. Aborts on a database error. */
void
IoGateLinks_Put(
    struct IoGateLinks* links,
    char const* user,
    char const* display_name,
    char const* token,
    long long now);

/** The token `user` holds for the player `display_name` into `out_token`.
 *  1 when there is a row; 0 when `user` has not linked that player. */
int
IoGateLinks_Token(
    struct IoGateLinks* links,
    char const* user,
    char const* display_name,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1]);

/** Drop `user`'s row for the player `display_name`, if it has one. */
void
IoGateLinks_Drop(
    struct IoGateLinks* links,
    char const* user,
    char const* display_name);

/** The display names of the players `user` has linked, oldest first, at most
 *  `max`. Rows whose player record no longer agrees are deleted on the way. */
int
IoGateLinks_List(
    struct IoGateLinks* links,
    char const* user,
    char out_names[][IO_GATE_LINKS_NAME_MAX],
    int max);

#endif
