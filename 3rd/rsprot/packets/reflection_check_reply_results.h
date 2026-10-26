#ifndef RSPROT_PACKET_REFLECTION_CHECK_REPLY_RESULTS_H
#define RSPROT_PACKET_REFLECTION_CHECK_REPLY_RESULTS_H

/*
 * REFLECTION_CHECK_REPLY's results -- client -> server.
 *
 * The reply (reflection_check_reply.h) is an id and the rest of the payload;
 * what the rest holds only the checks the server sent with that id
 * (REFLECTION_CHECKER) say: one signed result byte per check, negative an
 * exception, else the check's answer -- a g4 for a field value or the
 * modifiers, nothing for a set, and for a method invocation a g8 (result 1)
 * or a string (result 2). A CRC32 of everything before it ends the payload.
 *
 * The caller fills `check_count` / `check_ops` (REFLECTION_CHECKER's
 * opcodes) before decoding. Hand-written from RSProx's
 * ReflectionCheckReplyDecoder; no generator writes this file.
 */

#include "../include/rsprot_exec.h"
#include "reflection_checker.h"

typedef struct MsgReflectionCheckResult {
	int32_t result;
	int32_t value;
	int64_t number;
	const char *text;
} MsgReflectionCheckResult;

typedef struct MsgReflectionCheckReplyResults {
	/* in */
	int32_t check_count;
	int32_t check_ops[MSG_REFLECTION_CHECKER_MAX_CHECKS];
	/* out */
	int32_t id;
	MsgReflectionCheckResult results[MSG_REFLECTION_CHECKER_MAX_CHECKS];
	/** What RSProx skips between the last result and the CRC. */
	const uint8_t *rest;
	int32_t rest_length;
	int32_t crc;
} MsgReflectionCheckReplyResults;

void packet_reflection_check_reply_results_in(RsprotExec *x, MsgReflectionCheckReplyResults *m);

#endif
