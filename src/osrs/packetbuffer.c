#include "packetbuffer.h"

#include "packetin.h"
#include "rscache/rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define packetbuffer_AWAITING_PACKET (-1)

static inline int
imin(
    int a,
    int b)
{
    return a < b ? a : b;
}

void
packetbuffer_init(
    struct PacketBuffer* packetbuffer,
    struct Isaac* random,
    enum GameProtoRevision revision)
{
    packetbuffer->state = PKTBUF_AWAITING_PACKET;
    packetbuffer->revision = revision;
    packetbuffer->random = random;
    packetbuffer->packet_type = 0;
    packetbuffer->packet_length = 0;
    packetbuffer->data = NULL;
    packetbuffer->data_size = 0;
}

static inline int
packetsize(
    enum GameProtoRevision revision,
    int packet_type)
{
    switch( revision )
    {
    case GAMEPROTO_REVISION_LC254:
        return packetin_size_lc254(packet_type);
    case GAMEPROTO_REVISION_LC245_2:
        return packetin_size_lc245_2(packet_type);
    }

    assert(0 && "Random is required for packet buffer");
    return 0;
}

int
packetbuffer_read(
    struct PacketBuffer* packetbuffer,
    uint8_t* data,
    int data_size)
{
    assert(data_size > 0);

    struct RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };

    int varlength = 0;
    int remaining;
    int packet_type;
    int packet_size;

    while( buffer.position < data_size )
    {
        remaining = data_size - buffer.position;

        switch( packetbuffer->state )
        {
        case PKTBUF_AWAITING_PACKET:
            packet_type = g1(&buffer);
            // isaac_next(login->random_in);
            // int decoded_byte = (encrypted_byte - isaac_value) & 0xff
            packet_type = (packet_type - isaac_next(packetbuffer->random)) & 0xff;
            packet_size = packetsize(packetbuffer->revision, packet_type);
            packetbuffer->packet_type = packet_type;

            if( packet_size == PKTIN_LENGTH_VARU8 )
            {
                packetbuffer->state = PKTBUF_READ_VARLEN_U8;
            }
            else if( packet_size == PKTIN_LENGTH_VARU16 )
            {
                packetbuffer->state = PKTBUF_READ_VARLEN_U16;
            }
            else
            {
                packetbuffer->packet_length = packet_size;
                packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
            }
            break;
        case PKTBUF_READ_VARLEN_U8:
            varlength = g1(&buffer);
            packetbuffer->packet_length = varlength;

            packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
            break;
        case PKTBUF_READ_VARLEN_U16:
            varlength = g2(&buffer);
            packetbuffer->packet_length = varlength;

            packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
            break;
        case PKTBUF_PREPARE_RECEIVE:
            packetbuffer->data = malloc(packetbuffer->packet_length);
            memset(packetbuffer->data, 0, packetbuffer->packet_length);

            packetbuffer->state = PKTBUF_READ_DATA;
            break;

        case PKTBUF_READ_DATA:
        {
            if( remaining <= 0 )
                goto done;

            int remaining_packet_len = packetbuffer->packet_length - packetbuffer->data_size;

            int copy_size = imin(remaining_packet_len, remaining);

            greadto(
                &buffer,
                packetbuffer->data + packetbuffer->data_size,
                packetbuffer->packet_length - packetbuffer->data_size,
                copy_size);

            packetbuffer->data_size += copy_size;

            if( packetbuffer->data_size == packetbuffer->packet_length )
            {
                packetbuffer->state = PKTBUF_PROCESS;
                goto done;
            }
        }
        break;
        case PKTBUF_PROCESS:
            break;
        }
    }

done:;
    return packetbuffer->state == PKTBUF_PROCESS;
}

bool
packetbuffer_ready(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->state == PKTBUF_PROCESS;
}

void
packetbuffer_reset(struct PacketBuffer* packetbuffer)
{
    if( packetbuffer->data )
        free(packetbuffer->data);
    packetbuffer->data = NULL;
    packetbuffer->data_size = 0;
    packetbuffer_init(packetbuffer, packetbuffer->random, packetbuffer->revision);
}

int
packetbuffer_size(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->data_size;
}

int
packetbuffer_packet_type(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->packet_type;
}

void*
packetbuffer_data(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->data;
}