#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2_host.h"

#include <stdio.h>

/* Every manifest suffix must name the same canonical CS2 opcode, and every
 * public request discriminator must retain that opcode's value. Token-pasting
 * here deliberately makes a renamed or family-level request fail to compile. */
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields)                                          \
    _Static_assert(                                                                             \
        (int)CS2_OP_##name == (int)(opcode),                                                     \
        "host request/opcode name mismatch: " #name);                                           \
    _Static_assert(                                                                             \
        (int)CS2VM_HOST_REQUEST_##name == (int)CS2_OP_##name,                                   \
        "host request/opcode value mismatch: " #name);                                         \
    _Static_assert(                                                                             \
        _Generic(                                                                               \
            &((struct CS2VM_HostRequest*)0)->u.name,                                             \
            struct CS2VM_HostRequest_##name*: 1,                                                \
            default: 0),                                                                        \
        "host request struct tag/arm mismatch: " #name);
#include "cs2vm2/cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND

#if defined(__GNUC__) || defined(__clang__)
_Static_assert(
    !__builtin_types_compatible_p(
        struct CS2VM_HostRequest_CC_INPUT_SETCURSORWIDTH,
        struct CS2VM_HostRequest_IF_INPUT_SETCURSORWIDTH),
    "CC and IF input setters must have distinct request struct types");
#endif

enum
{
    HOST_REQUEST_MANIFEST_COUNT = 0
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) +1
#include "cs2vm2/cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
};

_Static_assert(
    HOST_REQUEST_MANIFEST_COUNT == 639,
    "the CS2VM host-request manifest must contain all 639 host opcodes");

struct HostRequestKindEntry
{
    char const* name;
    int opcode;
    int request_kind;
};

static struct HostRequestKindEntry const HOST_REQUEST_KINDS[] = {
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    { #name, (int)CS2_OP_##name, (int)CS2VM_HOST_REQUEST_##name },
#include "cs2vm2/cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
};

int
main(void)
{
    int failures = 0;

    for( int i = 0; i < HOST_REQUEST_MANIFEST_COUNT; i++ )
    {
        struct HostRequestKindEntry const* entry = &HOST_REQUEST_KINDS[i];

        if( entry->request_kind != entry->opcode )
        {
            fprintf(
                stderr,
                "FAIL: %s request kind %d does not equal opcode %d\n",
                entry->name,
                entry->request_kind,
                entry->opcode);
            failures++;
        }
        if( i > 0 && HOST_REQUEST_KINDS[i - 1].opcode >= entry->opcode )
        {
            fprintf(
                stderr,
                "FAIL: %s (%d) must follow %s (%d) in strict opcode order\n",
                entry->name,
                entry->opcode,
                HOST_REQUEST_KINDS[i - 1].name,
                HOST_REQUEST_KINDS[i - 1].opcode);
            failures++;
        }
    }

    if( failures )
    {
        fprintf(stderr, "host request kind contract: %d failure(s)\n", failures);
        return 1;
    }

    printf(
        "host request kind contract: %d exact, unique kinds in opcode order\n",
        HOST_REQUEST_MANIFEST_COUNT);
    return 0;
}
