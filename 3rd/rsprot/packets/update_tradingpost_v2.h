#ifndef RSPROT_PACKET_UPDATE_TRADINGPOST_V2_H
#define RSPROT_PACKET_UPDATE_TRADINGPOST_V2_H

/*
 * UPDATE_TRADINGPOST_V2 -- server -> client.
 *
 * Hand-written from RSProx (UpdateTradingPostV2Decoder, protocol/osrs-<N>/.../decoder/codec/misc/player/UpdateTradingPostV2Decoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder returns early on a flag and loops over a counted offer list, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_UPDATE_TRADINGPOST_V2_MAX_OFFERS 512

typedef struct MsgTradingPostOffer {
	const char *name;
	const char *previous_name;
	int32_t world;
	int64_t time;
	int64_t price;
	int32_t count;
} MsgTradingPostOffer;

typedef struct MsgUpdateTradingpostV2 {
	int32_t has_update; /* 0: no offer list follows */
	int64_t age;
	int32_t obj;
	int32_t status;
	int32_t offer_count;
	MsgTradingPostOffer offers[MSG_UPDATE_TRADINGPOST_V2_MAX_OFFERS];
} MsgUpdateTradingpostV2;

/* v1 -- revs 241. */
void packet_update_tradingpost_v2_v1_out(RsprotExec *x, MsgUpdateTradingpostV2 *m);

#define packet_update_tradingpost_v2_osrs241_out packet_update_tradingpost_v2_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_tradingpost_v2_out[];
extern const int rsprot_update_tradingpost_v2_out_count;

#endif
