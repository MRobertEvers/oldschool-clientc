#ifndef SRC_TORIRSSERVER_WIRE_OSRS242_OSRS242_INBOUND_H
#define SRC_TORIRSSERVER_WIRE_OSRS242_OSRS242_INBOUND_H

#include <stdint.h>

/**
 * Revision 242's `translate_in` (torirs_server_wire.h): a client -> server
 * body in, the canonical body the world handlers read out, and the canonical
 * name it now answers to (PKTOUT_NAME_NONE drops it). Same contract as
 * osrs239_inbound_translate; see osrs242_inbound.c.
 */
int
osrs242_inbound_translate(
    int wire_opcode,
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len);

#endif /* SRC_TORIRSSERVER_WIRE_OSRS242_OSRS242_INBOUND_H */
