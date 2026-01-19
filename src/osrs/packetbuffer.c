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
packetbuffer_init(struct PacketBuffer* packetbuffer)
{
    packetbuffer->state = PKTBUF_AWAITING_PACKET;
    packetbuffer->packet_type = 0;
    packetbuffer->packet_length = 0;
    packetbuffer->data = NULL;
    packetbuffer->data_size = 0;
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
            packet_size = packetin_size_lc254(packet_type);
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
    packetbuffer_init(packetbuffer);
}

int
packetbuffer_size(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->data_size;
}

void*
packetbuffer_data(struct PacketBuffer* packetbuffer)
{
    return packetbuffer->data;
}