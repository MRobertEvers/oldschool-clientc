#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2_host.h"

#include <stdio.h>
#include <string.h>

/* Every manifest row is a wire-level command: its OldSchool name and wire id.
 * The discriminators are NOT the wire ids, and not the canonical instructions:
 * they are dense, 0..COUNT-1 in row order (cs2vm2_host.h), and related to the
 * instructions only through CS2VM_HostRequestKindOf /
 * CS2VM_HostRequestInstruction and to the wire ids only through
 * CS2VM_HostRequestWireOpcode, checked below. */
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields)                                          \
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
    int wire;
    int request_kind;
};

static struct HostRequestKindEntry const HOST_REQUEST_KINDS[] = {
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    { #name, (int)(opcode), (int)CS2VM_HOST_REQUEST_##name },
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
        enum CS2VM_HostRequestKind const kind = (enum CS2VM_HostRequestKind)entry->request_kind;
        if( CS2VM_HostRequestWireOpcode(kind) != entry->wire )
        {
            fprintf(
                stderr,
                "FAIL: %s CS2VM_HostRequestWireOpcode says %d, the row says %d\n",
                entry->name,
                CS2VM_HostRequestWireOpcode(kind),
                entry->wire);
            failures++;
        }
        /* The instruction a native script asks the kind with is the row's
         * command: the same wire id (its name may be the newest version of a
         * versioned family, or the alias the row spells). */
        int const ins = CS2VM_HostRequestInstruction(kind);
        char const* ins_name = CS2_OpCode_String(ins);
        if( ins < 0 || ins >= CS2_OP_PLAIN_COUNT || cs2_opcode_meta_lookup(ins)->wire != entry->wire )
        {
            fprintf(stderr, "FAIL: %s asked by instruction %d %s\n", entry->name, ins, ins_name);
            failures++;
        }
        if( (int)CS2VM_HostRequestKindOf(ins) != entry->request_kind )
        {
            fprintf(
                stderr,
                "FAIL: %s CS2VM_HostRequestKindOf(%s) says %d, the kind is %d\n",
                entry->name,
                ins_name,
                (int)CS2VM_HostRequestKindOf(ins),
                entry->request_kind);
            failures++;
        }
        if( i > 0 && HOST_REQUEST_KINDS[i - 1].wire >= entry->wire )
        {
            fprintf(
                stderr,
                "FAIL: %s (%d) must follow %s (%d) in strict wire order\n",
                entry->name,
                entry->wire,
                HOST_REQUEST_KINDS[i - 1].name,
                HOST_REQUEST_KINDS[i - 1].wire);
            failures++;
        }
    }

    /* Every version of a family asks its command's kind. */
    if( CS2VM_HostRequestKindOf(CS2_OP_CC_CREATE_V1) != CS2VM_HOST_REQUEST_CC_CREATE ||
        CS2VM_HostRequestKindOf(CS2_OP_CC_CREATE_V2) != CS2VM_HOST_REQUEST_CC_CREATE )
    {
        fprintf(stderr, "FAIL: CC_CREATE_V1 and _V2 must both ask CC_CREATE\n");
        failures++;
    }

    if( failures )
    {
        fprintf(stderr, "host request kind contract: %d failure(s)\n", failures);
        return 1;
    }

    printf(
        "host request kind contract: %d exact, unique kinds, dense in wire order, the three maps agree\n",
        HOST_REQUEST_MANIFEST_COUNT);
    return 0;
}
