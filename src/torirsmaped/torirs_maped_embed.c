#include "torirs_maped_embed.h"

#include "torirs_maped.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriRSMapEdEmbedClient
{
    int open;
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
    struct ToriRSMapEdSession session;
};

struct ToriRSMapEdEmbed
{
    struct ToriRSMapEd maped;
    struct ToriRSMapEdEmbedClient clients[TORIRSMAPED_EMBED_CLIENT_MAX];
};

/* The memory transport, from the server's point of view: it reads what the
 * client wrote and writes what the client will read. */
static int
memory_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct ToriRSMapEdEmbedClient* client = ctx;
    return ToriRSMapEd_BufRead(&client->to_server, dst, max);
}

static int
memory_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct ToriRSMapEdEmbedClient* client = ctx;
    return ToriRSMapEd_BufWrite(&client->to_client, src, len);
}

static void
client_open(
    struct ToriRSMapEdEmbed* embed,
    struct ToriRSMapEdEmbedClient* client)
{
    struct ToriRSMapEdTransport transport;

    assert(embed);
    assert(client);
    assert(!client->open);

    memset(client, 0, sizeof(*client));
    transport.ctx = client;
    transport.recv = memory_recv;
    transport.send = memory_send;
    ToriRSMapEd_SessionInit(&client->session, &embed->maped, &transport);
    client->open = 1;
}

static void
client_close(struct ToriRSMapEdEmbedClient* client)
{
    assert(client);

    ToriRSMapEd_SessionFree(&client->session);
    ToriRSMapEd_BufFree(&client->to_server);
    ToriRSMapEd_BufFree(&client->to_client);
    client->open = 0;
}

struct ToriRSMapEdEmbed*
ToriRSMapEd_EmbedStart(
    const char* content_dir,
    const char* repo_root)
{
    struct ToriRSMapEdEmbed* embed;

    assert(content_dir);

    embed = malloc(sizeof(*embed));
    assert(embed);
    memset(embed, 0, sizeof(*embed));

    ToriRSMapEd_Open(&embed->maped, content_dir, repo_root);
    client_open(embed, &embed->clients[0]);
    return embed;
}

int
ToriRSMapEd_EmbedConnect(struct ToriRSMapEdEmbed* embed)
{
    assert(embed);

    for( int i = 0; i < TORIRSMAPED_EMBED_CLIENT_MAX; i++ )
    {
        if( embed->clients[i].open )
            continue;
        client_open(embed, &embed->clients[i]);
        return i;
    }
    return -1;
}

int
ToriRSMapEd_EmbedDisconnect(
    struct ToriRSMapEdEmbed* embed,
    int client_id)
{
    assert(embed);
    assert(client_id >= 0);
    assert(client_id < TORIRSMAPED_EMBED_CLIENT_MAX);

    if( !embed->clients[client_id].open )
        return 0;
    client_close(&embed->clients[client_id]);
    return 1;
}

void
ToriRSMapEd_EmbedStop(struct ToriRSMapEdEmbed* embed)
{
    if( !embed )
        return;

    for( int i = 0; i < TORIRSMAPED_EMBED_CLIENT_MAX; i++ )
    {
        if( embed->clients[i].open )
            client_close(&embed->clients[i]);
    }
    ToriRSMapEd_Close(&embed->maped);
    free(embed);
}

int
ToriRSMapEd_EmbedWrite(
    struct ToriRSMapEdEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len)
{
    struct ToriRSMapEdEmbedClient* client;

    assert(embed);
    assert(client_id >= 0);
    assert(client_id < TORIRSMAPED_EMBED_CLIENT_MAX);
    assert(data || len == 0);

    client = &embed->clients[client_id];
    if( !client->open || !ToriRSMapEd_SessionAlive(&client->session) )
        return -1;
    return ToriRSMapEd_BufWrite(&client->to_server, data, len);
}

int
ToriRSMapEd_EmbedRead(
    struct ToriRSMapEdEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max)
{
    struct ToriRSMapEdEmbedClient* client;

    assert(embed);
    assert(client_id >= 0);
    assert(client_id < TORIRSMAPED_EMBED_CLIENT_MAX);

    client = &embed->clients[client_id];
    if( !client->open )
        return -1;
    return ToriRSMapEd_BufRead(&client->to_client, dst, max);
}

int
ToriRSMapEd_EmbedPending(
    const struct ToriRSMapEdEmbed* embed,
    int client_id)
{
    assert(embed);
    assert(client_id >= 0);
    assert(client_id < TORIRSMAPED_EMBED_CLIENT_MAX);

    if( !embed->clients[client_id].open )
        return 0;
    return ToriRSMapEd_BufAvailable(&embed->clients[client_id].to_client);
}

int
ToriRSMapEd_EmbedPump(struct ToriRSMapEdEmbed* embed)
{
    int alive = 0;

    assert(embed);

    for( int i = 0; i < TORIRSMAPED_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSMapEdEmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        if( ToriRSMapEd_SessionPump(&client->session) )
        {
            alive++;
            continue;
        }
        /* A dead session's outbound stays readable until the host drains it;
         * only the inbound half is finished. Closing the pipe marks EOF for
         * the reader without discarding what the server already said. */
        ToriRSMapEd_BufClose(&client->to_client);
    }
    return alive;
}
