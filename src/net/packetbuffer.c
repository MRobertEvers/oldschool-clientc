#include "packetbuffer.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdlib.h>
#include <string.h>

#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)

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
    struct GameProtoRevTable const* rev)
{
    assert(rev && rev->packetin_size);
    packetbuffer->state = PKTBUF_AWAITING_PACKET;
    packetbuffer->rev = rev;
    packetbuffer->random = random;
    packetbuffer->packet_type = 0;
    packetbuffer->packet_length = 0;
    packetbuffer->length_bytes_read = 0;
    packetbuffer->length_partial = 0;
    packetbuffer->opcode_high = 0;
    packetbuffer->data = NULL;
    packetbuffer->data_size = 0;
}

int
packetbuffer_read(
    struct PacketBuffer* packetbuffer,
    uint8_t* data,
    int data_size)
{
    assert(packetbuffer);
    /* ISAAC-scrambled revisions need the cipher; plaintext ones (xrsps) do not. */
    assert(packetbuffer->rev->opcode_plaintext || packetbuffer->random);
    assert(data_size > 0);

    struct RSCache_Buffer buffer = { .data = data,
                                     .size = (uint32_t)data_size,
                                     .position = 0 };

    int varlength = 0;
    int remaining;
    int packet_type;
    int packet_size;

    while( (int)buffer.position < data_size )
    {
    step:;
        remaining = data_size - (int)buffer.position;

        switch( packetbuffer->state )
        {
        case PKTBUF_AWAITING_PACKET:
        {
            packet_type = g1(&buffer);
            if( !packetbuffer->rev->opcode_plaintext )
                packet_type = (packet_type - isaac_next(packetbuffer->random)) & 0xff;
            /*
             * pSmart1Or2: a revision whose opcodes reach 0x80 writes the high
             * ones as two cipher bytes. The first carries `(op >> 8) | 0x80`,
             * so a decoded first byte with the top bit set means "one more
             * byte to come" rather than "opcode 0x80..0xFF".
             *
             * The second byte may not have arrived yet, which is why this
             * parks in its own state instead of reading ahead: the first
             * byte's ISAAC step has already been taken and cannot be replayed.
             */
            if( packetbuffer->rev->opcode_smart2 && (packet_type & 0x80) )
            {
                packetbuffer->opcode_high = packet_type & 0x7f;
                packetbuffer->state = PKTBUF_READ_OPCODE_LOW;
                break;
            }
            packet_size = packetbuffer->rev->packetin_size(packet_type);
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
                goto step;
            }
            break;
        }
        case PKTBUF_READ_OPCODE_LOW:
            packet_type = g1(&buffer);
            if( !packetbuffer->rev->opcode_plaintext )
                packet_type = (packet_type - isaac_next(packetbuffer->random)) & 0xff;
            packet_type |= packetbuffer->opcode_high << 8;
            packet_size = packetbuffer->rev->packetin_size(packet_type);
            packetbuffer->packet_type = packet_type;
            if( packet_size == PKTIN_LENGTH_VARU8 )
                packetbuffer->state = PKTBUF_READ_VARLEN_U8;
            else if( packet_size == PKTIN_LENGTH_VARU16 )
                packetbuffer->state = PKTBUF_READ_VARLEN_U16;
            else
            {
                packetbuffer->packet_length = packet_size;
                packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
                goto step;
            }
            break;
        case PKTBUF_READ_VARLEN_U8:
            varlength = g1(&buffer);
            packetbuffer->packet_length = varlength;
            packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
            goto step;
        case PKTBUF_READ_VARLEN_U16:
            /* Accumulate the two length bytes one at a time so a split u16
             * length still makes forward progress (the opcode's ISAAC step
             * already happened, so we cannot re-read from the top). */
            packetbuffer->length_partial =
                (packetbuffer->length_partial << 8) | g1(&buffer);
            if( ++packetbuffer->length_bytes_read < 2 )
                break; /* consumed one byte; wait for the next read */
            packetbuffer->packet_length = packetbuffer->length_partial;
            packetbuffer->state = PKTBUF_PREPARE_RECEIVE;
            goto step;
        case PKTBUF_PREPARE_RECEIVE:
            packetbuffer->data = malloc(
                packetbuffer->packet_length > 0 ? (size_t)packetbuffer->packet_length : 1);
            assert(packetbuffer->data);
            memset(
                packetbuffer->data,
                0,
                packetbuffer->packet_length > 0 ? (size_t)packetbuffer->packet_length : 1);
            packetbuffer->state = PKTBUF_READ_DATA;
            goto step;
        case PKTBUF_READ_DATA:
        {
            int remaining_packet_len = packetbuffer->packet_length - packetbuffer->data_size;
            int copy_size;

            if( remaining_packet_len == 0 )
            {
                packetbuffer->state = PKTBUF_PROCESS;
                goto done;
            }
            if( remaining <= 0 )
                goto done;

            copy_size = imin(remaining_packet_len, remaining);

            greadto(
                &buffer,
                (char*)(packetbuffer->data + packetbuffer->data_size),
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
            goto done;
        }
    }

    /* Zero-length packets complete without needing further bytes. */
    if( packetbuffer->state == PKTBUF_READ_DATA &&
        packetbuffer->data_size == packetbuffer->packet_length )
        packetbuffer->state = PKTBUF_PROCESS;

done:;
    return (int)buffer.position;
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
    packetbuffer->packet_length = 0;
    packetbuffer->packet_type = 0;

    packetbuffer_init(packetbuffer, packetbuffer->random, packetbuffer->rev);
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

int
packetbuffer_amt_recv_cnt(struct PacketBuffer* packetbuffer)
{
    switch( packetbuffer->state )
    {
    case PKTBUF_AWAITING_PACKET:
        return 1;
    case PKTBUF_READ_OPCODE_LOW:
        return 1;
    case PKTBUF_READ_VARLEN_U8:
        return 1;
    case PKTBUF_READ_VARLEN_U16:
        return 2;
    case PKTBUF_PREPARE_RECEIVE:
        assert(0);
        return 0;
    case PKTBUF_READ_DATA:
        return packetbuffer->packet_length - packetbuffer->data_size;
    case PKTBUF_PROCESS:
        return 0;
    }
    return 0;
}
