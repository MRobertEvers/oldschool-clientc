#ifndef RSPROT_PACKET_HISCORE_REPLY_H
#define RSPROT_PACKET_HISCORE_REPLY_H

/*
 * HISCORE_REPLY -- server -> client.
 *
 * Hand-written from RSProx (HiscoreReplyDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/HiscoreReplyDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder branches on the response type, with counted lists in the success arm, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_HISCORE_REPLY_MAX_STATS 255
#define MSG_HISCORE_REPLY_MAX_ACTIVITIES 512

typedef struct MsgHiscoreReplyResult {
	int32_t id;
	int32_t rank;
	int32_t result;
} MsgHiscoreReplyResult;

typedef struct MsgHiscoreReply {
	int32_t request_id;
	int32_t response_type; /* 0 success, 1 failure */
	/* response_type 0 */
	int32_t version;
	int32_t stat_count;
	MsgHiscoreReplyResult stats[MSG_HISCORE_REPLY_MAX_STATS];
	int32_t overall_rank;
	int64_t overall_experience;
	int32_t activity_count;
	MsgHiscoreReplyResult activities[MSG_HISCORE_REPLY_MAX_ACTIVITIES];
	/* response_type 1 */
	const char *reason;
} MsgHiscoreReply;

/* v1 -- revs 222-241. */
void packet_hiscore_reply_v1_out(RsprotExec *x, MsgHiscoreReply *m);

#define packet_hiscore_reply_osrs222_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs223_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs224_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs225_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs226_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs227_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs228_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs229_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs230_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs231_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs232_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs233_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs234_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs235_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs236_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs237_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs238_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs239_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs240_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrs241_out packet_hiscore_reply_v1_out
#define packet_hiscore_reply_osrsm241_out packet_hiscore_reply_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_hiscore_reply_out[];
extern const int rsprot_hiscore_reply_out_count;

#endif
