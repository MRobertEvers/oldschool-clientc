#ifndef RSPROT_PACKET_HISCORE_REPLY_H
#define RSPROT_PACKET_HISCORE_REPLY_H

/*
 * HISCORE_REPLY -- server -> client.
 *
 * Hand-written from RSProx (HiscoreReplyDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/HiscoreReplyDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder branches on the response type, with counted lists in the success arm, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
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

/* v100 -- revs 227-241. */
void packet_hiscore_reply_v100_out(RsprotExec *x, MsgHiscoreReply *m);

#define packet_hiscore_reply_rev227_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev228_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev229_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev230_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev231_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev232_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev233_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev234_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev235_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev236_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev237_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev238_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev239_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev240_out packet_hiscore_reply_v100_out
#define packet_hiscore_reply_rev241_out packet_hiscore_reply_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_hiscore_reply_out[];
extern const int rsprot_hiscore_reply_out_count;

#endif
