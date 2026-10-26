#ifndef RSPROT_PACKET_UPDATE_TRADINGPOST_V1_H
#define RSPROT_PACKET_UPDATE_TRADINGPOST_V1_H

/*
 * UPDATE_TRADINGPOST_V1 -- server -> client.
 *
 * The trading post's offers for an obj. `set` 0 resets it, and nothing follows.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_UPDATE_TRADINGPOST_V1_MAX_OFFERS 1024

typedef struct MsgUpdateTradingpostV1Offer {
	const char *name;
	const char *previous_name;
	int32_t world;
	int64_t time;
	int32_t price;
	int32_t count;
} MsgUpdateTradingpostV1Offer;

typedef struct MsgUpdateTradingpostV1 {
	int32_t set;
	int64_t age;
	int32_t obj;
	int32_t status;
	int32_t offer_count;
	MsgUpdateTradingpostV1Offer offers[MSG_UPDATE_TRADINGPOST_V1_MAX_OFFERS];
} MsgUpdateTradingpostV1;

/* v1 -- revs 223-241. */
void packet_update_tradingpost_v1_v1_out(RsprotExec *x, MsgUpdateTradingpostV1 *m);

#define packet_update_tradingpost_v1_osrs223_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs224_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs225_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs226_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs227_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs228_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs229_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs230_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs231_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs232_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs233_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs234_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs235_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs236_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs237_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs238_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs239_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs240_out packet_update_tradingpost_v1_v1_out
#define packet_update_tradingpost_v1_osrs241_out packet_update_tradingpost_v1_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_tradingpost_v1_out[];
extern const int rsprot_update_tradingpost_v1_out_count;

#endif
