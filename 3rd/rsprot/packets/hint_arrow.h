#ifndef RSPROT_PACKET_HINT_ARROW_H
#define RSPROT_PACKET_HINT_ARROW_H

/*
 * HINT_ARROW -- server -> client.
 *
 * Hand-written from RSProx (HintArrowDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/HintArrowDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder branches on the arrow type, with reads in each arm, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

/**
 * Every arm's fields. `type` says which are meaningful:
 *   0      reset                 (5 unused bytes)
 *   1, 10  npc / player `index`  (3 unused bytes)
 *   2..6   a tile: absolute `x`, `z`, `height` (one byte); the type is the
 *          arrow's position within the tile (2 centre, 3 west, 4 east, 5 south,
 *          6 north -- the deob's hint-arrow offset rule)
 *   11     world entity `index`, `height` in three bytes (235+)
 */
typedef struct MsgHintArrow {
	int32_t type;
	int32_t index;
	int32_t x;
	int32_t z;
	int32_t height;
	const uint8_t *skip0;
} MsgHintArrow;

/* v100 -- revs 227-234. */
void packet_hint_arrow_v100_out(RsprotExec *x, MsgHintArrow *m);
/* v101 -- revs 235-241. */
void packet_hint_arrow_v101_out(RsprotExec *x, MsgHintArrow *m);

#define packet_hint_arrow_rev227_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev228_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev229_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev230_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev231_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev232_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev233_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev234_out packet_hint_arrow_v100_out
#define packet_hint_arrow_rev235_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev236_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev237_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev238_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev239_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev240_out packet_hint_arrow_v101_out
#define packet_hint_arrow_rev241_out packet_hint_arrow_v101_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_hint_arrow_out[];
extern const int rsprot_hint_arrow_out_count;

#endif
