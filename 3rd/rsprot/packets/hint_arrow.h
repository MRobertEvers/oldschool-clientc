#ifndef RSPROT_PACKET_HINT_ARROW_H
#define RSPROT_PACKET_HINT_ARROW_H

/*
 * HINT_ARROW -- server -> client.
 *
 * Hand-written from RSProx (HintArrowDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/HintArrowDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder branches on the arrow type, with reads in each arm, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
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

/* v1 -- revs 222-234. */
void packet_hint_arrow_v1_out(RsprotExec *x, MsgHintArrow *m);
/* v2 -- revs 235-241. */
void packet_hint_arrow_v2_out(RsprotExec *x, MsgHintArrow *m);

#define packet_hint_arrow_osrs222_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs223_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs224_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs225_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs226_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs227_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs228_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs229_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs230_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs231_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs232_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs233_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs234_out packet_hint_arrow_v1_out
#define packet_hint_arrow_osrs235_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs236_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs237_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs238_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs239_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs240_out packet_hint_arrow_v2_out
#define packet_hint_arrow_osrs241_out packet_hint_arrow_v2_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_hint_arrow_out[];
extern const int rsprot_hint_arrow_out_count;

#endif
