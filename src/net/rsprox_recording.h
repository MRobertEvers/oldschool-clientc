#ifndef SRC_NET_RSPROX_RECORDING_H
#define SRC_NET_RSPROX_RECORDING_H

/*
 * An RSProx session recording (`.bin`, or the index server's `.bin.gz`), read
 * for replay.
 *
 * The file is RSProx's BinaryBlob: a BinaryHeader, then BinaryStream's packet
 * log to the end of the file (proxy/.../binary/BinaryHeader.kt, BinaryStream.kt):
 *
 *   header   p4 headerVersion  p4 revision  p4 subRevision  p1 clientType
 *            p1 platformType   p8 timestamp p2 worldId  p4 worldFlags
 *            p1 worldLocation  jstr host    jstr activity  p2 localPlayerIndex
 *            p2 n + n bytes accountHash     jstr clientName
 *            p4 n + n bytes js5MasterIndex
 *   packet   varint (direction | ms since the previous packet << 1)
 *            opcode   server->client: p1, or p2 with the top bit set (smart)
 *                     client->server: p1
 *            size     as the revision's prot table says: fixed, p1 or p2
 *            payload  plaintext, exactly as the decoder saw it
 *
 * Both directions are in the log and neither says how long its payload is
 * unless the prot is variable, so framing the stream needs both of the
 * revision's size tables (packetin_size and packetout_size). One wrong length
 * shifts every packet after it, which is why RsproxRecording_Index insists the
 * walk ends exactly at the end of the file.
 *
 * Only the server's packets are kept as frames: replay plays the server's half
 * of the conversation back at the client. The client's half is counted, not
 * stored -- what the recorder clicked is not what this client will click.
 */

#include <stddef.h>
#include <stdint.h>

struct GameProtoRevTable;

/*
 * Two server opcodes are RSProx's, not Jagex's: every revision's GameServerProt
 * ends with them, above any real prot (which stop at 151 through 241).
 *
 *   PACKET_GROUP_END  0xFE, no payload: closes a PACKET_GROUP_START group.
 *   RECONNECT         0xFF, var-short: the session reconnected (a world hop,
 *                     a dropped link) and this is the reconnect login's body,
 *                     the GPI init block (ReconnectDecoder). The player table
 *                     restarts from it, under `reconnect_index`.
 *
 * The index frames both; a consumer that plays frames to a client must treat
 * them as what they are, never as game packets.
 */
#define RSPROX_RECORDING_PACKET_GROUP_END 0xFE
#define RSPROX_RECORDING_RECONNECT 0xFF

/** One server->client packet. */
struct RsproxRecordingFrame
{
    /** Milliseconds since the header's timestamp (the login). */
    uint64_t t_ms;
    /** SERVER_TICK_ENDs before this packet: the tick it belongs to. */
    int tick;
    int opcode;
    /** The payload, as an offset into RsproxRecording.data. */
    uint32_t offset;
    uint32_t length;
    /** RECONNECT only: the local index the session continues under. The index
     *  writes RSProx's rule, the index it had; a world hop moves the player to
     *  a new slot the recording does not log (RSProx records the new world's
     *  LOGIN_OK as this RECONNECT), and rsprox_recording_resolve_reconnects
     *  (net/proto/) puts that slot here. -1 on every other frame. */
    int reconnect_index;
};

struct RsproxRecording
{
    int header_version;
    int revision;
    int sub_revision;
    int client_type;
    int platform_type;
    int64_t timestamp_ms;
    int world_id;
    int world_flags;
    int world_location;
    char world_host[128];
    char world_activity[128];
    char client_name[128];
    int local_player_index;

    /** The decompressed file. Frames point into it. */
    uint8_t* data;
    size_t data_len;
    /** Where the packet log starts in `data`. */
    size_t stream_offset;

    /* Filled by RsproxRecording_Index. */
    struct RsproxRecordingFrame* frames;
    int frame_count;
    int client_packet_count;
    /** SERVER_TICK_ENDs in the whole recording. */
    int tick_count;
    /** The last packet's t_ms, either direction. */
    uint64_t duration_ms;
};

/**
 * Take ownership of `data` (a ToriMem_Net() allocation of `len` bytes; gzip is
 * detected by its magic and inflated) and read the header.
 *
 * Returns 0, or -1 with a sentence in `err` when the bytes are not a
 * recording: a truncated header, an unknown header version. A malformed file
 * is input, not a contract violation, so it is reported rather than asserted;
 * `data` is freed either way on failure.
 */
int
RsproxRecording_Open(
    struct RsproxRecording* rec,
    uint8_t* data,
    size_t len,
    char* err,
    int err_cap);

/** RsproxRecording_Open on a file's contents. */
int
RsproxRecording_OpenFile(
    struct RsproxRecording* rec,
    char const* path,
    char* err,
    int err_cap);

/**
 * Frame the packet log with `rev`'s size tables and keep the server's
 * packets. `rev` must state packetout_size: a revision that cannot frame the
 * client's half cannot find where the server's next packet starts.
 *
 * Returns 0, or -1 with a sentence in `err`: an opcode neither table knows, a
 * payload running past the end, a walk that does not end at the end.
 */
int
RsproxRecording_Index(
    struct RsproxRecording* rec,
    struct GameProtoRevTable const* rev,
    char* err,
    int err_cap);

/**
 * Put the local index each RECONNECT continues under into its frame's
 * reconnect_index (net/proto/proto_reconnect.c), with `rev` the
 * generated table for the recording's revision (GameProtoRev_Osrs).
 * Returns how many moved the player to a new slot. Leaves the session's
 * player table as the last trial left it: the caller re-seeds before
 * decoding.
 */
int
rsprox_recording_resolve_reconnects(
    struct GameProtoRevTable const* rev, struct RsproxRecording* rec);

/** Revisions a recording can be replayed at: the supported OSRS revisions
 *  (src/net/rev/revisions.ini). */
#define RSPROX_RECORDING_REV_FIRST 227
#define RSPROX_RECORDING_REV_LAST 241

/** The client protocol name this recording's revision is played with
 *  ("rsprox<N>"), or NULL outside that range. */
char const*
RsproxRecording_RevName(struct RsproxRecording const* rec);

void
RsproxRecording_Free(struct RsproxRecording* rec);

#endif
