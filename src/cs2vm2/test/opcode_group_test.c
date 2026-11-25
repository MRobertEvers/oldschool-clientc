/* Generated opcode-group metadata must match the rev-239 dispatch boundaries
 * (by wire id), and the canonical numbering must keep each group one range;
 * the FAST range is exactly the instructions the loop runs itself. */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2_jit_internal.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK_GROUP(opcode, expected)                                                           \
    do                                                                                           \
    {                                                                                            \
        enum CS2_OpcodeGroup actual = CS2_WireOpcodeGroupOf(opcode);                            \
        if( actual != (expected) )                                                               \
        {                                                                                        \
            fprintf(                                                                            \
                stderr,                                                                         \
                "opcode %d: group %s, expected %s\n",                                          \
                (opcode),                                                                       \
                CS2_OpcodeGroupName(actual),                                                     \
                CS2_OpcodeGroupName(expected));                                                  \
            failures++;                                                                         \
        }                                                                                        \
    } while( 0 )

int
main(void)
{
    CHECK_GROUP(99, CS2_OPCODE_GROUP_VM_CORE);
    CHECK_GROUP(100, CS2_OPCODE_GROUP_COMPONENT);
    CHECK_GROUP(999, CS2_OPCODE_GROUP_COMPONENT);
    CHECK_GROUP(1000, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_GROUP(1099, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_GROUP(1100, CS2_OPCODE_GROUP_COMPONENT_APPEARANCE);
    CHECK_GROUP(1999, CS2_OPCODE_GROUP_COMPONENT_ACTION);
    CHECK_GROUP(2000, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_GROUP(2099, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_GROUP(2100, CS2_OPCODE_GROUP_COMPONENT_APPEARANCE);
    CHECK_GROUP(2899, CS2_OPCODE_GROUP_IF_TARGET);
    CHECK_GROUP(2900, CS2_OPCODE_GROUP_COMPONENT_ACTION);
    CHECK_GROUP(2999, CS2_OPCODE_GROUP_COMPONENT_ACTION);
    CHECK_GROUP(3000, CS2_OPCODE_GROUP_CLIENT);
    CHECK_GROUP(3199, CS2_OPCODE_GROUP_CLIENT);
    CHECK_GROUP(3200, CS2_OPCODE_GROUP_AUDIO_OPTIONS);
    CHECK_GROUP(4299, CS2_OPCODE_GROUP_OBJ);
    CHECK_GROUP(4300, CS2_OPCODE_GROUP_CHAT);
    CHECK_GROUP(5099, CS2_OPCODE_GROUP_CHAT);
    CHECK_GROUP(5100, CS2_OPCODE_GROUP_WINDOW);
    CHECK_GROUP(6299, CS2_OPCODE_GROUP_VIEWPORT);
    CHECK_GROUP(6300, CS2_OPCODE_GROUP_WORLD);
    CHECK_GROUP(6599, CS2_OPCODE_GROUP_WORLD);
    CHECK_GROUP(6600, CS2_OPCODE_GROUP_WORLDMAP);
    CHECK_GROUP(8099, CS2_OPCODE_GROUP_ARRAY);
    CHECK_GROUP(8100, CS2_OPCODE_GROUP_TYPED_DATA);
    CHECK_GROUP(8599, CS2_OPCODE_GROUP_TYPED_DATA);
    CHECK_GROUP(8600, CS2_OPCODE_GROUP_UNKNOWN);
    CHECK_GROUP(12999, CS2_OPCODE_GROUP_UNKNOWN);
    CHECK_GROUP(13000, CS2_OPCODE_GROUP_OP_COUNT);
    CHECK_GROUP(13999, CS2_OPCODE_GROUP_OP_COUNT);
    CHECK_GROUP(14000, CS2_OPCODE_GROUP_UNKNOWN);

    /* Canonical instructions: an OldSchool command's group is its wire id's,
     * the RS2 634 section and UNDEFINED_WIRE_ID are groups of their own, and
     * each group is the range its generated BEGIN/END constants name -- the
     * property every range test on the dense numbering (the wasm split of
     * CS2VM2_RunOp) relies on. */
#define CANONICAL_ONLY(meta_)                                                                      \
    ((meta_)->group == CS2_OPCODE_GROUP_RS2_634 || (meta_)->group == CS2_OPCODE_GROUP_UNDEFINED)
    for( int op = 0; op < CS2_OP_PLAIN_COUNT; op++ )
    {
        struct CS2_OpcodeMeta const* meta = cs2_opcode_meta_lookup(op);
        enum CS2_OpcodeGroup const want = CANONICAL_ONLY(meta) ? meta->group : CS2_WireOpcodeGroupOf(meta->wire);
        if( CS2_OpcodeGroupOf(op) != want || want == CS2_OPCODE_GROUP_UNKNOWN )
        {
            fprintf(stderr, "instruction %d %s: group %s, wire %d says %s\n", op, meta->name,
                CS2_OpcodeGroupName(CS2_OpcodeGroupOf(op)), meta->wire, CS2_OpcodeGroupName(want));
            failures++;
        }
        /* Wire order within each of the FAST and GENERAL ranges. */
        if( op > 0 && op != CS2_OP_FAST_END && !CANONICAL_ONLY(meta) &&
            !CANONICAL_ONLY(cs2_opcode_meta_lookup(op - 1)) && cs2_opcode_meta_lookup(op - 1)->wire > meta->wire )
        {
            fprintf(stderr, "instruction %d %s is out of wire order\n", op, meta->name);
            failures++;
        }
    }
#define CHECK_RANGE(begin, end, group)                                                             \
    for( int op = 0; op < CS2_OP_PLAIN_COUNT; op++ )                                               \
    {                                                                                              \
        int const inside = op >= (begin) && op < (end);                                            \
        int const in_group = CS2_OpcodeGroupOf(op) == (group);                                     \
        if( inside && !in_group )                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s range holds %s\n", #group, CS2_OpCode_String(op));                 \
            failures++;                                                                            \
        }                                                                                          \
    }
    CHECK_RANGE(CS2_OPS_VM_CORE_BEGIN, CS2_OPS_VM_CORE_END, CS2_OPCODE_GROUP_VM_CORE);
    CHECK_RANGE(CS2_OPS_COMPONENT_LAYOUT_CC_BEGIN, CS2_OPS_COMPONENT_LAYOUT_CC_END, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_RANGE(CS2_OPS_COMPONENT_LAYOUT_IF_BEGIN, CS2_OPS_COMPONENT_LAYOUT_IF_END, CS2_OPCODE_GROUP_COMPONENT_LAYOUT);
    CHECK_RANGE(CS2_OPS_CLIENT_BEGIN, CS2_OPS_CLIENT_END, CS2_OPCODE_GROUP_CLIENT);
    CHECK_RANGE(CS2_OPS_CLIENT_STATE_BEGIN, CS2_OPS_CLIENT_STATE_END, CS2_OPCODE_GROUP_CLIENT_STATE);
    CHECK_RANGE(CS2_OPS_STRING_BEGIN, CS2_OPS_STRING_END, CS2_OPCODE_GROUP_STRING);
    CHECK_RANGE(CS2_OPS_RS2_634_BEGIN, CS2_OPS_RS2_634_END, CS2_OPCODE_GROUP_RS2_634);
    CHECK_RANGE(CS2_OPS_UNDEFINED_BEGIN, CS2_OPS_UNDEFINED_END, CS2_OPCODE_GROUP_UNDEFINED);
#undef CHECK_RANGE
#undef CANONICAL_ONLY
    if( CS2_OpcodeGroupOf(CS2_OPS_CLIENT_STATE_BEGIN) != CS2_OPCODE_GROUP_CLIENT_STATE ||
        CS2_OpcodeGroupOf(CS2_OPS_CLIENT_STATE_END - 1) != CS2_OPCODE_GROUP_CLIENT_STATE ||
        CS2_OpcodeGroupOf(CS2_OPS_CLIENT_STATE_END) == CS2_OPCODE_GROUP_CLIENT_STATE ||
        CS2_OpcodeGroupOf(CS2_OPS_CLIENT_STATE_BEGIN - 1) == CS2_OPCODE_GROUP_CLIENT_STATE )
    {
        fprintf(stderr, "client-state range is not exactly its group\n");
        failures++;
    }

    /* The FAST range is the loop's own cases: exactly the instructions with a
     * plain hot code (g_cs2vm2_hot_op, one per case of cs2vm2_hot_cases.inc;
     * tools/cs2_gen_opcodes/check_fast_arms.py ties the cases to the list). */
    for( int op = 0; op < CS2_OP_PLAIN_COUNT; op++ )
    {
        int const fast = op < CS2_OP_FAST_END;
        int const hot = g_cs2vm2_hot_op[op] != CS2VM2_HOT_NONE;
        if( fast != hot )
        {
            fprintf(stderr, "%s: %s, but %s a case of the loop\n", CS2_OpCode_String(op),
                fast ? "FAST" : "GENERAL", hot ? "has" : "has no");
            failures++;
        }
    }

    if( strcmp(CS2_OpcodeGroupName(CS2_OPCODE_GROUP_WORLDMAP), "worldmap") != 0 )
    {
        fprintf(stderr, "worldmap group name mismatch\n");
        failures++;
    }
    if( failures )
        return 1;
    puts("CS2 opcode groups match the rev-239 dispatch. passed");
    return 0;
}
