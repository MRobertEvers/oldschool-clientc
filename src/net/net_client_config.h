#ifndef SRC_NET_NET_CLIENT_CONFIG_H
#define SRC_NET_NET_CLIENT_CONFIG_H

/*
 * The game client's network launch options: where it connects, who it signs
 * in as, which protocol revision and login block it speaks, and what the
 * embedded server is booted with.
 *
 * One member of struct AppConfig (app.h `net`). BootManifest_ApplyToConfig
 * fills it from `[net:boot]`; main.c's argument layers (--connect, --port,
 * --user, --pass, --resume, --rev, --offline) and the replay boot
 * (main_rsprox_replay_open) rewrite it before App_Init copies it. A zeroed
 * one is an offline boot.
 *
 * This header names no App: it is plain data.
 */

#include <stdint.h>

struct NetClientConfig
{
    /** --connect target "host[:port]". NULL/"" = offline (no networking). */
    char const* connect_target;
    /** `[net:boot] auth=oauth`: sign in through the account server (saved
     *  accounts, Play Now, one-time login tokens). 0 = the password form. */
    int connect_oauth;
    char const* connect_user;
    char const* connect_pass;
    /** --resume "s0,s1,s2,s3,index": a previous session's resume token
     *  (app_session_resume_token), from a reloaded browser tab. The first
     *  login then asks for that session back with GAMERECONNECT rather than
     *  logging in afresh. NULL = none. */
    char const* connect_resume;
    /** `[net:boot] scripts` — compiled script pack for the embedded mock
     * server. TORIRSSERVER_SCRIPTS still overrides it. NULL/"" = server default. */
    char const* server_scripts;
    /** `[net:boot] cheat` — "::" commands (';'-separated, no leading "::") to
     * send once right after login, e.g. "zuk" to enter the Inferno instance.
     * The manifest spelling of the TORIRS_NET_CHEAT harness hook; the env var
     * still overrides. NULL/"" = none. */
    char const* cheat;
    /** Protocol revision name ("lc254", "lc245_2"). NULL = lc254, the
     * authoritative LostCity_Server build. Mock/loopback tests pass
     * lc245_2 explicitly. */
    char const* rev_name;
    /** Game-socket port. 0 = 43594 (legacy default). Set from the manifest
     * `[net:boot] port` or --port; --connect "host:port" also fills it. */
    int connect_port;
    /** Login-block RSA key pair (hex). NULL = built-in lc245_2/254 default.
     * Env TORIRS_RSA_EXP/MOD > these > built-in. */
    char const* rsa_exp;
    char const* rsa_mod;
    /** Login-block jag-archive CRCs. Only consulted when jag_crc_set and the
     * env TORIRS_JAG_CRC is absent (env wins). */
    int32_t jag_crc[9];
    int jag_crc_set;
    /** Protocol client version sent in the login block. 0 = rev-table default. */
    int client_version;
};

#endif
