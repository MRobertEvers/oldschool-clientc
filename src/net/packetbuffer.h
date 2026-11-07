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
    /** A control frame's magic, type and length, after the escape
     *  (torirs_control.h). */
    PKTBUF_READ_CONTROL_HEADER,
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
    /** The item being read is a control frame, not a packet (torirs_control.h):
     *  `packet_type` is TORIRS_CONTROL_ESCAPE and `data` its payload. */
    int control;
    int control_type;
    int control_header_read;
    uint8_t control_header[12];

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

/** The completed item is a control frame (torirs_control.h), not a packet. */
bool
packetbuffer_is_control(struct PacketBuffer* packetbuffer);

/** The control frame's type. Only for a control frame. */
int
packetbuffer_control_type(struct PacketBuffer* packetbuffer);

/** Hand the payload to the caller, who frees it (ToriMem_Net); reset then
 *  frees nothing. */
uint8_t*
packetbuffer_take_data(struct PacketBuffer* packetbuffer);

int
packetbuffer_amt_recv_cnt(struct PacketBuffer* packetbuffer);

#endif
