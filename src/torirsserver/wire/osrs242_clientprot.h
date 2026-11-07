#ifndef SRC_TORIRSSERVER_WIRE_OSRS242_CLIENTPROT_H
#define SRC_TORIRSSERVER_WIRE_OSRS242_CLIENTPROT_H

/*
 * Run one of revision 242's client -> server layouts (osrs242_clientprot_table.h)
 * in either direction.
 *
 * The client builds a packet by filling `vals[role]` and calling
 * osrs242_cp_write; the server takes it apart with osrs242_cp_read into the
 * same array. One table row is the whole statement of a packet's bytes at 242,
 * so the two ends cannot disagree about field order or byte transforms -- the
 * failure every hand-transcribed 239 pair had to be checked for by eye.
 */

#include "osrs242_clientprot_table.h"

#include <stdint.h>

/**
 * Write the layout's fields from `vals` into `out`. Returns the byte count
 * (the sum of the field widths); asserts the layout fits in `cap`.
 */
int
osrs242_cp_write(
    struct Osrs242CpLayout const* layout,
    int32_t const vals[OSRS242_CP_ROLE_COUNT],
    uint8_t* out,
    int cap);

/**
 * Read the layout's fields from `in` into `vals` (roles the layout does not
 * carry are left untouched). Returns the bytes consumed, or -1 when `len` is
 * shorter than the layout -- a malformed packet, which the caller drops.
 */
int
osrs242_cp_read(
    struct Osrs242CpLayout const* layout,
    uint8_t const* in,
    int len,
    int32_t vals[OSRS242_CP_ROLE_COUNT]);

/** The layout's fixed byte count. */
int
osrs242_cp_bytes(struct Osrs242CpLayout const* layout);

#endif
