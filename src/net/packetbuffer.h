#ifndef SRC_NET_PACKETBUFFER_H
#define SRC_NET_PACKETBUFFER_H

/*
 * Inbound game-packet frame reader, ported from v0/osrs/packetbuffer.c: a
 * byte-stream state machine that ISAAC-decrypts the opcode byte, resolves
 * the payload length from the revision table (fixed / var-u8 / var-u16),
 * and accumulates the payload across arbitrarily fragmented reads.
 */

#include "isaac.h"
#include "rev/gameproto_revisions.h"

#include <stdbool.h>
#include <stdint.h>

enum PacketBufferState
{
    PKTBUF_AWAITING_PACKET,
    /** Second byte of a pSmart1Or2 opcode (revisions with `opcode_smart2`). */
    PKTBUF_READ_OPCODE_LOW,
    PKTBUF_READ_VARLEN_U8,
    PKTBUF_READ_VARLEN_U16,
    PKTBUF_PREPARE_RECEIVE,
    PKTBUF_READ_DATA,
    PKTBUF_PROCESS
};

struct PacketBuffer
{
    struct GameProtoRevTable const* rev;
    enum PacketBufferState state;
    struct Isaac* random;
    int packet_type;
    int packet_length;
    /** Partial u16 length accumulator: the two length bytes can arrive in
     *  separate reads, and the opcode's ISAAC step already happened, so the
     *  first byte is stashed here rather than re-read. */
    int length_bytes_read;
    int length_partial;
    /** High 7 bits of a two-byte opcode, held while its low byte is in flight. */
    int opcode_high;

    uint8_t* data;
    int data_size;
};

void
packetbuffer_init(
    struct PacketBuffer* packetbuffer,
    struct Isaac* random,
    struct GameProtoRevTable const* rev);

/** Consume bytes; returns how many were used (stops at a complete packet). */
int
packetbuffer_read(
    struct PacketBuffer* packetbuffer,
    uint8_t* data,
    int data_size);

bool
packetbuffer_ready(struct PacketBuffer* packetbuffer);

/** Free the payload and re-arm for the next packet. */
void
packetbuffer_reset(struct PacketBuffer* packetbuffer);

int
packetbuffer_size(struct PacketBuffer* packetbuffer);

int
packetbuffer_packet_type(struct PacketBuffer* packetbuffer);

void*
packetbuffer_data(struct PacketBuffer* packetbuffer);

int
packetbuffer_amt_recv_cnt(struct PacketBuffer* packetbuffer);

#endif
