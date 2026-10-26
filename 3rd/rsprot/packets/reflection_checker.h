#ifndef RSPROT_PACKET_REFLECTION_CHECKER_H
#define RSPROT_PACKET_REFLECTION_CHECKER_H

/*
 * REFLECTION_CHECKER -- server -> client.
 *
 * Hand-written from RSProx (ReflectionCheckerDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/ReflectionCheckerDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops over checks that each branch on an opcode, two of them with nested lists, , so it is
 * written by hand. Field names are RSProx's.
 *
 * Both counts are one byte on the wire. The parameter list takes the wire's
 * whole range -- Jagex sends 20 (RSProx index 1130, rev 237) where this was
 * once 16; the check list is bounded lower, because each check carries a
 * parameter list and 255 x 255 would make the struct megabytes. A payload past
 * either fails the codec rather than truncating.
 */

#include "../include/rsprot_exec.h"

#define MSG_REFLECTION_CHECKER_MAX_CHECKS 32
#define MSG_REFLECTION_CHECKER_MAX_PARAMS 255

/**
 * One check. `opcode`: 0 get field value, 1 set field value (`value`),
 * 2 get field modifiers, 3 invoke method, 4 get method modifiers. For 0-2
 * `member` is the field name; for 3-4 it is the method name, and the
 * parameter lists apply (`param_values` for 3 only).
 */
typedef struct MsgReflectionCheck {
	int32_t opcode;
	const char *class_name;
	const char *member;
	int32_t value;
	int32_t param_count;
	const char *param_classes[MSG_REFLECTION_CHECKER_MAX_PARAMS];
	const char *return_class;
	int32_t param_sizes[MSG_REFLECTION_CHECKER_MAX_PARAMS];
	const uint8_t *param_values[MSG_REFLECTION_CHECKER_MAX_PARAMS];
} MsgReflectionCheck;

typedef struct MsgReflectionChecker {
	int32_t checks_count;
	int32_t id;
	MsgReflectionCheck checks[MSG_REFLECTION_CHECKER_MAX_CHECKS];
} MsgReflectionChecker;

/* v1 -- revs 222-241. */
void packet_reflection_checker_v1_out(RsprotExec *x, MsgReflectionChecker *m);

#define packet_reflection_checker_osrs222_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs223_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs224_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs225_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs226_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs227_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs228_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs229_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs230_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs231_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs232_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs233_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs234_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs235_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs236_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs237_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs238_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs239_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs240_out packet_reflection_checker_v1_out
#define packet_reflection_checker_osrs241_out packet_reflection_checker_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_reflection_checker_out[];
extern const int rsprot_reflection_checker_out_count;

#endif
