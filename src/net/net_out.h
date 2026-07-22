#ifndef SRC_NET_NET_OUT_H
#define SRC_NET_NET_OUT_H

/*
 * Outbound packet builders. Each writes an ISAAC-encrypted opcode byte
 * (reference Packet.pIsaac) followed by the payload, into a caller buffer,
 * returning the byte count. The wire opcodes come from the packetout table so
 * this stays revision-neutral in shape (currently lc245_2 constants).
 */

#include "isaac.h"

#include <stdint.h>

/** Encrypt+write one opcode byte. Returns bytes written (1). */
int
net_out_opcode(
    struct Isaac* random_out,
    uint8_t* buf,
    int wire_opcode);

/* Each builder returns total bytes written into buf, or -1 on overflow. */

int
net_out_move_gameclick(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int ctrl_held);

int
net_out_if_button(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id);

int
net_out_resume_pausebutton(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id);

int
net_out_message_public(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text);

int
net_out_chat_setmode(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int public_mode,
    int private_mode,
    int trade_mode);

int
net_out_no_timeout(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap);

#endif
