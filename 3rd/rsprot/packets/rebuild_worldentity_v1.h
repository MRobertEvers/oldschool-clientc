#ifndef RSPROT_PACKET_REBUILD_WORLDENTITY_V1_H
#define RSPROT_PACKET_REBUILD_WORLDENTITY_V1_H

/*
 * REBUILD_WORLDENTITY_V1 -- server -> client, revisions 223-224.
 *
 * Hand-written from RSProx (RebuildWorldEntityV1Decoder, protocol/osrs-<N>/.../
 * decoder/codec/map/RebuildWorldEntityV1Decoder.kt; 223 and 224 are the same).
 * RSProt's encoder wraps the build in a try block and RSProx's decoder reads a
 * bit-packed GPI block in the middle, neither of which a spec's reads say, so
 * it is written by hand.
 *
 *   g2 index  g2 base_x  g2 base_z   (zones)
 *   the GPI init block, always (RSPROT_GPI_INIT_BYTES): the world's player
 *     table starts over from it, under the session's local index
 *   g2 xtea_count, the 4 x 13 x 13 zone grid, xtea_count keys
 */

#include "../include/rsprot_exec.h"
#include "../include/rsprot_tails.h"

typedef struct MsgRebuildWorldEntityV1 {
	int32_t index;
	int32_t base_x;
	int32_t base_z;
	const uint8_t *gpi_init;
	int32_t xtea_count;
	int32_t zones[RSPROT_REGION_ZONES];
	int32_t keys[RSPROT_REGION_KEYS_MAX * 4];
} MsgRebuildWorldEntityV1;

/* v1 -- revs 223-224. */
void packet_rebuild_worldentity_v1_v1_out(RsprotExec *x, MsgRebuildWorldEntityV1 *m);

#define packet_rebuild_worldentity_v1_osrs223_out packet_rebuild_worldentity_v1_v1_out
#define packet_rebuild_worldentity_v1_osrs224_out packet_rebuild_worldentity_v1_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_rebuild_worldentity_v1_out[];
extern const int rsprot_rebuild_worldentity_v1_out_count;

#endif
