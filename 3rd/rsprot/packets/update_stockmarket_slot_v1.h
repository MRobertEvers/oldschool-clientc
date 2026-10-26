#ifndef RSPROT_PACKET_UPDATE_STOCKMARKET_SLOT_V1_H
#define RSPROT_PACKET_UPDATE_STOCKMARKET_SLOT_V1_H

/*
 * UPDATE_STOCKMARKET_SLOT_V1 -- server -> client.
 *
 * A Grand Exchange slot. `status` 0 resets it, and 18 padding bytes follow.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgUpdateStockmarketSlotV1 {
	int32_t slot;
	int32_t status;
	int32_t obj;
	int32_t price;
	int32_t count;
	int32_t completed_count;
	int32_t completed_gold;
	const uint8_t *padding;
} MsgUpdateStockmarketSlotV1;

/* v1 -- revs 222-241. */
void packet_update_stockmarket_slot_v1_v1_out(RsprotExec *x, MsgUpdateStockmarketSlotV1 *m);

#define packet_update_stockmarket_slot_v1_osrs222_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs223_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs224_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs225_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs226_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs227_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs228_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs229_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs230_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs231_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs232_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs233_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs234_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs235_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs236_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs237_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs238_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs239_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs240_out packet_update_stockmarket_slot_v1_v1_out
#define packet_update_stockmarket_slot_v1_osrs241_out packet_update_stockmarket_slot_v1_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_stockmarket_slot_v1_out[];
extern const int rsprot_update_stockmarket_slot_v1_out_count;

#endif
