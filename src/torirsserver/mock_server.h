#ifndef SRC_TORIRSSERVER_MOCK_SERVER_H
#define SRC_TORIRSSERVER_MOCK_SERVER_H

/*
 * Scripted in-process mock server for the loopback test: emits the login
 * hello + a chosen response byte, then ISAAC-encrypts scripted game packets
 * with the same in-cipher the client derives (seed+50). No socket — the test
 * hands the emitted bytes to ToriRS_Network as NET_RECV commands.
 */

#include "net/isaac.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>

struct MockServer
{
    struct GameProtoRevTable const* rev;
    uint64_t server_seed;
    int response_byte;
    /** Server's outbound cipher = the client's random_in (seed+50). Seeded
     *  once the login block is known. */
    struct Isaac* enc;
    int login_sent;
    int credentials_seen;
};

void
MockServer_Init(
    struct MockServer* server,
    struct GameProtoRevTable const* rev,
    uint64_t server_seed,
    int response_byte);

void
MockServer_Free(struct MockServer* server);

/** Emit the 9 skip bytes + 8-byte seed. Returns bytes written. */
int
MockServer_Hello(
    struct MockServer* server,
    uint8_t* out,
    int cap);

/**
 * Called with the client's login block (after the hello) to derive the
 * server out-cipher from the same client seed. `client_seed` is the 4-word
 * seed the client used (the test knows it, since it injected it).
 */
void
MockServer_ArmCipher(
    struct MockServer* server,
    int32_t const* client_seed);

/** Emit the 1-byte login response. Returns 1. */
int
MockServer_Response(
    struct MockServer* server,
    uint8_t* out,
    int cap);

/**
 * ISAAC-encrypt and frame one game packet: opcode (by wire code) + payload.
 * Var-length packets are framed per the rev table (u8/u16 length prefix
 * before the payload). Returns bytes written.
 */
int
MockServer_Packet(
    struct MockServer* server,
    uint8_t* out,
    int cap,
    int wire_opcode,
    uint8_t const* payload,
    int payload_len);

#endif
