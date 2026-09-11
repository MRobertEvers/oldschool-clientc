/* Generated opcode-group metadata must match the rev-239 dispatch boundaries. */

#include "cs2vm2/cs2_opcode_meta.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK_GROUP(opcode, expected)                                                           \
    do                                                                                           \
    {                                                                                            \
        enum CS2_OpcodeGroup actual = CS2_OpcodeGroupOf(opcode);                                \
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
