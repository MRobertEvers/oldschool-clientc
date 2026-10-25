#ifndef RSPROT_PACKET_REFLECTION_CHECKER_H
#define RSPROT_PACKET_REFLECTION_CHECKER_H

/*
 * REFLECTION_CHECKER -- server -> client.
 *
 * Hand-written from RSProx (ReflectionCheckerDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/ReflectionCheckerDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops over checks that each branch on an opcode, two of them with nested lists, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

#define MSG_REFLECTION_CHECKER_MAX_CHECKS 32
#define MSG_REFLECTION_CHECKER_MAX_PARAMS 16

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

/* v100 -- revs 227-241. */
void packet_reflection_checker_v100_out(RsprotExec *x, MsgReflectionChecker *m);

#define packet_reflection_checker_rev227_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev228_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev229_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev230_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev231_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev232_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev233_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev234_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev235_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev236_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev237_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev238_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev239_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev240_out packet_reflection_checker_v100_out
#define packet_reflection_checker_rev241_out packet_reflection_checker_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_reflection_checker_out[];
extern const int rsprot_reflection_checker_out_count;

#endif
