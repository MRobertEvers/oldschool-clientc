#ifndef RSPROT_PACKET_UPDATE_STOCKMARKET_SLOT_V2_H
#define RSPROT_PACKET_UPDATE_STOCKMARKET_SLOT_V2_H

/*
 * UPDATE_STOCKMARKET_SLOT_V2 -- server -> client.
 *
 * Hand-written from RSProx (UpdateStockMarketSlotV2Decoder, protocol/osrs-<N>/.../decoder/codec/misc/player/UpdateStockMarketSlotV2Decoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder escapes into a versioned layout on a bit pattern of its second byte, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_UPDATE_STOCKMARKET_SLOT_V2_MAX_EXTENSION 0 /* the fixed 34-byte frame (241) holds the version-2 form and nothing more */

/**
 * `first_state & 7 == 7` escapes: `version` and `status` follow. Otherwise the
 * layout is version 1 and `first_state` IS the status. Version 1 carries the
 * price and completed gold in four bytes (`price32`, `completed_gold32`,
 * unsigned); version 2 in eight, plus an extension blob.
 */
typedef struct MsgUpdateStockmarketSlotV2 {
	int32_t slot;
	int32_t first_state;
	int32_t version;
	int32_t status;
	int32_t obj;
	int64_t price;
	int32_t price32;
	int32_t count;
	int32_t completed_count;
	int64_t completed_gold;
	int32_t completed_gold32;
	int32_t extension_length;
	const uint8_t *extension;
	const uint8_t *padding; /* version 1 only: the 14 bytes after it */
} MsgUpdateStockmarketSlotV2;

/* v1 -- revs 241. */
void packet_update_stockmarket_slot_v2_v1_out(RsprotExec *x, MsgUpdateStockmarketSlotV2 *m);

#define packet_update_stockmarket_slot_v2_osrs241_out packet_update_stockmarket_slot_v2_v1_out
#define packet_update_stockmarket_slot_v2_osrsm241_out packet_update_stockmarket_slot_v2_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_stockmarket_slot_v2_out[];
extern const int rsprot_update_stockmarket_slot_v2_out_count;

#endif
