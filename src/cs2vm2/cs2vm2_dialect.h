#ifndef CS2VM2_DIALECT_H
#define CS2VM2_DIALECT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What one era of CS2 means by the same bytes.
 *
 * The VM speaks one opcode numbering (cs2_opcode.h, OldSchool's) and one set
 * of handlers. A cache from another era is run by the same handlers, told by
 * its script's dialect where its meaning differs: a wire id that names
 * another opcode is renumbered at load (`remap`), and a command whose stack
 * shape or semantics changed reads a flag here at run time. One flag per
 * change, each switched at the revision the caches show it switching, so two
 * changes a revision apart are two flags rather than two more dialects.
 *
 * The engine picks one per cache profile (CS2_DialectForCache); a script
 * carries a pointer to it, so scripts from two caches can share a VM.
 */

struct CS2VM2_DialectRemap
{
    uint16_t wire;
    uint16_t canonical;
};

struct CS2VM2_Dialect
{
    char const* name;
    /* Wire ids this era spells differently, renumbered once at load. */
    struct CS2VM2_DialectRemap const* remap;
    int remap_count;
    /* CC_CREATE pops (parent, type, index, nested). Without it, three: RS2,
     * and OldSchool through 229 (script 3953 gains the push at 230). */
    bool cc_create_nested;
    /* An array is a handle in a string local of the frame that defined or
     * received it (the rev-239 deob keeps the table in the frame's object
     * locals and saves it across gosub). Without it, arrays are numbered and
     * shared by every frame of a run: OldSchool through 230, where script
     * 2255 sorts array 0 recursively with no array argument (231 passes it as
     * a string argument). */
    bool array_handles;
    /* The 634 client's commands where an id names a different command than
     * OldSchool's, and its CC_GETBLENDMODE. A handful of ids above 4000 carry
     * different stack signatures in the two eras -- 6506 pushes 4 ints + 3
     * strings at 634 and 4 + 2 at OldSchool 239, 4124 pops two ints at 634 and
     * none at OldSchool -- so an overlay table (g_cs2vm2_opcode_stack_rs2)
     * stands in for them rather than a private canonical id per divergence. */
    bool rs2_commands;
};

/* Numbered arrays a run shares when !array_handles (the classic client's five). */
#define CS2VM2_SHARED_ARRAYS 5

/* OldSchool from 231: what the VM implements natively. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS;
/* OldSchool 230: nested CC_CREATE, numbered arrays. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS_230;
/* OldSchool through 229: three-argument CC_CREATE, numbered arrays. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS_229;
/* RS2 on dat2, verified against the 634 client. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_RS2_634;

#endif
