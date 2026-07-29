/*
 * GENERATED FILE — do not edit.
 *
 * The ServerScript opcodes this server implements, derived from the
 * `case SS_OP_*:` labels in the sources that dispatch them. Regenerate with:
 *
 *     python3 net/mock/gen_opcode_coverage.py
 *
 * `make -C src test-mock230-coverage` fails when this file is stale.
 *
 * Coverage by layer:
 *     63  VM core
 *     92  host commands
 *    155  total, of 396 declared opcodes
 */

#ifndef SRC_NET_MOCK_MOCK230_OPCODE_COVERAGE_GEN_H
#define SRC_NET_MOCK_MOCK230_OPCODE_COVERAGE_GEN_H

#include <stdint.h>

#define MOCK230_OPCODE_COVERAGE_COUNT 155
#define MOCK230_OPCODE_DECLARED_COUNT 396

/*
 * One past the highest opcode *value*, which is nothing like the number of
 * opcodes: RuneScript numbers them sparsely and the host commands start at
 * 4000, so 396 declared opcodes span values up to 10003. Anything sizing an
 * array by opcode wants this, not the count — using the count silently
 * treats every real opcode as out of range.
 */
#define MOCK230_OPCODE_VALUE_LIMIT 10004

/* Ascending, so a lookup can binary-search. */
static const uint16_t MOCK230_OPCODE_COVERAGE[MOCK230_OPCODE_COVERAGE_COUNT] = {
    0, /* SS_OP_PUSH_CONSTANT_INT (VM core) */
    1, /* SS_OP_PUSH_VARP (host commands) */
    2, /* SS_OP_POP_VARP (host commands) */
    3, /* SS_OP_PUSH_CONSTANT_STRING (VM core) */
    6, /* SS_OP_BRANCH (VM core) */
    7, /* SS_OP_BRANCH_NOT (VM core) */
    8, /* SS_OP_BRANCH_EQUALS (VM core) */
    9, /* SS_OP_BRANCH_LESS_THAN (VM core) */
    10, /* SS_OP_BRANCH_GREATER_THAN (VM core) */
    21, /* SS_OP_RETURN (VM core) */
    22, /* SS_OP_GOSUB (VM core) */
    23, /* SS_OP_JUMP (VM core) */
    24, /* SS_OP_SWITCH (VM core) */
    25, /* SS_OP_PUSH_VARBIT (host commands) */
    27, /* SS_OP_POP_VARBIT (host commands) */
    31, /* SS_OP_BRANCH_LESS_THAN_OR_EQUALS (VM core) */
    32, /* SS_OP_BRANCH_GREATER_THAN_OR_EQUALS (VM core) */
    33, /* SS_OP_PUSH_INT_LOCAL (VM core) */
    34, /* SS_OP_POP_INT_LOCAL (VM core) */
    35, /* SS_OP_PUSH_STRING_LOCAL (VM core) */
    36, /* SS_OP_POP_STRING_LOCAL (VM core) */
    37, /* SS_OP_JOIN_STRING (VM core) */
    38, /* SS_OP_POP_INT_DISCARD (VM core) */
    39, /* SS_OP_POP_STRING_DISCARD (VM core) */
    40, /* SS_OP_GOSUB_WITH_PARAMS (VM core) */
    41, /* SS_OP_JUMP_WITH_PARAMS (VM core) */
    44, /* SS_OP_DEFINE_ARRAY (VM core) */
    45, /* SS_OP_PUSH_ARRAY_INT (VM core) */
    46, /* SS_OP_POP_ARRAY_INT (VM core) */
    1000, /* SS_OP_COORDX (host commands) */
    1001, /* SS_OP_COORDY (host commands) */
    1002, /* SS_OP_COORDZ (host commands) */
    1003, /* SS_OP_DISTANCE (host commands) */
    1008, /* SS_OP_MAP_CLOCK (host commands) */
    1013, /* SS_OP_MAP_MEMBERS (host commands) */
    1016, /* SS_OP_MOVECOORD (host commands) */
    1021, /* SS_OP_WORLD_DELAY (host commands) */
    2002, /* SS_OP_ANIM (host commands) */
    2013, /* SS_OP_CLEARTIMER (host commands) */
    2014, /* SS_OP_COORD (host commands) */
    2015, /* SS_OP_DAMAGE (host commands) */
    2016, /* SS_OP_DISPLAYNAME (host commands) */
    2017, /* SS_OP_FACESQUARE (host commands) */
    2026, /* SS_OP_HEALENERGY (host commands) */
    2033, /* SS_OP_IF_CLOSE (host commands) */
    2034, /* SS_OP_IF_OPENCHAT (host commands) */
    2035, /* SS_OP_IF_OPENMAIN_SIDE (host commands) */
    2036, /* SS_OP_IF_OPENMAIN (host commands) */
    2039, /* SS_OP_IF_SETANIM (host commands) */
    2041, /* SS_OP_IF_SETHIDE (host commands) */
    2043, /* SS_OP_IF_SETNPCHEAD (host commands) */
    2045, /* SS_OP_IF_SETPLAYERHEAD (host commands) */
    2047, /* SS_OP_IF_ADDRESUMEBUTTON (host commands) */
    2051, /* SS_OP_IF_SETTEXT (host commands) */
    2052, /* SS_OP_LAST_COM (host commands) */
    2053, /* SS_OP_LAST_INT (host commands) */
    2054, /* SS_OP_LAST_ITEM (host commands) */
    2056, /* SS_OP_LAST_SLOT (host commands) */
    2057, /* SS_OP_LAST_TARGETSLOT (host commands) */
    2063, /* SS_OP_MES (host commands) */
    2069, /* SS_OP_P_ARRIVEDELAY (host commands) */
    2071, /* SS_OP_P_COUNTDIALOG (host commands) */
    2072, /* SS_OP_P_DELAY (host commands) */
    2079, /* SS_OP_P_OPNPC (host commands) */
    2084, /* SS_OP_P_PAUSEBUTTON (host commands) */
    2087, /* SS_OP_P_STOPACTION (host commands) */
    2088, /* SS_OP_P_TELEJUMP (host commands) */
    2089, /* SS_OP_P_TELEPORT (host commands) */
    2093, /* SS_OP_QUEUE (host commands) */
    2098, /* SS_OP_SAY (host commands) */
    2104, /* SS_OP_SETTIMER (host commands) */
    2105, /* SS_OP_SOFTTIMER (host commands) */
    2106, /* SS_OP_SOUND_SYNTH (host commands) */
    2107, /* SS_OP_SPOTANIM_PL (host commands) */
    2110, /* SS_OP_STAT_ADVANCE (host commands) */
    2111, /* SS_OP_STAT_BASE (host commands) */
    2114, /* SS_OP_STAT_HEAL (host commands) */
    2115, /* SS_OP_STAT_RANDOM (host commands) */
    2118, /* SS_OP_STAT (host commands) */
    2125, /* SS_OP_UID (host commands) */
    2501, /* SS_OP_NPC_ANIM (host commands) */
    2503, /* SS_OP_NPC_ATTACKRANGE (host commands) */
    2507, /* SS_OP_NPC_CHANGETYPE (host commands) */
    2508, /* SS_OP_NPC_COORD (host commands) */
    2509, /* SS_OP_NPC_DAMAGE (host commands) */
    2511, /* SS_OP_NPC_DELAY (host commands) */
    2512, /* SS_OP_NPC_FACESQUARE (host commands) */
    2519, /* SS_OP_NPC_FINDHERO (host commands) */
    2528, /* SS_OP_NPC_NAME (host commands) */
    2529, /* SS_OP_NPC_PARAM (host commands) */
    2532, /* SS_OP_NPC_SAY (host commands) */
    2542, /* SS_OP_NPC_TYPE (host commands) */
    2547, /* SS_OP_SPOTANIM_NPC (host commands) */
    3500, /* SS_OP_OBJ_ADD (host commands) */
    4200, /* SS_OP_OC_CATEGORY (host commands) */
    4201, /* SS_OP_OC_CERT (host commands) */
    4207, /* SS_OP_OC_NAME (host commands) */
    4210, /* SS_OP_OC_STACKABLE (host commands) */
    4212, /* SS_OP_OC_UNCERT (host commands) */
    4302, /* SS_OP_INV_ADD (host commands) */
    4305, /* SS_OP_INV_CLEAR (host commands) */
    4307, /* SS_OP_INV_DEL (host commands) */
    4308, /* SS_OP_INV_DELSLOT (host commands) */
    4313, /* SS_OP_INV_FREESPACE (host commands) */
    4314, /* SS_OP_INV_GETNUM (host commands) */
    4315, /* SS_OP_INV_GETOBJ (host commands) */
    4316, /* SS_OP_INV_ITEMSPACE (host commands) */
    4317, /* SS_OP_INV_ITEMSPACE2 (host commands) */
    4319, /* SS_OP_INV_MOVEITEM_CERT (host commands) */
    4320, /* SS_OP_INV_MOVEITEM_UNCERT (host commands) */
    4321, /* SS_OP_INV_MOVEITEM (host commands) */
    4322, /* SS_OP_INV_MOVETOSLOT (host commands) */
    4324, /* SS_OP_INV_SIZE (host commands) */
    4326, /* SS_OP_INV_STOPTRANSMIT (host commands) */
    4327, /* SS_OP_INV_TOTAL (host commands) */
    4331, /* SS_OP_INV_TRANSMIT (host commands) */
    4500, /* SS_OP_APPEND_NUM (VM core) */
    4501, /* SS_OP_APPEND (VM core) */
    4502, /* SS_OP_APPEND_SIGNNUM (VM core) */
    4503, /* SS_OP_LOWERCASE (VM core) */
    4505, /* SS_OP_TOSTRING (VM core) */
    4506, /* SS_OP_COMPARE (VM core) */
    4507, /* SS_OP_TEXT_SWITCH (VM core) */
    4508, /* SS_OP_APPEND_CHAR (VM core) */
    4509, /* SS_OP_STRING_LENGTH (VM core) */
    4510, /* SS_OP_SUBSTRING (VM core) */
    4511, /* SS_OP_STRING_INDEXOF_CHAR (VM core) */
    4512, /* SS_OP_STRING_INDEXOF_STRING (VM core) */
    4600, /* SS_OP_ADD (VM core) */
    4601, /* SS_OP_SUB (VM core) */
    4602, /* SS_OP_MULTIPLY (VM core) */
    4603, /* SS_OP_DIVIDE (VM core) */
    4604, /* SS_OP_RANDOM (VM core) */
    4605, /* SS_OP_RANDOMINC (VM core) */
    4606, /* SS_OP_INTERPOLATE (VM core) */
    4607, /* SS_OP_ADDPERCENT (VM core) */
    4608, /* SS_OP_SETBIT (VM core) */
    4609, /* SS_OP_CLEARBIT (VM core) */
    4610, /* SS_OP_TESTBIT (VM core) */
    4611, /* SS_OP_MODULO (VM core) */
    4612, /* SS_OP_POW (VM core) */
    4613, /* SS_OP_INVPOW (VM core) */
    4614, /* SS_OP_AND (VM core) */
    4615, /* SS_OP_OR (VM core) */
    4616, /* SS_OP_MIN (VM core) */
    4617, /* SS_OP_MAX (VM core) */
    4618, /* SS_OP_SCALE (VM core) */
    4619, /* SS_OP_BITCOUNT (VM core) */
    4620, /* SS_OP_TOGGLEBIT (VM core) */
    4621, /* SS_OP_SETBIT_RANGE (VM core) */
    4622, /* SS_OP_CLEARBIT_RANGE (VM core) */
    4623, /* SS_OP_GETBIT_RANGE (VM core) */
    4624, /* SS_OP_SETBIT_RANGE_TOINT (VM core) */
    4628, /* SS_OP_ABS (VM core) */
    10001, /* SS_OP_ERROR (host commands) */
};

#endif
