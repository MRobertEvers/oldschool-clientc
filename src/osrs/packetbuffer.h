#ifndef CLIENTSTREAM_H
#define CLIENTSTREAM_H

#include "gameproto_revisions.h"
#include "isaac.h"

#include <stdbool.h>
#include <stdint.h>

enum PacketBufferState
{
    PKTBUF_AWAITING_PACKET,
    PKTBUF_READ_VARLEN_U8,
    PKTBUF_READ_VARLEN_U16,
    PKTBUF_PREPARE_RECEIVE,
    PKTBUF_READ_DATA,
    PKTBUF_PROCESS
};

struct PacketBuffer
{
    enum GameProtoRevision revision;
    enum PacketBufferState state;
    struct Isaac* random;
    int packet_type;
    int packet_length;

    void* data;
    int data_size;
};

void
packetbuffer_init(
    struct PacketBuffer* packetbuffer,
    struct Isaac* random,
    enum GameProtoRevision revision);

int
packetbuffer_read(
    struct PacketBuffer* packetbuffer,
    uint8_t* data,
    int data_size);

bool
packetbuffer_ready(struct PacketBuffer* packetbuffer);
void
packetbuffer_reset(struct PacketBuffer* packetbuffer);

int
packetbuffer_size(struct PacketBuffer* packetbuffer);
int
packetbuffer_packet_type(struct PacketBuffer* packetbuffer);
void*
packetbuffer_data(struct PacketBuffer* packetbuffer);

#endif