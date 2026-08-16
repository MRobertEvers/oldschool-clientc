#ifndef CS1_OPCODE_H
#define CS1_OPCODE_H

/*
 * ClientScript 1 opcodes — the IF1 component value-script dialect.
 * Reference: Client-TS Client.getIfVar.
 *
 * A script is a flat int stream: an opcode, then that opcode's inline operands,
 * repeated until CS1_OP_RETURN. Ops 15/16/17 push no value; they set the
 * arithmetic applied to the *next* value (default add).
 */

#define CS1_OP_RETURN 0
#define CS1_OP_STAT_LEVEL 1
#define CS1_OP_STAT_BASE_LEVEL 2
#define CS1_OP_STAT_XP 3
/** operands: {component_id, obj_id} */
#define CS1_OP_INV_COUNT 4
#define CS1_OP_PUSH_VARP 5
#define CS1_OP_STAT_XP_REMAINING 6
/** varp scaled to a 0..100 percentage: raw * 100 / 46875 */
#define CS1_OP_PUSH_VARP_SCALED 7
#define CS1_OP_COMBAT_LEVEL 8
#define CS1_OP_TOTAL_LEVEL 9
/** operands: {component_id, obj_id}; present -> CS1VM_VALUE_INFINITY */
#define CS1_OP_INV_CONTAINS 10
#define CS1_OP_RUNENERGY 11
#define CS1_OP_RUNWEIGHT 12
/** operands: {varp_id, bit} -> 0 or 1 */
#define CS1_OP_VARP_TESTBIT 13
#define CS1_OP_PUSH_VARBIT 14
#define CS1_OP_ARITH_SUB 15
#define CS1_OP_ARITH_DIV 16
#define CS1_OP_ARITH_MUL 17
#define CS1_OP_COORD_X 18
#define CS1_OP_COORD_Z 19
/** operand: the literal to push */
#define CS1_OP_PUSH_CONSTANT 20

#endif /* CS1_OPCODE_H */
