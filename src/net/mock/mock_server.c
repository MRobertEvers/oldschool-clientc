#include "mock_server.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)

void
MockServer_Init(
    struct MockServer* server,
    struct GameProtoRevTable const* rev,
    uint64_t server_seed,
    int response_byte)
{
    assert(server && rev);
    memset(server, 0, sizeof(*server));
    server->rev = rev;
    server->server_seed = server_seed;
    server->response_byte = response_byte;
}

void
MockServer_Free(struct MockServer* server)
{
    if( server && server->enc )
    {
        isaac_free(server->enc);
        server->enc = NULL;
    }
}

int
MockServer_Hello(
    struct MockServer* server,
    uint8_t* out,
    int cap)
{
    assert(cap >= 17);
    memset(out, 0, 9);
    for( int i = 0; i < 8; i++ )
        out[9 + i] = (uint8_t)(server->server_seed >> (56 - i * 8));
    server->login_sent = 1;
    return 17;
}

void
MockServer_ArmCipher(
    struct MockServer* server,
    int32_t const* client_seed)
{
    int32_t seed_in[4];
    for( int i = 0; i < 4; i++ )
        seed_in[i] = client_seed[i] + 50;
    if( server->enc )
        isaac_free(server->enc);
    server->enc = isaac_new(seed_in, 4);
}

int
MockServer_Response(
    struct MockServer* server,
    uint8_t* out,
    int cap)
{
    assert(cap >= 1);
    out[0] = (uint8_t)server->response_byte;
    return 1;
}

int
MockServer_Packet(
    struct MockServer* server,
    uint8_t* out,
    int cap,
    int wire_opcode,
    uint8_t const* payload,
    int payload_len)
{
    int pos = 0;
    int size = server->rev->packetin_size(wire_opcode);

    assert(server->enc && "MockServer_ArmCipher not called");
    assert(cap >= payload_len + 3);

    out[pos++] = (uint8_t)((wire_opcode + isaac_next(server->enc)) & 0xff);

    if( size == PKTIN_LENGTH_VARU8 )
    {
        out[pos++] = (uint8_t)payload_len;
    }
    else if( size == PKTIN_LENGTH_VARU16 )
    {
        out[pos++] = (uint8_t)(payload_len >> 8);
        out[pos++] = (uint8_t)(payload_len & 0xff);
    }

    if( payload_len > 0 && payload )
    {
        memcpy(out + pos, payload, payload_len);
        pos += payload_len;
    }
    return pos;
}
