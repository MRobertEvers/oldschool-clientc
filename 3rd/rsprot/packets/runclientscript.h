#ifndef RSPROT_PACKET_RUNCLIENTSCRIPT_H
#define RSPROT_PACKET_RUNCLIENTSCRIPT_H

/*
 * RUNCLIENTSCRIPT -- server -> client. Invoke a clientscript with typed args.
 *
 * HAND-WRITTEN. The generator parses this now (after `when` and the Kotlin
 * casts) and then emits C that does not compile, because the layout uses one
 * field two ways: `types` is written as a STRING and then SUBSCRIPTED,
 * `types[i]`, as the per-argument discriminator. Character access into a
 * transferred string is the missing feature; see REFUSE_CLASSES in
 * tools/rsprot_gen_codec.py.
 *
 * ## Three things about this layout that are easy to get wrong
 *
 * 1. The types string is written FIRST. That is what makes the packet
 *    decodable at all -- both the argument count and every argument's type are
 *    known before the first value is read. A layout that put the types last
 *    could not be decoded forward.
 *
 * 2. The arguments are written in REVERSE. `for (i in (length - 1) downTo 0)`.
 *    Reading them forward yields the right bytes for the wrong slots, and only
 *    when the types happen to be uniform does that go unnoticed.
 *
 * 3. The script id is LAST, after the arguments -- not first, where a reader
 *    scanning for something familiar tends to look for it.
 *
 * The type alphabet, from the Kotlin:
 *
 *     'W'  int array     -- pVarInt2 count, then that many pVarInt2s
 *     'X'  string array  -- pVarInt2 count, then that many pjstr
 *     's'  string        -- pjstr
 *     0xCF int64         -- p8   (Kotlin writes it as the char 'Ï')
 *     else int           -- p4
 */

#include "../include/rsprot_exec.h"

/** RSProt spells the 8-byte type as the character 'Ï'; in a Cp1252 payload
 *  that is one byte, 0xCF. Naming it avoids a non-ASCII literal in C source. */
#define MSG_RUNCLIENTSCRIPT_TYPE_LONG 0xCF

typedef struct MsgRunClientScriptArg
{
    /* Scalars: exactly one is meaningful, chosen by this argument's type
     * character in `types`. */
    int32_t ival;
    int64_t lval;
    const char *sval;
    /* Array forms ('W' and 'X'). */
    int32_t elem_count;
    int32_t *ints;
    const char **strs;
} MsgRunClientScriptArg;

typedef struct MsgRunClientScript
{
    /** One character per argument, and the length of this string IS the
     *  argument count. Transferred first. */
    const char *types;
    int32_t id;
    MsgRunClientScriptArg *args;
} MsgRunClientScript;

#define MSG_RUNCLIENTSCRIPT_MAX_ARGS 128
#define MSG_RUNCLIENTSCRIPT_MAX_ELEMS 512

/* v1 -- revs 221-239. */
void packet_runclientscript_v1_out(RsprotExec *x, MsgRunClientScript *m);

#define packet_runclientscript_rev221_out packet_runclientscript_v1_out
#define packet_runclientscript_rev230_out packet_runclientscript_v1_out
#define packet_runclientscript_rev239_out packet_runclientscript_v1_out

extern const RsprotVersionRange rsprot_runclientscript_out[];
extern const int rsprot_runclientscript_out_count;

#endif
