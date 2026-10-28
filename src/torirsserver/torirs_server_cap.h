#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_CAP_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_CAP_H
/*
 * Players one world can hold (`TORIRSSERVER_PLAYER_MAX` in torirs_server.h).
 *
 * A build flag (`-DTORIRSSERVER_PLAYER_CAP=n`) because the right number
 * depends on the host: the standalone server takes the default, while the
 * client-embedded world (phone, browser) serves one player and should not
 * carry a pool sized for two thousand. Its own header because the scene
 * module sizes its window pool from it and cannot include torirs_server.h.
 */
#ifndef TORIRSSERVER_PLAYER_CAP
#define TORIRSSERVER_PLAYER_CAP 2000
#endif
_Static_assert(TORIRSSERVER_PLAYER_CAP >= 1, "a world holds at least one player");
_Static_assert(TORIRSSERVER_PLAYER_CAP <= 2046,
               "the wire's player index is 11 bits and 2047 is the list terminator");

#endif
