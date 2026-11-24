#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2_host.h"

#include <stdio.h>

/* Every manifest suffix must name the same canonical CS2 opcode. Token-pasting
 * here deliberately makes a renamed or family-level request fail to compile.
 * The discriminators are NOT the opcodes' values: they are dense, 0..COUNT-1
 * in row order (cs2vm2_host.h), and related to the opcodes only through
 * CS2VM_HostRequestOpcode / CS2VM_HostRequestKindOf, checked below. */
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields)                                          \
    _Static_assert(                                                                             \
        (int)CS2_OP_##name == (int)(opcode),                                                     \
        "host request/opcode name mismatch: " #name);                                           \
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
        struct CS2VM_HostRequest_CC_INPUT_SETSELECTION,
        struct CS2VM_HostRequest_IF_INPUT_SETSELECTION),
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
    HOST_REQUEST_MANIFEST_COUNT == 912,
    "the CS2VM host-request manifest must contain all 912 host opcodes");
_Static_assert(
    (int)CS2VM_HOST_REQUEST_COUNT == HOST_REQUEST_MANIFEST_COUNT,
    "the request kinds are exactly the manifest's rows");

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

        if( entry->request_kind != i )
        {
            fprintf(
                stderr,
                "FAIL: %s request kind %d is not its row %d (kinds are dense, in row order)\n",
                entry->name,
                entry->request_kind,
                i);
            failures++;
        }
        if( CS2VM_HostRequestOpcode((enum CS2VM_HostRequestKind)entry->request_kind) !=
            entry->opcode )
        {
            fprintf(
                stderr,
                "FAIL: %s CS2VM_HostRequestOpcode says %d, the opcode is %d\n",
                entry->name,
                CS2VM_HostRequestOpcode((enum CS2VM_HostRequestKind)entry->request_kind),
                entry->opcode);
            failures++;
        }
        if( (int)CS2VM_HostRequestKindOf(entry->opcode) != entry->request_kind )
        {
            fprintf(
                stderr,
                "FAIL: %s CS2VM_HostRequestKindOf(%d) says %d, the kind is %d\n",
                entry->name,
                entry->opcode,
                (int)CS2VM_HostRequestKindOf(entry->opcode),
                entry->request_kind);
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
        "host request kind contract: %d exact, unique kinds, dense in opcode order, both maps agree\n",
        HOST_REQUEST_MANIFEST_COUNT);
    return 0;
}
