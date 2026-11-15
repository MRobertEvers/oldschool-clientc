#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_RECORDING_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_RECORDING_H

/*
 * Session recordings: every packet of a recorded account's session, both
 * directions, written as an RSProx recording (`.bin`) the client's replay
 * transport plays back (net/rsprox_recording.h, platform/net_transport_replay.c).
 *
 * An account is recorded when its record says `record = 1`
 * (`torirsserver --account record <name> on`, `torirsctl record <name> on`). It
 * is read at each login, so the switch takes effect at the next one.
 *
 *   <saves>/recordings/<account>/<yyyymmdd-hhmmss>.bin
 *
 * One file per login. The file is RSProx's BinaryBlob byte for byte:
 * BinaryHeader, then the packet log -- a varint of (direction | ms since the
 * previous packet << 1), the opcode, the length as the revision's prot table
 * frames it, and the plaintext payload. The server's packets are recorded as
 * the encoders built them (before ISAAC); the client's as they arrived, before
 * `translate_in` rewrites them. Neither the login block (which carries the
 * password or token) nor the login replies are recorded: the replay transport
 * answers the handshake itself from the header's local player index.
 *
 * A RECONNECT keeps the same file, as RSProx does: the reconnect's
 * RECONNECT_OK body (the player-info init block) is written as RSProx's
 * RECONNECT frame (opcode 0xFF), and the replay drops and resumes the
 * connection there. Between the old player going and the reconnect arriving
 * the recording is parked (closed, its path kept) for the claim window.
 *
 * Writes are whole packet records only: a tick's packets are gathered in a
 * buffer and written at the end of the tick (ToriRSServer_RecordingTick), so a
 * file cut off by a crash or a kill still ends on a record boundary and still
 * indexes. The packet log has no index or trailer, so a recording in progress
 * is a valid recording of everything so far.
 *
 * Only the socket host records (torirs_server_host.c). The scriptrun's
 * sessionless bots, the selftest and the in-client embedded world never call
 * ToriRSServer_RecordingLogin, so no player of theirs has a recording.
 */

#include <stdint.h>

struct ToriRSServerPlayer;
struct ToriRSServerRecording;

/**
 * Start or resume recording `player`'s session, if its account is recorded.
 * Call once the player is in its slot and initialised, before
 * ToriRSServer_WorldLogin sends anything. `reconnect` is the login's own
 * flag: a reconnect continues the account's parked recording, a fresh login
 * closes any and opens a new file. Sets `player->recording` (NULL when the
 * account is not recorded or the file cannot be opened -- said on stderr).
 */
void
ToriRSServer_RecordingLogin(
    struct ToriRSServerPlayer* player,
    int reconnect);

/** One server->client game packet, as ToriRSServer_Send framed it: `var` 0 for
 *  a fixed-size prot, 1 for a var-byte, 2 for a var-short. */
void
ToriRSServer_RecordingServerPacket(
    struct ToriRSServerRecording* rec,
    int opcode,
    const uint8_t* payload,
    int len,
    int var);

/** One client->server packet as it arrived: `len_bytes` is the length prefix
 *  the revision's table gave it (0 fixed, 1 or 2). */
void
ToriRSServer_RecordingClientPacket(
    struct ToriRSServerRecording* rec,
    int opcode,
    const uint8_t* payload,
    int len,
    int len_bytes);

/** RECONNECT_OK's body (the player-info init block), as RSProx's RECONNECT
 *  frame. */
void
ToriRSServer_RecordingReconnect(
    struct ToriRSServerRecording* rec,
    const uint8_t* body,
    int len);

/** The player is leaving its slot: write what is buffered, close the file and
 *  park the recording for a reconnect. Clears `player->recording`. */
void
ToriRSServer_RecordingPark(struct ToriRSServerPlayer* player);

/** End of a tick: write every live recording's buffer, and forget parked ones
 *  older than the claim window. */
void
ToriRSServer_RecordingTick(void);

#endif
