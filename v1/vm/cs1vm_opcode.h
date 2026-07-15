#ifndef CS1VM_OPCODE_H
#define CS1VM_OPCODE_H

/** CS1 interface-script opcodes (dat1 embedded scripts). See docs/cs1vm.md. */
#define CS1VM_OP_END 0
#define CS1VM_OP_STAT_LEVEL 1
#define CS1VM_OP_STAT_BASE_LEVEL 2
#define CS1VM_OP_STAT_XP 3
#define CS1VM_OP_INV_COUNT 4
#define CS1VM_OP_PUSH_VARP 5
#define CS1VM_OP_STAT_XP_REMAINING 6
#define CS1VM_OP_VARP_SCALE 7
#define CS1VM_OP_COMBAT_LEVEL 8
#define CS1VM_OP_TOTAL_LEVEL 9
#define CS1VM_OP_INV_CONTAINS 10
#define CS1VM_OP_RUNENERGY 11
#define CS1VM_OP_RUNWEIGHT 12
#define CS1VM_OP_TEST_BIT 13
#define CS1VM_OP_PUSH_VARBIT 14
#define CS1VM_OP_SUBTRACT 15
#define CS1VM_OP_DIVIDE 16
#define CS1VM_OP_MULTIPLY 17
#define CS1VM_OP_COORD_X 18
#define CS1VM_OP_COORD_Z 19
#define CS1VM_OP_PUSH_CONSTANT 20

/** inv_contains returns this sentinel when the object is present (Client.ts). */
#define CS1VM_INV_CONTAINS_PRESENT 999999999

#endif
