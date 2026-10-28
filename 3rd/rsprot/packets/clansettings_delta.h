#ifndef RSPROT_PACKET_CLANSETTINGS_DELTA_H
#define RSPROT_PACKET_CLANSETTINGS_DELTA_H

/*
 * CLANSETTINGS_DELTA -- server -> client.
 *
 * Changes to the clan settings, as opcode-tagged updates until a 0 opcode or
 * the end of the payload (RSProx ClanSettingsDeltaDecoder's 1..15).
 *
 * A member is named by a hash or, when its first byte is 0xFF, not: the
 * wire's g8 starts at that byte, so `hash_top` is its first byte and the
 * other seven follow only when it is not 0xFF (`hash_mid` three, `hash_low`
 * four); hash = top << 56 | mid << 32 | low.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_CLANSETTINGS_DELTA_MAX_UPDATES 512

typedef struct MsgClansettingsDeltaUpdate {
	int32_t opcode;
	int32_t hash_top;
	int32_t hash_mid;
	int32_t hash_low;
	const char *name;
	int32_t index;
	int32_t rank;
	int32_t allow_unaffined;
	int32_t talk_rank;
	int32_t kick_rank;
	int32_t lootshare_rank;
	int32_t coinshare_rank;
	int32_t setting;
	int32_t value;
	int64_t lvalue;
	const char *svalue;
	int32_t start_bit;
	int32_t end_bit;
	int32_t skip4;
	int32_t join_rune_day;
	int32_t muted;
} MsgClansettingsDeltaUpdate;

typedef struct MsgClansettingsDelta {
	int32_t clan_type;
	int64_t owner;
	int32_t update_num;
	int32_t update_count;
	MsgClansettingsDeltaUpdate updates[MSG_CLANSETTINGS_DELTA_MAX_UPDATES];
	const uint8_t *trailing;
	int32_t trailing_length;
} MsgClansettingsDelta;

/* v1 -- revs 222-241. */
void packet_clansettings_delta_v1_out(RsprotExec *x, MsgClansettingsDelta *m);

#define packet_clansettings_delta_osrs222_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs223_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs224_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs225_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs226_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs227_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs228_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs229_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs230_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs231_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs232_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs233_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs234_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs235_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs236_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs237_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs238_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs239_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs240_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrs241_out packet_clansettings_delta_v1_out
#define packet_clansettings_delta_osrsm241_out packet_clansettings_delta_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_clansettings_delta_out[];
extern const int rsprot_clansettings_delta_out_count;

#endif
