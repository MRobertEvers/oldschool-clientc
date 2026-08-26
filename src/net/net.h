#ifndef SRC_NET_NET_H
#define SRC_NET_NET_H

/*
 * ToriRS_Network: the networking subsystem the command bus feeds. Owns the
 * hierarchical state machine (DISCONNECTED -> LOGIN -> GAME), the ISAAC
 * ciphers, RSA key, login machine, and packet frame reader; ported from
 * v0/tori_rs_net.u.c with the GGame fields folded into this struct.
 *
 * Raw received bytes arrive via ToriRS_Network_HandleCmd (TORIRS_CMD_NET_RECV
 * on the bus); parsed game packets accumulate in a FIFO the exec layer drains
 * with PopPacket. Outbound bytes (CONNECT / SEND_DATA) go to `out`, which the
 * socket layer or mock server drains — this direction is not on the
 * serialized command bus.
 */

#include "cmd/cmdring.h"
#include "isaac.h"
#include "login_vtable.h"
#include "loginproto.h"
#include "packetbuffer.h"
#include "rev/gameproto_revisions.h"
#include "rev/revpacket.h"
#include "rsa.h"

#include <stdint.h>

enum ToriRS_NetState
{
    TORIRS_NET_DISCONNECTED = 0,
    TORIRS_NET_LOGIN,
    TORIRS_NET_GAME,
};

/** Mirrors the platform-side connection status (v0 ToriRSNetConnectionStatus). */
enum ToriRS_NetConnStatus
{
    TORIRS_NET_STATUS_DISCONNECTED = 0,
    TORIRS_NET_STATUS_CONNECTING = 1,
    TORIRS_NET_STATUS_CONNECTED = 2,
    TORIRS_NET_STATUS_FAILED = 3,
};

/** Outbound message types the socket layer consumes (v0 net message types). */
enum ToriRS_NetOutType
{
    TORIRS_NET_OUT_CONNECT = 1,   /* payload "host:port" */
    TORIRS_NET_OUT_SEND_DATA = 2, /* raw bytes to the socket */
    /*
     * Close the connection now, without waiting for the next CONNECT to do it
     * as a side effect.
     *
     * Which matters on a reconnect: the peer learns the old session is over
     * from the FIN, and a server that saves a character on disconnect needs
     * that to happen *before* the re-established session asks for it back. A
     * close folded into the re-dial makes the two simultaneous.
     */
    TORIRS_NET_OUT_DISCONNECT = 3,
};

struct ToriRS_Network
{
    enum ToriRS_NetState state;
    struct GameProtoRevTable const* rev;

    struct Isaac* random_in;
    struct Isaac* random_out;
    struct rsa rsa;
    int rsa_ready;

    struct LoginProto* loginproto;   /* classic login; live only in LOGIN */
    void* login_generic;             /* rev->login vtable handle (xrsps); live only in LOGIN */
    struct PacketBuffer packet_buffer; /* live only in GAME */

    /** game -> platform: CONNECT / SEND_DATA frames (not on the command bus). */
    struct ToriRS_CmdRing out;

    /** Parsed inbound packets, oldest first. */
    struct RevPacketItem* packets_head;
    struct RevPacketItem* packets_tail;

    int conn_status;

    char host[128];
    char username[64];
    char password[64];

    loginproto_seed_fn seed_fn;
    void* seed_user;

    /**
     * This handshake is a reconnect, not a fresh login.
     *
     * Read by the login drivers when they build their block: it selects
     * GAMERECONNECT (18) over GAMELOGIN (16) and, at revisions that separate
     * them, swaps the credential section for the previous session's cipher
     * seed. Set by ToriRS_Network_Reconnect and cleared once the handshake
     * either succeeds or fails, so a later fresh login is not mislabelled.
     */
    int reconnect;

    /** The cipher seed the previous session authenticated with; what a
     * reconnect presents in place of a password. Valid when `has_prev_seed`. */
    int32_t prev_seed[4];
    int has_prev_seed;

    /** The local player's slot as last stated by a login response, or -1.
     * A reconnect keeps it: RECONNECT_OK restates nothing. */
    int local_index;
};

/** Initialize with a revision table and RSA key (hex exponent/modulus). */
void
ToriRS_Network_Init(
    struct ToriRS_Network* net,
    struct GameProtoRevTable const* rev,
    char const* rsa_exp_hex,
    char const* rsa_mod_hex);

void
ToriRS_Network_Free(struct ToriRS_Network* net);

/** Deterministic client seed (tests / record-replay). */
void
ToriRS_Network_SetSeedFn(
    struct ToriRS_Network* net,
    loginproto_seed_fn seed_fn,
    void* user);

/** Begin the login handshake: enter LOGIN and push a CONNECT to `out`. */
void
ToriRS_Network_ConnectLogin(
    struct ToriRS_Network* net,
    char const* host,
    char const* username,
    char const* password);

/**
 * Re-establish a session that was lost, reusing the stored host/credentials.
 *
 * This is the wire half of the reference client's `lostCon` (Client-TS
 * Client.ts:2734, deob gameState 40): the same handshake as ConnectLogin, but
 * announced as GAMERECONNECT so a server that kept the character can hand the
 * session back instead of logging it in again. Callers own the client-side
 * reset; this touches only the connection.
 *
 * Which handshake that is, is the revision's to state: `rev->reconnect_kind`
 * picks between LostCity's credential block behind opcode 18, RSProt's
 * cipher-seed block behind the same opcode, and having no reconnect at all.
 * The last of those still re-establishes the session — it sends an ordinary
 * GAMELOGIN, and a server that persists a character on disconnect hands the
 * same one back. The difference is a password round trip, not an outcome.
 *
 * Returns 0 when there is nothing to reconnect to (no prior ConnectLogin).
 */
int
ToriRS_Network_Reconnect(struct ToriRS_Network* net);

/**
 * Handle one TORIRS_CMD_NET_* command (raw-byte semantics):
 *   NET_RECV   -> drain bytes through the active state machine
 *   NET_STATUS -> update conn status; disconnect on FAILED/DISCONNECTED
 *   NET_CONNECT is a producer-only echo and is ignored here.
 */
void
ToriRS_Network_HandleCmd(
    struct ToriRS_Network* net,
    uint32_t cmd_type,
    uint8_t const* data,
    int len);

/** Server-initiated logout: tear down the game session state and return to
 * DISCONNECTED (login state machine freed, parsed FIFO drained). */
void
ToriRS_Network_Logout(struct ToriRS_Network* net);

/** Queue raw bytes to the socket (SEND_DATA). */
void
ToriRS_Network_SendRaw(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int len);

/** Pop the oldest parsed packet into `out` (shallow copy; heap fields
 *  transfer to the caller, which must free with gameproto_free).
 *  Returns 1 when a packet was popped. */
int
ToriRS_Network_PopPacket(
    struct ToriRS_Network* net,
    struct RevPacket* out);

/** Drain the outbound ring into a socket-facing consumer. Returns 1 and
 *  fills header/payload when a frame was popped. payload must hold
 *  TORIRS_CMD_MAX_PAYLOAD bytes. */
int
ToriRS_Network_PopOut(
    struct ToriRS_Network* net,
    struct ToriRS_CmdHeader* header,
    uint8_t* payload);

#endif
