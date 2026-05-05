#ifndef RSBUF_ISAAC_H
#define RSBUF_ISAAC_H

/* Outbound packet opcode helpers that require Isaac cipher or RSA.
 * Kept separate from rsbuf.h so the core utility has no isaac/rsa dependency. */

#include "osrs/rscache/rsbuf.h"

struct Isaac;

/** Write one opcode byte XORed with the next Isaac value.
 * Every client-to-server packet starts with this before the payload. */
void
rsbuf_p1isaac(struct RSBuffer* buf, struct Isaac* isaac_out, int opcode);

#endif /* RSBUF_ISAAC_H */
